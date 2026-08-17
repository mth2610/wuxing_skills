/* Guard for the 2D strip LUT's addressing math (Đợt G5).
 *
 * A strip LUT fails quietly. Off-by-one in the tile offset, a flipped green
 * axis, or filtering that reaches across a tile edge all produce a frame that
 * still looks like a frame — just with a colour cast nobody can attribute. So
 * the identity property is asserted numerically here rather than judged by eye:
 * the NEUTRAL strip pushed through the shader's own lookup must return its
 * input.
 *
 * This mirrors both halves — the generator in core/color_grade_lut.c and the
 * sampler in core/shaders/post_process.fs — so they cannot drift apart without
 * this failing. What it CANNOT validate: that the GPU's bilinear filter matches
 * the exact-lerp modelled here (it is specified to, within precision), and that
 * the texture is actually bound. Those need the real frame.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUT_SIZE   16
#define LUT_WIDTH  (LUT_SIZE * LUT_SIZE)
#define LUT_HEIGHT LUT_SIZE

static unsigned char g_strip[LUT_HEIGHT][LUT_WIDTH][3];

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (s == NULL) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *src, const char *needle, const char *msg)
{
    if (src != NULL && strstr(src, needle) != NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

/* Mirrors ColorGradeLut_BuildNeutralImage(). */
static void BuildNeutralStrip(void)
{
    const float denom = (float)(LUT_SIZE - 1);
    for (int slice = 0; slice < LUT_SIZE; ++slice)
        for (int g = 0; g < LUT_SIZE; ++g)
            for (int r = 0; r < LUT_SIZE; ++r) {
                int x = slice * LUT_SIZE + r;
                g_strip[g][x][0] = (unsigned char)((float)r * 255.0f / denom + 0.5f);
                g_strip[g][x][1] = (unsigned char)((float)g * 255.0f / denom + 0.5f);
                g_strip[g][x][2] = (unsigned char)((float)slice * 255.0f / denom + 0.5f);
            }
}

/* Bilinear tap in normalised UV, matching GL's texel-centre convention. */
static void SampleStrip(float u, float v, float outRgb[3])
{
    float x = u * (float)LUT_WIDTH - 0.5f;
    float y = v * (float)LUT_HEIGHT - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    for (int ch = 0; ch < 3; ++ch) {
        float acc = 0.0f;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                int sx = x0 + dx, sy = y0 + dy;
                if (sx < 0) sx = 0; if (sx >= LUT_WIDTH) sx = LUT_WIDTH - 1;
                if (sy < 0) sy = 0; if (sy >= LUT_HEIGHT) sy = LUT_HEIGHT - 1;
                float w = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
                acc += w * (float)g_strip[sy][sx][ch];
            }
        outRgb[ch] = acc / 255.0f;
    }
}

/* Mirrors ApplyColorGradeLut() in core/shaders/post_process.fs. */
static void ApplyLut(const float in[3], float out[3])
{
    const float size = (float)LUT_SIZE;
    float c[3];
    for (int i = 0; i < 3; ++i)
        c[i] = in[i] < 0.0f ? 0.0f : (in[i] > 1.0f ? 1.0f : in[i]);

    float b = c[2] * (size - 1.0f);
    float slice0 = floorf(b);
    float slice1 = slice0 + 1.0f; if (slice1 > size - 1.0f) slice1 = size - 1.0f;
    float blend = b - slice0;

    float uInTile = 0.5f + c[0] * (size - 1.0f);
    float v = (0.5f + c[1] * (size - 1.0f)) / (float)LUT_HEIGHT;

    float s0[3], s1[3];
    SampleStrip((slice0 * size + uInTile) / (float)LUT_WIDTH, v, s0);
    SampleStrip((slice1 * size + uInTile) / (float)LUT_WIDTH, v, s1);
    for (int i = 0; i < 3; ++i) out[i] = s0[i] + (s1[i] - s0[i]) * blend;
}

int main(void)
{
    int failed = 0;
    BuildNeutralStrip();

    /* ---- Identity round-trip across the cube ----------------------------- */
    /* 8-bit quantisation caps accuracy at ~1/255; anything worse than half a
     * code value means the addressing is wrong, not that the format is coarse. */
    const float tolerance = 1.5f / 255.0f;
    float worst = 0.0f;
    float worstIn[3] = {0};
    for (int bi = 0; bi <= 20; ++bi)
        for (int gi = 0; gi <= 20; ++gi)
            for (int ri = 0; ri <= 20; ++ri) {
                float in[3] = {(float)ri / 20.0f, (float)gi / 20.0f, (float)bi / 20.0f};
                float out[3];
                ApplyLut(in, out);
                for (int ch = 0; ch < 3; ++ch) {
                    float err = fabsf(out[ch] - in[ch]);
                    if (err > worst) { worst = err; memcpy(worstIn, in, sizeof(in)); }
                }
            }
    if (worst > tolerance) {
        fprintf(stderr, "FAIL: neutral LUT is not identity — worst error %.5f "
                        "(%.4f allowed) at rgb(%.2f, %.2f, %.2f)\n",
                worst, tolerance, worstIn[0], worstIn[1], worstIn[2]);
        failed++;
    }

    /* ---- No bleed across a tile edge -------------------------------------
     * The failure this catches: red = 1.0 sampling into the NEXT blue slice,
     * which tints the brightest reds with the wrong blue. Pure red at the top
     * of a slice must stay pure. */
    for (int bi = 0; bi < LUT_SIZE; ++bi) {
        float in[3] = {1.0f, 0.0f, (float)bi / (float)(LUT_SIZE - 1)};
        float out[3];
        ApplyLut(in, out);
        if (fabsf(out[0] - 1.0f) > tolerance || fabsf(out[2] - in[2]) > tolerance) {
            fprintf(stderr, "FAIL: tile-edge bleed at slice %d — "
                            "red 1.0 returned r=%.4f b=%.4f (expected 1.0, %.4f)\n",
                    bi, out[0], out[2], in[2]);
            failed++;
            break;
        }
    }

    /* ---- The two mirrors have not drifted -------------------------------- */
    char *shader = ReadFile("core/shaders/post_process.fs");
    char *lutC = ReadFile("core/color_grade_lut.c");
    char *lutH = ReadFile("core/color_grade_lut.h");

    failed += Require(shader, "float uInTile = 0.5 + c.r * (size - 1.0);",
                      "shader must address red at texel centres inside the tile");
    failed += Require(shader, "float v = (0.5 + c.g * (size - 1.0)) * u_lutParams.y;",
                      "shader must address green at texel centres");
    failed += Require(shader, "mix(s0, s1, blend)",
                      "shader must interpolate the blue axis between two slices");
    failed += Require(lutC, "slice * LUT_SIZE + r, g,",
                      "generator must lay tiles out left-to-right by blue slice");
    failed += Require(lutH, "#define COLOR_GRADE_LUT_SIZE 16",
                      "the strip size is shared by generator, shader and this test");

    /* Applied after tone mapping: a LUT is display-referred, and feeding it HDR
     * would clamp every highlight into the top slice. */
    /* The marker tracks the tone-map CALL SITE, not the curve's name: the shipping
     * curve is now reached through toneMapScene(), which dispatches between the ACES
     * fit and the hue-preserving candidate. The invariant under test is unchanged —
     * a display-referred LUT must come after whichever curve ran. */
    const char *tone = shader ? strstr(shader, "sceneCol.rgb = toneMapScene") : NULL;
    const char *lutApply = shader ? strstr(shader, "ApplyColorGradeLut(sceneCol.rgb)") : NULL;
    if (tone == NULL || lutApply == NULL || lutApply < tone) {
        fprintf(stderr, "FAIL: the LUT must be applied AFTER tone mapping\n");
        failed++;
    }

    free(shader); free(lutC); free(lutH);
    if (failed != 0) { puts("colour grade LUT: FAIL"); return 1; }
    printf("PASS: neutral strip LUT is identity (worst error %.5f), no tile-edge "
           "bleed, applied post-tonemap\n", worst);
    return 0;
}

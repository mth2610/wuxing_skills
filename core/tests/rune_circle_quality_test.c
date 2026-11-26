/* Contract for VFX_ComposeRuneCircle (Cổ Ấn Vạn Pháp — 6 WebGL-derived seals).
 *
 * Tests arithmetic invariants (alpha body chroma preservation on bright
 * backgrounds, blackbody heat ramp monotonicity) and the presence of the
 * structural mechanisms required by the engine's Bright-Background VFX
 * Contract, terrain conforming, and WebGL 6-variant seal forge.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } Rgb;

static int g_failures = 0;

static void Check(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); g_failures++; }
}

static Rgb Additive(Rgb dst, Rgb src, float alpha)
{
    return (Rgb){dst.r + src.r * alpha,
                 dst.g + src.g * alpha,
                 dst.b + src.b * alpha};
}

static Rgb AlphaOver(Rgb dst, Rgb src, float alpha)
{
    return (Rgb){dst.r * (1.0f - alpha) + src.r * alpha,
                 dst.g * (1.0f - alpha) + src.g * alpha,
                 dst.b * (1.0f - alpha) + src.b * alpha};
}

static float Chroma(Rgb c)
{
    float hi = fmaxf(c.r, fmaxf(c.g, c.b));
    float lo = fminf(c.r, fminf(c.g, c.b));
    return hi > 0.0f ? (hi - lo) / hi : 0.0f;
}

static float Smoothstep(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float Luma(Rgb c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

static Rgb PhysicalHeatRamp(float h, Rgb cDeep, Rgb cMid, Rgb cCore)
{
    float s1 = Smoothstep(0.02f, 0.26f, h);
    Rgb col = { cDeep.r * s1, cDeep.g * s1, cDeep.b * s1 };
    float s2 = Smoothstep(0.24f, 0.66f, h);
    col.r = col.r + (cMid.r - col.r) * s2;
    col.g = col.g + (cMid.g - col.g) * s2;
    col.b = col.b + (cMid.b - col.b) * s2;
    Rgb hotGold = {
        cMid.r * 1.45f * 0.55f + cCore.r * 0.45f,
        cMid.g * 1.45f * 0.55f + cCore.g * 0.45f,
        cMid.b * 1.45f * 0.55f + cCore.b * 0.45f,
    };
    float s3 = Smoothstep(0.64f, 0.88f, h);
    col.r = col.r + (hotGold.r - col.r) * s3;
    col.g = col.g + (hotGold.g - col.g) * s3;
    col.b = col.b + (hotGold.b - col.b) * s3;
    float s4 = Smoothstep(0.88f, 1.24f, h);
    col.r = col.r + (cCore.r * 1.55f - col.r) * s4;
    col.g = col.g + (cCore.g * 1.55f - col.g) * s4;
    col.b = col.b + (cCore.b * 1.55f - col.b) * s4;
    return col;
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *text = (char *)malloc((size_t)size + 1u);
    if (text == NULL) { fclose(f); return NULL; }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    return text;
}

static int Has(const char *text, const char *needle)
{
    return text != NULL && strstr(text, needle) != NULL;
}

/* ── 1. Why pigment lives in an alpha BODY pass ──────────────────────────── */
static void Test_AlphaBodyPreservesChroma(void)
{
    const Rgb brightStone = {0.62f, 0.67f, 0.72f};
    const Rgb warmRune = {1.00f, 0.28f, 0.06f};

    Rgb additiveOnly = brightStone;
    additiveOnly = Additive(additiveOnly, warmRune, 0.32f);
    additiveOnly = Additive(additiveOnly, warmRune, 0.92f);
    additiveOnly = Additive(additiveOnly, warmRune, 1.00f);

    Rgb split = AlphaOver(brightStone, warmRune, 0.90f);
    split = Additive(split, warmRune, 0.18f);
    split = Additive(split, warmRune, 0.72f);

    Check(Chroma(split) >= Chroma(additiveOnly) + 0.12f,
          "an alpha body materially preserves rune chroma over a tall additive stack");
}

/* ── 2. Blackbody heat ramp monotonicity & zero floor ────────────────────── */
static void Test_PhysicalHeatRampMonotonic(void)
{
    const Rgb deep = {0.48f, 0.10f, 0.00f};
    const Rgb mid  = {1.00f, 0.55f, 0.10f};
    const Rgb core = {1.00f, 0.96f, 0.80f};

    Rgb z = PhysicalHeatRamp(0.0f, deep, mid, core);
    Check(Luma(z) < 1e-5f, "heat ramp is strictly black at heat = 0");

    float prev = 0.0f;
    for (int i = 1; i <= 12; i++)
    {
        float h = (float)i * 0.1f;
        float lum = Luma(PhysicalHeatRamp(h, deep, mid, core));
        Check(lum >= prev - 1e-5f, "heat ramp luminance is monotonically non-decreasing");
        prev = lum;
    }
    Check(prev > 1.2f, "heat ramp reaches HDR peak at high heat");
}

/* ── 3. Structural mechanisms in composition & shaders ───────────────────── */
static void Test_MechanismsArePresent(void)
{
    char *inl = ReadFile("core/composition/common/vc_rune_circle.inl");
    char *fs  = ReadFile("core/shaders/rune_circle.fs");
    char *vs  = ReadFile("core/shaders/rune_circle.vs");
    char *hdr = ReadFile("core/composition/visual_composer.h");

    Check(inl != NULL, "the composition exists");
    Check(fs != NULL && vs != NULL, "the rune circle shader pair exists");
    Check(hdr != NULL, "visual_composer.h exists");

    Check(!Has(inl, "DrawRibbonStripEx("),
          "the rune circle does not trace circles with ribbon geometry");

    /* Bright-background VFX contract: BODY alpha + EMISSION premultiplied */
    Check(Has(inl, "VFX_RENDER_PASS_BODY") && Has(inl, "VFX_SURFACE_ALPHA"),
          "a colour-preserving BODY pass is present");
    Check(Has(inl, "VFX_RENDER_PASS_EMISSION") && Has(inl, "VFX_SURFACE_PREMULTIPLIED"),
          "emission uses premultiplied blending in its own pass");
    Check(Has(fs, "VFX_ResolveBody") && Has(fs, "VFX_ResolvePremultiplied"),
          "shader resolves BODY and EMISSION via shared vfx_composite.glsl");
    Check(Has(fs, "VividPigment("),
          "shader preserves chroma across bright backgrounds via VividPigment");

    /* All 6 WebGL Ancient Seal variants */
    Check(Has(hdr, "VFX_RUNE_PHAN_THIEN") && Has(hdr, "VFX_RUNE_BAT_QUAI") &&
          Has(hdr, "VFX_RUNE_TRU_TIEN") && Has(hdr, "VFX_RUNE_HUYET_SAT") &&
          Has(hdr, "VFX_RUNE_U_MINH") && Has(hdr, "VFX_RUNE_TINH_TU"),
          "all 6 WebGL rune circle variants are declared in visual_composer.h");
    Check(Has(inl, "VFX_ComposeRuneCircleVariant") && Has(inl, "VFX_RuneCircleVariant_Name"),
          "variant compose and name functions are implemented");

    /* WebGL shader features: blackbody ramp, void cut, outer rim clip, 3 layers + embers */
    Check(Has(fs, "physicalHeatRamp("), "blackbody physicalHeatRamp is present");
    Check(Has(fs, "float rim =") && Has(fs, "voidCut"),
          "outer rim and pure void interior cut are enforced");
    Check(Has(inl, "Rune_SubmitEmbers"),
          "batched pinpoint orbital embers are present");

    /* Phase folding & ground conforming */
    Check(Has(inl, "Rune_WrapTau") && Has(inl, "Rune_Wrap01"),
          "rotating phases are folded into their period on the CPU");
    Check(Has(inl, "MapManager_SampleGroundSurfaceAt") &&
          Has(inl, "RUNE_Y_LIFT") &&
          Has(inl, "RUNE_MESH_CELLS") && Has(inl, "RUNE_H_SAMPLES"),
          "ground conforming uses safe ground sampler, lift, and tessellated drape");

    free(inl);
    free(fs);
    free(vs);
    free(hdr);
}

int main(void)
{
    printf("=== rune circle (Cổ Ấn Vạn Pháp): 6 variants, blackbody, bright-bg contract ===\n");
    Test_AlphaBodyPreservesChroma();
    Test_PhysicalHeatRampMonotonic();
    Test_MechanismsArePresent();
    if (g_failures == 0)
        puts("PASS: rune circle contract holds");
    printf("---- %d failures\n", g_failures);
    return g_failures != 0;
}

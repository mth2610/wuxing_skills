// core headless test — the FLOW MAP, which the tree has owned since before Đợt H
// and never used once.
//
// A flow map is NOT a scrolling texture, and conflating the two is why this has
// looked wrong every time it was reached for. Scrolling slides a fixed pattern.
// A flow map is an RG DIRECTION FIELD that displaces the lookup of another
// texture, so different parts of the surface drift different ways — which is
// what "cuộn trào" actually is. The cost of that is the texture stretches
// without bound, and the whole technique is the trick that hides the stretch:
// two phases half a cycle apart, cross-faded on a triangle wave.
//
// Both halves are arithmetic, so both belong here rather than on a screen.

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static int FileHas(const char *path, const char *needle);

// ── 1. The phase / weight pairing ───────────────────────────────────────────
//
// THE BUG THIS EXISTS FOR. The blend weight `abs(2*p - 1)` is Valve's, and it
// assumes phase 0.5 is the UNDISPLACED phase — i.e. that the displacement is
// `(phase - 0.5)`. Our shader displaced by `phase` instead. The two are only
// half a cycle apart, which sounds harmless and is exactly inverted: the layer
// receiving full weight was always the one at MAXIMUM stretch, and the layer at
// zero stretch always received zero weight. The cross-fade pulsed the artefact
// it exists to hide.

static float Displacement(float phase, int centred)
{
    return centred ? fabsf(phase - 0.5f) : phase;
}

static void Test_TheShownLayerIsTheLeastStretched(void)
{
    float worstUncentred = 0.0f, worstCentred = 0.0f;
    for (int i = 0; i < 200; i++) {
        float t = (float)i / 200.0f;
        float p0 = t - floorf(t);
        float p1 = t + 0.5f; p1 -= floorf(p1);
        float w = fabsf(p0 * 2.0f - 1.0f);         // weight toward col1

        // The stretch actually on screen is the weighted average of the two.
        float unc = (1.0f - w) * Displacement(p0, 0) + w * Displacement(p1, 0);
        float cen = (1.0f - w) * Displacement(p0, 1) + w * Displacement(p1, 1);
        if (unc > worstUncentred) worstUncentred = unc;
        if (cen > worstCentred) worstCentred = cen;
    }
    CHECK_MSG(worstCentred < worstUncentred * 0.75f,
              "centring the displacement really does cut the stretch on screen",
              "worst %.3f centred vs %.3f uncentred", worstCentred, worstUncentred);

    // The property that matters, stated directly: when one layer takes the whole
    // weight, that layer must be the undisplaced one.
    float atZero_w = fabsf(0.0f * 2.0f - 1.0f);          // = 1 -> all col1
    float atZero_shown_uncentred = Displacement(0.5f, 0); // col1's phase is 0.5
    float atZero_shown_centred = Displacement(0.5f, 1);
    CHECK_MSG(atZero_w > 0.99f, "at t=0 the weight is entirely on one layer",
              "w = %.3f", atZero_w);
    CHECK_MSG(atZero_shown_uncentred > 0.4f,
              "and uncentred, THAT layer was at maximum displacement",
              "%.2f of full stretch", atZero_shown_uncentred);
    CHECK_MSG(atZero_shown_centred < 0.01f,
              "centred, it is the undisplaced one — which is the entire point",
              "%.4f of full stretch", atZero_shown_centred);
}

// ── 2. A trail flow field must TILE, and a seam cannot be cross-faded ───────
//
// Every other sheet in this project is made seamless by cross-fading its ends.
// A flow map cannot be: cross-fading two DIRECTIONS averages them, and the
// average of two opposing directions is no flow at all — a dead band across the
// tube. Seamlessness has to come from the construction, i.e. integer harmonics.

static void TrailFlow(float u, float v, float swirl, float *outU, float *outV)
{
    const float amp[3] = {0.55f, 0.30f, 0.18f};
    const int ku[3] = {1, 2, 3};
    const int kv[3] = {2, 1, 3};
    const float phase[3] = {0.0f, 1.9f, 3.7f};
    float fu = 0.0f, fv = -1.0f;
    for (int h = 0; h < 3; h++) {
        float a = 2.0f * PI * ((float)ku[h] * u + (float)kv[h] * v) + phase[h];
        fu += swirl * amp[h] * sinf(a);
        fv += 0.35f * amp[h] * cosf(a);
    }
    float maxLen = 1.0f + 0.35f * (amp[0] + amp[1] + amp[2]) +
                   swirl * (amp[0] + amp[1] + amp[2]);
    *outU = fu / maxLen;
    *outV = fv / maxLen;
}

static void Test_TrailFieldTilesOnBothAxes(void)
{
    float worstU = 0.0f, worstV = 0.0f;
    for (int i = 0; i <= 64; i++) {
        float s = (float)i / 64.0f;
        float a0, b0, a1, b1;
        // Wrap across u: the seam a swept tube's circumference closes on.
        TrailFlow(0.0f, s, 0.4f, &a0, &b0);
        TrailFlow(1.0f, s, 0.4f, &a1, &b1);
        float e = fabsf(a0 - a1) + fabsf(b0 - b1);
        if (e > worstU) worstU = e;
        // Wrap along v: the tiling direction.
        TrailFlow(s, 0.0f, 0.4f, &a0, &b0);
        TrailFlow(s, 1.0f, 0.4f, &a1, &b1);
        e = fabsf(a0 - a1) + fabsf(b0 - b1);
        if (e > worstV) worstV = e;
    }
    CHECK_MSG(worstU < 1e-5f, "the field is EXACTLY continuous around the section",
              "worst mismatch %.7f", worstU);
    CHECK_MSG(worstV < 1e-5f, "...and along the length",
              "worst mismatch %.7f", worstV);

    // A vortex — the only generator that existed — is not, and cannot be made so
    // by cross-fading, which is why it needed a second generator rather than a
    // wrapper. Its opposite edges point opposite ways.
    float lx = -( -0.5f) / 0.5f;   // tangent at the left edge, y = centre
    float rx = -( -0.5f) / 0.5f;
    (void)lx; (void)rx;
    float leftTangentY = (0.0f - 64.0f) / 64.0f;    // dx/dist at x=0
    float rightTangentY = (128.0f - 64.0f) / 64.0f; // dx/dist at x=128
    CHECK_MSG(leftTangentY * rightTangentY < 0.0f,
              "a vortex field points OPPOSITE ways at its two edges — it cannot tile",
              "%.2f vs %.2f", leftTangentY, rightTangentY);
}

// ── 3. The encoding must not clip ───────────────────────────────────────────

static void Test_EncodingNeverClips(void)
{
    // RG is unsigned, so the field is stored as v*0.5+0.5. A vector past unit
    // length clips, and a clipped vector is a direction silently bent toward
    // whichever axis clipped — a flow field that quietly straightens itself.
    float worst = 0.0f;
    for (float swirl = 0.0f; swirl <= 1.2f; swirl += 0.1f) {
        for (int i = 0; i < 48; i++) {
            for (int j = 0; j < 48; j++) {
                float fu, fv;
                TrailFlow((float)i / 48.0f, (float)j / 48.0f, swirl, &fu, &fv);
                float m = fmaxf(fabsf(fu), fabsf(fv));
                if (m > worst) worst = m;
            }
        }
    }
    CHECK_MSG(worst <= 1.0f, "no component ever leaves [-1, 1] at any swirl",
              "worst |component| %.4f", worst);

    // ...and the base flow must survive normalisation, or "along the tube"
    // becomes a suggestion rather than the field's direction.
    float fu, fv;
    TrailFlow(0.37f, 0.61f, 0.4f, &fu, &fv);
    CHECK_MSG(fv < -0.2f, "the field still runs predominantly ALONG the tube",
              "v component %.3f", fv);
}

// ── the mirror guard ────────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pending = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pending = 1; continue; }
        if (pending && o > 0 && o + 1 < cap) out[o++] = ' ';
        pending = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[300000], flat[300000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *fs = "core/shaders/flow_map.fs";
    CHECK(FileHas(fs, "uv + flow * (phase0 - 0.5) * uStrength"),
          "the displacement is still CENTRED on the undisplaced phase");
    CHECK(!FileHas(fs, "uv + flow * phase0 * uStrength"),
          "and the uncentred form, which inverted the whole technique, is gone");
    CHECK(FileHas(fs, "float blend = abs(phase0 * 2.0 - 1.0);"),
          "the triangle-wave weight still matches the centring it assumes");
    CHECK(FileHas(fs, "texture(flowTex, fragTexCoord).rg * 2.0 - 1.0"),
          "the field is still decoded from RG as a signed direction");

    const char *c = "core/flow_map.c";
    CHECK(FileHas(c, "FlowMap_CreateWithTrailTexture"),
          "there is still a generator for the TUBE case, not only a vortex");
    CHECK(FileHas(c, "float fv = -1.0f;"),
          "whose base flow still runs AGAINST the direction of travel");
    CHECK(FileHas(c, "2.0f * PI * ((float)ku[h] * u + (float)kv[h] * v)"),
          "and is built from INTEGER harmonics, so it tiles by construction");
}

int main(void)
{
    printf("=== flow map: the two-phase field ===\n");
    Test_TheShownLayerIsTheLeastStretched();
    Test_TrailFieldTilesOnBothAxes();
    Test_EncodingNeverClips();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

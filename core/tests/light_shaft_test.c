// core headless test — VFX_ComposeLightShaft's cone geometry and envelopes.
//
// The property that makes godrays godrays is geometric and therefore free to
// check: the shafts must CONVERGE at the source and separate with distance. A
// bundle of parallel bars is the failure mode, it is one missing `* t` away, and
// it would cost a full build-and-look cycle to notice.
//
// Mirrors core/composition/common/vc_light_shaft.inl; the mirror is pinned by
// Test_MirrorStillMatchesSource so it cannot rot into fiction.

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
#define PI 3.1415926535f
#endif

#define LIGHT_SHAFT_MAX 8

static float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float SmoothStep01(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }

static float ShaftAlpha(float t)
{
    float in  = SmoothStep01(t / 0.16f);
    float out = 1.0f - SmoothStep01((t - 0.35f) / 0.65f);
    return in * out;
}

// Cross-shaft profile (the generated texture's alpha, striation aside).
static float ShaftProfile(float u)
{
    float d = (u - 0.5f) / 0.26f;
    return expf(-d * d);
}

// The golden-angle placement of shaft `i` of `n`, as the .inl computes it.
static void ShaftOffset(int i, int n, float *ox, float *oy)
{
    float g   = (float)i * 2.39996f;
    float rad = sqrtf(((float)i + 0.5f) / (float)n);
    *ox = cosf(g) * rad;
    *oy = sinf(g) * rad;
}

static void Test_ShaftsConvergeAtTheSource(void)
{
    // The lateral offset is scaled by t, so at t = 0 every shaft is exactly at
    // `from`, and the separation grows without bound along the cone.
    const int n = 6;
    const float width = 2.0f;
    float worstAtSource = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float ox, oy;
        ShaftOffset(i, n, &ox, &oy);
        float spread = width * 0.5f * 0.0f;          // t = 0
        float off = sqrtf(ox * ox + oy * oy) * spread;
        if (off > worstAtSource) worstAtSource = off;
    }
    CHECK_MSG(worstAtSource < 1e-6f, "every shaft passes exactly through the source",
              "worst offset %.6f m", worstAtSource);

    // ...and separation is strictly increasing along the shaft.
    float ox, oy;
    ShaftOffset(3, n, &ox, &oy);
    float r = sqrtf(ox * ox + oy * oy);
    int rises = 1;
    float prev = -1.0f;
    for (int k = 0; k <= 20; k++)
    {
        float t = (float)k / 20.0f;
        float off = r * width * 0.5f * t;
        if (prev >= 0.0f && off <= prev) rises = 0;
        prev = off;
    }
    CHECK(rises, "the cone opens monotonically from source to far end");
}

static void Test_NoTwoShaftsOverlap(void)
{
    // Even spacing reads as a machine part; random per frame flickers. The golden
    // angle is neither — but only if it actually separates them, so measure.
    for (int n = 3; n <= LIGHT_SHAFT_MAX; n++)
    {
        float closest = 1e9f;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
            {
                float ax, ay, bx, by;
                ShaftOffset(i, n, &ax, &ay);
                ShaftOffset(j, n, &bx, &by);
                float d = sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
                if (d < closest) closest = d;
            }
        if (closest < 0.20f)
        {
            CHECK_MSG(0, "no two shafts land on top of each other at any count",
                      "n=%d closest %.3f of the cone radius", n, closest);
            return;
        }
    }
    CHECK(1, "no two shafts land on top of each other at any count");
}

static void Test_ShaftFadesAtBothEnds(void)
{
    // A hard bright cap at the source looks cut; a hard end at the far side looks
    // like the light stops in mid-air. Both ends have to go to zero, and the
    // brightest part must be near the source — light attenuates with distance.
    CHECK_MSG(ShaftAlpha(0.0f) < 1e-5f, "alpha is zero at the source end",
              "%.5f", ShaftAlpha(0.0f));
    CHECK_MSG(ShaftAlpha(1.0f) < 1e-5f, "alpha is zero at the far end",
              "%.5f", ShaftAlpha(1.0f));

    float peakAt = 0.0f, peak = 0.0f;
    for (int k = 0; k <= 1000; k++)
    {
        float t = (float)k / 1000.0f;
        float a = ShaftAlpha(t);
        if (a > peak) { peak = a; peakAt = t; }
    }
    CHECK_MSG(peakAt > 0.15f && peakAt < 0.42f,
              "the shaft is brightest in its first half (light attenuates)",
              "peak %.2f at t=%.2f", peak, peakAt);
    CHECK_MSG(peak > 0.85f, "the peak actually reaches full strength",
              "%.2f", peak);
}

static void Test_CrossProfileIsSymmetric(void)
{
    // Deliberately UNLIKE the slash: a blade has an edge, a shaft of light has a
    // middle. This is also what lets the two passes share a centre instead of
    // needing the outer-edge alignment (core/docs/LANDMINES.md).
    float worst = 0.0f;
    for (int x = 0; x < 64; x++)
    {
        float u = ((float)x + 0.5f) / 64.0f;
        float d = fabsf(ShaftProfile(u) - ShaftProfile(1.0f - u));
        if (d > worst) worst = d;
    }
    CHECK_MSG(worst < 1e-5f, "the cross-shaft profile is symmetric", "worst %.6f", worst);
    CHECK_MSG(ShaftProfile(0.5f) > 0.99f && ShaftProfile(0.0f) < 0.06f,
              "bright in the middle, gone at the edges",
              "centre %.3f, edge %.3f", ShaftProfile(0.5f), ShaftProfile(0.0f));
}

static char *SlurpFile(const char *path)
{
    static char buf[262144];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *src = SlurpFile("core/composition/common/vc_light_shaft.inl");
    CHECK(src != NULL, "core/composition/common/vc_light_shaft.inl readable");
    if (!src) return;

    CHECK(strstr(src, "float spread = width * 0.5f * s_shaftSpread * t;") != NULL,
          "mirror: the offset still scales with t (the cone converges)");
    CHECK(strstr(src, "float rad  = sqrtf(((float)sIdx + 0.5f) / (float)shafts);") != NULL,
          "mirror: golden-angle placement");
    CHECK(strstr(src, "float in  = SmoothStep01(t / 0.16f);") != NULL,
          "mirror: the source-end rise");
    CHECK(strstr(src, "float out = 1.0f - SmoothStep01((t - 0.35f) / 0.65f);") != NULL,
          "mirror: the distance falloff");
    CHECK(strstr(src, "float d = (u - 0.5f) / 0.26f;") != NULL,
          "mirror: the symmetric cross profile");

    // Contracts the mirror cannot compute.
    CHECK(strstr(src, "DrawRibbonStrip(pts") != NULL,
          "camera-facing ribbons (a shaft is seen from wherever the viewer is)");
    CHECK(strstr(src, "VC_Breathe(time + (float)sIdx") != NULL,
          "each shaft breathes on its OWN clock (one clock reads as a lamp)");
    CHECK(strstr(src, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false") != NULL &&
          strstr(src, "VFXRender_EndDraw(&renderScope)") != NULL,
          "depth-state change is batch-flushed (ENGINE_LANDMINES §1)");
    CHECK(strstr(src, "u_cameraDepthTex") == NULL && strstr(src, "sampler2D") == NULL,
          "no second sampler (soft particles are parked — rlvk binding landmine)");
    CHECK(strstr(src, "CameraShake") == NULL && strstr(src, "Camera_Shake") == NULL,
          "no camera shake on the composition's own initiative");
}

int main(void)
{
    printf("=== core headless test: light shaft ===\n");
    Test_ShaftsConvergeAtTheSource();
    Test_NoTwoShaftsOverlap();
    Test_ShaftFadesAtBothEnds();
    Test_CrossProfileIsSymmetric();
    Test_MirrorStillMatchesSource();
    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}

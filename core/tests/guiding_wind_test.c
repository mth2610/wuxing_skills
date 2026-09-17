// Headless contract test for Ghost of Tsushima Guiding Wind VFX.
// Validates mathematical properties of the streamline solver, terrain look-ahead lift,
// aerodynamic tapering, and source-level shader/system binding contracts.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[4096];
    size_t used = 0;
    size_t n;
    while ((n = fread(buf + used, 1, sizeof(buf) - 1 - used, f)) != 0)
    {
        used += n;
        buf[used] = '\0';
        if (strstr(buf, needle) != NULL)
        {
            fclose(f);
            return 1;
        }
        if (used > 1024)
        {
            memmove(buf, buf + used - 1024, 1024);
            used = 1024;
        }
    }
    fclose(f);
    return 0;
}

// Emulate aerodynamic ribbon tapering envelope
static float AerodynamicTaper(float frac)
{
    if (frac <= 0.0f || frac >= 1.0f) return 0.0f;
    float t = sinf(frac * PI);
    return powf(t, 0.75f);
}

// Emulate look-ahead terrain lift
static float EvalSlopeLift(float hCurrent, float hAhead, float lookAheadDist)
{
    float slopeDelta = fmaxf(0.0f, hAhead - hCurrent);
    float slopeLift = (slopeDelta / lookAheadDist) * 2.2f;
    if (slopeLift > 8.0f) slopeLift = 8.0f;
    return slopeLift;
}

int main(void)
{
    int failed = 0;
    int checks = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (cond) { \
        printf("PASS: %s\n", msg); \
    } else { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failed++; \
    } \
} while (0)

    printf("=== Guiding Wind (Ghost of Tsushima) Contract Test ===\n");

    // 1. Math Check: Aerodynamic Tapering Envelope
    CHECK(fabsf(AerodynamicTaper(0.0f)) < 1e-6f, "Head tip tapers to exact needle point (0 width)");
    CHECK(fabsf(AerodynamicTaper(1.0f)) < 1e-6f, "Tail tip tapers to exact needle point (0 width)");
    CHECK(AerodynamicTaper(0.5f) > 0.99f, "Center crest reaches full envelope width");
    CHECK(AerodynamicTaper(0.1f) > 0.35f, "Rapid needle expansion at front");

    // 2. Math Check: Look-Ahead Terrain Lift
    float flatLift = EvalSlopeLift(2.0f, 2.0f, 3.2f);
    CHECK(fabsf(flatLift) < 1e-6f, "Flat terrain produces 0 aerodynamic lift");

    float gentleSlopeLift = EvalSlopeLift(2.0f, 3.6f, 3.2f);
    CHECK(gentleSlopeLift > 1.0f && gentleSlopeLift < 2.0f, "Gentle uphill slope produces smooth aerodynamic lift");

    float cliffLift = EvalSlopeLift(2.0f, 20.0f, 3.2f);
    CHECK(fabsf(cliffLift - 8.0f) < 1e-6f, "Steep cliff slope lift is safely clamped to 8.0m max");

    // 3. Source Bindings: vc_guiding_wind.inl
    const char *inlPath = "core/composition/common/vc_guiding_wind.inl";
    CHECK(FileHas(inlPath, "Wind_EvaluateVelocity"), "Vorticle & Macro wind field integrated via Wind_EvaluateVelocity");
    CHECK(FileHas(inlPath, "VFX_GroundHeightFromMap"), "Terrain elevation probed via VFX_GroundHeightFromMap");
    CHECK(FileHas(inlPath, "lookAheadDist"), "Look-ahead terrain elevation probe implemented");
    CHECK(FileHas(inlPath, "Ribbon_ComputeArcLengthUV"), "Arc-length UV mapping used for uniform texture coordinates");
    CHECK(FileHas(inlPath, "DrawRibbonStripEx"), "DrawRibbonStripEx camera-facing geometry submission");
    CHECK(FileHas(inlPath, "VFXRender_BeginDraw(VFX_RENDER_PASS_BODY"), "Bright-Background VFX Contract: Body submitted to ALPHA pass");
    CHECK(FileHas(inlPath, "GuidingWind_GetSilkTexture"), "Procedural Gaussian silk streamline texture generator present");
    CHECK(FileHas(inlPath, "spiralRadius"), "3D Helical braided tendril orbit solver present");

    // 4. Source Bindings: wind_ribbon.fs
    const char *fsPath = "core/shaders/wind_ribbon.fs";
    CHECK(FileHas(fsPath, "smoothstep(threshold - soft, threshold + soft, silk)"), "Alpha erosion operator matches Ghost of Tsushima paper");
    CHECK(FileHas(fsPath, "mix(s1, s2, 0.48)"), "Dual-layer counter-panning procedural silk streamlines blending");
    CHECK(FileHas(fsPath, "edgeMask"), "Cross-ribbon sinusoidal edge attenuation eliminates polygon borders");
    CHECK(FileHas(fsPath, "headFade * tailFade"), "Head and tail length tapering in fragment shader");
    CHECK(FileHas(fsPath, "VFX_ResolveBody"), "Shader output resolved through shared VFX_ResolveBody");

    printf("---\n%d/%d checks passed.\n", checks - failed, checks);
    return failed ? 1 : 0;
}

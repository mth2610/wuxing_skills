// Contract for the hit-reaction shockwave primary. Its mesh test owns the
// topology; this test pins composition boundaries so it cannot quietly become
// a ground wave or absorb the separate flash/particle VFX.

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, name) do { \
    checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static float SmoothStep01(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v * v * (3.0f - 2.0f * v);
}

static float Radius01(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t, 2.8f);
}

static float Alpha01(float t)
{
    if (t <= 0.0f || t >= 1.0f) return 0.0f;
    return SmoothStep01(t / 0.055f) * powf(1.0f - t, 1.35f);
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    static char buffer[140000];
    size_t n;
    if (f == NULL) return 0;
    n = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[n] = '\0';
    fclose(f);
    return strstr(buffer, needle) != NULL;
}

static void Test_Envelope(void)
{
    CHECK(Radius01(0.0f) == 0.0f && Radius01(1.0f) == 1.0f,
          "the hit disc has an explicit zero-to-full expansion domain");
    CHECK(Radius01(0.20f) > 0.45f,
          "the disc expands decisively at impact instead of reading as a slow ripple");
    CHECK(Alpha01(0.0f) == 0.0f && Alpha01(1.0f) == 0.0f,
          "the disc appears and fully dissipates within its own lifetime");
    CHECK(Alpha01(0.08f) > Alpha01(0.72f),
          "the pressure front is front-loaded, then gives way to separate residual VFX");
}

static void Test_CompositionBoundary(void)
{
    const char *src = "core/composition/common/vc_impact_shockwave.inl";
    CHECK(FileHas(src, "void VFX_ComposeImpactShockwave("),
          "the impact disc has its own public composition primary");
    CHECK(FileHas(src, "ProceduralMesh_BuildImpactShockwave(&mesh"),
          "the primary uses the free-space impact mesh");
    CHECK(FileHas(src, "ScreenDistort_BeginVFXBody();") &&
          FileHas(src, "ScreenDistort_BeginVFXEmission();"),
          "the disc keeps coloured material separate from its restrained bloom");
    CHECK(FileHas(src, "#define IMPACT_SHOCKWAVE_LAYERS  3") &&
          FileHas(src, "s_impactShockLayers[IMPACT_SHOCKWAVE_LAYERS]") &&
          FileHas(src, "HIGH: thin, detailed erosion") &&
          FileHas(src, "LOW: broad, soft broken boundary"),
          "one master shader drives distinct High/Mid/Low shockwave layers");
    CHECK(FileHas(src, "It does not submit particles, a flash, or a decal.") &&
          !FileHas(src, "VFX_ComposeImpactShockwave(Vector3 center, VC_MaterialId mat,\n                                float radius, float t01, GroundHeightSampleFn"),
          "the hit disc does not take a terrain callback or subsume other impact VFX");
    CHECK(FileHas("core/shaders/impact_shockwave.fs", "vec2 p = baseP + uvWarp * 0.11;") &&
          FileHas("core/shaders/impact_shockwave.fs", "float warpedRadial = length(p);") &&
          FileHas("core/shaders/impact_shockwave.fs", "uniform float u_detailScale;"),
          "panned noise moves UV coordinates before the radial mask at three detail scales");
    CHECK(FileHas(src, "VFX_SurfaceRegistry_Get(VFX_SURFACE_IMPACT_SMOKE)") &&
          FileHas(src, "rlSetTexture(smokeSurface->body.id)") &&
          FileHas("core/shaders/impact_shockwave.fs", "uniform sampler2D texture0;") &&
          FileHas("core/shaders/impact_shockwave.fs", "texture(texture0, vec2(smokeU, smokeV)).a"),
          "one authored smoke strip maps once through polar UV and supplies the torn coverage");
    CHECK(FileHas(src, "void VFX_TriggerImpactShockwaveDistortion(") &&
          FileHas(src, "ScreenDistort_Add(center, radius, strength, 0.18f, 1.0f);"),
          "refraction is submitted once to the dedicated screen-distortion pass");
    CHECK(FileHas("scripts/vfx_test_manifest.json", "\"start_call\": \"VFX_TriggerImpactShockwaveDistortion"),
          "the bench triggers the refraction once, instead of adding one source every draw frame");
}

int main(void)
{
    printf("=== impact shockwave primary ===\n");
    Test_Envelope();
    Test_CompositionBoundary();
    printf("---- %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

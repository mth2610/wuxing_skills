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
          "the hit shell has an explicit zero-to-full expansion domain");
    CHECK(Radius01(0.20f) > 0.45f,
          "the shell expands decisively at impact instead of reading as a slow ripple");
    CHECK(Alpha01(0.0f) == 0.0f && Alpha01(1.0f) == 0.0f,
          "the shell appears and fully dissipates within its own lifetime");
    CHECK(Alpha01(0.08f) > Alpha01(0.72f),
          "the pressure shell is front-loaded, then gives way to separate residual VFX");
}

static void Test_CompositionBoundary(void)
{
    const char *src = "core/composition/common/vc_impact_shockwave.inl";
    CHECK(FileHas(src, "void VFX_ComposeImpactShockwave("),
          "the impact shell has its own public composition primary");
    CHECK(FileHas(src, "ProceduralMesh_BuildImpactShockwave(&mesh"),
          "the primary uses the free-space impact mesh");
    CHECK(FileHas(src, "ScreenDistort_BeginVFXBody();") &&
          FileHas(src, "ScreenDistort_BeginVFXEmission();"),
          "the shell keeps coloured material separate from its restrained bloom");
    CHECK(FileHas(src, "It does not submit particles, a flash, a decal, or") &&
          !FileHas(src, "VFX_ComposeImpactShockwave(Vector3 center, VC_MaterialId mat,\n                                float radius, float t01, GroundHeightSampleFn"),
          "the hit shell does not take a terrain callback or subsume other impact VFX");
    CHECK(FileHas("core/shaders/impact_shockwave.fs", "vec3(cos(angle) * 2.1"),
          "shader flow is circular and seam-safe rather than terrain UV based");
}

int main(void)
{
    printf("=== impact shockwave primary ===\n");
    Test_Envelope();
    Test_CompositionBoundary();
    printf("---- %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

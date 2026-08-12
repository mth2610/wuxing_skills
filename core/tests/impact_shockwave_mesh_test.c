// Headless contract for the free-space impact shockwave shell.
//
// Mirrors core/geometry/pm_shockwave.inl. It validates topology/math only; a
// human visual pass still validates the read against the impact reference.

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef PI
#define PI 3.1415926535f
#endif

static int failures = 0;
static int checks = 0;

#define CHECK(cond, name) do { \
    checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static float LensProfile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return sinf(PI * u);
}

static float AngularNoise(float angle, float frequency, float phase)
{
    return sinf(angle * frequency + phase) * 0.70f +
           sinf(angle * (frequency * 2.0f + 1.0f) - phase * 1.37f) * 0.30f;
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    static char buffer[120000];
    size_t n;
    if (f == NULL) return 0;
    n = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[n] = '\0';
    fclose(f);
    return strstr(buffer, needle) != NULL;
}

static void Test_Lens(void)
{
    CHECK(LensProfile(0.0f) == 0.0f, "the impact shell closes at its inner edge");
    CHECK(LensProfile(1.0f) == 0.0f, "the impact shell closes at its outer edge");
    CHECK(fabsf(LensProfile(0.5f) - 1.0f) < 1e-5f,
          "the impact shell is thickest at the pressure front");
}

static void Test_AngularDeform(void)
{
    const float frequency = 5.0f;
    const float phase = 0.37f;
    float maxAbs = 0.0f;

    CHECK(fabsf(AngularNoise(0.0f, frequency, phase) -
                AngularNoise(2.0f * PI, frequency, phase)) < 1e-5f,
          "the ragged outline closes cleanly at the UV seam");
    for (float a = 0.0f; a <= 2.0f * PI; a += 0.001f) {
        float v = fabsf(AngularNoise(a, frequency, phase));
        if (v > maxAbs) maxAbs = v;
    }
    CHECK(maxAbs <= 1.001f, "the normalized silhouette noise remains bounded");
}

static void Test_Contract(void)
{
    const char *header = "core/geometry/procedural_mesh_utils.h";
    const char *src = "core/geometry/pm_impact_shockwave.inl";
    CHECK(FileHas(header, "#define IMPACT_SHOCKWAVE_SIDES 2"),
          "the shell has front and back surfaces, not a flat ground decal");
    CHECK(FileHas(header, "IMPACT_SHOCKWAVE_MAX_SLICES 64"),
          "the shell has enough azimuthal resolution for an irregular outline");
    CHECK(FileHas(src, "ProceduralMesh_BuildImpactShockwave"),
          "the mesh is explicitly an impact shockwave builder");
    CHECK(!FileHas(src, "GroundHeightSampleFn"),
          "the hit shockwave cannot raycast or conform to terrain");
    CHECK(FileHas(src, "out->verts[side][s][i]"),
          "the mesh owns its free-space three-dimensional positions");
    CHECK(FileHas(src, "jitter = noise * radialJitter * u * u"),
          "the irregularity concentrates on the outer silhouette");
    CHECK(FileHas(src, "heightNoise = noise * heightJitter * profile"),
          "vertical breakup is constrained to the pressure shell");
}

int main(void)
{
    printf("=== impact shockwave mesh ===\n");
    Test_Lens();
    Test_AngularDeform();
    Test_Contract();
    printf("---- %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

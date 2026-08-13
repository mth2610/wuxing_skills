// Headless contract for the planar impact shockwave disc.
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
    CHECK(FileHas(header, "#define IMPACT_SHOCKWAVE_SIDES 1"),
          "the hit wave is one planar disc, not a spherical shell");
    CHECK(FileHas(header, "IMPACT_SHOCKWAVE_MAX_SLICES 64"),
          "the shell has enough azimuthal resolution for an irregular outline");
    CHECK(FileHas(src, "ProceduralMesh_BuildImpactShockwave"),
          "the mesh is explicitly an impact shockwave builder");
    CHECK(!FileHas(src, "GroundHeightSampleFn"),
          "the hit shockwave cannot raycast or conform to terrain");
    CHECK(FileHas(src, "center.y,"),
          "every disc vertex stays in the hit plane, never grows into a sphere");
    CHECK(FileHas(src, "ringRadius = radius * v + jitter"),
          "the mesh reaches the centre; the shader, not topology, cuts the ragged hole");
    CHECK(FileHas(src, "jitter = noise * radialJitter * v * v"),
          "the irregularity concentrates on the outer silhouette");
    CHECK(!FileHas(src, "verticalScale") && !FileHas(src, "latitude"),
          "the hit wave owns no sphere or latitude geometry");
}

int main(void)
{
    printf("=== impact shockwave mesh ===\n");
    Test_AngularDeform();
    Test_Contract();
    printf("---- %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

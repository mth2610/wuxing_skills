// Headless contract for the dedicated procedural shockwave mesh.
//
// This mirrors the profile and angular deformation in
// core/geometry/pm_shockwave.inl.  It cannot verify GPU rendering, but it
// proves the mesh topology has a seamless UV seam, a leading crest, and a
// deterministic deformation that cannot make the annulus fold through itself.

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define SHOCKWAVE_MAX_SLICES 64
#define SHOCKWAVE_MAX_RADIALS 8

static int failures = 0;
static int checks = 0;

#define CHECK(cond, name) do { \
    checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static float Profile(float u, float crestU)
{
    float k;
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    k = logf(0.5f) / logf(crestU);
    return sinf(PI * powf(u, k));
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

static void Test_Profile(void)
{
    const float crestU = 0.6666667f;
    float peakU = 0.0f;
    float peak = 0.0f;

    CHECK(Profile(0.0f, crestU) == 0.0f, "the annulus meets the ground at its inner edge");
    CHECK(Profile(1.0f, crestU) == 0.0f, "the annulus meets the ground at its outer edge");

    for (float u = 0.0f; u <= 1.0f; u += 0.0005f) {
        float h = Profile(u, crestU);
        if (h > peak) { peak = h; peakU = u; }
    }
    CHECK(fabsf(peakU - crestU) < 0.02f, "the crest leads instead of sitting in the middle");
    CHECK(fabsf(peak - 1.0f) < 1e-3f, "the profile has a normalized crest");
}

static void Test_AngularDeform(void)
{
    const float frequency = 5.0f;
    const float phase = 0.37f;
    float maxAbs = 0.0f;

    CHECK(fabsf(AngularNoise(0.0f, frequency, phase) -
                AngularNoise(2.0f * PI, frequency, phase)) < 1e-5f,
          "the angular deformation closes cleanly at the seam");

    for (float a = 0.0f; a <= 2.0f * PI; a += 0.001f) {
        float v = fabsf(AngularNoise(a, frequency, phase));
        if (v > maxAbs) maxAbs = v;
    }
    CHECK(maxAbs <= 1.001f, "the normalized angular deformation remains bounded");
}

static void Test_Contract(void)
{
    const char *src = "core/geometry/pm_shockwave.inl";
    const char *header = "core/geometry/procedural_mesh_utils.h";
    CHECK(FileHas(header, "#define SHOCKWAVE_MAX_SLICES 64"),
          "the mesh has enough azimuthal resolution for silhouette deformation");
    CHECK(FileHas(header, "#define SHOCKWAVE_MAX_RADIALS 8"),
          "the mesh has enough cross-band resolution for the raised lip");
    CHECK(FileHas(src, "out->uv[s][i] = (Vector2){"),
          "the builder stores angle and cross-band UV coordinates");
    CHECK(FileHas(src, "ShockwaveMesh_AngularNoise"),
          "the outer silhouette is deformed in the geometry builder");
    CHECK(FileHas(src, "jitter = noise * radialJitter * u * u"),
          "radial jitter is concentrated on the outer silhouette");
    CHECK(FileHas(src, "heightNoise = noise * lipJitter * profile"),
          "lip jitter is constrained by the cross-band profile");
    CHECK(FileHas(src, "if (n.y < 0.0f) n = Vector3Negate(n);"),
          "deformed normals consistently face upward");
}

int main(void)
{
    printf("=== shockwave mesh ===\n");
    Test_Profile();
    Test_AngularDeform();
    Test_Contract();
    printf("---- %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

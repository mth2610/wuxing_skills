#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_checks;
static int s_failures;

#define CHECK(condition, message) do { \
    s_checks++; \
    if (condition) printf("PASS: %s\n", message); \
    else { printf("FAIL: %s\n", message); s_failures++; } \
} while (0)

static float Clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float SmoothStep(float a, float b, float x)
{
    float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

static float CompositeAlpha(int layers, float layerAlpha)
{
    return 1.0f - powf(1.0f - layerAlpha, (float)layers);
}

static float OldEmissionGate(float emission)
{
    return powf(Clamp01(emission * 1.30f), 1.10f);
}

static float CompactEmissionGate(float emission)
{
    return powf(SmoothStep(0.06f, 0.75f, emission), 1.35f);
}

static int FileContains(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    static char source[1u << 20];
    size_t count = fread(source, 1, sizeof(source) - 1u, file);
    source[count] = '\0';
    fclose(file);
    return strstr(source, needle) != NULL;
}

int main(void)
{
    const float representativeLayerAlpha = 0.35f * 0.55f * 0.35f;
    float oldCentre = CompositeAlpha(68, representativeLayerAlpha);
    float cappedCentre = CompositeAlpha(18, representativeLayerAlpha);

    CHECK(oldCentre > 0.98f,
          "68 overlapping whole-puff layers mathematically collapse into a solid centre");
    CHECK(cappedCentre < 0.75f,
          "18 whole-puff layers preserve transmission without fragmenting the body");
    CHECK(CompactEmissionGate(0.20f) < OldEmissionGate(0.20f) * 0.25f,
          "compact gate suppresses broad low-emission orange fog");
    CHECK(CompactEmissionGate(1.0f) > 0.99f,
          "compact gate preserves the hottest authored flame cores");

    const char *source = "core/composition/fire/flame_volume.inl";
    const char *shader = "core/particles/shaders/particle_lit.fs";
    const char *particles = "core/particles/particle_system.c";
    CHECK(FileContains(source, "#define FVOL_MAX_VOLUME_LIVE 18"),
          "volume flame has a strict whole-puff live budget");
    CHECK(FileContains(source, "fminf(requestedLive, FVOL_MAX_VOLUME_LIVE)"),
          "runtime tuning cannot bypass the whole-puff budget");
    CHECK(FileContains(source, "static float s_fvolEmissive = 1.8f"),
          "volume flame uses a bounded default radiance gain");
    CHECK(FileContains(source, "static float s_fvolBodySize = 1.05f"),
          "fewer puffs grow slightly to retain a connected flame silhouette");
    CHECK(FileContains(shader, "smoothstep(0.06, 0.75, emis)"),
          "shader gates emission to the authored hot structure");
    CHECK(FileContains(shader, "pow(compactEmission, 1.35)"),
          "shader tightens the hot structure without clipping its peak");
    CHECK(FileContains(shader, "float flameMask = smoothstep(0.015, 0.08, emis);"),
          "pure flame silhouette is derived only from emission");
    CHECK(!FileContains(shader, "emis + opac * 0.4"),
          "packed smoke opacity cannot widen the pure flame silhouette");
    CHECK(FileContains(shader, "finalColor = vec4(flame * fade, 0.0);"),
          "pure flame carries radiance without smoke/body opacity");
    CHECK(FileContains(source, ".render.smokeGain = 0.0f"),
          "volume flame explicitly disables the packed smoke decoder");
    CHECK(FileContains(source, ".onDeathEmit = emitSmoke ? &s_fvolSmokeSeed : NULL"),
          "optional smoke is a distinct death sub-emitter");
    CHECK(!FileContains(source, "flame_smoke_gain"),
          "mixed packed-smoke tuning is removed from flame volume");
    CHECK(!FileContains(particles, "Random01() * 160.0f - 80.0f"),
          "death sub-emitter obeys authored metre-scale velocity");

    printf("---- %d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}

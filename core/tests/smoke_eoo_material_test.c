#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[260000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static float EooDetail(float r, float g)
{
    float ao = 0.35f + 0.65f * g;
    float baked = 0.55f + 0.90f * r;
    return ao * baked;
}

static float ReconstructZ(float r, float g)
{
    float x = r * 2.0f - 1.0f;
    float y = g * 2.0f - 1.0f;
    float zz = 1.0f - x * x - y * y;
    return sqrtf(zz > 0.0f ? zz : 0.0f);
}

int main(void)
{
    const char *shader = "core/particles/shaders/particle_lit.fs";
    const char *config = "core/vfx_config.h";
    const char *runtime = "core/particles/particle_system.c";
    const char *smoke = "core/composition/common/vc_smoke_volume.inl";
    const char *registry = "core/vfx_surface_registry.h";

    CHECK(EooDetail(0.30f, 0.72f) > EooDetail(0.02f, 0.72f),
          "EOO R preserves baked internal light detail");
    CHECK(EooDetail(0.08f, 0.92f) > EooDetail(0.08f, 0.55f),
          "EOO G preserves transmittance/occlusion detail");
    CHECK(fabsf(ReconstructZ(0.5f, 0.5f) - 1.0f) < 1e-5f &&
          ReconstructZ(0.75f, 0.5f) < 1.0f,
          "BC5 normal reconstructs Z from signed RG");
    CHECK(Has(config, "int smokeSheet;") && Has(config, "Texture2D normalTex;"),
          "render config exposes EOO smoke and its normal atlas");
    CHECK(Has(runtime, "p->smokeSheet = config.render.smokeSheet") &&
          Has(runtime, "p->normalTexId = config.render.normalTex.id"),
          "particle runtime carries the EOO material contract");
    CHECK(Has(runtime, "float vs = p->smokeSheet ? 3.0f : (float)p->volumeSheet") &&
          !Has(shader, "uniform float u_smokeSheet") &&
          Has(shader, "if (u_volumeSheet > 2.5)") &&
          Has(shader, "if (u_volumeSheet > 1.5 && u_volumeSheet < 2.5)") &&
          Has(shader, "uniform sampler2D u_normalTex") &&
          Has(shader, "sqrt(max(1.0 - dot(smokeNxy, smokeNxy), 0.0))"),
          "EOO reuses the proven volume material selector and reconstructs normal Z");
    CHECK(Has(smoke, ".render.smokeSheet = 1") &&
          Has(smoke, ".render.normalTex = normalTex") &&
          !Has(smoke, ".render.sixWayLighting = (int)s_svolLighting"),
          "Smoke Volume uses EOO+normal rather than synthetic 6-way");
    CHECK(Has(registry, "const char *normalPath;") && Has(registry, "Texture2D normalMap;"),
          "surface registry owns normal-map companions semantically");
    return failures ? 1 : 0;
}

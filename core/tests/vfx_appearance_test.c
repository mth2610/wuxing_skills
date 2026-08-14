#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vfx_contrast.c"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        failed++; \
    } \
} while (0)

static int Near(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char buffer[1024];
    size_t used = 0;
    size_t count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) {
            fclose(file);
            return 1;
        }
        if (used > 256) {
            memmove(buffer, buffer + used - 256, 256);
            used = 256;
        }
    }
    fclose(file);
    return 0;
}

int main(void)
{
    int failed = 0;
    VFXResolvedAppearance legacy = {
        .surface = VFX_SURFACE_PREMULTIPLIED,
        .contrast = VFX_CONTRAST_DUST,
        .bodyOpacity = 0.37f,
        .emissionIntensity = 7.5f,
        .emissionThreshold = 0.42f,
        .unlit = false
    };

    VFXResolvedAppearance inherited = VFXAppearance_Resolve(VFX_APPEARANCE_INHERIT,
                                                             legacy);
    CHECK(inherited.surface == legacy.surface);
    CHECK(inherited.contrast == legacy.contrast);
    CHECK(Near(inherited.bodyOpacity, legacy.bodyOpacity));
    CHECK(Near(inherited.emissionIntensity, legacy.emissionIntensity));
    CHECK(Near(inherited.emissionThreshold, legacy.emissionThreshold));
    CHECK(inherited.unlit == legacy.unlit);

    const VFXResolvedAppearance normal = VFXAppearance_Resolve(
        VFX_APPEARANCE_NORMAL, legacy);
    CHECK(normal.surface == VFX_SURFACE_ALPHA);
    CHECK(normal.contrast == VFX_CONTRAST_NONE);
    CHECK(Near(normal.emissionIntensity, 0.0f));
    CHECK(!normal.unlit);

    const VFXResolvedAppearance smoke = VFXAppearance_Resolve(
        VFX_APPEARANCE_SMOKE, legacy);
    CHECK(smoke.surface == VFX_SURFACE_ALPHA);
    CHECK(smoke.contrast == VFX_CONTRAST_SMOKE);
    CHECK(smoke.bodyOpacity > 0.8f);
    CHECK(Near(smoke.emissionIntensity, 0.0f));

    const VFXResolvedAppearance glow = VFXAppearance_Resolve(
        VFX_APPEARANCE_GLOW, legacy);
    CHECK(glow.surface == VFX_SURFACE_ADDITIVE);
    CHECK(glow.contrast == VFX_CONTRAST_ENERGY);
    CHECK(glow.unlit);
    CHECK(glow.emissionIntensity > 1.0f);
    CHECK(Near(glow.bodyOpacity, 0.0f));

    const VFXResolvedAppearance fire = VFXAppearance_Resolve(
        VFX_APPEARANCE_FIRE, legacy);
    CHECK(fire.surface == VFX_SURFACE_PREMULTIPLIED);
    CHECK(fire.contrast == VFX_CONTRAST_FIRE);
    CHECK(fire.unlit);
    CHECK(fire.bodyOpacity > 0.0f && fire.bodyOpacity < 1.0f);
    CHECK(fire.emissionIntensity > glow.emissionIntensity);

    CHECK(VFXResolvedAppearance_UsesBody(fire));
    CHECK(VFXResolvedAppearance_UsesEmission(fire));
    CHECK(!VFXResolvedAppearance_UsesBody(glow));
    CHECK(VFXResolvedAppearance_UsesEmission(glow));
    CHECK(VFXResolvedAppearance_UsesBody(inherited));
    CHECK(VFXResolvedAppearance_UsesEmission(inherited));

    /* Numeric resolution alone cannot prove that geometry providers consume
     * it. These wiring guards keep the shared policy on all three hot paths. */
    CHECK(Has("core/particles/particle_system.c",
              "VFXAppearance_Resolve(\n      config.render.appearance"));
    CHECK(Has("core/particles/particle_manager.c",
              "ParticleManager_ResolveAppearance"));
    CHECK(Has("core/trails/trail_system.c",
              "VFXAppearance_Resolve(\n            config.material.appearance"));
    CHECK(Has("core/decals/decal_system.c",
              "VFXAppearance_Resolve(\n            d->material.appearance"));
    CHECK(Has("core/decals/shaders/decal_material.fs",
              "alpha * fragColor.a * u_bodyOpacity"));
    CHECK(Has("core/decals/decal_system.c",
              "GetShaderLocation(g_MaterialDecalShader, \"u_bodyOpacity\")"));
    CHECK(Has("core/decals/decal_system.c",
              "s_locMaterialBodyOpacity, &d->material.bodyOpacity"));

    puts(failed ? "vfx appearance: FAIL" : "vfx appearance: PASS");
    return failed != 0;
}

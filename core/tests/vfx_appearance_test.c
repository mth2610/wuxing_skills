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

    const VFXResolvedAppearance magic = VFXAppearance_Resolve(
        VFX_APPEARANCE_MAGIC, legacy);
    CHECK(magic.surface == VFX_SURFACE_PREMULTIPLIED);
    CHECK(magic.bodyOpacity > 0.0f);
    CHECK(magic.bodyOpacity < 0.30f);
    CHECK(magic.emissionIntensity > 0.0f);

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
    CHECK(Has("core/shaders/common/vfx_composite.glsl",
              "VFX_ResolvePremultiplied"));
    CHECK(Has("core/shaders/common/vfx_composite.glsl",
              "bodyColor * max(bodyIntensity, 0.0) * a"));
    CHECK(Has("core/shaders/common/vfx_composite.glsl",
              "emissionColor * max(coreMask, 0.0) * max(emissionGain, 0.0)"));
    {
        const float coverage = 0.4f, body = 0.5f, emission = 2.0f;
        const float resolved = 0.25f * body * coverage + 0.75f * emission;
        CHECK(Near(resolved, 1.55f));
        CHECK(Near(0.0f * body * 0.0f + 0.0f, 0.0f));
        CHECK(Near(0.0f * body * 0.0f + emission, emission));
    }
    CHECK(Has("core/shaders/fire_funnel.fs",
              "VFX_ResolveBody"));
    CHECK(Has("core/shaders/plasma_shell.fs",
              "VFX_ResolveBody"));
    CHECK(Has("core/shaders/shock_ring.fs",
              "VFX_ResolveBody"));
    CHECK(Has("core/shaders/magic_filaments.fs",
              "VFX_ResolveBody"));
    CHECK(Has("core/trails/shaders/trail_deform.fs",
              "VFX_ResolveEmission"));
    CHECK(Has("core/shaders/vfx_layered_annulus.fs",
              "VFX_ResolvePremultiplied") ||
          Has("core/shaders/vfx_layered_annulus.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/aura_shell.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/ground_aura.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/smoke_column.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/black_hole_swirl.fs", "VFX_ResolveBody"));
    CHECK(Has("core/trails/shaders/trail_body.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/additive_soft.fs", "VFX_ResolveEmission"));
    CHECK(Has("core/shaders/ground_wave.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/water_splash.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/crystal.fs", "VFX_ResolveBody"));
    CHECK(Has("core/decals/shaders/decal_material.fs", "VFX_ResolveEmission"));
    CHECK(Has("core/particles/shaders/gpu/particle_gpu_fallback.fs", "VFX_ResolveEmission"));
    CHECK(Has("core/particles/shaders/particle_lit.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/effect_material.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/puddle.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/rim_glow.fs", "VFX_ResolveBody"));
    CHECK(Has("core/shaders/taiji.fs", "VFX_ResolveBody"));
    CHECK(Has("core/trails/shaders/trail_volume.fs", "VFX_ResolveBody"));

    puts(failed ? "vfx appearance: FAIL" : "vfx appearance: PASS");
    return failed != 0;
}

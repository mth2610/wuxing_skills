#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[180000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static float TargetLive(float density, float countMul)
{
    float target = 8.0f * density * countMul;
    return target < 8.0f ? target : 8.0f;
}

int main(void)
{
    const char *smoke = "core/composition/common/vc_smoke_volume.inl";
    const char *header = "core/composition/visual_composer.h";
    const char *composer = "core/composition/visual_composer.c";
    const char *manifest = "assets/vfx_surface_profiles.json";
    const char *flame = "core/composition/fire/flame_volume.inl";
    const char *sync = "scripts/sync_vfx_test.py";
    const char *shader = "core/particles/shaders/particle_lit.fs";

    CHECK(TargetLive(1.0f, 1.0f) == 8.0f &&
          TargetLive(1.0f, 4.0f) == 8.0f &&
          TargetLive(0.5f, 1.0f) == 4.0f,
          "whole-puff population has an absolute eight-sprite ceiling");
    CHECK(Has(header, "VFX_SMOKE_STYLE_DEFAULT = VFX_SMOKE_STYLE_ROIL") &&
          !Has(smoke, "case VFX_SMOKE_STYLE_DEFAULT:"),
          "default smoke style is an alias without a duplicate switch case");
    CHECK(Has(smoke, "tint = (Color){220, 224, 230, 255}") &&
          Has(smoke, "case VFX_SMOKE_STYLE_PUFF_DARK:"),
          "default roil is white smoke while the explicit dark style remains available");
    CHECK(Has(smoke, "SVOL_MAX_LIVE_PER_EMITTER 8") &&
          Has(smoke, "fminf(targetLive, (float)SVOL_MAX_LIVE_PER_EMITTER)"),
          "runtime applies the whole-puff live ceiling");
    CHECK(Has(composer, "VC_SmokeVolumeEmitter_Update(dt);") &&
          Has(sync, "\"VFX_ComposeSmokeVolume\"") &&
          Has(sync, "(\"emitter\", \"timed\",      \"continuous\")"),
          "smoke volume is wired into update and lifecycle metadata");
    CHECK(Has(smoke, "e->legacyFeedAge += dt;\n            if (e->legacyFeedAge > 0.25f)") &&
          !Has(smoke, "e->legacyFeedAge += dt;\n            e->legacyFeedAge += dt;"),
          "legacy one-shot smoke ages once per frame, preserving its feed window");
    CHECK(Has(smoke, "VFX_SURFACE_SMOKE_ROIL_NIAGARA") &&
          Has(smoke, "VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA") &&
          Has(smoke, "VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA") &&
          Has(smoke, "VFX_SURFACE_SMOKE_WISPY_NIAGARA") &&
          Has(smoke, "VFX_SurfaceRegistry_Get(") &&
          !Has(smoke, "ResourceManager_LoadTexture") &&
          !Has(smoke, "assets/textures/vfx/"),
          "smoke volume resolves all Niagara sheets semantically");
    CHECK(Has(sync, "[PARTICLE] SMOKE VOLUME") &&
          Has(sync, "[TRAIL/FLOW] VOLUME TRAIL") &&
          Has(sync, "[GAS] GAS PLUME") &&
          Has(sync, "\"modules\": [\"particle\"]"),
          "ambiguous volume fixtures expose their backend family in generated metadata");
    CHECK(Has(manifest, "VFX_SURFACE_SMOKE_ROIL_NIAGARA") &&
          Has(manifest, "VFX_SURFACE_SMOKE_WISPY_NIAGARA"),
          "surface manifest owns the Niagara smoke contracts");
    CHECK(Has(shader, "if (u_volumeSheet > 2.5)") &&
          !Has(shader, "uniform float u_smokeSheet") &&
          Has(shader, "uniform sampler2D u_normalTex"),
          "single-sheet Niagara smoke has a dedicated EOO plus normal material path");
    CHECK(Has(flame, "VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_ROIL_NIAGARA)") &&
          Has(flame, "VFX_SurfaceRegistry_Get(VFX_SURFACE_FIREBALL_NIAGARA)") &&
          !Has(flame, "ResourceManager_LoadTexture(\"assets/textures/vfx"),
          "existing Niagara fire styles also resolve through semantic profiles");
    return failures ? 1 : 0;
}

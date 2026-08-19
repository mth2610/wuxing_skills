#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char buffer[2048];
    size_t used = 0;
    size_t count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) { fclose(file); return 1; }
        if (used > 512) {
            memmove(buffer, buffer + used - 512, 512);
            used = 512;
        }
    }
    fclose(file);
    return 0;
}

static void Check(int condition, const char *message, int *failed)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); (*failed)++; }
}

int main(void)
{
    static const char *directCompositionDraws[] = {
        "core/composition/common/vc_light_shaft.inl",
        "core/composition/common/vc_rune_circle.inl",
        "core/composition/common/vc_ground_wave.inl",
        "core/composition/common/vc_shock_ring.inl",
        "core/composition/common/vc_portal_disc.inl",
        "core/composition/common/vc_energy_orb.inl",
        "core/composition/common/vc_sweep_slash.inl",
        "core/composition/common/vc_lightning_arc.inl",
        "core/composition/common/vc_dissolve_exit.inl",
        "core/composition/common/vc_debris_shards.inl",
        "core/composition/common/vc_shield_shell.inl",
        "core/composition/earth/fissure_streak.inl",
        "core/composition/taiji/vc_black_hole.inl",
        "core/composition/water/water_stream.inl",
    };
    int failed = 0;

    Check(Has("core/vfx_render.h", "VFX_RENDER_PASS_BODY"),
          "one shared semantic-pass enum must exist", &failed);
    Check(Has("core/vfx_render.h", "VFXRender_BeginPass"),
          "managers must enter the HDR scene through one pass API", &failed);
    Check(Has("core/vfx_render.h", "VFXRender_BeginDraw"),
          "direct geometry must use one target/blend/depth scope", &failed);
    Check(Has("core/vfx_render.h", "VFXRender_BeginAppearance"),
          "appearance must resolve and enter a semantic pass in one operation", &failed);
    Check(Has("core/scene_targets.c", "VFXRender_BeginDraw"),
          "the scene-target owner must implement the unified scope", &failed);
    Check(Has("core/scene_targets.c", "BLEND_ALPHA_PREMULTIPLY"),
          "the unified scope must preserve premultiplied coverage", &failed);
    Check(Has("core/scene_targets.c", "pass == VFX_RENDER_PASS_EMISSION") &&
          Has("core/scene_targets.c", "VFX_SURFACE_ADDITIVE : resolved.surface"),
          "semantic emission from a dual-layer appearance must use additive radiance", &failed);

    Check(Has("main.c", "VFXRender_BeginPass(VFX_RENDER_PASS_BODY)"),
          "main must use the unified body pass", &failed);
    Check(Has("main.c", "VFXRender_BeginPass(VFX_RENDER_PASS_EMISSION)"),
          "main must use the unified emission pass", &failed);
    Check(!Has("main.c", "SceneTargets_BeginVFXBody();"),
          "main must not bypass the unified renderer", &failed);
    Check(!Has("main.c", "SceneTargets_BeginVFXEmission();"),
          "main must not bypass the unified renderer", &failed);

    Check(Has("core/ribbon_strip.h", "DrawRibbonStripAppearanceEx"),
          "standalone ribbons need an appearance-aware high-level path", &failed);
    Check(Has("core/ribbon_strip.c", "VFXRender_BeginAppearance"),
          "appearance-aware ribbons must use the shared scope", &failed);

    for (size_t i = 0; i < sizeof(directCompositionDraws) / sizeof(directCompositionDraws[0]); ++i) {
        Check(!Has(directCompositionDraws[i], "ScreenDistort_BeginVFX"),
              directCompositionDraws[i], &failed);
        Check(Has(directCompositionDraws[i], "VFXRender_BeginDraw") ||
              Has(directCompositionDraws[i], "VFXRender_BeginAppearance"),
              directCompositionDraws[i], &failed);
    }

    Check(!Has("core/afterimage.c", "SceneTargets_BeginVFXBody();"),
          "afterimages must use the shared scope", &failed);
    Check(Has("core/afterimage.c", "VFXRender_BeginDraw"),
          "afterimages must declare their surface centrally", &failed);

    Check(Has("skills/fire/fire_ball/fire_skill.c", "VFXRender_BeginDraw") &&
          !Has("skills/fire/fire_ball/fire_skill.c", "ScreenDistort_BeginVFX"),
          "fire skill must not bypass the shared renderer", &failed);
    Check(Has("skills/metal/volume_smoke_skill/volume_smoke_skill.c", "VFXRender_BeginDraw") &&
          !Has("skills/metal/volume_smoke_skill/volume_smoke_skill.c", "ScreenDistort_BeginVFX"),
          "volume smoke must not bypass the shared renderer", &failed);
    Check(Has("skills/earth/stone_prison_skill/stone_prison_skill.c", "VFXRender_BeginDraw") &&
          !Has("skills/earth/stone_prison_skill/stone_prison_skill.c", "rlDisableDepthMask"),
          "standalone skill decals must not hand-roll depth state", &failed);

    puts(failed ? "vfx unified render contract: FAIL"
                : "vfx unified render contract: PASS");
    return failed != 0;
}

/* Ensures scene/VFX layer separation stays wired through the renderer. */
#include <stdio.h>
#include <string.h>

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
    int bad = 0;

    bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXBody");
    bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXEmission");
    bad += !Has("core/screen_distort.c", "vfxBodyTex");
    bad += !Has("core/screen_distort.c", "vfxEmissionTex");
    bad += !Has("core/screen_distort.c", "UnloadColorLayerTarget");
    bad += !Has("core/screen_distort.c", "SOFT_DEPTH_DOWNSCALE 2");
    bad += !Has("core/screen_distort.c", "ScreenDistort_RequestSoftDepthRegion");
    bad += !Has("core/screen_distort.c", "s_vfxLayersActive");
    bad += !Has("core/screen_distort.c", "ScreenDistort_BeginLayer(vfxEmissionTex, &s_vfxEmissionCleared);");
    bad += !Has("core/screen_distort.c", "s_vfxBodyUsed = false;");
    bad += !Has("core/screen_distort.c", "s_vfxEmissionUsed = false;");
    bad += !Has("core/shaders/distortion.fs", "u_hasVfxLayers");
    bad += !Has("core/screen_distort.c", "rlFramebufferAttach(target.id, 0, RL_ATTACHMENT_DEPTH");
    bad += !Has("core/shaders/distortion.fs", "u_vfxBodyTex");
    bad += Has("core/shaders/distortion.fs", "u_vfxEmissionTex");
    bad += !Has("core/shaders/distortion.fs", "float bodyCoverage = 1.0 - pow(1.0 - body.a, 6.0);");
    bad += !Has("core/shaders/distortion.fs", "bodyColor * bodyCoverage");
    bad += !Has("core/screen_distort.c", "BeginBlendMode(BLEND_ADD_COLORS);");

    bad += !Has("core/composition/common/vc_light_shaft.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_rune_circle.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_ground_wave.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_shock_ring.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_portal_disc.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_energy_orb.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/common/vc_sweep_slash.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/earth/fissure_streak.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/taiji/vc_black_hole.inl", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("core/composition/water/water_stream.inl", "ScreenDistort_BeginVFXBody();");
    bad += !Has("core/composition/water/water_stream.inl", "ScreenDistort_EndVFXLayer();");
    bad += !Has("core/composition/common/vc_dissolve_exit.inl", "ScreenDistort_BeginVFXBody();");
    bad += !Has("core/composition/common/vc_dissolve_exit.inl", "ScreenDistort_EndVFXLayer();");
    bad += !Has("core/composition/common/vc_debris_shards.inl", "ScreenDistort_BeginVFXBody();");
    bad += !Has("core/composition/common/vc_debris_shards.inl", "ScreenDistort_EndVFXLayer();");
    bad += !Has("core/composition/common/vc_shield_shell.inl", "ScreenDistort_BeginVFXBody();");
    bad += !Has("core/composition/common/vc_shield_shell.inl", "ScreenDistort_EndVFXLayer();");
    bad += !Has("core/afterimage.c", "ScreenDistort_BeginVFXBody();");
    bad += !Has("skills/metal/volume_smoke_skill/volume_smoke_skill.c", "ScreenDistort_BeginVFXBody();");
    bad += !Has("skills/fire/fire_ball/fire_skill.c", "ScreenDistort_BeginVFXEmission();");
    bad += !Has("skills/fire/fire_ball/fire_skill.c", "ScreenDistort_EndVFXLayer();");
    bad += !Has("core/atmosphere.c", "Ambient motes are low-energy background decoration");
    bad += !Has("main.c", "CompositeScreenSpaceVFX(camera);");
    bad += !Has("main.c", "MetaballFX_Composite();");
    bad += !Has("main.c", "FluidSurface_Composite();");
    bad += !Has("core/trails/trail_system.c", "void DrawTrailEntitiesBody(Camera3D camera)");
    bad += !Has("core/trails/trail_system.c", "void DrawTrailEntitiesEmission(Camera3D camera)");
    bad += !Has("core/particles/particle_system.c", "if (layerFilter == 0 && p->blendMode == VFX_BLEND_ADDITIVE) continue;");
    bad += !Has("core/trails/trail_system.c", "BlendMode bm = (layerFilter == 0) ? BLEND_ALPHA : sourceBm;");
    bad += !Has("core/trails/trail_system.c", "TrailUsesAdditiveBlend(t) &&");
    bad += !Has("core/trails/trail_system.c", "EnsureTrailBodyShader();");
    bad += !Has("core/trails/shaders/trail_body.fs", "sheet.rgb * fragColor.rgb * colDiffuse.rgb * 1.75");
    bad += !Has("main.c", "DrawDecalVFXLayers(camera);");
    bad += !Has("main.c", "DrawParticleTrailVFXLayers(camera, globalParticleTex);");
    bad += !Has("main.c", "if (g_debugHideParticles && g_debugHideTrails) return;");
    bad += !Has("main.c", "Particle/trail bodies share the same contrast-protected VFXBody target");
    bad += !Has("main.c", "ScreenDistort_BeginVFXBody();");
    bad += !Has("main.c", "Ribbons are already lifted in the HDR body shader");
    bad += !Has("main.c", "DecalSystem_DrawBody();");
    bad += !Has("main.c", "DecalSystem_DrawEmission();");
    bad += !Has("core/decals/decal_system.c", "bool DecalSystem_HasEmission(void)");
    bad += !Has("core/composition/common/vc_energy_burst.inl", "ParticleConfig bodyParticle =");
    bad += !Has("core/composition/common/vc_energy_burst.inl", ".render.blendMode = VFX_BLEND_ALPHA");
    bad += !Has("core/composition/common/vc_energy_burst.inl", "ParticleConfig accentParticle = bodyParticle;");
    bad += !Has("core/composition/common/vc_energy_burst.inl", "accentParticle.render.blendMode = VFX_BLEND_ADDITIVE;");
    bad += !Has("core/vfx_config.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/particles/particle_system.c", "VFXContrast_ApplyColor(");
    bad += !Has("core/particles/particle_manager.c", "VFXContrast_ApplyEmissionIntensity(");
    bad += !Has("core/ribbon_strip.h", "DrawRibbonStripProfiledEx");
    bad += !Has("core/trails/trail_system.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/decals/decal_system.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/composition/common/vc_energy_burst.inl", ".render.contrastProfile = VFX_CONTRAST_ENERGY");
    bad += !Has("core/composition/common/vc_strand_trail.inl", "VFX_CONTRAST_SMOKE");
    bad += !Has("core/composition/common/vc_decal.inl", ".contrastProfile = contrastProfile");

    puts(bad ? "vfx render layers: FAIL" : "vfx render layers: PASS");
    return bad;
}

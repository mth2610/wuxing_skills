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

    // The producer API survives the retirement of the split layers: call sites
    // still declare whether they occlude (body) or only add light (emission),
    // and the particle/trail systems route on exactly that distinction.
    bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXBody");
    bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXEmission");
    bad += !Has("core/screen_distort.c", "SOFT_DEPTH_DOWNSCALE 2");
    bad += !Has("core/screen_distort.c", "ScreenDistort_RequestSoftDepthRegion");

    // The two render targets and their composite are GONE. Measured equivalent
    // to drawing straight into the scene (alpha-over is associative and the
    // composite's rgb/a divide is an exact round trip), so re-introducing them
    // costs two full-screen R16F targets and a composite pass for no image
    // difference. These assertions are negative on purpose: they fail if the
    // machinery comes back rather than if it stays away.
    bad += Has("core/screen_distort.c", "vfxBodyTex");
    bad += Has("core/screen_distort.c", "vfxEmissionTex");
    bad += Has("core/screen_distort.c", "LoadColorLayerTarget");
    bad += Has("core/screen_distort.c", "s_vfxLayersActive");
    bad += Has("core/shaders/distortion.fs", "u_hasVfxLayers");
    bad += Has("core/shaders/distortion.fs", "u_vfxBodyTex");
    bad += Has("core/shaders/distortion.fs", "u_vfxEmissionTex");
    bad += Has("core/shaders/distortion.fs", "bodyColor * bodyCoverage");
    // BLEND_ADD_COLORS on a full-screen VFX layer is what discarded coverage
    // and made every emissive effect a milky film over a bright background.
    bad += Has("core/screen_distort.c", "BeginBlendMode(BLEND_ADD_COLORS);");

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
    // Đợt H: ADDITIVE is the ONLY mode that goes to the emission layer.
    // PREMULTIPLIED emits too but is drawn in the BODY pass, because the
    // emission layer is composited with BLEND_ADD_COLORS and therefore discards
    // coverage, while the body composite is already premultiplied-over. These
    // two lines and ParticleSystem_HasAdditiveParticles must agree — when they
    // disagreed, the particles were filtered out of body and then the emission
    // pass was skipped as empty, and nothing drew at all.
    bad += !Has("core/particles/particle_system.c", "if (layerFilter == 0 && p->blendMode == VFX_BLEND_ADDITIVE) continue;");
    bad += !Has("core/particles/particle_system.c", "if (layerFilter == 1 && p->blendMode != VFX_BLEND_ADDITIVE) continue;");
    bad += !Has("core/particles/particle_system.c", "? VFX_BLEND_PREMULTIPLIED");
    bad += !Has("core/trails/trail_system.c", "BlendMode bm = (layerFilter == 0) ? BLEND_ALPHA : sourceBm;");
    bad += !Has("core/trails/trail_system.c", "TrailUsesAdditiveBlend(t) &&");
    bad += !Has("core/trails/trail_system.c", "EnsureTrailBodyShader();");
    bad += !Has("core/trails/shaders/trail_body.fs", "VFXContrast_CoreMask(sheet.a, u_contrastParams)");
    bad += !Has("core/trails/shaders/trail_body.fs", "mix(1.0, coreGain, coreMask)");
    bad += !Has("main.c", "DrawDecalVFXLayers(camera);");
    bad += !Has("main.c", "DrawParticleTrailVFXLayers(camera, globalParticleTex);");
    bad += !Has("main.c", "if (g_debugHideParticles && g_debugHideTrails) return;");
    bad += !Has("main.c", "preserves stored coverage linearly");
    bad += !Has("main.c", "ScreenDistort_BeginVFXBody();");
    bad += !Has("main.c", "if (hasEmissionTrails) DrawTrailEntitiesEmission(camera);");
    bad += !Has("main.c", "DecalSystem_DrawBody();");
    bad += !Has("main.c", "DecalSystem_DrawEmission();");
    bad += !Has("core/decals/decal_system.c", "bool DecalSystem_HasEmission(void)");
    // Đợt H: the energy burst is ONE population of pure light. The alpha body
    // plus additive accent it used to have is gone on purpose — an explosion of
    // energy has no soot, so it must never occlude, and the coloured "contrast"
    // body was a tinted smoke puff standing in for per-texel colour that the
    // ramp LUT now provides properly. These are negative assertions: they fail
    // if the two-population build comes back.
    bad += Has("core/composition/common/vc_energy_burst.inl", "ParticleConfig accentParticle = bodyParticle;");
    bad += Has("core/composition/common/vc_energy_burst.inl", "accentParticle.render.blendMode = VFX_BLEND_ADDITIVE;");
    // ADDITIVE, not premultiplied. Pure light has no silhouette, so it belongs
    // in the emission pass; premultiplied is routed to BODY and drawing this
    // there cut horizontal bands out of the burst.
    bad += !Has("core/composition/common/vc_energy_burst.inl", ".render.blendMode = VFX_BLEND_ADDITIVE,");
    bad += Has("core/composition/common/vc_energy_burst.inl", ".render.blendMode = VFX_BLEND_PREMULTIPLIED,");
    // smokeGain 0 is the declaration that makes it pure light: the shader then
    // emits alpha 0 and premultiplied blending degenerates to exact addition.
    bad += !Has("core/composition/common/vc_energy_burst.inl", ".render.smokeGain = 0.0f,");
    bad += !Has("core/particles/shaders/particle_lit.fs", "if (u_smokeGain <= 0.0)");
    bad += !Has("core/vfx_config.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/particles/particle_system.c", "VFXContrast_ApplyColor(");
    bad += !Has("core/particles/particle_manager.c", "VFXContrast_ApplyEmissionIntensity(");
    bad += !Has("core/ribbon_strip.h", "DrawRibbonStripProfiledEx");
    bad += !Has("core/trails/trail_system.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/decals/decal_system.h", "VFXContrastProfileId contrastProfile;");
    bad += !Has("core/composition/common/vc_energy_burst.inl", ".render.contrastProfile = VFX_CONTRAST_ENERGY");
    bad += !Has("core/composition/common/vc_trail.inl", "VFX_CONTRAST_SMOKE");
    bad += !Has("core/composition/common/vc_decal.inl", ".contrastProfile = contrastProfile");

    puts(bad ? "vfx render layers: FAIL" : "vfx render layers: PASS");
    return bad;
}

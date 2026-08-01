/* Ensures scene/VFX layer separation stays wired through the renderer. */
#include <stdio.h>
#include <string.h>
static int Has(const char *p, const char *n) { FILE *f=fopen(p,"rb"); char b[1024]; size_t u=0,k; if(!f)return 0; while((k=fread(b+u,1,sizeof(b)-1-u,f))){u+=k;b[u]=0;if(strstr(b,n)){fclose(f);return 1;}if(u>256){memmove(b,b+u-256,256);u=256;}}fclose(f);return 0; }
int main(void) {
  int bad=0;
  bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXBody");
  bad += !Has("core/screen_distort.h", "ScreenDistort_BeginVFXEmission");
  bad += !Has("core/screen_distort.c", "vfxBodyTex");
  bad += !Has("core/screen_distort.c", "vfxEmissionTex");
  bad += !Has("core/screen_distort.c", "UnloadColorLayerTarget");
  bad += !Has("core/screen_distort.c", "s_vfxLayersActive");
  bad += !Has("core/screen_distort.c", "ScreenDistort_BeginLayer(vfxEmissionTex, &s_vfxEmissionCleared);");
  bad += !Has("core/shaders/distortion.fs", "u_hasVfxLayers");
  bad += !Has("core/screen_distort.c", "rlFramebufferAttach(target.id, 0, RL_ATTACHMENT_DEPTH");
  bad += !Has("core/shaders/distortion.fs", "u_vfxBodyTex");
  bad += Has("core/shaders/distortion.fs", "u_vfxEmissionTex");
  bad += !Has("core/shaders/distortion.fs", "float bodyCoverage = 1.0 - pow(1.0 - body.a, 6.0);");
  bad += !Has("core/shaders/distortion.fs", "bodyColor * bodyCoverage");
  bad += !Has("core/screen_distort.c", "BeginBlendMode(BLEND_ADD_COLORS);");
  bad += !Has("core/composition/common/vc_beam.inl", "ScreenDistort_BeginVFXEmission();");
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
  bad += !Has("core/atmosphere.c", "ScreenDistort_BeginVFXEmission();");
  bad += !Has("main.c", "MetaballFX_Prepare(camera");
  bad += !Has("main.c", "MetaballFX_Composite();");
  bad += !Has("main.c", "FluidSurface_Composite();");
  bad += !Has("core/trails/trail_system.c", "void DrawTrailEntitiesBody(Camera3D camera)");
  bad += !Has("core/trails/trail_system.c", "void DrawTrailEntitiesEmission(Camera3D camera)");
  bad += !Has("core/particles/particle_system.c", "int drawBlend = (layerFilter == 0) ? VFX_BLEND_ALPHA : p->blendMode;");
  bad += !Has("core/particles/particle_manager.c", "GPU particles share the same generic body/emission law");
  bad += !Has("main.c", "DrawTrailEntitiesBody(camera);");
  bad += !Has("main.c", "DrawTrailEntitiesEmission(camera);");
  bad += !Has("main.c", "DecalSystem_Draw();");
  bad += !Has("main.c", "ScreenDistort_BeginVFXBody();");
  puts(bad ? "vfx render layers: FAIL" : "vfx render layers: PASS"); return bad;
}

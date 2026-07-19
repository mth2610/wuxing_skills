// vc_archetype.inl — Stateful managed VFX archetypes orchestrator
// Included once by visual_composer.c. All state is private to this TU.

static Color Arch_ElementColor(EffectPresetType e)
{
    switch (e) {
    case EFFECT_PRESET_WATER_SPLASH:  return ELEMENT_COLOR_WATER;
    case EFFECT_PRESET_WOOD_BLOOM:    return ELEMENT_COLOR_WOOD;
    case EFFECT_PRESET_FIRE_EXPLOSION:return ELEMENT_COLOR_FIRE;
    case EFFECT_PRESET_EARTH_CRACK:   return ELEMENT_COLOR_EARTH;
    case EFFECT_PRESET_METAL_SHARD:   return ELEMENT_COLOR_METAL;
    case EFFECT_PRESET_TAIJI_BURST:   return ELEMENT_COLOR_TAIJI;
    default: return WHITE;
    }
}

static ColorGradient s_shardSparkleGrad = {0};
static bool s_shardSparkleInit = false;

static void ShardSparkle_Init(void) {
    if (s_shardSparkleInit) return;
    ColorGradient_AddStop(&s_shardSparkleGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_shardSparkleGrad, 0.15f, (Color){220, 245, 255, 255});
    ColorGradient_AddStop(&s_shardSparkleGrad, 1.0f, (Color){100, 180, 255, 0});
    s_shardSparkleInit = true;
}

// Include modular visual composer archetypes
#include "common/vc_proc_beam.inl"
#include "common/vc_ground_wave.inl"
#include "common/vc_orbital.inl"
#include "common/vc_aura_ring.inl"
#include "common/vc_chain_lightning.inl"
#include "common/vc_smoke_column.inl"
#include "common/vc_shard_debris.inl"
#include "common/vc_crown_splash.inl"
#include "common/vc_mesh_electricity.inl"
#include "common/vc_particle_upgrades_test.inl"
#include "common/vc_trail_upgrades_test.inl"

static void VC_Archetype_Update(float dt)
{
    VC_ProcBeam_Update(dt);
    VC_GroundWave_Update(dt);
    VC_Orbital_Update(dt);
    VC_AuraRing_Update(dt);
    VC_ChainLightning_Update(dt);
    VC_SmokeColumn_Update(dt);
    VC_ShardDebris_Update(dt);
    VC_CrownSplash_Update(dt);
    VC_MeshElectricity_Update(dt);
}

static void VC_Archetype_Draw3D(Camera3D cam)
{
    VC_ProcBeam_Draw3D(cam);
    VC_GroundWave_Draw3D(cam);
    VC_Orbital_Draw3D(cam);
    VC_AuraRing_Draw3D(cam);
    VC_ChainLightning_Draw3D(cam);
    VC_SmokeColumn_Draw3D(cam);
    VC_ShardDebris_Draw3D(cam);
    VC_CrownSplash_Draw3D(cam);
    VC_MeshElectricity_Draw3D(cam);
}

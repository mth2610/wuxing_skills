#include "visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/camera_fx.h"
#include "core/camera_context.h"
#include "environment/environment_system.h"
#include "core/ribbon_strip.h"
#include "core/path_spline.h"
#include "core/screen_distort.h"
#include "core/time_fx.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/geometry/mesh_cache.h"
#include "core/material/material_system.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/vfx_proc_ray.h"
#include "core/emitter_system.h"
#include "core/color_gradient.h"
#include "core/utils_math.h"
#include "core/mesh_adjacency.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// Include modular visual composer implementations (grouped by element/functionality)
#include "common/common.inl"

#include "metal/metal.inl"
#include "wood/wood.inl"
#include "water/water.inl"
#include "fire/fire.inl"
#include "earth/earth.inl"
#include "plasma/plasma.inl"
#include "taiji/taiji.inl"

// ─── Managed archetypes ──────────────────────────────────────────────────────
// Unlike the fire-and-forget compositions above, each of these owns a private
// pool and is driven every frame by the two entry points below. They are listed
// in the manifest's `exclude_from_auto_include` so common.inl leaves them alone
// — this file is their single include site.
//
// TO DELETE ONE: remove its .inl, then its #include here and its two calls
// below. All three live in this one file, adjacent and in matching order, so
// grepping the archetype name here finds every reference.
// (They were previously routed through a vc_archetype.inl orchestrator, which
// made this a SECOND, easy-to-miss include site: a deletion had to be mirrored
// in two files, and a missed dispatch call failed with an error that named a
// symbol rather than the file that was removed.)
#include "common/vc_proc_beam.inl"
#include "common/vc_ground_wave.inl"
#include "common/vc_orbital.inl"
#include "common/vc_aura_ring.inl"
#include "common/vc_chain_lightning.inl"
#include "common/vc_smoke_column.inl"
#include "common/vc_shard_debris.inl"
#include "common/vc_crown_splash.inl"
#include "common/vc_mesh_electricity.inl"
// No Update/Draw pair — included for their VFX_Compose* entry points only:
#include "common/vc_particle_upgrades_test.inl"
#include "common/vc_trail_upgrades_test.inl"

void VFX_Compose_Update(float dt)
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

void VFX_Compose_Draw3D(Camera3D cam)
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
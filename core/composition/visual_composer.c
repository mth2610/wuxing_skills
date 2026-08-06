#include "visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/particles/particle_system.h"
#include "core/decals/decal_system.h"
#include "core/fluid/fluid_impact.h"
#include "core/particles/particle_manager.h"
#include "core/fluid/fluid_surface.h"
#include "core/vfx_light.h"
#include "core/trails/trail_system.h"
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
#include "core/vfx_surface_registry.h"
#include "core/skill_manager.h"
#include "core/map_manager.h"   // H2 ground wave: VFX_GroundHeightFromMap
#include "core/gfx_quality.h"
#include "core/emitter_system.h"
#include "core/composition/vfx_sequence.h"
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

// The surviving compositions. F0 (28/07/2026) deleted the rest; see
// visual_composer.h for what remains and why.
#include "common/common.inl"
#include "fire/flame_volume.inl"   // F3 — reads vc_smoke_puff.inl's sheet, so it comes after it

// ── Restored after F0 (owner, 28/07/2026) ───────────────────────────────────
// Brought back to rebuild the two water skills. These are pre-Đợt-E effects and
// are NOT part of the survivor set: they are here to be rewritten against, not
// kept. A `.inl` that exists but is not included compiles into nothing and fails
// at LINK time with an undefined symbol — which is exactly how this batch
// arrived, so restoring a file means restoring its include too.
#include "water/water.inl"         // -> ice_crystal.inl, water_stream.inl
#include "earth/earth.inl"         // -> stone_pillar.inl, fissure_streak.inl
#include "metal/metal.inl"
#include "wood/wood.inl"
#include "taiji/vc_black_hole.inl"

// The only POOLED component left. Unlike a fire-and-forget composition it owns
// state and is driven by the two entry points below; both live in this one file,
// adjacent and in matching order, so a deletion cannot be half-done.
// @gen:archetype_includes begin
#include "common/vc_character_aura.inl"
#include "common/vc_ribbon_trail.inl"
#include "common/vc_projectile.inl"
#include "common/vc_volume_trail.inl"
#include "common/vc_debris_shards.inl"
#include "common/vc_ember_trail.inl"
#include "common/vc_shield_shell.inl"
#include "common/vc_smoke_column.inl"
#include "common/vc_smoke_trail.inl"
#include "common/vc_beam.inl"
// @gen:archetype_includes end

void VFX_Compose_Update(float dt)
{
    (void)dt;
// @gen:archetype_update begin
    VC_CharacterAura_Update(dt);
    VC_SweptTrail_Update(dt);
    VC_Projectile_Update(dt);
    VC_VolumeTrail_Update(dt);
    VC_DebrisShards_Update(dt);
    VC_EmberTrail_Update(dt);
    VC_ShieldShell_Update(dt);
    VC_SmokeColumn_Update(dt);
    VC_SmokeTrail_Update(dt);
    VC_Beam_Update(dt);
    VC_FlameEmitter_Update(dt);
// @gen:archetype_update end
    SmokeEmitter_Update(dt);
    // E3 — the choreography layer rides the same single main.c wiring. Kept
    // OUTSIDE the generated block on purpose: sync_vfx_test.py rewrites what is
    // between the markers from the archetype scan, so a hand-written call in
    // there is deleted by the next sync.
    // `dt` is already post-TimeFX_Apply, the scaled clock sequences run on;
    // VFX_Sequence_Update takes the raw one itself for `unscaled` beats.
    VFX_Sequence_Update(dt);
    WaterOrb_Update(dt);
}

void VFX_Compose_Draw3D(Camera3D cam)
{
    (void)cam;
// @gen:archetype_draw begin
    VC_CharacterAura_Draw3D(cam);
    VC_SweptTrail_Draw3D(cam);
    VC_Projectile_Draw3D(cam);
    VC_VolumeTrail_Draw3D(cam);
    VC_DebrisShards_Draw3D(cam);
    VC_EmberTrail_Draw3D(cam);
    VC_ShieldShell_Draw3D(cam);
    VC_SmokeColumn_Draw3D(cam);
    VC_SmokeTrail_Draw3D(cam);
    VC_Beam_Draw3D(cam);
    VC_FlameEmitter_Draw3D(cam);
// @gen:archetype_draw end
    SmokeEmitter_Draw3D(cam);
}

/* Screen-space VFX producers that must submit their particle streams before the
 * pending-fluids check. Called from main.c's CompositeScreenSpaceVFX alongside
 * FluidImpact_Draw(). Not an archetype and never fixture-scanned: static workers
 * inside the .inl it drives are out of reach of main.c, so this is the bridge. */
void VFX_Compose_SubmitScreenSpaceVFX(void)
{
    WaterOrb_SubmitSurface();
}

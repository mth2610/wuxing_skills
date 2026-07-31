// common.inl — master include for the surviving compositions.
// Included once by visual_composer.c.
//
// ORDER MATTERS in exactly two places, because these are pasted into ONE
// translation unit and later files read earlier files' statics:
//   - vc_common.inl first: it defines the shared draw/colour helpers.
//   - vc_smoke_puff.inl before vc_energy_burst.inl and fire/flame_volume.inl:
//     both call SmokePuff_InitShared() and read its sheet.
// Everything else here is independent.
//
// F0 (28/07/2026) deleted every composition predating the Đợt E/F rebuild; what
// is left is this list. The `@gen` block is still machine-managed — add the new
// VFX to scripts/vfx_test_manifest.json and run scripts/sync_vfx_test.py rather
// than editing it here, or the next sync will overwrite the edit.

// @gen:common_includes begin
// 22 include(s) — auto-managed by sync_vfx_test.py
#include "vc_common.inl"
#include "vc_smoke_puff.inl"
#include "vc_energy_burst.inl"
#include "vc_impact_package.inl"
#include "vc_glint_sparkle.inl"
#include "vc_rune_circle.inl"
#include "vc_core_glow.inl"
#include "vc_energy_orb.inl"
#include "vc_charge_converge.inl"
#include "vc_dissolve_exit.inl"
#include "vc_sweep_slash.inl"
#include "vc_light_shaft.inl"
#include "vc_particle_upgrades_test.inl"
#include "vc_ground_wave.inl"
#include "vc_spark_trail.inl"
#include "vc_converge_motes.inl"
#include "vc_beam.inl"
#include "vc_portal_disc.inl"
#include "vc_shock_ring.inl"
#include "vc_core_smoketrail.inl"
#include "vc_impact_dust.inl"
#include "vc_contact_spark.inl"
// @gen:common_includes end


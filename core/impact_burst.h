#ifndef CORE_IMPACT_BURST_H
#define CORE_IMPACT_BURST_H

#include "raylib.h"
#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/particle_radial_burst.h"

/* ============================================================================
 * IMPACT BURST (NEW MODULE)
 * ----------------------------------------------------------------------------
 * Purpose: factor out the recurring 4-step "collision/impact VFX combo"
 * seen at the end of every projectile skill (e.g. TriggerWaterBurst in
 * tube_skill.c, TriggerImpactVFX in the section-11 template):
 *
 *   1) ScreenDistort_Add(pos, ...)      - heat-shimmer / shockwave ring
 *   2) DecalSystem_Add(pos, ...)        - ground mark (random rotation)
 *   3) VFXLight_Spawn(pos, ...)         - brief point light flash
 *   4) ParticleSystem_SpawnRadialBurst  - particles flying outward
 *
 * Every element-specific impact (water splash, fire embers, earth debris,
 * metal sparks...) is the same 4 steps with different numbers. A skill
 * declares one static ImpactBurstConfig per impact "flavor" it needs and
 * calls VFX_TriggerImpactBurst() at the collision point.
 *
 * This module depends on core/particle_radial_burst.h (step 4) and
 * core/color_gradient.h (gradient passed through to the particle burst).
 * It does NOT replace ScreenDistort/DecalSystem/VFXLight - it orchestrates
 * them in the standard order with standard scaling-by-sizeScale behavior.
 *
 * NOTE ON ENCODING: ASCII-only comments. See core/procedural_mesh_utils.h.
 * ==========================================================================*/

typedef struct {
    /* --- Step 1: screen distortion --- */
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;

    /* --- Step 2: ground decal --- */
    bool     decalEnabled;
    Texture2D decalTex;
    float     decalScale;   /* multiplied by sizeScale at call time */
    float     decalLife;
    Color     decalTint;
    bool      decalRandomRotation; /* true = GetRandomValue(0,360), false = use decalFixedRotation */
    float     decalFixedRotation;

    /* --- Step 3: point light flash --- */
    bool  lightEnabled;
    Color lightColor;
    float lightRadius;  /* multiplied by sizeScale at call time */
    float lightLife;

    /* --- Step 4: radial particle burst --- */
    bool particlesEnabled;
    ParticleRadialBurstConfig particles;
} ImpactBurstConfig;

/*
 * Runs the full 4-step impact combo at `pos`, scaling distort/decal/light
 * radii and the particle burst by `sizeScale` (same convention as
 * tube_skill.c's TriggerWaterBurst). Any step with its *Enabled flag set
 * to false is skipped, so a skill can use this for e.g. a silent particle
 * burst with no decal.
 */
void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);

#endif /* CORE_IMPACT_BURST_H */

#ifndef CORE_PARTICLE_RADIAL_BURST_H
#define CORE_PARTICLE_RADIAL_BURST_H

#include "raylib.h"
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/color_gradient.h"

/* ============================================================================
 * PARTICLE RADIAL BURST (ADDITION)
 * ----------------------------------------------------------------------------
 * Append this into the existing core/particle_system.h (keep the existing
 * ParticleConfig struct and SpawnParticle() declaration in the original
 * file).
 *
 * Purpose: factor out the recurring "spawn N particles flying outward from
 * a point" loop seen in impact/burst VFX (e.g. TriggerWaterBurst in
 * tube_skill.c):
 *
 *   for (int s = 0; s < count; s++) {
 *       float angle = Random01() * PI * 2.0f;
 *       float pitch = (Random01() - 0.5f) * PI;
 *       float speed = Math_Mix(minSpeed, maxSpeed, Random01());
 *       velocity = (cosf(angle)*speed*cosf(pitch), sinf(pitch)*speed + upBias, sinf(angle)*speed*cosf(pitch));
 *       ... SpawnParticle(cfg);
 *   }
 *
 * This is a SPHERICAL burst when pitchRange = PI (full sphere of
 * directions, like tube_skill.c's water splash), or a FLAT/RING burst
 * when pitchRange = 0 (all particles fly outward in the horizontal plane
 * only, like the simpler template in section 11 of the API doc).
 *
 * NOTE ON ENCODING: ASCII-only comments. See core/procedural_mesh_utils.h.
 * ==========================================================================*/

typedef struct {
    int   countMin, countMax;       /* particle count range (use GetRandomValue internally) */
    float speedMin, speedMax;       /* outward speed range */
    float radiusMin, radiusMax;     /* particle visual radius range */
    float lifetimeMin, lifetimeMax; /* particle lifetime range, in seconds */
    float pitchRange;               /* 0 = flat/ring burst (horizontal plane only).
                                      * PI = full spherical burst (any direction).
                                      * Any value in between = a cone/dome spread. */
    float upwardBias;               /* added to vertical velocity after pitch is applied,
                                      * e.g. 100.0f to make a splash kick upward like water */

    Color colorStart, colorEnd;          /* used only if gradient == NULL */
    const ColorGradient *gradient;       /* preferred: overrides colorStart/colorEnd if set */
    const ForceField *forceField;        /* e.g. gravity+drag field applied after spawn */
} ParticleRadialBurstConfig;

/*
 * Spawns a radial burst of particles outward from `origin`, scaled by
 * `sizeScale` (multiplies countMax, speed, and radius the same way
 * tube_skill.c's TriggerWaterBurst scales by sizeScale).
 *
 * cfg fields are NOT modified. Pass cfg by pointer since it is typically a
 * static const config shared across casts of the same skill.
 */
void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);

#endif /* CORE_PARTICLE_RADIAL_BURST_H */

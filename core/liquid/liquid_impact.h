#ifndef CORE_LIQUID_IMPACT_H
#define CORE_LIQUID_IMPACT_H

// Event-driven water impacts.  Gameplay owns collision and submits an exact
// hit point/normal; the VFX path never depends on compute shaders for contact.

#include "raylib.h"
#include "core/liquid/liquid_motion.h"
#include <stdbool.h>

#define LIQUID_IMPACT_MAX_HERO_DROPLETS 48
#define LIQUID_IMPACT_MAX_BODIES 4

typedef enum {
    LIQUID_IMPACT_BACKEND_FORCE_FIELD = 0,
    LIQUID_IMPACT_BACKEND_PBD = 1
} LiquidImpactBackend;

typedef struct {
    Vector3 position;
    Vector3 normal;
} LiquidImpactCollision;

// Return true when the swept droplet from `from` to `to` hits a receiver.
// Maps/physics own this query; VFX only consumes its result. The callback must
// be deterministic and must not retain the output pointer.
typedef bool (*LiquidImpactCollisionQueryFn)(Vector3 from, Vector3 to,
                                            float radius,
                                            LiquidImpactCollision *outHit,
                                            void *userData);

typedef struct {
    Vector3 hitPoint;
    Vector3 hitNormal;         // normalized internally; zero means world up
    Vector3 impulseDirection;  // splash bias; zero derives from hitNormal
    Vector3 initialVelocity;   // incoming water-body velocity in m/s; zero uses legacy bias
    float force01;             // clamped [0,1], controls count and radius
    float scale;               // metres; <= 0 selects 1.0
    /* Optical identity supplied by the VFX material owner. All-zero RGB
     * fields fall back to VC_MAT_WATER for source compatibility. */
    Color bodyColor;
    Color glowColor;
    Color softColor;
    /* Compatibility override: true always selects force fields, even when
     * backend requests PBD. Zero-initialized events now also use force fields. */
    bool forceFieldOnly;
    /* The caller continues to render the same incoming particle body. Core
     * only creates residue/material state; it must not seed another volume. */
    bool externalBody;
    /* PBD is opt-in and initialized lazily on the first admitted PBD impact.
     * Unavailable/busy PBD falls back to a force-field body. External bodies
     * never initialize or spawn either backend. */
    LiquidImpactBackend backend;
    /* Zero is water. Selects both canonical motion and optical class; any
     * non-zero body/glow/soft colors above override only those colour lanes. */
    LiquidMotionProfile motionProfile;
} LiquidImpactEvent;

// One-shot water impact. Safe on both compute/SSBO and CPU/VBO particle paths.
// Call only from a collision/state transition, never from a draw loop.
void LiquidImpact_SpawnWater(const LiquidImpactEvent *event);

// Optional swept physics/world collision hook for bounded CPU hero droplets.
// NULL restores a bounded active-map terrain query (eight probes + six TOI
// refinements); narrow features can be missed. Walls/props require this hook.
// Coherent GPU ForceField/PBD bodies currently collide only with their authored
// receiver plane; this callback does not imply GPU world-collision support.
void LiquidImpact_SetCollisionQuery(LiquidImpactCollisionQueryFn query, void *userData);

// Engine lifecycle: main.c calls these once per frame inside the update/3D draw
// phases. Skill/gameplay code only calls LiquidImpact_SpawnWater.
// Update must precede ParticleManager_Update so timestep-averaged body fields
// describe the interval the particle manager is about to integrate.
void LiquidImpact_Update(float dt);
void LiquidImpact_Draw(void);
void LiquidImpact_GetStats(int *active, int *max);

#endif // CORE_LIQUID_IMPACT_H

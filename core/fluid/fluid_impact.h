#ifndef CORE_FLUID_IMPACT_H
#define CORE_FLUID_IMPACT_H

// Event-driven water impacts.  Gameplay owns collision and submits an exact
// hit point/normal; the VFX path never depends on compute shaders for contact.

#include "raylib.h"
#include "core/fluid/fluid_motion.h"
#include <stdbool.h>

#define FLUID_IMPACT_MAX_HERO_DROPLETS 48
#define FLUID_IMPACT_MAX_BODIES 4

typedef enum {
    FLUID_IMPACT_BACKEND_FORCE_FIELD = 0,
    FLUID_IMPACT_BACKEND_PBD = 1
} FluidImpactBackend;

typedef struct {
    Vector3 position;
    Vector3 normal;
} FluidImpactCollision;

// Return true when the swept droplet from `from` to `to` hits a receiver.
// Maps/physics own this query; VFX only consumes its result. The callback must
// be deterministic and must not retain the output pointer.
typedef bool (*FluidImpactCollisionQueryFn)(Vector3 from, Vector3 to,
                                            float radius,
                                            FluidImpactCollision *outHit,
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
    FluidImpactBackend backend;
    /* Zero is water. Selects both canonical motion and optical class; any
     * non-zero body/glow/soft colors above override only those colour lanes. */
    FluidMotionProfile motionProfile;
} FluidImpactEvent;

// One-shot water impact. Safe on both compute/SSBO and CPU/VBO particle paths.
// Call only from a collision/state transition, never from a draw loop.
void FluidImpact_SpawnWater(const FluidImpactEvent *event);

// Optional physics/world collision hook. NULL restores the active-map ground
// query, which supports terrain/heightmap receivers but not walls or props.
void FluidImpact_SetCollisionQuery(FluidImpactCollisionQueryFn query, void *userData);

// Engine lifecycle: main.c calls these once per frame inside the update/3D draw
// phases. Skill/gameplay code only calls FluidImpact_SpawnWater.
void FluidImpact_Update(float dt);
void FluidImpact_Draw(void);
void FluidImpact_GetStats(int *active, int *max);

#endif // CORE_FLUID_IMPACT_H

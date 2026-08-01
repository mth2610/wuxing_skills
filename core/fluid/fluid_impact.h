#ifndef CORE_FLUID_IMPACT_H
#define CORE_FLUID_IMPACT_H

// Event-driven water impacts.  Gameplay owns collision and submits an exact
// hit point/normal; the VFX path never depends on compute shaders for contact.

#include "raylib.h"

#define FLUID_IMPACT_MAX_HERO_DROPLETS 48

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

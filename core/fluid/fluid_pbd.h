#ifndef CORE_FLUID_PBD_H
#define CORE_FLUID_PBD_H

#include "raylib.h"

/* CPU PBD reference/fallback for coherent liquid motion. The fixed pool is
 * deliberately small enough for O(n^2) neighbour constraints on no-compute
 * hardware; the render API exposes ellipsoids for the SSF surface pass. */
#define FLUID_PBD_MAX_PARTICLES 384

typedef struct FluidPBDRenderParticle {
    Vector3 position;
    Vector3 radii;
} FluidPBDRenderParticle;

void FluidPBD_Init(void);
void FluidPBD_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse,
                          float force01, float scale);
void FluidPBD_Update(float dt);
int FluidPBD_GetRenderParticles(FluidPBDRenderParticle *outParticles, int maxParticles);

#endif

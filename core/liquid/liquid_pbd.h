#ifndef CORE_LIQUID_PBD_H
#define CORE_LIQUID_PBD_H

#include "raylib.h"

/* CPU PBD reference/fallback for coherent liquid motion. The fixed pool is
 * deliberately small enough for O(n^2) neighbour constraints on no-compute
 * hardware; the render API exposes ellipsoids for the SSF surface pass. */
#define LIQUID_PBD_MAX_PARTICLES 384

typedef struct LiquidPBDRenderParticle {
    Vector3 position;
    Vector3 radii;
} LiquidPBDRenderParticle;

void LiquidPBD_Init(void);
void LiquidPBD_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse,
                          float force01, float scale);
void LiquidPBD_Update(float dt);
int LiquidPBD_GetRenderParticles(LiquidPBDRenderParticle *outParticles, int maxParticles);

#endif

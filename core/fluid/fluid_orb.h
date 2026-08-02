#ifndef CORE_FLUID_ORB_H
#define CORE_FLUID_ORB_H

#include "raylib.h"

/* A coherent water projectile made from SSF input particles guided by force
 * fields. It deliberately has no PBD dependency: the supplied end point is
 * the authoritative impact receiver. */
typedef struct {
    Vector3 start;
    Vector3 target;
    Vector3 hitNormal;       /* receiver normal; zero selects world up */
    float travelTime;       /* seconds; <= 0 selects 0.65 */
    float radius;           /* metres; <= 0 selects 0.42 */
    float force01;
    Color bodyColor;
    Color glowColor;
    Color softColor;
} FluidWaterOrbEvent;

void FluidWaterOrb_Spawn(const FluidWaterOrbEvent *event);
void FluidWaterOrb_Update(float dt);
void FluidWaterOrb_Draw(void);
void FluidWaterOrb_GetStats(int *active, int *max);

#endif

#ifndef CORE_LIQUID_PBD_GPU_H
#define CORE_LIQUID_PBD_GPU_H
#include "raylib.h"
#include <stdbool.h>

#define LIQUID_PBD_GPU_MAX_PARTICLES 4096
bool LiquidPBDGPU_Init(void);
void LiquidPBDGPU_Unload(void);
bool LiquidPBDGPU_IsActive(void);
void LiquidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale);
/* The liquid-table slot this body was spawned with. The body outlives by ~2.5 s
 * the frame that bound its material, so the slot has to travel with the body
 * rather than being read off LiquidSurface's "current" one at capture time. */
int LiquidPBDGPU_GetMaterial(void);
void LiquidPBDGPU_Update(float dt, float groundY);
unsigned int LiquidPBDGPU_GetStateBuffer(void);
int LiquidPBDGPU_GetParticleCount(void);
void LiquidPBDGPU_DrawSurfaceDepth(Camera3D camera);
/* Far surface of the same particles, for dual-depth thickness. */
void LiquidPBDGPU_DrawSurfaceBackDepth(Camera3D camera);
#endif

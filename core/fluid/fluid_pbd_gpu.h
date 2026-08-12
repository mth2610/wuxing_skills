#ifndef CORE_FLUID_PBD_GPU_H
#define CORE_FLUID_PBD_GPU_H
#include "raylib.h"
#include <stdbool.h>

#define FLUID_PBD_GPU_MAX_PARTICLES 4096
bool FluidPBDGPU_Init(void);
void FluidPBDGPU_Unload(void);
bool FluidPBDGPU_IsActive(void);
void FluidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale);
void FluidPBDGPU_Update(float dt, float groundY);
unsigned int FluidPBDGPU_GetStateBuffer(void);
int FluidPBDGPU_GetParticleCount(void);
void FluidPBDGPU_DrawSurfaceDepth(Camera3D camera);
/* Far surface of the same particles, for dual-depth thickness. */
void FluidPBDGPU_DrawSurfaceBackDepth(Camera3D camera);
#endif

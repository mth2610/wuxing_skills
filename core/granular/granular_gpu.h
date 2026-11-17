#ifndef CORE_GRANULAR_GPU_H
#define CORE_GRANULAR_GPU_H

#include "core/granular/granular_sim.h"
#include "raylib.h"
#include <stdbool.h>

bool GranularGpu_Init(void);
void GranularGpu_Unload(void);
bool GranularGpu_IsAvailable(void);
void GranularGpu_UploadGrains(const GranularGrainState *grains, int count,
                              Vector3 receiverPoint, Vector3 receiverNormal,
                              float cellSize, const GranularMaterialProfile *mat);
int  GranularGpu_AppendGrains(const GranularGrainState *grains, int count,
                              Vector3 receiverPoint, Vector3 receiverNormal,
                              float cellSize);
void GranularGpu_SetImpulse(Vector3 center, float radius,
                            Vector3 accel, float swirlRadPerSec);
void GranularGpu_SetTornadoFunnel(Vector3 basePos, float baseRadius, float topRadius,
                                  float height, float swirlSpeed, float updraftSpeed,
                                  bool active);
void GranularGpu_SetWaves(const GranularWaveDesc *waves, int waveCount);
void GranularGpu_Update(float dt);
void GranularGpu_DrawBody(Camera3D camera, const GranularMaterialProfile *mat);
int  GranularGpu_GetGrainCount(void);
void GranularGpu_Clear(void);

#endif // CORE_GRANULAR_GPU_H

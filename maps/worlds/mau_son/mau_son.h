#ifndef MAU_SON_MAP_H
#define MAU_SON_MAP_H

#include "raylib.h"
#include <stdbool.h>

void InitMauSonMap(void);
void UpdateMauSonMap(float dt);
void DrawMauSonMap(void);
void DrawTransparentMauSonMap(void);
void UnloadMauSonMap(void);
float GetGroundHeightMauSonMap(float x, float z);
bool SampleGroundSurfaceMauSonMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);
bool GetWaterInfoMauSonMap(float x, float z, float *outSurfaceY, float *outWaterDepth);
void SetWaterInteractorMauSonMap(Vector3 position, Vector3 velocity, float radius);
void AddWaterRippleMauSonMap(Vector3 position, float radius, float intensity);
void ResolveCollisionMauSonMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);

#endif // MAU_SON_MAP_H

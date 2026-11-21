#ifndef PHU_QUOC_MAP_H
#define PHU_QUOC_MAP_H

#include "raylib.h"
#include <stdbool.h>

void InitPhuQuocMap(void);
void UpdatePhuQuocMap(float dt);
void DrawPhuQuocMap(void);
void DrawTransparentPhuQuocMap(void);
void UnloadPhuQuocMap(void);
float GetGroundHeightPhuQuocMap(float x, float z);
bool SampleGroundSurfacePhuQuocMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);
bool GetWaterInfoPhuQuocMap(float x, float z, float *outSurfaceY, float *outWaterDepth);
void SetWaterInteractorPhuQuocMap(Vector3 position, Vector3 velocity, float radius);
void AddWaterRipplePhuQuocMap(Vector3 position, float radius, float intensity);
void ResolveCollisionPhuQuocMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);

#endif // PHU_QUOC_MAP_H

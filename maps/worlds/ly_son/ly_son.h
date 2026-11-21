#ifndef LY_SON_MAP_H
#define LY_SON_MAP_H

#include "raylib.h"
#include <stdbool.h>

void InitLySonMap(void);
void DrawLySonMap(void);
void DrawTransparentLySonMap(void);
void UpdateLySonMap(float dt);
void UnloadLySonMap(void);

float GetGroundHeightLySonMap(float x, float z);
bool SampleGroundSurfaceLySonMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);
bool GetWaterInfoLySonMap(float x, float z, float *outSurfaceY, float *outWaterDepth);
void SetWaterInteractorLySonMap(Vector3 position, Vector3 velocity, float radius);
void AddWaterRippleLySonMap(Vector3 position, float radius, float intensity);
void SetRainLySonMap(float intensity);

#endif // LY_SON_MAP_H

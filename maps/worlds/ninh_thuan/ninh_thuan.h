#ifndef NINH_THUAN_MAP_H
#define NINH_THUAN_MAP_H

#include "raylib.h"
#include <stdbool.h>

void InitNinhThuanMap(void);
void DrawNinhThuanMap(void);
void DrawTransparentNinhThuanMap(void);
void UpdateNinhThuanMap(float dt);
void UnloadNinhThuanMap(void);

float GetGroundHeightNinhThuanMap(float x, float z);
bool SampleGroundSurfaceNinhThuanMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);
bool GetWaterInfoNinhThuanMap(float x, float z, float *outSurfaceY, float *outWaterDepth);
void SetWaterInteractorNinhThuanMap(Vector3 position, Vector3 velocity, float radius);
void AddWaterRippleNinhThuanMap(Vector3 position, float radius, float intensity);
void SetRainNinhThuanMap(float intensity);

#endif // NINH_THUAN_MAP_H

#ifndef SONG_QUAO_MAP_H
#define SONG_QUAO_MAP_H

#include "raylib.h"
#include <stdbool.h>

void InitSongQuaoMap(void);
void DrawSongQuaoMap(void);
void DrawTransparentSongQuaoMap(void);
void UpdateSongQuaoMap(float dt);
void UnloadSongQuaoMap(void);

float GetGroundHeightSongQuaoMap(float x, float z);
bool SampleGroundSurfaceSongQuaoMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);
bool GetWaterInfoSongQuaoMap(float x, float z, float *outSurfaceY, float *outWaterDepth);
void SetWaterInteractorSongQuaoMap(Vector3 position, Vector3 velocity, float radius);
void AddWaterRippleSongQuaoMap(Vector3 position, float radius, float intensity);

#endif // SONG_QUAO_MAP_H

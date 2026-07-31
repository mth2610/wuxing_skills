#ifndef VERDANT_PATH_MAP_H
#define VERDANT_PATH_MAP_H

#include "raylib.h"
#include <stdbool.h>

// A grass field with a stone path and scattered rocks — 100m x 75m
// rectangle (diagonal 125m), sized so a corner-to-corner walk at the game
// screen's 3.5 m/s pace takes ~36s (see verdant_path.c for the math).
void InitVerdantPathMap(void);
void DrawVerdantPathMap(void);
// Absolute world-space ground Y at (x,z) — real heightmap-based island
// terrain (plateau + sunken cliff edge), not flat. Auto-registered with
// MapManager by scripts/generate_map_registry.py's naming convention.
float GetGroundHeightVerdantPathMap(float x, float z);
bool SampleGroundSurfaceVerdantPathMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal);

#endif // VERDANT_PATH_MAP_H

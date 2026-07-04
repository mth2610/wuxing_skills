#ifndef AFTERIMAGE_H
#define AFTERIMAGE_H

#include "raylib.h"
#include "raymath.h"

// Item 31: Mesh afterimage / ghost trail.
// Stores model REFERENCE + transform snapshot (no mesh copy).
// Drawn with translucency dissolve-out over lifetime using the shared
// effect_material shader. Caller must not unload the model while ghosts
// live — guaranteed by the no-Unload rule (ResourceManager_LoadShader).
//
// Typical use: spawn one ghost every ~0.04s while a blade/dash is active.
//
// Usage:
//   Afterimage_Spawn(myModel, MatrixIdentity(), ELEMENT_COLOR_METAL, 0.35f);

#define MAX_AFTERIMAGES 64

void Afterimage_Init(void);
void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life);
void Afterimage_Update(float dt);
void Afterimage_Draw(void);   // BLEND_ALPHA, depth-write off
void Afterimage_GetStats(int *active, int *max);

#endif // AFTERIMAGE_H

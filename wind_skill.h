#ifndef WIND_SKILL_H
#define WIND_SKILL_H

#include "raylib.h"

void InitWindSkill(int screenWidth, int screenHeight);
void CastWindSkill(Vector3 startPos, Vector3 target, float sizeScale);
void UpdateWindSkill(float dt);
void DrawWindSkill(void);
void UnloadWindSkill(void);

// Query active wind vortex pull fields for the physics system
bool GetWindPullForce(Vector3* outPullCenter, float* outPullStrength);

#endif // WIND_SKILL_H

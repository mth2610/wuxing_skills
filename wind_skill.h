#ifndef WIND_SKILL_H
#define WIND_SKILL_H

#include "raylib.h"
#include "skill_manager.h" // Bắt buộc phải có dòng này để hiểu SkillParams

void InitWindSkill(int screenWidth, int screenHeight);

// ĐỒNG BỘ THAM SỐ CHUẨN: Nhận struct SkillParams thay vì float sizeScale
void CastWindSkill(Vector3 startPos, Vector3 target, SkillParams params);

void UpdateWindSkill(float dt);
void DrawWindSkill(void);
void UnloadWindSkill(void);

bool GetWindPullForce(Vector3 *outPullCenter, float *outPullStrength);

#endif // WIND_SKILL_H
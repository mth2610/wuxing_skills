#ifndef CORE_TEST_SKILL_H
#define CORE_TEST_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

void InitCoreTestSkill(int screenWidth, int screenHeight);
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

#endif // CORE_TEST_SKILL_H

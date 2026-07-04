#ifndef WOOD_SILK_SKILL_H
#define WOOD_SILK_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

void InitWoodSilkSkill(int screenWidth, int screenHeight);
void CastWoodSilkSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWoodSilkSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWoodSilkSkill(void);
void UnloadWoodSilkSkill(void);

#endif // WOOD_SILK_SKILL_H

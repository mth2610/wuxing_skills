#ifndef SKILL_CAMPFIRE_H
#define SKILL_CAMPFIRE_H

#include "raylib.h"
#include "core/skill_manager.h"

// Volume campfire — a raymarched fire volume (Vulkan stress showcase). Places a grounded,
// wide-based licking flame at the cast target. Pure VFX (no damage wiring).
void InitCampfireSkill(int screenWidth, int screenHeight);
void CastCampfireSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCampfireSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCampfireSkill(void);
void UnloadCampfireSkill(void);

#endif // SKILL_CAMPFIRE_H

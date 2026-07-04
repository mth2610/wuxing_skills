#ifndef METAL_WIRE_ORB_SKILL_H
#define METAL_WIRE_ORB_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"
#include <stdbool.h>

void InitMetalWireOrbSkill(int screenWidth, int screenHeight);
void CastMetalWireOrbSkill(int agentId, Vector3 pos, Vector3 target, SkillParams params);
void UpdateMetalWireOrbSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawMetalWireOrbSkill(void);
void UnloadMetalWireOrbSkill(void);

// Skill Status API
bool IsMetalWireOrbSkillCoiling(void);
int GetMetalWireOrbSkillProjectiles(Vector3 *positions, int maxCount);
void DeactivateMetalWireOrbSkillProjectile(int index);

#endif // METAL_WIRE_ORB_SKILL_H

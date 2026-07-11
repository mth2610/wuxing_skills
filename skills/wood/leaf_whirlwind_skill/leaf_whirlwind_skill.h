#ifndef SKILL_LEAF_WHIRLWIND_H
#define SKILL_LEAF_WHIRLWIND_H

#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Main lifecycle
void InitLeafWhirlwindSkill(int screenWidth, int screenHeight);
void CastLeafWhirlwindSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLeafWhirlwindSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLeafWhirlwindSkill(void);
void UnloadLeafWhirlwindSkill(void);

// Engine <-> Skill communication
bool IsLeafWhirlwindSkillCoiling(void);
int GetLeafWhirlwindSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLeafWhirlwindProjectile(int index);

#endif // SKILL_LEAF_WHIRLWIND_H

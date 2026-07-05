#ifndef SKILL_GLOWING_VINES_H
#define SKILL_GLOWING_VINES_H

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
void InitGlowingVinesSkill(int screenWidth, int screenHeight);
void CastGlowingVinesSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGlowingVinesSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGlowingVinesSkill(void);
void UnloadGlowingVinesSkill(void);

// Engine <-> Skill communication
bool IsGlowingVinesSkillCoiling(void);
int GetGlowingVinesSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGlowingVinesProjectile(int index);

#endif // SKILL_GLOWING_VINES_H

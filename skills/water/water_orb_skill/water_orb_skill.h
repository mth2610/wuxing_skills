#ifndef SKILL_WATERORB_H
#define SKILL_WATERORB_H

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
void InitWaterOrbSkill(int screenWidth, int screenHeight);
void CastWaterOrbSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWaterOrbSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWaterOrbSkill(void);
void UnloadWaterOrbSkill(void);

// Engine <-> Skill communication
bool IsWaterOrbSkillCoiling(void);
int GetWaterOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateWaterOrbProjectile(int index);

#endif // SKILL_WATERORB_H

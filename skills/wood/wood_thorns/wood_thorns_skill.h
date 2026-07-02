#ifndef SKILL_WOOD_THORNS_H
#define SKILL_WOOD_THORNS_H

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

// Standard wuxing lifecycle API
void InitWoodThornsSkill(int screenWidth, int screenHeight);
void CastWoodThornsSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWoodThornsSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWoodThornsSkill(void);
void UnloadWoodThornsSkill(void);

// Custom communications for physical/combat integration
bool IsWoodThornsSkillCoiling(void);
int GetWoodThornsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateWoodThornsProjectile(int index);

#endif // SKILL_WOOD_THORNS_H

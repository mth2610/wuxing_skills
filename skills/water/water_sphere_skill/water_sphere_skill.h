#ifndef SKILL_WATER_SPHERE_H
#define SKILL_WATER_SPHERE_H

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

void InitWaterSphereSkill(int screenWidth, int screenHeight);
void CastWaterSphereSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWaterSphereSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWaterSphereSkill(void);
void UnloadWaterSphereSkill(void);

bool IsWaterSphereSkillCoiling(void);
int GetWaterSphereSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateWaterSphereProjectile(int index);

#endif // SKILL_WATER_SPHERE_H
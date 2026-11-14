#ifndef METAL_BLADE_STORM_SKILL_H
#define METAL_BLADE_STORM_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float   radius;
    bool    active;
} SkillProjectile;
#endif

void InitMetalBladeStormSkill(int screenWidth, int screenHeight);
void CastMetalBladeStormSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateMetalBladeStormSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawMetalBladeStormSkill(void);
void UnloadMetalBladeStormSkill(void);

bool IsMetalBladeStormSkillCoiling(void);
int  GetMetalBladeStormSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateMetalBladeStormProjectile(int index);

#endif // METAL_BLADE_STORM_SKILL_H

#ifndef SAND_STORM_SKILL_H
#define SAND_STORM_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"

#include <stdbool.h>

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitSandStormSkill(int screenWidth, int screenHeight);
void CastSandStormSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateSandStormSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawSandStormSkill(void);
void UnloadSandStormSkill(void);

int  GetSandStormSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateSandStormProjectile(int index);

#endif // SAND_STORM_SKILL_H

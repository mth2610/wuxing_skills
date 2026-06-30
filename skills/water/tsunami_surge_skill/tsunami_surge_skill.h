#ifndef SKILL_TSUNAMI_SURGE_H
#define SKILL_TSUNAMI_SURGE_H

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

void InitTsunamiSurgeSkill(int screenWidth, int screenHeight);
void CastTsunamiSurgeSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTsunamiSurgeSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTsunamiSurgeSkill(void);
void UnloadTsunamiSurgeSkill(void);

bool IsTsunamiSurgeSkillCoiling(void);
int  GetTsunamiSurgeSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTsunamiSurgeProjectile(int index);

#endif
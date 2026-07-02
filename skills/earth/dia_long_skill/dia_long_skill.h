#ifndef DIA_LONG_SKILL_H
#define DIA_LONG_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"
#include <stdbool.h>

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float   radius;
    bool    active;
} SkillProjectile;
#endif

void InitDiaLongSkill(int screenWidth, int screenHeight);
void CastDiaLongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateDiaLongSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawDiaLongSkill(void);
void UnloadDiaLongSkill(void);
bool IsDiaLongSkillCoiling(void);
int  GetDiaLongSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateDiaLongProjectile(int index);

#endif // DIA_LONG_SKILL_H

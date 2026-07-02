#ifndef THUNDER_ORB_SKILL_H
#define THUNDER_ORB_SKILL_H

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

void InitThunderOrbSkill(int screenWidth, int screenHeight);
void CastThunderOrbSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateThunderOrbSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawThunderOrbSkill(void);
void UnloadThunderOrbSkill(void);
bool IsThunderOrbSkillCoiling(void);
int  GetThunderOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateThunderOrbProjectile(int index);

#endif // THUNDER_ORB_SKILL_H

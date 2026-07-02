#ifndef THUY_KINH_SKILL_H
#define THUY_KINH_SKILL_H

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

void InitThuyKinhSkill(int screenWidth, int screenHeight);
void CastThuyKinhSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateThuyKinhSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawThuyKinhSkill(void);
void UnloadThuyKinhSkill(void);
bool IsThuyKinhSkillCoiling(void);
int  GetThuyKinhSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateThuyKinhProjectile(int index);

#endif // THUY_KINH_SKILL_H

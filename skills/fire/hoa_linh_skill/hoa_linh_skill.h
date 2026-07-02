#ifndef HOA_LINH_SKILL_H
#define HOA_LINH_SKILL_H

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

void InitHoaLinhSkill(int screenWidth, int screenHeight);
void CastHoaLinhSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateHoaLinhSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawHoaLinhSkill(void);
void UnloadHoaLinhSkill(void);
bool IsHoaLinhSkillCoiling(void);
int  GetHoaLinhSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateHoaLinhProjectile(int index);

#endif // HOA_LINH_SKILL_H

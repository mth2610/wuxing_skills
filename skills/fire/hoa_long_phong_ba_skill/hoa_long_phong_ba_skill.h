#ifndef HOA_LONG_PHONG_BA_SKILL_H
#define HOA_LONG_PHONG_BA_SKILL_H

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

void InitHoaLongPhongBaSkill(int w, int h);
void CastHoaLongPhongBaSkill(int agentId, Vector3 start, Vector3 target, SkillParams p);
void UpdateHoaLongPhongBaSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawHoaLongPhongBaSkill(void);
void UnloadHoaLongPhongBaSkill(void);
bool IsHoaLongPhongBaSkillCoiling(void);
int GetHoaLongPhongBaSkillProjectiles(SkillProjectile *out, int max);
void DeactivateHoaLongPhongBaProjectile(int index);

#endif // HOA_LONG_PHONG_BA_SKILL_H

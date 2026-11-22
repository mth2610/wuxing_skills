#ifndef TCDK_LUU_TINH_THU_SKILL_H
#define TCDK_LUU_TINH_THU_SKILL_H

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

void InitTcdkLuuTinhThuSkill(int screenWidth, int screenHeight);
void CastTcdkLuuTinhThuSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkLuuTinhThuSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkLuuTinhThuSkill(void);
void UnloadTcdkLuuTinhThuSkill(void);

bool IsTcdkLuuTinhThuSkillCoiling(void);
int  GetTcdkLuuTinhThuSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkLuuTinhThuProjectile(int index);

#endif /* TCDK_LUU_TINH_THU_SKILL_H */

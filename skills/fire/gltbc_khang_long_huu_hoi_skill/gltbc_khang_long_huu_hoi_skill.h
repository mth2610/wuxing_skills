#ifndef GLTBC_KHANG_LONG_HUU_HOI_SKILL_H
#define GLTBC_KHANG_LONG_HUU_HOI_SKILL_H

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

void InitGltbcKhangLongHuuHoiSkill(int screenWidth, int screenHeight);
void CastGltbcKhangLongHuuHoiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcKhangLongHuuHoiSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcKhangLongHuuHoiSkill(void);
void UnloadGltbcKhangLongHuuHoiSkill(void);

bool IsGltbcKhangLongHuuHoiSkillCoiling(void);
int  GetGltbcKhangLongHuuHoiSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcKhangLongHuuHoiProjectile(int index);

#endif /* GLTBC_KHANG_LONG_HUU_HOI_SKILL_H */

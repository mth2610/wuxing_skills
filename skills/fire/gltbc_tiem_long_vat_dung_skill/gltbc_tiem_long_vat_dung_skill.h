#ifndef GLTBC_TIEM_LONG_VAT_DUNG_SKILL_H
#define GLTBC_TIEM_LONG_VAT_DUNG_SKILL_H

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

void InitGltbcTiemLongVatDungSkill(int screenWidth, int screenHeight);
void CastGltbcTiemLongVatDungSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcTiemLongVatDungSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcTiemLongVatDungSkill(void);
void UnloadGltbcTiemLongVatDungSkill(void);

bool IsGltbcTiemLongVatDungSkillCoiling(void);
int  GetGltbcTiemLongVatDungSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcTiemLongVatDungProjectile(int index);

#endif /* GLTBC_TIEM_LONG_VAT_DUNG_SKILL_H */

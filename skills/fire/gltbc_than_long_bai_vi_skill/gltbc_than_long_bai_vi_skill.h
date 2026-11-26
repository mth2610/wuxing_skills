#ifndef GLTBC_THAN_LONG_BAI_VI_SKILL_H
#define GLTBC_THAN_LONG_BAI_VI_SKILL_H

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

void InitGltbcThanLongBaiViSkill(int screenWidth, int screenHeight);
void CastGltbcThanLongBaiViSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcThanLongBaiViSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcThanLongBaiViSkill(void);
void UnloadGltbcThanLongBaiViSkill(void);

bool IsGltbcThanLongBaiViSkillCoiling(void);
int  GetGltbcThanLongBaiViSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcThanLongBaiViProjectile(int index);

#endif /* GLTBC_THAN_LONG_BAI_VI_SKILL_H */

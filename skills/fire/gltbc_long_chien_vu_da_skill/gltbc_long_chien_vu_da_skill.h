#ifndef GLTBC_LONG_CHIEN_VU_DA_SKILL_H
#define GLTBC_LONG_CHIEN_VU_DA_SKILL_H

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

void InitGltbcLongChienVuDaSkill(int screenWidth, int screenHeight);
void CastGltbcLongChienVuDaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcLongChienVuDaSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcLongChienVuDaSkill(void);
void UnloadGltbcLongChienVuDaSkill(void);

bool IsGltbcLongChienVuDaSkillCoiling(void);
int  GetGltbcLongChienVuDaSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcLongChienVuDaProjectile(int index);

#endif /* GLTBC_LONG_CHIEN_VU_DA_SKILL_H */

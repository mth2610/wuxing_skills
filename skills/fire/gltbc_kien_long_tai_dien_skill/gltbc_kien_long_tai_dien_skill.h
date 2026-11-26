#ifndef GLTBC_KIEN_LONG_TAI_DIEN_SKILL_H
#define GLTBC_KIEN_LONG_TAI_DIEN_SKILL_H

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

void InitGltbcKienLongTaiDienSkill(int screenWidth, int screenHeight);
void CastGltbcKienLongTaiDienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcKienLongTaiDienSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcKienLongTaiDienSkill(void);
void UnloadGltbcKienLongTaiDienSkill(void);

bool IsGltbcKienLongTaiDienSkillCoiling(void);
int  GetGltbcKienLongTaiDienSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcKienLongTaiDienProjectile(int index);

#endif /* GLTBC_KIEN_LONG_TAI_DIEN_SKILL_H */

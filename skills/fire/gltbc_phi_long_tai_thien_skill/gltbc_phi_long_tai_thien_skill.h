#ifndef GLTBC_PHI_LONG_TAI_THIEN_SKILL_H
#define GLTBC_PHI_LONG_TAI_THIEN_SKILL_H

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

void InitGltbcPhiLongTaiThienSkill(int screenWidth, int screenHeight);
void CastGltbcPhiLongTaiThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateGltbcPhiLongTaiThienSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawGltbcPhiLongTaiThienSkill(void);
void UnloadGltbcPhiLongTaiThienSkill(void);

bool IsGltbcPhiLongTaiThienSkillCoiling(void);
int  GetGltbcPhiLongTaiThienSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateGltbcPhiLongTaiThienProjectile(int index);

#endif /* GLTBC_PHI_LONG_TAI_THIEN_SKILL_H */

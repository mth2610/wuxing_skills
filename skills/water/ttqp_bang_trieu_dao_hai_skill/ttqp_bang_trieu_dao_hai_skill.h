#ifndef TTQP_BANG_TRIEU_DAO_HAI_SKILL_H
#define TTQP_BANG_TRIEU_DAO_HAI_SKILL_H

#include "raylib.h"
#include "skills/skill_manager.h"
#include "skills/skill_helper.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

void InitTtqpBangTrieuDaoHaiSkill(int screenWidth, int screenHeight);
void CastTtqpBangTrieuDaoHaiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpBangTrieuDaoHaiSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpBangTrieuDaoHaiSkill(void);
void UnloadTtqpBangTrieuDaoHaiSkill(void);
bool IsTtqpBangTrieuDaoHaiSkillCoiling(void);
int  GetTtqpBangTrieuDaoHaiSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpBangTrieuDaoHaiProjectile(int index);

#endif /* TTQP_BANG_TRIEU_DAO_HAI_SKILL_H */

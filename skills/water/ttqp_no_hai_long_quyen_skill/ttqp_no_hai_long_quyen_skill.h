#ifndef TTQP_NO_HAI_LONG_QUYEN_SKILL_H
#define TTQP_NO_HAI_LONG_QUYEN_SKILL_H

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

void InitTtqpNoHaiLongQuyenSkill(int screenWidth, int screenHeight);
void CastTtqpNoHaiLongQuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpNoHaiLongQuyenSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpNoHaiLongQuyenSkill(void);
void UnloadTtqpNoHaiLongQuyenSkill(void);
bool IsTtqpNoHaiLongQuyenSkillCoiling(void);
int  GetTtqpNoHaiLongQuyenSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpNoHaiLongQuyenProjectile(int index);

#endif /* TTQP_NO_HAI_LONG_QUYEN_SKILL_H */

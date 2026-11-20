#ifndef TTQP_THUY_KINH_QUYEN_THUAN_SKILL_H
#define TTQP_THUY_KINH_QUYEN_THUAN_SKILL_H

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

void InitTtqpThuyKinhQuyenThuanSkill(int screenWidth, int screenHeight);
void CastTtqpThuyKinhQuyenThuanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpThuyKinhQuyenThuanSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpThuyKinhQuyenThuanSkill(void);
void UnloadTtqpThuyKinhQuyenThuanSkill(void);
bool IsTtqpThuyKinhQuyenThuanSkillCoiling(void);
int  GetTtqpThuyKinhQuyenThuanSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpThuyKinhQuyenThuanProjectile(int index);

#endif /* TTQP_THUY_KINH_QUYEN_THUAN_SKILL_H */

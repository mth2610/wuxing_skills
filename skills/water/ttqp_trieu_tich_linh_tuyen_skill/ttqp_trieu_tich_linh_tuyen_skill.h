#ifndef TTQP_TRIEU_TICH_LINH_TUYEN_SKILL_H
#define TTQP_TRIEU_TICH_LINH_TUYEN_SKILL_H

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

void InitTtqpTrieuTichLinhTuyenSkill(int screenWidth, int screenHeight);
void CastTtqpTrieuTichLinhTuyenSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpTrieuTichLinhTuyenSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpTrieuTichLinhTuyenSkill(void);
void UnloadTtqpTrieuTichLinhTuyenSkill(void);
bool IsTtqpTrieuTichLinhTuyenSkillCoiling(void);
int  GetTtqpTrieuTichLinhTuyenSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpTrieuTichLinhTuyenProjectile(int index);

#endif /* TTQP_TRIEU_TICH_LINH_TUYEN_SKILL_H */

#ifndef TTQP_HAN_UYEN_QUYEN_TOAN_SKILL_H
#define TTQP_HAN_UYEN_QUYEN_TOAN_SKILL_H

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

void InitTtqpHanUyenQuyenToanSkill(int screenWidth, int screenHeight);
void CastTtqpHanUyenQuyenToanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpHanUyenQuyenToanSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpHanUyenQuyenToanSkill(void);
void UnloadTtqpHanUyenQuyenToanSkill(void);
bool IsTtqpHanUyenQuyenToanSkillCoiling(void);
int  GetTtqpHanUyenQuyenToanSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpHanUyenQuyenToanProjectile(int index);

#endif /* TTQP_HAN_UYEN_QUYEN_TOAN_SKILL_H */

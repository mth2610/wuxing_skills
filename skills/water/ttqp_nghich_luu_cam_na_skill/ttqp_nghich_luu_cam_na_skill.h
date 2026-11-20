#ifndef TTQP_NGHICH_LUU_CAM_NA_SKILL_H
#define TTQP_NGHICH_LUU_CAM_NA_SKILL_H

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

void InitTtqpNghichLuuCamNaSkill(int screenWidth, int screenHeight);
void CastTtqpNghichLuuCamNaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTtqpNghichLuuCamNaSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTtqpNghichLuuCamNaSkill(void);
void UnloadTtqpNghichLuuCamNaSkill(void);
bool IsTtqpNghichLuuCamNaSkillCoiling(void);
int  GetTtqpNghichLuuCamNaSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTtqpNghichLuuCamNaProjectile(int index);

#endif /* TTQP_NGHICH_LUU_CAM_NA_SKILL_H */

#ifndef TCDK_CHAN_AM_KIEM_TRAN_SKILL_H
#define TCDK_CHAN_AM_KIEM_TRAN_SKILL_H

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

void InitTcdkChanAmKiemTranSkill(int screenWidth, int screenHeight);
void CastTcdkChanAmKiemTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkChanAmKiemTranSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkChanAmKiemTranSkill(void);
void UnloadTcdkChanAmKiemTranSkill(void);

bool IsTcdkChanAmKiemTranSkillCoiling(void);
int  GetTcdkChanAmKiemTranSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkChanAmKiemTranProjectile(int index);

#endif /* TCDK_CHAN_AM_KIEM_TRAN_SKILL_H */

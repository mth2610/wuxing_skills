#ifndef TCDK_KIM_CANG_PHAN_CHAN_SKILL_H
#define TCDK_KIM_CANG_PHAN_CHAN_SKILL_H

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

void InitTcdkKimCangPhanChanSkill(int screenWidth, int screenHeight);
void CastTcdkKimCangPhanChanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkKimCangPhanChanSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkKimCangPhanChanSkill(void);
void UnloadTcdkKimCangPhanChanSkill(void);

bool IsTcdkKimCangPhanChanSkillCoiling(void);
int  GetTcdkKimCangPhanChanSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkKimCangPhanChanProjectile(int index);

#endif /* TCDK_KIM_CANG_PHAN_CHAN_SKILL_H */

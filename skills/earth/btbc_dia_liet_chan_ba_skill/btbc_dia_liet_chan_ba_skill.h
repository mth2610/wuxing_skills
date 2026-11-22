#ifndef BTBC_DIA_LIET_CHAN_BA_SKILL_H
#define BTBC_DIA_LIET_CHAN_BA_SKILL_H

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

void InitBtbcDiaLietChanBaSkill(int screenWidth, int screenHeight);
void CastBtbcDiaLietChanBaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcDiaLietChanBaSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcDiaLietChanBaSkill(void);
void UnloadBtbcDiaLietChanBaSkill(void);
bool IsBtbcDiaLietChanBaSkillCoiling(void);
int  GetBtbcDiaLietChanBaSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcDiaLietChanBaProjectile(int index);

#endif /* BTBC_DIA_LIET_CHAN_BA_SKILL_H */

#ifndef BTBC_THACH_TRU_XUNG_THIEN_SKILL_H
#define BTBC_THACH_TRU_XUNG_THIEN_SKILL_H

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

void InitBtbcThachTruXungThienSkill(int screenWidth, int screenHeight);
void CastBtbcThachTruXungThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcThachTruXungThienSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcThachTruXungThienSkill(void);
void UnloadBtbcThachTruXungThienSkill(void);
bool IsBtbcThachTruXungThienSkillCoiling(void);
int  GetBtbcThachTruXungThienSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcThachTruXungThienProjectile(int index);

#endif /* BTBC_THACH_TRU_XUNG_THIEN_SKILL_H */

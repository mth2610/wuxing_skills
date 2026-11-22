#ifndef BTBC_HAU_THO_TRAN_NHAC_TRAN_SKILL_H
#define BTBC_HAU_THO_TRAN_NHAC_TRAN_SKILL_H

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

void InitBtbcHauThoTranNhacTranSkill(int screenWidth, int screenHeight);
void CastBtbcHauThoTranNhacTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcHauThoTranNhacTranSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcHauThoTranNhacTranSkill(void);
void UnloadBtbcHauThoTranNhacTranSkill(void);
bool IsBtbcHauThoTranNhacTranSkillCoiling(void);
int  GetBtbcHauThoTranNhacTranSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcHauThoTranNhacTranProjectile(int index);

#endif /* BTBC_HAU_THO_TRAN_NHAC_TRAN_SKILL_H */

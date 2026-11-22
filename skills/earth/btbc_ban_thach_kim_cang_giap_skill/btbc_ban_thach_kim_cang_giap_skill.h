#ifndef BTBC_BAN_THACH_KIM_CANG_GIAP_SKILL_H
#define BTBC_BAN_THACH_KIM_CANG_GIAP_SKILL_H

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

void InitBtbcBanThachKimCangGiapSkill(int screenWidth, int screenHeight);
void CastBtbcBanThachKimCangGiapSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcBanThachKimCangGiapSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcBanThachKimCangGiapSkill(void);
void UnloadBtbcBanThachKimCangGiapSkill(void);
bool IsBtbcBanThachKimCangGiapSkillCoiling(void);
int  GetBtbcBanThachKimCangGiapSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcBanThachKimCangGiapProjectile(int index);

#endif /* BTBC_BAN_THACH_KIM_CANG_GIAP_SKILL_H */

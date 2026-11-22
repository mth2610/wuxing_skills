#ifndef BTBC_THIET_SON_KHAO_SKILL_H
#define BTBC_THIET_SON_KHAO_SKILL_H

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

void InitBtbcThietSonKhaoSkill(int screenWidth, int screenHeight);
void CastBtbcThietSonKhaoSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcThietSonKhaoSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcThietSonKhaoSkill(void);
void UnloadBtbcThietSonKhaoSkill(void);
bool IsBtbcThietSonKhaoSkillCoiling(void);
int  GetBtbcThietSonKhaoSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcThietSonKhaoProjectile(int index);

#endif /* BTBC_THIET_SON_KHAO_SKILL_H */

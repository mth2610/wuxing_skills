#ifndef BTBC_BANG_SON_TOAI_CHUONG_SKILL_H
#define BTBC_BANG_SON_TOAI_CHUONG_SKILL_H

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

void InitBtbcBangSonToaiChuongSkill(int screenWidth, int screenHeight);
void CastBtbcBangSonToaiChuongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcBangSonToaiChuongSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcBangSonToaiChuongSkill(void);
void UnloadBtbcBangSonToaiChuongSkill(void);
bool IsBtbcBangSonToaiChuongSkillCoiling(void);
int  GetBtbcBangSonToaiChuongSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcBangSonToaiChuongProjectile(int index);

#endif /* BTBC_BANG_SON_TOAI_CHUONG_SKILL_H */

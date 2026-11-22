#ifndef BTBC_KHAI_SON_THACH_DAN_SKILL_H
#define BTBC_KHAI_SON_THACH_DAN_SKILL_H

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

void InitBtbcKhaiSonThachDanSkill(int screenWidth, int screenHeight);
void CastBtbcKhaiSonThachDanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateBtbcKhaiSonThachDanSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawBtbcKhaiSonThachDanSkill(void);
void UnloadBtbcKhaiSonThachDanSkill(void);
bool IsBtbcKhaiSonThachDanSkillCoiling(void);
int  GetBtbcKhaiSonThachDanSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateBtbcKhaiSonThachDanProjectile(int index);

#endif /* BTBC_KHAI_SON_THACH_DAN_SKILL_H */

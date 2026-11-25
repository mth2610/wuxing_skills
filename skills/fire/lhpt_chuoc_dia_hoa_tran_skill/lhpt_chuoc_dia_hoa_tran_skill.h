#ifndef LHPT_CHUOC_DIA_HOA_TRAN_SKILL_H
#define LHPT_CHUOC_DIA_HOA_TRAN_SKILL_H

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

void InitLhptChuocDiaHoaTranSkill(int screenWidth, int screenHeight);
void CastLhptChuocDiaHoaTranSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptChuocDiaHoaTranSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptChuocDiaHoaTranSkill(void);
void UnloadLhptChuocDiaHoaTranSkill(void);

bool IsLhptChuocDiaHoaTranSkillCoiling(void);
int  GetLhptChuocDiaHoaTranSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptChuocDiaHoaTranProjectile(int index);

#endif /* LHPT_CHUOC_DIA_HOA_TRAN_SKILL_H */

#ifndef LHPT_HOA_VU_HOI_PHONG_CUOC_SKILL_H
#define LHPT_HOA_VU_HOI_PHONG_CUOC_SKILL_H

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

void InitLhptHoaVuHoiPhongCuocSkill(int screenWidth, int screenHeight);
void CastLhptHoaVuHoiPhongCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptHoaVuHoiPhongCuocSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptHoaVuHoiPhongCuocSkill(void);
void UnloadLhptHoaVuHoiPhongCuocSkill(void);

bool IsLhptHoaVuHoiPhongCuocSkillCoiling(void);
int  GetLhptHoaVuHoiPhongCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptHoaVuHoiPhongCuocProjectile(int index);

#endif /* LHPT_HOA_VU_HOI_PHONG_CUOC_SKILL_H */

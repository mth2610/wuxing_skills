#ifndef LHPT_PHI_HOA_XUNG_CUOC_SKILL_H
#define LHPT_PHI_HOA_XUNG_CUOC_SKILL_H

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

void InitLhptPhiHoaXungCuocSkill(int screenWidth, int screenHeight);
void CastLhptPhiHoaXungCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptPhiHoaXungCuocSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptPhiHoaXungCuocSkill(void);
void UnloadLhptPhiHoaXungCuocSkill(void);

bool IsLhptPhiHoaXungCuocSkillCoiling(void);
int  GetLhptPhiHoaXungCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptPhiHoaXungCuocProjectile(int index);

#endif /* LHPT_PHI_HOA_XUNG_CUOC_SKILL_H */

#ifndef LHPT_VIEM_HO_BAN_NGUYET_CUOC_SKILL_H
#define LHPT_VIEM_HO_BAN_NGUYET_CUOC_SKILL_H

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

void InitLhptViemHoBanNguyetCuocSkill(int screenWidth, int screenHeight);
void CastLhptViemHoBanNguyetCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptViemHoBanNguyetCuocSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptViemHoBanNguyetCuocSkill(void);
void UnloadLhptViemHoBanNguyetCuocSkill(void);

bool IsLhptViemHoBanNguyetCuocSkillCoiling(void);
int  GetLhptViemHoBanNguyetCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptViemHoBanNguyetCuocProjectile(int index);

#endif /* LHPT_VIEM_HO_BAN_NGUYET_CUOC_SKILL_H */

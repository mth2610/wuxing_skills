#ifndef LHPT_PHAN_DIA_TRAM_CUOC_SKILL_H
#define LHPT_PHAN_DIA_TRAM_CUOC_SKILL_H

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

void InitLhptPhanDiaTramCuocSkill(int screenWidth, int screenHeight);
void CastLhptPhanDiaTramCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptPhanDiaTramCuocSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptPhanDiaTramCuocSkill(void);
void UnloadLhptPhanDiaTramCuocSkill(void);

bool IsLhptPhanDiaTramCuocSkillCoiling(void);
int  GetLhptPhanDiaTramCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptPhanDiaTramCuocProjectile(int index);

#endif /* LHPT_PHAN_DIA_TRAM_CUOC_SKILL_H */

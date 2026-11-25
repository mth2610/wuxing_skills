#ifndef LHPT_XICH_DIEM_TRAC_CUOC_SKILL_H
#define LHPT_XICH_DIEM_TRAC_CUOC_SKILL_H

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

void InitLhptXichDiemTracCuocSkill(int screenWidth, int screenHeight);
void CastLhptXichDiemTracCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptXichDiemTracCuocSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptXichDiemTracCuocSkill(void);
void UnloadLhptXichDiemTracCuocSkill(void);

bool IsLhptXichDiemTracCuocSkillCoiling(void);
int  GetLhptXichDiemTracCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptXichDiemTracCuocProjectile(int index);

#endif /* LHPT_XICH_DIEM_TRAC_CUOC_SKILL_H */

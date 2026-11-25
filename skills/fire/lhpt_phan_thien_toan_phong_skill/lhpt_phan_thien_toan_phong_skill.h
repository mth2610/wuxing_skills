#ifndef LHPT_PHAN_THIEN_TOAN_PHONG_SKILL_H
#define LHPT_PHAN_THIEN_TOAN_PHONG_SKILL_H

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

void InitLhptPhanThienToanPhongSkill(int screenWidth, int screenHeight);
void CastLhptPhanThienToanPhongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateLhptPhanThienToanPhongSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawLhptPhanThienToanPhongSkill(void);
void UnloadLhptPhanThienToanPhongSkill(void);

bool IsLhptPhanThienToanPhongSkillCoiling(void);
int  GetLhptPhanThienToanPhongSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateLhptPhanThienToanPhongProjectile(int index);

#endif /* LHPT_PHAN_THIEN_TOAN_PHONG_SKILL_H */

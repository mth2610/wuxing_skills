#ifndef TCDK_THIEN_LA_KIEM_VONG_SKILL_H
#define TCDK_THIEN_LA_KIEM_VONG_SKILL_H

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

void InitTcdkThienLaKiemVongSkill(int screenWidth, int screenHeight);
void CastTcdkThienLaKiemVongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkThienLaKiemVongSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkThienLaKiemVongSkill(void);
void UnloadTcdkThienLaKiemVongSkill(void);

bool IsTcdkThienLaKiemVongSkillCoiling(void);
int  GetTcdkThienLaKiemVongSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkThienLaKiemVongProjectile(int index);

#endif /* TCDK_THIEN_LA_KIEM_VONG_SKILL_H */

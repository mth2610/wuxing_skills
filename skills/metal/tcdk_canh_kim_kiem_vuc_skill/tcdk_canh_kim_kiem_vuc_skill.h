#ifndef TCDK_CANH_KIM_KIEM_VUC_SKILL_H
#define TCDK_CANH_KIM_KIEM_VUC_SKILL_H

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

void InitTcdkCanhKimKiemVucSkill(int screenWidth, int screenHeight);
void CastTcdkCanhKimKiemVucSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkCanhKimKiemVucSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkCanhKimKiemVucSkill(void);
void UnloadTcdkCanhKimKiemVucSkill(void);

bool IsTcdkCanhKimKiemVucSkillCoiling(void);
int  GetTcdkCanhKimKiemVucSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkCanhKimKiemVucProjectile(int index);

#endif /* TCDK_CANH_KIM_KIEM_VUC_SKILL_H */

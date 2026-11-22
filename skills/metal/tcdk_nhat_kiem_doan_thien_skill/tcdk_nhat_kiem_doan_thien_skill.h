#ifndef TCDK_NHAT_KIEM_DOAN_THIEN_SKILL_H
#define TCDK_NHAT_KIEM_DOAN_THIEN_SKILL_H

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

void InitTcdkNhatKiemDoanThienSkill(int screenWidth, int screenHeight);
void CastTcdkNhatKiemDoanThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateTcdkNhatKiemDoanThienSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawTcdkNhatKiemDoanThienSkill(void);
void UnloadTcdkNhatKiemDoanThienSkill(void);

bool IsTcdkNhatKiemDoanThienSkillCoiling(void);
int  GetTcdkNhatKiemDoanThienSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateTcdkNhatKiemDoanThienProjectile(int index);

#endif /* TCDK_NHAT_KIEM_DOAN_THIEN_SKILL_H */

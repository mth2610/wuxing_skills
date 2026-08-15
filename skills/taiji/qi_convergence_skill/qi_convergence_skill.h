#ifndef QI_CONVERGENCE_SKILL_H
#define QI_CONVERGENCE_SKILL_H

#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Cast-only visual: broad Qi streams gather into a compact hand focus.
void InitQiConvergenceSkill(int screenWidth, int screenHeight);
void CastQiConvergenceSkill(int agentId, Vector3 startPos, Vector3 target,
                            SkillParams params);
void UpdateQiConvergenceSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawQiConvergenceSkill(void);
void UnloadQiConvergenceSkill(void);

bool IsQiConvergenceSkillCoiling(void);
int GetQiConvergenceSkillProjectiles(SkillProjectile *outProjectiles,
                                     int maxProjectiles);
void DeactivateQiConvergenceProjectile(int index);

#endif // QI_CONVERGENCE_SKILL_H

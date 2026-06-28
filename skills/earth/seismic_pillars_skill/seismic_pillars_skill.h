#ifndef SEISMIC_PILLARS_SKILL_H
#define SEISMIC_PILLARS_SKILL_H

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

// Lifecycle
void InitSeismicPillarsSkill(int screenWidth, int screenHeight);
void CastSeismicPillarsSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateSeismicPillarsSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawSeismicPillarsSkill(void);
void UnloadSeismicPillarsSkill(void);

// Callouts
bool IsSeismicPillarsSkillActive(void);
int GetSeismicPillarsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateSeismicPillarsProjectile(int index);

#endif // SEISMIC_PILLARS_SKILL_H

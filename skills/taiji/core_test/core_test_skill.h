#ifndef SKILL_CORE_TEST_H
#define SKILL_CORE_TEST_H

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

// Skill test toàn bộ common shader mới:
//   noise.glsl  → fbm2, fbm2N, hash3
//   lighting.glsl → calcDiffuse (mới), calcFresnel, calcSpecular, perturbNormal
//   fx.glsl     → dissolveCalc, emissiveMask
//   C-side      → WindZone_Set / WindZone_Clear

void InitCoreTestSkill(int screenWidth, int screenHeight);
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

bool IsCoreTestSkillCoiling(void);
int  GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateCoreTestProjectile(int index);

#endif // SKILL_CORE_TEST_H

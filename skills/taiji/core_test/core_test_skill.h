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

// Internal test/sandbox skill (Taiji) — exercises item 3 of the Core API
// update: soft particles (core/screen_distort.h + soft_particle.glsl),
// dissolve edge glow (core/shaders/common/fx.glsl's dissolveCalc, already
// existed — confirmed here), and metaballs/screen-space fluid
// (core/metaball_fx.h). Not a gameplay skill.

void InitCoreTestSkill(int screenWidth, int screenHeight);
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

bool IsCoreTestSkillCoiling(void);
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateCoreTestProjectile(int index);

#endif // SKILL_CORE_TEST_H

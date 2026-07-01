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

// Internal test/sandbox skill (Taiji) — not a gameplay skill. Whatever Core
// API item is currently being verified lives here; confirmed-done features
// get removed once tested (see CORE_ISSUES.md), keep this file minimal.

void InitCoreTestSkill(int screenWidth, int screenHeight);
void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

bool IsCoreTestSkillCoiling(void);
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateCoreTestProjectile(int index);

#endif // SKILL_CORE_TEST_H

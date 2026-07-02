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
void CastCoreTestSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawCoreTestSkill(void);
void UnloadCoreTestSkill(void);

// 2D screen-space HUD (call after EndMode3D, in main.c's 2D pass) — shows
// the last numeric depth readback (press L) on screen since console stdout
// isn't reliably visible in every setup. Test-harness-only, not a real skill
// lifecycle function.
void DrawCoreTestSkillDebugHUD(void);

bool IsCoreTestSkillCoiling(void);
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateCoreTestProjectile(int index);

// Autotest bridge (sandbox/auto_test.h) — forces the test active and exposes
// the last readback numerically, without going through CastSkill()/the UI
// panel. Test-harness-only, not a real skill lifecycle function.
void CoreTestSkill_ForceActivate(int agentId, Vector3 spherePos);
void CoreTestSkill_TriggerReadback(void);
bool CoreTestSkill_GetReadback(int sampleIndex, float *outSceneLinear, float *outFragLinear, float *outDiff);

#endif // SKILL_CORE_TEST_H

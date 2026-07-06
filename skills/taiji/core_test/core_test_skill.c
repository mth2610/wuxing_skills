#include "core_test_skill.h"

// Internal test/sandbox skill (Taiji) — kept minimal per this file's own
// convention (see header comment). All ad-hoc VFX verification (Phase 1-3
// composition additions: metal/fire parity, beauty primitives, new
// archetypes) has been confirmed and migrated to sandbox/vfx_test.c's
// "NEW FX" tab — use that instead of adding test calls here.

void InitCoreTestSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
}

void CastCoreTestSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)agentId;
    (void)startPos;
    (void)target;
    (void)params;
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)dt;
    (void)enemyPos;
    (void)enemyRadius;
}

void DrawCoreTestSkill(void) {}
void DrawCoreTestSkillDebugHUD(void) {}
void UnloadCoreTestSkill(void) {}

bool IsCoreTestSkillCoiling(void) { return false; }
int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    (void)outProjectiles;
    (void)maxProjectiles;
    return 0;
}
void DeactivateCoreTestProjectile(int index) { (void)index; }
void CoreTestSkill_ForceActivate(int agentId, Vector3 spherePos) {
    (void)agentId;
    (void)spherePos;
}
void CoreTestSkill_TriggerReadback(void) {}
bool CoreTestSkill_GetReadback(int sampleIndex, float *outSceneLinear, float *outFragLinear, float *outDiff) {
    (void)sampleIndex;
    (void)outSceneLinear;
    (void)outFragLinear;
    (void)outDiff;
    return false;
}

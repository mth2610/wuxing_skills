#include "core_test_skill.h"
#include "core/composition/visual_composer.h"
#include "core/camera_context.h"
#include "sandbox/auto_test.h"
#include "raymath.h"

// Internal test/sandbox skill (Taiji) — kept minimal per this file's own
// convention (see header comment). All ad-hoc VFX verification (Phase 1-3
// composition additions: metal/fire parity, beauty primitives, new
// archetypes) has been confirmed and migrated to sandbox/vfx_test.c's
// "NEW FX" tab — use that instead of adding test calls here.

// TEMP (remove after user confirms the visual): plasma orb screenshot case —
// draws VFX_ComposePlasmaOrb in front of the camera and saves a PNG artifact
// for headless visual inspection.
static bool s_plasmaActive = false;
static float s_plasmaTime = 0.0f;

static AutoTestResult PlasmaOrbCase(int frameInCase, char *outReason, int outReasonSize) {
    (void)outReason;
    (void)outReasonSize;
    s_plasmaActive = true;
    if (frameInCase >= 150) { // ~2.5s in: shell noise + filaments fully developed
        AutoTest_SaveScreenshot("plasma_orb");
        s_plasmaActive = false;
        return AUTOTEST_PASS;
    }
    return AUTOTEST_RUNNING;
}

void InitCoreTestSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    if (AutoTest_IsEnabled())
        AutoTest_Register("plasma_orb_visual", PlasmaOrbCase, 300);
}

void CastCoreTestSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)agentId;
    (void)startPos;
    (void)target;
    (void)params;
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    if (s_plasmaActive)
        s_plasmaTime += dt;
}

void DrawCoreTestSkill(void) {
    if (!s_plasmaActive)
        return;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 orbPos = Vector3Add(camera.position, Vector3Scale(fwd, 2.5f));
    VFX_ComposePlasmaOrb(orbPos, 0.5f, s_plasmaTime);
}
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

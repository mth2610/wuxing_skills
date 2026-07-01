#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"

#include <stddef.h>

// core_test is a minimal harness — no active test currently running.
// See CORE_ISSUES.md for the next item to test here.

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
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

void UnloadCoreTestSkill(void) {}

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  (void)outProjectiles;
  (void)maxProjectiles;
  return 0;
}

void DeactivateCoreTestProjectile(int index) { (void)index; }

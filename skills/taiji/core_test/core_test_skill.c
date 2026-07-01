#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"

#include <stddef.h>

// Test: dual-filter bloom (Item 4d).
// Cast places a bright white sphere at the caster's feet — produces bright
// pixels that the bloom bright-pass extracts. A wide, soft glow halo should
// be visible around the sphere; compare radius vs the old Gaussian bloom.
#define CORE_TEST_SPHERE_RADIUS 12.0f

static bool    s_active   = false;
static Vector3 s_pos      = {0};

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  s_active = false;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  (void)target;
  (void)params;
  s_pos    = startPos;
  s_active = true;
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)dt;
  (void)enemyPos;
  (void)enemyRadius;
}

void DrawCoreTestSkill(void) {
  if (!s_active) return;
  DrawSphere(s_pos, CORE_TEST_SPHERE_RADIUS, WHITE);
}

void UnloadCoreTestSkill(void) { s_active = false; }

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  (void)outProjectiles;
  (void)maxProjectiles;
  return 0;
}

void DeactivateCoreTestProjectile(int index) { (void)index; }

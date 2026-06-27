#include "skills/metal/sword_rain/sword_rain_skill.h"
#include "skills/metal/metal_projectile/metal_skill.h"
#include "core/skill_manager.h"
#include <stddef.h>

static void CastSwordRain(Vector3 startPos, Vector3 target,
                          SkillParams params) {
  // Call CastMetalSkill using the adjusted positions and overridden
  // quantity/size scale
  CastMetalSkill(startPos, target, params);
}

// Auto-register using compiler constructor attribute
#ifdef __GNUC__
__attribute__((constructor))
#endif
static void
AutoRegisterSwordRain(void) {
  InitSwordRainSkill();
}

void InitSwordRainSkill(void) {
  // Register Sword Rain skill under dynamic registry.
  // Reuses Metal's update, draw, and unload, so those callbacks are NULL.
  int id =
      RegisterSkill("SWORD RAIN", GOLD, NULL, CastSwordRain, NULL, NULL, NULL);
  if (id != -1) {
    SetSkillOverrides(id, CAST_PATH_SKY, CAST_ANCHOR_TARGET, 20, 1.6f);
  }
}

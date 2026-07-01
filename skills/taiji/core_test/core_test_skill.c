#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/skill_helper.h"
#include "core/procedural_mesh_utils.h"

#include <stddef.h>

// core_test: Item 8 (parametrized EffectMaterial) verification. Cast loads
// one EffectMaterial via Material_LoadCustom() — rim glow, Fresnel power,
// emissive boost, vertex wobble (distortion) and a secondary detail texture
// (assets/textures/crack.png) all configured through EffectMaterialParams,
// no new GLSL written for this look. Sphere holds solid for 1.5s then
// dissolves out over the last 1s via Material_SetFloat(u_dissolve) — reusing
// the existing generic uniform setter. Remove once confirmed (see
// CORE_ISSUES.md).

static bool s_active = false;
static bool s_materialLoaded = false;
static Vector3 s_pos = {0};
static float s_t = 0.0f;
static EffectMaterial s_material;

static const float HOLD_DURATION = 1.5f;
static const float DISSOLVE_DURATION = 1.0f;

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  (void)startPos;
  (void)params;
  s_pos = target;
  s_t = 0.0f;
  s_active = true;

  if (!s_materialLoaded) {
    EffectMaterialParams matParams = {0};
    matParams.baseColor = ELEMENT_COLOR_TAIJI;
    matParams.rimStrength = 0.7f;
    matParams.fresnelPower = 4.0f;
    matParams.emissiveIntensity = 0.15f;
    matParams.distortionStrength = 0.4f;
    matParams.translucency = 1.0f;
    matParams.texture1 = ResourceManager_LoadTexture("assets/textures/crack.png");
    s_material = Material_LoadCustom(matParams);
    s_materialLoaded = true;
  }
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;
  if (!s_active)
    return;

  s_t += dt;
  if (s_t >= HOLD_DURATION + DISSOLVE_DURATION)
    s_active = false;
}

void DrawCoreTestSkill(void) {
  if (!s_active)
    return;

  float dissolve = (s_t <= HOLD_DURATION)
                       ? 0.0f
                       : (s_t - HOLD_DURATION) / DISSOLVE_DURATION;
  Material_SetFloat(&s_material, "u_dissolve", dissolve);

  BeginBlendMode(BLEND_ALPHA); // required for translucency=1.0's alpha<1 to blend
  Material_Begin(s_material);
  // Mesh color arg below is irrelevant to the final look — this material's
  // shader ignores per-vertex color and takes its tint from u_baseColor
  // (EffectMaterialParams.baseColor) instead.
  DrawCoreSphere(s_pos, 20.0f, 24, 24, WHITE);
  Material_End();
  EndBlendMode();
}

void UnloadCoreTestSkill(void) {}

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  (void)outProjectiles;
  (void)maxProjectiles;
  return 0;
}

void DeactivateCoreTestProjectile(int index) { (void)index; }

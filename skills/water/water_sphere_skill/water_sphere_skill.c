#include "skills/water/water_sphere_skill/water_sphere_skill.h"
#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/force_field.h"
#include "environment/environment_system.h"
#include "core/impact_burst.h"
#include "core/particle_radial_burst.h"
#include "core/particle_system.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/utils_math.h"
#include "core/vfx_light.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// Pool-size constant — sizes a static array, not tunable (would need malloc).
#define MAX_WATER_SPHERES 5

#include "water_sphere_skill_params.inl"

extern Camera3D camera;

typedef struct {
  bool active;
  Vector3 position;
  Vector3 target;
  Vector3 direction;
  float sizeScale;
  int ownerAgentId;
} WaterSphere;

static WaterSphere s_spheres[MAX_WATER_SPHERES];
static Shader s_sphereShader;
static int s_timeLoc;
static int s_viewPosLoc;
static int s_lightDirLoc;
static int s_dissolveLoc;

static Texture2D s_causticsTex;
static ForceField s_splashField;   // used by impact burst particles (built at Init, fixed)
static ColorGradient s_splashGrad;
static ImpactBurstConfig s_waterImpactConfig;
static int s_skillIndex = -1;

// Rebuild the drip-particle force field from the static splash layers +
// the tunable extra force mix.  Called each frame before SpawnParticle so
// sandbox changes take effect immediately.
static void RebuildDripField(void) {
    ForceField_Clear(&s_dripField);
    ForceField_AddLayer(&s_dripField, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1.0f, 0},
        .strength  = s_splashGravity
    });
    ForceField_AddLayer(&s_dripField, (ForceLayer){
        .type       = FORCE_NOISE_PERLIN,
        .strength   = s_splashNoise,
        .noiseScale = 0.015f,
        .noiseSpeed = 0.8f
    });
    ForceField_AddLayer(&s_dripField, (ForceLayer){.type = FORCE_DRAG, .strength = s_splashDrag});
    SkillForceMix_AddLayers(&s_flightForce, &s_dripField);
}

// Rebuild the impact-burst splash field (fixed layers, called once at Init
// and whenever a tunable changes the gravity/noise scalars).
static void RebuildSplashField(void) {
    ForceField_Clear(&s_splashField);
    ForceField_AddLayer(&s_splashField, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1.0f, 0},
        .strength  = s_splashGravity
    });
    ForceField_AddLayer(&s_splashField, (ForceLayer){
        .type       = FORCE_NOISE_PERLIN,
        .strength   = s_splashNoise,
        .noiseScale = 0.015f,
        .noiseSpeed = 0.8f
    });
    ForceField_AddLayer(&s_splashField, (ForceLayer){.type = FORCE_DRAG, .strength = s_splashDrag});
}

// Rebuild the ImpactBurstConfig from current tunables.
static void RebuildImpactConfig(void) {
    s_waterImpactConfig.distortEnabled  = true;
    s_waterImpactConfig.distortRadius   = s_impactDistortRadius;
    s_waterImpactConfig.distortStrength = s_impactDistortStrength;
    s_waterImpactConfig.distortLife     = s_impactDistortLife;
    s_waterImpactConfig.distortSpeed    = s_impactDistortSpeed;

    s_waterImpactConfig.decalEnabled       = true;
    s_waterImpactConfig.decalTex           = s_causticsTex;
    s_waterImpactConfig.decalScale         = s_impactDecalScale;
    s_waterImpactConfig.decalLife          = s_impactDecalLife;
    s_waterImpactConfig.decalTint          = ColorAlpha(ELEMENT_COLOR_WATER, 0.8f);
    s_waterImpactConfig.decalRandomRotation = true;

    s_waterImpactConfig.lightEnabled  = true;
    s_waterImpactConfig.lightColor    = ELEMENT_COLOR_WATER;
    s_waterImpactConfig.lightRadius   = s_impactLightRadius;
    s_waterImpactConfig.lightLife     = s_impactLightLife;

    s_waterImpactConfig.particlesEnabled         = true;
    s_waterImpactConfig.particles.countMin       = 35;
    s_waterImpactConfig.particles.countMax       = 50;
    s_waterImpactConfig.particles.speedMin       = s_burstSpeedMin;
    s_waterImpactConfig.particles.speedMax       = s_burstSpeedMax;
    s_waterImpactConfig.particles.radiusMin      = s_burstRadiusMin;
    s_waterImpactConfig.particles.radiusMax      = s_burstRadiusMax;
    s_waterImpactConfig.particles.lifetimeMin    = s_burstLifeMin;
    s_waterImpactConfig.particles.lifetimeMax    = s_burstLifeMax;
    s_waterImpactConfig.particles.pitchRange     = PI;
    s_waterImpactConfig.particles.upwardBias     = s_burstUpwardBias;
    s_waterImpactConfig.particles.forceField     = &s_splashField;
    s_waterImpactConfig.particles.gradient       = &s_splashGrad;
}

void InitWaterSphereSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  s_skillIndex = Skill_GetIndexByName("WATER_SPHERE");

  s_sphereShader = ResourceManager_LoadShader(
      "skills/water/water_sphere_skill/water_sphere.vs",
      "skills/water/water_sphere_skill/water_sphere.fs");
  s_timeLoc    = GetShaderLocation(s_sphereShader, "u_time");
  s_viewPosLoc = GetShaderLocation(s_sphereShader, "viewPos");
  s_lightDirLoc = GetShaderLocation(s_sphereShader, "u_lightDir");
  s_dissolveLoc = GetShaderLocation(s_sphereShader, "u_dissolve");

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    s_spheres[i].active = false;
  }

  s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");

  ColorGradient_StandardFade(&s_splashGrad, ELEMENT_COLOR_WATER, 0.40f, 0.2f);

  // Seed per-phase curves flat (no-op until shaped in sandbox)
  SkillCurve_SetConstant(&s_flightRadiusCurve,   1.0f);
  SkillCurve_SetConstant(&s_flightSpeedCurve,    1.0f);
  SkillCurve_SetConstant(&s_flightAlphaCurve,    1.0f);
  SkillCurve_SetConstant(&s_flightEmissiveCurve, 1.0f);

  RebuildSplashField();
  RebuildImpactConfig();

  static SkillTunableEntry s_tunables[WATER_SPHERE_TUNABLE_COUNT];
  int tn = 0;
  #include "water_sphere_skill_tunables.inl"

  SkillTunables_LoadPersisted(
      "skills/water/water_sphere_skill/water_sphere_skill.tuning",
      s_tunables, tn);
  RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}

void CastWaterSphereSkill(int agentId, Vector3 startPos, Vector3 target,
                          SkillParams params) {
  if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active) {
      s_spheres[i].active       = true;
      s_spheres[i].ownerAgentId = agentId;

      // Perpendicular jitter to avoid perfectly straight layouts (Rule 12.2)
      Vector3 rawDir = Vector3Normalize(Vector3Subtract(target, startPos));
      Vector3 perp   = (Vector3){-rawDir.z, 0.0f, rawDir.x};
      float jitter   = (float)GetRandomValue(-50, 50) / 1000.0f; // ±0.05 m

      s_spheres[i].position  = Vector3Add(startPos, Vector3Scale(perp, jitter));
      s_spheres[i].target    = target;
      s_spheres[i].direction = Vector3Normalize(Vector3Subtract(target, s_spheres[i].position));
      s_spheres[i].sizeScale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

      VFXLight_Spawn(s_spheres[i].position, ELEMENT_COLOR_WATER,
                     s_sphereBaseRadius * s_flightLightRadiusMult, 2.0f,
                     VFX_PRIORITY_LOW);

      SkillManager_TriggerCooldown(
          s_skillIndex, agentId,
          Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, params));
      break;
    }
  }
}

void UpdateWaterSphereSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;

  // Zero-instance early-out: skip per-frame field rebuilds when idle.
  bool anyActive = false;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active) { anyActive = true; break; }
  }
  if (!anyActive)
    return;

  RebuildSplashField();
  RebuildImpactConfig();

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active)
      continue;

    s_spheres[i].position =
        Vector3Add(s_spheres[i].position,
                   Vector3Scale(s_spheres[i].direction,
                                s_sphereSpeed * s_spheres[i].sizeScale * dt));

    // Drip trail particles
    if (GetRandomValue(0, 100) < 40) {
      RebuildDripField();
      ParticleConfig drip = {0};
      drip.position   = s_spheres[i].position;
      drip.velocity   = (Vector3){
          (Random01() - 0.5f) * s_dripVelXZ * 2.0f,
          s_dripVelY,
          (Random01() - 0.5f) * s_dripVelXZ * 2.0f
      };
      drip.radius     = Math_Mix(s_dripRadiusMin, s_dripRadiusMax, Random01())
                        * s_spheres[i].sizeScale;
      drip.lifetime   = s_dripLifetime;
      drip.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
      drip.colorEnd   = (Color){255, 255, 255, 0};
      drip.forceField = &s_dripField;
      drip.radiusCurve   = &s_flightRadiusCurve;
      drip.speedCurve    = &s_flightSpeedCurve;
      drip.alphaCurve    = &s_flightAlphaCurve;
      drip.emissiveCurve = &s_flightEmissiveCurve;
      SpawnParticle(drip);
    }

    // Arrival check — distance-proportional floor/cap (Pass 4)
    float dist   = Vector3Distance(s_spheres[i].position, s_spheres[i].target);
    float thresh = fminf(fmaxf(s_arrivalRadius, 0.1f), dist * 0.6f);
    if (dist < thresh) {
      s_spheres[i].active = false;
      VFX_TriggerImpactBurst(s_spheres[i].position, s_spheres[i].sizeScale,
                             &s_waterImpactConfig);
      if (s_shakeEnable > 0.5f) CameraFX_Shake(0.6f);
    }
  }
}

void DrawWaterSphereSkill(void) {
  bool anyActive = false;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active) { anyActive = true; break; }
  }
  if (!anyActive) return;

  float time = GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(s_sphereShader);
  // Rule 11: raw BeginShaderMode() never auto-uploads matModel for rlgl
  // immediate-mode draws — use SkillManager_BeginShader() instead (CORE_API.md).
  SkillManager_BeginShader(s_sphereShader);

  SetShaderValue(s_sphereShader, s_timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_sphereShader, s_viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);
  // u_lightDir is a custom uniform not auto-bound by SkillManager_BeginShader()
  Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
  SetShaderValue(s_sphereShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  float dissolve = 0.0f;
  SetShaderValue(s_sphereShader, s_dissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

  // Rule 11.1: Reset vertex color before drawing with a custom color/texture shader
  rlColor4ub(255, 255, 255, 255);

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active) continue;
    float currentRadius = s_sphereBaseRadius * s_spheres[i].sizeScale;
    DrawCoreSphere(s_spheres[i].position, currentRadius, 24, 24, WHITE);
  }

  SkillManager_EndShader();
  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadWaterSphereSkill(void) {
  // Assets managed globally by Resource Manager
}

bool IsWaterSphereSkillCoiling(void) { return false; }

int GetWaterSphereSkillProjectiles(SkillProjectile *outProjectiles,
                                   int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active && count < maxProjectiles) {
      outProjectiles[count].position = s_spheres[i].position;
      outProjectiles[count].radius   = s_sphereBaseRadius * s_spheres[i].sizeScale;
      outProjectiles[count].active   = true;
      count++;
    }
  }
  return count;
}

void DeactivateWaterSphereProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active) {
      if (count == index) {
        s_spheres[i].active = false;
        VFX_TriggerImpactBurst(s_spheres[i].position, s_spheres[i].sizeScale,
                               &s_waterImpactConfig);
        if (s_shakeEnable > 0.5f) CameraFX_Shake(0.6f);
        return;
      }
      count++;
    }
  }
}

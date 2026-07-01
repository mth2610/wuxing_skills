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

#define MAX_WATER_SPHERES 5
#define SPHERE_BASE_RADIUS 15.0f // Scale bự theo chuẩn API mới
#define SPHERE_SPEED 180.0f
extern Camera3D camera; // <--- THÊM DÒNG NÀY VÀO ĐÂY
typedef struct {
  bool active;
  Vector3 position;
  Vector3 target;
  Vector3 direction;
  float sizeScale;
} WaterSphere;

static WaterSphere s_spheres[MAX_WATER_SPHERES];
static Shader s_sphereShader;
static int s_timeLoc;
static int s_viewPosLoc;
static int s_lightDirLoc;
static int s_dissolveLoc;

static Texture2D s_causticsTex;
static ForceField s_splashField;
static ColorGradient s_splashGrad;
static ImpactBurstConfig s_waterImpactConfig;

void InitWaterSphereSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  s_sphereShader = ResourceManager_LoadShader(
      "skills/water/water_sphere_skill/water_sphere.vs",
      "skills/water/water_sphere_skill/water_sphere.fs");
  s_timeLoc = GetShaderLocation(s_sphereShader, "u_time");
  s_viewPosLoc = GetShaderLocation(s_sphereShader, "viewPos");
  s_lightDirLoc = GetShaderLocation(s_sphereShader, "u_lightDir");
  s_dissolveLoc = GetShaderLocation(s_sphereShader, "u_dissolve");

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    s_spheres[i].active = false;
  }

  s_causticsTex =
      ResourceManager_LoadTexture("assets/textures/water_caustics.png");

  ColorGradient_StandardFade(&s_splashGrad, ELEMENT_COLOR_WATER, 0.40f, 0.2f);

  // Lực văng nước chuẩn scale bự: Trọng lực 600.0f, cản 3.0f
  ForceField_Clear(&s_splashField);
  ForceField_AddLayer(&s_splashField, (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                                   .direction = {0, -1.0f, 0},
                                                   .strength = 600.0f});
  ForceField_AddLayer(&s_splashField, (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                                   .strength = 45.0f,
                                                   .noiseScale = 0.015f,
                                                   .noiseSpeed = 0.8f});
  ForceField_AddLayer(&s_splashField,
                      (ForceLayer){.type = FORCE_DRAG, .strength = 3.0f});

  // Cấu hình vụ nổ (Impact)
  s_waterImpactConfig.distortEnabled = true;
  s_waterImpactConfig.distortRadius = 90.0f;
  s_waterImpactConfig.distortStrength = 0.8f;
  s_waterImpactConfig.distortLife = 0.6f;
  s_waterImpactConfig.distortSpeed = 160.0f;

  s_waterImpactConfig.decalEnabled = true;
  s_waterImpactConfig.decalTex = s_causticsTex;
  s_waterImpactConfig.decalScale = SPHERE_BASE_RADIUS * 0.35f;
  s_waterImpactConfig.decalLife = 3.5f;
  s_waterImpactConfig.decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.8f);
  s_waterImpactConfig.decalRandomRotation = true;

  s_waterImpactConfig.lightEnabled = true;
  s_waterImpactConfig.lightColor = ELEMENT_COLOR_WATER;
  s_waterImpactConfig.lightRadius = 75.0f;
  s_waterImpactConfig.lightLife = 0.5f;

  // Hạt văng tung tóe với vận tốc cao
  s_waterImpactConfig.particlesEnabled = true;
  s_waterImpactConfig.particles.countMin = 35;
  s_waterImpactConfig.particles.countMax = 50;
  s_waterImpactConfig.particles.speedMin = 150.0f;
  s_waterImpactConfig.particles.speedMax = 320.0f;
  s_waterImpactConfig.particles.radiusMin = 4.0f;
  s_waterImpactConfig.particles.radiusMax = 12.0f;
  s_waterImpactConfig.particles.lifetimeMin = 0.5f;
  s_waterImpactConfig.particles.lifetimeMax = 1.0f;
  s_waterImpactConfig.particles.pitchRange = PI;
  s_waterImpactConfig.particles.upwardBias = 120.0f;
  s_waterImpactConfig.particles.forceField = &s_splashField;
  s_waterImpactConfig.particles.gradient = &s_splashGrad;
}

void CastWaterSphereSkill(Vector3 startPos, Vector3 target,
                          SkillParams params) {
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active) {
      s_spheres[i].active = true;

      // Jitter chống đơ cứng (Rule 12.2)
      Vector3 rawDir = Vector3Normalize(Vector3Subtract(target, startPos));
      Vector3 perp = (Vector3){-rawDir.z, 0.0f, rawDir.x};
      float jitter = (float)GetRandomValue(-50, 50) / 10.0f;

      s_spheres[i].position = Vector3Add(startPos, Vector3Scale(perp, jitter));
      s_spheres[i].target = target;
      s_spheres[i].direction =
          Vector3Normalize(Vector3Subtract(target, s_spheres[i].position));

      // Lấy scale từ params hoặc default 1.0f
      s_spheres[i].sizeScale =
          (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

      // Spawn ánh sáng đi kèm quả cầu
      VFXLight_Spawn(s_spheres[i].position, ELEMENT_COLOR_WATER,
                     SPHERE_BASE_RADIUS * 4.0f, 2.0f);
      break;
    }
  }
}

void UpdateWaterSphereSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active)
      continue;

    // Di chuyển quả cầu
    s_spheres[i].position =
        Vector3Add(s_spheres[i].position,
                   Vector3Scale(s_spheres[i].direction, SPHERE_SPEED * dt));

    // Nhỏ giọt nước trên đường bay
    if (GetRandomValue(0, 100) < 40) {
      ParticleConfig drip = {0};
      drip.position = s_spheres[i].position;
      drip.velocity = (Vector3){(Random01() - 0.5f) * 20.0f, -50.0f,
                                (Random01() - 0.5f) * 20.0f};
      drip.radius = Math_Mix(3.0f, 6.0f, Random01()) * s_spheres[i].sizeScale;
      drip.lifetime = 0.4f;
      drip.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
      drip.colorEnd = (Color){255, 255, 255, 0};
      drip.forceField = &s_splashField;
      SpawnParticle(drip);
    }

    // Check tới đích
    float distSq =
        Vector3DistanceSqr(s_spheres[i].position, s_spheres[i].target);
    if (distSq < 400.0f) { // ~20 units
      s_spheres[i].active = false;
      VFX_TriggerImpactBurst(s_spheres[i].position, s_spheres[i].sizeScale,
                             &s_waterImpactConfig);
      CameraFX_Shake(0.6f);
    }
  }
}

void DrawWaterSphereSkill(void) {
  bool anyActive = false;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active) {
      anyActive = true;
      break;
    }
  }
  if (!anyActive)
    return;

  float time = GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(s_sphereShader);

  SetShaderValue(s_sphereShader, s_timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_sphereShader, s_viewPosLoc, &camera.position,
                 SHADER_UNIFORM_VEC3);
  // This skill uses raw BeginShaderMode(), not SkillManager_BeginShader(),
  // so u_lightDir isn't auto-bound (CORE_ISSUES.md Item 10) — set it here
  // the same way viewPos already is.
  Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
  SetShaderValue(s_sphereShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  float dissolve = 0.0f; // Trạng thái sống chưa dissolve
  SetShaderValue(s_sphereShader, s_dissolveLoc, &dissolve,
                 SHADER_UNIFORM_FLOAT);

  // Rule 11.1: Reset màu
  rlColor4ub(255, 255, 255, 255);

  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (!s_spheres[i].active)
      continue;

    float currentRadius = SPHERE_BASE_RADIUS * s_spheres[i].sizeScale;

    // Sử dụng Primitive của Engine để vẽ quả cầu
    DrawCoreSphere(s_spheres[i].position, currentRadius, 24, 24, WHITE);
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadWaterSphereSkill(void) {
  // Tài nguyên do Resource Manager lo
}

bool IsWaterSphereSkillCoiling(void) { return false; }

int GetWaterSphereSkillProjectiles(SkillProjectile *outProjectiles,
                                   int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_WATER_SPHERES; i++) {
    if (s_spheres[i].active && count < maxProjectiles) {
      outProjectiles[count].position = s_spheres[i].position;
      outProjectiles[count].radius =
          SPHERE_BASE_RADIUS * s_spheres[i].sizeScale;
      outProjectiles[count].active = true;
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
        CameraFX_Shake(0.6f);
        return;
      }
      count++;
    }
  }
}
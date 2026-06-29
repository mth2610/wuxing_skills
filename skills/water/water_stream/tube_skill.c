#include "skills/water/water_stream/tube_skill.h"
#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/force_field.h"
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

#define MAX_TUBE_EMITTERS 5

// --- Force Fields cua Tube Skill ---
static ForceField
    s_tubeSplashField;             // nuoc va cham: trong luc + Perlin gon song
static ForceField s_tubeMistField; // suong dau ong: trong luc nhe + drift

#define TUBE_TRAVEL_SPEED 1.2f
#define TUBE_BASE_RADIUS 12.0f
#define TUBE_SEGMENTS 30
#define TUBE_RADIAL_SEGMENTS 20
#define TUBE_UV_LENGTH_SCALE 3.0f

#define GRAVITY_Y -650.0f
#define FLUID_DRAG_SPLASH 3.0f

extern Camera3D camera;

typedef struct {
  bool active;
  Vector3 p0, p1, p2, p3;
  float progress;
  float sizeScale;
  Vector3 headPos;
} TubeEmitter;

static TubeEmitter emitters[MAX_TUBE_EMITTERS];
static Shader tubeShader;
static int timeLoc;
static int viewPosLoc;
static int uvLengthLoc;

// Cau hinh bien doi mesh huu co rieng cua Water Stream.
// Day la TOAN BO phan "chu ky nuoc" con lai trong skill - moi toan Bezier/
// Frenet-frame/wobble/deform/end-cap da chuyen vao core/procedural_mesh_utils.
static TubeMeshConfig s_waterTubeConfig;

// Cau hinh combo VFX va cham (distort + decal + light + particle burst),
// gop lai tu TriggerWaterBurst() ban goc. Day la phan "chu ky nuoc" con lai
// cho hieu ung va cham - moi logic orchestrate da chuyen vao core/impact_burst.
static ImpactBurstConfig s_waterImpactConfig;

static inline float ClampSizeScale(float scale) {
  return Clamp(scale, 0.2f, 3.0f);
}

static Texture2D s_causticsTex;
static ColorGradient s_splashGrad;

void InitTubeSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  tubeShader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs",
                                          "skills/water/water_stream/tube.fs");
  timeLoc = GetShaderLocation(tubeShader, "u_time");
  viewPosLoc = GetShaderLocation(tubeShader, "viewPos");
  uvLengthLoc =
      GetShaderLocation(tubeShader, "u_uvLength"); // Lay chung cho ca VS va FS
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    emitters[i].active = false;
  }

  s_causticsTex =
      ResourceManager_LoadTexture("assets/textures/water_caustics.png");

  // Gradient "sinh ra tu mau nuoc -> sang hon chut -> mo dan ve trong suot".
  // Thay 3 dong ColorGradient_AddStop bang 1 lan goi duy nhat.
  ColorGradient_StandardFade(&s_splashGrad, ELEMENT_COLOR_WATER, 0.40f, 0.2f);

  // Nuoc va cham: trong luc manh + Perlin gon song lan + drag 3.0
  ForceField_Clear(&s_tubeSplashField);
  ForceField_AddLayer(&s_tubeSplashField,
                      (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                   .direction = {0, -1, 0},
                                   .strength = 650.0f});
  ForceField_AddLayer(&s_tubeSplashField,
                      (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                   .strength = 25.0f,
                                   .noiseScale = 0.010f,
                                   .noiseSpeed = 0.5f});
  ForceField_AddLayer(
      &s_tubeSplashField,
      (ForceLayer){.type = FORCE_DRAG, .strength = FLUID_DRAG_SPLASH});

  // Suong dau ong: trong luc nhe + drift theo Perlin + drag 2.0
  ForceField_Clear(&s_tubeMistField);
  ForceField_AddLayer(&s_tubeMistField, (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                                     .direction = {0, -1, 0},
                                                     .strength = 325.0f});
  ForceField_AddLayer(&s_tubeMistField, (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                                     .strength = 15.0f,
                                                     .noiseScale = 0.008f,
                                                     .noiseSpeed = 0.3f});
  ForceField_AddLayer(&s_tubeMistField,
                      (ForceLayer){.type = FORCE_DRAG, .strength = 2.0f});

  // Mesh config: dung default (dung hanh vi goc cua Water Stream).
  s_waterTubeConfig = ProceduralMesh_DefaultTubeConfig();

  // Impact burst config: gop lai dung 4 buoc trong TriggerWaterBurst() ban goc.
  s_waterImpactConfig.distortEnabled = true;
  s_waterImpactConfig.distortRadius = 85.0f;
  s_waterImpactConfig.distortStrength = 0.7f;
  s_waterImpactConfig.distortLife = 0.6f;
  s_waterImpactConfig.distortSpeed = 150.0f;

  s_waterImpactConfig.decalEnabled = true;
  s_waterImpactConfig.decalTex = s_causticsTex;
  s_waterImpactConfig.decalScale =
      TUBE_BASE_RADIUS * 0.25f; // nhan sizeScale luc goi
  s_waterImpactConfig.decalLife = 4.0f;
  s_waterImpactConfig.decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.7f);
  s_waterImpactConfig.decalRandomRotation = true;

  s_waterImpactConfig.lightEnabled = true;
  s_waterImpactConfig.lightColor = ELEMENT_COLOR_WATER;
  s_waterImpactConfig.lightRadius = 55.0f; // nhan sizeScale luc goi
  s_waterImpactConfig.lightLife = 0.5f;

  s_waterImpactConfig.particlesEnabled = true;
  s_waterImpactConfig.particles.countMin = 25;
  s_waterImpactConfig.particles.countMax = 40;
  s_waterImpactConfig.particles.speedMin = 120.0f;
  s_waterImpactConfig.particles.speedMax = 250.0f;
  s_waterImpactConfig.particles.radiusMin = 3.0f;
  s_waterImpactConfig.particles.radiusMax = 8.0f;
  s_waterImpactConfig.particles.lifetimeMin = 0.6f;
  s_waterImpactConfig.particles.lifetimeMax = 1.2f;
  s_waterImpactConfig.particles.pitchRange =
      PI; // spherical splash, giong ban goc
  s_waterImpactConfig.particles.upwardBias = 100.0f;
  s_waterImpactConfig.particles.colorStart = ELEMENT_COLOR_WATER;
  s_waterImpactConfig.particles.colorEnd =
      ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.3f), 0.0f);
  s_waterImpactConfig.particles.forceField = &s_tubeSplashField;
  s_waterImpactConfig.particles.gradient = &s_splashGrad;
}

void CastTubeSkill(Vector3 startPos, Vector3 target, float twistPhase,
                   float sizeScale) {
  float clampedScale = ClampSizeScale(sizeScale);
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].p0 = startPos;
      emitters[i].p3 = target;
      emitters[i].progress = 0.0f;
      emitters[i].sizeScale = clampedScale;
      emitters[i].headPos = startPos;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
      Vector3 right = Vector3Normalize(Vector3CrossProduct(up, dir));

      float lateralOffset = dist * 0.4f * cosf(twistPhase);
      float heightOffset = dist * 0.3f;

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.3f));
      emitters[i].p1 =
          Vector3Add(emitters[i].p1, Vector3Scale(right, lateralOffset));
      emitters[i].p1.y += heightOffset;

      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.7f));
      emitters[i].p2 =
          Vector3Add(emitters[i].p2, Vector3Scale(right, -lateralOffset));
      emitters[i].p2.y += heightOffset * 0.5f;
      break;
    }
  }
}

void UpdateTubeSkill(float dt) {
  for (int e = 0; e < MAX_TUBE_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    emitters[e].progress += dt * TUBE_TRAVEL_SPEED;
    if (emitters[e].progress >= 1.0f) {
      emitters[e].active = false;
      VFX_TriggerImpactBurst(emitters[e].p3, emitters[e].sizeScale,
                             &s_waterImpactConfig);
      continue;
    }
    emitters[e].headPos = ProceduralMesh_BezierPoint(
        emitters[e].p0, emitters[e].p1, emitters[e].p2, emitters[e].p3,
        emitters[e].progress);

    if (GetRandomValue(0, 100) < 60) {
      ParticleConfig cfgMist = {0};
      cfgMist.position = emitters[e].headPos;
      cfgMist.velocity =
          (Vector3){(Random01() - 0.5f) * 40.0f, Random01() * 50.0f,
                    (Random01() - 0.5f) * 40.0f};
      cfgMist.radius = Math_Mix(2.0f, 5.0f, Random01()) * emitters[e].sizeScale;
      cfgMist.lifetime = Math_Mix(0.2f, 0.5f, Random01());
      cfgMist.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.7f);
      cfgMist.colorEnd = (Color){255, 255, 255, 0};
      cfgMist.forceField = &s_tubeMistField;
      cfgMist.gradient = &s_splashGrad;
      SpawnParticle(cfgMist);
    }
  }
}

void DrawTubeSkill(void) {
  bool anyActive = false;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active) {
      anyActive = true;
      break;
    }
  }
  if (!anyActive)
    return;

  float time = GetTime();
  rlDisableDepthMask();
  rlEnableBackfaceCulling();
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(tubeShader);

  SetShaderValue(tubeShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(tubeShader, viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);
  float uvLength = TUBE_UV_LENGTH_SCALE;
  SetShaderValue(tubeShader, uvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);

  // Quy tac 9.1: reset vertex color truoc khi ve mesh dung shader mau/texture
  // rieng
  rlColor4ub(255, 255, 255, 255);

  for (int e = 0; e < MAX_TUBE_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    float radius = TUBE_BASE_RADIUS * emitters[e].sizeScale;

    TubeMeshData mesh;
    ProceduralMesh_BuildTube(&mesh, emitters[e].p0, emitters[e].p1,
                             emitters[e].p2, emitters[e].p3, radius,
                             emitters[e].progress, time, TUBE_SEGMENTS,
                             TUBE_RADIAL_SEGMENTS, &s_waterTubeConfig);
    ProceduralMesh_DrawTube(&mesh, TUBE_UV_LENGTH_SCALE);
  }

  EndShaderMode();
  EndBlendMode();
  rlDisableBackfaceCulling();
  rlEnableDepthMask();
}

void UnloadTubeSkill(void) {
  /* Assets are cached and managed globally by the Resource Manager */
}

int GetTubeSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      outProjectiles[count].position = emitters[i].headPos;
      outProjectiles[count].radius =
          TUBE_BASE_RADIUS * emitters[i].sizeScale * 1.6f;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateTubeProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        VFX_TriggerImpactBurst(emitters[i].headPos, emitters[i].sizeScale,
                               &s_waterImpactConfig);
        return;
      }
      count++;
    }
  }
}
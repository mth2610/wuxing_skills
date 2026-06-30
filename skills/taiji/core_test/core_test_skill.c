#include "skills/taiji/core_test/core_test_skill.h"
#include "core/metaball_fx.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define CORE_TEST_DURATION 3.0f

// --- Shape 1: Dissolve quad with edge burn glow ---
#define CORE_TEST_DISSOLVE_OFFSET ((Vector3){-40.0f, 35.0f, 0.0f})
#define CORE_TEST_DISSOLVE_SIZE ((Vector2){80.0f, 80.0f})

// --- Shape 2: Metaball cluster (3 blobs orbiting -> merge/separate) ---
#define CORE_TEST_METABALL_OFFSET ((Vector3){60.0f, 35.0f, 0.0f})
#define CORE_TEST_METABALL_RADIUS 22.0f
#define CORE_TEST_METABALL_ORBIT 20.0f
#define CORE_TEST_METABALL_SPEED 1.4f

static bool s_active = false;
static float s_timer = 0.0f;
static Vector3 s_startPos = {0};

static Shader s_dissolveShader;
static int s_dissolveColorLoc, s_dissolveEdgeColorLoc, s_dissolveAmountLoc;

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  s_dissolveShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_dissolve.vs",
      "skills/taiji/core_test/core_test_dissolve.fs");

  s_dissolveColorLoc = GetShaderLocation(s_dissolveShader, "u_color");
  s_dissolveEdgeColorLoc = GetShaderLocation(s_dissolveShader, "u_edgeColor");
  s_dissolveAmountLoc = GetShaderLocation(s_dissolveShader, "u_dissolve");

  s_active = false;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  (void)target;
  (void)params;
  s_startPos = startPos;
  s_timer = 0.0f;
  s_active = true;
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;
  if (!s_active)
    return;

  s_timer += dt;
  if (s_timer >= CORE_TEST_DURATION) {
    s_active = false;
  }
}

static void DrawDissolveShape(void) {
  Vector3 pos = Vector3Add(s_startPos, CORE_TEST_DISSOLVE_OFFSET);
  float dissolve = Clamp(s_timer / CORE_TEST_DURATION, 0.0f, 1.0f);
  Vector4 color = ColorNormalize(ELEMENT_COLOR_TAIJI);
  Vector3 edgeColor = {ELEMENT_COLOR_FIRE.r / 255.0f, ELEMENT_COLOR_FIRE.g / 255.0f,
                       ELEMENT_COLOR_FIRE.b / 255.0f};

  SkillManager_BeginShader(s_dissolveShader);
  SetShaderValue(s_dissolveShader, s_dissolveColorLoc, &color, SHADER_UNIFORM_VEC4);
  SetShaderValue(s_dissolveShader, s_dissolveEdgeColorLoc, &edgeColor,
                 SHADER_UNIFORM_VEC3);
  SetShaderValue(s_dissolveShader, s_dissolveAmountLoc, &dissolve,
                 SHADER_UNIFORM_FLOAT);

  BeginShaderMode(s_dissolveShader);
  rlColor4ub(255, 255, 255, 255);
  DrawCorePlaneRect(pos, CORE_TEST_DISSOLVE_SIZE, WHITE);
  EndShaderMode();
}

static void RegisterMetaballShape(void) {
  Vector3 center = Vector3Add(s_startPos, CORE_TEST_METABALL_OFFSET);
  float time = (float)GetTime();

  // 3 blobs orbiting the center at varying phase — orbit radius breathes
  // in/out over time so they visibly merge into one mass and separate again.
  float orbit = CORE_TEST_METABALL_ORBIT * (0.5f + 0.5f * sinf(time * 0.7f));
  for (int i = 0; i < 3; i++) {
    float angle = time * CORE_TEST_METABALL_SPEED + (float)i * (2.0f * PI / 3.0f);
    Vector3 blobPos = {
        center.x + cosf(angle) * orbit,
        center.y,
        center.z + sinf(angle) * orbit,
    };
    MetaballFX_RegisterBlob(blobPos, CORE_TEST_METABALL_RADIUS);
  }
}

void DrawCoreTestSkill(void) {
  if (!s_active)
    return;

  DrawDissolveShape();
  RegisterMetaballShape();
}

void UnloadCoreTestSkill(void) {
  // Shader tài nguyên tồn tại theo vòng đời app — không Unload ở đây.
}

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  if (!s_active || maxProjectiles < 1)
    return 0;

  outProjectiles[0].position = s_startPos;
  outProjectiles[0].radius = CORE_TEST_METABALL_RADIUS;
  outProjectiles[0].active = true;
  return 1;
}

void DeactivateCoreTestProjectile(int index) {
  if (index == 0) {
    s_active = false;
  }
}

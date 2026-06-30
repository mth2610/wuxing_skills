#include "skills/taiji/core_test/core_test_skill.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include "rlgl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define CORE_TEST_DURATION 3.0f

// --- Shape: triplanar-mapped low-poly rock ---
// CORE_ISSUES.md Item 4a — ProceduralMesh_DrawRock vẽ qua rlBegin immediate-
// mode (position + normal only, không có UV) nên không thể texture bằng UV
// thường mà không bị stretch/streak trên facet jagged. Test triplanar.glsl
// chiếu pattern theo world-space 3 trục, blend theo normal.
// Khoảng cách từ nhân vật tới rock (theo hướng cast target) — đủ xa để rock
// không lồng vào nhân vật, không phải đứng sát ngay người cast.
#define CORE_TEST_ROCK_DISTANCE 120.0f
#define CORE_TEST_ROCK_HEIGHT 40.0f
#define CORE_TEST_ROCK_RADIUS 45.0f
#define CORE_TEST_ROCK_JITTER 0.3f
#define CORE_TEST_ROCK_SUBDIV 2
#define CORE_TEST_ROCK_SEED 7
#define CORE_TEST_TRIPLANAR_SCALE 0.025f
#define CORE_TEST_TRIPLANAR_SHARPNESS 4.0f

static bool s_active = false;
static float s_timer = 0.0f;
static Vector3 s_startPos = {0};

static Shader s_triplanarShader;
static int s_colorALoc, s_colorBLoc, s_scaleLoc, s_sharpnessLoc;
static RockMeshData s_rockData;

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  s_triplanarShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_triplanar.vs",
      "skills/taiji/core_test/core_test_triplanar.fs");

  s_colorALoc = GetShaderLocation(s_triplanarShader, "u_colorA");
  s_colorBLoc = GetShaderLocation(s_triplanarShader, "u_colorB");
  s_scaleLoc = GetShaderLocation(s_triplanarShader, "u_scale");
  s_sharpnessLoc = GetShaderLocation(s_triplanarShader, "u_sharpness");

  s_active = false;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  (void)params;
  s_startPos = startPos;
  s_timer = 0.0f;
  s_active = true;

  // Rock spawns offset toward the cast target direction, not stacked on top
  // of the caster — falls back to +X if target == startPos (zero-length dir).
  Vector3 toTarget = Vector3Subtract(target, startPos);
  toTarget.y = 0.0f;
  Vector3 dir = (Vector3Length(toTarget) > 0.001f)
                    ? Vector3Normalize(toTarget)
                    : (Vector3){1.0f, 0.0f, 0.0f};
  Vector3 rockCenter = Vector3Add(
      startPos, Vector3Add(Vector3Scale(dir, CORE_TEST_ROCK_DISTANCE),
                           (Vector3){0.0f, CORE_TEST_ROCK_HEIGHT, 0.0f}));

  ProceduralMesh_BuildRock(&s_rockData, rockCenter, CORE_TEST_ROCK_RADIUS,
                           CORE_TEST_ROCK_JITTER, CORE_TEST_ROCK_SEED,
                           CORE_TEST_ROCK_SUBDIV);
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

static void DrawTriplanarRockShape(void) {
  Vector4 colorA = ColorNormalize(ELEMENT_COLOR_EARTH);
  Vector4 colorB = ColorNormalize(ELEMENT_COLOR_METAL);

  SkillManager_BeginShader(s_triplanarShader);
  SetShaderValue(s_triplanarShader, s_colorALoc, &colorA, SHADER_UNIFORM_VEC4);
  SetShaderValue(s_triplanarShader, s_colorBLoc, &colorB, SHADER_UNIFORM_VEC4);
  SetShaderValue(s_triplanarShader, s_scaleLoc,
                 (float[]){CORE_TEST_TRIPLANAR_SCALE}, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_triplanarShader, s_sharpnessLoc,
                 (float[]){CORE_TEST_TRIPLANAR_SHARPNESS}, SHADER_UNIFORM_FLOAT);

  BeginShaderMode(s_triplanarShader);
  rlColor4ub(255, 255, 255, 255);
  ProceduralMesh_DrawRock(&s_rockData, WHITE);
  EndShaderMode();
}

void DrawCoreTestSkill(void) {
  if (!s_active)
    return;

  DrawTriplanarRockShape();
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
  outProjectiles[0].radius = CORE_TEST_ROCK_RADIUS;
  outProjectiles[0].active = true;
  return 1;
}

void DeactivateCoreTestProjectile(int index) {
  if (index == 0) {
    s_active = false;
  }
}

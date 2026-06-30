#include "skills/taiji/core_test/core_test_skill.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define CORE_TEST_DURATION 2.5f

// --- Shape 1: AlongPath (tube bending between cast start/target) ---
#define CORE_TEST_RADIUS_START 14.0f
#define CORE_TEST_RADIUS_END 6.0f
#define CORE_TEST_WOBBLE_AMOUNT 60.0f
#define CORE_TEST_WOBBLE_SPEED 3.0f

// --- Shape 2: Noise (rippling grid plane), offset to one side ---
#define CORE_TEST_GRID_SIZE 100.0f
#define CORE_TEST_GRID_OFFSET ((Vector3){-140.0f, 0.0f, 0.0f})
#define CORE_TEST_NOISE_AMPLITUDE 8.0f
#define CORE_TEST_NOISE_FREQUENCY 0.04f
#define CORE_TEST_NOISE_SPEED 0.6f

// --- Shape 3: TwistAndTaper (stationary swirling column), offset opposite ---
#define CORE_TEST_TWIST_OFFSET ((Vector3){140.0f, 0.0f, 0.0f})
#define CORE_TEST_TWIST_HEIGHT 70.0f
#define CORE_TEST_TWIST_RADIUS_START 16.0f
#define CORE_TEST_TWIST_RADIUS_END 4.0f
#define CORE_TEST_TWIST_SPEED 1.2f
#define CORE_TEST_TWIST_MAX_ANGLE (PI * 2.0f) // 1 full turn top-to-bottom, oscillating

static bool s_active = false;
static float s_timer = 0.0f;
static Vector3 s_startPos = {0};
static Vector3 s_target = {0};

static Shader s_pathShader, s_gridShader, s_twistShader;
static int s_pathColorLoc, s_gridColorLoc, s_twistColorLoc;
static Mesh s_cylinderMesh; // shared by path-bend + twist shapes
static Mesh s_gridMesh;
static Material s_pathMaterial, s_gridMaterial, s_twistMaterial;

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  s_pathShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test.vs",
      "skills/taiji/core_test/core_test.fs");
  s_gridShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_grid.vs",
      "skills/taiji/core_test/core_test.fs");
  s_twistShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_twist.vs",
      "skills/taiji/core_test/core_test.fs");

  s_pathColorLoc = GetShaderLocation(s_pathShader, "u_color");
  s_gridColorLoc = GetShaderLocation(s_gridShader, "u_color");
  s_twistColorLoc = GetShaderLocation(s_twistShader, "u_color");

  // Cast-time-equivalent bake: built once here, never rebuilt per frame.
  s_cylinderMesh = ProceduralMesh_CreateBaseCylinder(12, 24);
  s_gridMesh = ProceduralMesh_CreateBaseGrid(CORE_TEST_GRID_SIZE,
                                             CORE_TEST_GRID_SIZE, 16, 16);

  s_pathMaterial = LoadMaterialDefault();
  s_pathMaterial.shader = s_pathShader;
  s_gridMaterial = LoadMaterialDefault();
  s_gridMaterial.shader = s_gridShader;
  s_twistMaterial = LoadMaterialDefault();
  s_twistMaterial.shader = s_twistShader;

  s_active = false;
}

void CastCoreTestSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  (void)params;
  s_startPos = startPos;
  s_target = target;
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

static void DrawPathShape(float time, Vector4 color) {
  // DisplaceVertex_AlongPath: world-space path control points + wobble
  // offset perpendicular to the path, recomputed every frame.
  Vector3 pathDir = Vector3Subtract(s_target, s_startPos);
  Vector3 up = (fabsf(pathDir.y) > 0.99f * Vector3Length(pathDir))
                  ? (Vector3){1.0f, 0.0f, 0.0f}
                  : (Vector3){0.0f, 1.0f, 0.0f};
  Vector3 wobbleAxis = Vector3Normalize(Vector3CrossProduct(pathDir, up));
  float wobble = sinf(time * CORE_TEST_WOBBLE_SPEED) * CORE_TEST_WOBBLE_AMOUNT;
  Vector3 wobbleOffset = Vector3Scale(wobbleAxis, wobble);

  MeshDisplacementParams disp = ProceduralMesh_DefaultDisplacementParams();
  disp.pathP0 = s_startPos;
  disp.pathP1 = Vector3Add(Vector3Lerp(s_startPos, s_target, 0.33f), wobbleOffset);
  disp.pathP2 = Vector3Add(Vector3Lerp(s_startPos, s_target, 0.66f), wobbleOffset);
  disp.pathP3 = s_target;
  disp.taperStart = CORE_TEST_RADIUS_START;
  disp.taperEnd = CORE_TEST_RADIUS_END;
  disp.twistAmount = sinf(time * 1.5f) * PI * 0.3f;

  SkillManager_BeginShader(s_pathShader);
  ProceduralMesh_SetDisplacementUniforms(s_pathShader, &disp);
  SetShaderValue(s_pathShader, s_pathColorLoc, &color, SHADER_UNIFORM_VEC4);

  // matModel stays identity: AlongPath already outputs world-space
  // positions from world-space path control points.
  DrawMesh(s_cylinderMesh, s_pathMaterial, MatrixIdentity());
}

static void DrawGridShape(float time, Vector4 color) {
  (void)time; // noise driven by u_time directly in core_test_grid.vs
  // DisplaceVertex_Noise: ripples the flat grid by `amplitude` along its
  // normal, using fbm2-driven noise computed in core_test_grid.vs.
  MeshDisplacementParams disp = ProceduralMesh_DefaultDisplacementParams();
  disp.amplitude = CORE_TEST_NOISE_AMPLITUDE;
  disp.frequency = CORE_TEST_NOISE_FREQUENCY;
  disp.speed = CORE_TEST_NOISE_SPEED;

  SkillManager_BeginShader(s_gridShader);
  ProceduralMesh_SetDisplacementUniforms(s_gridShader, &disp);
  SetShaderValue(s_gridShader, s_gridColorLoc, &color, SHADER_UNIFORM_VEC4);

  // Local-space displacement (no path baked in) -> matModel places it.
  Vector3 pos = Vector3Add(s_startPos, CORE_TEST_GRID_OFFSET);
  Matrix matModel = MatrixTranslate(pos.x, pos.y, pos.z);
  DrawMesh(s_gridMesh, s_gridMaterial, matModel);
}

static void DrawTwistShape(float time, Vector4 color) {
  // DisplaceVertex_TwistAndTaper: stationary swirling column, no path bend.
  MeshDisplacementParams disp = ProceduralMesh_DefaultDisplacementParams();
  disp.taperStart = CORE_TEST_TWIST_RADIUS_START;
  disp.taperEnd = CORE_TEST_TWIST_RADIUS_END;
  disp.twistAmount = sinf(time * CORE_TEST_TWIST_SPEED) * CORE_TEST_TWIST_MAX_ANGLE;

  SkillManager_BeginShader(s_twistShader);
  ProceduralMesh_SetDisplacementUniforms(s_twistShader, &disp);
  SetShaderValue(s_twistShader, s_twistColorLoc, &color, SHADER_UNIFORM_VEC4);

  // Local-space displacement: matModel scales local height [0,1] -> real
  // height and translates to the column's world position. Radius is
  // already in world units via taperStart/End (see displacement.glsl).
  Vector3 pos = Vector3Add(s_startPos, CORE_TEST_TWIST_OFFSET);
  Matrix matModel = MatrixMultiply(MatrixScale(1.0f, CORE_TEST_TWIST_HEIGHT, 1.0f),
                                   MatrixTranslate(pos.x, pos.y, pos.z));
  DrawMesh(s_cylinderMesh, s_twistMaterial, matModel);
}

void DrawCoreTestSkill(void) {
  if (!s_active)
    return;

  float time = (float)GetTime();
  Vector4 color = ColorNormalize(ELEMENT_COLOR_TAIJI);

  DrawPathShape(time, color);
  DrawGridShape(time, color);
  DrawTwistShape(time, color);
}

void UnloadCoreTestSkill(void) {
  // Shader/mesh tài nguyên tồn tại theo vòng đời app — không Unload ở đây.
}

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  if (!s_active || maxProjectiles < 1)
    return 0;

  outProjectiles[0].position = s_target;
  outProjectiles[0].radius = CORE_TEST_RADIUS_END;
  outProjectiles[0].active = true;
  return 1;
}

void DeactivateCoreTestProjectile(int index) {
  if (index == 0) {
    s_active = false;
  }
}

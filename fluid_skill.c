#include "fluid_skill.h"
#include "particle_system.h"
#include "path_spline.h"
#include "raymath.h"
#include "rlgl.h"
#include "skill_manager.h"
#include "utils_math.h"
#include <math.h>

#define MAX_EMITTERS 10

#define WATER_TRAVEL_SPEED 1.6f
#define WATER_BASE_RADIUS 16.0f

#define MIST_SPAWN_RATE 45.0f
#define GRAVITY_Y -650.0f
#define FLUID_DRAG_SPLASH 3.0f
#define FLUID_DRAG_MIST 4.5f

extern Camera3D camera;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 p1, p2;
  float progress;
  float sizeScale;
  float spawnAccumulator;
  Vector3 currentPos;
} FluidEmitter;

static FluidEmitter emitters[MAX_EMITTERS];
static Shader fluidShader;
static int timeLocVS;
static int timeLocFS;
static int viewPosLoc;

static inline float ClampSizeScale(float scale) {
  return Clamp(scale, 0.2f, 3.0f);
}

static void TriggerFluidImpact(Vector3 pos, float sizeScale) {
  int splashCount = GetRandomValue(15, 25) * sizeScale;
  for (int s = 0; s < splashCount; s++) {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = Math_Mix(200.0f, 500.0f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity = (Vector3){cosf(angle) * speed * cosf(pitch),
                             sinf(pitch) * speed + (250.0f * sizeScale),
                             sinf(angle) * speed * cosf(pitch)};
    cfg.force = (Vector3){0.0f, GRAVITY_Y, 0.0f};
    cfg.drag = FLUID_DRAG_SPLASH;
    cfg.radius = Math_Mix(2.5f, 7.0f, Random01()) * sizeScale * 3.5f;
    cfg.lifetime = Math_Mix(0.4f, 1.2f, Random01());
    cfg.colorStart = (Color){200, 240, 255, 230};
    cfg.colorEnd = (Color){60, 150, 220, 0};
    cfg.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
    SpawnParticle(cfg);
  }
}

// HÀM TỰ DỰNG KHỐI CẦU 3D ĐỘ PHÂN GIẢI CAO QUA TẦNG RLGL
static void RenderCustom3DSphere(Vector3 center, float radius, int rings,
                                 int columns) {
  rlPushMatrix();
  rlTranslatef(center.x, center.y,
               center.z); // Chỉ gọi hàm dịch chuyển ma trận thôi

  rlBegin(RL_QUADS);

  for (int i = 0; i < rings; i++) {
    float theta1 = (float)i * PI / rings;
    float theta2 = (float)(i + 1) * PI / rings;

    for (int j = 0; j < columns; j++) {
      float phi1 = (float)j * 2.0f * PI / columns;
      float phi2 = (float)(j + 1) * 2.0f * PI / columns;

      // Đỉnh 1
      Vector3 n1 = {sinf(theta1) * cosf(phi1), cosf(theta1),
                    sinf(theta1) * sinf(phi1)};
      rlNormal3f(n1.x, n1.y, n1.z);
      rlTexCoord2f((float)j / columns, (float)i / rings);
      rlVertex3f(n1.x * radius, n1.y * radius, n1.z * radius);

      // Đỉnh 2
      Vector3 n2 = {sinf(theta1) * cosf(phi2), cosf(theta1),
                    sinf(theta1) * sinf(phi2)};
      rlNormal3f(n2.x, n2.y, n2.z);
      rlTexCoord2f((float)(j + 1) / columns, (float)i / rings);
      rlVertex3f(n2.x * radius, n2.y * radius, n2.z * radius);

      // Đỉnh 3
      Vector3 n3 = {sinf(theta2) * cosf(phi2), cosf(theta2),
                    sinf(theta2) * sinf(phi2)};
      rlNormal3f(n3.x, n3.y, n3.z);
      rlTexCoord2f((float)(j + 1) / columns, (float)(i + 1) / rings);
      rlVertex3f(n3.x * radius, n3.y * radius, n3.z * radius);

      // Đỉnh 4
      Vector3 n4 = {sinf(theta2) * cosf(phi1), cosf(theta2),
                    sinf(theta2) * sinf(phi1)};
      rlNormal3f(n4.x, n4.y, n4.z);
      rlTexCoord2f((float)j / columns, (float)(i + 1) / rings);
      rlVertex3f(n4.x * radius, n4.y * radius, n4.z * radius);
    }
  }
  rlEnd();
  rlPopMatrix();
}

void InitFluidSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  fluidShader = LoadShader("fluid.vs", "fluid.fs");

  timeLocVS = GetShaderLocation(fluidShader, "u_timeVS");
  timeLocFS = GetShaderLocation(fluidShader, "u_timeFS");
  viewPosLoc = GetShaderLocation(fluidShader, "viewPos");

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;
}

void CastFluidSkill(Vector3 startPos, Vector3 target, float twistPhase,
                    float sizeScale) {
  (void)twistPhase;
  float clampedScale = ClampSizeScale(sizeScale);

  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].progress = 0.0f;
      emitters[i].spawnAccumulator = 0.0f;
      emitters[i].sizeScale = clampedScale;
      emitters[i].currentPos = startPos;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      float arcHeight = dist * 0.25f;

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f));
      emitters[i].p1.y += arcHeight;
      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.65f));
      emitters[i].p2.y += arcHeight;
      break;
    }
  }

  int burstCount = GetRandomValue(10, 16) * clampedScale;
  for (int s = 0; s < burstCount; s++) {
    ParticleConfig cfg = {0};
    cfg.position = startPos;
    cfg.velocity =
        (Vector3){Math_Mix(-150.0f, 250.0f, Random01()) * clampedScale,
                  Math_Mix(0.0f, 300.0f, Random01()) * clampedScale,
                  Math_Mix(-150.0f, 250.0f, Random01()) * clampedScale};
    cfg.force = (Vector3){0.0f, GRAVITY_Y, 0.0f};
    cfg.drag = FLUID_DRAG_SPLASH;
    cfg.radius = Math_Mix(3.0f, 8.0f, Random01()) * clampedScale * 3.5f;
    cfg.lifetime = Math_Mix(0.3f, 0.8f, Random01());
    cfg.colorStart = (Color){190, 235, 255, 220};
    cfg.colorEnd = (Color){40, 120, 200, 0};
    cfg.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
    SpawnParticle(cfg);
  }
}

void UpdateFluidSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    emitters[e].progress += dt * WATER_TRAVEL_SPEED;

    if (emitters[e].progress >= 1.0f) {
      emitters[e].active = false;
      TriggerFluidImpact(emitters[e].targetPos, emitters[e].sizeScale);
      continue;
    }

    emitters[e].currentPos =
        GetBezierPoint(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                       emitters[e].targetPos, emitters[e].progress);

    emitters[e].spawnAccumulator +=
        MIST_SPAWN_RATE * emitters[e].sizeScale * dt;
    int dropToSpawn = (int)emitters[e].spawnAccumulator;
    emitters[e].spawnAccumulator -= dropToSpawn;

    for (int k = 0; k < dropToSpawn; k++) {
      Vector3 randomDir = Vector3Normalize(
          (Vector3){Random01() - 0.5f, Random01() - 0.5f, Random01() - 0.5f});
      ParticleConfig cfgMist = {0};
      cfgMist.position =
          Vector3Add(emitters[e].currentPos,
                     Vector3Scale(randomDir, 6.0f * emitters[e].sizeScale));
      cfgMist.velocity =
          Vector3Scale(randomDir, Math_Mix(20.0f, 60.0f, Random01()) *
                                      emitters[e].sizeScale);
      cfgMist.force = (Vector3){0.0f, GRAVITY_Y * 0.8f, 0.0f};
      cfgMist.drag = FLUID_DRAG_MIST;
      cfgMist.radius =
          Math_Mix(1.5f, 4.0f, Random01()) * emitters[e].sizeScale * 2.5f;
      cfgMist.lifetime = Math_Mix(0.3f, 0.6f, Random01());
      cfgMist.colorStart = (Color){200, 245, 255, 180};
      cfgMist.colorEnd = (Color){100, 150, 200, 0};
      cfgMist.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
      SpawnParticle(cfgMist);
    }
  }
}

void DrawFluidSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      active = true;
      break;
    }
  }
  if (!active)
    return;

  float time = GetTime();

  rlDisableDepthMask();
  rlEnableBackfaceCulling(); // <--- THÊM DÒNG NÀY ĐỂ LỌC MẶT SAU
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(fluidShader);

  SetShaderValue(fluidShader, timeLocVS, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(fluidShader, timeLocFS, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(fluidShader, viewPosLoc, &camera.position,
                 SHADER_UNIFORM_VEC3);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    float radius = WATER_BASE_RADIUS * emitters[e].sizeScale;

    // Vẽ lưới 3D thực với mật độ 40x40 ô đa giác để biến đổi mượt mà
    RenderCustom3DSphere(emitters[e].currentPos, radius, 40, 40);
  }

  EndShaderMode();
  EndBlendMode();
  rlDisableBackfaceCulling(); // <--- TẮT ĐI SAU KHI VẼ XONG ĐỂ KHÔNG ẢNH HƯỞNG
                              // SKILL KHÁC
  rlEnableDepthMask();
}

void UnloadFluidSkill(void) { UnloadShader(fluidShader); }

int GetFluidSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      outProjectiles[count].position = emitters[i].currentPos;
      outProjectiles[count].radius = WATER_BASE_RADIUS * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateFluidProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        TriggerFluidImpact(emitters[i].currentPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}
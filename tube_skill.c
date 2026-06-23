#include "tube_skill.h"
#include "particle_system.h"
#include "raymath.h"
#include "rlgl.h"
#include "utils_math.h"
#include <math.h>

#define MAX_TUBE_EMITTERS 5

#define TUBE_TRAVEL_SPEED 1.2f
#define TUBE_BASE_RADIUS 12.0f
#define TUBE_SEGMENTS 30
#define TUBE_RADIAL_SEGMENTS 20
#define TUBE_UV_LENGTH_SCALE 3.0f

#define WATER_BODY_PULSE_AMPLITUDE 0.15f
#define WATER_BODY_PULSE_FREQUENCY 4.0f
#define WATER_BODY_PULSE_SPEED 5.0f

#define WATER_BODY_WOBBLE_AMPLITUDE 0.1f
#define WATER_BODY_WOBBLE_FREQUENCY 4.0f
#define WATER_BODY_WOBBLE_SPEED 8.0f

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

// Hàm nội suy mượt thay thế cho smoothstep của GLSL
static float Math_Smoothstep(float edge0, float edge1, float x) {
  float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static inline float ClampSizeScale(float scale) {
  return Clamp(scale, 0.2f, 3.0f);
}

static Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2,
                                   Vector3 p3, float t) {
  float u = 1.0f - t;
  Vector3 d = {0};
  d.x = 3.0f * u * u * (p1.x - p0.x) + 6.0f * u * t * (p2.x - p1.x) +
        3.0f * t * t * (p3.x - p2.x);
  d.y = 3.0f * u * u * (p1.y - p0.y) + 6.0f * u * t * (p2.y - p1.y) +
        3.0f * t * t * (p3.y - p2.y);
  d.z = 3.0f * u * u * (p1.z - p0.z) + 6.0f * u * t * (p2.z - p1.z) +
        3.0f * t * t * (p3.z - p2.z);
  return d;
}

static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                              float t) {
  float u = 1.0f - t;
  float tt = t * t;
  float uu = u * u;
  float uuu = uu * u;
  float ttt = tt * t;

  Vector3 p = Vector3Scale(p0, uuu);
  p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
  p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
  p = Vector3Add(p, Vector3Scale(p3, ttt));
  return p;
}

static void TriggerWaterBurst(Vector3 pos, float sizeScale) {
  int splashCount = GetRandomValue(20, 35) * sizeScale;
  for (int s = 0; s < splashCount; s++) {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = Math_Mix(300.0f, 600.0f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity = (Vector3){cosf(angle) * speed * cosf(pitch),
                             sinf(pitch) * speed + (300.0f * sizeScale),
                             sinf(angle) * speed * cosf(pitch)};
    cfg.force = (Vector3){0.0f, GRAVITY_Y, 0.0f};
    cfg.drag = FLUID_DRAG_SPLASH;
    cfg.radius = Math_Mix(3.0f, 9.0f, Random01()) * sizeScale * 3.5f;
    cfg.lifetime = Math_Mix(0.4f, 1.0f, Random01());
    cfg.colorStart = (Color){220, 245, 255, 240};
    cfg.colorEnd = (Color){80, 160, 230, 0};
    cfg.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
    SpawnParticle(cfg);
  }
}

static void RenderCustom3DTube(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                               float radius, float flowProgress) {
  Vector3 rings[TUBE_SEGMENTS + 1][TUBE_RADIAL_SEGMENTS];
  Vector3 normals[TUBE_SEGMENTS + 1][TUBE_RADIAL_SEGMENTS];

  float sinPhi[TUBE_RADIAL_SEGMENTS];
  float cosPhi[TUBE_RADIAL_SEGMENTS];
  for (int j = 0; j < TUBE_RADIAL_SEGMENTS; j++) {
    float phi = (float)j * (2.0f * PI) / (float)TUBE_RADIAL_SEGMENTS;
    sinPhi[j] = sinf(phi);
    cosPhi[j] = cosf(phi);
  }

  Vector3 tailTangent = {0}, headTangent = {0};
  Vector3 tailCenter = {0}, headCenter = {0};
  float tailRadius = 0.0f, headRadius = 0.0f;
  float time = GetTime();

  for (int i = 0; i <= TUBE_SEGMENTS; i++) {
    float t = (float)i / (float)TUBE_SEGMENTS;
    float currentT = t * flowProgress;

    Vector3 pos = GetBezierPoint(p0, p1, p2, p3, currentT);
    Vector3 tangent =
        Vector3Normalize(GetBezierDerivative(p0, p1, p2, p3, currentT));

    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
    if (fabsf(tangent.y) > 0.99f)
      up = (Vector3){1.0f, 0.0f, 0.0f};

    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, tangent));
    up = Vector3CrossProduct(tangent, right);

    // --- CHỈNH LẠI HÌNH DÁNG THEO Ý BẠN ---
    // 1. Giữ nguyên form bo tròn 2 đầu mà bạn ưng ý ở bản trước
    float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI)));

    // 2. Vuốt nhỏ dần về phía đuôi (t=0 là đuôi chỉ còn 15% kích thước, t=1 là
    // đầu giữ nguyên 100%)
    float tailTaper = 0.15f + 0.85f * t;

    float capsuleCurve = baseCapsule * tailTaper;
    float bodyNoise = 1.0f + 0.18f * sinf(t * 22.0f + time * 7.0f) +
                      0.08f * sinf(t * 55.0f - time * 13.0f);

    capsuleCurve *= bodyNoise;

    float headWeight = 1.0f + 0.2f * t;

    if (i == 0) {
      tailTangent = tangent;
      tailCenter = pos;
    }
    if (i == TUBE_SEGMENTS) {
      headTangent = tangent;
      headCenter = pos;
    }

    float wobble = WATER_BODY_WOBBLE_AMPLITUDE *
                   sinf(t * PI * WATER_BODY_WOBBLE_FREQUENCY +
                        time * WATER_BODY_WOBBLE_SPEED);

    Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)),
                                   Vector3Scale(right, sinf(wobble)));

    Vector3 twistedRight =
        Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));

    for (int j = 0; j < TUBE_RADIAL_SEGMENTS; j++) {
      Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosPhi[j]),
                                  Vector3Scale(twistedUp, sinPhi[j]));
      normals[i][j] = normal;
      float phi = (float)j * (2.0f * PI) / (float)TUBE_RADIAL_SEGMENTS;
      float deform1 = sinf(t * 18.0f + phi * 3.0f + time * 10.0f);

      float deform2 = sinf(t * 9.0f - phi * 5.0f - time * 6.0f);

      float deform = 1.0f + deform1 * 0.12f + deform2 * 0.08f;

      rings[i][j] =
          Vector3Add(pos, Vector3Scale(normal, radius * capsuleCurve *
                                                   headWeight * deform));
    }

    if (i == 0)
      tailRadius = radius * capsuleCurve * headWeight;
    if (i == TUBE_SEGMENTS)
      headRadius = radius * capsuleCurve * headWeight;
  }

  rlPushMatrix();
  rlBegin(RL_QUADS);
  for (int i = 0; i < TUBE_SEGMENTS; i++) {
    float v1 = (float)i / (float)TUBE_SEGMENTS * TUBE_UV_LENGTH_SCALE;
    float v2 = (float)(i + 1) / (float)TUBE_SEGMENTS * TUBE_UV_LENGTH_SCALE;

    for (int j = 0; j < TUBE_RADIAL_SEGMENTS; j++) {
      int nextJ = (j + 1) % TUBE_RADIAL_SEGMENTS;
      float u1 = (float)j / (float)TUBE_RADIAL_SEGMENTS;
      float u2 = (float)(j + 1) / (float)TUBE_RADIAL_SEGMENTS;

      rlNormal3f(normals[i][j].x, normals[i][j].y, normals[i][j].z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(rings[i][j].x, rings[i][j].y, rings[i][j].z);
      rlNormal3f(normals[i][nextJ].x, normals[i][nextJ].y, normals[i][nextJ].z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(rings[i][nextJ].x, rings[i][nextJ].y, rings[i][nextJ].z);
      rlNormal3f(normals[i + 1][nextJ].x, normals[i + 1][nextJ].y,
                 normals[i + 1][nextJ].z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(rings[i + 1][nextJ].x, rings[i + 1][nextJ].y,
                 rings[i + 1][nextJ].z);
      rlNormal3f(normals[i + 1][j].x, normals[i + 1][j].y, normals[i + 1][j].z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(rings[i + 1][j].x, rings[i + 1][j].y, rings[i + 1][j].z);
    }
  }
  rlEnd();

  rlBegin(RL_TRIANGLES);

  // Đuôi nhỏ, vát tù mượt mà
  Vector3 tailApex = Vector3Subtract(
      tailCenter, Vector3Scale(tailTangent, tailRadius * 0.25f));
  float tailV_apex = -0.1f;
  float tailV_ring = 0.0f;
  for (int j = 0; j < TUBE_RADIAL_SEGMENTS; j++) {
    int nextJ = (j + 1) % TUBE_RADIAL_SEGMENTS;
    float u1 = (float)j / (float)TUBE_RADIAL_SEGMENTS;
    float u2 = (float)(nextJ) / (float)TUBE_RADIAL_SEGMENTS;
    float uCenter = (u1 + u2) * 0.5f;

    rlNormal3f(-tailTangent.x, -tailTangent.y, -tailTangent.z);
    rlTexCoord2f(uCenter, tailV_apex);
    rlVertex3f(tailApex.x, tailApex.y, tailApex.z);
    rlNormal3f(normals[0][j].x, normals[0][j].y, normals[0][j].z);
    rlTexCoord2f(u1, tailV_ring);
    rlVertex3f(rings[0][j].x, rings[0][j].y, rings[0][j].z);
    rlNormal3f(normals[0][nextJ].x, normals[0][nextJ].y, normals[0][nextJ].z);
    rlTexCoord2f(u2, tailV_ring);
    rlVertex3f(rings[0][nextJ].x, rings[0][nextJ].y, rings[0][nextJ].z);
  }

  // Đầu giữ nguyên độ bo tròn ưng ý
  Vector3 headApex =
      Vector3Add(headCenter, Vector3Scale(headTangent, headRadius * 0.8f));

  float headV_ring = TUBE_UV_LENGTH_SCALE;
  float headV_apex = TUBE_UV_LENGTH_SCALE + 0.1f;
  for (int j = 0; j < TUBE_RADIAL_SEGMENTS; j++) {
    int nextJ = (j + 1) % TUBE_RADIAL_SEGMENTS;
    float u1 = (float)j / (float)TUBE_RADIAL_SEGMENTS;
    float u2 = (float)(nextJ) / (float)TUBE_RADIAL_SEGMENTS;
    float uCenter = (u1 + u2) * 0.5f;

    Vector3 avgNormal1 =
        Vector3Normalize(Vector3Add(normals[TUBE_SEGMENTS][j], headTangent));
    Vector3 avgNormal2 = Vector3Normalize(
        Vector3Add(normals[TUBE_SEGMENTS][nextJ], headTangent));

    rlNormal3f(headTangent.x, headTangent.y, headTangent.z);
    rlTexCoord2f(uCenter, headV_apex);
    rlVertex3f(headApex.x, headApex.y, headApex.z);
    rlNormal3f(avgNormal1.x, avgNormal1.y, avgNormal1.z);
    rlTexCoord2f(u1, headV_ring);
    rlVertex3f(rings[TUBE_SEGMENTS][j].x, rings[TUBE_SEGMENTS][j].y,
               rings[TUBE_SEGMENTS][j].z);
    rlNormal3f(avgNormal2.x, avgNormal2.y, avgNormal2.z);
    rlTexCoord2f(u2, headV_ring);
    rlVertex3f(rings[TUBE_SEGMENTS][nextJ].x, rings[TUBE_SEGMENTS][nextJ].y,
               rings[TUBE_SEGMENTS][nextJ].z);
  }

  rlEnd();
  rlPopMatrix();
}

void InitTubeSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  tubeShader = LoadShader("tube.vs", "tube.fs");
  timeLoc = GetShaderLocation(tubeShader, "u_time");
  viewPosLoc = GetShaderLocation(tubeShader, "viewPos");
  uvLengthLoc =
      GetShaderLocation(tubeShader, "u_uvLength"); // Lấy chung cho cả VS và FS
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    emitters[i].active = false;
  }
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
      TriggerWaterBurst(emitters[e].p3, emitters[e].sizeScale);
      continue;
    }
    emitters[e].headPos =
        GetBezierPoint(emitters[e].p0, emitters[e].p1, emitters[e].p2,
                       emitters[e].p3, emitters[e].progress);

    if (GetRandomValue(0, 100) < 60) {
      ParticleConfig cfgMist = {0};
      cfgMist.position = emitters[e].headPos;
      cfgMist.velocity =
          (Vector3){(Random01() - 0.5f) * 40.0f, Random01() * 50.0f,
                    (Random01() - 0.5f) * 40.0f};
      cfgMist.force = (Vector3){0.0f, GRAVITY_Y * 0.5f, 0.0f};
      cfgMist.drag = 2.0f;
      cfgMist.radius = Math_Mix(2.0f, 5.0f, Random01()) * emitters[e].sizeScale;
      cfgMist.lifetime = Math_Mix(0.2f, 0.5f, Random01());
      cfgMist.colorStart = (Color){200, 245, 255, 180};
      cfgMist.colorEnd = (Color){100, 150, 200, 0};
      cfgMist.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
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

  for (int e = 0; e < MAX_TUBE_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    float radius = TUBE_BASE_RADIUS * emitters[e].sizeScale;
    RenderCustom3DTube(emitters[e].p0, emitters[e].p1, emitters[e].p2,
                       emitters[e].p3, radius, emitters[e].progress);
  }

  EndShaderMode();
  EndBlendMode();
  rlDisableBackfaceCulling();
  rlEnableDepthMask();
}

void UnloadTubeSkill(void) { UnloadShader(tubeShader); }

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
        TriggerWaterBurst(emitters[i].headPos, emitters[i].sizeScale);
        return;
      }
      count++;
    }
  }
}
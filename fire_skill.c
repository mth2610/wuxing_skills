#include "fire_skill.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PARTICLES 2400
#define MAX_EMITTERS 10

// ---- Hành trình theo thời gian ----
#define FIRE_TRAVEL_SPEED 0.9f
#define FIRE_CAST_DURATION 0.45f
#define FIRE_PROGRESS_MAX 2.5f // Bay vút lên trời

// ---- Đường cong Bezier ----
#define BEZIER_CONTROL1_X_OFFSET 300.0f
#define BEZIER_CONTROL1_Y_OFFSET 200.0f
#define BEZIER_CONTROL2_Y_OFFSET 400.0f

// ---- Tia lửa & Va chạm ----
#define CAST_BURST_COUNT_MIN 20
#define CAST_BURST_COUNT_MAX 35
#define CAST_BURST_VEL_X_MIN -200.0f
#define CAST_BURST_VEL_X_MAX 300.0f
#define CAST_BURST_VEL_Y_MIN -400.0f
#define CAST_BURST_VEL_Y_MAX 100.0f
#define CAST_BURST_RADIUS_MIN 2.5f
#define CAST_BURST_RADIUS_MAX 6.5f
#define CAST_BURST_LIFETIME_MIN 0.3f
#define CAST_BURST_LIFETIME_MAX 0.8f

#define SPLASH_BURST_COUNT_MIN 4
#define SPLASH_BURST_COUNT_MAX 8
#define SPLASH_VEL_X_MIN -400.0f
#define SPLASH_VEL_X_MAX 400.0f
#define SPLASH_VEL_Y_MIN -400.0f
#define SPLASH_VEL_Y_MAX 100.0f
#define SPLASH_RADIUS_MIN 2.5f
#define SPLASH_RADIUS_MAX 8.0f
#define SPLASH_LIFETIME_MIN 0.4f
#define SPLASH_LIFETIME_MAX 1.2f

// ---- Đầu rồng ----
#define DRAGON_HEAD_BASE_SCALE 0.12f
#define DRAGON_JAW_OSCILLATION_SPEED 30.0f
#define DRAGON_JAW_OSCILLATION_AMP 0.15f
#define DRAGON_HEAD_ORIGIN_X_FACTOR 0.2f

// Tham chiếu camera từ main để chiếu tọa độ 3D sang màn hình
extern Camera3D camera;

static float Math_Mix(float x, float y, float a) {
  return x * (1.0f - a) + y * a;
}

static float SmoothStep01(float x) {
  x = Clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

static Color FireDensityColor(float density, float alpha) {
  unsigned char v = (unsigned char)Clamp(density * 255.0f, 0.0f, 255.0f);
  unsigned char a = (unsigned char)Clamp(alpha * 255.0f, 0.0f, 255.0f);
  return (Color){v, v, v, a};
}

static float Random01(void) {
  return (float)GetRandomValue(0, 10000) / 10000.0f;
}

// Lấy các điểm cách đều nhau một khoảng cố định dọc theo đường đi (path) trong không gian 3D
static int SamplePath(const Vector3* path, int pathCount, float spacing, Vector3* outSegments, int maxSegments) {
  if (pathCount == 0 || maxSegments <= 0) return 0;

  outSegments[0] = path[0];
  int segmentIndex = 1;
  float targetDist = spacing;
  float accumulatedDist = 0.0f;

  for (int i = 0; i < pathCount - 1; i++) {
    Vector3 p1 = path[i];
    Vector3 p2 = path[i + 1];
    float d = Vector3Distance(p1, p2);

    while (accumulatedDist + d >= targetDist) {
      float needed = targetDist - accumulatedDist;
      float t = (d > 0.0f) ? (needed / d) : 0.0f;
      Vector3 interpPos = Vector3Lerp(p1, p2, t);

      outSegments[segmentIndex] = interpPos;
      segmentIndex++;
      if (segmentIndex >= maxSegments) {
        return segmentIndex;
      }
      targetDist += spacing;
    }
    accumulatedDist += d;
  }

  return segmentIndex;
}

// =============================================================================
// Struct dữ liệu
// =============================================================================

#define EMITTER_PATH_MAX 360
#define MAX_SAMPLED_SEGMENTS 96

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 p1, p2;
  float headProgress;
  float twistPhase;
  float sizeScale; // Hệ số thu phóng rồng lửa
  Vector3 path[EMITTER_PATH_MAX];
  int pathCount;
  Vector3 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
} FireEmitter;

#define PARTICLE_HISTORY_COUNT 6

typedef struct {
  bool active;
  Vector3 position;
  Vector3 velocity;
  float radius;
  float lifetime;
  float maxLifetime;
  int type; // 1: Wind Trails, 2: Spark Bursts, 3: Floating Embers, 4: Fire Vortex, 5: Fire Breath, 6: Shockwave Ring, 7: Ambient Light Flash
  Vector3 history[PARTICLE_HISTORY_COUNT];
  int historyCount;
  float angle;
} FireParticle;

static FireParticle firePool[MAX_PARTICLES];
static FireEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader fireShader;
static int timeLoc;
static Texture2D dragonHeadTex;

// =============================================================================
// Toán học: Đường bay Thăng Long trong không gian 3D
// =============================================================================

static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
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

static Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t) {
  float u = 1.0f - t;
  Vector3 tangent = Vector3Add(
      Vector3Add(Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
                 Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t)),
      Vector3Scale(Vector3Subtract(target, p2), 3.0f * t * t));
  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){1.0f, 0.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static Vector3 GetDragonPathPos(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t) {
  if (t <= 1.0f)
    return GetBezierPoint(p0, p1, p2, target, t);

  float over = t - 1.0f;
  Vector3 vIn = Vector3Scale(Vector3Subtract(target, p2), 3.0f);
  if (Vector3Length(vIn) > 800.0f)
    vIn = Vector3Scale(Vector3Normalize(vIn), 800.0f);

  float upwardSpeed = 1600.0f;
  float waveFreq = 12.0f;
  float waveAmp = 200.0f;

  // Trục Y hướng lên trời (độ cao), trục X-Z là mặt đất phẳng phẳng
  Vector3 idealUpPos = {
      target.x + sinf(over * waveFreq) * waveAmp,
      target.y + upwardSpeed * over,
      target.z
  };
  Vector3 inertiaPos = {
      target.x + vIn.x * over,
      target.y + vIn.y * over,
      target.z + vIn.z * over
  };

  float blend = fminf(over * 3.5f, 1.0f);
  blend = blend * blend * (3.0f - 2.0f * blend);

  return (Vector3){
      Math_Mix(inertiaPos.x, idealUpPos.x, blend),
      Math_Mix(inertiaPos.y, idealUpPos.y, blend),
      Math_Mix(inertiaPos.z, idealUpPos.z, blend)
  };
}

static Vector3 GetDragonPathTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t) {
  if (t <= 1.0f)
    return GetBezierTangent(p0, p1, p2, target, t);
  float delta = 0.01f;
  Vector3 tangent = Vector3Subtract(GetDragonPathPos(p0, p1, p2, target, t + delta),
                                    GetDragonPathPos(p0, p1, p2, target, t));
  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){0.0f, 1.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static Vector2 GetTexturePointInWorld(Vector2 headPos, Vector2 tangent, float scaleX, float scaleY, Vector2 origin, Vector2 texPt, float flipY) {
  float lx = (texPt.x * scaleX) - origin.x;
  float ly = (texPt.y * scaleY - origin.y) * flipY;
  Vector2 perp = {-tangent.y, tangent.x};
  Vector2 worldPos = {
    headPos.x + tangent.x * lx + perp.x * ly,
    headPos.y + tangent.y * lx + perp.y * ly
  };
  return worldPos;
}

static void SpawnParticle(int type, Vector3 pos, Vector3 vel, float baseRadius, float maxLife) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    int index = (lastUsedParticle + i) % MAX_PARTICLES;
    if (!firePool[index].active) {
      firePool[index].type = type;
      firePool[index].position = pos;
      firePool[index].velocity = vel;
      firePool[index].radius = baseRadius;
      firePool[index].maxLifetime = maxLife;
      firePool[index].lifetime = maxLife;
      firePool[index].active = true;
      firePool[index].historyCount = 0;
      for (int h = 0; h < PARTICLE_HISTORY_COUNT; h++) {
        firePool[index].history[h] = pos;
      }
      firePool[index].angle = Random01() * 3.14159265f * 2.0f;
      lastUsedParticle = (index + 1) % MAX_PARTICLES;
      break;
    }
  }
}

static void TriggerFireImpact(Vector3 pos, float sizeScale) {
  // 1. Tia lửa phát tán nhanh trong không gian 3D (Radial Spark Burst)
  int sparkCount = GetRandomValue(12, 18) * sizeScale;
  for (int s = 0; s < sparkCount; s++) {
    float angle = Random01() * 3.14159265f * 2.0f;
    float pitch = (Random01() - 0.5f) * 3.14159265f;
    float speed = Math_Mix(160.0f, 420.0f, Random01()) * sizeScale;
    Vector3 vel = {
      cosf(angle) * speed * cosf(pitch),
      sinf(pitch) * speed,
      sinf(angle) * speed * cosf(pitch)
    };
    float rad = Math_Mix(0.8f, 2.2f, Random01()) * sizeScale;
    float life = Math_Mix(0.3f, 0.7f, Random01());
    SpawnParticle(2, pos, vel, rad, life);
  }

  // 2. Hạt bụi lửa bốc lên theo gió (Dispersing Noise Particles)
  int disperseCount = GetRandomValue(14, 22) * sizeScale;
  for (int v = 0; v < disperseCount; v++) {
    float angle = Random01() * 3.14159265f * 2.0f;
    float speed = Math_Mix(80.0f, 260.0f, Random01()) * sizeScale;
    // Tán rộng trên mặt phẳng X-Z và đẩy vận tốc Y dương (hướng lên trên)
    Vector3 vel = {
      cosf(angle) * speed,
      (Math_Mix(80.0f, 260.0f, Random01()) + 80.0f) * sizeScale,
      sinf(angle) * speed
    };
    float rad = Math_Mix(2.5f, 6.5f, Random01()) * sizeScale;
    float life = Math_Mix(0.6f, 1.3f, Random01());
    SpawnParticle(4, pos, vel, rad, life);
  }

  // 3. Vòng sóng kích nổ (Fiery Shockwave Rings - Type 6)
  SpawnParticle(6, pos, (Vector3){0, 0, 0}, 60.0f * sizeScale, 0.40f); // Vòng ngoài
  SpawnParticle(6, pos, (Vector3){0, 0, 0}, 33.0f * sizeScale, 0.25f);  // Vòng trong

  // 4. Ánh chớp phát sáng môi trường (Ambient Light Flash - Type 7)
  SpawnParticle(7, pos, (Vector3){0, 0, 0}, 110.0f * sizeScale, 0.25f);

  // 5. Lưỡi lửa bùng nổ lên phía trên
  int flameTongues = GetRandomValue(4, 7) * sizeScale;
  for (int f = 0; f < flameTongues; f++) {
    float angle = Random01() * 3.14159265f * 2.0f;
    float speed = Math_Mix(140.0f, 340.0f, Random01()) * sizeScale;
    Vector3 vel = {
      cosf(angle) * speed,
      (float)GetRandomValue(50, 200) * sizeScale,
      sinf(angle) * speed
    };
    SpawnParticle(5, pos, vel, Math_Mix(2.0f, 5.2f, Random01()) * sizeScale,
                  Math_Mix(0.22f, 0.46f, Random01()));
  }
}

// =============================================================================
// Core API
// =============================================================================

void InitFireSkill(int screenWidth, int screenHeight) {
  canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
  fireShader = LoadShader(0, "fire.fs");
  timeLoc = GetShaderLocation(fireShader, "u_time");
  dragonHeadTex = LoadTexture("dragon_head.png");

  for (int i = 0; i < MAX_PARTICLES; i++)
    firePool[i].active = false;
  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;
}

void CastFireSkill(Vector3 startPos, Vector3 target, float twistPhase, float sizeScale) {
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].headProgress = 0.0f;
      emitters[i].twistPhase = twistPhase;
      emitters[i].sizeScale = sizeScale;
      emitters[i].pathCount = 1;
      emitters[i].path[0] = startPos;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      // Vector perpendicular phẳng trên trục X-Z (quay quanh trục Y up)
      Vector3 perp = (Vector3){ -dir.z, 0.0f, dir.x };

      // twistPhase biểu diễn góc phát tán rẽ quạt đối xứng quanh đường thẳng hướng đi
      float spreadScale = sinf(twistPhase);

      emitters[i].p1 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.45f)), Vector3Scale(perp, spreadScale * dist * 0.35f));
      emitters[i].p2 = Vector3Add(target, Vector3Scale(perp, -spreadScale * dist * 0.15f));
      break;
    }
  }

  // Tạo chớp sáng năng lượng lúc bắt đầu tung chiêu
  SpawnParticle(7, startPos, (Vector3){0, 0, 0}, 100.0f * sizeScale, 0.30f);

  int burstCount = GetRandomValue(8, 14) * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    Vector3 vel = {
      (float)GetRandomValue(-200, 300) * sizeScale,
      (float)GetRandomValue(100, 400) * sizeScale, // Hướng lên theo trục Y dương
      (float)GetRandomValue(-200, 300) * sizeScale
    };
    float rad = Math_Mix(CAST_BURST_RADIUS_MIN, CAST_BURST_RADIUS_MAX, Random01()) * sizeScale;
    float life = Math_Mix(CAST_BURST_LIFETIME_MIN, CAST_BURST_LIFETIME_MAX, Random01());
    SpawnParticle(2, startPos, vel, rad, life);
  }
}

void UpdateFireSkill(float dt) {
  float time = GetTime();

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float prevProgress = emitters[e].headProgress;
    float targetProgress = prevProgress + dt * FIRE_TRAVEL_SPEED;
    if (targetProgress >= FIRE_PROGRESS_MAX) {
      targetProgress = FIRE_PROGRESS_MAX;
    }

    // Chia nhỏ bước mô phỏng để ghi lại quỹ đạo bay mịn
    float step = 0.008f;
    float currentProgress = prevProgress;
    while (currentProgress < targetProgress) {
      currentProgress += step;
      if (currentProgress > targetProgress) currentProgress = targetProgress;

      Vector3 pos = GetDragonPathPos(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                                     emitters[e].targetPos, currentProgress);

      float dist = 0.0f;
      if (emitters[e].pathCount > 0) {
        dist = Vector3Distance(pos, emitters[e].path[0]);
      } else {
        dist = Vector3Distance(pos, emitters[e].startPos);
      }

      if (dist > 1.5f || emitters[e].pathCount == 0) {
        for (int h = EMITTER_PATH_MAX - 1; h > 0; h--) {
          emitters[e].path[h] = emitters[e].path[h - 1];
        }
        emitters[e].path[0] = pos;
        if (emitters[e].pathCount < EMITTER_PATH_MAX) {
          emitters[e].pathCount++;
        }
      }
    }
    emitters[e].headProgress = targetProgress;

    // Hủy bỏ emitter nếu đã kết thúc đường đi
    if (emitters[e].headProgress >= FIRE_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    // Lấy các phân đoạn mẫu dọc ribbon với khoảng cách rộng hơn để giảm chi phí dựng hình
    emitters[e].sampledCount = SamplePath(emitters[e].path, emitters[e].pathCount, 8.0f, emitters[e].sampledPath, MAX_SAMPLED_SEGMENTS);
    if (prevProgress <= 1.0f && emitters[e].headProgress > 1.0f) {
      TriggerFireImpact(emitters[e].targetPos, emitters[e].sizeScale);
    }

    // Xác định thông tin của đầu rồng để tạo hiệu ứng
    Vector3 headPos = (emitters[e].pathCount > 0) ? emitters[e].path[0] : emitters[e].startPos;
    Vector3 tangent = GetDragonPathTangent(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                                           emitters[e].targetPos, emitters[e].headProgress);
    Vector3 visualHeadPos = headPos;

    if (emitters[e].headProgress <= 1.0f) {
      float jawOsc = sinf(time * DRAGON_JAW_OSCILLATION_SPEED) * DRAGON_JAW_OSCILLATION_AMP;
      float scaleY = DRAGON_HEAD_BASE_SCALE * (1.0f + jawOsc) * emitters[e].sizeScale;
      
      // Tính vị trí phun lửa 3D của miệng rồng theo tangent đầu
      float mouthOffset = (dragonHeadTex.width * DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale) * 0.35f;
      Vector3 mouthWorldPos = Vector3Add(visualHeadPos, Vector3Scale(tangent, mouthOffset));

      int breathCount = GetRandomValue(2, 4) * emitters[e].sizeScale;
      for (int b = 0; b < breathCount; b++) {
        float angleOffset = (float)GetRandomValue(-18, 18) * DEG2RAD;
        float pitchOffset = (float)GetRandomValue(-18, 18) * DEG2RAD;
        
        Vector3 perpY = (Vector3){ -tangent.z, 0.0f, tangent.x };
        if (Vector3Length(perpY) == 0.0f) perpY = (Vector3){ 0.0f, 0.0f, 1.0f };
        perpY = Vector3Normalize(perpY);
        Vector3 perpUp = Vector3Normalize(Vector3CrossProduct(tangent, perpY));

        Vector3 dir = tangent;
        dir = Vector3Add(Vector3Scale(dir, cosf(angleOffset)), Vector3Scale(perpY, sinf(angleOffset)));
        dir = Vector3Add(Vector3Scale(dir, cosf(pitchOffset)), Vector3Scale(perpUp, sinf(pitchOffset)));
        dir = Vector3Normalize(dir);

        float speed = (float)GetRandomValue(420, 760);
        Vector3 vel = Vector3Scale(dir, speed);
        float rad = Math_Mix(1.3f, 3.2f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.16f, 0.36f, Random01());
        SpawnParticle(5, mouthWorldPos, vel, rad, life);
      }
    }

    // Phát ra dải quầng gió và tàn lửa xung quanh thân dọc theo đường đi
    if (emitters[e].sampledCount > 1) {
      int wispsToSpawn = GetRandomValue(11, 18) * emitters[e].sizeScale;
      for (int k = 0; k < wispsToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector3 purePos = emitters[e].sampledPath[idx];

        Vector3 pureTangent;
        if (idx < emitters[e].sampledCount - 1)
          pureTangent = Vector3Normalize(Vector3Subtract(purePos, emitters[e].sampledPath[idx + 1]));
        else
          pureTangent = Vector3Normalize(Vector3Subtract(emitters[e].sampledPath[idx - 1], purePos));

        Vector3 purePerp = {-pureTangent.z, 0.0f, pureTangent.x};
        if (Vector3Length(purePerp) == 0.0f) purePerp = (Vector3){0, 0, 1};
        purePerp = Vector3Normalize(purePerp);

        float distFromHead = idx * 8.0f;
        float normDist = (emitters[e].sampledCount > 1) ? ((float)idx / (float)(emitters[e].sampledCount - 1)) : 0.0f;
        float taper = powf(1.0f - normDist, 0.5f);
        float coreRad = Math_Mix(2.2f, 8.0f, taper) * emitters[e].sizeScale;

        float spiralPhase = time * 11.0f - distFromHead * 0.075f + emitters[e].twistPhase + Random01() * PI * 2.0f;
        float coilRad = coreRad * Math_Mix(0.75f, 1.65f, Random01());

        Vector3 spawnPos = {
          purePos.x + purePerp.x * cosf(spiralPhase) * coilRad,
          purePos.y + sinf(spiralPhase) * coilRad, // Thay đổi độ cao xoắn trục Y
          purePos.z + purePerp.z * cosf(spiralPhase) * coilRad
        };

        spawnPos.x += Math_Mix(-2.0f, 2.0f, Random01()) * emitters[e].sizeScale;
        spawnPos.y += Math_Mix(-2.0f, 2.0f, Random01()) * emitters[e].sizeScale;
        spawnPos.z += Math_Mix(-2.0f, 2.0f, Random01()) * emitters[e].sizeScale;

        float cosVal = cosf(spiralPhase);
        float sinVal = sinf(spiralPhase);
        Vector3 radDir = { purePerp.x * cosVal, sinVal, purePerp.z * cosVal };
        Vector3 rotDir = { -purePerp.x * sinVal, cosVal, -purePerp.z * sinVal };

        float ribbonSpeed = Math_Mix(105.0f, 220.0f, Random01());
        float orbitSpeed = Math_Mix(120.0f, 220.0f, Random01());
        float expandSpeed = Math_Mix(-8.0f, 12.0f, Random01());

        Vector3 windVel = {
          -pureTangent.x * ribbonSpeed + rotDir.x * orbitSpeed + radDir.x * expandSpeed,
          -pureTangent.y * ribbonSpeed + rotDir.y * orbitSpeed + radDir.y * expandSpeed,
          -pureTangent.z * ribbonSpeed + rotDir.z * orbitSpeed + radDir.z * expandSpeed
        };

        float rad = Math_Mix(2.0f, 4.8f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.32f, 0.62f, Random01());
        SpawnParticle(1, spawnPos, windVel, rad, life);
      }

      // Embers (Type 3)
      int embersToSpawn = GetRandomValue(3, 5) * emitters[e].sizeScale;
      for (int k = 0; k < embersToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector3 purePos = emitters[e].sampledPath[idx];

        Vector3 pureTangent;
        if (idx < emitters[e].sampledCount - 1)
          pureTangent = Vector3Normalize(Vector3Subtract(purePos, emitters[e].sampledPath[idx + 1]));
        else
          pureTangent = Vector3Normalize(Vector3Subtract(emitters[e].sampledPath[idx - 1], purePos));

        Vector3 purePerp = {-pureTangent.z, 0.0f, pureTangent.x};
        if (Vector3Length(purePerp) == 0.0f) purePerp = (Vector3){0, 0, 1};
        purePerp = Vector3Normalize(purePerp);

        float distFromHead = idx * 8.0f;
        float normDist = (emitters[e].sampledCount > 1) ? ((float)idx / (float)(emitters[e].sampledCount - 1)) : 0.0f;
        float taper = powf(1.0f - normDist, 0.5f);
        float coreRad = Math_Mix(2.2f, 8.0f, taper) * emitters[e].sizeScale;

        float spiralPhase = time * 8.0f - distFromHead * 0.06f + emitters[e].twistPhase + Random01() * PI * 2.0f;
        float coilRad = coreRad * Math_Mix(1.1f, 2.25f, Random01());

        Vector3 spawnPos = {
          purePos.x + purePerp.x * cosf(spiralPhase) * coilRad,
          purePos.y + sinf(spiralPhase) * coilRad,
          purePos.z + purePerp.z * cosf(spiralPhase) * coilRad
        };

        spawnPos.x += Math_Mix(-2.5f, 2.5f, Random01()) * emitters[e].sizeScale;
        spawnPos.y += Math_Mix(-2.5f, 2.5f, Random01()) * emitters[e].sizeScale;
        spawnPos.z += Math_Mix(-2.5f, 2.5f, Random01()) * emitters[e].sizeScale;

        float cosVal = cosf(spiralPhase);
        float sinVal = sinf(spiralPhase);
        Vector3 radDir = { purePerp.x * cosVal, sinVal, purePerp.z * cosVal };
        Vector3 rotDir = { -purePerp.x * sinVal, cosVal, -purePerp.z * sinVal };

        float forwardSpeed = Math_Mix(70.0f, 150.0f, Random01());
        float orbitSpeed = Math_Mix(95.0f, 170.0f, Random01());
        float expandSpeed = Math_Mix(-4.0f, 18.0f, Random01());

        Vector3 vel = {
          -pureTangent.x * forwardSpeed + rotDir.x * orbitSpeed + radDir.x * expandSpeed,
          -pureTangent.y * forwardSpeed + rotDir.y * orbitSpeed + radDir.y * expandSpeed,
          -pureTangent.z * forwardSpeed + rotDir.z * orbitSpeed + radDir.z * expandSpeed
        };
        float rad = Math_Mix(2.8f, 6.2f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.62f, 1.15f, Random01());
        SpawnParticle(3, spawnPos, vel, rad, life);
      }
    }
  }

  // 2. CẬP NHẬT CÁC HẠT LỬA
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!firePool[i].active)
      continue;

    firePool[i].lifetime -= dt;
    if (firePool[i].lifetime <= 0.0f) {
      firePool[i].active = false;
      continue;
    }

    for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--) {
      firePool[i].history[h] = firePool[i].history[h - 1];
    }
    firePool[i].history[0] = firePool[i].position;
    if (firePool[i].historyCount < PARTICLE_HISTORY_COUNT) {
      firePool[i].historyCount++;
    }

    firePool[i].velocity.x *= (1.0f - 2.5f * dt);
    firePool[i].velocity.y *= (1.0f - 2.5f * dt);
    firePool[i].velocity.z *= (1.0f - 2.5f * dt);

    if (firePool[i].type == 1) {
      firePool[i].velocity.y += 45.0f * dt; // Bốc lên theo trục Y dương
    } else if (firePool[i].type == 3) {
      firePool[i].velocity.y += 75.0f * dt; // Bốc lên theo trục Y dương
    } else {
      firePool[i].velocity.y -= 180.0f * dt; // Trọng lực hút xuống
    }

    if (firePool[i].type == 1) {
      float pX = firePool[i].position.x * 0.015f;
      float pZ = firePool[i].position.z * 0.015f;
      firePool[i].velocity.x += sinf(pZ + time * 8.0f) * 85.0f * dt;
      firePool[i].velocity.z += cosf(pX + time * 8.0f) * 85.0f * dt;
    }
    else if (firePool[i].type == 3) {
      firePool[i].angle += dt * 6.0f;
      firePool[i].velocity.x += sinf(firePool[i].angle) * 70.0f * dt;
      firePool[i].velocity.z += cosf(firePool[i].angle) * 70.0f * dt;
    }
    else if (firePool[i].type == 4) {
      firePool[i].velocity.x *= (1.0f - 3.5f * dt);
      firePool[i].velocity.z *= (1.0f - 3.5f * dt);
      firePool[i].velocity.y += 260.0f * dt; // Bay ngược lên cuộn xoáy
      float pX = firePool[i].position.x * 0.02f;
      float pZ = firePool[i].position.z * 0.02f;
      firePool[i].velocity.x += sinf(pZ + time * 10.0f) * 180.0f * dt;
      firePool[i].velocity.z += cosf(pX + time * 10.0f) * 180.0f * dt;
    }
    else if (firePool[i].type == 5) {
      firePool[i].velocity.x *= (1.0f - 4.5f * dt);
      firePool[i].velocity.y *= (1.0f - 4.5f * dt);
      firePool[i].velocity.z *= (1.0f - 4.5f * dt);
      firePool[i].velocity.y -= 90.0f * dt;
    }
    else if (firePool[i].type == 7) {
      firePool[i].velocity = (Vector3){0, 0, 0};
    }

    firePool[i].position.x += firePool[i].velocity.x * dt;
    firePool[i].position.y += firePool[i].velocity.y * dt;
    firePool[i].position.z += firePool[i].velocity.z * dt;
  }
}

void DrawFireSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) { active = true; break; }
  }
  if (!active) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
      if (firePool[i].active) { active = true; break; }
    }
  }
  if (!active) return;

  float time = GetTime();

  BeginTextureMode(canvasTexture);
  ClearBackground(BLANK);

  // =======================================================
  // 1. VẼ KHÓI MỜ BỌC ÔM SÁT THÂN RỒNG (BLEND_ALPHA)
  // =======================================================
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    int bodySegments = emitters[e].sampledCount;
    if (bodySegments == 0) continue;

    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < bodySegments; i += 2) {
      Vector3 pos3D = emitters[e].sampledPath[i];
      Vector2 drawPos = GetWorldToScreen(pos3D, camera);

      float dist = Vector3Distance(camera.position, pos3D);
      float depthFactor = 800.0f / (dist + 0.1f);
      if (depthFactor < 0.2f) depthFactor = 0.2f;
      if (depthFactor > 3.0f) depthFactor = 3.0f;

      float normDist = (bodySegments > 1) ? ((float)i / (float)(bodySegments - 1)) : 0.0f;
      float taper = powf(1.0f - normDist, 0.5f);

      float smokeRad = Math_Mix(8.0f, 20.0f, taper);
      unsigned char smokeAlpha = (unsigned char)(255.0f * taper * 0.18f);
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, smokeRad * depthFactor,
                         (Color){70, 30, 30, smokeAlpha}, BLANK);
    }
    EndBlendMode();
  }

  // =======================================================
  // 2. VẼ VỆT SỢI XÉ GIÓ VÀ CÁC HẠT (BLEND_ADDITIVE)
  // =======================================================
  BeginBlendMode(BLEND_ADDITIVE);
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!firePool[i].active)
      continue;

    float lifeRatio = firePool[i].lifetime / firePool[i].maxLifetime;
    Color edgeCol = {0, 0, 0, 0};

    Vector2 screenPos = GetWorldToScreen(firePool[i].position, camera);
    float dist = Vector3Distance(camera.position, firePool[i].position);
    float depthFactor = 800.0f / (dist + 0.1f);
    if (depthFactor < 0.2f) depthFactor = 0.2f;
    if (depthFactor > 3.0f) depthFactor = 3.0f;

    if (firePool[i].type == 1) {
      float rad = firePool[i].radius * (0.5f + 0.5f * lifeRatio);
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.45f);
      Color wispCol = {intensity, (unsigned char)(intensity * 0.5f), (unsigned char)(intensity * 0.15f), 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.5f * depthFactor, wispCol, edgeCol);
    } else if (firePool[i].type == 2) {
      float rad = firePool[i].radius * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.9f);
      Color sparkColor = {intensity, intensity, intensity, 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.0f * depthFactor, sparkColor, edgeCol);
    } else if (firePool[i].type == 3) {
      float rad = firePool[i].radius * (0.4f + 0.6f * lifeRatio);
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.8f);
      Color emberCol = {intensity, (unsigned char)(intensity * 0.9f), (unsigned char)(intensity * 0.3f), 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.5f * depthFactor, emberCol, edgeCol);
      DrawCircle((int)screenPos.x, (int)screenPos.y, rad * 0.5f * depthFactor, (Color){intensity, intensity, intensity, 255});
    } else if (firePool[i].type == 4) {
      float rad = firePool[i].radius * (1.1f - 0.3f * lifeRatio) * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.85f);
      Color vortexCol = {intensity, (unsigned char)(intensity * 0.4f), (unsigned char)(intensity * 0.1f), 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.8f * depthFactor, vortexCol, edgeCol);
      DrawCircle((int)screenPos.x, (int)screenPos.y, rad * 0.6f * depthFactor, (Color){intensity, (unsigned char)(intensity * 0.8f), (unsigned char)(intensity * 0.5f), 255});
    } else if (firePool[i].type == 5) {
      float rad = firePool[i].radius * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.95f);
      Color breathCol = {intensity, (unsigned char)(intensity * 0.6f), (unsigned char)(intensity * 0.2f), 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad * 2.2f * depthFactor, breathCol, edgeCol);
    } else if (firePool[i].type == 6) {
      float progress = 1.0f - lifeRatio;
      float outerRad = firePool[i].radius * progress * depthFactor;
      float innerRad = outerRad * 0.70f;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.85f);
      Color ringCol = {intensity, intensity, intensity, 255};
      DrawRing(screenPos, innerRad, outerRad, 0.0f, 360.0f, 32, ringCol);
    } else if (firePool[i].type == 7) {
      float progress = 1.0f - lifeRatio;
      float rad = firePool[i].radius * (0.6f + 0.4f * progress) * depthFactor;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.45f);
      Color flashCol = {intensity, intensity, intensity, 255};
      DrawCircleGradient((int)screenPos.x, (int)screenPos.y, rad, flashCol, edgeCol);
    }
  }
  EndBlendMode();

  // =======================================================
  // 3. Vẽ một ribbon xương sống, đầu rồng và mắt (BLEND_ADDITIVE)
  // =======================================================
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    int bodySegments = emitters[e].sampledCount;
    if (bodySegments == 0) continue;

    BeginBlendMode(BLEND_ADDITIVE);
    Vector2 prevDrawPos = {0};
    Vector2 headDrawPos = {0};
    Vector2 headPureTangent = {1, 0};
    float headDepthFactor = 1.0f;

    for (int i = 0; i < bodySegments; i++) {
      Vector3 pos3D = emitters[e].sampledPath[i];
      Vector2 drawPos = GetWorldToScreen(pos3D, camera);

      float dist = Vector3Distance(camera.position, pos3D);
      float depthFactor = 800.0f / (dist + 0.1f);
      if (depthFactor < 0.2f) depthFactor = 0.2f;
      if (depthFactor > 3.0f) depthFactor = 3.0f;

      if (i == 0) {
        headDrawPos = drawPos;
        headDepthFactor = depthFactor;
        
        Vector2 nextDrawPos = drawPos;
        if (bodySegments > 1) {
          nextDrawPos = GetWorldToScreen(emitters[e].sampledPath[1], camera);
        }
        headPureTangent = Vector2Normalize(Vector2Subtract(drawPos, nextDrawPos));
        if (headPureTangent.x == 0 && headPureTangent.y == 0) {
          headPureTangent = (Vector2){1.0f, 0.0f};
        }
      }

      float normDist = (bodySegments > 1) ? ((float)i / (float)(bodySegments - 1)) : 0.0f;
      float taper = powf(1.0f - normDist, 0.5f);
      float headFade = Math_Mix(0.82f, 1.0f, SmoothStep01(Clamp((float)i / 8.0f, 0.0f, 1.0f)));

      float ribbonRad = Math_Mix(1.5f, 5.0f, taper) * emitters[e].sizeScale;
      float auraRad = Math_Mix(4.0f, 10.5f, taper) * emitters[e].sizeScale;
      float bodyPulse = 0.94f + 0.06f * sinf(time * 9.0f - ((float)i * 8.0f) * 0.045f);
      float brightness = taper * headFade * bodyPulse;

      Color outerColor = FireDensityColor(0.12f * brightness, 1.0f);
      Color ribbonColor = FireDensityColor(0.42f * brightness, 1.0f);
      Color hotColor = FireDensityColor(0.88f * brightness, 1.0f);

      if (i > 0) {
        DrawLineEx(prevDrawPos, drawPos, auraRad * 1.7f * depthFactor, outerColor);
        DrawLineEx(prevDrawPos, drawPos, ribbonRad * 1.9f * depthFactor, ribbonColor);
        DrawLineEx(prevDrawPos, drawPos, ribbonRad * 0.55f * depthFactor, hotColor);
      }
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, auraRad * depthFactor, outerColor, BLANK);
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, ribbonRad * 0.85f * depthFactor, hotColor, BLANK);

      // Vẽ gai nhọn hai bên hông
      if (i % 8 == 0 && i > 4 && i < bodySegments - 5) {
        Vector2 nextDrawPos = drawPos;
        if (i < bodySegments - 1) {
          nextDrawPos = GetWorldToScreen(emitters[e].sampledPath[i + 1], camera);
        }
        Vector2 pureTangent = Vector2Normalize(Vector2Subtract(drawPos, nextDrawPos));
        if (pureTangent.x == 0 && pureTangent.y == 0) pureTangent = (Vector2){1.0f, 0.0f};

        Vector2 purePerp = {-pureTangent.y, pureTangent.x};
        float sideSign = (i % 14 == 0) ? 1.0f : -1.0f;
        float scaleLen = ribbonRad * Math_Mix(1.9f, 3.1f, taper) * depthFactor;
        float baseWidth = ribbonRad * Math_Mix(1.7f, 2.5f, taper) * depthFactor;
        Vector2 baseCenter = Vector2Add(drawPos, Vector2Scale(purePerp, sideSign * ribbonRad * 0.55f * depthFactor));
        Vector2 tip = {
            baseCenter.x + purePerp.x * sideSign * scaleLen - pureTangent.x * ribbonRad * 0.65f * depthFactor,
            baseCenter.y + purePerp.y * sideSign * scaleLen - pureTangent.y * ribbonRad * 0.65f * depthFactor
        };
        Vector2 baseA = {
            baseCenter.x - pureTangent.x * baseWidth,
            baseCenter.y - pureTangent.y * baseWidth
        };
        Vector2 baseB = {
            baseCenter.x + pureTangent.x * baseWidth,
            baseCenter.y + pureTangent.y * baseWidth
        };
        Vector2 root = Vector2Add(drawPos, Vector2Scale(purePerp, -sideSign * ribbonRad * 0.12f * depthFactor));
        DrawTriangle(baseA, baseB, tip, FireDensityColor(0.40f * brightness, 1.0f));
        DrawTriangle(baseA, tip, root, FireDensityColor(0.58f * brightness, 1.0f));
        DrawCircleGradient((int)tip.x, (int)tip.y, ribbonRad * 0.55f * depthFactor,
                           FireDensityColor(0.42f * brightness, 1.0f), BLANK);
      }
      prevDrawPos = drawPos;
    }

    // Đuôi rồng
    if (bodySegments > 5) {
      Vector2 tailPos = prevDrawPos;
      Vector2 tailTangent;
      if (bodySegments > 1) {
        tailTangent = Vector2Normalize(Vector2Subtract(GetWorldToScreen(emitters[e].sampledPath[bodySegments - 2], camera), GetWorldToScreen(emitters[e].sampledPath[bodySegments - 1], camera)));
      } else {
        tailTangent = headPureTangent;
      }
      Vector2 tailTip = {tailPos.x - tailTangent.x * 20.0f * headDepthFactor,
                         tailPos.y - tailTangent.y * 20.0f * headDepthFactor};
      DrawLineEx(tailPos, tailTip, 2.5f * headDepthFactor, FireDensityColor(0.55f, 1.0f));
      DrawCircleGradient((int)tailPos.x, (int)tailPos.y, 6.0f * headDepthFactor,
                         FireDensityColor(0.18f, 1.0f), BLANK);
    }

    if (emitters[e].headProgress < FIRE_PROGRESS_MAX && bodySegments > 1) {
      Vector2 headPerp = {-headPureTangent.y, headPureTangent.x};
      float rotation = atan2f(headPureTangent.y, headPureTangent.x) * RAD2DEG;
      float flipY = (headPureTangent.x < 0.0f) ? -1.0f : 1.0f;
      Rectangle sourceRec = {0.0f, 0.0f, (float)dragonHeadTex.width,
                             (float)dragonHeadTex.height * flipY};

      float jawOscillation = sinf(time * DRAGON_JAW_OSCILLATION_SPEED) *
                             DRAGON_JAW_OSCILLATION_AMP;
      float scaleX = DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale * headDepthFactor;
      float scaleY = DRAGON_HEAD_BASE_SCALE * (1.0f + jawOscillation) * emitters[e].sizeScale * headDepthFactor;
      Rectangle destRec = {headDrawPos.x, headDrawPos.y,
                           dragonHeadTex.width * scaleX,
                           dragonHeadTex.height * scaleY};
      Vector2 origin = {destRec.width * DRAGON_HEAD_ORIGIN_X_FACTOR,
                        destRec.height * 0.5f};

      float headAlpha = (emitters[e].headProgress > FIRE_PROGRESS_MAX - 0.4f)
                            ? Clamp(1.0f - (emitters[e].headProgress -
                                            (FIRE_PROGRESS_MAX - 0.4f)) /
                                               0.4f,
                                    0.0f, 1.0f)
                            : 1.0f;

      unsigned char jointV = (unsigned char)(255.0f * headAlpha * 0.35f);
      DrawCircleGradient((int)headDrawPos.x, (int)headDrawPos.y, 18.0f * scaleY,
                         (Color){jointV, jointV, jointV, 255}, BLANK);

      DrawTexturePro(dragonHeadTex, sourceRec, destRec, origin, rotation,
                     ColorAlpha(WHITE, headAlpha));

      Vector2 whiskerBaseTop = GetTexturePointInWorld(
          headDrawPos, headPureTangent, scaleX, scaleY, origin,
          (Vector2){320.0f, 180.0f}, flipY);
      Vector2 whiskerBaseBottom = GetTexturePointInWorld(
          headDrawPos, headPureTangent, scaleX, scaleY, origin,
          (Vector2){320.0f, 260.0f}, flipY);

      Vector2 prevPtTop = whiskerBaseTop, prevPtBottom = whiskerBaseBottom;
      for (int s = 1; s <= 12; s++) {
        float tFactor = (float)s / 12.0f;
        float segmentDist = (float)s * 5.0f * headDepthFactor;

        float waveTop = sinf(time * 16.0f - (float)s * 0.7f) * 6.5f * tFactor * headDepthFactor;
        float waveBottom =
            cosf(time * 16.0f - (float)s * 0.7f) * 6.5f * tFactor * headDepthFactor;

        Vector2 ptTop = {whiskerBaseTop.x - headPureTangent.x * segmentDist +
                             headPerp.x * waveTop,
                         whiskerBaseTop.y - headPureTangent.y * segmentDist +
                             headPerp.y * waveTop};
        Vector2 ptBottom = {
            whiskerBaseBottom.x - headPureTangent.x * segmentDist +
                headPerp.x * waveBottom,
            whiskerBaseBottom.y - headPureTangent.y * segmentDist +
                headPerp.y * waveBottom};

        float thickness = Math_Mix(2.5f, 0.7f, tFactor) * scaleY;
        unsigned char whiskerV =
            (unsigned char)(255.0f * headAlpha * (1.0f - tFactor * 0.4f) *
                            0.75f);
        Color whiskerColor = {whiskerV, whiskerV, whiskerV, 255};

        DrawLineEx(prevPtTop, ptTop, thickness, whiskerColor);
        DrawLineEx(prevPtBottom, ptBottom, thickness, whiskerColor);
        prevPtTop = ptTop;
        prevPtBottom = ptBottom;
      }

      Vector2 eyePos = GetTexturePointInWorld(
          headDrawPos, headPureTangent, scaleX, scaleY, origin,
          (Vector2){320.0f, 180.0f}, flipY);
      float eyeRad = 6.0f * scaleY;
      unsigned char eyeV = (unsigned char)(255.0f * headAlpha);
      DrawCircleGradient((int)eyePos.x, (int)eyePos.y, eyeRad * 2.2f,
                         (Color){eyeV, (unsigned char)(eyeV * 0.65f),
                                 (unsigned char)(eyeV * 0.3f), 255},
                         BLANK);
      DrawCircle((int)eyePos.x, (int)eyePos.y, eyeRad * 0.4f,
                 (Color){255, 255, 255, 255});
    }
    EndBlendMode();
  }

  EndTextureMode();

  // Áp dụng shader và vẽ texture lên màn hình
  SetShaderValue(fireShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(fireShader);
  DrawTextureRec(canvasTexture.texture,
                 (Rectangle){0, 0, (float)canvasTexture.texture.width,
                             (float)-canvasTexture.texture.height},
                 (Vector2){0, 0}, WHITE);
  EndShaderMode();
}

void UnloadFireSkill(void) {
  UnloadShader(fireShader);
  UnloadTexture(dragonHeadTex);
  UnloadRenderTexture(canvasTexture);
}

// Lấy va chạm của chiêu thức
int GetFireSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      Vector3 headPos = (emitters[i].pathCount > 0) ? emitters[i].path[0] : emitters[i].startPos;
      outProjectiles[count].position = headPos;
      outProjectiles[count].radius = 15.0f * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateFireProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        Vector3 headPos = (emitters[i].pathCount > 0) ? emitters[i].path[0] : emitters[i].startPos;
        TriggerFireImpact(headPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}

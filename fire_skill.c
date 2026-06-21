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

// Lấy các điểm cách đều nhau một khoảng cố định dọc theo đường đi (path)
static int SamplePath(const Vector2* path, int pathCount, float spacing, Vector2* outSegments, int maxSegments) {
  if (pathCount == 0 || maxSegments <= 0) return 0;

  outSegments[0] = path[0];
  int segmentIndex = 1;
  float targetDist = spacing;
  float accumulatedDist = 0.0f;

  for (int i = 0; i < pathCount - 1; i++) {
    Vector2 p1 = path[i];
    Vector2 p2 = path[i + 1];
    float d = Vector2Distance(p1, p2);

    while (accumulatedDist + d >= targetDist) {
      float needed = targetDist - accumulatedDist;
      float t = (d > 0.0f) ? (needed / d) : 0.0f;
      Vector2 interpPos = Vector2Lerp(p1, p2, t);

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
  Vector2 startPos;
  Vector2 targetPos;
  Vector2 p1, p2;
  float headProgress;
  float twistPhase;
  float sizeScale; // Hệ số thu phóng rồng lửa
  Vector2 path[EMITTER_PATH_MAX];
  int pathCount;
  Vector2 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
} FireEmitter;

#define PARTICLE_HISTORY_COUNT 6

typedef struct {
  bool active;
  Vector2 position;
  Vector2 velocity;
  float radius;
  float lifetime;
  float maxLifetime;
  int type; // 1: Wind Trails, 2: Spark Bursts, 3: Floating Embers, 4: Fire Vortex, 5: Fire Breath, 6: Shockwave Ring, 7: Ambient Light Flash
  Vector2 history[PARTICLE_HISTORY_COUNT];
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
// Toán học: Đường bay Thăng Long
// =============================================================================

static Vector2 GetBezierPoint(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3,
                              float t) {
  float u = 1.0f - t;
  float tt = t * t;
  float uu = u * u;
  float uuu = uu * u;
  float ttt = tt * t;
  return Vector2Add(Vector2Add(Vector2Add(Vector2Scale(p0, uuu),
                                          Vector2Scale(p1, 3.0f * uu * t)),
                               Vector2Scale(p2, 3.0f * u * tt)),
                    Vector2Scale(p3, ttt));
}

static Vector2 GetBezierTangent(Vector2 p0, Vector2 p1, Vector2 p2,
                                Vector2 target, float t) {
  float u = 1.0f - t;
  Vector2 tangent = Vector2Add(
      Vector2Add(Vector2Scale(Vector2Subtract(p1, p0), 3.0f * u * u),
                 Vector2Scale(Vector2Subtract(p2, p1), 6.0f * u * t)),
      Vector2Scale(Vector2Subtract(target, p2), 3.0f * t * t));
  if (tangent.x == 0 && tangent.y == 0)
    return (Vector2){1.0f, 0.0f};
  return Vector2Normalize(tangent);
}

static Vector2 GetDragonPathPos(Vector2 p0, Vector2 p1, Vector2 p2,
                                Vector2 target, float t) {
  if (t <= 1.0f)
    return GetBezierPoint(p0, p1, p2, target, t);

  float over = t - 1.0f;
  Vector2 vIn = Vector2Scale(Vector2Subtract(target, p2), 3.0f);
  if (Vector2Length(vIn) > 800.0f)
    vIn = Vector2Scale(Vector2Normalize(vIn), 800.0f);

  float upwardSpeed = 1600.0f;
  float waveFreq = 12.0f;
  float waveAmp = 200.0f;

  Vector2 idealUpPos = {target.x + sinf(over * waveFreq) * waveAmp,
                        target.y - upwardSpeed * over};
  Vector2 inertiaPos = {target.x + vIn.x * over, target.y + vIn.y * over};

  float blend = fminf(over * 3.5f, 1.0f);
  blend = blend * blend * (3.0f - 2.0f * blend);

  return (Vector2){Math_Mix(inertiaPos.x, idealUpPos.x, blend),
                   Math_Mix(inertiaPos.y, idealUpPos.y, blend)};
}

static Vector2 GetDragonPathTangent(Vector2 p0, Vector2 p1, Vector2 p2,
                                    Vector2 target, float t) {
  if (t <= 1.0f)
    return GetBezierTangent(p0, p1, p2, target, t);
  float delta = 0.01f;
  Vector2 tangent =
      Vector2Subtract(GetDragonPathPos(p0, p1, p2, target, t + delta),
                      GetDragonPathPos(p0, p1, p2, target, t));
  if (tangent.x == 0 && tangent.y == 0)
    return (Vector2){0.0f, -1.0f};
  return Vector2Normalize(tangent);
}

static Vector2 GetTexturePointInWorld(Vector2 headPos, Vector2 tangent, float scaleX, float scaleY, Vector2 origin, Vector2 texPt, float flipY) {
  float lx = texPt.x * DRAGON_HEAD_BASE_SCALE - origin.x;
  float ly = (texPt.y * scaleY - origin.y) * flipY;
  Vector2 perp = {-tangent.y, tangent.x};
  Vector2 worldPos = {
    headPos.x + tangent.x * lx + perp.x * ly,
    headPos.y + tangent.y * lx + perp.y * ly
  };
  return worldPos;
}

static void SpawnParticle(int type, Vector2 pos, Vector2 vel, float baseRadius,
                          float maxLife) {
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

static void TriggerFireImpact(Vector2 pos, float sizeScale) {
  // 1. Tia lửa phát tán nhanh (Radial Spark Burst)
  int sparkCount = GetRandomValue(12, 18) * sizeScale;
  for (int s = 0; s < sparkCount; s++) {
    float angle = Random01() * 3.14159265f * 2.0f;
    float speed = Math_Mix(160.0f, 420.0f, Random01()) * sizeScale;
    Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
    float rad = Math_Mix(0.8f, 2.2f, Random01()) * sizeScale;
    float life = Math_Mix(0.3f, 0.7f, Random01());
    SpawnParticle(2, pos, vel, rad, life);
  }

  // 2. Hạt bụi lửa tán ra rồi bốc lên theo gió (Dispersing Noise Particles)
  int disperseCount = GetRandomValue(14, 22) * sizeScale;
  for (int v = 0; v < disperseCount; v++) {
    float angle = Random01() * 3.14159265f * 2.0f;
    float speed = Math_Mix(80.0f, 260.0f, Random01()) * sizeScale;
    Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed - 80.0f * sizeScale }; // Tán rộng ra và hơi bay lên
    float rad = Math_Mix(2.5f, 6.5f, Random01()) * sizeScale;
    float life = Math_Mix(0.6f, 1.3f, Random01());
    SpawnParticle(4, pos, vel, rad, life);
  }

  // 3. Vòng sóng kích nổ (Fiery Shockwave Rings - Type 6)
  SpawnParticle(6, pos, (Vector2){0, 0}, 60.0f * sizeScale, 0.40f); // Vòng ngoài
  SpawnParticle(6, pos, (Vector2){0, 0}, 33.0f * sizeScale, 0.25f);  // Vòng trong

  // 4. Ánh chớp phát sáng môi trường (Ambient Light Flash - Type 7)
  SpawnParticle(7, pos, (Vector2){0, 0}, 110.0f * sizeScale, 0.25f);

  // 5. Lưỡi lửa bật ngược lại sau cú cắn để impact có cảm giác nổ tung.
  int flameTongues = GetRandomValue(4, 7) * sizeScale;
  for (int f = 0; f < flameTongues; f++) {
    float angle = Math_Mix(-PI * 0.92f, -PI * 0.08f, Random01());
    float speed = Math_Mix(140.0f, 340.0f, Random01()) * sizeScale;
    Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
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

void CastFireSkill(Vector2 startPos, Vector2 target, float twistPhase, float sizeScale) {
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

      float dist = Vector2Distance(startPos, target);
      Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
      Vector2 perp = (Vector2){ -dir.y, dir.x };

      // twistPhase represents a spread angle. Symmetrical fanning around the straight line.
      float spreadScale = sinf(twistPhase);

      emitters[i].p1 = Vector2Add(Vector2Add(startPos, Vector2Scale(dir, dist * 0.45f)), Vector2Scale(perp, spreadScale * dist * 0.35f));
      emitters[i].p2 = Vector2Add(target, Vector2Scale(perp, -spreadScale * dist * 0.15f));
      break;
    }
  }

  // Casting heat flash - scale radius by sizeScale
  SpawnParticle(7, startPos, (Vector2){0, 0}, 100.0f * sizeScale, 0.30f);

  int burstCount = GetRandomValue(8, 14) * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    Vector2 vel = {(float)GetRandomValue((int)CAST_BURST_VEL_X_MIN,
                                         (int)CAST_BURST_VEL_X_MAX) * sizeScale,
                   (float)GetRandomValue((int)CAST_BURST_VEL_Y_MIN,
                                         (int)CAST_BURST_VEL_Y_MAX) * sizeScale};
    float rad =
        Math_Mix(CAST_BURST_RADIUS_MIN, CAST_BURST_RADIUS_MAX, Random01()) * sizeScale;
    float life =
        Math_Mix(CAST_BURST_LIFETIME_MIN, CAST_BURST_LIFETIME_MAX, Random01());
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

    // Sub-stepping to record a smooth straight path
    float step = 0.008f;
    float currentProgress = prevProgress;
    while (currentProgress < targetProgress) {
      currentProgress += step;
      if (currentProgress > targetProgress) currentProgress = targetProgress;

      Vector2 pos = GetDragonPathPos(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                                     emitters[e].targetPos, currentProgress);

      float dist = 0.0f;
      if (emitters[e].pathCount > 0) {
        dist = Vector2Distance(pos, emitters[e].path[0]);
      } else {
        dist = Vector2Distance(pos, emitters[e].startPos);
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

    // If progress exceeded max, deactivate
    if (emitters[e].headProgress >= FIRE_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    // Sample path at wider spacing to keep one fluid ribbon and reduce draw cost.
    emitters[e].sampledCount = SamplePath(emitters[e].path, emitters[e].pathCount, 8.0f, emitters[e].sampledPath, MAX_SAMPLED_SEGMENTS);
    if (prevProgress <= 1.0f && emitters[e].headProgress > 1.0f) {
      TriggerFireImpact(emitters[e].targetPos, emitters[e].sizeScale);
    }

    // Determine current head properties for visuals
    Vector2 headPos = (emitters[e].pathCount > 0) ? emitters[e].path[0] : emitters[e].startPos;
    Vector2 tangent = GetDragonPathTangent(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                                           emitters[e].targetPos, emitters[e].headProgress);
    Vector2 visualHeadPos = headPos;

    if (emitters[e].headProgress <= 1.0f) {
      float jawOsc = sinf(time * DRAGON_JAW_OSCILLATION_SPEED) * DRAGON_JAW_OSCILLATION_AMP;
      float scaleY = DRAGON_HEAD_BASE_SCALE * (1.0f + jawOsc) * emitters[e].sizeScale;
      float flipY = (tangent.x < 0.0f) ? -1.0f : 1.0f;
      Vector2 origin = { dragonHeadTex.width * DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale * DRAGON_HEAD_ORIGIN_X_FACTOR,
                         dragonHeadTex.height * scaleY * 0.5f };
      Vector2 mouthTexPt = { 460.0f, 260.0f };
      Vector2 mouthWorldPos = GetTexturePointInWorld(visualHeadPos, tangent, DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale, scaleY, origin, mouthTexPt, flipY);

      int breathCount = GetRandomValue(2, 4) * emitters[e].sizeScale;
      for (int b = 0; b < breathCount; b++) {
        float angleOffset = (float)GetRandomValue(-18, 18) * DEG2RAD;
        Vector2 dir = {
            tangent.x * cosf(angleOffset) - tangent.y * sinf(angleOffset),
            tangent.x * sinf(angleOffset) + tangent.y * cosf(angleOffset)
        };
        float speed = (float)GetRandomValue(420, 760);
        Vector2 vel = Vector2Scale(dir, speed);
        float rad = Math_Mix(1.3f, 3.2f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.16f, 0.36f, Random01());
        SpawnParticle(5, mouthWorldPos, vel, rad, life);
      }
    }

    // Spawn wind trails and embers along the sampled path
    if (emitters[e].sampledCount > 1) {
      // Wisps (Type 1) - bám theo ribbon, vừa trôi dọc thân vừa cuộn nhiễu quanh nó.
      int wispsToSpawn = GetRandomValue(11, 18) * emitters[e].sizeScale;
      for (int k = 0; k < wispsToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector2 purePos = emitters[e].sampledPath[idx];

        Vector2 pureTangent;
        if (idx < emitters[e].sampledCount - 1)
          pureTangent = Vector2Normalize(Vector2Subtract(purePos, emitters[e].sampledPath[idx + 1]));
        else
          pureTangent = Vector2Normalize(Vector2Subtract(emitters[e].sampledPath[idx - 1], purePos));

        Vector2 purePerp = {-pureTangent.y, pureTangent.x};
        float distFromHead = idx * 8.0f;

        float normDist = (emitters[e].sampledCount > 1) ? ((float)idx / (float)(emitters[e].sampledCount - 1)) : 0.0f;
        float taper = powf(1.0f - normDist, 0.5f);
        float coreRad = Math_Mix(2.2f, 8.0f, taper) * emitters[e].sizeScale;

        float spiralPhase = time * 11.0f - distFromHead * 0.075f +
                            emitters[e].twistPhase + Random01() * PI * 2.0f;
        float coilRad = coreRad * Math_Mix(0.75f, 1.65f, Random01());

        Vector2 spawnPos = {
          purePos.x + purePerp.x * cosf(spiralPhase) * coilRad,
          purePos.y + purePerp.y * cosf(spiralPhase) * coilRad
        };

        spawnPos.x += Math_Mix(-2.0f, 2.0f, Random01()) * emitters[e].sizeScale;
        spawnPos.y += Math_Mix(-2.0f, 2.0f, Random01()) * emitters[e].sizeScale;

        float cosVal = cosf(spiralPhase);
        float sinVal = sinf(spiralPhase);
        Vector2 radDir = { purePerp.x * cosVal, purePerp.y * cosVal };
        Vector2 rotDir = { -purePerp.x * sinVal, -purePerp.y * sinVal };

        float ribbonSpeed = Math_Mix(105.0f, 220.0f, Random01());
        float orbitSpeed = Math_Mix(120.0f, 220.0f, Random01());
        float expandSpeed = Math_Mix(-8.0f, 12.0f, Random01());

        Vector2 windVel = {
          -pureTangent.x * ribbonSpeed + rotDir.x * orbitSpeed + radDir.x * expandSpeed,
          -pureTangent.y * ribbonSpeed + rotDir.y * orbitSpeed + radDir.y * expandSpeed
        };

        float rad = Math_Mix(2.0f, 4.8f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.32f, 0.62f, Random01());
        SpawnParticle(1, spawnPos, windVel, rad, life);
      }

      // Embers (Type 3) - ít hơn, rộng hơn, dùng để tạo lửa bốc quanh ribbon.
      int embersToSpawn = GetRandomValue(3, 5) * emitters[e].sizeScale;
      for (int k = 0; k < embersToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector2 purePos = emitters[e].sampledPath[idx];

        Vector2 pureTangent;
        if (idx < emitters[e].sampledCount - 1)
          pureTangent = Vector2Normalize(Vector2Subtract(purePos, emitters[e].sampledPath[idx + 1]));
        else
          pureTangent = Vector2Normalize(Vector2Subtract(emitters[e].sampledPath[idx - 1], purePos));

        Vector2 purePerp = {-pureTangent.y, pureTangent.x};
        float distFromHead = idx * 8.0f;
        float normDist = (emitters[e].sampledCount > 1) ? ((float)idx / (float)(emitters[e].sampledCount - 1)) : 0.0f;
        float taper = powf(1.0f - normDist, 0.5f);
        float coreRad = Math_Mix(2.2f, 8.0f, taper) * emitters[e].sizeScale;

        float spiralPhase = time * 8.0f - distFromHead * 0.06f +
                            emitters[e].twistPhase + Random01() * PI * 2.0f;
        float coilRad = coreRad * Math_Mix(1.1f, 2.25f, Random01());

        Vector2 spawnPos = {
          purePos.x + purePerp.x * cosf(spiralPhase) * coilRad,
          purePos.y + purePerp.y * cosf(spiralPhase) * coilRad
        };

        spawnPos.x += Math_Mix(-2.5f, 2.5f, Random01()) * emitters[e].sizeScale;
        spawnPos.y += Math_Mix(-2.5f, 2.5f, Random01()) * emitters[e].sizeScale;

        float cosVal = cosf(spiralPhase);
        float sinVal = sinf(spiralPhase);
        Vector2 radDir = { purePerp.x * cosVal, purePerp.y * cosVal };
        Vector2 rotDir = { -purePerp.x * sinVal, -purePerp.y * sinVal };

        float forwardSpeed = Math_Mix(70.0f, 150.0f, Random01());
        float orbitSpeed = Math_Mix(95.0f, 170.0f, Random01());
        float expandSpeed = Math_Mix(-4.0f, 18.0f, Random01());

        Vector2 vel = {
          -pureTangent.x * forwardSpeed + rotDir.x * orbitSpeed + radDir.x * expandSpeed,
          -pureTangent.y * forwardSpeed + rotDir.y * orbitSpeed + radDir.y * expandSpeed
        };
        float rad = Math_Mix(2.8f, 6.2f, Random01()) * emitters[e].sizeScale;
        float life = Math_Mix(0.62f, 1.15f, Random01());
        SpawnParticle(3, spawnPos, vel, rad, life);
      }
    }
  }

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

    if (firePool[i].type == 1) {
      firePool[i].velocity.y -= 45.0f * dt;
    } else if (firePool[i].type == 3) {
      firePool[i].velocity.y -= 75.0f * dt;
    } else {
      firePool[i].velocity.y -= 180.0f * dt;
    }

    if (firePool[i].type == 1) {
      float pX = firePool[i].position.x * 0.015f;
      float pY = firePool[i].position.y * 0.015f;
      firePool[i].velocity.x += sinf(pY + time * 8.0f) * 85.0f * dt;
      firePool[i].velocity.y += cosf(pX + time * 8.0f) * 85.0f * dt;
    }
    else if (firePool[i].type == 3) {
      firePool[i].angle += dt * 6.0f;
      firePool[i].velocity.x += sinf(firePool[i].angle) * 70.0f * dt;
      firePool[i].velocity.y -= 35.0f * dt;
    }
    else if (firePool[i].type == 4) {
      firePool[i].velocity.x *= (1.0f - 3.5f * dt);
      firePool[i].velocity.y *= (1.0f - 2.0f * dt);
      firePool[i].velocity.y -= 260.0f * dt;
      float pX = firePool[i].position.x * 0.02f;
      float pY = firePool[i].position.y * 0.02f;
      firePool[i].velocity.x += sinf(pY + time * 10.0f) * 180.0f * dt;
      firePool[i].velocity.y += cosf(pX + time * 10.0f) * 180.0f * dt;
    }
    else if (firePool[i].type == 5) {
      firePool[i].velocity.x *= (1.0f - 4.5f * dt);
      firePool[i].velocity.y *= (1.0f - 4.5f * dt);
      firePool[i].velocity.y -= 90.0f * dt;
    }
    else if (firePool[i].type == 7) {
      firePool[i].velocity = (Vector2){0, 0};
    }

    firePool[i].position.x += firePool[i].velocity.x * dt;
    firePool[i].position.y += firePool[i].velocity.y * dt;
  }
}

void DrawFireSkill(void) {
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
      Vector2 purePos_i = emitters[e].sampledPath[i];
      Vector2 drawPos = purePos_i;

      float normDist = (bodySegments > 1) ? ((float)i / (float)(bodySegments - 1)) : 0.0f;
      float taper = powf(1.0f - normDist, 0.5f);

      float smokeRad = Math_Mix(8.0f, 20.0f, taper);
      unsigned char smokeAlpha = (unsigned char)(255.0f * taper * 0.18f);
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, smokeRad,
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

    if (firePool[i].type == 1) {
      float rad = firePool[i].radius * (0.5f + 0.5f * lifeRatio);
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.45f);
      Color wispCol = {intensity, (unsigned char)(intensity * 0.5f), (unsigned char)(intensity * 0.15f), 255};
      DrawCircleGradient((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 2.5f, wispCol, edgeCol);
    } else if (firePool[i].type == 2) {
      float rad = firePool[i].radius * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.9f);
      Color sparkColor = {intensity, intensity, intensity, 255};
      DrawCircleGradient((int)firePool[i].position.x,
                         (int)firePool[i].position.y, rad * 2.0f, sparkColor, edgeCol);
    } else if (firePool[i].type == 3) {
      float rad = firePool[i].radius * (0.4f + 0.6f * lifeRatio);
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.8f);
      Color emberCol = {intensity, (unsigned char)(intensity * 0.9f), (unsigned char)(intensity * 0.3f), 255};
      DrawCircleGradient((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 2.5f, emberCol, edgeCol);
      DrawCircle((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 0.5f, (Color){intensity, intensity, intensity, 255});
    } else if (firePool[i].type == 4) {
      float rad = firePool[i].radius * (1.1f - 0.3f * lifeRatio) * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.85f);
      Color vortexCol = {intensity, (unsigned char)(intensity * 0.4f), (unsigned char)(intensity * 0.1f), 255};
      DrawCircleGradient((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 2.8f, vortexCol, edgeCol);
      DrawCircle((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 0.6f, (Color){intensity, (unsigned char)(intensity * 0.8f), (unsigned char)(intensity * 0.5f), 255});
    } else if (firePool[i].type == 5) {
      float rad = firePool[i].radius * lifeRatio;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.95f);
      Color breathCol = {intensity, (unsigned char)(intensity * 0.6f), (unsigned char)(intensity * 0.2f), 255};
      DrawCircleGradient((int)firePool[i].position.x, (int)firePool[i].position.y, rad * 2.2f, breathCol, edgeCol);
    } else if (firePool[i].type == 6) {
      float progress = 1.0f - lifeRatio;
      float outerRad = firePool[i].radius * progress;
      float innerRad = outerRad * 0.70f;
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.85f);
      Color ringCol = {intensity, intensity, intensity, 255};
      DrawRing(firePool[i].position, innerRad, outerRad, 0.0f, 360.0f, 32, ringCol);
    } else if (firePool[i].type == 7) {
      float progress = 1.0f - lifeRatio;
      float rad = firePool[i].radius * (0.6f + 0.4f * progress);
      unsigned char intensity = (unsigned char)(255.0f * lifeRatio * 0.45f);
      Color flashCol = {intensity, intensity, intensity, 255};
      DrawCircleGradient((int)firePool[i].position.x, (int)firePool[i].position.y, rad, flashCol, edgeCol);
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

    for (int i = 0; i < bodySegments; i++) {
      Vector2 purePos_i = emitters[e].sampledPath[i];
      Vector2 pureTangent;
      if (i < bodySegments - 1)
        pureTangent = Vector2Normalize(Vector2Subtract(purePos_i, emitters[e].sampledPath[i + 1]));
      else if (i > 0)
        pureTangent = Vector2Normalize(Vector2Subtract(emitters[e].sampledPath[i - 1], purePos_i));
      else
        pureTangent = (Vector2){1, 0};

      float distFromHead = (float)i * 8.0f;
      Vector2 drawPos = purePos_i;

      if (i == 0) {
        headDrawPos = drawPos;
        headPureTangent = pureTangent;
      }

      float normDist = (bodySegments > 1) ? ((float)i / (float)(bodySegments - 1)) : 0.0f;
      float taper = powf(1.0f - normDist, 0.5f);
      float headFade = Math_Mix(0.82f, 1.0f, SmoothStep01(Clamp((float)i / 8.0f, 0.0f, 1.0f)));

      float ribbonRad = Math_Mix(1.5f, 5.0f, taper) * emitters[e].sizeScale;
      float auraRad = Math_Mix(4.0f, 10.5f, taper) * emitters[e].sizeScale;
      float bodyPulse = 0.94f + 0.06f * sinf(time * 9.0f - distFromHead * 0.045f);
      float brightness = taper * headFade * bodyPulse;

      Color outerColor = FireDensityColor(0.12f * brightness, 1.0f);
      Color ribbonColor = FireDensityColor(0.42f * brightness, 1.0f);
      Color hotColor = FireDensityColor(0.88f * brightness, 1.0f);

      if (i > 0) {
        DrawLineEx(prevDrawPos, drawPos, auraRad * 1.7f, outerColor);
        DrawLineEx(prevDrawPos, drawPos, ribbonRad * 1.9f, ribbonColor);
        DrawLineEx(prevDrawPos, drawPos, ribbonRad * 0.55f, hotColor);
      }
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, auraRad, outerColor, BLANK);
      DrawCircleGradient((int)drawPos.x, (int)drawPos.y, ribbonRad * 0.85f, hotColor, BLANK);

      if (i % 8 == 0 && i > 4 && i < bodySegments - 5) {
        Vector2 purePerp = {-pureTangent.y, pureTangent.x};
        float sideSign = (i % 14 == 0) ? 1.0f : -1.0f;
        float scaleLen = ribbonRad * Math_Mix(1.9f, 3.1f, taper);
        float baseWidth = ribbonRad * Math_Mix(1.7f, 2.5f, taper);
        Vector2 baseCenter = Vector2Add(drawPos, Vector2Scale(purePerp, sideSign * ribbonRad * 0.55f));
        Vector2 tip = {
            baseCenter.x + purePerp.x * sideSign * scaleLen - pureTangent.x * ribbonRad * 0.65f,
            baseCenter.y + purePerp.y * sideSign * scaleLen - pureTangent.y * ribbonRad * 0.65f
        };
        Vector2 baseA = {
            baseCenter.x - pureTangent.x * baseWidth,
            baseCenter.y - pureTangent.y * baseWidth
        };
        Vector2 baseB = {
            baseCenter.x + pureTangent.x * baseWidth,
            baseCenter.y + pureTangent.y * baseWidth
        };
        Vector2 root = Vector2Add(drawPos, Vector2Scale(purePerp, -sideSign * ribbonRad * 0.12f));
        DrawTriangle(baseA, baseB, tip, FireDensityColor(0.40f * brightness, 1.0f));
        DrawTriangle(baseA, tip, root, FireDensityColor(0.58f * brightness, 1.0f));
        DrawCircleGradient((int)tip.x, (int)tip.y, ribbonRad * 0.55f,
                           FireDensityColor(0.42f * brightness, 1.0f), BLANK);
      }
      prevDrawPos = drawPos;
    }

    // Đuôi chỉ là đoạn ribbon vuốt nhọn, tránh hình gai/gươm làm mất chất dải lụa.
    if (bodySegments > 5) {
      Vector2 tailPos = prevDrawPos;
      Vector2 tailTangent;
      if (bodySegments > 1) {
        tailTangent = Vector2Normalize(Vector2Subtract(emitters[e].sampledPath[bodySegments - 2], emitters[e].sampledPath[bodySegments - 1]));
      } else {
        tailTangent = headPureTangent;
      }
      Vector2 tailTip = {tailPos.x - tailTangent.x * 20.0f,
                         tailPos.y - tailTangent.y * 20.0f};
      DrawLineEx(tailPos, tailTip, 2.5f, FireDensityColor(0.55f, 1.0f));
      DrawCircleGradient((int)tailPos.x, (int)tailPos.y, 6.0f,
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
      float scaleY = DRAGON_HEAD_BASE_SCALE * (1.0f + jawOscillation) * emitters[e].sizeScale;
      Rectangle destRec = {headDrawPos.x, headDrawPos.y,
                           dragonHeadTex.width * DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale,
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
          headDrawPos, headPureTangent, DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale, scaleY, origin,
          (Vector2){320.0f, 180.0f}, flipY);
      Vector2 whiskerBaseBottom = GetTexturePointInWorld(
          headDrawPos, headPureTangent, DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale, scaleY, origin,
          (Vector2){320.0f, 260.0f}, flipY);

      Vector2 prevPtTop = whiskerBaseTop, prevPtBottom = whiskerBaseBottom;
      for (int s = 1; s <= 12; s++) {
        float tFactor = (float)s / 12.0f;
        float segmentDist = (float)s * 5.0f;

        float waveTop = sinf(time * 16.0f - (float)s * 0.7f) * 6.5f * tFactor;
        float waveBottom =
            cosf(time * 16.0f - (float)s * 0.7f) * 6.5f * tFactor;

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
          headDrawPos, headPureTangent, DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale, scaleY, origin,
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

  // Set shader & draw canvas
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

// Projectile Collision Interfaces
int GetFireSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      Vector2 headPos = (emitters[i].pathCount > 0) ? emitters[i].path[0] : emitters[i].startPos;
      outProjectiles[count].position = headPos;
      outProjectiles[count].radius = 15.0f * emitters[i].sizeScale; // Head / body collision radius scaled
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
        Vector2 headPos = (emitters[i].pathCount > 0) ? emitters[i].path[0] : emitters[i].startPos;
        TriggerFireImpact(headPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}

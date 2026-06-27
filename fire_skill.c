#include "fire_skill.h"
#include "force_field.h"
#include "particle_system.h"
#include "path_spline.h"
#include "raymath.h"
#include "ribbon_strip.h"
#include "rlgl.h"
#include "skill_manager.h"
#include "utils_math.h"
#include <math.h>
#include <string.h>

#define MAX_EMITTERS 10

// --- Force Fields của Fire Skill ---
// Mỗi loại particle có "cá tính" riêng, đã được hợp nhất lực drag vào ForceField
static ForceField s_fireImpactField;   // tia lửa va chạm: rơi xuống + drag 2.5
static ForceField s_fireDisperseField; // quầng lửa bốc: curl + bốc lên + drag 3.5
static ForceField s_flameBodyField;    // thân rồng lửa (core): curl nhẹ + bốc lên + drag 9.5
static ForceField s_flameAuraField;    // thân rồng lửa (aura): curl nhẹ + bốc lên + drag 5.2
static ForceField s_fireBurstField;    // tia lửa bắn khi cast: chỉ drag 2.5

#define FIRE_TRAVEL_SPEED 1.8f
#define FIRE_PROGRESS_MAX 2.5f

#define CAST_BURST_RADIUS_MIN 2.5f
#define CAST_BURST_RADIUS_MAX 6.5f
#define CAST_BURST_LIFETIME_MIN 0.3f
#define CAST_BURST_LIFETIME_MAX 0.8f

#define DRAGON_HEAD_BASE_SCALE 0.12f
#define DRAGON_JAW_OSCILLATION_SPEED 30.0f
#define DRAGON_JAW_OSCILLATION_AMP 0.15f
#define DRAGON_HEAD_ORIGIN_X_FACTOR 0.35f

#define DRAGON_BODY_RIBBON_WIDTH_SCALE 2.0f

#define EMITTER_PATH_MAX 360
#define MAX_SAMPLED_SEGMENTS                                                   \
  256 // TỐI ƯU: Tăng kích thước tránh tràn mảng khi hạ spacing
#define MAX_PATH_STEPS_PER_FRAME                                               \
  80 // TỐI ƯU: Chặn đứng tình trạng stall CPU do lag đột biến

extern Camera3D camera;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 p1, p2;
  float headProgress;
  float twistPhase;
  float sizeScale;
  Vector3 path[EMITTER_PATH_MAX];
  int pathCount;
  int pathHead; // ĐÃ CẬP NHẬT: Đầu Ring Buffer
  Vector3 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
  float spawnAccum;
} FireEmitter;

static FireEmitter emitters[MAX_EMITTERS];
static RibbonPoint ribbonBuffer[MAX_SAMPLED_SEGMENTS];

static Texture2D particleTex;
static Shader fireShader;
static int timeLoc;
static Texture2D dragonHeadTex;

static Vector3 GetDragonPathPos(FireEmitter *emitter, float t) {
  if (t <= 1.0f) {
    float dist = Vector3Distance(emitter->startPos, emitter->targetPos);
    Vector3 dir = Vector3Normalize(
        Vector3Subtract(emitter->targetPos, emitter->startPos));

    Vector3 perpX = (Vector3){-dir.z, 0.0f, dir.x};
    if (Vector3Length(perpX) == 0.0f)
      perpX = (Vector3){0.0f, 0.0f, 1.0f};
    perpX = Vector3Normalize(perpX);
    Vector3 perpY = Vector3Normalize(Vector3CrossProduct(dir, perpX));

    float waveFreq = 5.5f;
    float waveAmp = dist * 0.18f * sinf(t * waveFreq + emitter->twistPhase);
    float waveAmpVert =
        dist * 0.10f * cosf(t * waveFreq * 1.5f + emitter->twistPhase);

    Vector3 dynamicP1 = Vector3Add(emitter->p1, Vector3Scale(perpX, waveAmp));
    dynamicP1 = Vector3Add(dynamicP1, Vector3Scale(perpY, waveAmpVert));

    Vector3 dynamicP2 =
        Vector3Add(emitter->p2, Vector3Scale(perpX, -waveAmp * 0.8f));
    dynamicP2 = Vector3Add(dynamicP2, Vector3Scale(perpY, -waveAmpVert * 0.8f));

    return GetBezierPoint(emitter->startPos, dynamicP1, dynamicP2,
                          emitter->targetPos, t);
  }

  float over = t - 1.0f;
  Vector3 vIn =
      Vector3Scale(Vector3Subtract(emitter->targetPos, emitter->p2), 3.0f);
  if (Vector3Length(vIn) > 800.0f)
    vIn = Vector3Scale(Vector3Normalize(vIn), 800.0f);

  Vector3 idealUpPos = {emitter->targetPos.x + sinf(over * 12.0f) * 200.0f,
                        emitter->targetPos.y + 1600.0f * over,
                        emitter->targetPos.z};
  Vector3 inertiaPos = {emitter->targetPos.x + vIn.x * over,
                        emitter->targetPos.y + vIn.y * over,
                        emitter->targetPos.z + vIn.z * over};

  float blend = fminf(over * 3.5f, 1.0f);
  blend = blend * blend * (3.0f - 2.0f * blend);
  return (Vector3){Math_Mix(inertiaPos.x, idealUpPos.x, blend),
                   Math_Mix(inertiaPos.y, idealUpPos.y, blend),
                   Math_Mix(inertiaPos.z, idealUpPos.z, blend)};
}

static Vector3 GetDragonPathTangent(FireEmitter *emitter, float t) {
  Vector3 tangent = Vector3Subtract(GetDragonPathPos(emitter, t + 0.01f),
                                    GetDragonPathPos(emitter, t));
  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){0.0f, 1.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static void TriggerFireImpact(Vector3 pos, float sizeScale) {
  int sparkCount = GetRandomValue(12, 18) * sizeScale;
  for (int s = 0; s < sparkCount; s++) {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = Math_Mix(160.0f, 420.0f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                  sinf(angle) * speed * cosf(pitch)};
    cfg.radius = Math_Mix(0.8f, 2.2f, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(0.3f, 0.7f, Random01());
    cfg.colorStart = (Color){255, 200, 40, 230};
    cfg.colorEnd = (Color){200, 20, 0, 0};
    cfg.forceField = &s_fireImpactField;
    SpawnParticle(cfg);
  }

  int disperseCount = GetRandomValue(14, 22) * sizeScale;
  for (int v = 0; v < disperseCount; v++) {
    float angle = Random01() * PI * 2.0f;
    float speed = Math_Mix(80.0f, 260.0f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed,
                  (Math_Mix(80.0f, 260.0f, Random01()) + 80.0f) * sizeScale,
                  sinf(angle) * speed};
    cfg.radius = Math_Mix(2.5f, 6.5f, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(0.6f, 1.3f, Random01());
    cfg.colorStart = (Color){255, 120, 20, 200};
    cfg.colorEnd = (Color){120, 10, 0, 0};
    cfg.forceField = &s_fireDisperseField;
    SpawnParticle(cfg);
  }

  ParticleConfig staticCore1 = {0};
  staticCore1.position = pos;
  staticCore1.radius = 60.0f * sizeScale * 4.0f;
  staticCore1.lifetime = 0.40f;
  staticCore1.colorStart = (Color){255, 100, 10, 180};
  staticCore1.colorEnd = (Color){0, 0, 0, 0};
  SpawnParticle(staticCore1);

  ParticleConfig staticCore2 = {0};
  staticCore2.position = pos;
  staticCore2.radius = 33.0f * sizeScale * 4.0f;
  staticCore2.lifetime = 0.25f;
  staticCore2.colorStart = (Color){255, 230, 80, 255};
  staticCore2.colorEnd = (Color){100, 0, 0, 0};
  SpawnParticle(staticCore2);
}

void InitFireSkill(int screenWidth, int screenHeight) {
  fireShader = LoadShader(0, "fire.fs");
  timeLoc = GetShaderLocation(fireShader, "u_time");
  dragonHeadTex = LoadTexture("dragon_head.png");

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  particleTex = LoadTextureFromImage(img);
  UnloadImage(img);

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  // --- Khởi tạo ForceField ---

  // Tia lửa va chạm: rơi xuống như than đỏ + cản 2.5
  ForceField_Clear(&s_fireImpactField);
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 180.0f
  });
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 2.5f
  });

  // Quầng lửa bốc: cuộn theo curl + lực bốc lên + cản 3.5
  ForceField_Clear(&s_fireDisperseField);
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = 260.0f
  });
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = 50.0f,
    .noiseScale = 0.015f, .noiseSpeed = 0.6f
  });
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 3.5f
  });

  // Thân rồng (core): curl mạnh làm ngọn lửa uốn lượn + bốc nhẹ + cản 9.5
  ForceField_Clear(&s_flameBodyField);
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = 120.0f,
    .noiseScale = 0.018f, .noiseSpeed = 0.8f
  });
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = 80.0f
  });
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 9.5f
  });

  // Thân rồng (aura): curl mạnh làm ngọn lửa uốn lượn + bốc nhẹ + cản 5.2
  ForceField_Clear(&s_flameAuraField);
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = 120.0f,
    .noiseScale = 0.018f, .noiseSpeed = 0.8f
  });
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = 80.0f
  });
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 5.2f
  });

  // Burst khi cast: chỉ cản 2.5
  ForceField_Clear(&s_fireBurstField);
  ForceField_AddLayer(&s_fireBurstField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 2.5f
  });
}

void CastFireSkill(Vector3 startPos, Vector3 target, float twistPhase,
                   float sizeScale) {
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].headProgress = 0.0f;
      emitters[i].twistPhase = twistPhase;
      emitters[i].sizeScale = sizeScale;
      emitters[i].pathCount = 1;
      emitters[i].pathHead = 0;
      emitters[i].path[0] = startPos;
      emitters[i].spawnAccum = 0.0f;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f));
      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f));
      break;
    }
  }

  ParticleConfig flash = {0};
  flash.position = startPos;
  flash.radius = 80.0f * sizeScale;
  flash.lifetime = 0.25f;
  flash.colorStart = (Color){255, 140, 20, 255};
  flash.colorEnd = (Color){0, 0, 0, 0};
  SpawnParticle(flash);

  int burstCount = GetRandomValue(8, 14) * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    ParticleConfig cfg = {0};
    cfg.position = startPos;
    cfg.velocity = (Vector3){(float)GetRandomValue(-200, 300) * sizeScale,
                             (float)GetRandomValue(100, 400) * sizeScale,
                             (float)GetRandomValue(-200, 300) * sizeScale};
    cfg.radius =
        Math_Mix(CAST_BURST_RADIUS_MIN, CAST_BURST_RADIUS_MAX, Random01()) *
        sizeScale * 4.0f;
    cfg.lifetime =
        Math_Mix(CAST_BURST_LIFETIME_MIN, CAST_BURST_LIFETIME_MAX, Random01());
    cfg.colorStart = (Color){255, 90, 10, 200};
    cfg.colorEnd = (Color){0, 0, 0, 0};
    cfg.forceField = &s_fireBurstField;
    SpawnParticle(cfg);
  }
}

void UpdateFireSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float targetProgress = emitters[e].headProgress + dt * FIRE_TRAVEL_SPEED;
    if (targetProgress >= FIRE_PROGRESS_MAX)
      targetProgress = FIRE_PROGRESS_MAX;

    float step = 0.008f;
    float currentProgress = emitters[e].headProgress;
    int stepCount = 0;

    while (currentProgress < targetProgress &&
           stepCount < MAX_PATH_STEPS_PER_FRAME) {
      stepCount++;
      currentProgress += step;
      if (currentProgress > targetProgress)
        currentProgress = targetProgress;

      Vector3 pos = GetDragonPathPos(&emitters[e], currentProgress);

      float dist =
          (emitters[e].pathCount > 0)
              ? Vector3Distance(pos, emitters[e].path[emitters[e].pathHead])
              : Vector3Distance(pos, emitters[e].startPos);
      if (dist > 1.5f || emitters[e].pathCount == 0) {
        emitters[e].pathHead =
            (emitters[e].pathHead - 1 + EMITTER_PATH_MAX) % EMITTER_PATH_MAX;
        emitters[e].path[emitters[e].pathHead] = pos;
        if (emitters[e].pathCount < EMITTER_PATH_MAX)
          emitters[e].pathCount++;
      }
    }
    emitters[e].headProgress = currentProgress;

    if (emitters[e].headProgress >= FIRE_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    // TỐI ƯU SIÊU TỐC: Trích xuất Ring Buffer ra mảng phẳng bằng memcpy (Không
    // thèm dùng vòng lặp for)
    static Vector3 linearPath[EMITTER_PATH_MAX];
    int head = emitters[e].pathHead;
    int count = emitters[e].pathCount;

    if (head + count <= EMITTER_PATH_MAX) {
      memcpy(linearPath, &emitters[e].path[head], count * sizeof(Vector3));
    } else {
      int part1 = EMITTER_PATH_MAX - head;
      int part2 = count - part1;
      memcpy(linearPath, &emitters[e].path[head], part1 * sizeof(Vector3));
      memcpy(&linearPath[part1], &emitters[e].path[0], part2 * sizeof(Vector3));
    }

    emitters[e].sampledCount =
        SamplePath(linearPath, emitters[e].pathCount, 4.5f,
                   emitters[e].sampledPath, MAX_SAMPLED_SEGMENTS);

    if (emitters[e].headProgress > 1.0f &&
        (emitters[e].headProgress - dt * FIRE_TRAVEL_SPEED) <= 1.0f) {
      Vector3 impactPos = (emitters[e].sampledCount > 0)
                              ? emitters[e].sampledPath[0]
                              : emitters[e].targetPos;
      TriggerFireImpact(impactPos, emitters[e].sizeScale);
    }

    if (emitters[e].sampledCount > 1) {
      emitters[e].spawnAccum += 750.0f * emitters[e].sizeScale * dt;
      int flamesToSpawn = (int)emitters[e].spawnAccum;
      emitters[e].spawnAccum -= flamesToSpawn;

      for (int k = 0; k < flamesToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector3 purePos = emitters[e].sampledPath[idx];
        float normDist = (float)idx / (float)(emitters[e].sampledCount - 1);
        float sizeTaper = powf(1.0f - normDist, 1.2f);
        float rad = Math_Mix(1.5f, 6.0f, sizeTaper) * emitters[e].sizeScale *
                    Math_Mix(0.8f, 1.2f, Random01());

        Vector3 pureTangent =
            (idx < emitters[e].sampledCount - 1)
                ? Vector3Normalize(Vector3Subtract(
                      purePos, emitters[e].sampledPath[idx + 1]))
                : Vector3Normalize(Vector3Subtract(
                      emitters[e].sampledPath[idx - 1], purePos));

        Vector3 randomDir = {Random01() - 0.5f, Random01() - 0.5f,
                             Random01() - 0.5f};
        if (Vector3Length(randomDir) == 0.0f)
          randomDir = (Vector3){0, 1, 0};
        randomDir = Vector3Normalize(randomDir);

        float outwardSpeed =
            Math_Mix(10.0f, 40.0f, Random01()) * emitters[e].sizeScale;
        Vector3 vel = Vector3Scale(randomDir, outwardSpeed);
        float backwardSpeed =
            Math_Mix(160.0f, 340.0f, Random01()) * emitters[e].sizeScale;
        vel = Vector3Add(vel, Vector3Scale(pureTangent, -backwardSpeed));

        Vector3 spawnPos = {purePos.x + randomDir.x * 1.2f * sizeTaper,
                            purePos.y + randomDir.y * 1.2f * sizeTaper,
                            purePos.z + randomDir.z * 1.2f * sizeTaper};

        ParticleConfig cfgCore = {0};
        cfgCore.position = spawnPos;
        cfgCore.velocity = vel;
        cfgCore.radius = rad * 1.8f;
        cfgCore.lifetime = Math_Mix(0.2f, 0.4f, Random01());
        cfgCore.colorStart = (Color){255, 230, 100, 255};
        cfgCore.colorEnd = (Color){255, 60, 0, 0};
        cfgCore.forceField = &s_flameBodyField;
        SpawnParticle(cfgCore);

        ParticleConfig cfgAura = {0};
        cfgAura.position = (Vector3){spawnPos.x + (Random01() - 0.5f) * 3.0f,
                                     spawnPos.y + (Random01() - 0.5f) * 3.0f,
                                     spawnPos.z + (Random01() - 0.5f) * 3.0f};
        cfgAura.velocity = Vector3Scale(vel, 0.75f);
        cfgAura.radius = rad * 4.8f;
        cfgAura.lifetime = Math_Mix(0.35f, 0.65f, Random01());
        cfgAura.colorStart = (Color){255, 90, 15, 140};
        cfgAura.colorEnd = (Color){100, 5, 0, 0};
        cfgAura.forceField = &s_flameAuraField;
        SpawnParticle(cfgAura);
      }
    }
  }
}

void DrawFireSkill(void) {
  bool skillActive = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active)
      skillActive = true;
  }
  if (!skillActive)
    return;

  float time = GetTime();
  rlDisableDepthMask();

  SetShaderValue(fireShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(fireShader);
  BeginBlendMode(BLEND_ADDITIVE);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].sampledCount < 2)
      continue;

    int bodySegments = emitters[e].sampledCount;
    for (int i = 0; i < bodySegments; i++) {
      float normDist = (float)i / (float)(bodySegments - 1);
      float taper = powf(1.0f - normDist, 1.4f);

      float baseWidth = Math_Mix(1.5f, 40.0f, taper);
      float width =
          baseWidth * emitters[e].sizeScale * DRAGON_BODY_RIBBON_WIDTH_SCALE;

      float brightness =
          taper * (0.8f + 0.2f * sinf(time * 9.0f - (i * 8.0f) * 0.045f));
      unsigned char heat = (unsigned char)(255.0f * brightness);

      ribbonBuffer[i].position = emitters[e].sampledPath[i];
      ribbonBuffer[i].halfWidth = width * 0.5f;
      ribbonBuffer[i].tint = (Color){heat, (unsigned char)(heat * 0.6f),
                                     (unsigned char)(heat * 0.1f),
                                     (unsigned char)(255.0f * taper)};
      ribbonBuffer[i].v = normDist;
    }
    DrawRibbonStrip(ribbonBuffer, bodySegments, particleTex, camera);
  }
  EndBlendMode();
  EndShaderMode();

  BeginBlendMode(BLEND_ADDITIVE);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].sampledCount < 2)
      continue;

    Vector3 tangent =
        GetDragonPathTangent(&emitters[e], emitters[e].headProgress);
    float scale = DRAGON_HEAD_BASE_SCALE * emitters[e].sizeScale;
    float jawAnim = 1.0f + sinf(time * DRAGON_JAW_OSCILLATION_SPEED) *
                               DRAGON_JAW_OSCILLATION_AMP;
    Vector2 size = {dragonHeadTex.width * scale,
                    dragonHeadTex.height * scale * jawAnim};
    Vector2 origin = {size.x * DRAGON_HEAD_ORIGIN_X_FACTOR, size.y * 0.5f};

    Vector3 basePoint = emitters[e].sampledPath[0];
    float offsetBackward = size.x * 0.38f;
    Vector3 headPos =
        Vector3Subtract(basePoint, Vector3Scale(tangent, offsetBackward));

    Vector3 pointAhead = Vector3Add(headPos, Vector3Scale(tangent, 100.0f));
    Vector2 screenHead = GetWorldToScreen(headPos, camera);
    Vector2 dir =
        Vector2Subtract(GetWorldToScreen(pointAhead, camera), screenHead);
    float rotation = -atan2f(dir.y, dir.x) * RAD2DEG;
    Rectangle sourceRec = {0.0f, 0.0f, (float)dragonHeadTex.width,
                           (float)dragonHeadTex.height *
                               ((dir.x < 0.0f) ? -1.0f : 1.0f)};

    float fade = (emitters[e].headProgress > FIRE_PROGRESS_MAX - 0.4f)
                     ? Clamp(1.0f - (emitters[e].headProgress -
                                     (FIRE_PROGRESS_MAX - 0.4f)) /
                                        0.4f,
                             0.0f, 1.0f)
                     : 1.0f;
    unsigned char alpha = (unsigned char)(255.0f * fade);

    DrawBillboardPro(camera, dragonHeadTex, sourceRec, headPos, camera.up,
                     (Vector2){size.x * 1.3f, size.y * 1.3f},
                     (Vector2){origin.x * 1.3f, origin.y * 1.3f}, rotation,
                     (Color){255, 40, 0, (unsigned char)(alpha * 0.6f)});
    DrawBillboardPro(camera, dragonHeadTex, sourceRec, headPos, camera.up, size,
                     origin, rotation, (Color){255, 180, 40, alpha});
  }
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadFireSkill(void) {
  UnloadShader(fireShader);
  UnloadTexture(dragonHeadTex);
  UnloadTexture(particleTex);
}

int GetFireSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      Vector3 headPos = (emitters[i].sampledCount > 0)
                            ? emitters[i].sampledPath[0]
                            : emitters[i].startPos;
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
        Vector3 headPos = (emitters[i].sampledCount > 0)
                              ? emitters[i].sampledPath[0]
                              : emitters[i].startPos;
        TriggerFireImpact(headPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}
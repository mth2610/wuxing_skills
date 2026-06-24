#include "metal_skill.h"
#include "particle_system.h"
#include "raymath.h"
#include "ribbon_strip.h"
#include "rlgl.h"
#include "skill_manager.h"
#include <math.h>
#include <stddef.h>

// Hàm nội suy mượt (Smoothstep) tự viết cho C
static float SmoothStepC(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

#define MAX_METAL_PARTICLES 350
#define MAX_EMITTERS 10
#define PARTICLE_HISTORY_COUNT 20

#define SPARKLE_SPAWN_CHANCE_PERCENT 30

extern Camera3D camera;

typedef enum {
  PARTICLE_SWORD = 0,
  PARTICLE_PORTAL = 1,
  PARTICLE_WISP = 2
} MetalParticleType;

typedef struct {
  Vector3 position;
  Vector3 velocity;
  Vector3 target;
  float length;
  float thickness;
  float lifetime;
  float maxLifetime;
  int type;
  bool active;

  Vector3 history[PARTICLE_HISTORY_COUNT];
  int historyCount;
  float angle;
  float wobblePhase;
  float scale;
} MetalParticle;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 portalPositions[5];
  float spawnDelay[5];
  bool spawned[5];
  bool portalSpawned[5];
  float portalSizes[5];
  int count;
  float timer;
} MetalEmitter;

static MetalParticle metalPool[MAX_METAL_PARTICLES];
static MetalEmitter emitters[MAX_EMITTERS];
static int lastUsedIndex = 0;

static Shader metalShader;
static int timeLocMetal;
static Texture2D swordSprite;

static void DrawCameraFacingQuad(Vector3 center, float width, float height,
                                 float rotation, Color tint, Texture2D tex) {
  Matrix matView = GetCameraMatrix(camera);
  Vector3 right = {matView.m0, matView.m4, matView.m8};
  Vector3 up = {matView.m1, matView.m5, matView.m9};

  float cosR = cosf(rotation);
  float sinR = sinf(rotation);
  Vector3 rVec = Vector3Add(Vector3Scale(right, cosR), Vector3Scale(up, sinR));
  Vector3 uVec =
      Vector3Subtract(Vector3Scale(up, cosR), Vector3Scale(right, sinR));

  Vector3 tl =
      Vector3Add(Vector3Subtract(center, Vector3Scale(rVec, width * 0.5f)),
                 Vector3Scale(uVec, height * 0.5f));
  Vector3 tr = Vector3Add(Vector3Add(center, Vector3Scale(rVec, width * 0.5f)),
                          Vector3Scale(uVec, height * 0.5f));
  Vector3 bl =
      Vector3Subtract(Vector3Subtract(center, Vector3Scale(rVec, width * 0.5f)),
                      Vector3Scale(uVec, height * 0.5f));
  Vector3 br =
      Vector3Subtract(Vector3Add(center, Vector3Scale(rVec, width * 0.5f)),
                      Vector3Scale(uVec, height * 0.5f));

  if (tex.id > 0)
    rlSetTexture(tex.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlTexCoord2f(0.0f, 0.0f);
  rlVertex3f(tl.x, tl.y, tl.z);
  rlTexCoord2f(0.0f, 1.0f);
  rlVertex3f(bl.x, bl.y, bl.z);
  rlTexCoord2f(1.0f, 1.0f);
  rlVertex3f(br.x, br.y, br.z);
  rlTexCoord2f(1.0f, 0.0f);
  rlVertex3f(tr.x, tr.y, tr.z);
  rlEnd();
  if (tex.id > 0)
    rlSetTexture(0);
}

static void SpawnMetal(int type, Vector3 pos, Vector3 vel, float len,
                       float thick, float life, Vector3 target,
                       float initialAngle, float wobblePhase, float scale) {
  for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
    int index = (lastUsedIndex + i) % MAX_METAL_PARTICLES;
    if (!metalPool[index].active) {
      metalPool[index].type = type;
      metalPool[index].position = pos;
      metalPool[index].velocity = vel;
      metalPool[index].target = target;
      metalPool[index].length = len;
      metalPool[index].thickness = thick;
      metalPool[index].lifetime = life;
      metalPool[index].maxLifetime = life;
      metalPool[index].active = true;
      metalPool[index].angle = initialAngle;
      metalPool[index].wobblePhase = wobblePhase;
      metalPool[index].scale = scale;

      // KỸ THUẬT: TẠO DẢI KHÍ TỨC THÌ CHO WISP
      if (type == PARTICLE_WISP) {
        metalPool[index].historyCount = PARTICLE_HISTORY_COUNT;
        Vector3 strandDir = target;

        float waveFreq = (float)GetRandomValue(10, 20);
        float waveAmp = (float)GetRandomValue(5, 18) * scale;

        for (int h = 0; h < PARTICLE_HISTORY_COUNT; h++) {
          float t = (float)h / (PARTICLE_HISTORY_COUNT - 1);
          Vector3 basePos = Vector3Add(pos, Vector3Scale(strandDir, t * len));

          Vector3 wUp = (Vector3){0.0f, 1.0f, 0.0f};
          if (fabsf(strandDir.y) > 0.99f)
            wUp = (Vector3){1.0f, 0.0f, 0.0f};
          Vector3 wRight =
              Vector3Normalize(Vector3CrossProduct(strandDir, wUp));

          float wave = sinf(t * waveFreq + wobblePhase) * waveAmp * t;
          metalPool[index].history[h] =
              Vector3Add(basePos, Vector3Scale(wRight, wave));
        }
      } else {
        metalPool[index].historyCount = 0;
        for (int h = 0; h < PARTICLE_HISTORY_COUNT; h++) {
          metalPool[index].history[h] = pos;
        }
      }

      lastUsedIndex = (index + 1) % MAX_METAL_PARTICLES;
      break;
    }
  }
}

void InitMetalSkill(int screenWidth, int screenHeight) {
  metalShader = LoadShader(0, "metal.fs");
  timeLocMetal = GetShaderLocation(metalShader, "u_time");
  swordSprite = LoadTexture("sword.png");

  for (int i = 0; i < MAX_METAL_PARTICLES; i++)
    metalPool[i].active = false;
  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;
}

void CastMetalSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  int count = params.quantity;
  float sizeScale = params.sizeScale;

  if (count > 5)
    count = 5;
  if (count < 1)
    count = 1;

  int emitterIndex = -1;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitterIndex = i;
      break;
    }
  }
  if (emitterIndex == -1)
    return;

  MetalEmitter *em = &emitters[emitterIndex];
  em->active = true;
  em->startPos = startPos;
  em->targetPos = target;
  em->count = count;
  em->timer = 0.0f;

  Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
  Vector3 perp = (Vector3){-dir.z, 0.0f, dir.x};

  for (int j = 0; j < count; j++) {
    em->spawned[j] = false;
    em->portalSpawned[j] = false;
    em->spawnDelay[j] = (float)j * 0.15f;

    float offsetFactor = (float)j - (float)(count - 1) / 2.0f;
    em->portalPositions[j] = Vector3Add(
        startPos, Vector3Scale(perp, offsetFactor * 40.0f * sizeScale));

    float distFromCenter = fabsf(offsetFactor);
    if (distFromCenter < 0.1f)
      em->portalSizes[j] = 0.80f * sizeScale;
    else if (distFromCenter < 1.1f)
      em->portalSizes[j] = 0.56f * sizeScale;
    else
      em->portalSizes[j] = 0.42f * sizeScale;
  }

  for (int i = 0; i < 22; i++) {
    Vector3 flashVel = {
        dir.x * GetRandomValue(250, 500) + GetRandomValue(-120, 120),
        dir.y * GetRandomValue(250, 500) + GetRandomValue(-120, 120),
        dir.z * GetRandomValue(250, 500) + GetRandomValue(-120, 120)};
    ParticleConfig p = {0};
    p.position = startPos;
    p.velocity = flashVel;
    p.radius = (float)GetRandomValue(10, 22);
    p.lifetime = 0.35f;
    p.drag = 1.0f;
    p.turbulence = 40.0f;
    p.colorStart = (Color){255, 230, 100, 255};
    p.colorEnd = (Color){255, 180, 0, 0};
    p.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_TURBULENCE;
    SpawnParticle(p);
  }
}

void UpdateMetalSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    emitters[e].timer += dt;
    bool allSpawned = true;

    for (int j = 0; j < emitters[e].count; j++) {
      if (!emitters[e].spawned[j]) {
        allSpawned = false;
        Vector3 portalPos = emitters[e].portalPositions[j];
        float sizeFactor = emitters[e].portalSizes[j];

        float portalAppearTime = emitters[e].spawnDelay[j] - 0.25f;
        if (portalAppearTime < 0.0f)
          portalAppearTime = 0.0f;

        if (emitters[e].timer >= portalAppearTime &&
            !emitters[e].portalSpawned[j]) {
          emitters[e].portalSpawned[j] = true;
          float portalLife =
              emitters[e].spawnDelay[j] - emitters[e].timer + 0.25f;
          if (portalLife < 0.25f)
            portalLife = 0.25f;
          SpawnMetal(PARTICLE_PORTAL, portalPos, (Vector3){0, 0, 0},
                     45.0f * sizeFactor, 0.0f, portalLife, (Vector3){0, 0, 0},
                     0.0f, 0.0f, sizeFactor);
        }

        if (emitters[e].portalSpawned[j] &&
            emitters[e].timer < emitters[e].spawnDelay[j]) {
          if (GetRandomValue(1, 100) <= 25) {
            float spawnAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
            float spawnDist = (float)GetRandomValue(25, 45) * sizeFactor;
            Vector3 sparkPos = {portalPos.x + cosf(spawnAngle) * spawnDist,
                                portalPos.y + GetRandomValue(-10, 10),
                                portalPos.z + sinf(spawnAngle) * spawnDist};
            Vector3 sparkVel = Vector3Scale(
                Vector3Normalize(Vector3Subtract(portalPos, sparkPos)),
                (float)GetRandomValue(140, 240));

            ParticleConfig p = {0};
            p.position = sparkPos;
            p.velocity = sparkVel;
            p.radius = (float)GetRandomValue(5, 10);
            p.lifetime = spawnDist / Vector3Length(sparkVel);
            p.colorStart = (Color){255, 240, 120, 255};
            p.colorEnd = (Color){255, 150, 0, 0};
            SpawnParticle(p);
          }
        }

        if (emitters[e].timer >= emitters[e].spawnDelay[j]) {
          emitters[e].spawned[j] = true;
          Vector3 baseDir = Vector3Normalize(
              Vector3Subtract(emitters[e].targetPos, portalPos));
          float spreadAngle =
              (((float)j - (float)(emitters[e].count - 1) / 2.0f) * 16.0f) *
              DEG2RAD;

          Vector3 launchDir = {
              baseDir.x * cosf(spreadAngle) - baseDir.z * sinf(spreadAngle),
              baseDir.y,
              baseDir.x * sinf(spreadAngle) + baseDir.z * cosf(spreadAngle)};
          launchDir = Vector3Normalize(launchDir);

          Vector3 vel =
              Vector3Scale(launchDir, (float)GetRandomValue(300, 500));

          SpawnMetal(PARTICLE_SWORD, portalPos, vel,
                     (float)GetRandomValue(100, 140) * sizeFactor,
                     (float)GetRandomValue(12, 16) * sizeFactor, 2.0f,
                     emitters[e].targetPos, 0.0f,
                     (float)GetRandomValue(0, 100) * 0.1f, sizeFactor);
        }
      }
    }
    if (allSpawned)
      emitters[e].active = false;
  }

  for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
    if (!metalPool[i].active)
      continue;

    metalPool[i].lifetime -= dt;
    if (metalPool[i].lifetime <= 0.0f) {
      metalPool[i].active = false;
      continue;
    }

    if (metalPool[i].type == PARTICLE_SWORD) {
      for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--)
        metalPool[i].history[h] = metalPool[i].history[h - 1];

      Vector3 dir = Vector3Normalize(metalPool[i].velocity);
      metalPool[i].history[0] =
          Vector3Subtract(metalPool[i].position,
                          Vector3Scale(dir, metalPool[i].length * 0.45f));

      if (metalPool[i].historyCount < PARTICLE_HISTORY_COUNT)
        metalPool[i].historyCount++;

      metalPool[i].wobblePhase += dt * 8.0f;
      metalPool[i].position = Vector3Add(
          metalPool[i].position, Vector3Scale(metalPool[i].velocity, dt));

      // -----------------------------------------------------------------------
      // SPAWN: DẢI CHÂN KHÍ BÁM DỌC THANH KIẾM
      // -----------------------------------------------------------------------
      int wispCount = GetRandomValue(1, 2);
      for (int w = 0; w < wispCount; w++) {
        // 1. Phân bố đều 100% dọc theo sống kiếm (-100 đến 100 = từ mũi tới
        // chuôi)
        float randOffset = (float)GetRandomValue(-100, 100) / 100.0f;
        Vector3 pointOnBlade = Vector3Add(
            metalPool[i].position,
            Vector3Scale(dir, metalPool[i].length * 0.45f * randOffset));

        Vector3 upVec = (Vector3){0.0f, 1.0f, 0.0f};
        if (fabsf(dir.y) > 0.99f)
          upVec = (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 rightVec = Vector3Normalize(Vector3CrossProduct(dir, upVec));
        Vector3 realUp = Vector3Normalize(Vector3CrossProduct(rightVec, dir));

        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        Vector3 perpOut = Vector3Add(Vector3Scale(rightVec, cosf(angle)),
                                     Vector3Scale(realUp, sinf(angle)));

        Vector3 strandDir = Vector3Normalize(
            Vector3Add(Vector3Scale(perpOut, 0.15f), Vector3Scale(dir, -1.0f)));

        // 2. Vận tốc bốc hơi: Bay THEO kiếm (metalPool[i].velocity), chỉ trượt
        // lùi về sau 1 chút (-120 đến -50)
        Vector3 slideVel =
            Vector3Add(Vector3Scale(perpOut, (float)GetRandomValue(10, 30)),
                       Vector3Scale(dir, (float)GetRandomValue(-120, -50)));
        Vector3 wispVel = Vector3Add(metalPool[i].velocity, slideVel);

        float wispLen = (float)GetRandomValue(40, 90) * metalPool[i].scale;
        float wispThick = (float)GetRandomValue(4, 12) * metalPool[i].scale;
        float wispLife = (float)GetRandomValue(15, 30) *
                         0.01f; // Sống ngắn để tan ngay khi trượt ra khỏi kiếm
        float wispPhase = (float)GetRandomValue(0, 314) * 0.01f;

        SpawnMetal(PARTICLE_WISP, pointOnBlade, wispVel, wispLen, wispThick,
                   wispLife, strandDir, 0.0f, wispPhase, metalPool[i].scale);
      }

      Vector3 toTarget =
          Vector3Subtract(metalPool[i].target, metalPool[i].position);
      float distToTarget = Vector3Length(toTarget);

      if (distToTarget > 20.0f) {
        Vector3 desiredDir = Vector3Normalize(toTarget);
        float newSpeed =
            fminf(Vector3Length(metalPool[i].velocity) + 150.0f * dt, 600.0f);
        Vector3 perpDir = {-desiredDir.z, 0.0f, desiredDir.x};
        float curveStrength = fminf(distToTarget / 250.0f, 1.0f);
        float wobble =
            sinf(metalPool[i].wobblePhase) * 350.0f * curveStrength * dt;
        Vector3 desiredVel = Vector3Add(Vector3Scale(desiredDir, newSpeed),
                                        Vector3Scale(perpDir, wobble));
        metalPool[i].velocity =
            Vector3Lerp(metalPool[i].velocity, desiredVel, dt * 3.2f);
      }

      if (distToTarget < 30.0f || metalPool[i].lifetime < 0.1f) {
        metalPool[i].active = false;
        float scale = fmaxf(metalPool[i].scale, 0.5f);

        for (int s = 0; s < GetRandomValue(10, 16); s++) {
          float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
          float pAngle = (float)GetRandomValue(15, 75) * DEG2RAD;
          float sp = (float)GetRandomValue(280, 650) * scale;
          ParticleConfig shard = {0};
          shard.position = metalPool[i].position;
          shard.velocity = (Vector3){cosf(sAngle) * sp * cosf(pAngle),
                                     sinf(pAngle) * sp + 100.0f,
                                     sinf(sAngle) * sp * cosf(pAngle)};
          shard.force = (Vector3){0, -800.0f, 0};
          shard.radius = (float)GetRandomValue(8, 18) * scale;
          shard.lifetime = 0.60f;
          shard.colorStart = (Color){255, 230, 100, 255};
          shard.colorEnd = (Color){255, 180, 0, 0};
          shard.physicsFlags = P_PHYSICS_FORCE;
          SpawnParticle(shard);
        }

        for (int s = 0; s < GetRandomValue(14, 22); s++) {
          float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
          float pAngle = (float)GetRandomValue(-45, 45) * DEG2RAD;
          float sp = (float)GetRandomValue(400, 850) * scale;
          ParticleConfig spark = {0};
          spark.position = metalPool[i].position;
          spark.velocity =
              (Vector3){cosf(sAngle) * sp * cosf(pAngle), sinf(pAngle) * sp,
                        sinf(sAngle) * sp * cosf(pAngle)};
          spark.drag = 1.5f;
          spark.radius = (float)GetRandomValue(6, 16);
          spark.lifetime = 0.35f;
          spark.colorStart = (Color){255, 255, 200, 255};
          spark.colorEnd = (Color){255, 150, 0, 0};
          spark.physicsFlags = P_PHYSICS_DRAG;
          SpawnParticle(spark);
        }

        ParticleConfig shockwave = {0};
        shockwave.position = metalPool[i].position;
        shockwave.velocity = (Vector3){0, 0, 0};
        shockwave.radius = 55.0f * scale;
        shockwave.lifetime = 0.28f;
        shockwave.colorStart = (Color){255, 230, 150, 255};
        shockwave.colorEnd = (Color){255, 180, 0, 0};
        SpawnParticle(shockwave);
      }
    } else if (metalPool[i].type == PARTICLE_WISP) {
      // Giảm lực cản cực thấp để luồng khí trôi THEO kiếm, chứ không bị tuột
      // lại tít mù phía sau
      metalPool[i].velocity =
          Vector3Scale(metalPool[i].velocity, 1.0f - (dt * 0.8f));
      Vector3 drift = Vector3Scale(metalPool[i].velocity, dt);

      metalPool[i].wobblePhase += dt * 15.0f;

      Vector3 sDir = metalPool[i].target;
      Vector3 wUp = (Vector3){0.0f, 1.0f, 0.0f};
      if (fabsf(sDir.y) > 0.99f)
        wUp = (Vector3){1.0f, 0.0f, 0.0f};
      Vector3 wRight = Vector3Normalize(Vector3CrossProduct(sDir, wUp));

      for (int h = 0; h < metalPool[i].historyCount; h++) {
        float t = (float)h / (PARTICLE_HISTORY_COUNT - 1);
        // Cập nhật vị trí bằng cách cộng Vector drift (mang vận tốc của kiếm)
        metalPool[i].history[h] = Vector3Add(metalPool[i].history[h], drift);
        float wriggle =
            cosf(t * 15.0f + metalPool[i].wobblePhase) * 12.0f * t * dt;
        metalPool[i].history[h] =
            Vector3Add(metalPool[i].history[h], Vector3Scale(wRight, wriggle));
      }
    } else if (metalPool[i].type == PARTICLE_PORTAL) {
      metalPool[i].angle += 140.0f * dt;
    }
  }
}

void DrawMetalSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      active = true;
      break;
    }
  }
  if (!active) {
    for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
      if (metalPool[i].active) {
        active = true;
        break;
      }
    }
  }
  if (!active)
    return;

  float time = (float)GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  SetShaderValue(metalShader, timeLocMetal, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(metalShader);

  for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
    if (!metalPool[i].active)
      continue;
    float lifeRatio = metalPool[i].lifetime / metalPool[i].maxLifetime;

    if (metalPool[i].type == PARTICLE_SWORD) {
      if (metalPool[i].historyCount > 1) {
        RibbonPoint outerStrip[PARTICLE_HISTORY_COUNT];
        RibbonPoint innerStrip[PARTICLE_HISTORY_COUNT];

        for (int h = 0; h < metalPool[i].historyCount; h++) {
          float segRatio =
              1.0f - (float)h / (float)(metalPool[i].historyCount - 1);
          float taper = powf(segRatio, 1.2f);

          outerStrip[h].position = metalPool[i].history[h];
          outerStrip[h].halfWidth = metalPool[i].thickness * 1.3f * taper;
          outerStrip[h].v = segRatio;
          outerStrip[h].tint = (Color){(unsigned char)(segRatio * 255.0f), 45,
                                       45, (unsigned char)(80.0f * lifeRatio)};

          innerStrip[h].position = metalPool[i].history[h];
          innerStrip[h].halfWidth = metalPool[i].thickness * 0.65f * taper;
          innerStrip[h].v = segRatio;
          innerStrip[h].tint = (Color){(unsigned char)(segRatio * 255.0f), 45,
                                       45, (unsigned char)(255.0f * lifeRatio)};
        }
        DrawRibbonStrip(outerStrip, metalPool[i].historyCount, (Texture2D){0},
                        camera);
        DrawRibbonStrip(innerStrip, metalPool[i].historyCount, (Texture2D){0},
                        camera);
      }

      Matrix matView = GetCameraMatrix(camera);
      Vector3 right = {matView.m0, matView.m4, matView.m8};
      Vector3 up = {matView.m1, matView.m5, matView.m9};
      Vector3 vDir = Vector3Normalize(metalPool[i].velocity);
      float rotation =
          atan2f(Vector3DotProduct(vDir, up), Vector3DotProduct(vDir, right));

      Color spriteTint =
          (Color){128, 128, 128, (unsigned char)(255.0f * lifeRatio)};
      DrawCameraFacingQuad(metalPool[i].position, metalPool[i].length * 1.1f,
                           metalPool[i].thickness * 2.0f, rotation, spriteTint,
                           swordSprite);

    } else if (metalPool[i].type == PARTICLE_WISP) {
      if (metalPool[i].historyCount > 1) {
        RibbonPoint wispStrip[PARTICLE_HISTORY_COUNT];
        for (int h = 0; h < metalPool[i].historyCount; h++) {
          float segRatio =
              1.0f - (float)h / (float)(metalPool[i].historyCount - 1);

          float taper = SmoothStepC(0.0f, 0.2f, segRatio) *
                        SmoothStepC(1.0f, 0.5f, 1.0f - segRatio);

          wispStrip[h].position = metalPool[i].history[h];
          wispStrip[h].halfWidth = metalPool[i].thickness * 0.8f * taper;
          wispStrip[h].v = segRatio;

          unsigned char finalAlpha =
              (unsigned char)(255.0f * lifeRatio * taper);
          wispStrip[h].tint = (Color){255, 45, 45, finalAlpha};
        }
        DrawRibbonStrip(wispStrip, metalPool[i].historyCount, (Texture2D){0},
                        camera);
      }
    } else if (metalPool[i].type == PARTICLE_PORTAL) {
      float radius = metalPool[i].length;
      float age = metalPool[i].maxLifetime - metalPool[i].lifetime;
      if (age < 0.12f)
        radius *= (age / 0.12f);

      Color portalTint =
          (Color){128, 128, 220, (unsigned char)(255.0f * lifeRatio)};
      DrawCameraFacingQuad(metalPool[i].position, radius * 2.6f, radius * 2.6f,
                           metalPool[i].angle * DEG2RAD, portalTint,
                           (Texture2D){0});
    }
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadMetalSkill(void) {
  UnloadTexture(swordSprite);
  UnloadShader(metalShader);
}

int GetMetalSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
    if (metalPool[i].active && metalPool[i].type == PARTICLE_SWORD &&
        count < maxProjectiles) {
      outProjectiles[count].position = metalPool[i].position;
      outProjectiles[count].radius = 9.0f * metalPool[i].scale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateMetalProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_METAL_PARTICLES; i++) {
    if (metalPool[i].active && metalPool[i].type == PARTICLE_SWORD) {
      if (count == index) {
        metalPool[i].lifetime = 0.0f;
        break;
      }
      count++;
    }
  }
}
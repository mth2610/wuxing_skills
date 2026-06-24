#include "metal_skill.h"
#include "particle_system.h"
#include "raymath.h"
#include "ribbon_strip.h"
#include "rlgl.h"
#include "skill_manager.h"
#include <math.h>
#include <stddef.h>

#define MAX_METAL_PARTICLES                                                    \
  200 // Giữ nguyên mức thấp vì đã ủy thác mảnh vỡ cho GPU
#define MAX_EMITTERS 10
#define PARTICLE_HISTORY_COUNT 20 // Ribbon dài mượt

extern Camera3D camera;

typedef enum { PARTICLE_SWORD = 0, PARTICLE_PORTAL = 1 } MetalParticleType;

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

static MetalParticle metalPool[MAX_METAL_PARTICLES];
static MetalEmitter emitters[MAX_EMITTERS];
static int lastUsedIndex = 0;

static Shader metalShader;
static int timeLocMetal;
static Texture2D swordSprite;

// Helper vẽ Billboard 3D xoay tự do
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
      metalPool[index].historyCount = 0;
      metalPool[index].angle = initialAngle;
      metalPool[index].wobblePhase = wobblePhase;
      metalPool[index].scale = scale;

      for (int h = 0; h < PARTICLE_HISTORY_COUNT; h++) {
        metalPool[index].history[h] = pos;
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

  // Nổ tia lửa lúc Cast (Đổi sang dải màu vàng cam hệ Kim)
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
    p.colorStart = (Color){255, 230, 100, 255}; // Vàng kim chói
    p.colorEnd = (Color){255, 100, 0, 0};       // Cam đỏ fade
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
            p.colorStart = (Color){255, 220, 80, 255}; // Tia lửa vàng
            p.colorEnd = (Color){200, 80, 0, 0};
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

      // Đã sửa: Dời điểm ghim kiếm khí lùi về phía chuôi kiếm (tránh bị hở
      // đuôi)
      Vector3 dir = Vector3Normalize(metalPool[i].velocity);
      metalPool[i].history[0] =
          Vector3Subtract(metalPool[i].position,
                          Vector3Scale(dir, metalPool[i].length * 0.45f));

      if (metalPool[i].historyCount < PARTICLE_HISTORY_COUNT)
        metalPool[i].historyCount++;

      metalPool[i].wobblePhase += dt * 8.0f;
      metalPool[i].position = Vector3Add(
          metalPool[i].position, Vector3Scale(metalPool[i].velocity, dt));

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

      // Impact Trigger
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
          shard.colorStart = (Color){255, 220, 100, 255}; // Mảnh vỡ vàng
          shard.colorEnd = (Color){180, 60, 0, 0};
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
          spark.colorStart = (Color){255, 245, 180, 255}; // Tia nổ sáng chói
          spark.colorEnd = (Color){255, 120, 0, 0};
          spark.physicsFlags = P_PHYSICS_DRAG;
          SpawnParticle(spark);
        }

        ParticleConfig shockwave = {0};
        shockwave.position = metalPool[i].position;
        shockwave.velocity = (Vector3){0, 0, 0};
        shockwave.radius = 55.0f * scale;
        shockwave.lifetime = 0.28f;
        shockwave.colorStart = (Color){255, 230, 150, 255};
        shockwave.colorEnd = (Color){255, 100, 0, 0};
        SpawnParticle(shockwave);
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
      // Draw Ribbon Trail
      if (metalPool[i].historyCount > 1) {
        RibbonPoint strip[PARTICLE_HISTORY_COUNT];
        for (int h = 0; h < metalPool[i].historyCount; h++) {
          strip[h].position = metalPool[i].history[h];
          float segRatio =
              1.0f - (float)h / (float)(metalPool[i].historyCount - 1);

          float taper = segRatio * segRatio * segRatio;

          // Làm kiếm khí bành ra một chút để bao bọc lưỡi kiếm
          strip[h].halfWidth = metalPool[i].thickness * 0.55f * taper;

          strip[h].v = segRatio;

          unsigned char glowByte = (unsigned char)(lifeRatio * 255.0f);
          strip[h].tint = (Color){(unsigned char)(segRatio * 255.0f), glowByte,
                                  45, (unsigned char)(220.0f * lifeRatio)};
        }
        DrawRibbonStrip(strip, metalPool[i].historyCount, (Texture2D){0},
                        camera);
      }

      // Draw Sword Sprite
      // Draw Sword Sprite
      Matrix matView = GetCameraMatrix(camera);
      Vector3 right = {matView.m0, matView.m4, matView.m8};
      Vector3 up = {matView.m1, matView.m5, matView.m9};
      Vector3 vDir = Vector3Normalize(metalPool[i].velocity);
      float rotation =
          atan2f(Vector3DotProduct(vDir, up), Vector3DotProduct(vDir, right));

      Color spriteTint =
          (Color){128, 128, 128, (unsigned char)(255.0f * lifeRatio)};

      // Đã sửa: Đảo lại đúng trục (width = length, height = thickness)
      // Chỉnh lại tỷ lệ cho cân đối (không quá dài, không quá mập)
      DrawCameraFacingQuad(metalPool[i].position,
                           metalPool[i].length * 1.1f, // Thu ngắn lại
                           metalPool[i].thickness * 2.0f, // Bề ngang vừa phải
                           rotation, spriteTint, swordSprite);

    } else if (metalPool[i].type == PARTICLE_PORTAL) {
      // Draw Portal Disc
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
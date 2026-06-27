#include "skills/metal/metal_projectile/metal_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "raymath.h"
#include "core/skill_manager.h"
#include "core/trail_system.h"
#include <math.h>
#include <stddef.h>

#define MAX_EMITTERS 10
#define METAL_SKILL_TAG 1
#define METAL_SWORD_WISP_COUNT 10

static ForceField s_metalSparkField;     // tia lửa khi kiếm bể: Perlin + drag 1.5
static ForceField s_metalCastSparkField; // tia lửa khi cast kiếm: Perlin + drag 1.0
static ForceField s_metalShardField;     // mảnh vỡ kiếm: gravity
static ForceField s_metalWispField;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 spawnPositions[5];
  float spawnDelay[5];
  bool spawned[5];
  float projectileSizes[5];
  int count;
  float timer;
} MetalEmitter;

static MetalEmitter emitters[MAX_EMITTERS];
static Texture2D swordSprite;
static Shader swordShader;

// Mảng chứa 10 dải lụa cho mỗi thanh kiếm
static int swordFollowers[MAX_TRAIL_PARTICLES][METAL_SWORD_WISP_COUNT];

static void SwordDeathCallback(Vector3 pos, float scale) {

  for (int s = 0; s < GetRandomValue(10, 16); s++) {
    float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
    float pAngle = (float)GetRandomValue(15, 75) * DEG2RAD;
    float sp = (float)GetRandomValue(280, 650) * scale;
    ParticleConfig shard = {0};
    shard.position = pos;
    shard.velocity =
        (Vector3){cosf(sAngle) * sp * cosf(pAngle), sinf(pAngle) * sp + 100.0f,
                  sinf(sAngle) * sp * cosf(pAngle)};
    shard.radius = (float)GetRandomValue(8, 18) * scale;
    shard.lifetime = 0.60f;
    shard.colorStart = (Color){255, 230, 100, 255};
    shard.colorEnd = (Color){255, 180, 0, 0};
    shard.forceField = &s_metalShardField;
    SpawnParticle(shard);
  }

  for (int s = 0; s < GetRandomValue(14, 22); s++) {
    float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
    float pAngle = (float)GetRandomValue(-45, 45) * DEG2RAD;
    float sp = (float)GetRandomValue(400, 850) * scale;
    ParticleConfig spark = {0};
    spark.position = pos;
    spark.velocity =
        (Vector3){cosf(sAngle) * sp * cosf(pAngle), sinf(pAngle) * sp,
                  sinf(sAngle) * sp * cosf(pAngle)};
    spark.radius = (float)GetRandomValue(6, 16);
    spark.lifetime = 0.35f;
    spark.colorStart = (Color){255, 255, 200, 255};
    spark.colorEnd = (Color){255, 150, 0, 0};
    spark.forceField = &s_metalSparkField;
    SpawnParticle(spark);
  }

  ParticleConfig shockwave = {0};
  shockwave.position = pos;
  shockwave.velocity = (Vector3){0, 0, 0};
  shockwave.radius = 55.0f * scale;
  shockwave.lifetime = 0.28f;
  shockwave.colorStart = (Color){255, 230, 150, 255};
  shockwave.colorEnd = (Color){255, 180, 0, 0};
  SpawnParticle(shockwave);
}

static void SwordUpdateCallback(int trailId, float dt) {
  (void)dt;
  TrailEntity *sword = GetTrail(trailId);
  if (!sword)
    return;

  Vector3 dir = Vector3Normalize(sword->velocity);

  // Đã XÓA frontBias vì không cần thiết nữa

  for (int k = 0; k < METAL_SWORD_WISP_COUNT; k++) {
    int followerId = swordFollowers[trailId][k];
    if (followerId < 0)
      continue;

    float fraction = (float)k / (float)(METAL_SWORD_WISP_COUNT - 1);

    // SỬA Ở ĐÂY: Rải điểm neo an toàn lọt thỏm trong thân kiếm.
    // fraction chạy từ 0 -> 1.
    // offsetMultiplier sẽ chạy từ +0.45f (gần mũi kiếm) lùi dần về -0.45f (gần
    // chuôi).
    float offsetMultiplier = 0.45f - (fraction * 0.90f);

    float lengthOffset = sword->length * offsetMultiplier;
    Vector3 anchorPoint =
        Vector3Add(sword->position, Vector3Scale(dir, lengthOffset));

    UpdateFollowerPosition(followerId, anchorPoint);
    SetFollowerAxis(followerId, sword->position, dir);
  }
}

void InitMetalSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  swordSprite = LoadTexture("skills/metal/metal_projectile/sword.png");
  swordShader = LoadShader(0, "skills/metal/metal_projectile/metal.fs");
  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    for (int k = 0; k < METAL_SWORD_WISP_COUNT; k++) {
      swordFollowers[i][k] = -1;
    }
  }

  // Tia lửa khi kiếm bể: Perlin + drag 1.5
  ForceField_Clear(&s_metalSparkField);
  ForceField_AddLayer(&s_metalSparkField,
                      (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                   .strength = 60.0f,
                                   .noiseScale = 0.020f,
                                   .noiseSpeed = 2.0f});
  ForceField_AddLayer(&s_metalSparkField,
                      (ForceLayer){.type = FORCE_DRAG, .strength = 1.5f});

  // Tia lửa khi cast kiếm: giống spark bể nhưng cản ít hơn (drag 1.0)
  ForceField_Clear(&s_metalCastSparkField);
  ForceField_AddLayer(&s_metalCastSparkField,
                      (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                   .strength = 60.0f,
                                   .noiseScale = 0.020f,
                                   .noiseSpeed = 2.0f});
  ForceField_AddLayer(&s_metalCastSparkField,
                      (ForceLayer){.type = FORCE_DRAG, .strength = 1.0f});

  // Mảnh vỡ kiếm rơi xuống
  ForceField_Clear(&s_metalShardField);
  ForceField_AddLayer(&s_metalShardField,
                      (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                   .direction = {0, -1, 0},
                                   .strength = 800.0f});

  // 1. VISCOSITY (Ma sát/Độ nhớt) - CHÌA KHÓA LÀM MƯỢT
  // Kìm hãm vận tốc văng, ép dải lụa phải đi theo đúng biên độ của Force Field
  // ForceField_AddLayer(&s_metalWispField,
  //                     (ForceLayer){.type = FORCE_VISCOSITY, .strength =
  //                     0.0f});
  ForceField_Clear(&s_metalWispField);

  // Ma sát cực nhỏ để hạt giữ quán tính xoay mượt, không bị khựng giật cục
  // ForceField_AddLayer(&s_metalWispField,
  //                     (ForceLayer){.type = FORCE_VISCOSITY, .strength
  //                     = 1.5f});

  // LI TÂM: Đẩy hạt xòe rộng ra tối đa 60 units, nhưng giảm cực nhanh theo bình
  // phương (falloff = 2.0)
  ForceField_AddLayer(&s_metalWispField, (ForceLayer){.type = FORCE_RADIAL_AXIS,
                                                      .strength = -400.0f,
                                                      .radius = 60.0f,
                                                      .falloff = 1.0f});

  // XOÁY: Giữ ở mức vừa phải để cuốn hạt bo tròn theo thành ống rộng 60 units
  // đó
  // ForceField_AddLayer(&s_metalWispField, (ForceLayer){.type =
  // FORCE_VORTEX_AXIS,
  //                                                     .strength = 100.0f,
  //                                                     .radius = 60.0f,
  //                                                     .falloff = 0.0f});

  // 4. NHIỄU (Curl Noise) - Phá vỡ sự hoàn hảo, làm rung rinh
  ForceField_AddLayer(&s_metalWispField, (ForceLayer){.type = FORCE_NOISE_CURL,
                                                      .strength = 100.0f,
                                                      .noiseScale = 0.05f,
                                                      .noiseSpeed = 100.0f});
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
    em->spawnDelay[j] = (float)j * 0.15f;

    float offsetFactor = (float)j - (float)(count - 1) / 2.0f;
    em->spawnPositions[j] = Vector3Add(
        startPos, Vector3Scale(perp, offsetFactor * 40.0f * sizeScale));

    float distFromCenter = fabsf(offsetFactor);
    if (distFromCenter < 0.1f)
      em->projectileSizes[j] = 0.80f * sizeScale;
    else if (distFromCenter < 1.1f)
      em->projectileSizes[j] = 0.56f * sizeScale;
    else
      em->projectileSizes[j] = 0.42f * sizeScale;
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
    p.colorStart = (Color){255, 230, 100, 255};
    p.colorEnd = (Color){255, 180, 0, 0};
    p.forceField = &s_metalCastSparkField;
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

        if (emitters[e].timer >= emitters[e].spawnDelay[j]) {
          emitters[e].spawned[j] = true;
          Vector3 spawnPos = emitters[e].spawnPositions[j];
          float sizeFactor = emitters[e].projectileSizes[j];

          Vector3 baseDir = Vector3Normalize(
              Vector3Subtract(emitters[e].targetPos, spawnPos));
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

          int swordId = SpawnTrailEntity((TrailConfig){
              .type = TRAIL_TYPE_PROJECTILE,
              .pos = spawnPos,
              .vel = vel,
              .len = (float)GetRandomValue(100, 100) * sizeFactor,
              .thick = (float)GetRandomValue(12, 16) * sizeFactor,
              .trailLength = (float)GetRandomValue(10, 15) *
                             sizeFactor, // Độ dài trail của thanh kiếm chính
              .life = 2.0f,
              .target = emitters[e].targetPos,
              .initialAngle = 0.0f,
              .wobblePhase = (float)GetRandomValue(0, 100) * 0.1f,
              .scale = sizeFactor,
              .tex = swordSprite,
              .shader = swordShader,
              .tint = (Color){255, 45, 45, 255},
              .onUpdate = SwordUpdateCallback,
              .onDeath = SwordDeathCallback,
              .ownerTag = METAL_SKILL_TAG});

          if (swordId >= 0 && swordId < MAX_TRAIL_PARTICLES) {
            for (int k = 0; k < METAL_SWORD_WISP_COUNT; k++) {
              swordFollowers[swordId][k] = SpawnTrailEntity((TrailConfig){
                  .type = TRAIL_TYPE_FOLLOWER,
                  .pos = spawnPos,
                  .thick = (float)GetRandomValue(4, 6) * sizeFactor,
                  .trailLength =
                      (float)GetRandomValue(30, 50) *
                      sizeFactor, // Độ dài trail của dải lụa khí chất
                  .scale = sizeFactor,
                  .life = 99.0f,
                  .tint = (Color){255, 60, 45, 200},
                  .forceField = &s_metalWispField,
                  .ownerTag = METAL_SKILL_TAG});
            }
          }
        }
      }
    }
    if (allSpawned)
      emitters[e].active = false;
  }
}

void DrawMetalSkill(void) {}

void UnloadMetalSkill(void) {
  UnloadTexture(swordSprite);
  UnloadShader(swordShader);
}

int GetMetalSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    TrailEntity *t = GetTrail(i);
    if (t && t->active && t->type == TRAIL_TYPE_PROJECTILE &&
        t->ownerTag == METAL_SKILL_TAG && count < maxProjectiles) {
      outProjectiles[count].position = t->position;
      outProjectiles[count].radius = 9.0f * t->scale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateMetalProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    TrailEntity *t = GetTrail(i);
    if (t && t->active && t->type == TRAIL_TYPE_PROJECTILE &&
        t->ownerTag == METAL_SKILL_TAG) {
      if (count == index) {
        KillTrail(i);
        break;
      }
      count++;
    }
  }
}
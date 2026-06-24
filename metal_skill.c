#include "metal_skill.h"
#include "force_field.h"
#include "particle_system.h"
#include "raymath.h"
#include "skill_manager.h"
#include "trail_system.h"
#include <math.h>
#include <stddef.h> // Thêm dòng này vào

#define MAX_EMITTERS 10
#define METAL_SKILL_TAG 1 // ID độc nhất cho hệ kim

// --- Force Fields của Metal Skill ---
static ForceField s_metalSparkField; // spark trinh hiện: Perlin kim loại bung tỏa
static ForceField s_metalShardField; // mảnh kiếm vỡ: trọng lực mạnh rơi nặng

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

static MetalEmitter emitters[MAX_EMITTERS];
static Texture2D swordSprite;

// =========================================================================
// CALLBACKS (Được gọi tự động bởi TrailSystem)
// =========================================================================

static void SwordDeathCallback(Vector3 pos, float scale) {
  // Kiếm nổ -> tạo Shard (mảnh vỡ)
  for (int s = 0; s < GetRandomValue(10, 16); s++) {
    float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
    float pAngle = (float)GetRandomValue(15, 75) * DEG2RAD;
    float sp = (float)GetRandomValue(280, 650) * scale;
    ParticleConfig shard = {0};
    shard.position = pos;
    shard.velocity =
        (Vector3){cosf(sAngle) * sp * cosf(pAngle), sinf(pAngle) * sp + 100.0f,
                  sinf(sAngle) * sp * cosf(pAngle)};
    shard.force = (Vector3){0, -800.0f, 0};
    shard.radius = (float)GetRandomValue(8, 18) * scale;
    shard.lifetime = 0.60f;
    shard.colorStart = (Color){255, 230, 100, 255};
    shard.colorEnd = (Color){255, 180, 0, 0};
    shard.physicsFlags = P_PHYSICS_FORCE;
    shard.forceField = &s_metalShardField;
    SpawnParticle(shard);
  }

  // Kiếm nổ -> tạo Spark (tia lửa)
  for (int s = 0; s < GetRandomValue(14, 22); s++) {
    float sAngle = (float)GetRandomValue(0, 360) * DEG2RAD;
    float pAngle = (float)GetRandomValue(-45, 45) * DEG2RAD;
    float sp = (float)GetRandomValue(400, 850) * scale;
    ParticleConfig spark = {0};
    spark.position = pos;
    spark.velocity =
        (Vector3){cosf(sAngle) * sp * cosf(pAngle), sinf(pAngle) * sp,
                  sinf(sAngle) * sp * cosf(pAngle)};
    spark.drag = 1.5f;
    spark.radius = (float)GetRandomValue(6, 16);
    spark.lifetime = 0.35f;
    spark.colorStart = (Color){255, 255, 200, 255};
    spark.colorEnd = (Color){255, 150, 0, 0};
    spark.physicsFlags = P_PHYSICS_DRAG;
    spark.forceField = &s_metalSparkField;
    SpawnParticle(spark);
  }

  // Kiếm nổ -> tạo Shockwave
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
  TrailEntity *sword = GetTrail(trailId);
  if (!sword)
    return;

  Vector3 dir = Vector3Normalize(sword->velocity);
  int wispCount = GetRandomValue(1, 2);

  for (int w = 0; w < wispCount; w++) {
    float randOffset = (float)GetRandomValue(-100, 100) / 100.0f;
    Vector3 pointOnBlade = Vector3Add(
        sword->position, Vector3Scale(dir, sword->length * 0.45f * randOffset));

    Vector3 upVec = (fabsf(dir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                           : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 rightVec = Vector3Normalize(Vector3CrossProduct(dir, upVec));
    Vector3 realUp = Vector3Normalize(Vector3CrossProduct(rightVec, dir));

    float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
    Vector3 perpOut = Vector3Add(Vector3Scale(rightVec, cosf(angle)),
                                 Vector3Scale(realUp, sinf(angle)));
    Vector3 strandDir = Vector3Normalize(
        Vector3Add(Vector3Scale(perpOut, 0.15f), Vector3Scale(dir, -1.0f)));

    Vector3 slideVel =
        Vector3Add(Vector3Scale(perpOut, (float)GetRandomValue(10, 30)),
                   Vector3Scale(dir, (float)GetRandomValue(-120, -50)));
    Vector3 wispVel = Vector3Add(sword->velocity, slideVel);

    float wispLen = (float)GetRandomValue(40, 90) * sword->scale;
    float wispThick = (float)GetRandomValue(4, 12) * sword->scale;
    float wispLife = (float)GetRandomValue(15, 30) * 0.01f;
    float wispPhase = (float)GetRandomValue(0, 314) * 0.01f;

    // Sinh dải khí Wisp bay kèm thanh kiếm. Gán Callback NULL vì Wisp không cần
    // xử lý thêm logic riêng.
    SpawnTrailEntity(TRAIL_TYPE_WISP, pointOnBlade, wispVel, wispLen, wispThick,
                     wispLife, strandDir, 0.0f, wispPhase, sword->scale,
                     (Texture2D){0}, (Color){255, 45, 45, 255}, NULL, NULL,
                     METAL_SKILL_TAG);
  }
}

// =========================================================================
// MAIN SKILL LOGIC
// =========================================================================

void InitMetalSkill(int screenWidth, int screenHeight) {
  swordSprite = LoadTexture("sword.png");
  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  // Spark trinh hiện: Perlin noise + không trọng lực = bắn tía ra bốn phía
  ForceField_Clear(&s_metalSparkField);
  ForceField_AddLayer(&s_metalSparkField, (ForceLayer){
    .type = FORCE_NOISE_PERLIN, .strength = 60.0f,
    .noiseScale = 0.020f, .noiseSpeed = 2.0f
  });

  // Mảnh kiếm vỡ: trọng lực kim loại nặng nề
  ForceField_Clear(&s_metalShardField);
  ForceField_AddLayer(&s_metalShardField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 800.0f
  });
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
    SpawnParticle(p); // Gọi hệ thống Hạt Vô Tri (Compute Shader)
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

          SpawnTrailEntity(TRAIL_TYPE_PORTAL, portalPos, (Vector3){0, 0, 0},
                           45.0f * sizeFactor, 0.0f, portalLife,
                           (Vector3){0, 0, 0}, 0.0f, 0.0f, sizeFactor,
                           (Texture2D){0}, (Color){128, 128, 220, 255}, NULL,
                           NULL, METAL_SKILL_TAG);
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
            SpawnParticle(p); // Spark từ cổng
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

          // Gọi hệ thống Thực thể Thông Minh sinh Kiếm. Gắn Callback Update và
          // Callback Death vào.
          SpawnTrailEntity(
              TRAIL_TYPE_PROJECTILE, portalPos, vel,
              (float)GetRandomValue(100, 140) * sizeFactor,
              (float)GetRandomValue(12, 16) * sizeFactor, 2.0f,
              emitters[e].targetPos, 0.0f, (float)GetRandomValue(0, 100) * 0.1f,
              sizeFactor, swordSprite, (Color){255, 45, 45, 255},
              SwordUpdateCallback, SwordDeathCallback, METAL_SKILL_TAG);
        }
      }
    }
    if (allSpawned)
      emitters[e].active = false;
  }
}

void DrawMetalSkill(void) {
  // Trống. Bạn nên gọi `DrawTrailEntities(camera)` một lần duy nhất trong hàm
  // Render chính của game loop, bên cạnh `DrawParticles(...)`.
}

void UnloadMetalSkill(void) { UnloadTexture(swordSprite); }

int GetMetalSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles) {
  int count = 0;
  // Tìm trong Global Trail Pool những hạt là Kiếm (PROJECTILE) do MetalSkill
  // (Tag 1) tạo ra
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
        KillTrail(i); // Tiêu diệt thông qua API
        break;
      }
      count++;
    }
  }
}
#include <string.h>

#include "particle_system.h"
#include "path_spline.h"
#include "raymath.h"
#include "ribbon_strip.h"
#include "rlgl.h"
#include "skill_manager.h"
#include "wood_skill.h"
#include "force_field.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_EMITTERS 25
#define EMITTER_PATH_MAX 300
#define MAX_SAMPLED_SEGMENTS 256

#define WOOD_TRAVEL_SPEED 0.95f
#define WOOD_PROGRESS_MAX 2.4f

static ForceField s_woodLeafField;
static ForceField s_woodShardField;

typedef struct {
  bool active;
  bool isSubBranch;
  bool hasBranched;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 contactPos;
  Vector3 p1, p2;
  float progress;
  float speedMultiplier;
  int branchCount;
  float sproutAccumulator;
  float sizeScale;

  Vector3 path[EMITTER_PATH_MAX];
  int pathCount;
  int pathHead;
  Vector3 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
} WoodEmitter;

// CẤU TRÚC ĐÓNG GIẢ: Mô phỏng chính xác 100% memory layout của SkillProjectile
// Giúp file Mộc độc lập, tự ép kiểu mà không cần xin file hệ Lôi
typedef struct {
  Vector3 position;
  float radius;
  bool active;
} WoodProjectileProxy;

static WoodEmitter emitters[MAX_EMITTERS];
static RibbonPoint ribbonBuffer[MAX_SAMPLED_SEGMENTS];

static RenderTexture2D canvasTexture;
static Shader woodShader;
static int timeLoc;
static int glowPassLoc;

extern Camera3D camera;

static float Math_Mix(float x, float y, float a) {
  return x * (1.0f - a) + y * a;
}
static float smoothstep(float edge0, float edge1, float x) {
  float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
static float Random01(void) {
  return (float)GetRandomValue(0, 10000) / 10000.0f;
}

static Vector3 GetWoodPointAt(const WoodEmitter *em, float t, int branchIndex) {
  if (t <= 1.0f) {
    Vector3 pos =
        GetBezierPoint(em->startPos, em->p1, em->p2, em->contactPos, t);

    Vector3 dir =
        Vector3Normalize(Vector3Subtract(em->targetPos, em->startPos));

    Vector3 perp = (Vector3){-dir.z, 0, dir.x};

    float wiggle = sinf(t * 18.0f) + sinf(t * 31.0f) * 0.4f;

    pos = Vector3Add(pos, Vector3Scale(perp, wiggle * 6.0f));

    return pos;
  } else {
    float t_spiral = t - 1.0f;
    float ratio = t_spiral / 0.8f;
    if (ratio > 1.0f)
      ratio = 1.0f;

    float contactAngle = atan2f(em->contactPos.z - em->targetPos.z,
                                em->contactPos.x - em->targetPos.x);
    float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;
    float theta = ratio * 2.2f * (2.0f * PI) * coilDir +
                  sinf(ratio * 8.0f) * 0.8f + sinf(ratio * 19.0f) * 0.25f;

    theta += sinf(ratio * 15.0f) * 0.25f;

    float phiOffset = 0.0f;
    if (em->branchCount > 1) {
      phiOffset = ((float)branchIndex / (float)em->branchCount) * PI;
    }
    float phi = contactAngle + phiOffset;

    float r_a = 45.0f * em->sizeScale;
    float r_b = 45.0f * em->sizeScale;

    float wobbleScale = ratio > 0.1f ? 1.0f : ratio / 0.1f;
    float wobble =
        (sinf(theta * 6.0f) * 4.0f + cosf(theta * 11.0f) * 1.5f) * wobbleScale;

    float tighten = 1.0f - ratio * 0.45f;
    float curr_ra = (r_a + wobble) * tighten;
    float curr_rb = (r_b + wobble) * tighten;
    float wrapFactor = 0.75f + 0.25f * sinf(ratio * 10.0f);

    curr_ra *= wrapFactor;
    curr_rb *= wrapFactor;

    float radiusNoise = 1.0f + sinf(theta * 3.0f) * 0.08f;

    curr_ra *= radiusNoise;
    curr_rb *= radiusNoise;

    float height = 80.0f * em->sizeScale;

    float y = em->targetPos.y - height * 0.5f + ratio * height;

    return (Vector3){em->targetPos.x + curr_ra * cosf(theta) * cosf(phi) -
                         curr_rb * sinf(theta) * sinf(phi),
                     y + wobble * 0.1f,
                     em->targetPos.z + curr_ra * cosf(theta) * sinf(phi) +
                         curr_rb * sinf(theta) * cosf(phi)};
  }
}

void UpdateWoodSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float prevProgress = emitters[e].progress;
    float travelRate = WOOD_TRAVEL_SPEED;
    if (emitters[e].progress < 1.0f) {
      travelRate *=
          Math_Mix(1.4f, 0.7f, smoothstep(0.0f, 1.0f, emitters[e].progress));
    } else if (emitters[e].progress < 1.8f) {
      travelRate *= 1.15f;
    }
    emitters[e].progress += dt * travelRate * emitters[e].speedMultiplier;

    if (emitters[e].progress >= WOOD_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    Vector3 headPos =
        GetWoodPointAt(&emitters[e], fminf(emitters[e].progress, 1.8f), 0);
    if (emitters[e].pathCount == 0) {
      emitters[e].path[0] = headPos;
      emitters[e].pathCount = 1;
      emitters[e].pathHead = 0;
    } else {
      float dist =
          Vector3Distance(headPos, emitters[e].path[emitters[e].pathHead]);
      if (dist > 1.2f) {
        emitters[e].pathHead =
            (emitters[e].pathHead - 1 + EMITTER_PATH_MAX) % EMITTER_PATH_MAX;
        emitters[e].path[emitters[e].pathHead] = headPos;
        if (emitters[e].pathCount < EMITTER_PATH_MAX)
          emitters[e].pathCount++;
      }
    }

    if (emitters[e].progress <= 1.8f) {
      emitters[e].sproutAccumulator += dt;
      if (emitters[e].sproutAccumulator >= 0.05f) {
        emitters[e].sproutAccumulator = 0.0f;

        ParticleConfig cfgLeaf = {0};

        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);

        cfgLeaf.position = emitters[e].sampledPath[idx];

        cfgLeaf.velocity = (Vector3){(Random01() - 0.5f) * 40.0f,
                                     Math_Mix(20.0f, 60.0f, Random01()),
                                     (Random01() - 0.5f) * 40.0f};
        cfgLeaf.drag = 1.5f;
        cfgLeaf.forceField = &s_woodLeafField;
        cfgLeaf.radius =
            Math_Mix(2.5f, 5.0f, Random01()) * emitters[e].sizeScale;
        cfgLeaf.lifetime = Math_Mix(1.0f, 1.8f, Random01());
        cfgLeaf.colorStart = (Color){60, 180, 80, 255};
        cfgLeaf.colorEnd = (Color){20, 60, 30, 0};
        cfgLeaf.physicsFlags = P_PHYSICS_DRAG;
        SpawnParticle(cfgLeaf);

        ParticleConfig cfgPollen = {0};
        cfgPollen.position = headPos;
        cfgPollen.velocity = (Vector3){(Random01() - 0.5f) * 20.0f,
                                       Math_Mix(40.0f, 90.0f, Random01()),
                                       (Random01() - 0.5f) * 20.0f};
        cfgPollen.drag = 0.8f;
        cfgPollen.radius =
            Math_Mix(1.0f, 2.0f, Random01()) * emitters[e].sizeScale;
        cfgPollen.lifetime = Math_Mix(0.8f, 1.5f, Random01());
        cfgPollen.colorStart = (Color){220, 255, 120, 200};
        cfgPollen.colorEnd = (Color){100, 180, 40, 0};
        cfgPollen.physicsFlags = P_PHYSICS_DRAG;
        SpawnParticle(cfgPollen);
      }
    }

    if (prevProgress <= 1.0f && emitters[e].progress > 1.0f) {
      int shardCount = GetRandomValue(12, 20) * emitters[e].sizeScale;
      for (int s = 0; s < shardCount; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed =
            Math_Mix(150.0f, 380.0f, Random01()) * emitters[e].sizeScale;

        ParticleConfig cfgShard = {0};
        cfgShard.position = emitters[e].contactPos;
        cfgShard.velocity = (Vector3){cosf(angle) * speed,
                                      Math_Mix(100.0f, 300.0f, Random01()) *
                                          emitters[e].sizeScale,
                                      sinf(angle) * speed};
        cfgShard.drag = 0.5f;
        cfgShard.forceField = &s_woodShardField;
        cfgShard.radius =
            Math_Mix(2.0f, 5.0f, Random01()) * emitters[e].sizeScale;
        cfgShard.lifetime = Math_Mix(0.6f, 1.1f, Random01());
        cfgShard.colorStart = (Color){140, 90, 50, 255};
        cfgShard.colorEnd = (Color){60, 40, 20, 0};
        cfgShard.physicsFlags = P_PHYSICS_DRAG;
        SpawnParticle(cfgShard);
      }
    }
  }
}

void DrawWoodSkill(void) {
  bool anyActive = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      anyActive = true;
      break;
    }
  }
  if (!anyActive)
    return;

  float time = (float)GetTime();
  rlDisableDepthMask();

  float glowOff = 0.0f;
  SetShaderValue(woodShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(woodShader, glowPassLoc, &glowOff, SHADER_UNIFORM_FLOAT);

  BeginShaderMode(woodShader);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].pathCount < 2)
      continue;

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

    emitters[e].sampledCount = SamplePath(
        linearPath, count, 3.5f, emitters[e].sampledPath, MAX_SAMPLED_SEGMENTS);

    for (int i = 0; i < emitters[e].sampledCount; i++) {
      float normDist = (float)i / (float)(emitters[e].sampledCount - 1);
      float taper = powf(1.0f - normDist, 0.6f);

      ribbonBuffer[i].position = emitters[e].sampledPath[i];
      ribbonBuffer[i].halfWidth = 6.0f * emitters[e].sizeScale;
      ribbonBuffer[i].v = normDist;

      ribbonBuffer[i].tint =
          (Color){(unsigned char)(normDist * 255.0f), 128, 45,
                  (unsigned char)(255.0f *
                                  (1.0f - (emitters[e].progress > 1.8f
                                               ? (emitters[e].progress - 1.8f) /
                                                     (WOOD_PROGRESS_MAX - 1.8f)
                                               : 0.0f)))};
    }

    int visibleCount = (int)((float)emitters[e].sampledCount *
                             Clamp(emitters[e].progress, 0.0f, 1.0f));

    if (visibleCount < 2)
      visibleCount = 2;

    DrawRibbonStrip(ribbonBuffer, visibleCount, (Texture2D){0}, camera);
  }
  EndShaderMode();

  float glowOn = 1.0f;
  SetShaderValue(woodShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(woodShader, glowPassLoc, &glowOn, SHADER_UNIFORM_FLOAT);
  BeginBlendMode(BLEND_ADDITIVE);
  BeginShaderMode(woodShader);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].sampledCount < 2)
      continue;
    DrawRibbonStrip(ribbonBuffer, emitters[e].sampledCount, (Texture2D){0},
                    camera);
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void InitWoodSkill(int screenWidth, int screenHeight) {
  canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
  woodShader = LoadShader(0, "wood.fs");
  timeLoc = GetShaderLocation(woodShader, "u_time");
  glowPassLoc = GetShaderLocation(woodShader, "u_glowPass");

  for (int i = 0; i < MAX_EMITTERS; i++) {
    emitters[i].active = false;
    emitters[i].pathCount = 0;
  }

  // Lá rơi: curl noise nhẹ = tạo cảm giác lá bay lướt không phải rơi thẳng
  ForceField_Clear(&s_woodLeafField);
  ForceField_AddLayer(&s_woodLeafField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = 25.0f,
    .noiseScale = 0.012f, .noiseSpeed = 0.4f
  });
  ForceField_AddLayer(&s_woodLeafField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 65.0f
  });

  // Mảnh gỗ vỡ: trọng lực mạnh, không nhiễu — rơi nặng nề
  ForceField_Clear(&s_woodShardField);
  ForceField_AddLayer(&s_woodShardField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 750.0f
  });
}

void CastWoodSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  int branchCount = params.quantity;
  float sizeScale = params.sizeScale;

  if (branchCount > 5)
    branchCount = 5;
  if (branchCount < 1)
    branchCount = 1;

  int emitterIndex = -1;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitterIndex = i;
      break;
    }
  }
  if (emitterIndex == -1)
    return;

  WoodEmitter *em = &emitters[emitterIndex];
  em->active = true;
  em->isSubBranch = false;
  em->hasBranched = false;
  em->startPos = startPos;
  em->targetPos = target;
  em->progress = 0.0f;
  em->speedMultiplier = 1.0f;
  em->branchCount = branchCount;
  em->sproutAccumulator = 0.0f;
  em->sizeScale = sizeScale;
  em->pathCount = 0;

  float curveSign = (Random01() > 0.5f) ? 1.0f : -1.0f;
  float dist = Vector3Distance(startPos, target);
  Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
  Vector3 perp = (Vector3){-dir.z, 0.0f, dir.x};
  if (Vector3Length(perp) == 0.0f)
    perp = (Vector3){0, 0, 1};
  perp = Vector3Normalize(perp);

  em->p1 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f)),
                      Vector3Scale(perp, curveSign * dist * 0.25f));
  em->p2 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f)),
                      Vector3Scale(perp, -curveSign * dist * 0.15f));

  float wrapRadius = 28.0f * sizeScale;
  Vector3 dirToTarget = Vector3Normalize(Vector3Subtract(target, em->p2));
  em->contactPos =
      Vector3Subtract(target, Vector3Scale(dirToTarget, wrapRadius));

  ParticleConfig cfgBurst1 = {0};
  cfgBurst1.position = startPos;
  cfgBurst1.radius = 35.0f * sizeScale;
  cfgBurst1.lifetime = 0.5f;
  cfgBurst1.colorStart = (Color){60, 255, 100, 255};
  cfgBurst1.colorEnd = (Color){20, 100, 30, 0};
  SpawnParticle(cfgBurst1);

  ParticleConfig cfgBurst2 = {0};
  cfgBurst2.position = startPos;
  cfgBurst2.radius = 60.0f * sizeScale;
  cfgBurst2.lifetime = 0.35f;
  cfgBurst2.colorStart = (Color){100, 255, 150, 200};
  cfgBurst2.colorEnd = (Color){30, 120, 40, 0};
  SpawnParticle(cfgBurst2);

  int burstCount = 15 * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    float angle =
        ((float)s / (float)burstCount) * PI * 2.0f + Random01() * 0.3f;
    float speed = Math_Mix(140.0f, 280.0f, Random01()) * sizeScale;

    ParticleConfig cfgLeaf = {0};
    cfgLeaf.position = startPos;
    cfgLeaf.velocity = (Vector3){
        cosf(angle) * speed, Math_Mix(-50.0f, 150.0f, Random01()) * sizeScale,
        sinf(angle) * speed};
    cfgLeaf.radius = Math_Mix(5.0f, 10.0f, Random01()) * sizeScale;
    cfgLeaf.lifetime = Math_Mix(0.8f, 1.4f, Random01());
    cfgLeaf.colorStart = (Color){60, 180, 80, 255};
    cfgLeaf.colorEnd = (Color){20, 60, 30, 0};
    SpawnParticle(cfgLeaf);
  }
}

void UnloadWoodSkill(void) {
  UnloadShader(woodShader);
  UnloadRenderTexture(canvasTexture);
}

bool IsWoodSkillCoiling(void) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (emitters[e].active && emitters[e].progress >= 1.0f &&
        emitters[e].progress <= 1.8f) {
      return true;
    }
  }
  return false;
}

// KHÔNG CẦN ÉP KIỂU, KHÔNG CẦN CON TRỎ VOID* NỮA
int GetWoodSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && emitters[i].progress < 1.0f &&
        count < maxProjectiles) {
      Vector3 headPos = GetWoodPointAt(&emitters[i], emitters[i].progress, 0);
      outProjectiles[count].position = headPos;
      outProjectiles[count].radius = 18.0f * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateWoodProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && emitters[i].progress < 1.0f) {
      if (count == index) {
        emitters[i].progress = 1.0f;
        Vector3 impactPos = GetWoodPointAt(&emitters[i], 1.0f, 0);

        int pCount = 15 * emitters[i].sizeScale;
        for (int p = 0; p < pCount; p++) {
          float angle = Random01() * PI * 2.0f;
          float speed =
              Math_Mix(100.0f, 300.0f, Random01()) * emitters[i].sizeScale;
          float pitch = (Random01() - 0.5f) * PI;

          ParticleConfig cfgShard = {0};
          cfgShard.position = impactPos;
          cfgShard.velocity =
              (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                        sinf(angle) * speed * cosf(pitch)};
          cfgShard.drag = 0.5f;
          cfgShard.forceField = &s_woodShardField;
          cfgShard.radius =
              Math_Mix(5.0f, 12.0f, Random01()) * emitters[i].sizeScale;
          cfgShard.lifetime = Math_Mix(0.4f, 0.8f, Random01());
          cfgShard.colorStart = (Color){140, 90, 50, 255};
          cfgShard.colorEnd = (Color){60, 40, 20, 0};
          cfgShard.physicsFlags = P_PHYSICS_DRAG;
          SpawnParticle(cfgShard);
        }
        break;
      }
      count++;
    }
  }
}
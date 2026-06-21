#include "wood_skill.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_WOOD_PARTICLES 4000
#define MAX_EMITTERS 25
#define EMITTER_PATH_MAX 300
#define PARTICLE_HISTORY_COUNT 6

// Speed and progress settings
#define WOOD_TRAVEL_SPEED 0.95f
#define WOOD_PROGRESS_MAX                                                      \
  2.4f // Allow roots to persist on screen after hitting target

// Emitter Structure
typedef struct {
  bool active;
  bool isSubBranch;
  bool hasBranched;
  Vector2 startPos;
  Vector2 targetPos;
  Vector2 contactPos; // Vị trí tiếp xúc rìa Enemy
  Vector2 p1, p2;
  float progress;
  float speedMultiplier;
  int branchCount;
  float sproutAccumulator;
  float sizeScale; // Hệ số thu phóng rễ cây và lá mộc
} WoodEmitter;

// Particle Types
typedef enum {
  PARTICLE_LEAF = 0,        // Green leaves drifting/spinning
  PARTICLE_POLLEN = 1,      // Glowing blue/yellow magical dust
  PARTICLE_WOOD_SHARD = 2,  // Wooden debris
  PARTICLE_FLOWER = 3,      // Blooming flower at target
  PARTICLE_ENERGY_FLASH = 4 // Caster burst ring
} WoodParticleType;

// Particle Structure
typedef struct {
  bool active;
  int type;
  Vector2 position;
  Vector2 velocity;
  float radius;
  float lifetime;
  float maxLifetime;
  float angle;
  float spinSpeed;
  float swayPhase;
  Vector2
      history[PARTICLE_HISTORY_COUNT]; // Vệt vị trí gần nhất để vẽ motion trail
  int historyFilled; // Số ô history đã có dữ liệu hợp lệ
} WoodParticle;

// Pool data
static WoodParticle woodPool[MAX_WOOD_PARTICLES];
static WoodEmitter emitters[MAX_EMITTERS];
static int lastUsedParticle = 0;

static RenderTexture2D canvasTexture;
static Shader woodShader;
static int timeLoc;
static int glowPassLoc; // Uniform bật/tắt pass tách glow để giả lập bloom

// Math helpers
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

// Vector2 rotation helper
static Vector2 RotatePoint(Vector2 pt, float angle) {
  float s = sinf(angle);
  float c = cosf(angle);
  return (Vector2){pt.x * c - pt.y * s, pt.x * s + pt.y * c};
}

// Bezier Curve
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

static Vector2 GetWoodPointAt(const WoodEmitter *em, float t, int branchIndex) {
  if (t <= 1.0f) {
    return GetBezierPoint(em->startPos, em->p1, em->p2, em->contactPos, t);
  } else {
    float t_spiral = t - 1.0f;
    float ratio = t_spiral / 0.8f;
    if (ratio > 1.0f)
      ratio = 1.0f;

    float contactAngle = atan2f(em->contactPos.y - em->targetPos.y,
                                em->contactPos.x - em->targetPos.x);

    float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;
    float theta = ratio * 3.0f * (2.0f * PI) * coilDir;

    float phiOffset = 0.0f;
    if (em->branchCount > 1) {
      phiOffset = ((float)branchIndex / (float)em->branchCount) * PI;
    }
    float phi = contactAngle + phiOffset;

    // Nhân theo sizeScale để vòng quấn khớp với kích thước rìa va chạm
    // (contactPos) đã tính theo sizeScale ở CastWoodSkill - tránh quấn lọt
    // thỏm vào quái to hoặc lơ lửng cách xa quái nhỏ
    float r_a = 28.0f * em->sizeScale;
    float r_b = 13.0f * em->sizeScale;

    float wobbleScale = ratio > 0.1f ? 1.0f : ratio / 0.1f;
    float wobble =
        (sinf(theta * 6.0f) * 4.0f + cosf(theta * 11.0f) * 1.5f) * wobbleScale;

    float curr_ra = r_a + wobble;
    float curr_rb = r_b + wobble;

    float x_local = curr_ra * cosf(theta);
    float y_local = curr_rb * sinf(theta);

    return (Vector2){
        em->targetPos.x + x_local * cosf(phi) - y_local * sinf(phi),
        em->targetPos.y + x_local * sinf(phi) + y_local * cosf(phi)};
  }
}

static void DrawProceduralLeafUV(Vector2 position, float radius, float angle,
                                 float fade) {
  Vector2 left = RotatePoint((Vector2){-radius * 0.5f, 0}, angle);
  Vector2 right = RotatePoint((Vector2){radius * 0.5f, 0}, angle);
  Vector2 head = RotatePoint((Vector2){0, radius}, angle);
  Vector2 tail = RotatePoint((Vector2){0, -radius * 0.8f}, angle);

  left = Vector2Add(left, position);
  right = Vector2Add(right, position);
  head = Vector2Add(head, position);
  tail = Vector2Add(tail, position);

  unsigned char aVal = (unsigned char)(fade * 255.0f);

  // V0 = tail (base): U=0, V=128, B=128
  Color cTail = (Color){0, 128, 128, aVal};
  // V1 = left:        U=128, V=0, B=128
  Color cLeft = (Color){128, 0, 128, aVal};
  // V2 = right:       U=128, V=255, B=128
  Color cRight = (Color){128, 255, 128, aVal};
  // V3 = head (tip):  U=255, V=128, B=128
  Color cHead = (Color){255, 128, 128, aVal};

  rlBegin(RL_TRIANGLES);
  // Triangle 1: tail, left, head
  rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a);
  rlVertex2f(tail.x, tail.y);
  rlColor4ub(cLeft.r, cLeft.g, cLeft.b, cLeft.a);
  rlVertex2f(left.x, left.y);
  rlColor4ub(cHead.r, cHead.g, cHead.b, cHead.a);
  rlVertex2f(head.x, head.y);

  // Triangle 2: tail, head, right
  rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a);
  rlVertex2f(tail.x, tail.y);
  rlColor4ub(cHead.r, cHead.g, cHead.b, cHead.a);
  rlVertex2f(head.x, head.y);
  rlColor4ub(cRight.r, cRight.g, cRight.b, cRight.a);
  rlVertex2f(right.x, right.y);
  rlEnd();
}

static void DrawProceduralFlowerUV(Vector2 position, float radius, float angle,
                                   float fade) {
  float c = cosf(angle);
  float s = sinf(angle);
  float hs = radius;

  Vector2 corners[4] = {
      {-hs, -hs}, // Top-Left
      {hs, -hs},  // Top-Right
      {-hs, hs},  // Bottom-Left
      {hs, hs}    // Bottom-Right
  };

  Vector2 rotated[4];
  for (int i = 0; i < 4; i++) {
    rotated[i] = (Vector2){position.x + corners[i].x * c - corners[i].y * s,
                           position.y + corners[i].x * s + corners[i].y * c};
  }

  unsigned char aVal = (unsigned char)(fade * 255.0f);
  // Blue = 255 indicates flower/magic
  Color c0 = (Color){0, 0, 255, aVal};
  Color c1 = (Color){255, 0, 255, aVal};
  Color c2 = (Color){0, 255, 255, aVal};
  Color c3 = (Color){255, 255, 255, aVal};

  rlBegin(RL_TRIANGLES);
  rlColor4ub(c0.r, c0.g, c0.b, c0.a);
  rlVertex2f(rotated[0].x, rotated[0].y);
  rlColor4ub(c1.r, c1.g, c1.b, c1.a);
  rlVertex2f(rotated[1].x, rotated[1].y);
  rlColor4ub(c3.r, c3.g, c3.b, c3.a);
  rlVertex2f(rotated[3].x, rotated[3].y);

  rlColor4ub(c0.r, c0.g, c0.b, c0.a);
  rlVertex2f(rotated[0].x, rotated[0].y);
  rlColor4ub(c3.r, c3.g, c3.b, c3.a);
  rlVertex2f(rotated[3].x, rotated[3].y);
  rlColor4ub(c2.r, c2.g, c2.b, c2.a);
  rlVertex2f(rotated[2].x, rotated[2].y);
  rlEnd();
}

static void DrawProceduralShardUV(Vector2 position, float radius, float angle,
                                  float fade) {
  Vector2 left = RotatePoint((Vector2){-radius, -radius * 0.4f}, angle);
  Vector2 right = RotatePoint((Vector2){radius, -radius * 0.4f}, angle);
  Vector2 tip = RotatePoint((Vector2){0, radius}, angle);

  left = Vector2Add(left, position);
  right = Vector2Add(right, position);
  tip = Vector2Add(tip, position);

  unsigned char aVal = (unsigned char)(fade * 255.0f);
  Color cLeft = (Color){0, 0, 0, aVal};
  Color cRight = (Color){255, 0, 0, aVal};
  Color cTip = (Color){128, 255, 0, aVal};

  rlBegin(RL_TRIANGLES);
  rlColor4ub(cLeft.r, cLeft.g, cLeft.b, cLeft.a);
  rlVertex2f(left.x, left.y);
  rlColor4ub(cRight.r, cRight.g, cRight.b, cRight.a);
  rlVertex2f(right.x, right.y);
  rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
  rlVertex2f(tip.x, tip.y);
  rlEnd();
}

static void DrawProceduralEnergyFlashUV(Vector2 position, float radius,
                                        float fade) {
  float hs = radius;
  Vector2 rotated[4] = {{position.x - hs, position.y - hs},
                        {position.x + hs, position.y - hs},
                        {position.x - hs, position.y + hs},
                        {position.x + hs, position.y + hs}};
  unsigned char aVal = (unsigned char)(fade * 255.0f);
  // Blue = 128 indicates leaf/green energy
  Color c0 = (Color){0, 0, 128, aVal};
  Color c1 = (Color){255, 0, 128, aVal};
  Color c2 = (Color){0, 255, 128, aVal};
  Color c3 = (Color){255, 255, 128, aVal};

  rlBegin(RL_TRIANGLES);
  rlColor4ub(c0.r, c0.g, c0.b, c0.a);
  rlVertex2f(rotated[0].x, rotated[0].y);
  rlColor4ub(c1.r, c1.g, c1.b, c1.a);
  rlVertex2f(rotated[1].x, rotated[1].y);
  rlColor4ub(c3.r, c3.g, c3.b, c3.a);
  rlVertex2f(rotated[3].x, rotated[3].y);

  rlColor4ub(c0.r, c0.g, c0.b, c0.a);
  rlVertex2f(rotated[0].x, rotated[0].y);
  rlColor4ub(c3.r, c3.g, c3.b, c3.a);
  rlVertex2f(rotated[3].x, rotated[3].y);
  rlColor4ub(c2.r, c2.g, c2.b, c2.a);
  rlVertex2f(rotated[2].x, rotated[2].y);
  rlEnd();
}

// Particle spawning
static void SpawnWoodParticle(int type, Vector2 pos, Vector2 vel,
                              float baseRadius, float maxLife, float angle,
                              float spinSpeed) {
  for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
    int index = (lastUsedParticle + i) % MAX_WOOD_PARTICLES;
    if (!woodPool[index].active) {
      woodPool[index].type = type;
      woodPool[index].position = pos;
      woodPool[index].velocity = vel;
      woodPool[index].radius = baseRadius;
      woodPool[index].maxLifetime = maxLife;
      woodPool[index].lifetime = maxLife;
      woodPool[index].active = true;
      woodPool[index].angle = angle;
      woodPool[index].spinSpeed = spinSpeed;
      woodPool[index].swayPhase = Random01() * 3.14159f * 2.0f;
      woodPool[index].historyFilled = 0;
      lastUsedParticle = (index + 1) % MAX_WOOD_PARTICLES;
      break;
    }
  }
}

void InitWoodSkill(int screenWidth, int screenHeight) {
  canvasTexture = LoadRenderTexture(screenWidth, screenHeight);
  woodShader = LoadShader(0, "wood.fs");
  timeLoc = GetShaderLocation(woodShader, "u_time");
  glowPassLoc = GetShaderLocation(woodShader, "u_glowPass");

  for (int i = 0; i < MAX_WOOD_PARTICLES; i++)
    woodPool[i].active = false;
  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;
}

void CastWoodSkill(Vector2 startPos, Vector2 target, int branchCount,
                   float sizeScale) {
  if (branchCount > 5)
    branchCount = 5;
  if (branchCount < 1)
    branchCount = 1;
  // Search for free emitter slot
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

  // Calculate nice Bezier control points for organic bending
  // Đảo chiều cong ngẫu nhiên mỗi lần cast để quỹ đạo dây leo không lặp lại y
  // hệt nhau
  float curveSign = (Random01() > 0.5f) ? 1.0f : -1.0f;
  float dist = Vector2Distance(startPos, target);
  Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
  Vector2 perp = (Vector2){-dir.y, dir.x};

  em->p1 = Vector2Add(Vector2Add(startPos, Vector2Scale(dir, dist * 0.35f)),
                      Vector2Scale(perp, curveSign * dist * 0.25f));
  em->p2 = Vector2Add(Vector2Add(startPos, Vector2Scale(dir, dist * 0.70f)),
                      Vector2Scale(perp, -curveSign * dist * 0.15f));

  // Calculate contactPos on target's outer boundary (radius 28.0 for initial
  // python-coil entry)
  float wrapRadius = 28.0f * sizeScale;
  Vector2 dirToTarget = Vector2Normalize(Vector2Subtract(target, em->p2));
  em->contactPos =
      Vector2Subtract(target, Vector2Scale(dirToTarget, wrapRadius));

  // Caster nature burst: 2 layered expanding shockwaves (Green/leaf energy)
  SpawnWoodParticle(PARTICLE_ENERGY_FLASH, startPos, (Vector2){0, 0},
                    35.0f * sizeScale, 0.5f, 0, 0);
  SpawnWoodParticle(PARTICLE_ENERGY_FLASH, startPos, (Vector2){0, 0},
                    60.0f * sizeScale, 0.35f, 0, 0);

  // Swirl of 15 leaves spinning out from caster
  int burstCount = 15 * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    float angle =
        ((float)s / (float)burstCount) * PI * 2.0f + Random01() * 0.3f;
    float speed = Math_Mix(140.0f, 280.0f, Random01()) * sizeScale;
    Vector2 vel = {cosf(angle) * speed, sinf(angle) * speed};
    SpawnWoodParticle(PARTICLE_LEAF, startPos, vel,
                      Math_Mix(5.0f, 10.0f, Random01()) * sizeScale,
                      Math_Mix(0.8f, 1.4f, Random01()), angle,
                      Math_Mix(-3.0f, 3.0f, Random01()));
  }

  // Swirl of 12 magic pollen particles floating up from caster
  for (int s = 0; s < 12; s++) {
    float angle = Random01() * PI * 2.0f;
    float speed = Math_Mix(40.0f, 100.0f, Random01());
    Vector2 vel = {cosf(angle) * speed, -Math_Mix(60.0f, 140.0f, Random01())};
    SpawnWoodParticle(PARTICLE_POLLEN, startPos, vel,
                      Math_Mix(2.0f, 4.0f, Random01()),
                      Math_Mix(1.0f, 2.0f, Random01()), 0, 0);
  }
}

void UpdateWoodSkill(float dt) {
  float time = GetTime();

  // 1. UPDATE EMITTERS
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float prevProgress = emitters[e].progress;

    // Đường cong tốc độ theo từng pha: vọt nhanh lúc xuất phát rồi chậm dần
    // khi sắp cắm rễ (tạo cảm giác "nhắm trúng"), rồi siết nhanh hơn một
    // chút trong lúc cuộn quanh mục tiêu cho dứt khoát hơn.
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

    // Sprout leaves and pollen as it travels (only up to 1.8f)
    if (emitters[e].progress <= 1.8f) {
      emitters[e].sproutAccumulator += dt;
      if (emitters[e].sproutAccumulator >= 0.04f) {
        emitters[e].sproutAccumulator = 0.0f;

        for (int b = 0; b < emitters[e].branchCount; b++) {
          if (emitters[e].progress <= 1.0f && b > 0)
            continue;

          Vector2 headPos =
              GetWoodPointAt(&emitters[e], emitters[e].progress, b);
          Vector2 tangent;
          float prevProg = fmaxf(0.0f, emitters[e].progress - 0.01f);
          if (emitters[e].progress > prevProg) {
            tangent = Vector2Normalize(Vector2Subtract(
                headPos, GetWoodPointAt(&emitters[e], prevProg, b)));
          } else {
            tangent = (Vector2){1.0f, 0.0f};
          }
          Vector2 perp = (Vector2){-tangent.y, tangent.x};

          // Spawn a drifting leaf particle
          float side = (Random01() > 0.5f) ? 1.0f : -1.0f;
          Vector2 leafVel =
              Vector2Scale(Vector2Add(Vector2Scale(perp, side * 80.0f),
                                      Vector2Scale(tangent, -40.0f)),
                           Random01());
          SpawnWoodParticle(
              PARTICLE_LEAF, headPos, leafVel,
              Math_Mix(2.5f, 6.0f, Random01()), // slightly smaller leaves for
                                                // performance when branching
              Math_Mix(1.0f, 1.8f, Random01()), Random01() * PI * 2.0f,
              Math_Mix(-4.0f, 4.0f, Random01()));

          // Spawn some glowing pollen dust
          Vector2 pollenVel = {(Random01() - 0.5f) * 60.0f,
                               -Random01() * 40.0f};
          SpawnWoodParticle(PARTICLE_POLLEN, headPos, pollenVel,
                            Math_Mix(1.0f, 2.3f, Random01()),
                            Math_Mix(0.8f, 1.5f, Random01()), 0, 0);
        }
      }
    }

    // 2. TRIGGER IMPACT AT 100% PROGRESS (Start of coiling)
    if (prevProgress <= 1.0f && emitters[e].progress > 1.0f) {
      Vector2 hitPos = emitters[e].targetPos;
      Vector2 contactPos = emitters[e].contactPos;

      // Spawn dynamic blooming flowers (fewer and smaller) at the contact
      // position on the enemy boundary
      SpawnWoodParticle(PARTICLE_FLOWER, contactPos, (Vector2){0, 0},
                        11.0f * emitters[e].sizeScale, 1.4f, Random01() * PI,
                        0.4f);

      // Explosion leaf burst
      int leafBurst = GetRandomValue(10, 18) * emitters[e].sizeScale;
      for (int s = 0; s < leafBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed =
            Math_Mix(80.0f, 300.0f, Random01()) * emitters[e].sizeScale;
        Vector2 vel = {cosf(angle) * speed,
                       sinf(angle) * speed - 50.0f * emitters[e].sizeScale};
        SpawnWoodParticle(
            PARTICLE_LEAF, hitPos, vel,
            Math_Mix(2.5f, 5.2f, Random01()) * emitters[e].sizeScale,
            Math_Mix(1.2f, 2.2f, Random01()), Random01() * PI * 2.0f,
            Math_Mix(-6.0f, 6.0f, Random01()));
      }

      // Explosion pollen bloom
      int pollenBurst = GetRandomValue(12, 22) * emitters[e].sizeScale;
      for (int s = 0; s < pollenBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed =
            Math_Mix(40.0f, 150.0f, Random01()) * emitters[e].sizeScale;
        Vector2 vel = {cosf(angle) * speed,
                       sinf(angle) * speed - 30.0f * emitters[e].sizeScale};
        SpawnWoodParticle(PARTICLE_POLLEN, hitPos, vel,
                          Math_Mix(1.0f, 2.5f, Random01()) *
                              emitters[e].sizeScale,
                          Math_Mix(1.0f, 2.0f, Random01()), 0, 0);
      }

      // Explode wood splinters/shards
      int shardBurst = GetRandomValue(8, 14) * emitters[e].sizeScale;
      for (int s = 0; s < shardBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed =
            Math_Mix(150.0f, 450.0f, Random01()) * emitters[e].sizeScale;
        Vector2 vel = {cosf(angle) * speed, sinf(angle) * speed};
        SpawnWoodParticle(
            PARTICLE_WOOD_SHARD, hitPos, vel,
            Math_Mix(1.6f, 4.0f, Random01()) * emitters[e].sizeScale,
            Math_Mix(0.6f, 1.0f, Random01()), Random01() * PI * 2.0f,
            Math_Mix(-8.0f, 8.0f, Random01()));
      }
    }

    // 3. TRIGGER FINAL COIL COMPLETION IMPACT AT 1.8f PROGRESS
    if (prevProgress <= 1.8f && emitters[e].progress > 1.8f) {
      // Vòng sáng "khoá chặt" mục tiêu tại tâm — chốt nhịp cho khoảnh khắc
      // coil hoàn tất, cân bằng với 2 vòng burst lúc xuất chiêu ở caster
      SpawnWoodParticle(PARTICLE_ENERGY_FLASH, emitters[e].targetPos,
                        (Vector2){0, 0}, 26.0f * emitters[e].sizeScale, 0.4f, 0,
                        0);
      SpawnWoodParticle(PARTICLE_ENERGY_FLASH, emitters[e].targetPos,
                        (Vector2){0, 0}, 45.0f * emitters[e].sizeScale, 0.3f, 0,
                        0);

      for (int b = 0; b < emitters[e].branchCount; b++) {
        Vector2 hitPos = GetWoodPointAt(&emitters[e], 1.8f, b);

        // Spawn 1 small flower at the tip of each branch (radius 9.0f for
        // visibility)
        SpawnWoodParticle(PARTICLE_FLOWER, hitPos, (Vector2){0, 0},
                          9.0f * emitters[e].sizeScale, 1.3f, Random01() * PI,
                          -0.5f);

        // Explosion pollen bloom
        int pollenBurst = GetRandomValue(5, 9) * emitters[e].sizeScale;
        for (int s = 0; s < pollenBurst; s++) {
          float angle = Random01() * PI * 2.0f;
          float speed =
              Math_Mix(30.0f, 80.0f, Random01()) * emitters[e].sizeScale;
          Vector2 vel = {cosf(angle) * speed, sinf(angle) * speed};
          SpawnWoodParticle(PARTICLE_POLLEN, hitPos, vel,
                            Math_Mix(1.0f, 2.0f, Random01()) *
                                emitters[e].sizeScale,
                            Math_Mix(0.8f, 1.6f, Random01()), 0, 0);
        }
      }
    }

    // 4. DECAY PHASE CRUMBLING DEBRIS (progress > 1.8f)
    if (emitters[e].progress > 1.8f) {
      emitters[e].sproutAccumulator += dt;
      if (emitters[e].sproutAccumulator >= 0.05f) {
        emitters[e].sproutAccumulator = 0.0f;

        // Spawn a dead leaf or wood shard along a random branch/stem
        float randomT = Random01() * 1.8f;
        int randomBranch = GetRandomValue(0, emitters[e].branchCount - 1);
        Vector2 crumblePos =
            GetWoodPointAt(&emitters[e], randomT, randomBranch);

        Vector2 decayVel = {(Random01() - 0.5f) * 40.0f,
                            Math_Mix(60.0f, 160.0f, Random01())};

        if (Random01() > 0.4f) {
          SpawnWoodParticle(PARTICLE_LEAF, crumblePos, decayVel,
                            Math_Mix(5.0f, 10.0f, Random01()),
                            Math_Mix(0.8f, 1.5f, Random01()),
                            Random01() * PI * 2.0f,
                            Math_Mix(-2.0f, 2.0f, Random01()));
        } else {
          SpawnWoodParticle(PARTICLE_WOOD_SHARD, crumblePos, decayVel,
                            Math_Mix(3.0f, 7.0f, Random01()),
                            Math_Mix(0.6f, 1.2f, Random01()),
                            Random01() * PI * 2.0f,
                            Math_Mix(-4.0f, 4.0f, Random01()));
        }
      }
    }
  }

  // 2. UPDATE PARTICLES
  for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
    if (!woodPool[i].active)
      continue;

    woodPool[i].lifetime -= dt;
    if (woodPool[i].lifetime <= 0.0f) {
      woodPool[i].active = false;
      continue;
    }

    // Ghi lại lịch sử vị trí cho lá/mảnh gỗ để vẽ vệt chuyển động (motion
    // trail) - các loại còn lại gần như đứng yên hoặc nở tại chỗ nên không
    // cần tốn bộ nhớ lưu vệt
    if (woodPool[i].type == PARTICLE_LEAF ||
        woodPool[i].type == PARTICLE_WOOD_SHARD) {
      for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--) {
        woodPool[i].history[h] = woodPool[i].history[h - 1];
      }
      woodPool[i].history[0] = woodPool[i].position;
      if (woodPool[i].historyFilled < PARTICLE_HISTORY_COUNT)
        woodPool[i].historyFilled++;
    }

    // Apply physics based on particle type
    switch (woodPool[i].type) {
    case PARTICLE_LEAF: {
      // Apply drag
      woodPool[i].velocity.x *= (1.0f - 1.5f * dt);
      woodPool[i].velocity.y *= (1.0f - 1.5f * dt);

      // Sway left/right like a leaf drifting down
      woodPool[i].swayPhase += dt * 5.0f;
      woodPool[i].position.x += sinf(woodPool[i].swayPhase) * 22.0f * dt;

      // Slow falling gravity
      woodPool[i].velocity.y += 65.0f * dt;
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_POLLEN: {
      // Pollen floats gently upward/sideways (Brownian-like movement)
      woodPool[i].velocity.x += sinf(time * 6.0f + (float)i) * 35.0f * dt;
      woodPool[i].velocity.y -= 15.0f * dt;
      woodPool[i].velocity.x *= (1.0f - 0.8f * dt);
      woodPool[i].velocity.y *= (1.0f - 0.8f * dt);
      break;
    }
    case PARTICLE_WOOD_SHARD: {
      // Heavy gravity, high speed, spins fast
      woodPool[i].velocity.y += 750.0f * dt;
      woodPool[i].velocity.x *= (1.0f - 0.5f * dt);
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_FLOWER: {
      // Flower stays in place but grows and rotates slightly
      woodPool[i].velocity = (Vector2){0, 0};
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_ENERGY_FLASH: {
      // Expands and stays in place
      woodPool[i].velocity = (Vector2){0, 0};
      woodPool[i].radius += 110.0f * dt;
      break;
    }
    }

    woodPool[i].position.x += woodPool[i].velocity.x * dt;
    woodPool[i].position.y += woodPool[i].velocity.y * dt;
  }
}

static float GetWoodThickness(float t, float maxThick, int branchCount) {
  float progressRatio = t / 1.8f;
  float taper = powf(1.0f - progressRatio, 0.6f);
  float thick = Math_Mix(3.0f, maxThick, taper);
  float nodeBump = sinf(t * 45.0f + (float)branchCount * 5.0f) * 2.2f;
  return fmaxf(2.0f, thick + nodeBump);
}

void DrawWoodSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) { active = true; break; }
  }
  if (!active) {
    for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
      if (woodPool[i].active) { active = true; break; }
    }
  }
  if (!active) return;

  float time = GetTime();

  BeginTextureMode(canvasTexture);
  ClearBackground(BLANK);

  // ========================================================
  // LAYER 1: VINE STEMS (CHANNEL RED)
  // ========================================================
  BeginBlendMode(BLEND_ALPHA);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    // Dissolve/fade effect after coiling completes
    float fade = 1.0f;
    if (emitters[e].progress > 1.8f) {
      float over = emitters[e].progress - 1.8f;
      fade = Clamp(1.0f - over / (WOOD_PROGRESS_MAX - 1.8f), 0.0f, 1.0f);
    }

    float maxThick =
        (emitters[e].isSubBranch ? 5.0f : 8.0f) * emitters[e].sizeScale;

    // 1. Draw Main Stem
    {
      float t_start = 0.0f;
      float t_end = fminf(emitters[e].progress, 1.0f);
      if (t_start < t_end) {
        int numPoints = 80;
        Vector2 purePos[80];
        float tValues[80];
        for (int i = 0; i < numPoints; i++) {
          float t = t_start + ((float)i / (numPoints - 1)) * (t_end - t_start);
          purePos[i] = GetWoodPointAt(&emitters[e], t, 0);
          tValues[i] = t;
        }

        // Draw quad segments for stem
        for (int i = 0; i < numPoints - 1; i++) {
          float thick_i =
              GetWoodThickness(tValues[i], maxThick, emitters[e].branchCount);
          float thick_ip1 = GetWoodThickness(tValues[i + 1], maxThick,
                                             emitters[e].branchCount);

          Vector2 tangent_i;
          if (i == 0) {
            tangent_i =
                Vector2Normalize(Vector2Subtract(purePos[1], purePos[0]));
          } else {
            tangent_i = Vector2Normalize(
                Vector2Subtract(purePos[i + 1], purePos[i - 1]));
          }
          Vector2 perp_i = (Vector2){-tangent_i.y, tangent_i.x};

          Vector2 tangent_ip1;
          if (i + 1 == numPoints - 1) {
            tangent_ip1 = Vector2Normalize(Vector2Subtract(
                purePos[numPoints - 1], purePos[numPoints - 2]));
          } else {
            tangent_ip1 =
                Vector2Normalize(Vector2Subtract(purePos[i + 2], purePos[i]));
          }
          Vector2 perp_ip1 = (Vector2){-tangent_ip1.y, tangent_ip1.x};

          Vector2 V0 =
              Vector2Subtract(purePos[i], Vector2Scale(perp_i, thick_i * 0.5f));
          Vector2 V1 =
              Vector2Add(purePos[i], Vector2Scale(perp_i, thick_i * 0.5f));
          Vector2 V2 = Vector2Subtract(
              purePos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));
          Vector2 V3 = Vector2Add(purePos[i + 1],
                                  Vector2Scale(perp_ip1, thick_ip1 * 0.5f));

          unsigned char aVal = (unsigned char)(fade * 255.0f);
          Color C0 = (Color){(unsigned char)(tValues[i] * 255.0f), 0, 0, aVal};
          Color C1 =
              (Color){(unsigned char)(tValues[i] * 255.0f), 255, 0, aVal};
          Color C2 =
              (Color){(unsigned char)(tValues[i + 1] * 255.0f), 0, 0, aVal};
          Color C3 =
              (Color){(unsigned char)(tValues[i + 1] * 255.0f), 255, 0, aVal};

          rlBegin(RL_TRIANGLES);
          rlColor4ub(C0.r, C0.g, C0.b, C0.a);
          rlVertex2f(V0.x, V0.y);
          rlColor4ub(C1.r, C1.g, C1.b, C1.a);
          rlVertex2f(V1.x, V1.y);
          rlColor4ub(C3.r, C3.g, C3.b, C3.a);
          rlVertex2f(V3.x, V3.y);

          rlColor4ub(C0.r, C0.g, C0.b, C0.a);
          rlVertex2f(V0.x, V0.y);
          rlColor4ub(C3.r, C3.g, C3.b, C3.a);
          rlVertex2f(V3.x, V3.y);
          rlColor4ub(C2.r, C2.g, C2.b, C2.a);
          rlVertex2f(V2.x, V2.y);
          rlEnd();

          // Thorns along stem (every 14th segment)
          // if (i % 14 == 0 && i > 6 && i < numPoints - 6) {
          //   float side = ((i / 14) % 2 == 0) ? 1.0f : -1.0f;
          //   float thornLen = thick_i * 1.5f;
          //   Vector2 tip =
          //       Vector2Add(purePos[i], Vector2Scale(perp_i, side *
          //       thornLen));
          //   Vector2 base1 =
          //       Vector2Add(purePos[i], Vector2Scale(tangent_i, thick_i *
          //       0.7f));
          //   Vector2 base2 = Vector2Subtract(
          //       purePos[i], Vector2Scale(tangent_i, thick_i * 0.7f));

          //   Color cThorn =
          //       (Color){(unsigned char)(tValues[i] * 255.0f),
          //               (unsigned char)(side == 1.0f ? 255 : 0), 0, aVal};
          //   rlBegin(RL_TRIANGLES);
          //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
          //   rlVertex2f(base1.x, base1.y);
          //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
          //   rlVertex2f(base2.x, base2.y);
          //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
          //   rlVertex2f(tip.x, tip.y);
          //   rlEnd();
          // }
        }
      }
    }

    // 2. Draw Branch Vines (If progress > 1.0f)
    if (emitters[e].progress > 1.0f) {
      float t_start = 1.0f;
      float t_end = fminf(emitters[e].progress, 1.8f);
      if (t_start < t_end) {
        int numPoints = 80;
        Vector2 purePos[80];
        float tValues[80];

        for (int b = 0; b < emitters[e].branchCount; b++) {
          for (int i = 0; i < numPoints; i++) {
            float t =
                t_start + ((float)i / (numPoints - 1)) * (t_end - t_start);
            purePos[i] = GetWoodPointAt(&emitters[e], t, b);
            tValues[i] = t;
          }

          for (int i = 0; i < numPoints - 1; i++) {
            float maxBranchThick = maxThick * 0.75f;
            float thick_i = GetWoodThickness(tValues[i], maxBranchThick,
                                             emitters[e].branchCount + b);
            float thick_ip1 = GetWoodThickness(tValues[i + 1], maxBranchThick,
                                               emitters[e].branchCount + b);

            Vector2 tangent_i;
            if (i == 0) {
              tangent_i =
                  Vector2Normalize(Vector2Subtract(purePos[1], purePos[0]));
            } else {
              tangent_i = Vector2Normalize(
                  Vector2Subtract(purePos[i + 1], purePos[i - 1]));
            }
            Vector2 perp_i = (Vector2){-tangent_i.y, tangent_i.x};

            Vector2 tangent_ip1;
            if (i + 1 == numPoints - 1) {
              tangent_ip1 = Vector2Normalize(Vector2Subtract(
                  purePos[numPoints - 1], purePos[numPoints - 2]));
            } else {
              tangent_ip1 =
                  Vector2Normalize(Vector2Subtract(purePos[i + 2], purePos[i]));
            }
            Vector2 perp_ip1 = (Vector2){-tangent_ip1.y, tangent_ip1.x};

            Vector2 V0 = Vector2Subtract(purePos[i],
                                         Vector2Scale(perp_i, thick_i * 0.5f));
            Vector2 V1 =
                Vector2Add(purePos[i], Vector2Scale(perp_i, thick_i * 0.5f));
            Vector2 V2 = Vector2Subtract(
                purePos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));
            Vector2 V3 = Vector2Add(purePos[i + 1],
                                    Vector2Scale(perp_ip1, thick_ip1 * 0.5f));

            unsigned char aVal = (unsigned char)(fade * 255.0f);
            Color C0 =
                (Color){(unsigned char)(tValues[i] * 255.0f), 0, 0, aVal};
            Color C1 =
                (Color){(unsigned char)(tValues[i] * 255.0f), 255, 0, aVal};
            Color C2 =
                (Color){(unsigned char)(tValues[i + 1] * 255.0f), 0, 0, aVal};
            Color C3 =
                (Color){(unsigned char)(tValues[i + 1] * 255.0f), 255, 0, aVal};

            rlBegin(RL_TRIANGLES);
            rlColor4ub(C0.r, C0.g, C0.b, C0.a);
            rlVertex2f(V0.x, V0.y);
            rlColor4ub(C1.r, C1.g, C1.b, C1.a);
            rlVertex2f(V1.x, V1.y);
            rlColor4ub(C3.r, C3.g, C3.b, C3.a);
            rlVertex2f(V3.x, V3.y);

            rlColor4ub(C0.r, C0.g, C0.b, C0.a);
            rlVertex2f(V0.x, V0.y);
            rlColor4ub(C3.r, C3.g, C3.b, C3.a);
            rlVertex2f(V3.x, V3.y);
            rlColor4ub(C2.r, C2.g, C2.b, C2.a);
            rlVertex2f(V2.x, V2.y);
            rlEnd();

            // Thorns along branches
            // if (i % 14 == 0 && i > 6 && i < numPoints - 6) {
            //   float side = ((i / 14) % 2 == 0) ? 1.0f : -1.0f;
            //   float thornLen = thick_i * 1.5f;
            //   Vector2 tip =
            //       Vector2Add(purePos[i], Vector2Scale(perp_i, side *
            //       thornLen));
            //   Vector2 base1 = Vector2Add(
            //       purePos[i], Vector2Scale(tangent_i, thick_i * 0.7f));
            //   Vector2 base2 = Vector2Subtract(
            //       purePos[i], Vector2Scale(tangent_i, thick_i * 0.7f));

            //   Color cThorn =
            //       (Color){(unsigned char)(tValues[i] * 255.0f),
            //               (unsigned char)(side == 1.0f ? 255 : 0), 0, aVal};
            //   rlBegin(RL_TRIANGLES);
            //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
            //   rlVertex2f(base1.x, base1.y);
            //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
            //   rlVertex2f(base2.x, base2.y);
            //   rlColor4ub(cThorn.r, cThorn.g, cThorn.b, cThorn.a);
            //   rlVertex2f(tip.x, tip.y);
            //   rlEnd();
            // }
          }
        }
      }
    }
  }
  EndBlendMode();

  // ========================================================
  // LAYER 1.5: GROWTH TIP GLOW (CHỒI NON PHÁT SÁNG Ở ĐẦU RỄ ĐANG MỌC)
  // ========================================================
  // Một quầng sáng nhỏ nhấp nháy bám theo đúng đầu mút đang vươn tới/đang
  // cuộn, giúp mắt người chơi dễ dõi theo "ngọn dây leo đang sống" thay vì
  // chỉ thấy lá/phấn hoa rải rác dọc đường.
  BeginBlendMode(BLEND_ALPHA);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    if (emitters[e].progress >= 1.8f)
      continue;

    float tipFade = Clamp(emitters[e].progress * 6.0f, 0.0f, 1.0f);
    float tipT = fminf(emitters[e].progress, 1.8f);
    Vector2 tipPos = GetWoodPointAt(&emitters[e], tipT, 0);

    float pulse = 0.65f + 0.35f * sinf(time * 13.0f);
    float tipGlowRadius = 9.0f * emitters[e].sizeScale * pulse;

    DrawProceduralFlowerUV(tipPos, tipGlowRadius, time * 2.2f, tipFade * 0.85f);
  }
  EndBlendMode();

  // ========================================================
  // LAYER 2: ATTACHED LEAVES (CHANNEL GREEN)
  // ========================================================
  BeginBlendMode(BLEND_ALPHA);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float fade = 1.0f;
    if (emitters[e].progress > 1.8f) {
      float over = emitters[e].progress - 1.8f;
      fade = Clamp(1.0f - over / (WOOD_PROGRESS_MAX - 1.8f), 0.0f, 1.0f);
    }

    float maxLeafRad =
        (emitters[e].isSubBranch ? 3.5f : 5.2f) * emitters[e].sizeScale;

    // 1. Sprout leaves along main stem
    float stemProgress = fminf(emitters[e].progress, 1.0f);
    for (float t = 0.05f; t < stemProgress - 0.02f; t += 0.08f) {
      Vector2 pos = GetWoodPointAt(&emitters[e], t, 0);

      Vector2 tangent = Vector2Normalize(Vector2Subtract(
          GetWoodPointAt(&emitters[e], t + 0.01f, 0),
          GetWoodPointAt(&emitters[e], fmaxf(0.0f, t - 0.01f), 0)));
      float baseAngle = atan2f(tangent.y, tangent.x);
      float sideSign = (sinf(t * 100.0f) > 0.0f) ? 1.0f : -1.0f;
      float leafAngle = baseAngle + sideSign * (PI * 0.4f);

      float windSway = sinf(time * 6.0f + t * 40.0f) * 0.15f;
      float finalLeafAngle = leafAngle + windSway;

      float leafGrowScale =
          Clamp((emitters[e].progress - t) / 0.15f, 0.0f, 1.0f);
      float leafRadius = maxLeafRad * leafGrowScale;

      Vector2 attachOffset =
          RotatePoint((Vector2){0, leafRadius * 0.2f}, finalLeafAngle);
      Vector2 leafPos = Vector2Add(pos, attachOffset);

      DrawProceduralLeafUV(leafPos, leafRadius, finalLeafAngle, fade);
    }

    // 2. Sprout leaves along branches
    if (emitters[e].progress > 1.0f) {
      float branchProgress = fminf(emitters[e].progress, 1.8f);
      for (int b = 0; b < emitters[e].branchCount; b++) {
        for (float t = 1.05f; t < branchProgress - 0.02f; t += 0.10f) {
          Vector2 pos = GetWoodPointAt(&emitters[e], t, b);

          Vector2 tangent = Vector2Normalize(
              Vector2Subtract(GetWoodPointAt(&emitters[e], t + 0.01f, b),
                              GetWoodPointAt(&emitters[e], t - 0.01f, b)));
          float baseAngle = atan2f(tangent.y, tangent.x);
          float sideSign = (sinf(t * 100.0f + (float)b) > 0.0f) ? 1.0f : -1.0f;
          float leafAngle = baseAngle + sideSign * (PI * 0.4f);

          float windSway = sinf(time * 6.0f + t * 40.0f) * 0.15f;
          float finalLeafAngle = leafAngle + windSway;

          float leafGrowScale =
              Clamp((emitters[e].progress - t) / 0.15f, 0.0f, 1.0f);
          float leafRadius = maxLeafRad * 0.85f * leafGrowScale;

          Vector2 attachOffset =
              RotatePoint((Vector2){0, leafRadius * 0.2f}, finalLeafAngle);
          Vector2 leafPos = Vector2Add(pos, attachOffset);

          DrawProceduralLeafUV(leafPos, leafRadius, finalLeafAngle, fade);
        }
      }
    }
  }
  EndBlendMode();

  // ========================================================
  // LAYER 3: DYNAMIC PARTICLES (RED, GREEN, BLUE CHANNELS)
  // ========================================================
  BeginBlendMode(BLEND_ALPHA);
  for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
    if (!woodPool[i].active)
      continue;

    float lifeRatio = woodPool[i].lifetime / woodPool[i].maxLifetime;

    // Vệt chuyển động (motion trail) cho lá/mảnh gỗ bay nhanh: vẽ vài bản
    // sao mờ dần, nhỏ dần dọc theo lịch sử vị trí trước khi vẽ particle
    // chính đè lên trên cùng
    if ((woodPool[i].type == PARTICLE_LEAF ||
         woodPool[i].type == PARTICLE_WOOD_SHARD) &&
        woodPool[i].historyFilled > 0) {
      for (int h = woodPool[i].historyFilled - 1; h >= 0; h--) {
        float trailT =
            1.0f - (float)(h + 1) / (float)(PARTICLE_HISTORY_COUNT + 1);
        float trailFade = lifeRatio * trailT * 0.5f;
        float trailRadius = woodPool[i].radius * (0.3f + 0.7f * lifeRatio) *
                            (0.4f + 0.5f * trailT);
        if (woodPool[i].type == PARTICLE_LEAF) {
          DrawProceduralLeafUV(woodPool[i].history[h], trailRadius,
                               woodPool[i].angle, trailFade);
        } else {
          DrawProceduralShardUV(woodPool[i].history[h], trailRadius,
                                woodPool[i].angle, trailFade);
        }
      }
    }

    if (woodPool[i].type == PARTICLE_LEAF) {
      DrawProceduralLeafUV(woodPool[i].position,
                           woodPool[i].radius * (0.3f + 0.7f * lifeRatio),
                           woodPool[i].angle, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_POLLEN) {
      DrawProceduralFlowerUV(woodPool[i].position,
                             woodPool[i].radius * (0.5f + 0.5f * lifeRatio),
                             0.0f, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_WOOD_SHARD) {
      DrawProceduralShardUV(woodPool[i].position, woodPool[i].radius,
                            woodPool[i].angle, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_FLOWER) {
      float sizeMult = smoothstep(1.0f, 0.7f, lifeRatio);
      DrawProceduralFlowerUV(woodPool[i].position,
                             woodPool[i].radius * sizeMult, woodPool[i].angle,
                             lifeRatio);
    } else if (woodPool[i].type == PARTICLE_ENERGY_FLASH) {
      DrawProceduralEnergyFlashUV(woodPool[i].position, woodPool[i].radius,
                                  lifeRatio);
    }
  }
  EndBlendMode();

  EndTextureMode();

  // ========================================================
  // LAYER 4: APPLY MULTI-CHANNEL SHADER & DRAW TO SCREEN
  // ========================================================
  SetShaderValue(woodShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

  Rectangle srcRect = (Rectangle){0, 0, (float)canvasTexture.texture.width,
                                  (float)-canvasTexture.texture.height};

  // 4a. Bloom giá rẻ: rải vài bản sao lệch nhẹ của riêng phần phát sáng
  // (nhựa cây, lõi hoa, viền vàng) rồi cộng màu (additive) - giả lập glow
  // mềm quanh các điểm sáng mà không cần thêm render target hay shader
  // blur riêng
  float glowOn = 1.0f;
  SetShaderValue(woodShader, glowPassLoc, &glowOn, SHADER_UNIFORM_FLOAT);
  BeginBlendMode(BLEND_ADDITIVE);
  BeginShaderMode(woodShader);
  Vector2 bloomOffsets[6] = {{1.5f, 0.0f},  {-1.5f, 0.0f}, {0.0f, 1.5f},
                             {0.0f, -1.5f}, {1.0f, 1.0f},  {-1.0f, -1.0f}};
  Color glowTint = (Color){255, 255, 255, 45};
  for (int k = 0; k < 6; k++) {
    DrawTextureRec(canvasTexture.texture, srcRect, bloomOffsets[k], glowTint);
  }
  EndShaderMode();
  EndBlendMode();

  // 4b. Lớp hình ảnh chính, sắc nét đầy đủ vỏ cây / lá / hoa, vẽ đè lên trên
  float glowOff = 0.0f;
  SetShaderValue(woodShader, glowPassLoc, &glowOff, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(woodShader);
  DrawTextureRec(canvasTexture.texture, srcRect, (Vector2){0, 0}, WHITE);
  EndShaderMode();
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

int GetWoodSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && emitters[i].progress < 1.0f &&
        count < maxProjectiles) {
      Vector2 headPos = GetWoodPointAt(&emitters[i], emitters[i].progress, 0);
      outProjectiles[count].position = headPos;
      outProjectiles[count].radius =
          18.0f * emitters[i].sizeScale; // Bán kính va chạm rễ gỗ đang bay
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
        // Nhảy thẳng sang giai đoạn quấn rễ (coiling) tại mục tiêu
        emitters[i].progress = 1.0f;

        // Spawn hiệu ứng nổ mảnh vụn gỗ và phấn hoa tại đầu rễ lúc va chạm
        Vector2 impactPos = GetWoodPointAt(&emitters[i], 1.0f, 0);
        int pCount = 15 * emitters[i].sizeScale;
        for (int p = 0; p < pCount; p++) {
          float angle = Random01() * PI * 2.0f;
          float speed =
              Math_Mix(100.0f, 300.0f, Random01()) * emitters[i].sizeScale;
          Vector2 vel = {cosf(angle) * speed, sinf(angle) * speed};
          SpawnWoodParticle(PARTICLE_WOOD_SHARD, impactPos, vel,
                            Math_Mix(5.0f, 12.0f, Random01()) *
                                emitters[i].sizeScale,
                            Math_Mix(0.4f, 0.8f, Random01()), Random01() * PI,
                            (Random01() - 0.5f) * 2.0f);
        }
        break;
      }
      count++;
    }
  }
}
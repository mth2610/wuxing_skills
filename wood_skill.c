#include "wood_skill.h"
#include "skill_manager.h"
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
#define WOOD_PROGRESS_MAX 2.4f // Cho phép rễ cây đứng yên sau khi đánh trúng mục tiêu

// Emitter Structure
typedef struct {
  bool active;
  bool isSubBranch;
  bool hasBranched;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 contactPos; // Vị trí tiếp xúc rìa Enemy
  Vector3 p1, p2;
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
  Vector3 position;
  Vector3 velocity;
  float radius;
  float lifetime;
  float maxLifetime;
  float angle;
  float spinSpeed;
  float swayPhase;
  Vector3 history[PARTICLE_HISTORY_COUNT]; // Vệt vị trí gần nhất để vẽ motion trail
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

// Tham chiếu camera từ main.c
extern Camera3D camera;

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

// Vector2 rotation helper (cho xoay vẽ lá/hoa 2D trên screen-space)
static Vector2 RotatePoint(Vector2 pt, float angle) {
  float s = sinf(angle);
  float c = cosf(angle);
  return (Vector2){pt.x * c - pt.y * s, pt.x * s + pt.y * c};
}

// Bezier Curve in 3D
static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
  float u = 1.0f - t;
  float tt = t * t;
  float uu = u * u;
  float uuu = uu * u;
  float ttt = tt * t;
  return Vector3Add(Vector3Add(Vector3Add(Vector3Scale(p0, uuu),
                                          Vector3Scale(p1, 3.0f * uu * t)),
                               Vector3Scale(p2, 3.0f * u * tt)),
                    Vector3Scale(p3, ttt));
}

static Vector3 GetWoodPointAt(const WoodEmitter *em, float t, int branchIndex) {
  if (t <= 1.0f) {
    return GetBezierPoint(em->startPos, em->p1, em->p2, em->contactPos, t);
  } else {
    float t_spiral = t - 1.0f;
    float ratio = t_spiral / 0.8f;
    if (ratio > 1.0f)
      ratio = 1.0f;

    // Tính toán góc tiếp xúc trên mặt phẳng nằm ngang X-Z
    float contactAngle = atan2f(em->contactPos.z - em->targetPos.z,
                                em->contactPos.x - em->targetPos.x);

    float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;
    float theta = ratio * 3.0f * (2.0f * PI) * coilDir;

    float phiOffset = 0.0f;
    if (em->branchCount > 1) {
      phiOffset = ((float)branchIndex / (float)em->branchCount) * PI;
    }
    float phi = contactAngle + phiOffset;

    // Bán kính quấn quanh mục tiêu
    float r_a = 28.0f * em->sizeScale;
    float r_b = 28.0f * em->sizeScale;

    float wobbleScale = ratio > 0.1f ? 1.0f : ratio / 0.1f;
    float wobble = (sinf(theta * 6.0f) * 4.0f + cosf(theta * 11.0f) * 1.5f) * wobbleScale;

    float curr_ra = r_a + wobble;
    float curr_rb = r_b + wobble;

    float x_local = curr_ra * cosf(theta);
    float z_local = curr_rb * sinf(theta);

    return (Vector3){
        em->targetPos.x + x_local * cosf(phi) - z_local * sinf(phi),
        em->targetPos.y + wobble * 0.1f, // Dao động nhẹ theo chiều cao Y
        em->targetPos.z + x_local * sinf(phi) + z_local * cosf(phi)
    };
  }
}

static void DrawProceduralLeafUV(Vector2 position, float radius, float angle, float fade) {
  Vector2 left = RotatePoint((Vector2){-radius * 0.5f, 0}, angle);
  Vector2 right = RotatePoint((Vector2){radius * 0.5f, 0}, angle);
  Vector2 head = RotatePoint((Vector2){0, radius}, angle);
  Vector2 tail = RotatePoint((Vector2){0, -radius * 0.8f}, angle);

  left = Vector2Add(left, position);
  right = Vector2Add(right, position);
  head = Vector2Add(head, position);
  tail = Vector2Add(tail, position);

  unsigned char aVal = (unsigned char)(fade * 255.0f);

  Color cTail = (Color){0, 128, 128, aVal};
  Color cLeft = (Color){128, 0, 128, aVal};
  Color cRight = (Color){128, 255, 128, aVal};
  Color cHead = (Color){255, 128, 128, aVal};

  rlBegin(RL_TRIANGLES);
  rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a);
  rlVertex2f(tail.x, tail.y);
  rlColor4ub(cLeft.r, cLeft.g, cLeft.b, cLeft.a);
  rlVertex2f(left.x, left.y);
  rlColor4ub(cHead.r, cHead.g, cHead.b, cHead.a);
  rlVertex2f(head.x, head.y);

  rlColor4ub(cTail.r, cTail.g, cTail.b, cTail.a);
  rlVertex2f(tail.x, tail.y);
  rlColor4ub(cHead.r, cHead.g, cHead.b, cHead.a);
  rlVertex2f(head.x, head.y);
  rlColor4ub(cRight.r, cRight.g, cRight.b, cRight.a);
  rlVertex2f(right.x, right.y);
  rlEnd();
}

static void DrawProceduralFlowerUV(Vector2 position, float radius, float angle, float fade) {
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

static void DrawProceduralShardUV(Vector2 position, float radius, float angle, float fade) {
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

static void DrawProceduralEnergyFlashUV(Vector2 position, float radius, float fade) {
  float hs = radius;
  Vector2 rotated[4] = {{position.x - hs, position.y - hs},
                        {position.x + hs, position.y - hs},
                        {position.x - hs, position.y + hs},
                        {position.x + hs, position.y + hs}};
  unsigned char aVal = (unsigned char)(fade * 255.0f);
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

// Particle spawning in 3D
static void SpawnWoodParticle(int type, Vector3 pos, Vector3 vel,
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

void CastWoodSkill(Vector3 startPos, Vector3 target, int branchCount, float sizeScale) {
  if (branchCount > 5)
    branchCount = 5;
  if (branchCount < 1)
    branchCount = 1;

  // Tìm emitter trống
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

  // Tính toán Bezier Control Points
  float curveSign = (Random01() > 0.5f) ? 1.0f : -1.0f;
  float dist = Vector3Distance(startPos, target);
  Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
  Vector3 perp = (Vector3){-dir.z, 0.0f, dir.x};
  if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
  perp = Vector3Normalize(perp);

  em->p1 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f)),
                      Vector3Scale(perp, curveSign * dist * 0.25f));
  em->p2 = Vector3Add(Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f)),
                      Vector3Scale(perp, -curveSign * dist * 0.15f));

  // Điểm chạm biên mục tiêu
  float wrapRadius = 28.0f * sizeScale;
  Vector3 dirToTarget = Vector3Normalize(Vector3Subtract(target, em->p2));
  em->contactPos = Vector3Subtract(target, Vector3Scale(dirToTarget, wrapRadius));

  // Caster burst: Sóng chấn động kép
  SpawnWoodParticle(PARTICLE_ENERGY_FLASH, startPos, (Vector3){0, 0, 0},
                    35.0f * sizeScale, 0.5f, 0, 0);
  SpawnWoodParticle(PARTICLE_ENERGY_FLASH, startPos, (Vector3){0, 0, 0},
                    60.0f * sizeScale, 0.35f, 0, 0);

  // Vòng xoáy lá cây xung quanh caster
  int burstCount = 15 * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    float angle = ((float)s / (float)burstCount) * PI * 2.0f + Random01() * 0.3f;
    float speed = Math_Mix(140.0f, 280.0f, Random01()) * sizeScale;
    Vector3 vel = {
        cosf(angle) * speed,
        Math_Mix(-50.0f, 150.0f, Random01()) * sizeScale, // Có vận tốc Y hướng lên
        sinf(angle) * speed
    };
    SpawnWoodParticle(PARTICLE_LEAF, startPos, vel,
                      Math_Mix(5.0f, 10.0f, Random01()) * sizeScale,
                      Math_Mix(0.8f, 1.4f, Random01()), angle,
                      Math_Mix(-3.0f, 3.0f, Random01()));
  }

  // Phấn hoa bốc nhẹ lên trên
  for (int s = 0; s < 12; s++) {
    float angle = Random01() * PI * 2.0f;
    float speed = Math_Mix(40.0f, 100.0f, Random01());
    Vector3 vel = {
        cosf(angle) * speed,
        Math_Mix(60.0f, 140.0f, Random01()), // Bay hướng lên
        sinf(angle) * speed
    };
    SpawnWoodParticle(PARTICLE_POLLEN, startPos, vel,
                      Math_Mix(2.0f, 4.0f, Random01()),
                      Math_Mix(1.0f, 2.0f, Random01()), 0, 0);
  }
}

void UpdateWoodSkill(float dt) {
  float time = GetTime();

  // 1. CẬP NHẬT EMITTERS
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float prevProgress = emitters[e].progress;
    float travelRate = WOOD_TRAVEL_SPEED;
    if (emitters[e].progress < 1.0f) {
      travelRate *= Math_Mix(1.4f, 0.7f, smoothstep(0.0f, 1.0f, emitters[e].progress));
    } else if (emitters[e].progress < 1.8f) {
      travelRate *= 1.15f;
    }
    emitters[e].progress += dt * travelRate * emitters[e].speedMultiplier;

    if (emitters[e].progress >= WOOD_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    // Nảy nở lá và phấn hoa trên đường bay
    if (emitters[e].progress <= 1.8f) {
      emitters[e].sproutAccumulator += dt;
      if (emitters[e].sproutAccumulator >= 0.04f) {
        emitters[e].sproutAccumulator = 0.0f;

        for (int b = 0; b < emitters[e].branchCount; b++) {
          if (emitters[e].progress <= 1.0f && b > 0)
            continue;

          Vector3 headPos = GetWoodPointAt(&emitters[e], emitters[e].progress, b);
          Vector3 tangent;
          float prevProg = fmaxf(0.0f, emitters[e].progress - 0.01f);
          if (emitters[e].progress > prevProg) {
            tangent = Vector3Normalize(Vector3Subtract(headPos, GetWoodPointAt(&emitters[e], prevProg, b)));
          } else {
            tangent = (Vector3){1.0f, 0.0f, 0.0f};
          }
          Vector3 perp = (Vector3){-tangent.z, 0.0f, tangent.x};
          if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
          perp = Vector3Normalize(perp);

          // Tạo lá trôi dạt
          float side = (Random01() > 0.5f) ? 1.0f : -1.0f;
          Vector3 leafVel = Vector3Scale(
              Vector3Add(Vector3Scale(perp, side * 80.0f), Vector3Scale(tangent, -40.0f)),
              Random01()
          );
          leafVel.y += (Random01() - 0.5f) * 30.0f;
          
          SpawnWoodParticle(
              PARTICLE_LEAF, headPos, leafVel,
              Math_Mix(2.5f, 6.0f, Random01()),
              Math_Mix(1.0f, 1.8f, Random01()), Random01() * PI * 2.0f,
              Math_Mix(-4.0f, 4.0f, Random01()));

          // Phấn hoa ma thuật
          Vector3 pollenVel = {
              (Random01() - 0.5f) * 60.0f,
              Random01() * 40.0f,
              (Random01() - 0.5f) * 60.0f
          };
          SpawnWoodParticle(PARTICLE_POLLEN, headPos, pollenVel,
                            Math_Mix(1.0f, 2.3f, Random01()),
                            Math_Mix(0.8f, 1.5f, Random01()), 0, 0);
        }
      }
    }

    // 2. TRIGGER IMPACT KHI ĐẠT 100% TIẾN TRÌNH (Bắt đầu cuộn)
    if (prevProgress <= 1.0f && emitters[e].progress > 1.0f) {
      Vector3 hitPos = emitters[e].targetPos;
      Vector3 contactPos = emitters[e].contactPos;

      // Hoa nở tại chỗ tiếp xúc rễ
      SpawnWoodParticle(PARTICLE_FLOWER, contactPos, (Vector3){0, 0, 0},
                        11.0f * emitters[e].sizeScale, 1.4f, Random01() * PI,
                        0.4f);

      // Bùng nổ lá
      int leafBurst = GetRandomValue(10, 18) * emitters[e].sizeScale;
      for (int s = 0; s < leafBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed = Math_Mix(80.0f, 300.0f, Random01()) * emitters[e].sizeScale;
        Vector3 vel = {
            cosf(angle) * speed,
            (sinf(angle) * speed + 50.0f) * emitters[e].sizeScale, // Đẩy lên trục Y
            sinf(angle) * speed
        };
        SpawnWoodParticle(
            PARTICLE_LEAF, hitPos, vel,
            Math_Mix(2.5f, 5.2f, Random01()) * emitters[e].sizeScale,
            Math_Mix(1.2f, 2.2f, Random01()), Random01() * PI * 2.0f,
            Math_Mix(-6.0f, 6.0f, Random01()));
      }

      // Bùng nổ phấn hoa
      int pollenBurst = GetRandomValue(12, 22) * emitters[e].sizeScale;
      for (int s = 0; s < pollenBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float speed = Math_Mix(40.0f, 150.0f, Random01()) * emitters[e].sizeScale;
        Vector3 vel = {
            cosf(angle) * speed,
            (sinf(angle) * speed + 30.0f) * emitters[e].sizeScale,
            sinf(angle) * speed
        };
        SpawnWoodParticle(PARTICLE_POLLEN, hitPos, vel,
                          Math_Mix(1.0f, 2.5f, Random01()) * emitters[e].sizeScale,
                          Math_Mix(1.0f, 2.0f, Random01()), 0, 0);
      }

      // Mảnh vụn gỗ văng ra
      int shardBurst = GetRandomValue(8, 14) * emitters[e].sizeScale;
      for (int s = 0; s < shardBurst; s++) {
        float angle = Random01() * PI * 2.0f;
        float pitch = (Random01() - 0.5f) * PI;
        float speed = Math_Mix(150.0f, 450.0f, Random01()) * emitters[e].sizeScale;
        Vector3 vel = {
            cosf(angle) * speed * cosf(pitch),
            sinf(pitch) * speed,
            sinf(angle) * speed * cosf(pitch)
        };
        SpawnWoodParticle(
            PARTICLE_WOOD_SHARD, hitPos, vel,
            Math_Mix(1.6f, 4.0f, Random01()) * emitters[e].sizeScale,
            Math_Mix(0.6f, 1.0f, Random01()), Random01() * PI * 2.0f,
            Math_Mix(-8.0f, 8.0f, Random01()));
      }
    }

    // 3. TRIGGER IMPACT KHI HOÀN TẤT CUỘN (1.8f)
    if (prevProgress <= 1.8f && emitters[e].progress > 1.8f) {
      SpawnWoodParticle(PARTICLE_ENERGY_FLASH, emitters[e].targetPos,
                        (Vector3){0, 0, 0}, 26.0f * emitters[e].sizeScale, 0.4f, 0, 0);
      SpawnWoodParticle(PARTICLE_ENERGY_FLASH, emitters[e].targetPos,
                        (Vector3){0, 0, 0}, 45.0f * emitters[e].sizeScale, 0.3f, 0, 0);

      for (int b = 0; b < emitters[e].branchCount; b++) {
        Vector3 hitPos = GetWoodPointAt(&emitters[e], 1.8f, b);

        // Nở hoa nhỏ ở mỗi đầu nhánh rễ
        SpawnWoodParticle(PARTICLE_FLOWER, hitPos, (Vector3){0, 0, 0},
                          9.0f * emitters[e].sizeScale, 1.3f, Random01() * PI,
                          -0.5f);

        int pollenBurst = GetRandomValue(5, 9) * emitters[e].sizeScale;
        for (int s = 0; s < pollenBurst; s++) {
          float angle = Random01() * PI * 2.0f;
          float speed = Math_Mix(30.0f, 80.0f, Random01()) * emitters[e].sizeScale;
          Vector3 vel = {
              cosf(angle) * speed,
              (float)GetRandomValue(30, 80) * emitters[e].sizeScale,
              sinf(angle) * speed
          };
          SpawnWoodParticle(PARTICLE_POLLEN, hitPos, vel,
                            Math_Mix(1.0f, 2.0f, Random01()) * emitters[e].sizeScale,
                            Math_Mix(0.8f, 1.6f, Random01()), 0, 0);
        }
      }
    }

    // 4. DECAY PHASE: MẢNH VỤN RƠI RỤNG (progress > 1.8f)
    if (emitters[e].progress > 1.8f) {
      emitters[e].sproutAccumulator += dt;
      if (emitters[e].sproutAccumulator >= 0.05f) {
        emitters[e].sproutAccumulator = 0.0f;

        float randomT = Random01() * 1.8f;
        int randomBranch = GetRandomValue(0, emitters[e].branchCount - 1);
        Vector3 crumblePos = GetWoodPointAt(&emitters[e], randomT, randomBranch);

        // Rơi rụng xuống đất (Y âm)
        Vector3 decayVel = {
            (Random01() - 0.5f) * 40.0f,
            -Math_Mix(60.0f, 160.0f, Random01()),
            (Random01() - 0.5f) * 40.0f
        };

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

  // 2. CẬP NHẬT CÁC HẠT MỘC
  for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
    if (!woodPool[i].active)
      continue;

    woodPool[i].lifetime -= dt;
    if (woodPool[i].lifetime <= 0.0f) {
      woodPool[i].active = false;
      continue;
    }

    if (woodPool[i].type == PARTICLE_LEAF || woodPool[i].type == PARTICLE_WOOD_SHARD) {
      for (int h = PARTICLE_HISTORY_COUNT - 1; h > 0; h--) {
        woodPool[i].history[h] = woodPool[i].history[h - 1];
      }
      woodPool[i].history[0] = woodPool[i].position;
      if (woodPool[i].historyFilled < PARTICLE_HISTORY_COUNT)
        woodPool[i].historyFilled++;
    }

    switch (woodPool[i].type) {
    case PARTICLE_LEAF: {
      woodPool[i].velocity.x *= (1.0f - 1.5f * dt);
      woodPool[i].velocity.y *= (1.0f - 1.5f * dt);
      woodPool[i].velocity.z *= (1.0f - 1.5f * dt);

      woodPool[i].swayPhase += dt * 5.0f;
      woodPool[i].position.x += sinf(woodPool[i].swayPhase) * 22.0f * dt;
      woodPool[i].position.z += cosf(woodPool[i].swayPhase) * 22.0f * dt;

      // Trọng lực rơi chậm (Y âm)
      woodPool[i].velocity.y -= 65.0f * dt;
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_POLLEN: {
      // Phấn hoa bốc nhẹ lên và chuyển động ngẫu nhiên
      woodPool[i].velocity.x += sinf(time * 6.0f + (float)i) * 35.0f * dt;
      woodPool[i].velocity.z += cosf(time * 6.0f + (float)i) * 35.0f * dt;
      woodPool[i].velocity.y += 15.0f * dt; // Bay lên (Y dương)
      woodPool[i].velocity.x *= (1.0f - 0.8f * dt);
      woodPool[i].velocity.y *= (1.0f - 0.8f * dt);
      woodPool[i].velocity.z *= (1.0f - 0.8f * dt);
      break;
    }
    case PARTICLE_WOOD_SHARD: {
      // Trọng lực nặng hơn rơi nhanh
      woodPool[i].velocity.y -= 750.0f * dt;
      woodPool[i].velocity.x *= (1.0f - 0.5f * dt);
      woodPool[i].velocity.z *= (1.0f - 0.5f * dt);
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_FLOWER: {
      woodPool[i].velocity = (Vector3){0, 0, 0};
      woodPool[i].angle += woodPool[i].spinSpeed * dt;
      break;
    }
    case PARTICLE_ENERGY_FLASH: {
      woodPool[i].velocity = (Vector3){0, 0, 0};
      woodPool[i].radius += 110.0f * dt;
      break;
    }
    }

    woodPool[i].position.x += woodPool[i].velocity.x * dt;
    woodPool[i].position.y += woodPool[i].velocity.y * dt;
    woodPool[i].position.z += woodPool[i].velocity.z * dt;
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

    float fade = 1.0f;
    if (emitters[e].progress > 1.8f) {
      float over = emitters[e].progress - 1.8f;
      fade = Clamp(1.0f - over / (WOOD_PROGRESS_MAX - 1.8f), 0.0f, 1.0f);
    }

    float maxThick = (emitters[e].isSubBranch ? 5.0f : 8.0f) * emitters[e].sizeScale;

    // 1. Vẽ thân chính
    {
      float t_start = 0.0f;
      float t_end = fminf(emitters[e].progress, 1.0f);
      if (t_start < t_end) {
        int numPoints = 80;
        Vector2 screenPos[80];
        float tValues[80];
        float depthFactors[80];

        for (int i = 0; i < numPoints; i++) {
          float t = t_start + ((float)i / (numPoints - 1)) * (t_end - t_start);
          Vector3 pos3D = GetWoodPointAt(&emitters[e], t, 0);
          ProjectedPoint pt = ProjectPointCached(pos3D, camera);
          screenPos[i] = pt.screenPos;
          tValues[i] = t;
          depthFactors[i] = pt.depthFactor;
        }

        // Vẽ các đốt rễ bằng đa giác
        for (int i = 0; i < numPoints - 1; i++) {
          float thick_i = GetWoodThickness(tValues[i], maxThick, emitters[e].branchCount) * depthFactors[i];
          float thick_ip1 = GetWoodThickness(tValues[i + 1], maxThick, emitters[e].branchCount) * depthFactors[i + 1];

          Vector2 tangent_i;
          if (i == 0) {
            tangent_i = Vector2Normalize(Vector2Subtract(screenPos[1], screenPos[0]));
          } else {
            tangent_i = Vector2Normalize(Vector2Subtract(screenPos[i + 1], screenPos[i - 1]));
          }
          Vector2 perp_i = (Vector2){-tangent_i.y, tangent_i.x};

          Vector2 tangent_ip1;
          if (i + 1 == numPoints - 1) {
            tangent_ip1 = Vector2Normalize(Vector2Subtract(screenPos[numPoints - 1], screenPos[numPoints - 2]));
          } else {
            tangent_ip1 = Vector2Normalize(Vector2Subtract(screenPos[i + 2], screenPos[i]));
          }
          Vector2 perp_ip1 = (Vector2){-tangent_ip1.y, tangent_ip1.x};

          Vector2 V0 = Vector2Subtract(screenPos[i], Vector2Scale(perp_i, thick_i * 0.5f));
          Vector2 V1 = Vector2Add(screenPos[i], Vector2Scale(perp_i, thick_i * 0.5f));
          Vector2 V2 = Vector2Subtract(screenPos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));
          Vector2 V3 = Vector2Add(screenPos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));

          unsigned char aVal = (unsigned char)(fade * 255.0f);
          Color C0 = (Color){(unsigned char)(tValues[i] * 255.0f), 0, 0, aVal};
          Color C1 = (Color){(unsigned char)(tValues[i] * 255.0f), 255, 0, aVal};
          Color C2 = (Color){(unsigned char)(tValues[i + 1] * 255.0f), 0, 0, aVal};
          Color C3 = (Color){(unsigned char)(tValues[i + 1] * 255.0f), 255, 0, aVal};

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
        }
      }
    }

    // 2. Vẽ nhánh phụ (nếu progress > 1.0f)
    if (emitters[e].progress > 1.0f) {
      float t_start = 1.0f;
      float t_end = fminf(emitters[e].progress, 1.8f);
      if (t_start < t_end) {
        int numPoints = 80;
        Vector2 screenPos[80];
        float tValues[80];
        float depthFactors[80];

        for (int b = 0; b < emitters[e].branchCount; b++) {
          for (int i = 0; i < numPoints; i++) {
            float t = t_start + ((float)i / (numPoints - 1)) * (t_end - t_start);
            Vector3 pos3D = GetWoodPointAt(&emitters[e], t, b);
            ProjectedPoint pt = ProjectPointCached(pos3D, camera);
            screenPos[i] = pt.screenPos;
            tValues[i] = t;
            depthFactors[i] = pt.depthFactor;
          }

          for (int i = 0; i < numPoints - 1; i++) {
            float maxBranchThick = maxThick * 0.75f;
            float thick_i = GetWoodThickness(tValues[i], maxBranchThick, emitters[e].branchCount + b) * depthFactors[i];
            float thick_ip1 = GetWoodThickness(tValues[i + 1], maxBranchThick, emitters[e].branchCount + b) * depthFactors[i + 1];

            Vector2 tangent_i;
            if (i == 0) {
              tangent_i = Vector2Normalize(Vector2Subtract(screenPos[1], screenPos[0]));
            } else {
              tangent_i = Vector2Normalize(Vector2Subtract(screenPos[i + 1], screenPos[i - 1]));
            }
            Vector2 perp_i = (Vector2){-tangent_i.y, tangent_i.x};

            Vector2 tangent_ip1;
            if (i + 1 == numPoints - 1) {
              tangent_ip1 = Vector2Normalize(Vector2Subtract(screenPos[numPoints - 1], screenPos[numPoints - 2]));
            } else {
              tangent_ip1 = Vector2Normalize(Vector2Subtract(screenPos[i + 2], screenPos[i]));
            }
            Vector2 perp_ip1 = (Vector2){-tangent_ip1.y, tangent_ip1.x};

            Vector2 V0 = Vector2Subtract(screenPos[i], Vector2Scale(perp_i, thick_i * 0.5f));
            Vector2 V1 = Vector2Add(screenPos[i], Vector2Scale(perp_i, thick_i * 0.5f));
            Vector2 V2 = Vector2Subtract(screenPos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));
            Vector2 V3 = Vector2Add(screenPos[i + 1], Vector2Scale(perp_ip1, thick_ip1 * 0.5f));

            unsigned char aVal = (unsigned char)(fade * 255.0f);
            Color C0 = (Color){(unsigned char)(tValues[i] * 255.0f), 0, 0, aVal};
            Color C1 = (Color){(unsigned char)(tValues[i] * 255.0f), 255, 0, aVal};
            Color C2 = (Color){(unsigned char)(tValues[i + 1] * 255.0f), 0, 0, aVal};
            Color C3 = (Color){(unsigned char)(tValues[i + 1] * 255.0f), 255, 0, aVal};

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
          }
        }
      }
    }
  }
  EndBlendMode();

  // ========================================================
  // LAYER 1.5: GROWTH TIP GLOW (Chồi non phát sáng đầu rễ)
  // ========================================================
  BeginBlendMode(BLEND_ALPHA);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    if (emitters[e].progress >= 1.8f)
      continue;

    float tipFade = Clamp(emitters[e].progress * 6.0f, 0.0f, 1.0f);
    float tipT = fminf(emitters[e].progress, 1.8f);
    Vector3 tipPos3D = GetWoodPointAt(&emitters[e], tipT, 0);
    ProjectedPoint pt = ProjectPointCached(tipPos3D, camera);
    if (pt.behindCamera) continue;
    Vector2 tipPos = pt.screenPos;
    float depthFactor = pt.depthFactor;

    float pulse = 0.65f + 0.35f * sinf(time * 13.0f);
    float tipGlowRadius = 9.0f * emitters[e].sizeScale * pulse * depthFactor;

    DrawProceduralFlowerUV(tipPos, tipGlowRadius, time * 2.2f, tipFade * 0.85f);
  }
  EndBlendMode();

  // ========================================================
  // LAYER 2: ATTACHED LEAVES (Green leaves sprout)
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

    float maxLeafRad = (emitters[e].isSubBranch ? 3.5f : 5.2f) * emitters[e].sizeScale;

    // 1. Lá mọc dọc thân chính
    float stemProgress = fminf(emitters[e].progress, 1.0f);
    for (float t = 0.05f; t < stemProgress - 0.02f; t += 0.08f) {
      Vector3 pos3D = GetWoodPointAt(&emitters[e], t, 0);
      ProjectedPoint pt = ProjectPointCached(pos3D, camera);
      if (pt.behindCamera) continue;
      Vector2 pos = pt.screenPos;
      float depthFactor = pt.depthFactor;

      Vector2 screenNext = ProjectPointCached(GetWoodPointAt(&emitters[e], t + 0.01f, 0), camera).screenPos;
      Vector2 screenPrev = ProjectPointCached(GetWoodPointAt(&emitters[e], fmaxf(0.0f, t - 0.01f), 0), camera).screenPos;
      Vector2 tangent = Vector2Normalize(Vector2Subtract(screenNext, screenPrev));
      float baseAngle = atan2f(tangent.y, tangent.x);
      float sideSign = (sinf(t * 100.0f) > 0.0f) ? 1.0f : -1.0f;
      float leafAngle = baseAngle + sideSign * (PI * 0.4f);

      float windSway = sinf(time * 6.0f + t * 40.0f) * 0.15f;
      float finalLeafAngle = leafAngle + windSway;

      float leafGrowScale = Clamp((emitters[e].progress - t) / 0.15f, 0.0f, 1.0f);
      float leafRadius = maxLeafRad * leafGrowScale * depthFactor;

      Vector2 attachOffset = RotatePoint((Vector2){0, leafRadius * 0.2f}, finalLeafAngle);
      Vector2 leafPos = Vector2Add(pos, attachOffset);

      DrawProceduralLeafUV(leafPos, leafRadius, finalLeafAngle, fade);
    }

    // 2. Lá mọc dọc nhánh rễ
    if (emitters[e].progress > 1.0f) {
      float branchProgress = fminf(emitters[e].progress, 1.8f);
      for (int b = 0; b < emitters[e].branchCount; b++) {
        for (float t = 1.05f; t < branchProgress - 0.02f; t += 0.10f) {
          Vector3 pos3D = GetWoodPointAt(&emitters[e], t, b);
          ProjectedPoint pt = ProjectPointCached(pos3D, camera);
          if (pt.behindCamera) continue;
          Vector2 pos = pt.screenPos;
          float depthFactor = pt.depthFactor;

          Vector2 screenNext = ProjectPointCached(GetWoodPointAt(&emitters[e], t + 0.01f, b), camera).screenPos;
          Vector2 screenPrev = ProjectPointCached(GetWoodPointAt(&emitters[e], t - 0.01f, b), camera).screenPos;
          Vector2 tangent = Vector2Normalize(Vector2Subtract(screenNext, screenPrev));
          float baseAngle = atan2f(tangent.y, tangent.x);
          float sideSign = (sinf(t * 100.0f + (float)b) > 0.0f) ? 1.0f : -1.0f;
          float leafAngle = baseAngle + sideSign * (PI * 0.4f);

          float windSway = sinf(time * 6.0f + t * 40.0f) * 0.15f;
          float finalLeafAngle = leafAngle + windSway;

          float leafGrowScale = Clamp((emitters[e].progress - t) / 0.15f, 0.0f, 1.0f);
          float leafRadius = maxLeafRad * 0.85f * leafGrowScale * depthFactor;

          Vector2 attachOffset = RotatePoint((Vector2){0, leafRadius * 0.2f}, finalLeafAngle);
          Vector2 leafPos = Vector2Add(pos, attachOffset);

          DrawProceduralLeafUV(leafPos, leafRadius, finalLeafAngle, fade);
        }
      }
    }
  }
  EndBlendMode();

  // ========================================================
  // LAYER 3: DYNAMIC PARTICLES
  // ========================================================
  BeginBlendMode(BLEND_ALPHA);
  for (int i = 0; i < MAX_WOOD_PARTICLES; i++) {
    if (!woodPool[i].active)
      continue;

    float lifeRatio = woodPool[i].lifetime / woodPool[i].maxLifetime;
    ProjectedPoint pt = ProjectPointCached(woodPool[i].position, camera);
    if (pt.behindCamera) continue;
    Vector2 screenPos = pt.screenPos;
    float depthFactor = pt.depthFactor;

    // Vẽ vệt motion trail cho lá và mảnh gỗ
    if ((woodPool[i].type == PARTICLE_LEAF || woodPool[i].type == PARTICLE_WOOD_SHARD) && woodPool[i].historyFilled > 0) {
      for (int h = woodPool[i].historyFilled - 1; h >= 0; h--) {
        float trailT = 1.0f - (float)(h + 1) / (float)(PARTICLE_HISTORY_COUNT + 1);
        float trailFade = lifeRatio * trailT * 0.5f;
        float trailRadius = woodPool[i].radius * (0.3f + 0.7f * lifeRatio) * (0.4f + 0.5f * trailT) * depthFactor;
        Vector2 trailScreenPos = ProjectPointCached(woodPool[i].history[h], camera).screenPos;
        if (woodPool[i].type == PARTICLE_LEAF) {
          DrawProceduralLeafUV(trailScreenPos, trailRadius, woodPool[i].angle, trailFade);
        } else {
          DrawProceduralShardUV(trailScreenPos, trailRadius, woodPool[i].angle, trailFade);
        }
      }
    }

    if (woodPool[i].type == PARTICLE_LEAF) {
      DrawProceduralLeafUV(screenPos,
                           woodPool[i].radius * (0.3f + 0.7f * lifeRatio) * depthFactor,
                           woodPool[i].angle, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_POLLEN) {
      DrawProceduralFlowerUV(screenPos,
                             woodPool[i].radius * (0.5f + 0.5f * lifeRatio) * depthFactor,
                             0.0f, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_WOOD_SHARD) {
      DrawProceduralShardUV(screenPos, woodPool[i].radius * depthFactor,
                            woodPool[i].angle, lifeRatio);
    } else if (woodPool[i].type == PARTICLE_FLOWER) {
      float sizeMult = smoothstep(1.0f, 0.7f, lifeRatio);
      DrawProceduralFlowerUV(screenPos,
                             woodPool[i].radius * sizeMult * depthFactor, woodPool[i].angle,
                             lifeRatio);
    } else if (woodPool[i].type == PARTICLE_ENERGY_FLASH) {
      DrawProceduralEnergyFlashUV(screenPos, woodPool[i].radius * depthFactor,
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

  // Bloom
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

  // Lớp chính
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

int GetWoodSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
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
          float speed = Math_Mix(100.0f, 300.0f, Random01()) * emitters[i].sizeScale;
          float pitch = (Random01() - 0.5f) * PI;
          Vector3 vel = {
              cosf(angle) * speed * cosf(pitch),
              sinf(pitch) * speed,
              sinf(angle) * speed * cosf(pitch)
          };
          SpawnWoodParticle(PARTICLE_WOOD_SHARD, impactPos, vel,
                            Math_Mix(5.0f, 12.0f, Random01()) * emitters[i].sizeScale,
                            Math_Mix(0.4f, 0.8f, Random01()), Random01() * PI,
                            (Random01() - 0.5f) * 2.0f);
        }
        break;
      }
      count++;
    }
  }
}
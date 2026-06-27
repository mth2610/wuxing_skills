#include "electric_skill.h"
#include "force_field.h"
#include "particle_system.h"
#include "raymath.h"
#include "ribbon_strip.h"
#include "rlgl.h"
#include "skill_manager.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_EMITTERS 10

// --- Force Fields của Electric Skill ---
static ForceField s_electricSparkField; // tia lửa nổ: Perlin jấy giật mạnh
static ForceField s_electricArcField; // tia điện bốc lên: Perlin + gravity nhẹ

// ---- Electric Skill Travel Settings ----
#define ELECTRIC_TRAVEL_SPEED 1.6f
#define ELECTRIC_SHOCK_DURATION 0.9f

// ---- Emitter Structure ----
typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 currentPos;
  float progress;
  float durationTimer;
  float spawnAccumulator;
  float erraticTimer;
  bool impacted;
  float lightningFlashTimer;
  Vector3 lightningPath[24];
  int lightningPathCount;
  Vector3 branchPath1[12];
  int branchPath1Count;
  Vector3 branchPath2[12];
  int branchPath2Count;
  float sizeScale;
} ElectricEmitter;

static ElectricEmitter emitters[MAX_EMITTERS];

static Shader electricShader;
static int timeLoc;

extern Camera3D camera;

// ---- Math Helpers ----
static float Random01(void) {
  return (float)GetRandomValue(0, 10000) / 10000.0f;
}

// Thuật toán midpoint displacement 3D tạo đường sét ngoằn ngoèo
static void GenerateJaggedPath(Vector3 start, Vector3 end, Vector3 *outPoints,
                               int *outCount, int maxPoints,
                               float maxDisplacement) {
  if (maxPoints < 2)
    return;
  outPoints[0] = start;
  outPoints[maxPoints - 1] = end;
  *outCount = maxPoints;

  Vector3 dir = Vector3Subtract(end, start);
  float length = Vector3Length(dir);
  if (length < 1.0f) {
    *outCount = 2;
    outPoints[0] = start;
    outPoints[1] = end;
    return;
  }

  Vector3 perp = (Vector3){-dir.z / length, 0.0f, dir.x / length};
  if (Vector3Length(perp) == 0.0f)
    perp = (Vector3){0.0f, 0.0f, 1.0f};
  perp = Vector3Normalize(perp);
  Vector3 up = Vector3Normalize(Vector3CrossProduct(dir, perp));

  for (int i = 1; i < maxPoints - 1; i++) {
    float t = (float)i / (maxPoints - 1);
    Vector3 basePos = Vector3Add(start, Vector3Scale(dir, t));
    float envelope = sinf(t * 3.14159265f);

    float dispVal = ((float)GetRandomValue(-1000, 1000) / 1000.0f) *
                    maxDisplacement * envelope;
    float dispVal2 = ((float)GetRandomValue(-1000, 1000) / 1000.0f) *
                     maxDisplacement * envelope;

    Vector3 offset =
        Vector3Add(Vector3Scale(perp, dispVal), Vector3Scale(up, dispVal2));
    outPoints[i] = Vector3Add(basePos, offset);
  }
}

void InitElectricSkill(int screenWidth, int screenHeight) {
  electricShader = LoadShader(0, "electric.fs");
  timeLoc = GetShaderLocation(electricShader, "u_time");

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  // Spark jấy giật mạnh: Perlin noise nhanh + không trọng lực
  ForceField_Clear(&s_electricSparkField);
  ForceField_AddLayer(&s_electricSparkField,
                      (ForceLayer){
                          .type = FORCE_NOISE_PERLIN,
                          .strength = 80.0f,
                          .noiseScale = 0.025f,
                          .noiseSpeed = 3.0f // speed cao = jấy giật nhanh
                      });

  // Tia điện bốc lên sau va chạm: Perlin + trọng lực nhẹ kéo xuống
  ForceField_Clear(&s_electricArcField);
  ForceField_AddLayer(&s_electricArcField,
                      (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                   .strength = 60.0f,
                                   .noiseScale = 0.020f,
                                   .noiseSpeed = 2.5f});
  ForceField_AddLayer(&s_electricArcField,
                      (ForceLayer){.type = FORCE_GRAVITY_DIR,
                                   .direction = {0, -1, 0},
                                   .strength = 100.0f});
}

void CastElectricSkill(Vector3 startPos, Vector3 target, float sizeScale) {
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].currentPos = startPos;
      emitters[i].progress = 0.0f;
      emitters[i].durationTimer = ELECTRIC_SHOCK_DURATION;
      emitters[i].spawnAccumulator = 0.0f;
      emitters[i].erraticTimer = 0.0f;
      emitters[i].impacted = false;
      emitters[i].lightningFlashTimer = 0.0f;
      emitters[i].lightningPathCount = 0;
      emitters[i].branchPath1Count = 0;
      emitters[i].branchPath2Count = 0;
      emitters[i].sizeScale = sizeScale;
      break;
    }
  }

  // Nổ tia lửa điện tung tóe khi xuất chiêu
  int burstCount = 15 * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    float angle = Random01() * 2.0f * PI;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = (100.0f + Random01() * 250.0f) * sizeScale;
    Vector3 vel = {cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                   sinf(angle) * speed * cosf(pitch)};

    ParticleConfig p = {0};
    p.position = startPos;
    p.velocity = vel;
    p.drag = 1.2f;
    p.radius = (3.0f + Random01() * 4.0f) * sizeScale;
    p.lifetime = 0.4f + Random01() * 0.4f;
    p.colorStart = (Color){150, 200, 255, 255};
    p.colorEnd = (Color){20, 50, 255, 0};
    p.physicsFlags = P_PHYSICS_DRAG;
    p.forceField = &s_electricSparkField;
    SpawnParticle(p);
  }
}

void UpdateElectricSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    emitters[e].erraticTimer += dt;

    if (!emitters[e].impacted) {
      emitters[e].progress += dt * ELECTRIC_TRAVEL_SPEED;

      if (emitters[e].progress >= 1.0f) {
        emitters[e].progress = 1.0f;
        emitters[e].impacted = true;
        emitters[e].currentPos = emitters[e].targetPos;

        float scale = emitters[e].sizeScale;

        // Sóng kích nổ mặt đất (Bắn các hạt plasma bay ngang dẹp)
        for (int s = 0; s < 35 * scale; s++) {
          float angle = Random01() * 2.0f * PI;
          Vector3 vel = {cosf(angle) * 350.0f * scale, 0,
                         sinf(angle) * 350.0f * scale};

          ParticleConfig shock = {0};
          shock.position = emitters[e].targetPos;
          shock.velocity = vel;
          shock.drag = 3.5f;
          shock.radius = 8.0f * scale;
          shock.lifetime = 0.5f;
          shock.colorStart = (Color){200, 255, 255, 255};
          shock.colorEnd = (Color){50, 100, 255, 0};
          shock.physicsFlags = P_PHYSICS_DRAG;
          SpawnParticle(shock);
        }

        // Cầu plasma nổ rực tại tâm
        ParticleConfig core = {0};
        core.position = emitters[e].targetPos;
        core.velocity = (Vector3){0, 0, 0};
        core.radius = 80.0f * scale;
        core.lifetime = 0.3f;
        core.colorStart = (Color){255, 255, 255, 255};
        core.colorEnd = (Color){50, 150, 255, 0};
        SpawnParticle(core);

        // Tia điện nổ văng lên cao
        int sparkCount = 35 * scale;
        for (int s = 0; s < sparkCount; s++) {
          float angle = Random01() * 2.0f * PI;
          float pitch = (Random01() - 0.5f) * PI;
          float speed = (150.0f + Random01() * 450.0f) * scale;
          Vector3 vel = {cosf(angle) * speed * cosf(pitch),
                         sinf(pitch) * speed + 100.0f,
                         sinf(angle) * speed * cosf(pitch)};

          ParticleConfig p = {0};
          p.position = emitters[e].targetPos;
          p.velocity = vel;
          p.drag = 1.0f;
          p.radius = (2.5f + Random01() * 3.5f) * scale;
          p.lifetime = 0.5f + Random01() * 0.5f;
          p.colorStart = (Color){150, 220, 255, 255};
          p.colorEnd = (Color){10, 30, 255, 0};
          p.physicsFlags = P_PHYSICS_DRAG;
          p.forceField = &s_electricArcField;
          SpawnParticle(p);
        }

        // Tạo đường sét thiên kiếp đánh từ trên cao
        Vector3 skyPos = {
            emitters[e].targetPos.x + (float)GetRandomValue(-80, 80) * scale,
            emitters[e].targetPos.y + 600.0f,
            emitters[e].targetPos.z + (float)GetRandomValue(-80, 80) * scale};
        GenerateJaggedPath(skyPos, emitters[e].targetPos,
                           emitters[e].lightningPath,
                           &emitters[e].lightningPathCount, 16, 35.0f * scale);

        if (emitters[e].lightningPathCount > 8) {
          Vector3 bStart = emitters[e].lightningPath[8];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150) * scale,
                          bStart.y - (float)GetRandomValue(50, 200) * scale,
                          bStart.z + (float)GetRandomValue(-150, 150) * scale};
          GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1,
                             &emitters[e].branchPath1Count, 8, 15.0f * scale);
        }
        if (emitters[e].lightningPathCount > 12) {
          Vector3 bStart = emitters[e].lightningPath[12];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150) * scale,
                          bStart.y - (float)GetRandomValue(50, 200) * scale,
                          bStart.z + (float)GetRandomValue(-150, 150) * scale};
          GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2,
                             &emitters[e].branchPath2Count, 8, 15.0f * scale);
        }
      } else {
        // Đạn cầu sét bay tới mục tiêu
        Vector3 basePos = Vector3Lerp(
            emitters[e].startPos, emitters[e].targetPos, emitters[e].progress);
        Vector3 dir = Vector3Normalize(
            Vector3Subtract(emitters[e].targetPos, emitters[e].startPos));
        Vector3 perp = (Vector3){-dir.z, 0.0f, dir.x};
        if (Vector3Length(perp) == 0.0f)
          perp = (Vector3){0, 0, 1};
        perp = Vector3Normalize(perp);

        float wobble = sinf(emitters[e].progress * 25.0f) * 20.0f *
                       (1.0f - emitters[e].progress);
        emitters[e].currentPos =
            Vector3Add(basePos, Vector3Scale(perp, wobble));

        // Sinh hạt plasma tạo ảo giác cầu điện liên tục
        ParticleConfig orb = {0};
        orb.position = emitters[e].currentPos;
        orb.velocity = (Vector3){0, 0, 0};
        orb.radius = 22.0f * emitters[e].sizeScale;
        orb.lifetime = 0.12f;
        orb.colorStart = (Color){200, 255, 255, 200};
        orb.colorEnd = (Color){50, 100, 255, 0};
        SpawnParticle(orb);

        emitters[e].spawnAccumulator += dt;
        if (emitters[e].spawnAccumulator >= 0.015f) {
          Vector3 backVel = Vector3Scale(dir, -50.0f);
          Vector3 spreadVel = {(float)GetRandomValue(-30, 30),
                               (float)GetRandomValue(-30, 30),
                               (float)GetRandomValue(-30, 30)};

          ParticleConfig trail = {0};
          trail.position = emitters[e].currentPos;
          trail.velocity = Vector3Add(backVel, spreadVel);
          trail.drag = 1.0f;
          trail.radius = (3.0f + Random01() * 3.0f) * emitters[e].sizeScale;
          trail.lifetime = 0.3f + Random01() * 0.3f;
          trail.colorStart = (Color){150, 200, 255, 255};
          trail.colorEnd = (Color){20, 50, 255, 0};
          trail.physicsFlags = P_PHYSICS_DRAG;
          trail.forceField = &s_electricSparkField;
          SpawnParticle(trail);

          emitters[e].spawnAccumulator = 0.0f;
        }
      }
    } else {
      // GIAI ĐOẠN ĐIỆN GIẬT TẠI CHỖ (SHOCK PHASE)
      emitters[e].durationTimer -= dt;
      if (emitters[e].durationTimer <= 0.0f) {
        emitters[e].active = false;
        continue;
      }

      emitters[e].lightningFlashTimer += dt;
      if (emitters[e].lightningFlashTimer >= 0.05f) {
        emitters[e].lightningFlashTimer = 0.0f;
        Vector3 skyPos = {
            emitters[e].targetPos.x + (float)GetRandomValue(-80, 80),
            emitters[e].targetPos.y + 600.0f,
            emitters[e].targetPos.z + (float)GetRandomValue(-80, 80)};
        GenerateJaggedPath(skyPos, emitters[e].targetPos,
                           emitters[e].lightningPath,
                           &emitters[e].lightningPathCount, 16, 35.0f);

        if (emitters[e].lightningPathCount > 8) {
          Vector3 bStart = emitters[e].lightningPath[8];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150),
                          bStart.y - (float)GetRandomValue(50, 200),
                          bStart.z + (float)GetRandomValue(-150, 150)};
          GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath1,
                             &emitters[e].branchPath1Count, 8, 15.0f);
        }
        if (emitters[e].lightningPathCount > 12) {
          Vector3 bStart = emitters[e].lightningPath[12];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150),
                          bStart.y - (float)GetRandomValue(50, 200),
                          bStart.z + (float)GetRandomValue(-150, 150)};
          GenerateJaggedPath(bStart, bEnd, emitters[e].branchPath2,
                             &emitters[e].branchPath2Count, 8, 15.0f);
        }
      }

      if (GetRandomValue(1, 10) <= 6) {
        float angle = Random01() * 2.0f * PI;
        float pitch = (Random01() - 0.5f) * PI;
        float speed = 80.0f + Random01() * 250.0f;

        ParticleConfig p = {0};
        p.position = emitters[e].targetPos;
        p.velocity =
            (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                      sinf(angle) * speed * cosf(pitch)};
        p.drag = 1.0f;
        p.radius = 2.0f + Random01() * 3.0f;
        p.lifetime = 0.2f + Random01() * 0.3f;
        p.colorStart = (Color){200, 255, 255, 255};
        p.colorEnd = (Color){20, 50, 255, 0};
        p.physicsFlags = P_PHYSICS_DRAG;
        p.forceField = &s_electricSparkField;
        SpawnParticle(p);
      }
    }
  }
}

void DrawElectricSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && emitters[i].impacted) {
      active = true;
      break;
    }
  }
  if (!active)
    return;

  float time = (float)GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  SetShaderValue(electricShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(electricShader);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || !emitters[e].impacted)
      continue;

    float lifeRatio = emitters[e].durationTimer / ELECTRIC_SHOCK_DURATION;
    if (lifeRatio > 0.3f) {
      float boltFade = Clamp((lifeRatio - 0.3f) / 0.7f, 0.0f, 1.0f);

      // Vẽ dải 3D Ribbon cho tia sét chính
      if (emitters[e].lightningPathCount > 1) {
        RibbonPoint strip[24];
        float width = 22.0f * boltFade * emitters[e].sizeScale;
        for (int i = 0; i < emitters[e].lightningPathCount; i++) {
          strip[i].position = emitters[e].lightningPath[i];
          strip[i].halfWidth = width * 0.5f;
          float v = (float)i / (emitters[e].lightningPathCount - 1);
          strip[i].v = v;
          strip[i].tint = (Color){(unsigned char)(v * 255.0f), 128, 255,
                                  (unsigned char)(255.0f * boltFade)};
        }
        DrawRibbonStrip(strip, emitters[e].lightningPathCount, (Texture2D){0},
                        camera);
      }

      // Vẽ nhánh rẽ 1
      if (emitters[e].branchPath1Count > 1) {
        RibbonPoint strip[12];
        float width = 10.0f * boltFade * emitters[e].sizeScale;
        for (int i = 0; i < emitters[e].branchPath1Count; i++) {
          strip[i].position = emitters[e].branchPath1[i];
          strip[i].halfWidth = width * 0.5f;
          float v = (float)i / (emitters[e].branchPath1Count - 1);
          strip[i].v = v;
          strip[i].tint = (Color){(unsigned char)(v * 255.0f), 128, 255,
                                  (unsigned char)(255.0f * boltFade)};
        }
        DrawRibbonStrip(strip, emitters[e].branchPath1Count, (Texture2D){0},
                        camera);
      }

      // Vẽ nhánh rẽ 2
      if (emitters[e].branchPath2Count > 1) {
        RibbonPoint strip[12];
        float width = 10.0f * boltFade * emitters[e].sizeScale;
        for (int i = 0; i < emitters[e].branchPath2Count; i++) {
          strip[i].position = emitters[e].branchPath2[i];
          strip[i].halfWidth = width * 0.5f;
          float v = (float)i / (emitters[e].branchPath2Count - 1);
          strip[i].v = v;
          strip[i].tint = (Color){(unsigned char)(v * 255.0f), 128, 255,
                                  (unsigned char)(255.0f * boltFade)};
        }
        DrawRibbonStrip(strip, emitters[e].branchPath2Count, (Texture2D){0},
                        camera);
      }
    }
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadElectricSkill(void) { UnloadShader(electricShader); }

bool IsElectricSkillShocking(void) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (emitters[e].active && emitters[e].impacted) {
      return true;
    }
  }
  return false;
}

int GetElectricSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && !emitters[i].impacted && count < maxProjectiles) {
      outProjectiles[count].position = emitters[i].currentPos;
      outProjectiles[count].radius = 18.0f * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateElectricProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && !emitters[i].impacted) {
      if (count == index) {
        emitters[i].progress = 1.0f;
        emitters[i].impacted = true;

        float scale = emitters[i].sizeScale;

        for (int s = 0; s < 35 * scale; s++) {
          float angle = Random01() * 2.0f * PI;
          Vector3 vel = {cosf(angle) * 350.0f * scale, 0,
                         sinf(angle) * 350.0f * scale};
          ParticleConfig shock = {0};
          shock.position = emitters[i].currentPos;
          shock.velocity = vel;
          shock.drag = 3.5f;
          shock.radius = 8.0f * scale;
          shock.lifetime = 0.5f;
          shock.colorStart = (Color){200, 255, 255, 255};
          shock.colorEnd = (Color){50, 100, 255, 0};
          shock.physicsFlags = P_PHYSICS_DRAG;
          SpawnParticle(shock);
        }

        int sparkCount = 35 * scale;
        for (int s = 0; s < sparkCount; s++) {
          float angle = Random01() * 2.0f * PI;
          float pitch = (Random01() - 0.5f) * PI;
          float speed = (150.0f + Random01() * 450.0f) * scale;
          Vector3 vel = {cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                         sinf(angle) * speed * cosf(pitch)};

          ParticleConfig p = {0};
          p.position = emitters[i].currentPos;
          p.velocity = vel;
          p.drag = 1.0f;
          p.radius = (2.5f + Random01() * 3.5f) * scale;
          p.lifetime = 0.5f + Random01() * 0.5f;
          p.colorStart = (Color){150, 200, 255, 255};
          p.colorEnd = (Color){20, 50, 255, 0};
          p.physicsFlags = P_PHYSICS_DRAG;
          p.forceField = &s_electricArcField;
          SpawnParticle(p);
        }

        Vector3 skyPos = {
            emitters[i].currentPos.x + (float)GetRandomValue(-80, 80) * scale,
            emitters[i].currentPos.y + 600.0f,
            emitters[i].currentPos.z + (float)GetRandomValue(-80, 80) * scale};
        GenerateJaggedPath(skyPos, emitters[i].currentPos,
                           emitters[i].lightningPath,
                           &emitters[i].lightningPathCount, 16, 35.0f * scale);

        if (emitters[i].lightningPathCount > 8) {
          Vector3 bStart = emitters[i].lightningPath[8];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150) * scale,
                          bStart.y - (float)GetRandomValue(50, 200) * scale,
                          bStart.z + (float)GetRandomValue(-150, 150) * scale};
          GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath1,
                             &emitters[i].branchPath1Count, 8, 15.0f * scale);
        }
        if (emitters[i].lightningPathCount > 12) {
          Vector3 bStart = emitters[i].lightningPath[12];
          Vector3 bEnd = {bStart.x + (float)GetRandomValue(-150, 150) * scale,
                          bStart.y - (float)GetRandomValue(50, 200) * scale,
                          bStart.z + (float)GetRandomValue(-150, 150) * scale};
          GenerateJaggedPath(bStart, bEnd, emitters[i].branchPath2,
                             &emitters[i].branchPath2Count, 8, 15.0f * scale);
        }

        emitters[i].targetPos = emitters[i].currentPos;
        break;
      }
      count++;
    }
  }
}
#include "skills/taiji/wind_storm/wind_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "raymath.h"
#include "core/ribbon_strip.h"
#include "rlgl.h"
#include "core/skill_manager.h"
#include <math.h>
#include <stddef.h>

#define MAX_EMITTERS 5
#define TORNADO_RIBBONS 5
#define RIBBON_SEGMENTS 40

// --- Force Fields của Wind Skill ---
static ForceField s_tornadoField;   // lốc: vortex quanh trục Y + curl noise + drag 0.5
static ForceField s_disperseField;  // tán nước: Perlin loạn xạ khi tan + drag 3.0
static ForceField s_castBurstField; // hạt tạo ban đầu: chỉ drag 2.5

typedef struct {
  bool active;
  Vector3 position;
  float radius;
  float maxLifetime;
  float lifetime;
  float pullStrength;
  float sizeScale;
  float spawnAccumulator;
  float angleOffsets[TORNADO_RIBBONS];
} WindEmitter;

static WindEmitter emitters[MAX_EMITTERS];

static Shader windShader;
static int timeLoc;

extern Camera3D camera;

static float Random01(void) {
  return (float)GetRandomValue(0, 10000) / 10000.0f;
}

static float smoothstep(float edge0, float edge1, float x) {
  float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void InitWindSkill(int screenWidth, int screenHeight) {
  windShader = LoadShader(0, "skills/taiji/wind_storm/wind.fs");
  timeLoc = GetShaderLocation(windShader, "u_time");

  for (int i = 0; i < MAX_EMITTERS; i++) {
    emitters[i].active = false;
  }

  // Lốc xoáy: vortex quanh trục Y + curl đẩy hạt theo làn sóng gió + drag 0.5
  ForceField_Clear(&s_tornadoField);
  ForceField_AddLayer(&s_tornadoField, (ForceLayer){
    .type      = FORCE_VORTEX,
    .origin    = {0,0,0},          // cập nhật dynamic khi spawn
    .direction = {0,1,0},          // trục Y
    .strength  = 400.0f,
  });
  ForceField_AddLayer(&s_tornadoField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = 40.0f,
    .noiseScale = 0.012f, .noiseSpeed = 1.0f
  });
  ForceField_AddLayer(&s_tornadoField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 0.5f
  });

  // Gió tán khi lốc kết thúc: Perlin loạn xạ + drag 3.0
  ForceField_Clear(&s_disperseField);
  ForceField_AddLayer(&s_disperseField, (ForceLayer){
    .type = FORCE_NOISE_PERLIN, .strength = 50.0f,
    .noiseScale = 0.010f, .noiseSpeed = 0.8f
  });
  ForceField_AddLayer(&s_disperseField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 3.0f
  });

  // Hạt tạo ban đầu: chỉ drag 2.5
  ForceField_Clear(&s_castBurstField);
  ForceField_AddLayer(&s_castBurstField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 2.5f
  });
}

void CastWindSkill(Vector3 startPos, Vector3 target, SkillParams params) {
  float sizeScale = params.sizeScale;

  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].position = target;
      emitters[i].position.y = 0.1f;
      emitters[i].radius = 100.0f * sizeScale;
      emitters[i].maxLifetime = 4.0f;
      emitters[i].lifetime = 4.0f;
      emitters[i].pullStrength = 300.0f;
      emitters[i].sizeScale = sizeScale;
      emitters[i].spawnAccumulator = 0.0f;

      for (int r = 0; r < TORNADO_RIBBONS; r++) {
        emitters[i].angleOffsets[r] =
            (float)r * ((2.0f * PI) / TORNADO_RIBBONS);
      }

      for (int s = 0; s < 20; s++) {
        float angle = Random01() * 2.0f * PI;
        float speed = 150.0f * sizeScale;
        ParticleConfig p = {0};
        p.position = target;
        p.position.y += 5.0f;
        p.velocity = (Vector3){cosf(angle) * speed, 10.0f, sinf(angle) * speed};
        p.radius = (5.0f + Random01() * 8.0f) * sizeScale;
        p.lifetime = 0.4f;
        p.colorStart = (Color){150, 200, 255, 180}; // Giảm độ gắt của hạt
        p.colorEnd = (Color){50, 100, 200, 0};
        p.forceField = &s_castBurstField;
        SpawnParticle(p);
      }
      break;
    }
  }
}

void UpdateWindSkill(float dt) {
  float time = (float)GetTime();

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    emitters[e].lifetime -= dt;
    if (emitters[e].lifetime <= 0.0f) {
      emitters[e].active = false;

      for (int s = 0; s < 20; s++) {
        float angle = Random01() * 2.0f * PI;
        float pitch = (Random01() - 0.5f) * PI;
        float speed = 200.0f * emitters[e].sizeScale;
        ParticleConfig p = {0};
        p.position = emitters[e].position;
        p.position.y += Random01() * 100.0f * emitters[e].sizeScale;
        p.velocity =
            (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                      sinf(angle) * speed * cosf(pitch)};
        p.radius = (8.0f + Random01() * 12.0f) * emitters[e].sizeScale;
        p.lifetime = 0.5f;
        p.colorStart = (Color){180, 220, 255, 120};
        p.colorEnd = (Color){50, 100, 200, 0};
        p.forceField = &s_disperseField;
        SpawnParticle(p);
      }
      continue;
    }

    for (int r = 0; r < TORNADO_RIBBONS; r++) {
      // CẢI TIẾN: Thêm nhiễu sóng sin vào tốc độ xoay để lốc giật từng cơn
      emitters[e].angleOffsets[r] +=
          (10.0f + sinf(time * 6.0f + r) * 4.0f) * dt;
    }

    emitters[e].spawnAccumulator += dt;
    float spawnRate = 0.015f;
    while (emitters[e].spawnAccumulator >= spawnRate) {
      float spawnRadius = GetRandomValue(15, 90) * emitters[e].sizeScale;
      float spawnAngle = Random01() * 2.0f * PI;
      Vector3 pos = emitters[e].position;
      pos.x += cosf(spawnAngle) * spawnRadius;
      pos.y += Random01() * 15.0f;
      pos.z += sinf(spawnAngle) * spawnRadius;

      Vector3 toCenter = Vector3Subtract(emitters[e].position, pos);
      Vector3 perpDir = Vector3Normalize((Vector3){-toCenter.z, 0, toCenter.x});

      float speedXY = 180.0f * emitters[e].sizeScale;
      float speedUp = (80.0f + Random01() * 150.0f) * emitters[e].sizeScale;
      Vector3 vel =
          Vector3Add(Vector3Scale(perpDir, speedXY), (Vector3){0, speedUp, 0});

      Vector3 suckForce = Vector3Scale(Vector3Normalize(toCenter), 400.0f);

      // Cập nhật origin của vortex layer theo vị trí emitter hiện tại
      s_tornadoField.layers[0].origin = emitters[e].position;

      ParticleConfig p = {0};
      p.position = pos;
      p.velocity = vel;
      p.radius = (2.0f + Random01() * 5.0f) * emitters[e].sizeScale;
      p.lifetime = 0.6f + Random01() * 0.6f;

      if (GetRandomValue(1, 10) <= 3) {
        p.colorStart = (Color){180, 180, 160, 150};
        p.colorEnd = (Color){80, 80, 60, 0};
      } else {
        p.colorStart = (Color){150, 200, 255, 80};
        p.colorEnd = (Color){50, 100, 200, 0};
      }

      p.forceField = &s_tornadoField;
      SpawnParticle(p);

      emitters[e].spawnAccumulator -= spawnRate;
    }
  }
}

void DrawWindSkill(void) {
  bool active = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      active = true;
      break;
    }
  }
  if (!active)
    return;

  float time = (float)GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  SetShaderValue(windShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(windShader);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float fadeOut = smoothstep(0.0f, 0.4f, emitters[e].lifetime);
    float fadeIn =
        smoothstep(emitters[e].maxLifetime, emitters[e].maxLifetime - 0.2f,
                   emitters[e].lifetime);
    float alphaMult = fadeOut * fadeIn;

    for (int r = 0; r < TORNADO_RIBBONS; r++) {
      RibbonPoint strip[RIBBON_SEGMENTS];
      float baseAngle = emitters[e].angleOffsets[r];

      for (int i = 0; i < RIBBON_SEGMENTS; i++) {
        float t = (float)i / (float)(RIBBON_SEGMENTS - 1);

        float height = t * 220.0f * emitters[e].sizeScale;

        // CẢI TIẾN 1: Bán kính nhấp nhô không đều (Pulse)
        float pulse =
            sinf(t * 15.0f - time * 12.0f) * 6.0f * emitters[e].sizeScale;
        float currentRad = (8.0f + t * 80.0f + pulse) * emitters[e].sizeScale;

        // CẢI TIẾN 2: Giảm số vòng xoắn xuống để dải lốc thoáng hơn
        float angle = baseAngle + t * PI * 4.5f;

        // CẢI TIẾN 3: Tạo độ uốn éo (Wobble/Bending) cho thân lốc xoáy
        float bendX =
            sinf(t * 6.0f + time * 4.0f) * 25.0f * t * emitters[e].sizeScale;
        float bendZ =
            cosf(t * 5.0f + time * 3.5f) * 25.0f * t * emitters[e].sizeScale;

        Vector3 pos = emitters[e].position;
        pos.x += cosf(angle) * currentRad + bendX;
        pos.y += height;
        pos.z += sinf(angle) * currentRad + bendZ;

        strip[i].position = pos;
        strip[i].halfWidth = (8.0f + t * 20.0f) * emitters[e].sizeScale;
        strip[i].v = t;

        // CẢI TIẾN 4: Giảm Alpha từ 255 xuống 110 để bớt cháy sáng (Blow-out)
        strip[i].tint = (Color){(unsigned char)(t * 255.0f), 128, 80,
                                (unsigned char)(110.0f * alphaMult)};
      }

      DrawRibbonStrip(strip, RIBBON_SEGMENTS, (Texture2D){0}, camera);
    }
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadWindSkill(void) { UnloadShader(windShader); }

bool GetWindPullForce(Vector3 *outPullCenter, float *outPullStrength) {
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      *outPullCenter = emitters[i].position;
      *outPullStrength = emitters[i].pullStrength * emitters[i].sizeScale;
      return true;
    }
  }
  return false;
}
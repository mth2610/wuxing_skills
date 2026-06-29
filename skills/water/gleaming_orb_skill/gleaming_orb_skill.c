#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/force_field.h"
#include "core/impact_burst.h"
#include "core/particle_radial_burst.h"
#include "core/particle_system.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "raymath.h"
#include "rlgl.h"
#include "skills/water/gleaming_orb_skill/gleaming_orb_skill.h"

#define MAX_ORBS 8

/* Cấu trúc quản lý trạng thái quả cầu */
typedef struct {
  Vector3 position;
  Vector3 velocity;
  Vector3 target;
  float radius;
  float baseRadius;
  float lifeTime;
  float maxLife;
  float damage;
  float scaleRandomizer;
  float seed;
  bool active;
} WaterOrbInstance;

/* Bộ nhớ tĩnh tuân thủ ràng buộc hệ thống */
static WaterOrbInstance s_orbs[MAX_ORBS];
static float s_globalTime = 0.0f;
static Shader s_waterShader;

void InitGleamingOrbSkill(int screenWidth, int screenHeight) {
  s_globalTime = 0.0f;
  for (int i = 0; i < MAX_ORBS; i++) {
    s_orbs[i].active = false;
  }

  /* Tải Shader tùy biến xử lý hiệu ứng long lanh/Fresnel phản chiếu */
  s_waterShader = ResourceManager_LoadShader(
      "skills/water/gleaming_orb_skill/gleaming_orb.vs",
      "skills/water/gleaming_orb_skill/gleaming_orb.fs");
}

void CastGleamingOrbSkill(Vector3 startPos, Vector3 target,
                          SkillParams params) {
  for (int i = 0; i < MAX_ORBS; i++) {
    if (!s_orbs[i].active) {
      s_orbs[i].position = startPos;

      Vector3 dir = Vector3Subtract(target, startPos);
      float dist = Vector3Length(dir);
      if (dist > 0.001f) {
        dir = Vector3Scale(dir, 1.0f / dist);
      } else {
        dir = (Vector3){0.0f, 0.0f, 1.0f};
      }

      float projSpeed = 14.0f;
      s_orbs[i].velocity = Vector3Scale(dir, projSpeed);
      s_orbs[i].target = target;
      s_orbs[i].baseRadius = 1.3f * params.sizeScale;
      s_orbs[i].radius = s_orbs[i].baseRadius;
      s_orbs[i].maxLife = (dist / projSpeed) * 1.2f;
      if (s_orbs[i].maxLife > 4.5f)
        s_orbs[i].maxLife = 4.5f;
      s_orbs[i].lifeTime = 0.0f;
      s_orbs[i].damage = params.damage;

      /* Quy luật bất đối xứng: Ngẫu nhiên hóa thực thể (Rule 12.3) */
      s_orbs[i].scaleRandomizer = ((float)GetRandomValue(85, 115) / 100.0f);
      s_orbs[i].seed = (float)GetRandomValue(0, 1000) / 10.0f;

      s_orbs[i].active = true;
      break;
    }
  }
}

void UpdateGleamingOrbSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  s_globalTime += dt;

  for (int i = 0; i < MAX_ORBS; i++) {
    if (!s_orbs[i].active)
      continue;

    s_orbs[i].lifeTime += dt;

    /* 1. Xử lý quỹ đạo Jitter tạo sự tự nhiên (Rule 12.2) */
    Vector3 forward = Vector3Normalize(s_orbs[i].velocity);
    Vector3 perp = (Vector3){-forward.z, 0.0f, forward.x};
    float jitterFreq = 8.0f;
    float jitterAmp = 0.4f;
    float jitter = sinf(s_globalTime * jitterFreq + s_orbs[i].seed) * jitterAmp;

    Vector3 currentVel =
        Vector3Add(s_orbs[i].velocity, Vector3Scale(perp, jitter));
    s_orbs[i].position =
        Vector3Add(s_orbs[i].position, Vector3Scale(currentVel, dt));

    /* 2. Giả lập hiệu ứng "Núng nính" - Wobble đa tần số */
    float wobbleTime = s_globalTime * 13.5f + s_orbs[i].seed;
    float wobbleFactor =
        1.0f + 0.14f * sinf(wobbleTime) * cosf(wobbleTime * 0.65f);
    s_orbs[i].radius =
        s_orbs[i].baseRadius * s_orbs[i].scaleRandomizer * wobbleFactor;

    /* 3. Phát bụi hạt nước/bọt khí liên tục khi di chuyển (VFX Standard) */
    ParticleConfig bubbleConfig = {0};
    bubbleConfig.position = Vector3Add(
        s_orbs[i].position, (Vector3){(float)GetRandomValue(-3, 3) * 0.1f,
                                      (float)GetRandomValue(-3, 3) * 0.1f,
                                      (float)GetRandomValue(-3, 3) * 0.1f});
    bubbleConfig.velocity = (Vector3){(float)GetRandomValue(-15, 15) * 0.1f,
                                      (float)GetRandomValue(5, 20) * 0.1f,
                                      (float)GetRandomValue(-15, 15) * 0.1f};
    bubbleConfig.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.7f);
    bubbleConfig.colorEnd = ColorAlpha(WHITE, 0.0f);
    bubbleConfig.radius = s_orbs[i].radius * 0.18f;
    bubbleConfig.lifetime = 0.5f;
    SpawnParticle(bubbleConfig);

    /* 4. Kiểm tra va chạm vật lý */
    bool hitTarget = false;
    if (Vector3Distance(s_orbs[i].position, enemyPos) <=
        (s_orbs[i].radius + enemyRadius)) {
      hitTarget = true;
    }

    /* 5. Xử lý vụ nổ nước khi va chạm hoặc hết hạn sinh mệnh */
    if (hitTarget || s_orbs[i].lifeTime >= s_orbs[i].maxLife) {
      ApplyAoEDamage(s_orbs[i].position, s_orbs[i].radius * 2.8f,
                     s_orbs[i].damage, 14.0f);
      CameraFX_Shake(0.45f); /* Rung chấn vừa */

      /* Khởi tạo cấu trúc vụ nổ tích hợp ImpactBurst */
      static ColorGradient splashGrad;
      ColorGradient_StandardFade(&splashGrad, ELEMENT_COLOR_WATER, 0.35f, 0.4f);

      ImpactBurstConfig impact = {0};
      impact.distortEnabled = true;
      impact.distortRadius = s_orbs[i].radius * 3.5f;
      impact.distortStrength = 0.22f;
      impact.distortLife = 0.45f;
      impact.distortSpeed = 3.2f;

      impact.decalEnabled = true;
      impact.decalTex =
          ResourceManager_LoadTexture("assets/textures/water_splash_decal.png");
      impact.decalScale = 3.5f;
      impact.decalLife = 1.8f;
      impact.decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
      impact.decalRandomRotation = true;

      impact.lightEnabled = true;
      impact.lightColor =
          ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.6f); /* Ánh sáng óng ánh */
      impact.lightRadius = s_orbs[i].radius * 4.5f;
      impact.lightLife = 0.35f;

      impact.particlesEnabled = true;
      impact.particles.countMin = 30;
      impact.particles.countMax = 50;
      impact.particles.speedMin = 5.0f;
      impact.particles.speedMax = 12.0f;
      impact.particles.radiusMin = 0.08f;
      impact.particles.radiusMax = 0.35f;
      impact.particles.lifetimeMin = 0.4f;
      impact.particles.lifetimeMax = 0.9f;
      impact.particles.pitchRange = 50.0f;
      impact.particles.upwardBias = 2.5f;
      impact.particles.gradient = &splashGrad;

      VFX_TriggerImpactBurst(s_orbs[i].position, 1.0f, &impact);

      s_orbs[i].active = false;
    }
  }
}

void DrawGleamingOrbSkill(void) {
  for (int i = 0; i < MAX_ORBS; i++) {
    if (!s_orbs[i].active)
      continue;

    SkillManager_BeginShader(s_waterShader);

    /* Reset cấu hình màu Vertex tránh nhiễu (Rule 11.1) */
    rlColor4ub(255, 255, 255, 255);

    /* Bảo toàn khối 3D (Rule 12.5): Kết hợp đa lớp lõi vững chắc và vỏ mềm */

    /* Lớp vỏ nước biến dạng ngoài (Núng nính) */
    Color outerWaterTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.35f);
    DrawCoreSphere(s_orbs[i].position, s_orbs[i].radius, 32, 32,
                   outerWaterTint);

    /* Lớp lõi phản chiếu đặc trong suốt (Long lanh) */
    float coreWobble =
        1.0f + 0.08f * cosf(s_globalTime * 16.0f + s_orbs[i].seed);
    float coreRadius =
        s_orbs[i].baseRadius * 0.55f * s_orbs[i].scaleRandomizer * coreWobble;

    Color innerGleamColor = ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.55f);
    innerGleamColor = ColorAlpha(innerGleamColor, 0.75f);

    /* Độ lệch tâm nhẹ tránh sự đối xứng hoàn hảo của máy móc */
    Vector3 slightOffset = (Vector3){sinf(s_globalTime * 3.0f) * 0.04f,
                                     cosf(s_globalTime * 2.5f) * 0.04f, 0.0f};
    Vector3 corePos = Vector3Add(s_orbs[i].position, slightOffset);
    DrawCoreSphere(corePos, coreRadius, 20, 20, innerGleamColor);

    SkillManager_EndShader();
  }
}

void UnloadGleamingOrbSkill(void) {
  /* Tuân thủ luật Teardown: Để trống để ResourceManager tự giải phóng tài
   * nguyên hệ thống */
}

bool IsGleamingOrbSkillCoiling(void) { return false; }

int GetGleamingOrbSkillProjectiles(SkillProjectile *outProjectiles,
                                   int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_ORBS && count < maxProjectiles; i++) {
    if (s_orbs[i].active) {
      outProjectiles[count].position = s_orbs[i].position;
      outProjectiles[count].radius = s_orbs[i].radius;
      outProjectiles[count].active = s_orbs[i].active;
      count++;
    }
  }
  return count;
}

void DeactivateGleamingOrbProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_ORBS; i++) {
    if (s_orbs[i].active) {
      if (count == index) {
        s_orbs[i].active = false;
        break;
      }
      count++;
    }
  }
}
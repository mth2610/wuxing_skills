#include "particle_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <string.h>

#define MAX_PARTICLES                                                          \
  2000 // Giới hạn mảng tĩnh an toàn cho CPU mode / No-malloc

typedef struct {
  Vector3 position;
  Vector3 velocity;
  Color colorStart;
  Color colorEnd;
  float radius;
  float lifetime;
  float maxLifetime;

  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;

  // Bộ bộ nhớ phẳng lưu trữ cấu hình Sub-Emitter tránh cấp phát động
  ParticleConfig onDeathConfig;
  int onDeathCount;
  bool hasDeathEmit;

  ParticleConfig onLiveConfig;
  float onLiveEmitRate;
  float onLiveEmitTimer; // Bộ đếm tích lũy thời gian để nhả hạt đều đặn
  bool hasLiveEmit;

  bool active;
} ParticleInternal;

// Quản lý mảng tĩnh toàn cục
static ParticleInternal g_Particles[MAX_PARTICLES];
static float s_particleTime = 0.0f; // Thời gian tích lũy dùng cho WindZone noise

void InitParticleSystem(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    g_Particles[i].active = false;
  }
}

void SpawnParticle(ParticleConfig config) {
  int targetIdx = -1;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!g_Particles[i].active) {
      targetIdx = i;
      break;
    }
  }

  // Mảng tĩnh đầy, bỏ qua để bảo toàn FPS ổn định cho game
  if (targetIdx == -1)
    return;

  ParticleInternal *p = &g_Particles[targetIdx];
  p->position = config.position;
  p->velocity = config.velocity;
  p->colorStart = config.colorStart;
  p->colorEnd = config.colorEnd;
  p->radius = config.radius;
  p->lifetime = config.lifetime;
  p->maxLifetime = config.lifetime;
  p->forceField = config.forceField;
  p->gradient = config.gradient;
  p->spriteAnim = config.spriteAnim;
  p->active = true;

  // Trích xuất cấu hình Sub-Emitter khi hạt chết (onDeath)
  if (config.onDeathEmit && config.onDeathEmitCount > 0) {
    p->onDeathConfig = *config.onDeathEmit;
    // KHÓA AN TOÀN TRÁNH ĐỆ QUY VÔ HẠN: Hạt con sinh ra khi chết sẽ không tự đẻ
    // tiếp hạt cháu
    p->onDeathConfig.onDeathEmit = NULL;
    p->onDeathConfig.onLiveEmit = NULL;
    p->onDeathCount = config.onDeathEmitCount;
    p->hasDeathEmit = true;
  } else {
    p->hasDeathEmit = false;
  }

  // Trích xuất cấu hình Sub-Emitter khi hạt đang bay (onLive)
  if (config.onLiveEmit && config.onLiveEmitRate > 0.0f) {
    p->onLiveConfig = *config.onLiveEmit;
    // KHÓA AN TOÀN TRÁNH ĐỆ QUY VÔ HẠN: Hạt bụi vệt đuôi sinh ra sẽ không tự đẻ
    // tiếp hạt bụi khác
    p->onLiveConfig.onLiveEmit = NULL;
    p->onLiveConfig.onDeathEmit = NULL;
    p->onLiveEmitRate = config.onLiveEmitRate;
    p->onLiveEmitTimer = 0.0f;
    p->hasLiveEmit = true;
  } else {
    p->hasLiveEmit = false;
  }
}

void UpdateParticles(float dt) {
  s_particleTime += dt;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!g_Particles[i].active)
      continue;

    ParticleInternal *p = &g_Particles[i];
    p->lifetime -= dt;

    // --------------------------------------------------------
    // 1. XỬ LÝ SUB-EMITTER: ON LIVE EMIT (Đẻ vệt đuôi khi đang sống)
    // --------------------------------------------------------
    if (p->lifetime > 0.0f && p->hasLiveEmit) {
      p->onLiveEmitTimer += dt;
      float spawnInterval = 1.0f / p->onLiveEmitRate;

      int safetyCounter = 0;
      // Giới hạn vòng lặp nhả tối đa 5 hạt/frame để tránh lag đột biến kéo chết
      // cụm hạt
      while (p->onLiveEmitTimer >= spawnInterval && safetyCounter < 5) {
        p->onLiveConfig.position =
            p->position; // Hạt con lấy vị trí tức thời của hạt mẹ
        SpawnParticle(p->onLiveConfig);
        p->onLiveEmitTimer -= spawnInterval;
        safetyCounter++;
      }
    }

    // Cập nhật vật lý di chuyển và ForceField (nếu có)
    if (p->forceField) {
      Vector3 force =
          ForceField_Evaluate(p->forceField, p->position, p->velocity,
                              p->lifetime, (Vector3){0}, (Vector3){0});
      p->velocity = Vector3Add(p->velocity, Vector3Scale(force, dt));
    }
    // Áp dụng WindZone toàn cục (auto, không cần set per-particle)
    if (WindZone_IsActive()) {
      Vector3 windForce = WindZone_Evaluate(p->position, p->velocity, s_particleTime);
      p->velocity = Vector3Add(p->velocity, Vector3Scale(windForce, dt));
    }
    p->position = Vector3Add(p->position, Vector3Scale(p->velocity, dt));

    // --------------------------------------------------------
    // 2. XỬ LÝ SUB-EMITTER: ON DEATH EMIT (Nổ tung hạt con khi chết)
    // --------------------------------------------------------
    if (p->lifetime <= 0.0f) {
      p->active =
          false; // Thu hồi hạt mẹ trước để giải phóng slot trống cho hạt con

      if (p->hasDeathEmit) {
        for (int c = 0; c < p->onDeathCount; c++) {
          p->onDeathConfig.position =
              p->position; // Xuất phát từ điểm chết của hạt mẹ

          // Thêm lực nhiễu ngẫu nhiên đa hướng để tạo hiệu ứng bùng nổ đẹp mắt
          Vector3 randomVelocity = {(float)GetRandomValue(-80, 80),
                                    (float)GetRandomValue(-80, 80),
                                    (float)GetRandomValue(-80, 80)};

          // Tạo bản sao tạm để tránh tích lũy sai lệch vận tốc vào cấu hình gốc
          ParticleConfig tempChild = p->onDeathConfig;
          tempChild.velocity = Vector3Add(tempChild.velocity, randomVelocity);

          SpawnParticle(tempChild);
        }
      }
    }
  }
}

void DrawParticles(Camera3D camera, Texture2D texture) {
  // Kỹ thuật camera-facing billboards / Quads cho hệ thống hạt CPU
  Vector3 viewDir =
      Vector3Normalize(Vector3Subtract(camera.position, camera.target));
  Vector3 right = {camera.up.y * viewDir.z - camera.up.z * viewDir.y,
                   camera.up.z * viewDir.x - camera.up.x * viewDir.z,
                   camera.up.x * viewDir.y - camera.up.y * viewDir.x};
  right = Vector3Normalize(right);
  Vector3 up = Vector3CrossProduct(viewDir, right);

  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!g_Particles[i].active)
      continue;

    ParticleInternal *p = &g_Particles[i];
    float lifeRatio = p->lifetime / p->maxLifetime;
    if (lifeRatio < 0.0f)
      lifeRatio = 0.0f;
    if (lifeRatio > 1.0f)
      lifeRatio = 1.0f;

    // Nội suy màu sắc
    Color c = p->colorStart;
    if (p->gradient) {
      c = ColorGradient_Sample(p->gradient, 1.0f - lifeRatio);
    } else {
      c.r = (unsigned char)(p->colorStart.r * lifeRatio +
                            p->colorEnd.r * (1.0f - lifeRatio));
      c.g = (unsigned char)(p->colorStart.g * lifeRatio +
                            p->colorEnd.g * (1.0f - lifeRatio));
      c.b = (unsigned char)(p->colorStart.b * lifeRatio +
                            p->colorEnd.b * (1.0f - lifeRatio));
      c.a = (unsigned char)(p->colorStart.a * lifeRatio +
                            p->colorEnd.a * (1.0f - lifeRatio));
    }

    // Tọa độ UV chuẩn (Bỏ qua tính toán SpriteAnim lỗi để giữ an toàn tuyệt
    // đối)
    float uMin = 0.0f, vMin = 0.0f, uMax = 1.0f, vMax = 1.0f;

    // Đẩy hình học qua rlgl immediate mode
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);

    // Top-Left
    rlTexCoord2f(uMin, vMin);
    rlVertex3f(p->position.x + (right.x - up.x) * p->radius,
               p->position.y + (right.y - up.y) * p->radius,
               p->position.z + (right.z - up.z) * p->radius);

    // Bottom-Left
    rlTexCoord2f(uMin, vMax);
    rlVertex3f(p->position.x + (right.x + up.x) * p->radius,
               p->position.y + (right.y + up.y) * p->radius,
               p->position.z + (right.z + up.z) * p->radius);

    // Bottom-Right
    rlTexCoord2f(uMax, vMax);
    rlVertex3f(p->position.x + (-right.x + up.x) * p->radius,
               p->position.y + (-right.y + up.y) * p->radius,
               p->position.z + (-right.z + up.z) * p->radius);

    // Top-Right
    rlTexCoord2f(uMax, vMin);
    rlVertex3f(p->position.x + (-right.x - up.x) * p->radius,
               p->position.y + (-right.y - up.y) * p->radius,
               p->position.z + (-right.z - up.z) * p->radius);
    rlEnd();
  }
  rlSetTexture(0);
}

void UnloadParticleSystem(void) { InitParticleSystem(); }

bool IsParticleSystemActive(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (g_Particles[i].active)
      return true;
  }
  return false;
}

void ParticleSystem_ResetForceFieldRegistry(void) {
  // No-op ở CPU mode
}
#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/sprite_anim.h"
#include "raylib.h"
#include <stdbool.h>

// Forward Declaration để cấu trúc có thể tự tham chiếu chính nó cho Sub-Emitter
typedef struct ParticleConfig ParticleConfig;

struct ParticleConfig {
  Vector3 position;
  Vector3 velocity;
  Color colorStart;
  Color colorEnd;
  float radius;
  float lifetime;

  // ForceField tùy chọn: NULL = không dùng; non-NULL = apply force field mỗi
  // frame
  const ForceField *forceField;

  // Tùy chọn chuyển màu dải stop và ảnh hoạt cảnh atlas
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;

  // ============================================================
  // 3.1 SUB-EMITTER SYSTEM — MỞ RỘNG[cite: 4]
  // ============================================================
  const ParticleConfig
      *onDeathEmit; // NULL = không dùng, hạt con nổ ra khi hạt mẹ chết[cite: 4]
  int onDeathEmitCount; // Số lượng hạt con bùng nổ khi chết[cite: 4]

  const ParticleConfig
      *onLiveEmit; // Phát liên tục (tạo vệt đuôi bụi) khi còn sống[cite: 4]
  float onLiveEmitRate; // Số lượng hạt con sinh ra trên mỗi giây
                        // (particles/sec)[cite: 4]
};

void InitParticleSystem(void);
void SpawnParticle(ParticleConfig config);
void UpdateParticles(float dt);
void DrawParticles(Camera3D camera, Texture2D texture);
void UnloadParticleSystem(void);
bool IsParticleSystemActive(void);

// ============================================================
// CHỈ CÓ Ý NGHĨA Ở GPU COMPUTE MODE
// ============================================================
#define MAX_GPU_FORCE_FIELDS 8
void ParticleSystem_ResetForceFieldRegistry(void);

#endif // PARTICLE_SYSTEM_H
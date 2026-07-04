#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/skill_curve.h"
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

  // Optional over-lifetime multiplier curves (t01 = 0 at spawn, 1 at death —
  // same "age fraction" convention as `gradient` above). NULL = today's
  // exact legacy behavior (fixed radius; velocity driven only by
  // forceField/physics). Non-NULL: sampled fresh every frame, multiplying
  // the base value — e.g. radiusCurve = {0,1,1,1,0} makes a particle fade
  // in then shrink away instead of popping at a constant size.
  const SkillCurve *radiusCurve; // multiplies `radius` when drawn
  const SkillCurve *speedCurve;  // multiplies `velocity`'s contribution to
                                  // position each Update frame (does not
                                  // touch the stored velocity itself, so it
                                  // composes cleanly with forceField physics)
  const SkillCurve *alphaCurve;  // multiplies `colorStart.a` when drawn,
                                  // overriding the colorStart/colorEnd (or
                                  // gradient) alpha computation entirely for
                                  // this particle — RGB is unaffected, still
                                  // comes from colorStart/colorEnd/gradient
  const SkillCurve *emissiveCurve; // multiplies RGB brightness over lifetime
                                   // (>1.0 pushes channels toward 255, making
                                   // the particle brighter and more likely to
                                   // exceed PostFX bloomThreshold). NULL = no-op.

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
void ParticleSystem_GetStats(int *active, int *max); // Item 32
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
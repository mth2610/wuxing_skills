#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include "core/force_field.h"
#include "raylib.h"
#include <stdbool.h>

typedef struct {
  Vector3 position;
  Vector3 velocity;
  Color colorStart;
  Color colorEnd;
  float radius;
  float lifetime;

  // ForceField tùy chọn: NULL = không dùng; non-NULL = apply force field mỗi
  // frame Caller sở hữu bộ nhớ — particle_system chỉ giữ con trỏ, không copy
  const ForceField *forceField;
} ParticleConfig;

void InitParticleSystem(void);
void SpawnParticle(ParticleConfig config);
void UpdateParticles(float dt);
void DrawParticles(Camera3D camera, Texture2D texture);
void UnloadParticleSystem(void);
bool IsParticleSystemActive(void);

// ============================================================
// CHỈ CÓ Ý NGHĨA Ở GPU COMPUTE MODE (Linux/Windows/Android).
// Trên CPU mode (macOS) đây là no-op vì particle giữ trực tiếp con trỏ
// ForceField, không qua registry/slot nào cả.
// ============================================================

// Số ForceField KHÁC NHAU tối đa có thể active đồng thời trên GPU mode.
// Particle nào dùng ForceField vượt quá số này sẽ chạy như không có force
// field (chỉ còn drag) + log cảnh báo, KHÔNG crash.
#define MAX_GPU_FORCE_FIELDS 8

// Xoá toàn bộ đăng ký ForceField trên GPU. Nên gọi giữa các màn/round/scene
// (ví dụ lúc load lại trận trong "Ngũ Hành Tỷ Võ") để giải phóng slot bị
// chiếm bởi ForceField cũ không còn particle nào tham chiếu tới nữa.
void ParticleSystem_ResetForceFieldRegistry(void);

#endif // PARTICLE_SYSTEM_H
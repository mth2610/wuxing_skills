#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include "force_field.h"
#include "raylib.h"
#include <stdbool.h>

// Cờ vật lý — chỉ giữ Drag vì Force/Turbulence đã chuyển hoàn toàn qua ForceField
typedef enum {
  P_PHYSICS_NONE = 0,
  P_PHYSICS_DRAG = 1 << 0,
} ParticlePhysicsFlags;

typedef struct {
  Vector3 position;
  Vector3 velocity;
  Color colorStart;
  Color colorEnd;
  float radius;
  float lifetime;
  float drag;
  float viscosity;
  int physicsFlags;

  // ForceField tùy chọn: NULL = không dùng; non-NULL = apply force field mỗi frame
  // Caller sở hữu bộ nhớ — particle_system chỉ giữ con trỏ, không copy
  const ForceField *forceField;
} ParticleConfig;

void InitParticleSystem(void);
void SpawnParticle(ParticleConfig config);
void UpdateParticles(float dt);
void DrawParticles(Camera3D camera, Texture2D texture);
void UnloadParticleSystem(void);
bool IsParticleSystemActive(void);

#endif // PARTICLE_SYSTEM_H
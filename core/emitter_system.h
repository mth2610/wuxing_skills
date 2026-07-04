#ifndef EMITTER_SYSTEM_H
#define EMITTER_SYSTEM_H

#include "core/particle_system.h"
#include "raylib.h"

#define MAX_EMITTER_ENTITIES 256

typedef struct {
  ParticleConfig baseParticle;
  float spawnDistance; // Thả 1 hạt mỗi X đơn vị khoảng cách di chuyển
  float spawnRate; // Thả X hạt mỗi giây (nếu vật thể đứng im)
  float randomPosOffset; // Độ nhiễu vị trí khi thả hạt
} EmitterConfig;

void InitEmitterSystem(void);
int CreateEmitter(EmitterConfig config, Vector3 startPos);
void UpdateEmitterTarget(int id, Vector3 newPos, float dt);
void StopEmitter(int id);
void KillEmitter(int id);
void EmitterSystem_GetStats(int *active, int *max); // Item 32

#endif // EMITTER_SYSTEM_H
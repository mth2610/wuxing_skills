#ifndef EMITTER_SYSTEM_H
#define EMITTER_SYSTEM_H

#include "raylib.h"
#include "particle_system.h" 

#define MAX_EMITTER_ENTITIES 256

typedef struct {
    ParticleConfig baseParticle;
    float spawnDistance;   // Thả 1 hạt mỗi X đơn vị khoảng cách di chuyển
    float spawnRate;       // Thả X hạt mỗi giây (nếu vật thể đứng im)
    float randomPosOffset; // Độ nhiễu vị trí khi thả hạt
} EmitterConfig;

void InitEmitterSystem(void);
int CreateEmitter(EmitterConfig config, Vector3 startPos);
void UpdateEmitterTarget(int id, Vector3 newPos, float dt);
void StopEmitter(int id); 
void KillEmitter(int id); 

#endif // EMITTER_SYSTEM_H
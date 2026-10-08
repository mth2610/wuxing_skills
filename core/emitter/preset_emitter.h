#ifndef CORE_PRESET_EMITTER_H
#define CORE_PRESET_EMITTER_H
#include "raylib.h"
#include <stdbool.h>
// 4. Particle Emitter System
typedef enum {
    EMITTER_FIRE,
    EMITTER_SNOW,
    EMITTER_WATER_SPURT,
    EMITTER_SHOCKED_SPARKS,
    EMITTER_WOOD_LEAVES,
    EMITTER_EARTH_DUST,
    EMITTER_METAL_SPARKS,
    EMITTER_TAIJI_MOTES
} EmitterPreset;

typedef struct {
    EmitterPreset type;
    Vector3 pos;
    float rate;       // Số hạt phát ra mỗi giây
    float duration;   // Thời gian tồn tại
    float timer;      // Bộ đếm thời gian
    float spawnAccum; // Tích luỹ hạt sinh
    bool active;
} ParticleEmitter;

void EmitterSystem_Init(void);
void EmitterSystem_Update(float dt);
int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId);
void EmitterSystem_Unload(void);

#endif

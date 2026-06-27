#include "core/emitter_system.h"
#include "raymath.h"

typedef struct {
    bool active;
    bool emitting;
    EmitterConfig config;
    Vector3 lastPos;
    float distAccumulator;
    float timeAccumulator;
} EmitterEntity;

static EmitterEntity emitterPool[MAX_EMITTER_ENTITIES];
static int lastUsedIndex = 0;

void InitEmitterSystem(void) {
    for (int i = 0; i < MAX_EMITTER_ENTITIES; i++) {
        emitterPool[i].active = false;
    }
}

int CreateEmitter(EmitterConfig config, Vector3 startPos) {
    for (int i = 0; i < MAX_EMITTER_ENTITIES; i++) {
        int idx = (lastUsedIndex + i) % MAX_EMITTER_ENTITIES;
        if (!emitterPool[idx].active) {
            emitterPool[idx].active = true;
            emitterPool[idx].emitting = true;
            emitterPool[idx].config = config;
            emitterPool[idx].lastPos = startPos;
            emitterPool[idx].distAccumulator = 0.0f;
            emitterPool[idx].timeAccumulator = 0.0f;
            lastUsedIndex = (idx + 1) % MAX_EMITTER_ENTITIES;
            return idx;
        }
    }
    return -1;
}

void UpdateEmitterTarget(int id, Vector3 newPos, float dt) {
    if (id < 0 || id >= MAX_EMITTER_ENTITIES || !emitterPool[id].active) return;
    
    EmitterEntity *e = &emitterPool[id];
    if (!e->emitting) {
        e->lastPos = newPos;
        return;
    }

    Vector3 delta = Vector3Subtract(newPos, e->lastPos);
    float dist = Vector3Length(delta);
    
    // 1. Thả hạt theo khoảng cách di chuyển (Dành cho vệt kiếm bay nhanh)
    if (e->config.spawnDistance > 0.0f) {
        e->distAccumulator += dist;
        int spawnCount = (int)(e->distAccumulator / e->config.spawnDistance);
        
        if (spawnCount > 0) {
            for (int i = 1; i <= spawnCount; i++) {
                // Nội suy (Lerp) rải hạt đều trên quãng đường để khói không đứt quãng
                float t = (float)i / (float)spawnCount;
                Vector3 spawnPos = Vector3Lerp(e->lastPos, newPos, t);
                
                ParticleConfig p = e->config.baseParticle;
                p.position.x = spawnPos.x + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                p.position.y = spawnPos.y + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                p.position.z = spawnPos.z + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                
                SpawnParticle(p);
            }
            e->distAccumulator -= spawnCount * e->config.spawnDistance;
        }
    } 
    // 2. Thả hạt theo thời gian (Dành cho vật thể đứng im)
    else if (e->config.spawnRate > 0.0f) {
        e->timeAccumulator += dt;
        float spawnInterval = 1.0f / e->config.spawnRate;
        int spawnCount = (int)(e->timeAccumulator / spawnInterval);
        
        if (spawnCount > 0) {
            for (int i = 0; i < spawnCount; i++) {
                ParticleConfig p = e->config.baseParticle;
                p.position.x = newPos.x + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                p.position.y = newPos.y + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                p.position.z = newPos.z + (float)GetRandomValue(-100, 100) * 0.01f * e->config.randomPosOffset;
                
                SpawnParticle(p);
            }
            e->timeAccumulator -= spawnCount * spawnInterval;
        }
    }

    e->lastPos = newPos;
}

void StopEmitter(int id) {
    if (id >= 0 && id < MAX_EMITTER_ENTITIES) emitterPool[id].emitting = false;
}

void KillEmitter(int id) {
    if (id >= 0 && id < MAX_EMITTER_ENTITIES) emitterPool[id].active = false;
}
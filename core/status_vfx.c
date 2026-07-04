#include "core/status_vfx.h"
#include "core/skill_manager.h"
#include "core/emitter_system.h"
#include "core/vfx_light.h"
#include "raylib.h"
#include <stddef.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define FADE_OUT_TIME 0.5f
#define AURA_RING_K   4

typedef struct {
    bool  active;
    int   agentId;
    EffectPresetType element;
    float duration;
    float elapsed;
    bool  fading;
    float fadeElapsed;
    int   emitterIds[AURA_RING_K];
    float ringRadius;
} StatusVFXSlot;

static StatusVFXSlot s_slots[MAX_STATUS_VFX];

static Color Element_ToColor(EffectPresetType e) {
    switch (e) {
        case EFFECT_PRESET_FIRE_EXPLOSION: return ELEMENT_COLOR_FIRE;
        case EFFECT_PRESET_WATER_SPLASH:   return ELEMENT_COLOR_WATER;
        case EFFECT_PRESET_WOOD_BLOOM:     return ELEMENT_COLOR_WOOD;
        case EFFECT_PRESET_EARTH_CRACK:    return ELEMENT_COLOR_EARTH;
        case EFFECT_PRESET_METAL_SHARD:    return ELEMENT_COLOR_METAL;
        case EFFECT_PRESET_TAIJI_BURST:    return ELEMENT_COLOR_TAIJI;
        default:                           return WHITE;
    }
}

static void Slot_StartEmitters(int slotIdx, Vector3 agentPos) {
    StatusVFXSlot *s = &s_slots[slotIdx];
    Color c = Element_ToColor(s->element);
    float r = s->ringRadius;
    for (int k = 0; k < AURA_RING_K; k++) {
        float angle = (2.0f * PI * k) / AURA_RING_K;
        Vector3 p = { agentPos.x + cosf(angle) * r, agentPos.y + 0.05f,
                      agentPos.z + sinf(angle) * r };
        EmitterConfig cfg = {0};
        cfg.baseParticle.colorStart = c;
        cfg.baseParticle.colorEnd   = ColorAlpha(c, 0);
        cfg.baseParticle.radius     = 0.06f;
        cfg.baseParticle.lifetime   = 1.0f;
        cfg.baseParticle.velocity   = (Vector3){0, 0.4f, 0};
        cfg.spawnRate = 5.0f;
        cfg.randomPosOffset = 0.04f;
        s->emitterIds[k] = CreateEmitter(cfg, p);
    }
}

static void Slot_StopEmitters(int slotIdx) {
    StatusVFXSlot *s = &s_slots[slotIdx];
    for (int k = 0; k < AURA_RING_K; k++) {
        if (s->emitterIds[k] >= 0) {
            StopEmitter(s->emitterIds[k]);
            s->emitterIds[k] = -1;
        }
    }
}

int StatusVFX_Attach(int agentId, EffectPresetType element, float duration) {
    // refresh existing slot for same agent+element instead of stacking
    for (int i = 0; i < MAX_STATUS_VFX; i++) {
        if (s_slots[i].active && s_slots[i].agentId == agentId
            && s_slots[i].element == element && !s_slots[i].fading) {
            s_slots[i].elapsed = 0.0f;
            s_slots[i].duration = duration;
            return i;
        }
    }
    for (int i = 0; i < MAX_STATUS_VFX; i++) {
        if (!s_slots[i].active) {
            s_slots[i].active      = true;
            s_slots[i].agentId     = agentId;
            s_slots[i].element     = element;
            s_slots[i].duration    = duration;
            s_slots[i].elapsed     = 0.0f;
            s_slots[i].fading      = false;
            s_slots[i].fadeElapsed = 0.0f;
            s_slots[i].ringRadius  = 0.3f;
            for (int k = 0; k < AURA_RING_K; k++) s_slots[i].emitterIds[k] = -1;

            Vector3 pos;
            if (SkillManager_GetAgentPos(agentId, &pos)) {
                Slot_StartEmitters(i, pos);
                VFXLight_Spawn(pos, Element_ToColor(element), 1.5f,
                               duration, VFX_PRIORITY_LOW);
            }
            return i;
        }
    }
    return -1;
}

void StatusVFX_Detach(int handle) {
    if (handle < 0 || handle >= MAX_STATUS_VFX || !s_slots[handle].active) return;
    Slot_StopEmitters(handle);
    s_slots[handle].active = false;
}

void StatusVFX_Update(float dt) {
    for (int i = 0; i < MAX_STATUS_VFX; i++) {
        StatusVFXSlot *s = &s_slots[i];
        if (!s->active) continue;

        Vector3 pos;
        bool alive = SkillManager_GetAgentPos(s->agentId, &pos);

        if (!s->fading) {
            s->elapsed += dt;
            if (!alive || s->elapsed >= s->duration) {
                // start fade-out
                s->fading = true;
                s->fadeElapsed = 0.0f;
                Slot_StopEmitters(i);
            } else {
                // reposition emitters to follow agent
                for (int k = 0; k < AURA_RING_K; k++) {
                    if (s->emitterIds[k] >= 0) {
                        float angle = (2.0f * PI * k) / AURA_RING_K;
                        Vector3 p = { pos.x + cosf(angle) * s->ringRadius,
                                      pos.y + 0.05f,
                                      pos.z + sinf(angle) * s->ringRadius };
                        UpdateEmitterTarget(s->emitterIds[k], p, dt);
                    }
                }
            }
        } else {
            s->fadeElapsed += dt;
            if (s->fadeElapsed >= FADE_OUT_TIME) {
                s->active = false;
            }
        }
    }
}

void StatusVFX_Draw(void) {
    // emitter-driven slots draw themselves via EmitterSystem/ParticleSystem
}

void StatusVFX_GetStats(int *active, int *max) {
    int n = 0;
    for (int i = 0; i < MAX_STATUS_VFX; i++)
        if (s_slots[i].active) n++;
    *active = n;
    *max = MAX_STATUS_VFX;
}

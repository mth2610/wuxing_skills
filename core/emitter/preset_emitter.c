#include "core/emitter/preset_emitter.h"
#include "core/emitter/emitter.h"
#include "core/skill_helper.h"
#include "core/presets/vfx_presets.h"
#include "raymath.h"
#include <stdlib.h>
#define MAX_EMITTERS 32
static ParticleEmitter s_emitters[MAX_EMITTERS];
// 4. Particle Emitter System Implementation
void EmitterSystem_Init(void)
{
    InitHelperResources();
    EmissionSystem_Init();
    for (int i = 0; i < MAX_EMITTERS; i++)
    {
        s_emitters[i].active = false;
    }
}

void EmitterSystem_Update(float dt)
{
    for (int i = 0; i < MAX_EMITTERS; i++)
    {
        if (!s_emitters[i].active)
            continue;

        s_emitters[i].timer += dt;
        if (s_emitters[i].timer >= s_emitters[i].duration)
        {
            s_emitters[i].active = false;
            continue;
        }

        s_emitters[i].spawnAccum += s_emitters[i].rate * dt;
        int count = (int)s_emitters[i].spawnAccum;
        s_emitters[i].spawnAccum -= count;

        ColorGradient *grad = NULL;
        ForceField *fld = NULL;

        switch (s_emitters[i].type)
        {
        case EMITTER_FIRE:
            grad = &s_fireGrad;
            fld = &s_fireFld;
            break;
        case EMITTER_SNOW:
            grad = &s_snowGrad;
            fld = &s_snowFld;
            break;
        case EMITTER_WATER_SPURT:
            grad = &s_waterGrad;
            fld = &s_waterFld;
            break;
        case EMITTER_SHOCKED_SPARKS:
            grad = &s_lightningGrad;
            fld = &s_lightningFld;
            break;
        case EMITTER_WOOD_LEAVES:
            grad = &s_woodGrad;
            fld = &s_woodFld;
            break;
        case EMITTER_EARTH_DUST:
            grad = &s_earthGrad;
            fld = &s_earthFld;
            break;
        case EMITTER_METAL_SPARKS:
            grad = &s_metalGrad;
            fld = &s_metalFld;
            break;
        case EMITTER_TAIJI_MOTES:
            grad = &s_taijiGrad;
            fld = &s_taijiFld;
            break;
        }

        for (int k = 0; k < count; k++)
        {
            // Real-world-scaled ÷100 (root CLAUDE.md §scale).
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f};
            Vector3 pos = Vector3Add(s_emitters[i].pos, offset);
            Vector3 vel = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.10f,
                ((float)rand() / (float)RAND_MAX * 0.15f + 0.10f),
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.10f};

            SpawnParticle((ParticleConfig){
                .position = pos,
                .velocity = vel,
                .radius = (float)GetRandomValue(8, 20) / 1000.0f,
                .lifetime = (float)GetRandomValue(5, 12) / 10.0f,
                .gradient = grad,
                .forceField = fld});
        }
    }
}

int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration)
{
    for (int i = 0; i < MAX_EMITTERS; i++)
    {
        if (!s_emitters[i].active)
        {
            s_emitters[i].type = type;
            s_emitters[i].pos = pos;
            s_emitters[i].rate = ratePerSecond;
            s_emitters[i].duration = duration;
            s_emitters[i].timer = 0.0f;
            s_emitters[i].spawnAccum = 0.0f;
            s_emitters[i].active = true;
            return i;
        }
    }
    return -1;
}

void Emitter_Stop(int emitterId)
{
    if (emitterId >= 0 && emitterId < MAX_EMITTERS)
    {
        s_emitters[emitterId].active = false;
    }
}

void EmitterSystem_Unload(void)
{
    EmitterSystem_Init();
}


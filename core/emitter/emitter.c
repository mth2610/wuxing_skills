#include "core/emitter/emitter.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct EmissionSlot {
    bool active;
    uint32_t generation, seed;
    EmissionConfig config;
    EmissionStats stats;
    Vector3 origin;
    double age, carry;
} EmissionSlot;
static EmissionSlot emissionSlots[EMITTER_SCHEDULER_CAPACITY];

static bool Emission_FiniteVector(Vector3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static EmissionSlot *Emission_Lookup(EmissionHandle handle) {
    uint32_t index = (uint32_t)handle;
    if (index == 0 || index > EMITTER_SCHEDULER_CAPACITY) return NULL;
    EmissionSlot *slot = &emissionSlots[index - 1];
    return slot->active && slot->generation == (uint32_t)(handle >> 32) ? slot : NULL;
}
void EmissionSystem_Init(void) {
    for (int i = 0; i < EMITTER_SCHEDULER_CAPACITY; ++i) {
        emissionSlots[i].active = false;
        if (++emissionSlots[i].generation == 0) ++emissionSlots[i].generation;
    }
}
void EmissionSystem_Unload(void) { EmissionSystem_Init(); }
EmissionHandle Emission_Create(const EmissionConfig *c, Vector3 origin) {
    if (!c || !c->sink || c->callbackBudget > EMITTER_MAX_CALLBACK_BUDGET || !Emission_FiniteVector(origin) ||
        c->kind < EMISSION_PARTICLE || c->kind > EMISSION_MESH ||
        c->schedule < EMISSION_BURST || c->schedule > EMISSION_DISTANCE ||
        !isfinite(c->duration) || c->duration < 0.0f ||
        (c->schedule == EMISSION_TIMED_COUNT && c->duration <= 0.0f) ||
        (c->schedule == EMISSION_RATE && (!isfinite(c->rate) || c->rate <= 0.0f)) ||
        (c->schedule == EMISSION_DISTANCE && (!isfinite(c->spacing) || c->spacing <= 0.0f)))
        return EMISSION_HANDLE_INVALID;
    for (uint32_t i = 0; i < EMITTER_SCHEDULER_CAPACITY; ++i) {
        EmissionSlot *s = &emissionSlots[i];
        if (s->active) continue;
        uint32_t generation = s->generation + 1;
        if (!generation) generation = 1;
        memset(s, 0, sizeof(*s));
        s->active = true;
        s->generation = generation;
        s->seed = c->seed;
        s->config = *c;
        s->stats.emitting = true;
        s->origin = origin;
        return ((uint64_t)generation << 32) | (i + 1);
    }
    return EMISSION_HANDLE_INVALID;
}
bool Emission_Step(EmissionHandle handle, Vector3 origin, float dt) {
    EmissionSlot *s = Emission_Lookup(handle);
    if (!s || !isfinite(dt) || dt < 0.0f || !Emission_FiniteVector(origin)) return false;
    if (!s->stats.emitting) { s->origin = origin; return true; }
    const EmissionConfig *c = &s->config;
    double fraction = 1.0, stepTime = dt;
    if (c->duration > 0.0f && stepTime > c->duration - s->age) {
        stepTime = fmax(0.0, c->duration - s->age);
        fraction = dt > 0.0f ? stepTime / dt : 0.0;
    }
    double dx = (double)origin.x - s->origin.x;
    double dy = (double)origin.y - s->origin.y;
    double dz = (double)origin.z - s->origin.z;
    double distance = sqrt(dx*dx + dy*dy + dz*dz) * fraction;
    double previousCarry = s->carry, due = 0.0;
    s->age += stepTime;
    switch (c->schedule) {
    case EMISSION_BURST: due = c->count; s->stats.emitting = false; break;
    case EMISSION_TIMED_COUNT:
        due = floor(fmin(1.0, s->age / c->duration) * c->count) - (double)s->stats.scheduled;
        break;
    case EMISSION_RATE:
        s->carry += stepTime * c->rate;
        due = floor(s->carry);
        s->carry -= due;
        break;
    case EMISSION_DISTANCE:
        s->carry += distance;
        due = floor(s->carry / c->spacing);
        s->carry -= due * c->spacing;
        break;
    }
    uint64_t remaining = UINT64_MAX - s->stats.scheduled;
    uint64_t births = due <= 0.0 ? 0 : due >= (double)remaining ? remaining : (uint64_t)due;
    uint32_t budget = c->callbackBudget ? c->callbackBudget : EMITTER_MAX_SPAWNS_PER_STEP;
    uint32_t deliver = births > budget ? budget : (uint32_t)births;
    for (uint32_t i = 0; i < deliver; ++i) {
        double t = 1.0;
        if (c->schedule == EMISSION_DISTANCE && distance > 0.0)
            t = ((i + 1) * (double)c->spacing - previousCarry) / distance * fraction;
        EmissionSpawn spawn = {0};
        spawn.kind = c->kind;
        spawn.spawnTemplate = c->spawnTemplate;
        spawn.sequence = s->stats.scheduled + i;
        spawn.sample.normal = (Vector3){0.0f, 1.0f, 0.0f};
        if (c->sampleSource && !c->sampleSource(c->source, &s->seed, &spawn.sample)) {
            ++s->stats.rejected;
            continue;
        }
        spawn.sample.position.x += (float)(s->origin.x + dx*t);
        spawn.sample.position.y += (float)(s->origin.y + dy*t);
        spawn.sample.position.z += (float)(s->origin.z + dz*t);
        if (!Emission_FiniteVector(spawn.sample.position) || !Emission_FiniteVector(spawn.sample.normal) ||
            !c->sink(c->sinkUser, &spawn)) ++s->stats.rejected;
        else ++s->stats.accepted;
    }
    s->stats.scheduled += births;
    s->stats.dropped += births - deliver;
    s->origin = origin;
    if (c->duration > 0.0f && s->age >= c->duration) s->stats.emitting = false;
    return true;
}
bool Emission_Stop(EmissionHandle handle) {
    EmissionSlot *s = Emission_Lookup(handle);
    if (!s) return false;
    s->stats.emitting = false;
    return true;
}
bool Emission_Destroy(EmissionHandle handle) {
    EmissionSlot *s = Emission_Lookup(handle);
    if (!s) return false;
    s->active = false;
    return true;
}
bool Emission_GetStats(EmissionHandle handle, EmissionStats *stats) {
    EmissionSlot *s = Emission_Lookup(handle);
    if (!s || !stats) return false;
    *stats = s->stats;
    return true;
}

#ifndef WUXING_EMITTER_H
#define WUXING_EMITTER_H
#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define EMITTER_SCHEDULER_CAPACITY 128
#define EMITTER_MAX_SPAWNS_PER_STEP 256
#define EMITTER_MAX_CALLBACK_BUDGET 2048
typedef uint64_t EmissionHandle;
#define EMISSION_HANDLE_INVALID ((EmissionHandle)0)
typedef enum EmissionKind {
    EMISSION_PARTICLE, EMISSION_RIBBON, EMISSION_TRAIL_NODE, EMISSION_MESH
} EmissionKind;
typedef enum EmissionSchedule {
    EMISSION_BURST, EMISSION_TIMED_COUNT, EMISSION_RATE, EMISSION_DISTANCE
} EmissionSchedule;
typedef struct EmissionSample {
    Vector3 position; /* World meters, source supplies offset from emitter origin. */
    Vector3 normal;
} EmissionSample;
/* Called synchronously; source/template/user storage must outlive emitter. */
typedef bool (*EmissionSourceFn)(void *source, uint32_t *seed, EmissionSample *sample);
typedef struct EmissionSpawn {
    EmissionKind kind;
    EmissionSample sample;
    const void *spawnTemplate; /* Immutable component-owned template. */
    uint64_t sequence; /* Includes rejected and budget-dropped births. */
} EmissionSpawn;
/* Sink invokes a component allocator. false rejects birth, without retries.
 * Source/sink callbacks must not mutate or step the scheduler. */
typedef bool (*EmissionSinkFn)(void *user, const EmissionSpawn *spawn);
typedef struct EmissionConfig {
    EmissionKind kind;
    EmissionSchedule schedule;
    uint32_t count; /* Burst or timed total. Timed mode has no startup burst. */
    float duration; /* Required for timed count; optional rate/distance limit. */
    float rate; /* Births/s for RATE. */
    float spacing; /* Meters/birth for DISTANCE. */
    uint32_t seed;
    EmissionSourceFn sampleSource; /* NULL = point. */
    void *source;
    EmissionSinkFn sink;
    void *sinkUser;
    const void *spawnTemplate;
    uint32_t callbackBudget; /* 0 = 256; explicit budgets may reach 2048. */
} EmissionConfig;
typedef struct EmissionStats {
    uint64_t scheduled, accepted, rejected, dropped;
    bool emitting;
} EmissionStats;
/* Init invalidates existing handles. Explicit per-handle stepping, dt >= 0.
 * Burst runs on first Step. Stopped/completed slots persist until Destroy.
 * Stop ceases births; children drain in their component system independently.
 * Default 256 callbacks/Step; explicit callbackBudget is bounded at 2048.
 * Overflow births are consumed and counted.
 * Counters saturate. Single-threaded, fixed pool, no component simulation. */
void EmissionSystem_Init(void);
void EmissionSystem_Unload(void);
EmissionHandle Emission_Create(const EmissionConfig *config, Vector3 origin);
bool Emission_Step(EmissionHandle handle, Vector3 origin, float dt);
bool Emission_Stop(EmissionHandle handle);
bool Emission_Destroy(EmissionHandle handle);
bool Emission_GetStats(EmissionHandle handle, EmissionStats *stats);
#endif

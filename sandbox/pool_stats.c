#include "sandbox/pool_stats.h"
#include "core/particle_system.h"
#include "core/trail_system.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/emitter_system.h"
#include "core/skill_helper.h"
#include "core/status_vfx.h"
#include "core/afterimage.h"
#include "raylib.h"
#include <stdio.h>
#include <stdbool.h>

#define NUM_POOLS 8

typedef struct {
    const char *name;
    int peak;
    int max;
    bool everDropped;
} PoolRecord;

static PoolRecord s_records[NUM_POOLS];
static bool s_inited = false;

static const char *POOL_NAMES[NUM_POOLS] = {
    "Particle", "Trail", "Decal", "VFXLight",
    "Emitter", "DamageVolume", "StatusVFX", "Afterimage"
};

void PoolStats_Init(void) {
    for (int i = 0; i < NUM_POOLS; i++) {
        s_records[i].name        = POOL_NAMES[i];
        s_records[i].peak        = 0;
        s_records[i].max         = 0;
        s_records[i].everDropped = false;
    }
    s_inited = true;
}

static void Sample(void) {
    int a[NUM_POOLS], m[NUM_POOLS];
    ParticleSystem_GetStats(&a[0], &m[0]);
    TrailSystem_GetStats   (&a[1], &m[1]);
    DecalSystem_GetStats   (&a[2], &m[2]);
    VFXLight_GetStats      (&a[3], &m[3]);
    EmitterSystem_GetStats (&a[4], &m[4]);
    DamageVolume_GetStats  (&a[5], &m[5]);
    StatusVFX_GetStats     (&a[6], &m[6]);
    Afterimage_GetStats    (&a[7], &m[7]);

    for (int i = 0; i < NUM_POOLS; i++) {
        s_records[i].max = m[i];
        if (a[i] > s_records[i].peak) {
            if (s_records[i].peak == m[i]) s_records[i].everDropped = true;
            s_records[i].peak = a[i];
        }
        if (a[i] >= m[i]) {
            s_records[i].everDropped = true;
            TraceLog(LOG_WARNING, "[POOL_STATS] %s pool full (%d/%d) — drops occurring",
                     s_records[i].name, a[i], m[i]);
        }
    }
}

void PoolStats_DrawOverlay(void) {
    if (!IsKeyDown(KEY_F3)) return;
    if (!s_inited) PoolStats_Init();

    Sample();

    int a[NUM_POOLS], m[NUM_POOLS];
    ParticleSystem_GetStats(&a[0], &m[0]);
    TrailSystem_GetStats   (&a[1], &m[1]);
    DecalSystem_GetStats   (&a[2], &m[2]);
    VFXLight_GetStats      (&a[3], &m[3]);
    EmitterSystem_GetStats (&a[4], &m[4]);
    DamageVolume_GetStats  (&a[5], &m[5]);
    StatusVFX_GetStats     (&a[6], &m[6]);
    Afterimage_GetStats    (&a[7], &m[7]);

    int x = 10, y = 80;
    int rowH = 18;
    DrawRectangle(x - 4, y - 4, 280, NUM_POOLS * rowH + 22, (Color){0, 0, 0, 160});
    DrawText("Pool Stats (F3)", x, y, 14, YELLOW);
    y += 18;

    for (int i = 0; i < NUM_POOLS; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%-13s %3d/%-4d (peak %3d)",
                 s_records[i].name, a[i], m[i], s_records[i].peak);
        Color c = s_records[i].everDropped ? RED : GREEN;
        DrawText(buf, x, y, 13, c);
        y += rowH;
    }
}

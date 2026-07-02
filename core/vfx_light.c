#include "vfx_light.h"

typedef struct {
    Vector3 position;
    Color color;
    float radius;
    float lifetime;
    float maxLifetime;
    VFXPriority priority;
    bool active;
} VFXLightInternal;

// Quản lý bằng bộ nhớ tĩnh (No-malloc) theo đúng triết lý của project
static VFXLightInternal g_VFXLights[MAX_VFX_LIGHTS];

void VFXLight_Init(void) {
    VFXLight_Reset();
}

void VFXLight_Reset(void) {
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        g_VFXLights[i].active = false;
    }
}

void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXPriority priority) {
    int freeIdx = -1;
    int evictIdx = -1;
    VFXPriority evictPriority = VFX_PRIORITY_HIGH_ULTIMATE;
    float evictLifetime = 999999.0f;

    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        if (!g_VFXLights[i].active) {
            freeIdx = i;
            break;
        }
        // Track the lowest-priority slot; among equal priority, the one with
        // the shortest remaining lifetime (about to expire anyway).
        if (evictIdx == -1 || g_VFXLights[i].priority < evictPriority ||
            (g_VFXLights[i].priority == evictPriority &&
             g_VFXLights[i].lifetime < evictLifetime)) {
            evictIdx = i;
            evictPriority = g_VFXLights[i].priority;
            evictLifetime = g_VFXLights[i].lifetime;
        }
    }

    int targetIdx = -1;
    if (freeIdx != -1) {
        targetIdx = freeIdx;
    } else if (evictIdx != -1 && evictPriority <= priority) {
        // Only evict a slot whose priority is <= the incoming spawn's — a
        // low-priority spawn must not evict an existing higher-priority one.
        targetIdx = evictIdx;
    }

    if (targetIdx != -1) {
        g_VFXLights[targetIdx].position = pos;
        g_VFXLights[targetIdx].color = color;
        g_VFXLights[targetIdx].radius = radius;
        g_VFXLights[targetIdx].lifetime = lifetime;
        g_VFXLights[targetIdx].maxLifetime = lifetime;
        g_VFXLights[targetIdx].priority = priority;
        g_VFXLights[targetIdx].active = true;
    }
}

void VFXLight_Update(float dt) {
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        if (!g_VFXLights[i].active) continue;

        g_VFXLights[i].lifetime -= dt;
        if (g_VFXLights[i].lifetime <= 0.0f) {
            g_VFXLights[i].active = false;
        }
    }
}

void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount) {
    int activeCount = 0;
    int limit = (maxCount < MAX_VFX_LIGHTS) ? maxCount : MAX_VFX_LIGHTS;

    for (int i = 0; i < MAX_VFX_LIGHTS && activeCount < limit; i++) {
        if (g_VFXLights[i].active) {
            out[activeCount].position = g_VFXLights[i].position;
            out[activeCount].radius = g_VFXLights[i].radius;
            
            // Có thể thực hiện tính toán fade-out màu sắc dựa trên tỷ lệ lifetime còn lại tại đây
            float alphaRatio = g_VFXLights[i].lifetime / g_VFXLights[i].maxLifetime;
            Color finalColor = g_VFXLights[i].color;
            finalColor.a = (unsigned char)(finalColor.a * alphaRatio);
            
            out[activeCount].color = finalColor;
            activeCount++;
        }
    }
    *count = activeCount;
}
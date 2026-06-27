#include "vfx_light.h"

typedef struct {
    Vector3 position;
    Color color;
    float radius;
    float lifetime;
    float maxLifetime;
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

void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime) {
    int targetIdx = -1;
    float minLifetime = 999999.0f;

    // Tìm slot trống hoặc slot có light sắp hết hạn nhất để ghi đè (Tránh crash nếu quá tải vfx)
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        if (!g_VFXLights[i].active) {
            targetIdx = i;
            break;
        }
        if (g_VFXLights[i].lifetime < minLifetime) {
            minLifetime = g_VFXLights[i].lifetime;
            targetIdx = i;
        }
    }

    if (targetIdx != -1) {
        g_VFXLights[targetIdx].position = pos;
        g_VFXLights[targetIdx].color = color;
        g_VFXLights[targetIdx].radius = radius;
        g_VFXLights[targetIdx].lifetime = lifetime;
        g_VFXLights[targetIdx].maxLifetime = lifetime;
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
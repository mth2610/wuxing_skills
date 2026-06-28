#include "environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Vector3 s_sunDirection = { 0.5f, -1.0f, 0.3f }; // Direction OF the sun (pointing towards ground)
static Color s_sunColor = { 255, 245, 230, 255 };      // Warm white / yellowish sun light
static Color s_ambientColor = { 50, 50, 70, 255 };      // Cool bluish shadow ambient tone
static Color s_shadowColor = { 8, 8, 12, 180 };       // Transparent dark shadow tone
static EnvFogConfig s_fogConfig = {
    .color = { 30, 30, 40, 255 },
    .start = 200.0f,
    .end = 1200.0f,
    .density = 0.001f,
    .enabled = false
};

void Environment_Init(void) {
    s_sunDirection = Vector3Normalize(s_sunDirection);
}

void Environment_Update(float dt) {
    // Tương lai có thể xoay mặt trời hoặc cập nhật ánh sáng động ở đây
}

void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height) {
    if (s_sunDirection.y >= -0.01f) return;
    
    float skewFactor = 1.0f / fabsf(s_sunDirection.y);
    Vector2 shadowOffset = { s_sunDirection.x * skewFactor, s_sunDirection.z * skewFactor };
    
    float yGround = 0.09f; // Nâng nhẹ để tránh lỗi nhấp nháy Z-fighting và đè lên grid
    
    rlSetTexture(0);
    rlDisableDepthTest();
    rlDisableDepthMask(); rlDisableBackfaceCulling();
    
    if (shape == ENV_SHAPE_SPHERE) {
        // Chiều cao hiện tại so với mặt đất (pos.y)
        float heightFactor = pos.y / 200.0f;
        float shadowScale = fmaxf(1.0f - heightFactor * 0.4f, 0.1f);
        float shadowAlpha = fmaxf(1.0f - heightFactor * 1.2f, 0.0f);
        
        if (shadowAlpha <= 0.001f) return;
        
        Vector3 shadowCenter = {
            pos.x + shadowOffset.x * pos.y,
            yGround,
            pos.z + shadowOffset.y * pos.y
        };
        
        Color centerColor = { s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * shadowAlpha) };
        
        int segments = 16;
        rlColor4ub(centerColor.r, centerColor.g, centerColor.b, centerColor.a);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < segments; i++) {
            float angle1 = ((float)i / segments) * 2.0f * PI;
            float angle2 = ((float)(i + 1) / segments) * 2.0f * PI;
            
            float rx1 = cosf(angle1) * width * shadowScale;
            float rz1 = sinf(angle1) * width * shadowScale;
            float rx2 = cosf(angle2) * width * shadowScale;
            float rz2 = sinf(angle2) * width * shadowScale;
            
            rlVertex3f(shadowCenter.x, yGround, shadowCenter.z);
            rlVertex3f(shadowCenter.x + rx2, yGround, shadowCenter.z + rz2);
            rlVertex3f(shadowCenter.x + rx1, yGround, shadowCenter.z + rz1);
        }
        rlEnd();
    }
    else if (shape == ENV_SHAPE_CYLINDER) {
        // Đổ bóng trụ đứng xiên: gốc trụ nằm sát đất, ngọn trụ xiên theo mặt trời
        Vector3 baseCenter = { pos.x + shadowOffset.x * pos.y, yGround, pos.z + shadowOffset.y * pos.y };
        Vector3 topCenter = { pos.x + shadowOffset.x * (pos.y + height), yGround, pos.z + shadowOffset.y * (pos.y + height) };
        
        // Tính toán vector vuông góc 2D với hướng nắng đổ bóng
        float dx = topCenter.x - baseCenter.x;
        float dz = topCenter.z - baseCenter.z;
        float len = sqrtf(dx*dx + dz*dz);
        Vector2 perp = { 1.0f, 0.0f };
        if (len > 0.001f) {
            perp.x = -dz / len;
            perp.y = dx / len;
        }
        
        float tipWidth = width * 1.3f; // Ngọn bóng loe nhẹ và mờ hơn do tán xạ
        
        Vector3 bl = { baseCenter.x - perp.x * width, yGround, baseCenter.z - perp.y * width };
        Vector3 br = { baseCenter.x + perp.x * width, yGround, baseCenter.z + perp.y * width };
        Vector3 tl = { topCenter.x - perp.x * tipWidth, yGround, topCenter.z - perp.y * tipWidth };
        Vector3 tr = { topCenter.x + perp.x * tipWidth, yGround, topCenter.z + perp.y * tipWidth };
        
        // 1. Thân bóng
        rlBegin(RL_QUADS);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(bl.x, yGround, bl.z);
        rlVertex3f(br.x, yGround, br.z);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.25f));
        rlVertex3f(tr.x, yGround, tr.z);
        rlVertex3f(tl.x, yGround, tl.z);
        rlEnd();
        
        // 2. Đầu bóng (mờ và tròn)
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.25f));
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < 16; i++) {
            float angle1 = ((float)i / 16.0f) * 2.0f * PI;
            float angle2 = ((float)(i + 1) / 16.0f) * 2.0f * PI;
            
            rlVertex3f(topCenter.x, yGround, topCenter.z);
            rlVertex3f(topCenter.x + cosf(angle2) * tipWidth, yGround, topCenter.z + sinf(angle2) * tipWidth);
            rlVertex3f(topCenter.x + cosf(angle1) * tipWidth, yGround, topCenter.z + sinf(angle1) * tipWidth);
        }
        rlEnd();
        
        // 3. Chân bóng (đậm)
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < 16; i++) {
            float angle1 = ((float)i / 16.0f) * 2.0f * PI;
            float angle2 = ((float)(i + 1) / 16.0f) * 2.0f * PI;
            
            rlVertex3f(baseCenter.x, yGround, baseCenter.z);
            rlVertex3f(baseCenter.x + cosf(angle2) * width, yGround, baseCenter.z + sinf(angle2) * width);
            rlVertex3f(baseCenter.x + cosf(angle1) * width, yGround, baseCenter.z + sinf(angle1) * width);
        }
        rlEnd();
    }
    else if (shape == ENV_SHAPE_BOX) {
        // Đổ bóng hộp đứng xiên: Đóng gói toàn bộ hull bóng đổ 3D sập xuống đất
        Vector3 b0 = { pos.x - width, yGround, pos.z - width };
        Vector3 b1 = { pos.x + width, yGround, pos.z - width };
        Vector3 b2 = { pos.x + width, yGround, pos.z + width };
        Vector3 b3 = { pos.x - width, yGround, pos.z + width };
        
        Vector3 t0 = { pos.x - width + shadowOffset.x * height, yGround, pos.z - width + shadowOffset.y * height };
        Vector3 t1 = { pos.x + width + shadowOffset.x * height, yGround, pos.z - width + shadowOffset.y * height };
        Vector3 t2 = { pos.x + width + shadowOffset.x * height, yGround, pos.z + width + shadowOffset.y * height };
        Vector3 t3 = { pos.x - width + shadowOffset.x * height, yGround, pos.z + width + shadowOffset.y * height };
        
        rlBegin(RL_QUADS);
        
        // Mặt đáy (đậm)
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(b0.x, yGround, b0.z);
        rlVertex3f(b1.x, yGround, b1.z);
        rlVertex3f(b2.x, yGround, b2.z);
        rlVertex3f(b3.x, yGround, b3.z);
        
        // Mặt đỉnh (nhạt dần)
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.3f));
        rlVertex3f(t0.x, yGround, t0.z);
        rlVertex3f(t3.x, yGround, t3.z);
        rlVertex3f(t2.x, yGround, t2.z);
        rlVertex3f(t1.x, yGround, t1.z);
        
        // Các mặt bên nối đáy lên đỉnh
        // Mặt bên 0-1
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(b0.x, yGround, b0.z);
        rlVertex3f(b1.x, yGround, b1.z);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.3f));
        rlVertex3f(t1.x, yGround, t1.z);
        rlVertex3f(t0.x, yGround, t0.z);
        
        // Mặt bên 1-2
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(b1.x, yGround, b1.z);
        rlVertex3f(b2.x, yGround, b2.z);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.3f));
        rlVertex3f(t2.x, yGround, t2.z);
        rlVertex3f(t1.x, yGround, t1.z);
        
        // Mặt bên 2-3
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(b2.x, yGround, b2.z);
        rlVertex3f(b3.x, yGround, b3.z);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.3f));
        rlVertex3f(t3.x, yGround, t3.z);
        rlVertex3f(t2.x, yGround, t2.z);
        
        // Mặt bên 3-0
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, s_shadowColor.a);
        rlVertex3f(b3.x, yGround, b3.z);
        rlVertex3f(b0.x, yGround, b0.z);
        rlColor4ub(s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * 0.3f));
        rlVertex3f(t0.x, yGround, t0.z);
        rlVertex3f(t3.x, yGround, t3.z);
        
        rlEnd();
    }
    
    rlEnableDepthMask(); rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

Vector3 Environment_GetSunDirection(void) { return s_sunDirection; }
void Environment_SetSunDirection(Vector3 dir) { s_sunDirection = Vector3Normalize(dir); }

Color Environment_GetSunColor(void) { return s_sunColor; }
void Environment_SetSunColor(Color col) { s_sunColor = col; }

Color Environment_GetAmbientColor(void) { return s_ambientColor; }
void Environment_SetAmbientColor(Color col) { s_ambientColor = col; }

Color Environment_GetShadowColor(void) { return s_shadowColor; }
void Environment_SetShadowColor(Color col) { s_shadowColor = col; }

EnvFogConfig Environment_GetFogConfig(void) { return s_fogConfig; }
void Environment_SetFogConfig(EnvFogConfig config) { s_fogConfig = config; }

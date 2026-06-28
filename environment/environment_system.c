#include "environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Vector3 s_sunDirection = { -0.4f, -1.0f, 0.6f }; // Hướng mặt trời (bóng đổ về Tây Nam: Left & Down)
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
    
    if (shape == ENV_SHAPE_SPHERE || shape == ENV_SHAPE_CYLINDER) {
        float heightFactor = pos.y / 200.0f;
        float shadowScale = fmaxf(1.0f - heightFactor * 0.4f, 0.1f);
        float shadowAlpha = fmaxf(1.0f - heightFactor * 1.2f, 0.0f);
        
        if (shadowAlpha <= 0.001f) return;
        
        // Đổ bóng trụ đứng xiên: capsule bóng mượt (smooth capsule shadow)
        Vector3 baseCenter = { pos.x + shadowOffset.x * pos.y, yGround, pos.z + shadowOffset.y * pos.y };
        Vector3 topCenter = { pos.x + shadowOffset.x * (pos.y + height * shadowScale), yGround, pos.z + shadowOffset.y * (pos.y + height * shadowScale) };
        
        float dx = topCenter.x - baseCenter.x;
        float dz = topCenter.z - baseCenter.z;
        float len = sqrtf(dx*dx + dz*dz);
        float shadowAngle = 0.0f;
        if (len > 0.001f) shadowAngle = atan2f(dz, dx);
        
        float baseWidth = width * shadowScale;
        float tipWidth = width * 1.2f * shadowScale;
        
        Color colBaseCenter = { s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * shadowAlpha) };
        Color colTipCenter  = { s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)(s_shadowColor.a * shadowAlpha * 0.3f) };
        Color colEdge       = { s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, 0 };
        
        rlBegin(RL_TRIANGLES);
        
        // 1. Base Semi-circle (from shadowAngle + PI/2 to shadowAngle + 3*PI/2)
        int segments = 12;
        for (int i = 0; i < segments; i++) {
            float a1 = shadowAngle + PI/2.0f + ((float)i / segments) * PI;
            float a2 = shadowAngle + PI/2.0f + ((float)(i + 1) / segments) * PI;
            
            rlColor4ub(colBaseCenter.r, colBaseCenter.g, colBaseCenter.b, colBaseCenter.a);
            rlVertex3f(baseCenter.x, yGround, baseCenter.z);
            
            rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);
            rlVertex3f(baseCenter.x + cosf(a2) * baseWidth, yGround, baseCenter.z + sinf(a2) * baseWidth);
            rlVertex3f(baseCenter.x + cosf(a1) * baseWidth, yGround, baseCenter.z + sinf(a1) * baseWidth);
        }
        
        // 2. Right Body Quad (drawn as 2 triangles)
        Vector3 br = { baseCenter.x + cosf(shadowAngle - PI/2.0f) * baseWidth, yGround, baseCenter.z + sinf(shadowAngle - PI/2.0f) * baseWidth };
        Vector3 tr = { topCenter.x + cosf(shadowAngle - PI/2.0f) * tipWidth, yGround, topCenter.z + sinf(shadowAngle - PI/2.0f) * tipWidth };
        
        rlColor4ub(colBaseCenter.r, colBaseCenter.g, colBaseCenter.b, colBaseCenter.a); rlVertex3f(baseCenter.x, yGround, baseCenter.z);
        rlColor4ub(colTipCenter.r, colTipCenter.g, colTipCenter.b, colTipCenter.a);   rlVertex3f(topCenter.x, yGround, topCenter.z);
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(br.x, yGround, br.z);
        
        rlColor4ub(colTipCenter.r, colTipCenter.g, colTipCenter.b, colTipCenter.a);   rlVertex3f(topCenter.x, yGround, topCenter.z);
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(tr.x, yGround, tr.z);
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(br.x, yGround, br.z);
        
        // 3. Left Body Quad (drawn as 2 triangles)
        Vector3 bl = { baseCenter.x + cosf(shadowAngle + PI/2.0f) * baseWidth, yGround, baseCenter.z + sinf(shadowAngle + PI/2.0f) * baseWidth };
        Vector3 tl = { topCenter.x + cosf(shadowAngle + PI/2.0f) * tipWidth, yGround, topCenter.z + sinf(shadowAngle + PI/2.0f) * tipWidth };
        
        rlColor4ub(colBaseCenter.r, colBaseCenter.g, colBaseCenter.b, colBaseCenter.a); rlVertex3f(baseCenter.x, yGround, baseCenter.z);
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(bl.x, yGround, bl.z);
        rlColor4ub(colTipCenter.r, colTipCenter.g, colTipCenter.b, colTipCenter.a);   rlVertex3f(topCenter.x, yGround, topCenter.z);
        
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(bl.x, yGround, bl.z);
        rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);                       rlVertex3f(tl.x, yGround, tl.z);
        rlColor4ub(colTipCenter.r, colTipCenter.g, colTipCenter.b, colTipCenter.a);   rlVertex3f(topCenter.x, yGround, topCenter.z);
        
        // 4. Top Semi-circle (from shadowAngle - PI/2 to shadowAngle + PI/2)
        for (int i = 0; i < segments; i++) {
            float a1 = shadowAngle - PI/2.0f + ((float)i / segments) * PI;
            float a2 = shadowAngle - PI/2.0f + ((float)(i + 1) / segments) * PI;
            
            rlColor4ub(colTipCenter.r, colTipCenter.g, colTipCenter.b, colTipCenter.a);
            rlVertex3f(topCenter.x, yGround, topCenter.z);
            
            rlColor4ub(colEdge.r, colEdge.g, colEdge.b, colEdge.a);
            rlVertex3f(topCenter.x + cosf(a2) * tipWidth, yGround, topCenter.z + sinf(a2) * tipWidth);
            rlVertex3f(topCenter.x + cosf(a1) * tipWidth, yGround, topCenter.z + sinf(a1) * tipWidth);
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

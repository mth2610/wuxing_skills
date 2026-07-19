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

// --- Time-of-Day dynamic lighting cycle state ---
// Static storage only (project-wide no-malloc rule). Inert until a map/system
// opts in via Environment_SetTimeOfDayPresets() + Environment_SetTimeOfDaySpeed().
static EnvLightingPreset s_todPresets[MAX_TIME_OF_DAY_PRESETS];
static float s_todTimePoints[MAX_TIME_OF_DAY_PRESETS];
static int   s_todCount = 0;
static float s_todCurrentTime = 0.0f; // normalized [0,1)
static float s_todSpeed = 0.0f;       // cycles per second, 0 = paused

void Environment_Init(void) {
    s_sunDirection = Vector3Normalize(s_sunDirection);
}

static inline unsigned char EnvLerpByte(unsigned char a, unsigned char b, float t) {
    return (unsigned char)((float)a + ((float)b - (float)a) * t);
}

static inline Color EnvLerpColor(Color a, Color b, float t) {
    Color out;
    out.r = EnvLerpByte(a.r, b.r, t);
    out.g = EnvLerpByte(a.g, b.g, t);
    out.b = EnvLerpByte(a.b, b.b, t);
    out.a = EnvLerpByte(a.a, b.a, t);
    return out;
}

// Blends two lighting presets and writes the result directly into the
// existing engine-wide statics that Environment_DrawSmartShadow() and the
// Get* accessors read — no other function needs to know ToD exists.
static void EnvApplyBlendedPreset(const EnvLightingPreset *a, const EnvLightingPreset *b, float t) {
    s_ambientColor = EnvLerpColor(a->ambientColor, b->ambientColor, t);
    s_sunColor     = EnvLerpColor(a->sunColor, b->sunColor, t);
    s_shadowColor  = EnvLerpColor(a->shadowColor, b->shadowColor, t);

    Vector3 dir = Vector3Lerp(a->sunDirection, b->sunDirection, t);
    s_sunDirection = Vector3Normalize(dir);

    s_fogConfig.color   = EnvLerpColor(a->fog.color, b->fog.color, t);
    s_fogConfig.start   = a->fog.start + (b->fog.start - a->fog.start) * t;
    s_fogConfig.end     = a->fog.end + (b->fog.end - a->fog.end) * t;
    s_fogConfig.density = a->fog.density + (b->fog.density - a->fog.density) * t;
    // fog.enabled can't be interpolated (bool) — see header comment: all
    // presets passed together must agree on it, so either side is fine.
    s_fogConfig.enabled = a->fog.enabled;
}

void Environment_Update(float dt) {
    if (s_todSpeed == 0.0f || s_todCount <= 0) return; // fully inert: zero behavior change

    s_todCurrentTime += s_todSpeed * dt;
    s_todCurrentTime = fmodf(s_todCurrentTime, 1.0f);
    if (s_todCurrentTime < 0.0f) s_todCurrentTime += 1.0f;

    if (s_todCount == 1) {
        // Nothing to interpolate between — just apply the single preset.
        EnvApplyBlendedPreset(&s_todPresets[0], &s_todPresets[0], 0.0f);
        return;
    }

    float t = s_todCurrentTime;

    // Wrap-aware bracket search across sorted s_todTimePoints[0..count-1].
    if (t < s_todTimePoints[0] || t >= s_todTimePoints[s_todCount - 1]) {
        // Segment wraps from the last preset, through 1.0/0.0, to preset[0].
        float segStart = s_todTimePoints[s_todCount - 1];
        float segEnd   = s_todTimePoints[0] + 1.0f;
        float tt = (t < s_todTimePoints[0]) ? (t + 1.0f) : t;
        float span = segEnd - segStart;
        float f = (span > 0.00001f) ? (tt - segStart) / span : 0.0f;
        EnvApplyBlendedPreset(&s_todPresets[s_todCount - 1], &s_todPresets[0], f);
        return;
    }

    for (int i = 0; i < s_todCount - 1; i++) {
        if (t >= s_todTimePoints[i] && t < s_todTimePoints[i + 1]) {
            float span = s_todTimePoints[i + 1] - s_todTimePoints[i];
            float f = (span > 0.00001f) ? (t - s_todTimePoints[i]) / span : 0.0f;
            EnvApplyBlendedPreset(&s_todPresets[i], &s_todPresets[i + 1], f);
            return;
        }
    }
}

void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height) {
    if (s_sunDirection.y >= -0.01f) return;
    
    float skewFactor = 1.0f / fabsf(s_sunDirection.y);
    Vector2 shadowOffset = { s_sunDirection.x * skewFactor, s_sunDirection.z * skewFactor };
    
    float yGround = 0.09f; // Nâng nhẹ để tránh lỗi nhấp nháy Z-fighting và đè lên grid
    
    rlSetTexture(0);
    rlDrawRenderBatchActive(); // Bắt buộc xả batch trước khi đổi state!
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
    
    rlDrawRenderBatchActive(); // Bắt buộc xả batch của shadow trước khi đổi state!
    rlEnableDepthMask(); rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

Vector3 Environment_GetSunDirection(void) { return s_sunDirection; }
void Environment_SetSunDirection(Vector3 dir) { s_sunDirection = Vector3Normalize(dir); }

Color Environment_GetSunColor(void) { return s_sunColor; }
void Environment_SetSunColor(Color col) { s_sunColor = col; }

Color Environment_GetAmbientColor(void) { return s_ambientColor; }
void Environment_SetAmbientColor(Color col) { s_ambientColor = col; }

// Real Shading P1c — hemispheric split derived from the existing flat
// ambient (no preset-struct change yet): sky brighter/cooler, ground dimmer
// and slightly warm (bounce light off the arena floor).
static inline unsigned char EnvClampByteF(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)v;
}

Color Environment_GetSkyAmbient(void) {
    return (Color){
        EnvClampByteF(s_ambientColor.r * 1.25f),
        EnvClampByteF(s_ambientColor.g * 1.25f),
        EnvClampByteF(s_ambientColor.b * 1.35f),
        255
    };
}

Color Environment_GetGroundAmbient(void) {
    return (Color){
        EnvClampByteF(s_ambientColor.r * 0.55f),
        EnvClampByteF(s_ambientColor.g * 0.45f),
        EnvClampByteF(s_ambientColor.b * 0.40f),
        255
    };
}

Color Environment_GetShadowColor(void) { return s_shadowColor; }
void Environment_SetShadowColor(Color col) { s_shadowColor = col; }

EnvFogConfig Environment_GetFogConfig(void) { return s_fogConfig; }
void Environment_SetFogConfig(EnvFogConfig config) { s_fogConfig = config; }

void Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count) {
    if (count < 0) count = 0;
    if (count > MAX_TIME_OF_DAY_PRESETS) count = MAX_TIME_OF_DAY_PRESETS;

    s_todCount = count;
    for (int i = 0; i < count; i++) {
        s_todPresets[i] = presets[i];
        s_todPresets[i].sunDirection = Vector3Normalize(s_todPresets[i].sunDirection);
        s_todTimePoints[i] = timePoints[i];
    }
}

void Environment_SetTimeOfDaySpeed(float cyclesPerSecond) {
    s_todSpeed = cyclesPerSecond;
}

void Environment_SetTimeOfDay(float t) {
    t = fmodf(t, 1.0f);
    if (t < 0.0f) t += 1.0f;
    s_todCurrentTime = t;
}

float Environment_GetTimeOfDay(void) { return s_todCurrentTime; }

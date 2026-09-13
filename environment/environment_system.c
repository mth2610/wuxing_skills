#include "environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Real Shading P6 — moderate elevation (was y=-1.0 near-vertical, then a
// debug-era y=-0.5 which stretched shadows to 1.7x height and exaggerated
// the gap left by sparsely-rasterized legs). y=-0.7 (~40° elevation) keeps a
// clearly visible raking shadow (~1.2x height) without the exaggeration;
// same Tây Nam (southwest) horizontal direction as before.
static Vector3 s_sunDirection = { -0.6f, -0.7f, 0.6f }; // Hướng mặt trời (bóng đổ về Tây Nam)
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

static AtmosphereProfile s_atmosphereProfile = {
    .color = { 30, 30, 40, 255 },
    .start = 200.0f,
    .end = 1200.0f,
    .enabled = false,
    .optics = {
        .rayleighLMS = { 0.0076224f, 0.012935f, 0.024845f }, // Schüler/Patry D65 LMS Rayleigh coefficients
        .mieScattering = 0.002f,
        .mieAnisotropy = 0.80f,                              // Strong forward crepuscular scattering
        .multipleScatteringAmp = 2.16f                       // Multiple scattering boost for albedo ~0.9
    },
    .density = {
        .baseDensity = 0.001f,
        .heightFalloff = 0.05f,                              // Exponential decay along Y axis
        .baseAltitude = 0.0f,
        .enableSigmoidLayer = false,
        .layerAltitude = 5.0f,
        .layerThickness = 3.0f,
        .layerDensity = 0.005f
    }
};

static LocalFogVolume s_fogVolumes[MAX_LOCAL_FOG_VOLUMES] = { 0 };
static int            s_nextFogVolumeId = 1;

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

    // Synchronize full AtmosphereProfile with blended results
    s_atmosphereProfile.color   = s_fogConfig.color;
    s_atmosphereProfile.start   = s_fogConfig.start;
    s_atmosphereProfile.end     = s_fogConfig.end;
    s_atmosphereProfile.enabled = s_fogConfig.enabled;
    s_atmosphereProfile.density.baseDensity = s_fogConfig.density;

    s_atmosphereProfile.optics.mieScattering = a->atmosphere.optics.mieScattering + (b->atmosphere.optics.mieScattering - a->atmosphere.optics.mieScattering) * t;
    s_atmosphereProfile.optics.mieAnisotropy = a->atmosphere.optics.mieAnisotropy + (b->atmosphere.optics.mieAnisotropy - a->atmosphere.optics.mieAnisotropy) * t;
    s_atmosphereProfile.optics.multipleScatteringAmp = a->atmosphere.optics.multipleScatteringAmp + (b->atmosphere.optics.multipleScatteringAmp - a->atmosphere.optics.multipleScatteringAmp) * t;

    s_atmosphereProfile.density.heightFalloff = a->atmosphere.density.heightFalloff + (b->atmosphere.density.heightFalloff - a->atmosphere.density.heightFalloff) * t;
    s_atmosphereProfile.density.baseAltitude  = a->atmosphere.density.baseAltitude  + (b->atmosphere.density.baseAltitude  - a->atmosphere.density.baseAltitude)  * t;
    s_atmosphereProfile.density.enableSigmoidLayer = a->atmosphere.density.enableSigmoidLayer;
    s_atmosphereProfile.density.layerAltitude = a->atmosphere.density.layerAltitude + (b->atmosphere.density.layerAltitude - a->atmosphere.density.layerAltitude) * t;
    s_atmosphereProfile.density.layerThickness = a->atmosphere.density.layerThickness + (b->atmosphere.density.layerThickness - a->atmosphere.density.layerThickness) * t;
    s_atmosphereProfile.density.layerDensity = a->atmosphere.density.layerDensity + (b->atmosphere.density.layerDensity - a->atmosphere.density.layerDensity) * t;
}

void Environment_Update(float dt) {
    // 1. Update transient Local Fog Volumes (wind drift & lifetime decay)
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (!s_fogVolumes[i].active) continue;
        if (s_fogVolumes[i].maxLifetime > 0.0f) {
            s_fogVolumes[i].lifetime -= dt;
            if (s_fogVolumes[i].lifetime <= 0.0f) {
                s_fogVolumes[i].active = false;
                s_fogVolumes[i].id = 0;
                continue;
            }
        }
        if (s_fogVolumes[i].driftVelocity.x != 0.0f || s_fogVolumes[i].driftVelocity.y != 0.0f || s_fogVolumes[i].driftVelocity.z != 0.0f) {
            s_fogVolumes[i].position.x += s_fogVolumes[i].driftVelocity.x * dt;
            s_fogVolumes[i].position.y += s_fogVolumes[i].driftVelocity.y * dt;
            s_fogVolumes[i].position.z += s_fogVolumes[i].driftVelocity.z * dt;
        }
    }

    // 2. Update Time-of-Day cycle
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

static unsigned int s_lightingVersion = 1;

Vector3 Environment_GetSunDirection(void) { return s_sunDirection; }
void Environment_SetSunDirection(Vector3 dir) { s_sunDirection = Vector3Normalize(dir); s_lightingVersion++; }

Color Environment_GetSunColor(void) { return s_sunColor; }
void Environment_SetSunColor(Color col) { s_sunColor = col; s_lightingVersion++; }

Color Environment_GetAmbientColor(void) { return s_ambientColor; }
void Environment_SetAmbientColor(Color col) { s_ambientColor = col; s_lightingVersion++; }

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
void Environment_SetShadowColor(Color col) { s_shadowColor = col; s_lightingVersion++; }

EnvFogConfig Environment_GetFogConfig(void) { return s_fogConfig; }
void Environment_SetFogConfig(EnvFogConfig config) {
    s_fogConfig = config;
    s_atmosphereProfile.color = config.color;
    s_atmosphereProfile.start = config.start;
    s_atmosphereProfile.end = config.end;
    s_atmosphereProfile.enabled = config.enabled;
    s_atmosphereProfile.density.baseDensity = config.density;
    s_lightingVersion++;
}

AtmosphereProfile Environment_GetAtmosphereProfile(void) { return s_atmosphereProfile; }
void Environment_SetAtmosphereProfile(const AtmosphereProfile *profile) {
    if (!profile) return;
    s_atmosphereProfile = *profile;
    s_fogConfig.color = profile->color;
    s_fogConfig.start = profile->start;
    s_fogConfig.end = profile->end;
    s_fogConfig.density = profile->density.baseDensity;
    s_fogConfig.enabled = profile->enabled;
    s_lightingVersion++;
}

// Local Fog Volume Manager
int FogVolume_Create(const LocalFogVolume *volume) {
    if (!volume) return 0;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (!s_fogVolumes[i].active) {
            s_fogVolumes[i] = *volume;
            s_fogVolumes[i].id = s_nextFogVolumeId++;
            if (s_nextFogVolumeId <= 0) s_nextFogVolumeId = 1;
            s_fogVolumes[i].active = true;
            return s_fogVolumes[i].id;
        }
    }
    return 0; // Pool full
}

void FogVolume_Update(int id, const LocalFogVolume *volume) {
    if (id <= 0 || !volume) return;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (s_fogVolumes[i].active && s_fogVolumes[i].id == id) {
            int savedId = s_fogVolumes[i].id;
            s_fogVolumes[i] = *volume;
            s_fogVolumes[i].id = savedId;
            s_fogVolumes[i].active = true;
            return;
        }
    }
}

void FogVolume_Destroy(int id) {
    if (id <= 0) return;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (s_fogVolumes[i].active && s_fogVolumes[i].id == id) {
            s_fogVolumes[i].active = false;
            s_fogVolumes[i].id = 0;
            return;
        }
    }
}

void FogVolume_ClearAll(void) {
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        s_fogVolumes[i].active = false;
        s_fogVolumes[i].id = 0;
    }
}

int FogVolume_SpawnTransient(Vector3 pos, float radius, Color color, float density, float duration) {
    LocalFogVolume v = { 0 };
    v.shape = FOG_SHAPE_SPHERE;
    v.position = pos;
    v.extents = (Vector3){ radius, radius, radius };
    v.color = color;
    v.density = density;
    v.edgeSoftness = 0.8f;
    v.lifetime = duration;
    v.maxLifetime = duration;
    v.active = true;
    return FogVolume_Create(&v);
}

int FogVolume_GetActiveCount(void) {
    int count = 0;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (s_fogVolumes[i].active) count++;
    }
    return count;
}

const LocalFogVolume* FogVolume_GetByIndex(int index) {
    int current = 0;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (s_fogVolumes[i].active) {
            if (current == index) return &s_fogVolumes[i];
            current++;
        }
    }
    return NULL;
}

const LocalFogVolume* FogVolume_GetById(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++) {
        if (s_fogVolumes[i].active && s_fogVolumes[i].id == id) {
            return &s_fogVolumes[i];
        }
    }
    return NULL;
}

EnvFrameLighting Environment_GetFrameLighting(void) {
    EnvFrameLighting frame;
    frame.sunDirection = s_sunDirection;
    frame.sunColor = s_sunColor;
    frame.skyAmbient = Environment_GetSkyAmbient();
    frame.groundBounce = Environment_GetGroundAmbient();
    frame.shadowColor = s_shadowColor;
    frame.fog = s_fogConfig;
    frame.atmosphere = s_atmosphereProfile;
    frame.version = s_lightingVersion;
    return frame;
}

void Environment_ApplyProfile(const EnvLightingPreset *profile) {
    if (!profile) return;
    s_ambientColor = profile->ambientColor;
    s_sunColor = profile->sunColor;
    s_sunDirection = Vector3Normalize(profile->sunDirection);
    s_shadowColor = profile->shadowColor;
    s_fogConfig = profile->fog;
    s_atmosphereProfile = profile->atmosphere;
    // Keep them in sync
    s_atmosphereProfile.color = s_fogConfig.color;
    s_atmosphereProfile.start = s_fogConfig.start;
    s_atmosphereProfile.end = s_fogConfig.end;
    s_atmosphereProfile.enabled = s_fogConfig.enabled;
    s_lightingVersion++;
}

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



#include "environment_system.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdbool.h>

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
static float s_sunIntensity = 1.0f;
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

// Declared before the Time-of-Day blend so continuous ToD changes can
// invalidate consumers that cache GPU uploads by version.
static unsigned int s_lightingVersion = 1;

// --- Time-of-Day dynamic lighting cycle state ---
// Static storage only (project-wide no-malloc rule). Inert until a map/system
// opts in via Environment_SetTimeOfDayPresets() + Environment_SetTimeOfDaySpeed().
static EnvLightingPreset s_todPresets[MAX_TIME_OF_DAY_PRESETS];
static float s_todTimePoints[MAX_TIME_OF_DAY_PRESETS];
static int   s_todCount = 0;
static float s_todCurrentTime = 0.0f; // normalized [0,1)
static float s_todSpeed = 0.0f;       // cycles per second, 0 = paused

static EnvCloudShadowConfig s_cloudShadow = {
    .enabled = false,
    .strength = 0.22f,
    .worldSize = 96.0f,
    .planeHeight = 80.0f,
    .coverage = 0.48f,
    .softness = 0.16f,
    .windSpeedScale = 0.55f
};
static Vector2 s_cloudDriftUV = { 0 };
static unsigned int s_cloudVersion = 1;
static Texture2D s_cloudNoise = { 0 };
#define ENV_CLOUD_SHADER_CACHE_CAPACITY 16
#define ENV_CLOUD_TEXTURE_SLOT 3
typedef struct {
    unsigned int shaderId;
    int uvLocation, shapeLocation, projectionLocation, noiseLocation;
} EnvCloudShaderLocations;
static EnvCloudShaderLocations s_cloudShaderLocations[ENV_CLOUD_SHADER_CACHE_CAPACITY];
static unsigned int s_cloudShaderReplacement = 0;

static float EnvCloudClamp(float value, float minimum, float maximum, float fallback) {
    if (!isfinite(value)) return fallback;
    return fminf(fmaxf(value, minimum), maximum);
}

static inline bool EnvColorEq(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static inline bool EnvVec3Eq(Vector3 a, Vector3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// The cloud projection is derived from the sun direction, so any sun change
// must invalidate both the lighting frame and the cloud frame.
static inline void EnvSunDirectionChanged(void) {
    s_lightingVersion++;
    s_cloudVersion++;
}

static void EnvUpdateCloudShadow(float dt) {
    if (!s_cloudShadow.enabled || !isfinite(dt) || dt <= 0.0f) return;
    // Cloud transport follows the ambient airflow only; local skill vortices
    // and terrain lift must not drag the sky across individual receivers.
    WindMacroConfig wind = Wind_GetMacro();
    double scale = (double)s_cloudShadow.windSpeedScale / s_cloudShadow.worldSize;
    if (isfinite(wind.baseDirection.x)) {
        double phase = (double)s_cloudDriftUV.x + wind.baseDirection.x * scale * dt;
        s_cloudDriftUV.x = (float)(phase - floor(phase));
    }
    if (isfinite(wind.baseDirection.z)) {
        double phase = (double)s_cloudDriftUV.y + wind.baseDirection.z * scale * dt;
        s_cloudDriftUV.y = (float)(phase - floor(phase));
    }
}

// Forward declaration: the shadow batch flag is also reset by Environment_Init.
static bool s_shadowBatchOpen = false;

void Environment_Init(void) {
    s_sunDirection = Vector3Normalize(s_sunDirection);
    s_sunIntensity = 1.0f;
    s_cloudShadow.enabled = false;
    s_cloudDriftUV = (Vector2){ 0 };
    s_cloudVersion++;
    s_cloudNoise = (Texture2D){ 0 };
    for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++) s_cloudShaderLocations[i].shaderId = 0;
    s_cloudShaderReplacement = 0;
    s_shadowBatchOpen = false;
}

// Rounded (not truncated) so slow Time-of-Day transitions don't bias every
// channel toward the darker end and step visibly one level at a time.
static inline unsigned char EnvLerpByte(unsigned char a, unsigned char b, float t) {
    float v = (float)a + ((float)b - (float)a) * t + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (unsigned char)v;
}

static inline Color EnvLerpColor(Color a, Color b, float t) {
    Color out;
    out.r = EnvLerpByte(a.r, b.r, t);
    out.g = EnvLerpByte(a.g, b.g, t);
    out.b = EnvLerpByte(a.b, b.b, t);
    out.a = EnvLerpByte(a.a, b.a, t);
    return out;
}

// Spherical interpolation of two unit directions so the sun (and its shadows)
// sweep at constant angular speed. Plain lerp+normalize speeds up mid-segment
// and collapses to a zero vector for opposite directions.
static Vector3 EnvSlerpDirection(Vector3 a, Vector3 b, float t) {
    float d = Vector3DotProduct(a, b);
    if (d > -0.9995f && d < 0.9995f) {
        float theta = acosf(d);
        float invSin = 1.0f / sinf(theta);
        float wa = sinf((1.0f - t) * theta) * invSin;
        float wb = sinf(t * theta) * invSin;
        return Vector3Normalize((Vector3){
            a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb });
    }
    Vector3 l = Vector3Lerp(a, b, t);
    if (Vector3LengthSqr(l) > 1e-6f) return Vector3Normalize(l);
    return t < 0.5f ? a : b; // exactly opposite: no unique path, avoid NaN/zero
}

// Blends two lighting presets and writes the result directly into the
// existing engine-wide statics that Environment_DrawSmartShadow() and the
// Get* accessors read — no other function needs to know ToD exists.
static void EnvApplyBlendedPreset(const EnvLightingPreset *a, const EnvLightingPreset *b, float t) {
    s_ambientColor = EnvLerpColor(a->ambientColor, b->ambientColor, t);
    s_sunColor     = EnvLerpColor(a->sunColor, b->sunColor, t);
    s_shadowColor  = EnvLerpColor(a->shadowColor, b->shadowColor, t);

    // Presets are normalized at registration (Environment_SetTimeOfDayPresets).
    s_sunDirection = EnvSlerpDirection(a->sunDirection, b->sunDirection, t);

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

    // Every other setter bumps the version; the ToD path used to skip it, so
    // version-keyed consumers never saw the cycle move.
    EnvSunDirectionChanged();
}

void Environment_Update(float dt) {
    EnvUpdateCloudShadow(dt);
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

// ---------------------------------------------------------------------------
// Shadow decals
//
// Each decal used to flush the render batch twice (before and after toggling
// depth state), which turns N shadows into 2N draw calls. The state toggle is
// now separable: wrap a group of Environment_DrawSmartShadow() calls in
// Environment_BeginShadowBatch()/Environment_EndShadowBatch() and they share
// one pair of flushes. Un-wrapped calls behave exactly as before.
// ---------------------------------------------------------------------------
#define ENV_SHADOW_SEGMENTS 12
// Low sun makes 1/|sun.y| explode (100x height at y=-0.01). Cap the stretch
// at ~7° elevation and fade the decal out toward the horizon instead of
// popping it off at the hard cutoff.
#define ENV_SHADOW_MAX_SKEW 8.0f
#define ENV_SHADOW_FADE_START 0.01f
#define ENV_SHADOW_FADE_RANGE 0.14f

// cos/sin of i*PI/N, shared by every capsule decal (was ~100 sinf/cosf per decal).
static float s_shadowArcCos[ENV_SHADOW_SEGMENTS + 1];
static float s_shadowArcSin[ENV_SHADOW_SEGMENTS + 1];
static bool  s_shadowArcReady = false;

static void EnvBuildShadowArc(void) {
    for (int i = 0; i <= ENV_SHADOW_SEGMENTS; i++) {
        float a = ((float)i / (float)ENV_SHADOW_SEGMENTS) * PI;
        s_shadowArcCos[i] = cosf(a);
        s_shadowArcSin[i] = sinf(a);
    }
    s_shadowArcReady = true;
}

typedef struct {
    float yGround;
    float alpha;       // overall opacity: horizon fade * altitude fade
    float hAbove;      // sphere/cylinder: height above ground
    float scale;       // sphere/cylinder: altitude shrink factor
    float offX, offZ;  // ground displacement per unit of height
} EnvShadowPrep;

static inline Color EnvShadowTint(float alphaScale) {
    float a = (float)s_shadowColor.a * alphaScale;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return (Color){ s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)a };
}

static inline void EnvShadowVertex(Color c, float x, float y, float z) {
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlVertex3f(x, y, z);
}

// All the early-outs and map queries run BEFORE any GL state changes. The old
// sphere/cylinder path returned after disabling depth test/mask/culling and
// never restored them whenever the shadow had fully faded.
static bool EnvPrepareShadow(Vector3 pos, EnvShadowShapeType shape, EnvShadowPrep *out) {
    if (shape != ENV_SHAPE_SPHERE && shape != ENV_SHAPE_CYLINDER && shape != ENV_SHAPE_BOX) return false;

    float elevation = -s_sunDirection.y;
    float fade = fminf(fmaxf((elevation - ENV_SHADOW_FADE_START) / ENV_SHADOW_FADE_RANGE, 0.0f), 1.0f);
    if (fade <= 0.001f) return false; // sun at/below horizon (same cutoff as before)
    fade = fade * fade * (3.0f - 2.0f * fade);
    float skew = 1.0f / fmaxf(elevation, 1.0f / ENV_SHADOW_MAX_SKEW);

    float waterSurfaceY = 0.0f, waterDepth = 0.0f;
    if (MapManager_GetWaterInfoAt(pos.x, pos.z, &waterSurfaceY, &waterDepth)) {
        if (pos.y < waterSurfaceY + 0.05f) return false;
    }

    out->yGround = MapManager_GetGroundHeightAt(pos.x, pos.z) + 0.02f;
    out->offX = s_sunDirection.x * skew;
    out->offZ = s_sunDirection.z * skew;
    out->alpha = fade;
    out->hAbove = 0.0f;
    out->scale = 1.0f;

    if (shape != ENV_SHAPE_BOX) {
        float hAbove = fmaxf(pos.y - out->yGround, 0.0f);
        float heightFactor = hAbove / 200.0f;
        out->hAbove = hAbove;
        out->scale = fmaxf(1.0f - heightFactor * 0.4f, 0.1f);
        out->alpha *= fmaxf(1.0f - heightFactor * 1.2f, 0.0f);
        if (out->alpha <= 0.001f) return false;
    }
    return true;
}

// Geometry only — assumes depth/cull state is already set up. Everything is
// RL_TRIANGLES so a whole batch of decals stays a single draw call.
static void EnvEmitShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height,
                          const EnvShadowPrep *prep) {
    float yGround = prep->yGround;
    Color cBase = EnvShadowTint(prep->alpha);
    Color cTip  = EnvShadowTint(prep->alpha * 0.3f);
    Color cEdge = { s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, 0 };

    if (shape == ENV_SHAPE_SPHERE || shape == ENV_SHAPE_CYLINDER) {
        if (!s_shadowArcReady) EnvBuildShadowArc();

        float hAbove = prep->hAbove;
        float shadowScale = prep->scale;

        // Đổ bóng trụ đứng xiên: capsule bóng mượt (smooth capsule shadow)
        float reach = height * shadowScale;
        float bx = pos.x + prep->offX * hAbove;
        float bz = pos.z + prep->offZ * hAbove;
        float tx = pos.x + prep->offX * (hAbove + reach);
        float tz = pos.z + prep->offZ * (hAbove + reach);

        // Shadow axis f and its left perpendicular p, taken straight from the
        // displacement vector instead of atan2 + sin/cos per vertex.
        float dx = tx - bx, dz = tz - bz;
        float len = sqrtf(dx * dx + dz * dz);
        float fx = 1.0f, fz = 0.0f;
        if (len > 0.001f) { fx = dx / len; fz = dz / len; }
        float px = -fz, pz = fx;

        float baseWidth = width * shadowScale;
        float tipWidth = width * 1.2f * shadowScale;

        // Arc points (i = 0..N). Base arc sweeps the half-disc behind the
        // base, top arc the half-disc ahead of the tip.
        float baseArcX[ENV_SHADOW_SEGMENTS + 1], baseArcZ[ENV_SHADOW_SEGMENTS + 1];
        float topArcX[ENV_SHADOW_SEGMENTS + 1],  topArcZ[ENV_SHADOW_SEGMENTS + 1];
        for (int i = 0; i <= ENV_SHADOW_SEGMENTS; i++) {
            float c = s_shadowArcCos[i], s = s_shadowArcSin[i];
            baseArcX[i] = bx + (-fx * s + px * c) * baseWidth;
            baseArcZ[i] = bz + (-fz * s + pz * c) * baseWidth;
            topArcX[i]  = tx + ( fx * s - px * c) * tipWidth;
            topArcZ[i]  = tz + ( fz * s - pz * c) * tipWidth;
        }

        rlBegin(RL_TRIANGLES);

        // 1. Base semi-circle
        for (int i = 0; i < ENV_SHADOW_SEGMENTS; i++) {
            EnvShadowVertex(cBase, bx, yGround, bz);
            EnvShadowVertex(cEdge, baseArcX[i + 1], yGround, baseArcZ[i + 1]);
            EnvShadowVertex(cEdge, baseArcX[i], yGround, baseArcZ[i]);
        }

        // 2. Right body quad (2 triangles): edge points at -p
        float brx = bx - px * baseWidth, brz = bz - pz * baseWidth;
        float trx = tx - px * tipWidth,  trz = tz - pz * tipWidth;
        EnvShadowVertex(cBase, bx, yGround, bz);
        EnvShadowVertex(cTip,  tx, yGround, tz);
        EnvShadowVertex(cEdge, brx, yGround, brz);

        EnvShadowVertex(cTip,  tx, yGround, tz);
        EnvShadowVertex(cEdge, trx, yGround, trz);
        EnvShadowVertex(cEdge, brx, yGround, brz);

        // 3. Left body quad (2 triangles): edge points at +p
        float blx = bx + px * baseWidth, blz = bz + pz * baseWidth;
        float tlx = tx + px * tipWidth,  tlz = tz + pz * tipWidth;
        EnvShadowVertex(cBase, bx, yGround, bz);
        EnvShadowVertex(cEdge, blx, yGround, blz);
        EnvShadowVertex(cTip,  tx, yGround, tz);

        EnvShadowVertex(cEdge, blx, yGround, blz);
        EnvShadowVertex(cEdge, tlx, yGround, tlz);
        EnvShadowVertex(cTip,  tx, yGround, tz);

        // 4. Top semi-circle
        for (int i = 0; i < ENV_SHADOW_SEGMENTS; i++) {
            EnvShadowVertex(cTip,  tx, yGround, tz);
            EnvShadowVertex(cEdge, topArcX[i + 1], yGround, topArcZ[i + 1]);
            EnvShadowVertex(cEdge, topArcX[i], yGround, topArcZ[i]);
        }

        rlEnd();
    }
    else if (shape == ENV_SHAPE_BOX) {
        // Đổ bóng hộp đứng xiên. The swept footprint is the base square plus one
        // parallelogram per leading edge (an edge whose outward normal faces the
        // sweep direction). These pieces tile the footprint without overlapping,
        // so translucent regions are no longer blended twice into dark seams.
        float ox = prep->offX * height, oz = prep->offZ * height;
        float x0 = pos.x - width, x1 = pos.x + width;
        float z0 = pos.z - width, z1 = pos.z + width;
        const float eps = 1e-4f;

        rlBegin(RL_TRIANGLES);

        // Mặt đáy (đậm)
        EnvShadowVertex(cBase, x0, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z1);
        EnvShadowVertex(cBase, x0, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z1);
        EnvShadowVertex(cBase, x0, yGround, z1);

        // Leading X edge (right if sweeping +x, left if -x): dark at the base
        // edge, fading to the tip alpha at the far edge.
        if (ox > eps || ox < -eps) {
            float ex = ox > 0.0f ? x1 : x0;
            EnvShadowVertex(cBase, ex, yGround, z0);
            EnvShadowVertex(cBase, ex, yGround, z1);
            EnvShadowVertex(cTip,  ex + ox, yGround, z1 + oz);
            EnvShadowVertex(cBase, ex, yGround, z0);
            EnvShadowVertex(cTip,  ex + ox, yGround, z1 + oz);
            EnvShadowVertex(cTip,  ex + ox, yGround, z0 + oz);
        }
        // Leading Z edge
        if (oz > eps || oz < -eps) {
            float ez = oz > 0.0f ? z1 : z0;
            EnvShadowVertex(cBase, x0, yGround, ez);
            EnvShadowVertex(cBase, x1, yGround, ez);
            EnvShadowVertex(cTip,  x1 + ox, yGround, ez + oz);
            EnvShadowVertex(cBase, x0, yGround, ez);
            EnvShadowVertex(cTip,  x1 + ox, yGround, ez + oz);
            EnvShadowVertex(cTip,  x0 + ox, yGround, ez + oz);
        }

        rlEnd();
    }
}

static void EnvShadowStateBegin(void) {
    rlSetTexture(0);
    rlDrawRenderBatchActive(); // Bắt buộc xả batch trước khi đổi state!
    rlDisableDepthTest();
    rlDisableDepthMask(); rlDisableBackfaceCulling();
}

static void EnvShadowStateEnd(void) {
    rlDrawRenderBatchActive(); // Bắt buộc xả batch của shadow trước khi đổi state!
    rlEnableDepthMask(); rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

// Optional grouping: between Begin and End, Environment_DrawSmartShadow() only
// emits geometry (no flush, no state toggle). Do not draw anything else in
// between. Nested/unbalanced calls are ignored safely.
void Environment_BeginShadowBatch(void) {
    if (s_shadowBatchOpen) return;
    s_shadowBatchOpen = true;
    EnvShadowStateBegin();
}

void Environment_EndShadowBatch(void) {
    if (!s_shadowBatchOpen) return;
    s_shadowBatchOpen = false;
    EnvShadowStateEnd();
}

void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height) {
    EnvShadowPrep prep;
    if (!EnvPrepareShadow(pos, shape, &prep)) return;

    if (s_shadowBatchOpen) {
        EnvEmitShadow(pos, shape, width, height, &prep);
        return;
    }
    EnvShadowStateBegin();
    EnvEmitShadow(pos, shape, width, height, &prep);
    EnvShadowStateEnd();
}

Vector3 Environment_GetSunDirection(void) { return s_sunDirection; }
void Environment_SetSunDirection(Vector3 dir) {
    Vector3 n = Vector3Normalize(dir);
    if (EnvVec3Eq(n, s_sunDirection)) return;
    s_sunDirection = n;
    EnvSunDirectionChanged();
}

Color Environment_GetSunColor(void) { return s_sunColor; }
void Environment_SetSunColor(Color col) {
    if (EnvColorEq(col, s_sunColor)) return;
    s_sunColor = col;
    s_lightingVersion++;
}

float Environment_GetSunIntensity(void) { return s_sunIntensity; }
void Environment_SetSunIntensity(float intensity) {
    float resolved = isfinite(intensity) ? fminf(fmaxf(intensity, 0.0f), 64.0f) : 1.0f;
    if (resolved != s_sunIntensity) {
        s_sunIntensity = resolved;
        s_lightingVersion++;
    }
}

Color Environment_GetAmbientColor(void) { return s_ambientColor; }
void Environment_SetAmbientColor(Color col) {
    if (EnvColorEq(col, s_ambientColor)) return;
    s_ambientColor = col;
    s_lightingVersion++;
}

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
void Environment_SetShadowColor(Color col) {
    if (EnvColorEq(col, s_shadowColor)) return;
    s_shadowColor = col;
    s_lightingVersion++;
}

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
    frame.sunIntensity = s_sunIntensity;
    return frame;
}

EnvCloudShadowConfig Environment_GetCloudShadowConfig(void) {
    return s_cloudShadow;
}

void Environment_SetCloudShadowConfig(const EnvCloudShadowConfig *config) {
    if (!config) {
        s_cloudShadow.enabled = false;
        s_cloudDriftUV = (Vector2){ 0 };
        s_cloudVersion++;
        return;
    }
    s_cloudShadow.enabled = config->enabled;
    s_cloudShadow.strength = EnvCloudClamp(config->strength, 0.0f, 1.0f, 0.22f);
    s_cloudShadow.worldSize = EnvCloudClamp(config->worldSize, 8.0f, 4096.0f, 96.0f);
    s_cloudShadow.planeHeight = EnvCloudClamp(config->planeHeight, 1.0f, 10000.0f, 80.0f);
    s_cloudShadow.coverage = EnvCloudClamp(config->coverage, 0.0f, 1.0f, 0.48f);
    s_cloudShadow.softness = EnvCloudClamp(config->softness, 0.01f, 0.5f, 0.16f);
    s_cloudShadow.windSpeedScale = EnvCloudClamp(config->windSpeedScale, 0.0f, 4.0f, 0.55f);
    if (s_cloudShadow.enabled && !s_cloudNoise.id) {
        s_cloudNoise = ResourceManager_LoadTexture("environment/textures/cloud_noise.png");
        if (s_cloudNoise.id) {
            SetTextureFilter(s_cloudNoise, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_cloudNoise, TEXTURE_WRAP_REPEAT);
        }
    }
    s_cloudVersion++;
}

EnvCloudShadowFrame Environment_GetCloudShadowFrame(void) {
    EnvCloudShadowFrame frame;
    frame.noiseTexture = s_cloudNoise;
    float sunElevation = fmaxf(-s_sunDirection.y, 0.15f);
    // Fade near/below the horizon rather than creating an unbounded projection.
    float daylight = EnvCloudClamp((-s_sunDirection.y - 0.02f) / 0.13f, 0.0f, 1.0f, 0.0f);
    frame.uvTransform = (Vector4){
        1.0f / s_cloudShadow.worldSize, s_cloudDriftUV.x, s_cloudDriftUV.y,
        s_cloudShadow.enabled && s_cloudNoise.id ? s_cloudShadow.strength * daylight : 0.0f
    };
    frame.shape = (Vector4){ s_cloudShadow.coverage, s_cloudShadow.softness, s_cloudShadow.planeHeight, 0.0f };
    frame.projection = (Vector2){ s_sunDirection.x / sunElevation, s_sunDirection.z / sunElevation };
    frame.version = s_cloudVersion;
    return frame;
}

void Environment_BindCloudShadowShader(Shader shader) {
    if (!shader.id) return;
    EnvCloudShaderLocations *locations = NULL;
    for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++) {
        if (s_cloudShaderLocations[i].shaderId == shader.id) {
            locations = &s_cloudShaderLocations[i];
            break;
        }
    }
    if (!locations) {
        for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++) {
            if (!s_cloudShaderLocations[i].shaderId) { locations = &s_cloudShaderLocations[i]; break; }
        }
        if (!locations) locations = &s_cloudShaderLocations[s_cloudShaderReplacement++ % ENV_CLOUD_SHADER_CACHE_CAPACITY];
        locations->shaderId = shader.id;
        locations->uvLocation = GetShaderLocation(shader, "u_cloudUV");
        locations->shapeLocation = GetShaderLocation(shader, "u_cloudShape");
        locations->projectionLocation = GetShaderLocation(shader, "u_cloudProjection");
        locations->noiseLocation = GetShaderLocation(shader, "u_cloudNoise");
    }
    EnvCloudShadowFrame frame = Environment_GetCloudShadowFrame();
    if (locations->uvLocation >= 0) SetShaderValue(shader, locations->uvLocation, &frame.uvTransform, SHADER_UNIFORM_VEC4);
    if (locations->shapeLocation >= 0) SetShaderValue(shader, locations->shapeLocation, &frame.shape, SHADER_UNIFORM_VEC4);
    if (locations->projectionLocation >= 0) SetShaderValue(shader, locations->projectionLocation, &frame.projection, SHADER_UNIFORM_VEC2);
    if (locations->noiseLocation >= 0 && frame.noiseTexture.id) {
        int slot = ENV_CLOUD_TEXTURE_SLOT;
        SetShaderValue(shader, locations->noiseLocation, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(slot);
        rlEnableTexture(frame.noiseTexture.id);
        rlActiveTextureSlot(0);
    }
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
    EnvSunDirectionChanged();
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

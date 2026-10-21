#include "environment_system.h"
#include "maps/map_manager.h"
#include "environment/wind/wind_system.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h> // getenv, strtof (read once, see EnvTodEvalInterval / EnvDebugTick)

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Vector3 s_sunDirection = {-0.6f, -0.7f, 0.6f};
static Color s_sunColor = {255, 245, 230, 255};
static float s_sunIntensity = 1.0f;
static Color s_ambientColor = {50, 50, 70, 255};
static Color s_shadowColor = {8, 8, 12, 180};
static Color s_cachedSkyAmbient = {62, 62, 94, 255};
static Color s_cachedGroundAmbient = {27, 22, 28, 255};

static EnvFogConfig s_fogConfig = {
    .color = {30, 30, 40, 255},
    .start = 200.0f,
    .end = 1200.0f,
    .density = 0.001f,
    .enabled = false};

static AtmosphereProfile s_atmosphereProfile = {
    .color = {30, 30, 40, 255},
    .start = 200.0f,
    .end = 1200.0f,
    .enabled = false,
    .optics = {
        .rayleighLMS = {0.0076224f, 0.012935f, 0.024845f},
        .mieScattering = 0.002f,
        .mieAnisotropy = 0.80f,
        .multipleScatteringAmp = 2.16f},
    .density = {.baseDensity = 0.001f, .heightFalloff = 0.05f, .baseAltitude = 0.0f, .enableSigmoidLayer = false, .layerAltitude = 5.0f, .layerThickness = 3.0f, .layerDensity = 0.005f}};

static LocalFogVolume s_fogVolumes[MAX_LOCAL_FOG_VOLUMES] = {0};
static int s_activeFogIndices[MAX_LOCAL_FOG_VOLUMES] = {0};
static int s_activeFogCount = 0;
static int s_nextFogVolumeId = 1;

static unsigned int s_lightingVersion = 1;

// --- Time-of-Day dynamic lighting cycle state ---
static EnvLightingPreset s_todPresets[MAX_TIME_OF_DAY_PRESETS];
static float s_todTimePoints[MAX_TIME_OF_DAY_PRESETS];
static int s_todCount = 0;
static float s_todCurrentTime = 0.0f;
static float s_todSpeed = 0.0f;
// Time-of-Day *time* advances every frame, but the (comparatively expensive)
// preset blend + version bump only runs at this rate. EnvApplyBlendedPreset bumps
// s_lightingVersion/s_cloudVersion unconditionally, so evaluating at frame rate
// makes every consumer keyed on the version (uniform uploads, fog/atmosphere
// rebuilds, cloud shader rebinding) redo its work every single frame. 20 Hz moves
// the sun < 0.03 deg per step at a 10-minute day: invisible. WUXING_TOD_HZ=0
// restores per-frame evaluation.
#define ENV_TOD_EVAL_HZ_DEFAULT 20.0f
static float s_todEvalInterval = -1.0f; // seconds; <0 = env not read yet, 0 = every frame
static float s_todEvalAccum = 1.0f;     // starts above any interval so the first Update evaluates
static bool s_todEvalPending = false;   // time advanced since the last evaluation

static EnvCloudShadowConfig s_cloudShadow = {
    .enabled = false,
    .strength = 0.22f,
    .worldSize = 96.0f,
    .planeHeight = 80.0f,
    .coverage = 0.48f,
    .softness = 0.16f,
    .windSpeedScale = 0.55f};
static Vector2 s_cloudDriftUV = {0};
static unsigned int s_cloudVersion = 1;
static Texture2D s_cloudNoise = {0};
static EnvCloudShadowFrame s_cachedCloudFrame = {0};
static bool s_cloudFrameDirty = true;

// Was 16. With more than 16 distinct cloud-receiving shaders the round-robin
// replacement below evicts entries every frame, and every eviction costs four
// GetShaderLocation() string lookups plus a full uniform re-upload (boundVersion
// is reset to 0). 64 entries is ~3 KB of static memory.
#define ENV_CLOUD_SHADER_CACHE_CAPACITY 64
#define ENV_CLOUD_TEXTURE_SLOT 3
typedef struct
{
    unsigned int shaderId;
    int uvLocation, shapeLocation, projectionLocation, noiseLocation;
    unsigned int boundVersion;
    Vector2 boundDriftUV;
    unsigned int boundTextureId;
} EnvCloudShaderLocations;
static EnvCloudShaderLocations s_cloudShaderLocations[ENV_CLOUD_SHADER_CACHE_CAPACITY];
static unsigned int s_cloudShaderReplacement = 0;
static EnvCloudShaderLocations *s_lastCloudLocations = NULL; // O(1) hit when the same shader binds repeatedly

static bool s_shadowBatchOpen = false;

static inline unsigned char EnvClampByteF(float v)
{
    if (v < 0.0f)
        return 0;
    if (v > 255.0f)
        return 255;
    return (unsigned char)v;
}

static inline void EnvRefreshHemisphericAmbient(void)
{
    s_cachedSkyAmbient = (Color){
        EnvClampByteF(s_ambientColor.r * 1.25f),
        EnvClampByteF(s_ambientColor.g * 1.25f),
        EnvClampByteF(s_ambientColor.b * 1.35f),
        255};
    s_cachedGroundAmbient = (Color){
        EnvClampByteF(s_ambientColor.r * 0.55f),
        EnvClampByteF(s_ambientColor.g * 0.45f),
        EnvClampByteF(s_ambientColor.b * 0.40f),
        255};
}

static void FogVolume_RebuildActiveList(void)
{
    int count = 0;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++)
    {
        if (s_fogVolumes[i].active)
        {
            s_activeFogIndices[count++] = i;
        }
    }
    s_activeFogCount = count;
}

static float EnvCloudClamp(float value, float minimum, float maximum, float fallback)
{
    if (!isfinite(value))
        return fallback;
    return fminf(fmaxf(value, minimum), maximum);
}

static inline bool EnvColorEq(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static inline bool EnvVec3Eq(Vector3 a, Vector3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static inline Vector3 EnvNormalizeSunOrDefault(Vector3 dir, Vector3 fallback)
{
    if (!isfinite(dir.x) || !isfinite(dir.y) || !isfinite(dir.z))
        return fallback;
    float lenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (lenSq <= 1e-8f)
        return fallback;
    if (fabsf(lenSq - 1.0f) <= 1e-5f)
        return dir;
    return Vector3Scale(dir, 1.0f / sqrtf(lenSq));
}

static inline void EnvSunDirectionChanged(void)
{
    s_lightingVersion++;
    s_cloudVersion++;
    s_cloudFrameDirty = true;
}

static void EnvUpdateCloudShadow(float dt)
{
    if (!s_cloudShadow.enabled || !isfinite(dt) || dt <= 0.0f)
        return;
    WindMacroConfig wind = Wind_GetMacro();
    double scale = (double)s_cloudShadow.windSpeedScale / s_cloudShadow.worldSize;
    bool moved = false;
    if (isfinite(wind.baseDirection.x) && wind.baseDirection.x != 0.0f)
    {
        double phase = (double)s_cloudDriftUV.x + wind.baseDirection.x * scale * dt;
        s_cloudDriftUV.x = (float)(phase - floor(phase));
        moved = true;
    }
    if (isfinite(wind.baseDirection.z) && wind.baseDirection.z != 0.0f)
    {
        double phase = (double)s_cloudDriftUV.y + wind.baseDirection.z * scale * dt;
        s_cloudDriftUV.y = (float)(phase - floor(phase));
        moved = true;
    }
    if (moved)
    {
        s_cachedCloudFrame.uvTransform.y = s_cloudDriftUV.x;
        s_cachedCloudFrame.uvTransform.z = s_cloudDriftUV.y;
    }
}

void Environment_Init(void)
{
    s_sunDirection = EnvNormalizeSunOrDefault(s_sunDirection, (Vector3){-0.5144957f, -0.6002450f, 0.612f});
    s_sunIntensity = 1.0f;
    EnvRefreshHemisphericAmbient();
    s_cloudShadow.enabled = false;
    s_cloudDriftUV = (Vector2){0};
    s_cloudVersion++;
    s_cloudNoise = (Texture2D){0};
    s_cloudFrameDirty = true;
    for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++)
    {
        s_cloudShaderLocations[i] = (EnvCloudShaderLocations){0};
    }
    s_cloudShaderReplacement = 0;
    s_lastCloudLocations = NULL;
    s_shadowBatchOpen = false;
    FogVolume_RebuildActiveList();
}

static inline unsigned char EnvLerpByte(unsigned char a, unsigned char b, float t)
{
    float v = (float)a + ((float)b - (float)a) * t + 0.5f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 255.0f)
        v = 255.0f;
    return (unsigned char)v;
}

static inline Color EnvLerpColor(Color a, Color b, float t)
{
    Color out;
    out.r = EnvLerpByte(a.r, b.r, t);
    out.g = EnvLerpByte(a.g, b.g, t);
    out.b = EnvLerpByte(a.b, b.b, t);
    out.a = EnvLerpByte(a.a, b.a, t);
    return out;
}

static Vector3 EnvSlerpDirection(Vector3 a, Vector3 b, float t)
{
    float d = Vector3DotProduct(a, b);
    if (d > -0.9995f && d < 0.9995f)
    {
        float theta = acosf(d);
        float invSin = 1.0f / sinf(theta);
        float wa = sinf((1.0f - t) * theta) * invSin;
        float wb = sinf(t * theta) * invSin;
        return Vector3Normalize((Vector3){
            a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb});
    }
    Vector3 l = Vector3Lerp(a, b, t);
    if (Vector3LengthSqr(l) > 1e-6f)
        return Vector3Normalize(l);
    return t < 0.5f ? a : b;
}

static void EnvApplyBlendedPreset(const EnvLightingPreset *a, const EnvLightingPreset *b, float t)
{
    s_ambientColor = EnvLerpColor(a->ambientColor, b->ambientColor, t);
    EnvRefreshHemisphericAmbient();
    s_sunColor = EnvLerpColor(a->sunColor, b->sunColor, t);
    s_shadowColor = EnvLerpColor(a->shadowColor, b->shadowColor, t);

    s_sunDirection = EnvSlerpDirection(a->sunDirection, b->sunDirection, t);

    s_fogConfig.color = EnvLerpColor(a->fog.color, b->fog.color, t);
    s_fogConfig.start = a->fog.start + (b->fog.start - a->fog.start) * t;
    s_fogConfig.end = a->fog.end + (b->fog.end - a->fog.end) * t;
    s_fogConfig.density = a->fog.density + (b->fog.density - a->fog.density) * t;
    s_fogConfig.enabled = a->fog.enabled;

    s_atmosphereProfile.color = s_fogConfig.color;
    s_atmosphereProfile.start = s_fogConfig.start;
    s_atmosphereProfile.end = s_fogConfig.end;
    s_atmosphereProfile.enabled = s_fogConfig.enabled;
    s_atmosphereProfile.density.baseDensity = s_fogConfig.density;

    s_atmosphereProfile.optics.rayleighLMS = Vector3Lerp(a->atmosphere.optics.rayleighLMS, b->atmosphere.optics.rayleighLMS, t);
    s_atmosphereProfile.optics.mieScattering = a->atmosphere.optics.mieScattering + (b->atmosphere.optics.mieScattering - a->atmosphere.optics.mieScattering) * t;
    s_atmosphereProfile.optics.mieAnisotropy = a->atmosphere.optics.mieAnisotropy + (b->atmosphere.optics.mieAnisotropy - a->atmosphere.optics.mieAnisotropy) * t;
    s_atmosphereProfile.optics.multipleScatteringAmp = a->atmosphere.optics.multipleScatteringAmp + (b->atmosphere.optics.multipleScatteringAmp - a->atmosphere.optics.multipleScatteringAmp) * t;

    s_atmosphereProfile.density.heightFalloff = a->atmosphere.density.heightFalloff + (b->atmosphere.density.heightFalloff - a->atmosphere.density.heightFalloff) * t;
    s_atmosphereProfile.density.baseAltitude = a->atmosphere.density.baseAltitude + (b->atmosphere.density.baseAltitude - a->atmosphere.density.baseAltitude) * t;
    s_atmosphereProfile.density.enableSigmoidLayer = a->atmosphere.density.enableSigmoidLayer;
    s_atmosphereProfile.density.layerAltitude = a->atmosphere.density.layerAltitude + (b->atmosphere.density.layerAltitude - a->atmosphere.density.layerAltitude) * t;
    s_atmosphereProfile.density.layerThickness = a->atmosphere.density.layerThickness + (b->atmosphere.density.layerThickness - a->atmosphere.density.layerThickness) * t;
    s_atmosphereProfile.density.layerDensity = a->atmosphere.density.layerDensity + (b->atmosphere.density.layerDensity - a->atmosphere.density.layerDensity) * t;

    EnvSunDirectionChanged();
}

static void EnvEvaluateTimeOfDay(void)
{
    if (s_todCount <= 0)
        return;
    if (s_todCount == 1)
    {
        EnvApplyBlendedPreset(&s_todPresets[0], &s_todPresets[0], 0.0f);
        return;
    }

    float t = s_todCurrentTime;
    if (t < s_todTimePoints[0] || t >= s_todTimePoints[s_todCount - 1])
    {
        float segStart = s_todTimePoints[s_todCount - 1];
        float segEnd = s_todTimePoints[0] + 1.0f;
        float tt = (t < s_todTimePoints[0]) ? (t + 1.0f) : t;
        float span = segEnd - segStart;
        float f = (span > 0.00001f) ? (tt - segStart) / span : 0.0f;
        EnvApplyBlendedPreset(&s_todPresets[s_todCount - 1], &s_todPresets[0], f);
        return;
    }

    for (int i = 0; i < s_todCount - 1; i++)
    {
        if (t >= s_todTimePoints[i] && t < s_todTimePoints[i + 1])
        {
            float span = s_todTimePoints[i + 1] - s_todTimePoints[i];
            float f = (span > 0.00001f) ? (t - s_todTimePoints[i]) / span : 0.0f;
            EnvApplyBlendedPreset(&s_todPresets[i], &s_todPresets[i + 1], f);
            return;
        }
    }
}

static float EnvTodEvalInterval(void)
{
    if (s_todEvalInterval < 0.0f)
    {
        float hz = ENV_TOD_EVAL_HZ_DEFAULT;
        const char *e = getenv("WUXING_TOD_HZ");
        if (e != NULL)
        {
            float v = strtof(e, NULL);
            hz = v > 0.0f ? v : 0.0f;
        }
        s_todEvalInterval = hz > 0.0f ? 1.0f / hz : 0.0f;
    }
    return s_todEvalInterval;
}

// Opt-in diagnostics (WUXING_ENV_DEBUG=1): every 5 s log the number of active
// local fog volumes and how many lighting-version bumps happened. A fog count
// that keeps climbing while the player stands still is a leak; ~60 bumps/s
// means something is re-evaluating lighting every frame.
static void EnvDebugTick(float dt)
{
    static int enabled = -1;
    static float accum = 0.0f;
    static unsigned int lastVersion = 0;
    if (enabled < 0)
    {
        enabled = getenv("WUXING_ENV_DEBUG") != NULL ? 1 : 0;
        lastVersion = s_lightingVersion;
    }
    if (!enabled)
        return;
    accum += dt;
    if (accum < 5.0f)
        return;
    TraceLog(LOG_INFO, "ENV_DEBUG: fogVolumes=%d/%d lightingVersion=+%u in %.1fs todSpeed=%.6f",
             s_activeFogCount, MAX_LOCAL_FOG_VOLUMES, s_lightingVersion - lastVersion, accum, s_todSpeed);
    accum = 0.0f;
    lastVersion = s_lightingVersion;
}

void Environment_Update(float dt)
{
    if (!isfinite(dt) || dt <= 0.0f)
        return;

    EnvDebugTick(dt);
    EnvUpdateCloudShadow(dt);

    // 1. Update transient Local Fog Volumes (chỉ duyệt các fog volume đang active)
    if (s_activeFogCount > 0)
    {
        bool anyExpired = false;
        for (int idx = 0; idx < s_activeFogCount; idx++)
        {
            LocalFogVolume *fv = &s_fogVolumes[s_activeFogIndices[idx]];
            if (fv->maxLifetime > 0.0f)
            {
                fv->lifetime -= dt;
                if (fv->lifetime <= 0.0f)
                {
                    fv->active = false;
                    fv->id = 0;
                    anyExpired = true;
                    continue;
                }
            }
            if (fv->driftVelocity.x != 0.0f || fv->driftVelocity.y != 0.0f || fv->driftVelocity.z != 0.0f)
            {
                fv->position.x += fv->driftVelocity.x * dt;
                fv->position.y += fv->driftVelocity.y * dt;
                fv->position.z += fv->driftVelocity.z * dt;
            }
        }
        if (anyExpired)
        {
            FogVolume_RebuildActiveList();
        }
    }

    // 2. Update Time-of-Day cycle
    if (s_todSpeed == 0.0f || s_todCount <= 0)
        return;

    s_todCurrentTime += s_todSpeed * dt;
    s_todCurrentTime = fmodf(s_todCurrentTime, 1.0f);
    if (s_todCurrentTime < 0.0f)
        s_todCurrentTime += 1.0f;

    float interval = EnvTodEvalInterval();
    s_todEvalAccum += dt;
    if (interval <= 0.0f || s_todEvalAccum >= interval)
    {
        s_todEvalAccum = 0.0f;
        s_todEvalPending = false;
        EnvEvaluateTimeOfDay();
    }
    else
    {
        s_todEvalPending = true;
    }
}

// ---------------------------------------------------------------------------
// Shadow decals
// ---------------------------------------------------------------------------
#define ENV_SHADOW_SEGMENTS 12
#define ENV_SHADOW_MAX_SKEW 8.0f
#define ENV_SHADOW_FADE_START 0.01f
#define ENV_SHADOW_FADE_RANGE 0.14f

static float s_shadowArcCos[ENV_SHADOW_SEGMENTS + 1];
static float s_shadowArcSin[ENV_SHADOW_SEGMENTS + 1];
static bool s_shadowArcReady = false;

static void EnvBuildShadowArc(void)
{
    for (int i = 0; i <= ENV_SHADOW_SEGMENTS; i++)
    {
        float a = ((float)i / (float)ENV_SHADOW_SEGMENTS) * PI;
        s_shadowArcCos[i] = cosf(a);
        s_shadowArcSin[i] = sinf(a);
    }
    s_shadowArcReady = true;
}

typedef struct
{
    float yGround;
    float alpha;
    float hAbove;
    float scale;
    float offX, offZ;
} EnvShadowPrep;

static inline Color EnvShadowTint(float alphaScale)
{
    float a = (float)s_shadowColor.a * alphaScale;
    if (a < 0.0f)
        a = 0.0f;
    if (a > 255.0f)
        a = 255.0f;
    return (Color){s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, (unsigned char)a};
}

static inline void EnvShadowVertex(Color c, float x, float y, float z)
{
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlVertex3f(x, y, z);
}

static bool EnvPrepareShadow(Vector3 pos, EnvShadowShapeType shape, EnvShadowPrep *out)
{
    if (shape != ENV_SHAPE_SPHERE && shape != ENV_SHAPE_CYLINDER && shape != ENV_SHAPE_BOX)
        return false;

    float elevation = -s_sunDirection.y;
    float fade = fminf(fmaxf((elevation - ENV_SHADOW_FADE_START) * (1.0f / ENV_SHADOW_FADE_RANGE), 0.0f), 1.0f);
    if (fade <= 0.001f)
        return false;
    fade = fade * fade * (3.0f - 2.0f * fade);
    float skew = 1.0f / fmaxf(elevation, 1.0f / ENV_SHADOW_MAX_SKEW);

    float waterSurfaceY = 0.0f, waterDepth = 0.0f;
    if (MapManager_GetWaterInfoAt(pos.x, pos.z, &waterSurfaceY, &waterDepth))
    {
        if (pos.y < waterSurfaceY + 0.05f)
            return false;
    }

    out->yGround = MapManager_GetGroundHeightAt(pos.x, pos.z) + 0.02f;
    out->offX = s_sunDirection.x * skew;
    out->offZ = s_sunDirection.z * skew;
    out->alpha = fade;
    out->hAbove = 0.0f;
    out->scale = 1.0f;

    if (shape != ENV_SHAPE_BOX)
    {
        float hAbove = fmaxf(pos.y - out->yGround, 0.0f);
        float heightFactor = hAbove * (1.0f / 200.0f);
        out->hAbove = hAbove;
        out->scale = fmaxf(1.0f - heightFactor * 0.4f, 0.1f);
        out->alpha *= fmaxf(1.0f - heightFactor * 1.2f, 0.0f);
        if (out->alpha <= 0.001f)
            return false;
    }
    return true;
}

static void EnvEmitShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height,
                          const EnvShadowPrep *prep)
{
    float yGround = prep->yGround;
    Color cBase = EnvShadowTint(prep->alpha);
    Color cTip = EnvShadowTint(prep->alpha * 0.3f);
    Color cEdge = {s_shadowColor.r, s_shadowColor.g, s_shadowColor.b, 0};

    if (shape == ENV_SHAPE_SPHERE || shape == ENV_SHAPE_CYLINDER)
    {
        if (!s_shadowArcReady)
            EnvBuildShadowArc();

        float hAbove = prep->hAbove;
        float shadowScale = prep->scale;

        float reach = height * shadowScale;
        float bx = pos.x + prep->offX * hAbove;
        float bz = pos.z + prep->offZ * hAbove;
        float tx = pos.x + prep->offX * (hAbove + reach);
        float tz = pos.z + prep->offZ * (hAbove + reach);

        float dx = tx - bx, dz = tz - bz;
        float lenSq = dx * dx + dz * dz;
        float fx = 1.0f, fz = 0.0f;
        if (lenSq > 1e-6f)
        {
            float invLen = 1.0f / sqrtf(lenSq);
            fx = dx * invLen;
            fz = dz * invLen;
        }
        float px = -fz, pz = fx;

        float baseWidth = width * shadowScale;
        float tipWidth = width * 1.2f * shadowScale;

        float baseArcX[ENV_SHADOW_SEGMENTS + 1], baseArcZ[ENV_SHADOW_SEGMENTS + 1];
        float topArcX[ENV_SHADOW_SEGMENTS + 1], topArcZ[ENV_SHADOW_SEGMENTS + 1];
        for (int i = 0; i <= ENV_SHADOW_SEGMENTS; i++)
        {
            float c = s_shadowArcCos[i], s = s_shadowArcSin[i];
            baseArcX[i] = bx + (-fx * s + px * c) * baseWidth;
            baseArcZ[i] = bz + (-fz * s + pz * c) * baseWidth;
            topArcX[i] = tx + (fx * s - px * c) * tipWidth;
            topArcZ[i] = tz + (fz * s - pz * c) * tipWidth;
        }

        rlBegin(RL_TRIANGLES);

        for (int i = 0; i < ENV_SHADOW_SEGMENTS; i++)
        {
            EnvShadowVertex(cBase, bx, yGround, bz);
            EnvShadowVertex(cEdge, baseArcX[i + 1], yGround, baseArcZ[i + 1]);
            EnvShadowVertex(cEdge, baseArcX[i], yGround, baseArcZ[i]);
        }

        float brx = bx - px * baseWidth, brz = bz - pz * baseWidth;
        float trx = tx - px * tipWidth, trz = tz - pz * tipWidth;
        EnvShadowVertex(cBase, bx, yGround, bz);
        EnvShadowVertex(cTip, tx, yGround, tz);
        EnvShadowVertex(cEdge, brx, yGround, brz);

        EnvShadowVertex(cTip, tx, yGround, tz);
        EnvShadowVertex(cEdge, trx, yGround, trz);
        EnvShadowVertex(cEdge, brx, yGround, brz);

        float blx = bx + px * baseWidth, blz = bz + pz * baseWidth;
        float tlx = tx + px * tipWidth, tlz = tz + pz * tipWidth;
        EnvShadowVertex(cBase, bx, yGround, bz);
        EnvShadowVertex(cEdge, blx, yGround, blz);
        EnvShadowVertex(cTip, tx, yGround, tz);

        EnvShadowVertex(cEdge, blx, yGround, blz);
        EnvShadowVertex(cEdge, tlx, yGround, tlz);
        EnvShadowVertex(cTip, tx, yGround, tz);

        for (int i = 0; i < ENV_SHADOW_SEGMENTS; i++)
        {
            EnvShadowVertex(cTip, tx, yGround, tz);
            EnvShadowVertex(cEdge, topArcX[i + 1], yGround, topArcZ[i + 1]);
            EnvShadowVertex(cEdge, topArcX[i], yGround, topArcZ[i]);
        }

        rlEnd();
    }
    else if (shape == ENV_SHAPE_BOX)
    {
        float ox = prep->offX * height, oz = prep->offZ * height;
        float x0 = pos.x - width, x1 = pos.x + width;
        float z0 = pos.z - width, z1 = pos.z + width;
        const float eps = 1e-4f;

        rlBegin(RL_TRIANGLES);

        EnvShadowVertex(cBase, x0, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z1);
        EnvShadowVertex(cBase, x0, yGround, z0);
        EnvShadowVertex(cBase, x1, yGround, z1);
        EnvShadowVertex(cBase, x0, yGround, z1);

        if (ox > eps || ox < -eps)
        {
            float ex = ox > 0.0f ? x1 : x0;
            EnvShadowVertex(cBase, ex, yGround, z0);
            EnvShadowVertex(cBase, ex, yGround, z1);
            EnvShadowVertex(cTip, ex + ox, yGround, z1 + oz);
            EnvShadowVertex(cBase, ex, yGround, z0);
            EnvShadowVertex(cTip, ex + ox, yGround, z1 + oz);
            EnvShadowVertex(cTip, ex + ox, yGround, z0 + oz);
        }
        if (oz > eps || oz < -eps)
        {
            float ez = oz > 0.0f ? z1 : z0;
            EnvShadowVertex(cBase, x0, yGround, ez);
            EnvShadowVertex(cBase, x1, yGround, ez);
            EnvShadowVertex(cTip, x1 + ox, yGround, ez + oz);
            EnvShadowVertex(cBase, x0, yGround, ez);
            EnvShadowVertex(cTip, x1 + ox, yGround, ez + oz);
            EnvShadowVertex(cTip, x0 + ox, yGround, ez + oz);
        }

        rlEnd();
    }
}

static void EnvShadowStateBegin(void)
{
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
}

static void EnvShadowStateEnd(void)
{
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
}

void Environment_BeginShadowBatch(void)
{
    if (s_shadowBatchOpen)
        return;
    s_shadowBatchOpen = true;
    EnvShadowStateBegin();
}

void Environment_EndShadowBatch(void)
{
    if (!s_shadowBatchOpen)
        return;
    s_shadowBatchOpen = false;
    EnvShadowStateEnd();
}

void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height)
{
    EnvShadowPrep prep;
    if (!EnvPrepareShadow(pos, shape, &prep))
        return;

    if (s_shadowBatchOpen)
    {
        EnvEmitShadow(pos, shape, width, height, &prep);
        return;
    }
    EnvShadowStateBegin();
    EnvEmitShadow(pos, shape, width, height, &prep);
    EnvShadowStateEnd();
}

Vector3 Environment_GetSunDirection(void) { return s_sunDirection; }
void Environment_SetSunDirection(Vector3 dir)
{
    Vector3 n = EnvNormalizeSunOrDefault(dir, s_sunDirection);
    if (EnvVec3Eq(n, s_sunDirection))
        return;
    s_sunDirection = n;
    EnvSunDirectionChanged();
}

Color Environment_GetSunColor(void) { return s_sunColor; }
void Environment_SetSunColor(Color col)
{
    if (EnvColorEq(col, s_sunColor))
        return;
    s_sunColor = col;
    s_lightingVersion++;
}

float Environment_GetSunIntensity(void) { return s_sunIntensity; }
void Environment_SetSunIntensity(float intensity)
{
    float resolved = isfinite(intensity) ? fminf(fmaxf(intensity, 0.0f), 64.0f) : 1.0f;
    if (resolved != s_sunIntensity)
    {
        s_sunIntensity = resolved;
        s_lightingVersion++;
    }
}

Color Environment_GetAmbientColor(void) { return s_ambientColor; }
void Environment_SetAmbientColor(Color col)
{
    if (EnvColorEq(col, s_ambientColor))
        return;
    s_ambientColor = col;
    EnvRefreshHemisphericAmbient();
    s_lightingVersion++;
}

Color Environment_GetSkyAmbient(void) { return s_cachedSkyAmbient; }
Color Environment_GetGroundAmbient(void) { return s_cachedGroundAmbient; }

Color Environment_GetShadowColor(void) { return s_shadowColor; }
void Environment_SetShadowColor(Color col)
{
    if (EnvColorEq(col, s_shadowColor))
        return;
    s_shadowColor = col;
    s_lightingVersion++;
}

EnvFogConfig Environment_GetFogConfig(void) { return s_fogConfig; }
void Environment_SetFogConfig(EnvFogConfig config)
{
    s_fogConfig = config;
    s_atmosphereProfile.color = config.color;
    s_atmosphereProfile.start = config.start;
    s_atmosphereProfile.end = config.end;
    s_atmosphereProfile.enabled = config.enabled;
    s_atmosphereProfile.density.baseDensity = config.density;
    s_lightingVersion++;
}

AtmosphereProfile Environment_GetAtmosphereProfile(void) { return s_atmosphereProfile; }
void Environment_SetAtmosphereProfile(const AtmosphereProfile *profile)
{
    if (!profile)
        return;
    s_atmosphereProfile = *profile;
    s_fogConfig.color = profile->color;
    s_fogConfig.start = profile->start;
    s_fogConfig.end = profile->end;
    s_fogConfig.density = profile->density.baseDensity;
    s_fogConfig.enabled = profile->enabled;
    s_lightingVersion++;
}

// Local Fog Volume Manager
int FogVolume_Create(const LocalFogVolume *volume)
{
    if (!volume)
        return 0;
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++)
    {
        if (!s_fogVolumes[i].active)
        {
            s_fogVolumes[i] = *volume;
            s_fogVolumes[i].id = s_nextFogVolumeId++;
            if (s_nextFogVolumeId <= 0)
                s_nextFogVolumeId = 1;
            s_fogVolumes[i].active = true;
            FogVolume_RebuildActiveList();
            return s_fogVolumes[i].id;
        }
    }
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            TraceLog(LOG_WARNING, "ENV: local fog volume pool full (%d). Volumes with maxLifetime<=0 never expire "
                                  "- check for a spawner that is not paired with FogVolume_Destroy.",
                     MAX_LOCAL_FOG_VOLUMES);
        }
    }
    return 0;
}

void FogVolume_Update(int id, const LocalFogVolume *volume)
{
    if (id <= 0 || !volume)
        return;
    for (int idx = 0; idx < s_activeFogCount; idx++)
    {
        int i = s_activeFogIndices[idx];
        if (s_fogVolumes[i].id == id)
        {
            int savedId = s_fogVolumes[i].id;
            s_fogVolumes[i] = *volume;
            s_fogVolumes[i].id = savedId;
            s_fogVolumes[i].active = true;
            return;
        }
    }
}

void FogVolume_Destroy(int id)
{
    if (id <= 0)
        return;
    for (int idx = 0; idx < s_activeFogCount; idx++)
    {
        int i = s_activeFogIndices[idx];
        if (s_fogVolumes[i].id == id)
        {
            s_fogVolumes[i].active = false;
            s_fogVolumes[i].id = 0;
            FogVolume_RebuildActiveList();
            return;
        }
    }
}

void FogVolume_ClearAll(void)
{
    for (int i = 0; i < MAX_LOCAL_FOG_VOLUMES; i++)
    {
        s_fogVolumes[i].active = false;
        s_fogVolumes[i].id = 0;
    }
    s_activeFogCount = 0;
}

int FogVolume_SpawnTransient(Vector3 pos, float radius, Color color, float density, float duration)
{
    if (!(duration > 0.0f))
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            TraceLog(LOG_WARNING, "ENV: FogVolume_SpawnTransient(duration=%.3f) creates a volume that NEVER expires "
                                  "(maxLifetime<=0). Repeated calls fill the pool and grow fog raymarch cost.",
                     (double)duration);
        }
    }
    LocalFogVolume v = {0};
    v.shape = FOG_SHAPE_SPHERE;
    v.position = pos;
    v.extents = (Vector3){radius, radius, radius};
    v.color = color;
    v.density = density;
    v.edgeSoftness = 0.8f;
    v.lifetime = duration;
    v.maxLifetime = duration;
    v.active = true;
    return FogVolume_Create(&v);
}

int FogVolume_GetActiveCount(void)
{
    return s_activeFogCount;
}

const LocalFogVolume *FogVolume_GetByIndex(int index)
{
    if (index < 0 || index >= s_activeFogCount)
        return NULL;
    return &s_fogVolumes[s_activeFogIndices[index]];
}

const LocalFogVolume *FogVolume_GetById(int id)
{
    if (id <= 0)
        return NULL;
    for (int idx = 0; idx < s_activeFogCount; idx++)
    {
        int i = s_activeFogIndices[idx];
        if (s_fogVolumes[i].id == id)
        {
            return &s_fogVolumes[i];
        }
    }
    return NULL;
}

EnvFrameLighting Environment_GetFrameLighting(void)
{
    EnvFrameLighting frame;
    frame.sunDirection = s_sunDirection;
    frame.sunColor = s_sunColor;
    frame.skyAmbient = s_cachedSkyAmbient;
    frame.groundBounce = s_cachedGroundAmbient;
    frame.shadowColor = s_shadowColor;
    frame.fog = s_fogConfig;
    frame.atmosphere = s_atmosphereProfile;
    frame.version = s_lightingVersion;
    frame.sunIntensity = s_sunIntensity;
    return frame;
}

EnvCloudShadowConfig Environment_GetCloudShadowConfig(void)
{
    return s_cloudShadow;
}

void Environment_SetCloudShadowConfig(const EnvCloudShadowConfig *config)
{
    if (!config)
    {
        s_cloudShadow.enabled = false;
        s_cloudDriftUV = (Vector2){0};
        s_cloudVersion++;
        s_cloudFrameDirty = true;
        return;
    }
    s_cloudShadow.enabled = config->enabled;
    s_cloudShadow.strength = EnvCloudClamp(config->strength, 0.0f, 1.0f, 0.22f);
    s_cloudShadow.worldSize = EnvCloudClamp(config->worldSize, 8.0f, 4096.0f, 96.0f);
    s_cloudShadow.planeHeight = EnvCloudClamp(config->planeHeight, 1.0f, 10000.0f, 80.0f);
    s_cloudShadow.coverage = EnvCloudClamp(config->coverage, 0.0f, 1.0f, 0.48f);
    s_cloudShadow.softness = EnvCloudClamp(config->softness, 0.01f, 0.5f, 0.16f);
    s_cloudShadow.windSpeedScale = EnvCloudClamp(config->windSpeedScale, 0.0f, 4.0f, 0.55f);
    if (s_cloudShadow.enabled && !s_cloudNoise.id)
    {
        s_cloudNoise = ResourceManager_LoadTexture("environment/textures/cloud_noise.png");
        if (s_cloudNoise.id)
        {
            SetTextureFilter(s_cloudNoise, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_cloudNoise, TEXTURE_WRAP_REPEAT);
        }
    }
    s_cloudVersion++;
    s_cloudFrameDirty = true;
}

EnvCloudShadowFrame Environment_GetCloudShadowFrame(void)
{
    if (s_cloudFrameDirty)
    {
        float sunElevation = fmaxf(-s_sunDirection.y, 0.15f);
        float daylight = EnvCloudClamp((-s_sunDirection.y - 0.02f) * (1.0f / 0.13f), 0.0f, 1.0f, 0.0f);
        s_cachedCloudFrame.noiseTexture = s_cloudNoise;
        s_cachedCloudFrame.uvTransform = (Vector4){
            1.0f / s_cloudShadow.worldSize, s_cloudDriftUV.x, s_cloudDriftUV.y,
            (s_cloudShadow.enabled && s_cloudNoise.id) ? (s_cloudShadow.strength * daylight) : 0.0f};
        s_cachedCloudFrame.shape = (Vector4){
            s_cloudShadow.coverage, s_cloudShadow.softness, s_cloudShadow.planeHeight, 0.0f};
        s_cachedCloudFrame.projection = (Vector2){
            s_sunDirection.x / sunElevation, s_sunDirection.z / sunElevation};
        s_cachedCloudFrame.version = s_cloudVersion;
        s_cloudFrameDirty = false;
    }
    return s_cachedCloudFrame;
}

void Environment_BindCloudShadowShader(Shader shader)
{
    if (!shader.id)
        return;
    EnvCloudShaderLocations *locations = NULL;
    if (s_lastCloudLocations != NULL && s_lastCloudLocations->shaderId == shader.id)
    {
        locations = s_lastCloudLocations;
    }
    else
    {
        for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++)
        {
            if (s_cloudShaderLocations[i].shaderId == shader.id)
            {
                locations = &s_cloudShaderLocations[i];
                break;
            }
        }
    }
    if (!locations)
    {
        for (int i = 0; i < ENV_CLOUD_SHADER_CACHE_CAPACITY; i++)
        {
            if (!s_cloudShaderLocations[i].shaderId)
            {
                locations = &s_cloudShaderLocations[i];
                break;
            }
        }
        if (!locations)
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                TraceLog(LOG_WARNING, "ENV: cloud-shadow shader cache full (%d entries); "
                                      "evicting -> uniform locations re-queried every frame. "
                                      "Raise ENV_CLOUD_SHADER_CACHE_CAPACITY.",
                         ENV_CLOUD_SHADER_CACHE_CAPACITY);
            }
            locations = &s_cloudShaderLocations[s_cloudShaderReplacement++ % ENV_CLOUD_SHADER_CACHE_CAPACITY];
        }
        *locations = (EnvCloudShaderLocations){
            .shaderId = shader.id,
            .uvLocation = GetShaderLocation(shader, "u_cloudUV"),
            .shapeLocation = GetShaderLocation(shader, "u_cloudShape"),
            .projectionLocation = GetShaderLocation(shader, "u_cloudProjection"),
            .noiseLocation = GetShaderLocation(shader, "u_cloudNoise"),
            .boundVersion = 0,
            .boundDriftUV = {-1.0f, -1.0f},
            .boundTextureId = 0};
    }

    s_lastCloudLocations = locations;

    EnvCloudShadowFrame frame = Environment_GetCloudShadowFrame();
    bool versionChanged = (locations->boundVersion != frame.version);
    bool driftChanged = (locations->boundDriftUV.x != s_cloudDriftUV.x ||
                         locations->boundDriftUV.y != s_cloudDriftUV.y);

    // Fast-path: nếu version không đổi và bóng mây đang tắt (hoặc UV không trôi),
    // bỏ qua hoàn toàn các lời gọi SetShaderValue lặp lại mỗi frame.
    if (!versionChanged && (!s_cloudShadow.enabled || !driftChanged))
    {
        if (locations->noiseLocation >= 0 && frame.noiseTexture.id)
        {
            rlActiveTextureSlot(ENV_CLOUD_TEXTURE_SLOT);
            rlEnableTexture(frame.noiseTexture.id);
            rlActiveTextureSlot(0);
        }
        return;
    }

    if (locations->uvLocation >= 0 && (versionChanged || driftChanged))
    {
        SetShaderValue(shader, locations->uvLocation, &frame.uvTransform, SHADER_UNIFORM_VEC4);
    }
    if (versionChanged)
    {
        if (locations->shapeLocation >= 0)
        {
            SetShaderValue(shader, locations->shapeLocation, &frame.shape, SHADER_UNIFORM_VEC4);
        }
        if (locations->projectionLocation >= 0)
        {
            SetShaderValue(shader, locations->projectionLocation, &frame.projection, SHADER_UNIFORM_VEC2);
        }
    }
    if (locations->noiseLocation >= 0 && frame.noiseTexture.id)
    {
        if (locations->boundTextureId != frame.noiseTexture.id)
        {
            int slot = ENV_CLOUD_TEXTURE_SLOT;
            SetShaderValue(shader, locations->noiseLocation, &slot, SHADER_UNIFORM_INT);
            locations->boundTextureId = frame.noiseTexture.id;
        }
        rlActiveTextureSlot(ENV_CLOUD_TEXTURE_SLOT);
        rlEnableTexture(frame.noiseTexture.id);
        rlActiveTextureSlot(0);
    }

    locations->boundVersion = frame.version;
    locations->boundDriftUV = s_cloudDriftUV;
}

void Environment_ApplyProfile(const EnvLightingPreset *profile)
{
    if (!profile)
        return;
    s_ambientColor = profile->ambientColor;
    EnvRefreshHemisphericAmbient();
    s_sunColor = profile->sunColor;
    s_sunDirection = EnvNormalizeSunOrDefault(profile->sunDirection, s_sunDirection);
    s_shadowColor = profile->shadowColor;
    s_fogConfig = profile->fog;
    s_atmosphereProfile = profile->atmosphere;
    s_atmosphereProfile.color = s_fogConfig.color;
    s_atmosphereProfile.start = s_fogConfig.start;
    s_atmosphereProfile.end = s_fogConfig.end;
    s_atmosphereProfile.enabled = s_fogConfig.enabled;
    EnvSunDirectionChanged();
}

void Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count)
{
    if (!presets || !timePoints || count <= 0)
    {
        s_todCount = 0;
        return;
    }
    if (count > MAX_TIME_OF_DAY_PRESETS)
        count = MAX_TIME_OF_DAY_PRESETS;

    s_todCount = count;
    s_todEvalAccum = 1.0f; // re-evaluate on the next Update
    for (int i = 0; i < count; i++)
    {
        s_todPresets[i] = presets[i];
        s_todPresets[i].sunDirection = EnvNormalizeSunOrDefault(
            presets[i].sunDirection, (Vector3){-0.5144957f, -0.6002450f, 0.612f});
        s_todTimePoints[i] = timePoints[i];
    }
}

void Environment_SetTimeOfDaySpeed(float cyclesPerSecond)
{
    s_todSpeed = isfinite(cyclesPerSecond) ? cyclesPerSecond : 0.0f;
    // Update() stops evaluating once the speed is 0, so flush a pending step.
    if (s_todSpeed == 0.0f && s_todEvalPending)
    {
        s_todEvalPending = false;
        s_todEvalAccum = 0.0f;
        EnvEvaluateTimeOfDay();
    }
}

void Environment_SetTimeOfDay(float t)
{
    if (!isfinite(t))
        return;
    t = fmodf(t, 1.0f);
    if (t < 0.0f)
        t += 1.0f;
    s_todCurrentTime = t;
    s_todEvalAccum = 0.0f;
    s_todEvalPending = false;
    EnvEvaluateTimeOfDay();
}

float Environment_GetTimeOfDay(void) { return s_todCurrentTime; }
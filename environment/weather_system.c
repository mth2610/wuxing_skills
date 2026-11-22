#include "environment/weather_system.h"
#include "core/gfx_quality.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>

#define RAIN_STREAK_COUNT 1400
#define RAIN_COLUMN_RADIUS 22.0f
#define RAIN_COLUMN_HEIGHT 16.0f

#define SNOW_FLAKE_COUNT 1200
#define SNOW_COLUMN_RADIUS 24.0f
#define SNOW_COLUMN_HEIGHT 14.0f

static float s_rainTarget = 0.0f;
static float s_rainVisible = 0.0f;
static float s_rainRunoff = 0.0f;
static float s_rainWetness = 0.0f;

static float s_snowTarget = 0.0f;
static float s_snowVisible = 0.0f;

static inline float Weather_Approach(float cur, float target, float riseRate, float fallRate, float dt)
{
    float rate = (target > cur) ? riseRate : fallRate;
    float next = cur + (target - cur) * fminf(1.0f, dt * rate);
    return (fabsf(target - next) < 0.002f) ? target : next;
}

static inline float Weather_Hash(unsigned int n)
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return (float)(n & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

void Weather_Init(void)
{
    s_rainTarget = 0.0f;
    s_rainVisible = 0.0f;
    s_rainRunoff = 0.0f;
    s_rainWetness = 0.0f;
    s_snowTarget = 0.0f;
    s_snowVisible = 0.0f;
}

void Weather_Update(float dt)
{
    if (dt <= 0.0f || !isfinite(dt))
        return;
    if (dt > 0.1f)
        dt = 0.1f;

    s_rainVisible = Weather_Approach(s_rainVisible, s_rainTarget, 1.2f, 0.8f, dt);
    s_rainRunoff = Weather_Approach(s_rainRunoff, s_rainTarget, 0.42f, 0.28f, dt);
    float wetTarget = fminf(1.0f, s_rainTarget * 1.4f);
    s_rainWetness = Weather_Approach(s_rainWetness, wetTarget, 0.65f, 0.25f, dt);

    s_snowVisible = Weather_Approach(s_snowVisible, s_snowTarget, 0.85f, 0.65f, dt);
}

void Weather_SetRain(float intensity)
{
    s_rainTarget = fminf(fmaxf(intensity, 0.0f), 2.0f);
}

float Weather_GetRainTarget(void) { return s_rainTarget; }
float Weather_GetRain(void) { return s_rainVisible; }
float Weather_GetRunoff(void) { return s_rainRunoff; }
float Weather_GetWetness(void) { return s_rainWetness; }

void Weather_SetSnow(float intensity)
{
    s_snowTarget = fminf(fmaxf(intensity, 0.0f), 2.0f);
}

float Weather_GetSnow(void) { return s_snowVisible; }

typedef struct
{
    float dx, dz;
    float phaseOffset;
    float speedJ;
    unsigned char alphaScale;
} RainStreakDef;

static RainStreakDef s_rainTable[RAIN_STREAK_COUNT];
static bool s_rainTableBuilt = false;

static void Rain_EnsureTable(void)
{
    if (s_rainTableBuilt)
        return;
    for (int i = 0; i < RAIN_STREAK_COUNT; i++)
    {
        unsigned int s = (unsigned int)i * 2654435761u;
        float u = Weather_Hash(s + 1u), v = Weather_Hash(s + 2u);
        float phase = Weather_Hash(s + 3u);
        float su = sqrtf(u);
        float r = RAIN_COLUMN_RADIUS * su;
        float a = 6.2831853f * v;
        s_rainTable[i].dx = cosf(a) * r;
        s_rainTable[i].dz = sinf(a) * r;
        s_rainTable[i].phaseOffset = phase * RAIN_COLUMN_HEIGHT;
        s_rainTable[i].speedJ = 0.85f + 0.3f * Weather_Hash(s + 4u);
        float fade = 1.0f - su * 0.85f;
        s_rainTable[i].alphaScale = (unsigned char)(fade * 255.0f + 0.5f);
    }
    s_rainTableBuilt = true;
}

void Weather_DrawRain(Vector3 focus, float time)
{
    const float intensity = s_rainVisible;
    if (intensity < 0.02f)
        return;

    Rain_EnsureTable();

    int count = (int)(RAIN_STREAK_COUNT * fminf(1.0f, 0.25f + intensity * 0.6f));
    GfxQuality q = GfxQuality_Get();
    if (q < GFX_MED)
        count /= 2;

    const float fallSpeed = 17.0f + intensity * 4.0f;
    const float streakLen = 0.9f + intensity * 0.35f;
    const float k = streakLen / fallSpeed;
    const float windDx = 1.6f * k, windDz = 0.7f * k;
    const float topY = focus.y + RAIN_COLUMN_HEIGHT;
    const float cx = floorf(focus.x * 0.5f) * 2.0f;
    const float cz = floorf(focus.z * 0.5f) * 2.0f;
    const float alphaBase = fminf(0.5f, 0.16f + intensity * 0.14f);
    const float timeFall = time * fallSpeed;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_LINES);
    for (int i = 0; i < count; i++)
    {
        const RainStreakDef *st = &s_rainTable[i];
        float x = cx + st->dx, z = cz + st->dz;
        float fall = fmodf(st->phaseOffset + timeFall * st->speedJ, RAIN_COLUMN_HEIGHT);
        float y = topY - fall;
        unsigned char alpha = (unsigned char)((float)st->alphaScale * alphaBase);
        rlColor4ub(200, 220, 235, alpha);
        rlVertex3f(x, y, z);
        rlColor4ub(200, 220, 235, 0);
        rlVertex3f(x - windDx, y + streakLen, z - windDz);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}

typedef struct
{
    float dx, dz;
    float phaseOffset;
    float speedJ;
    float swirlPhase;
    float size;
    unsigned char alphaScale;
} SnowFlakeDef;

static SnowFlakeDef s_snowTable[SNOW_FLAKE_COUNT];
static bool s_snowTableBuilt = false;

static void Snow_EnsureTable(void)
{
    if (s_snowTableBuilt)
        return;
    for (int i = 0; i < SNOW_FLAKE_COUNT; i++)
    {
        unsigned int s = (unsigned int)i * 2654435761u + 0x9e3779b9u;
        float u = Weather_Hash(s + 1u), v = Weather_Hash(s + 2u);
        float phase = Weather_Hash(s + 3u);
        float su = sqrtf(u);
        float r = SNOW_COLUMN_RADIUS * su;
        float a = 6.2831853f * v;
        s_snowTable[i].dx = cosf(a) * r;
        s_snowTable[i].dz = sinf(a) * r;
        s_snowTable[i].phaseOffset = phase * SNOW_COLUMN_HEIGHT;
        s_snowTable[i].speedJ = 0.72f + 0.56f * Weather_Hash(s + 4u);
        s_snowTable[i].swirlPhase = Weather_Hash(s + 5u) * 6.2831853f;
        s_snowTable[i].size = 0.028f + 0.032f * Weather_Hash(s + 6u);
        float fade = 1.0f - su * 0.82f;
        s_snowTable[i].alphaScale = (unsigned char)(fade * 255.0f + 0.5f);
    }
    s_snowTableBuilt = true;
}

void Weather_DrawSnow(Vector3 focus, Camera3D cam, float time)
{
    const float intensity = s_snowVisible;
    if (intensity < 0.02f)
        return;

    Snow_EnsureTable();

    int count = (int)(SNOW_FLAKE_COUNT * fminf(1.0f, 0.30f + intensity * 0.55f));
    GfxQuality q = GfxQuality_Get();
    if (q < GFX_MED)
        count /= 2;

    Vector3 fwd = Vector3Subtract(cam.target, cam.position);
    float fLenSq = fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z;
    if (fLenSq < 1e-6f)
        return;
    fwd = Vector3Scale(fwd, 1.0f / sqrtf(fLenSq));
    Vector3 right = Vector3CrossProduct(fwd, cam.up);
    float rLenSq = right.x * right.x + right.y * right.y + right.z * right.z;
    if (rLenSq < 1e-6f)
        return;
    right = Vector3Scale(right, 1.0f / sqrtf(rLenSq));
    Vector3 up = Vector3CrossProduct(right, fwd);

    const float fallSpeed = 2.15f + intensity * 1.15f;
    const float windDriftX = -1.65f - intensity * 0.90f;
    const float windDriftZ = 1.10f + intensity * 0.60f;
    const float topY = focus.y + SNOW_COLUMN_HEIGHT;
    const float cx = floorf(focus.x * 0.5f) * 2.0f;
    const float cz = floorf(focus.z * 0.5f) * 2.0f;
    const float alphaBase = fminf(0.82f, 0.34f + intensity * 0.24f);
    const float timeFall = time * fallSpeed;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    for (int i = 0; i < count; i++)
    {
        const SnowFlakeDef *sf = &s_snowTable[i];
        float fall = fmodf(sf->phaseOffset + timeFall * sf->speedJ, SNOW_COLUMN_HEIGHT);
        float y = topY - fall;
        float flutter = time * 1.65f + sf->swirlPhase;
        float sFlut = sinf(flutter);
        float cFlut = cosf(flutter);

        float x = cx + fmodf(sf->dx + windDriftX * fall * 0.35f + sFlut * 0.45f + SNOW_COLUMN_RADIUS * 3.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;
        float z = cz + fmodf(sf->dz + windDriftZ * fall * 0.35f + cFlut * 0.45f + SNOW_COLUMN_RADIUS * 3.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;

        unsigned char alpha = (unsigned char)((float)sf->alphaScale * alphaBase);
        if (alpha < 6)
            continue;

        float hs = sf->size * 0.5f;
        float rx = right.x * hs, ry = right.y * hs, rz = right.z * hs;
        float ux = up.x * hs, uy = up.y * hs, uz = up.z * hs;

        rlColor4ub(238, 245, 255, alpha);
        rlVertex3f(x - rx - ux, y - ry - uy, z - rz - uz);
        rlVertex3f(x + rx - ux, y + ry - uy, z + rz - uz);
        rlVertex3f(x + rx + ux, y + ry + uy, z + rz + uz);
        rlVertex3f(x - rx + ux, y - ry + uy, z - rz + uz);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

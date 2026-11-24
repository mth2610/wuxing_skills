#include "environment/weather_system.h"
#include "environment/wind/wind_system.h"
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
static Texture2D s_snowFlakeTex = {0};

static void Snow_EnsureSpriteTexture(void)
{
    if (s_snowFlakeTex.id != 0)
        return;
    const int R = 32;
    Image img = GenImageColor(R, R, (Color){0, 0, 0, 0});
    for (int y = 0; y < R; y++)
    {
        for (int x = 0; x < R; x++)
        {
            float dx = ((float)x + 0.5f - (float)R * 0.5f) / ((float)R * 0.5f);
            float dy = ((float)y + 0.5f - (float)R * 0.5f) / ((float)R * 0.5f);
            float r = sqrtf(dx * dx + dy * dy);
            if (r >= 1.0f)
                continue;
            // Soft crystalline snowflake: bright core + subtle 6-pointed star + smooth Gaussian halo
            float ang = atan2f(dy, dx);
            float star = 0.82f + 0.18f * cosf(ang * 6.0f);
            float core = expf(-r * r * 6.5f);
            float halo = (1.0f - r) * (1.0f - r) * star * 0.58f;
            float a = fminf(1.0f, core * 0.85f + halo);
            unsigned char ab = (unsigned char)(a * 255.0f + 0.5f);
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255, ab});
        }
    }
    s_snowFlakeTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_snowFlakeTex.id != 0)
    {
        SetTextureFilter(s_snowFlakeTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_snowFlakeTex, TEXTURE_WRAP_CLAMP);
    }
}

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
    Snow_EnsureSpriteTexture();

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

    WindMacroConfig wind = Wind_GetMacro();
    float windX = isfinite(wind.baseDirection.x) ? wind.baseDirection.x : -1.65f;
    float windZ = isfinite(wind.baseDirection.z) ? wind.baseDirection.z : 1.10f;
    float windLen = sqrtf(windX * windX + windZ * windZ);
    if (windLen < 0.25f)
    {
        windX = -1.40f;
        windZ = 0.90f;
        windLen = sqrtf(windX * windX + windZ * windZ);
    }
    Vector3 windDir3 = {windX / windLen, 0.0f, windZ / windLen};

    const float fallSpeed = 2.05f + intensity * 1.10f;
    const float windDriftX = windX * (0.48f + intensity * 0.22f);
    const float windDriftZ = windZ * (0.48f + intensity * 0.22f);
    const float topY = focus.y + SNOW_COLUMN_HEIGHT;
    const float cx = floorf(focus.x * 0.5f) * 2.0f;
    const float cz = floorf(focus.z * 0.5f) * 2.0f;
    const float alphaBase = fminf(0.86f, 0.36f + intensity * 0.26f);
    const float timeFall = time * fallSpeed;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(s_snowFlakeTex.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < count; i++)
    {
        const SnowFlakeDef *sf = &s_snowTable[i];
        bool isSpindrift = (i % 5 == 0);
        float y, x, z;
        float hsX, hsY;
        Vector3 axisR = right;
        Vector3 axisU = up;

        if (isSpindrift)
        {
            // Low-altitude wind-driven snow spindrift sweeping across snow ridges
            float driftDist = time * (2.6f + windLen * 0.65f) * sf->speedJ;
            x = cx + fmodf(sf->dx + windDir3.x * driftDist + SNOW_COLUMN_RADIUS * 6.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;
            z = cz + fmodf(sf->dz + windDir3.z * driftDist + SNOW_COLUMN_RADIUS * 6.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;
            float wave = sinf(time * 2.1f + sf->swirlPhase) * 0.28f;
            y = focus.y + 0.25f + fmodf(sf->phaseOffset, 2.2f) + wave;
            hsX = sf->size * 2.4f;
            hsY = sf->size * 0.55f;
            // Align spindrift streak horizontally along camera-projected wind axis
            float projW = Vector3DotProduct(windDir3, right);
            Vector3 wFlat = Vector3Subtract(windDir3, Vector3Scale(fwd, Vector3DotProduct(windDir3, fwd)));
            float wFlatLen = Vector3Length(wFlat);
            if (wFlatLen > 0.05f)
            {
                axisR = Vector3Scale(wFlat, (projW < 0.0f ? -1.0f : 1.0f) / wFlatLen);
                axisU = Vector3CrossProduct(axisR, fwd);
            }
        }
        else
        {
            float fall = fmodf(sf->phaseOffset + timeFall * sf->speedJ, SNOW_COLUMN_HEIGHT);
            y = topY - fall;
            float flutter = time * 1.65f + sf->swirlPhase;
            float sFlut = sinf(flutter);
            float cFlut = cosf(flutter);

            x = cx + fmodf(sf->dx + windDriftX * fall * 0.35f + sFlut * 0.45f + SNOW_COLUMN_RADIUS * 6.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;
            z = cz + fmodf(sf->dz + windDriftZ * fall * 0.35f + cFlut * 0.45f + SNOW_COLUMN_RADIUS * 6.0f, SNOW_COLUMN_RADIUS * 2.0f) - SNOW_COLUMN_RADIUS;
            hsX = sf->size * 0.72f;
            hsY = sf->size * 0.72f;
        }

        unsigned char alpha = (unsigned char)((float)sf->alphaScale * alphaBase * (isSpindrift ? 0.52f : 1.0f));
        if (alpha < 6)
            continue;

        float rx = axisR.x * hsX, ry = axisR.y * hsX, rz = axisR.z * hsX;
        float ux = axisU.x * hsY, uy = axisU.y * hsY, uz = axisU.z * hsY;

        rlColor4ub(242, 248, 255, alpha);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(x - rx - ux, y - ry - uy, z - rz - uz);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(x + rx - ux, y + ry - uy, z + rz - uz);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(x + rx + ux, y + ry + uy, z + rz + uz);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(x - rx + ux, y - ry + uy, z - rz + uz);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

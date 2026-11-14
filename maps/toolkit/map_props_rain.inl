// Rain mode: global weather state (included by map_props.c before map_props_nature.inl).
// Smoothing constants follow the reference hydrology prototype (mang_luoi_song.html):
//   runoff  : rises 0.42/s, falls 0.28/s  -> stream swell lags the rain
//   wetness : rises 0.65/s, falls 0.25/s  -> ground saturation (infiltration drops as it rises)

#define RAIN_STREAK_COUNT 1400
#define RAIN_COLUMN_RADIUS 22.0f
#define RAIN_COLUMN_HEIGHT 16.0f

static float s_rainTarget = 0.0f;
static float s_rainVisible = 0.0f;
static float s_rainRunoff = 0.0f;
static float s_rainWetness = 0.0f;

void MapProp_SetRain(float intensity)
{
    s_rainTarget = fminf(fmaxf(intensity, 0.0f), 2.0f);
}

float MapProp_GetRainTarget(void) { return s_rainTarget; }
float MapProp_GetRain(void) { return s_rainVisible; }
float MapProp_GetRunoff(void) { return s_rainRunoff; }
float MapProp_GetWetness(void) { return s_rainWetness; }

static inline float Rain_Approach(float cur, float target, float riseRate, float fallRate, float dt)
{
    float rate = (target > cur) ? riseRate : fallRate;
    float next = cur + (target - cur) * fminf(1.0f, dt * rate);
    return (fabsf(target - next) < 0.002f) ? target : next;
}

void MapProp_UpdateRain(float dt)
{
    if (dt <= 0.0f)
        return;
    if (dt > 0.1f)
        dt = 0.1f;
    s_rainVisible = Rain_Approach(s_rainVisible, s_rainTarget, 1.2f, 0.8f, dt);
    s_rainRunoff = Rain_Approach(s_rainRunoff, s_rainTarget, 0.42f, 0.28f, dt);
    float wetTarget = fminf(1.0f, s_rainTarget * 1.4f);
    s_rainWetness = Rain_Approach(s_rainWetness, wetTarget, 0.65f, 0.25f, dt);
}

static inline float Rain_Hash(unsigned int n)
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return (float)(n & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

typedef struct
{
    float dx, dz;
    float phaseOffset;
    float speedJ;
    unsigned char alphaScale; // 0..255 radial fade
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
        float u = Rain_Hash(s + 1u), v = Rain_Hash(s + 2u);
        float phase = Rain_Hash(s + 3u);
        float su = sqrtf(u);
        float r = RAIN_COLUMN_RADIUS * su;
        float a = 6.2831853f * v;
        s_rainTable[i].dx = cosf(a) * r;
        s_rainTable[i].dz = sinf(a) * r;
        s_rainTable[i].phaseOffset = phase * RAIN_COLUMN_HEIGHT;
        s_rainTable[i].speedJ = 0.85f + 0.3f * Rain_Hash(s + 4u);
        float fade = 1.0f - su * 0.85f;
        s_rainTable[i].alphaScale = (unsigned char)(fade * 255.0f + 0.5f);
    }
    s_rainTableBuilt = true;
}

// Streaks use a precomputed disc table so per-frame drawing needs zero trig/hash calls.
void MapProp_DrawRain(Vector3 focus, float time)
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

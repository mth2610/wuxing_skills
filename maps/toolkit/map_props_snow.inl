// Snowfall & spindrift weather system (included by map_props.c)

#define SNOW_FLAKE_COUNT 1200
#define SNOW_COLUMN_RADIUS 24.0f
#define SNOW_COLUMN_HEIGHT 14.0f

static float s_snowTarget = 0.0f;
static float s_snowVisible = 0.0f;

void MapProp_SetSnow(float intensity)
{
    s_snowTarget = fminf(fmaxf(intensity, 0.0f), 2.0f);
}

float MapProp_GetSnow(void) { return s_snowVisible; }

static inline float Snow_Approach(float cur, float target, float riseRate, float fallRate, float dt)
{
    float rate = (target > cur) ? riseRate : fallRate;
    float next = cur + (target - cur) * fminf(1.0f, dt * rate);
    return (fabsf(target - next) < 0.002f) ? target : next;
}

void MapProp_UpdateSnow(float dt)
{
    if (dt <= 0.0f)
        return;
    if (dt > 0.1f)
        dt = 0.1f;
    s_snowVisible = Snow_Approach(s_snowVisible, s_snowTarget, 0.85f, 0.65f, dt);
}

static inline float Snow_Hash(unsigned int n)
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
        float u = Snow_Hash(s + 1u), v = Snow_Hash(s + 2u);
        float phase = Snow_Hash(s + 3u);
        float su = sqrtf(u);
        float r = SNOW_COLUMN_RADIUS * su;
        float a = 6.2831853f * v;
        s_snowTable[i].dx = cosf(a) * r;
        s_snowTable[i].dz = sinf(a) * r;
        s_snowTable[i].phaseOffset = phase * SNOW_COLUMN_HEIGHT;
        s_snowTable[i].speedJ = 0.72f + 0.56f * Snow_Hash(s + 4u);
        s_snowTable[i].swirlPhase = Snow_Hash(s + 5u) * 6.2831853f;
        s_snowTable[i].size = 0.028f + 0.032f * Snow_Hash(s + 6u);
        float fade = 1.0f - su * 0.82f;
        s_snowTable[i].alphaScale = (unsigned char)(fade * 255.0f + 0.5f);
    }
    s_snowTableBuilt = true;
}

void MapProp_DrawSnow(Vector3 focus, Camera3D cam, float time)
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
        float sFlut, cFlut;
        Nature_FastSinCos(flutter, &sFlut, &cFlut);

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

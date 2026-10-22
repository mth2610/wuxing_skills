#include "environment/wind/wind_system.h"
#include "core/force_field.h"
#include <float.h>
#include <math.h>
#include <stdio.h>  // WIND_STATS (stderr), chỉ dùng khi WUXING_WIND_STATS được đặt
#include <stdlib.h> // getenv
#include <string.h>

// =============================================================================
// BẤT BIẾN (invariants) - đọc trước khi sửa file này
//
//  1. Công thức field (hash / noise / Vorticle / terrain / guiding) giữ NGUYÊN
//     để khớp particle_gpu.comp. Mọi tối ưu trong file chỉ đổi cách tính, không
//     đổi kết quả. Nếu sửa công thức thì phải sửa shader tương ứng.
//  2. s_vorticles[0 .. s_activeCount) là các Vorticle đang sống và luôn thỏa
//     s_activeCount <= MAX_VORTICLES - s_motionAirflowCount. Nhờ vậy mọi
//     Vorticle trong pool đều thấy được bởi CPU evaluator và GPU snapshot
//     (trước đây Vorticle MỚI NHẤT là cái bị cắt khi motion airflow chiếm chỗ).
//  3. Không malloc/free. Không gọi raylib ngoài Wind_DrawDebug.
//  4. Không thread-safe: Update/Spawn/Evaluate/GetActiveVorticles phải cùng một
//     thread (hoặc caller tự đồng bộ).
// =============================================================================

#define WIND_MAX_MOTION_AIRFLOW 144

// Pool thường phải luôn còn chỗ khi motion airflow đầy (compile-time check, C99-safe).
typedef char Wind_StaticAssert_MotionBudget[(WIND_MAX_MOTION_AIRFLOW < MAX_VORTICLES) ? 1 : -1];

// Dữ liệu tiền tính toán theo frame cho từng Vorticle để tránh chia float lặp lại
// hàng ngàn lần trên mỗi particle / blade cỏ.
typedef struct
{
    Vector3 position;
    float radius;
    Vector3 direction;
    int type;
    float radiusSq;
    float invRadius;
    float effStrength;   // strength * temporalAtten (* attackWeight với TURBULENCE)
    float effInwardPull; // -inwardPull * temporalAtten
    float rCore;         // 0.10f * radius
    float invRCore;      // 1.0f / (0.10f * radius)
    bool hasInwardPull;
    bool valid;
} WindVorticleCompiled;

static VorticleData s_vorticles[MAX_VORTICLES];
static WindVorticleCompiled s_vorticlesCompiled[MAX_VORTICLES];
static int s_activeCount = 0;
static Vector3 s_ordinaryBoundsMin = {0};
static Vector3 s_ordinaryBoundsMax = {0};
static bool s_hasActiveOrdinary = false;

static VorticleData s_motionAirflow[WIND_MAX_MOTION_AIRFLOW];
static WindVorticleCompiled s_motionCompiled[WIND_MAX_MOTION_AIRFLOW];
static int s_motionAirflowCount = 0;
static Vector3 s_motionBoundsMin = {0};
static Vector3 s_motionBoundsMax = {0};
static bool s_hasActiveMotion = false;

// Snapshot motion + ordinary, chỉ dựng lại khi dữ liệu đổi (xem Wind_GetActiveVorticles).
static VorticleData s_combinedAirflow[MAX_VORTICLES];
static int s_combinedCount = 0;
static bool s_combinedValid = false;

static WindMacroConfig s_macroConfig;
static float s_macroBaseLen = 0.0f; // |baseDirection|, cache từ Wind_RefreshMacroCache
static float s_macroAmp = 0.0f;     // gustAmplitude * max(|base|, 2)
static WindGuidingGust s_guidingGust = {0};
static float s_guidingTotalDist = 0.0f; // cache, cố định suốt đợt gió
static TerrainHeightQueryFn s_terrainQuery = NULL;
static void *s_terrainUserData = NULL;
static WindTerrainGrid s_terrainGrid;
static Vector2 s_terrainInvCellSizeXZ = {0};
static unsigned int s_terrainVersionCounter = 0; // đơn điệu tăng, KHÔNG reset ở Init/Unload
static bool s_initialized = false;

// Memo 1 mục cho bước 1-3 của Wind_EvaluateVelocityChannels (macro + gradient độ cao +
// nâng địa hình). Ba bước này chỉ phụ thuộc (pos, time, s_macroConfig, terrain) và KHÔNG
// phụ thuộc includeMotion hay Vorticle, nên Wind_EvaluateVelocity và
// Wind_EvaluateBackgroundVelocity gọi liên tiếp cùng (pos, time) có thể dùng chung.
// Khóa so sánh theo từng bit nên kết quả giống hệt bản không memo. Phải đặt false mỗi khi
// macro hoặc terrain đổi (Init / Unload / SetMacro / SetTerrainHeightQuery / RebuildTerrainGrid).
static bool s_baseMemoValid = false;

// Bộ đếm chẩn đoán. In mỗi 5 giây khi đặt WUXING_WIND_STATS=1 (ra stderr).
typedef struct
{
    unsigned int updates;
    unsigned int macroCalls;     // Wind_GetMacroAt (3 lần noise mỗi lần gọi)
    unsigned int evalFull;       // Wind_EvaluateVelocity (có motion airflow)
    unsigned int evalBackground; // Wind_EvaluateBackgroundVelocity
    unsigned int memoHits;       // số lần bước 1-3 được lấy từ memo thay vì tính lại
} WindStats;
static WindStats s_stats;
static int s_statsEnabled = -1; // <0: chưa đọc env
static float s_statsAccum = 0.0f;

// -----------------------------------------------------------------------------
// Helpers an toàn số học & Fast Math
// -----------------------------------------------------------------------------
static inline bool Wind_IsFiniteVec3(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static inline Vector3 Wind_NormalizeOr(Vector3 v, Vector3 fallback)
{
    float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (isfinite(lenSq) && lenSq > 1e-8f)
    {
        if (fabsf(lenSq - 1.0f) <= 1e-5f)
            return v;
        return Vector3Scale(v, 1.0f / sqrtf(lenSq));
    }
    return fallback;
}

static unsigned int Wind_NextTerrainVersion(void)
{
    s_terrainVersionCounter++;
    if (s_terrainVersionCounter == 0u)
        s_terrainVersionCounter = 1u;
    return s_terrainVersionCounter;
}

static inline float Wind_FastFloor(float x)
{
    int xi = (int)x;
    return (float)(x < (float)xi ? xi - 1 : xi);
}

static inline float Wind_Fract(float v)
{
    return v - Wind_FastFloor(v);
}

// Dành riêng cho đối số không âm nằm trong khoảng int (ví dụ bên trong bước cuối của Hash3)
static inline float Wind_FractPositive(float v)
{
    return v - (float)(int)v;
}

static inline float Wind_TurbulenceAttackWeight(const VorticleData *v)
{
    float attackTime = v->direction.z;
    if (attackTime <= 1e-4f)
        return 1.0f;
    float age = fmaxf(0.0f, v->maxLifetime - v->lifetime);
    float t = fminf(age / attackTime, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline float Wind_HashCornerDot(float px, float py, float pz, float d,
                                       float fx, float fy, float fz)
{
    px += d;
    py += d;
    pz += d;
    float gx = Wind_FractPositive((px + py) * pz) * 2.0f - 1.0f;
    float gy = Wind_FractPositive((px + px) * py) * 2.0f - 1.0f;
    float gz = Wind_FractPositive((py + px) * px) * 2.0f - 1.0f;
    return gx * fx + gy * fy + gz * fz;
}

// Giữ nguyên 100% công thức của particle_gpu.comp nhưng chia sẻ phép tính
// giữa 8 đỉnh của ô lưới (giảm từ 54 lệnh floorf() xuống 9 phép FastFloor + 24 FractPositive).
static float Wind_NoiseScalar3D(Vector3 p)
{
    float lx0 = Wind_FastFloor(p.x);
    float ly0 = Wind_FastFloor(p.y);
    float lz0 = Wind_FastFloor(p.z);

    float fx0 = p.x - lx0;
    float fy0 = p.y - ly0;
    float fz0 = p.z - lz0;

    float fx1 = fx0 - 1.0f;
    float fy1 = fy0 - 1.0f;
    float fz1 = fz0 - 1.0f;

    float ux = fx0 * fx0 * (3.0f - 2.0f * fx0);
    float uy = fy0 * fy0 * (3.0f - 2.0f * fy0);
    float uz = fz0 * fz0 * (3.0f - 2.0f * fz0);

    float px0 = Wind_Fract(lx0 * 0.1031f);
    float px1 = Wind_Fract((lx0 + 1.0f) * 0.1031f);
    float py0 = Wind_Fract(ly0 * 0.1030f);
    float py1 = Wind_Fract((ly0 + 1.0f) * 0.1030f);
    float pz0 = Wind_Fract(lz0 * 0.0973f);
    float pz1 = Wind_Fract((lz0 + 1.0f) * 0.0973f);

    float dz0 = pz0 * (pz0 + 33.33f);
    float dz1 = pz1 * (pz1 + 33.33f);

    float dxy00 = px0 * (py0 + 33.33f) + py0 * (px0 + 33.33f);
    float dxy10 = px1 * (py0 + 33.33f) + py0 * (px1 + 33.33f);
    float dxy01 = px0 * (py1 + 33.33f) + py1 * (px0 + 33.33f);
    float dxy11 = px1 * (py1 + 33.33f) + py1 * (px1 + 33.33f);

    float n000 = Wind_HashCornerDot(px0, py0, pz0, dxy00 + dz0, fx0, fy0, fz0);
    float n100 = Wind_HashCornerDot(px1, py0, pz0, dxy10 + dz0, fx1, fy0, fz0);
    float n010 = Wind_HashCornerDot(px0, py1, pz0, dxy01 + dz0, fx0, fy1, fz0);
    float n110 = Wind_HashCornerDot(px1, py1, pz0, dxy11 + dz0, fx1, fy1, fz0);
    float n001 = Wind_HashCornerDot(px0, py0, pz1, dxy00 + dz1, fx0, fy0, fz1);
    float n101 = Wind_HashCornerDot(px1, py0, pz1, dxy10 + dz1, fx1, fy0, fz1);
    float n011 = Wind_HashCornerDot(px0, py1, pz1, dxy01 + dz1, fx0, fy1, fz1);
    float n111 = Wind_HashCornerDot(px1, py1, pz1, dxy11 + dz1, fx1, fy1, fz1);

    float nx00 = n000 + (n100 - n000) * ux;
    float nx10 = n010 + (n110 - n010) * ux;
    float nx01 = n001 + (n101 - n001) * ux;
    float nx11 = n011 + (n111 - n011) * ux;
    float nxy0 = nx00 + (nx10 - nx00) * uy;
    float nxy1 = nx01 + (nx11 - nx01) * uy;
    return nxy0 + (nxy1 - nxy0) * uz;
}

// -----------------------------------------------------------------------------
// Tiền biên dịch Vorticle & Hộp bao AABB (Broadphase)
// -----------------------------------------------------------------------------
static inline void Wind_CompileOneVorticle(const VorticleData *v,
                                           WindVorticleCompiled *out)
{
    if (!v->active || !(v->radius > 0.0f) || !isfinite(v->radius) ||
        !Wind_IsFiniteVec3(v->position) || !isfinite(v->strength))
    {
        out->valid = false;
        return;
    }
    float rSq = v->radius * v->radius;
    if (rSq < 1e-6f)
    {
        out->valid = false;
        return;
    }

    float temporalAtten = (v->maxLifetime > 1e-4f)
                              ? (v->lifetime / v->maxLifetime)
                              : 1.0f;
    if (!(temporalAtten > 0.0f) || !isfinite(temporalAtten))
    {
        out->valid = false;
        return;
    }

    float effStrength = v->strength * temporalAtten;
    if (v->type == VORTICLE_TURBULENCE)
    {
        effStrength *= Wind_TurbulenceAttackWeight(v);
        if (effStrength == 0.0f)
        {
            out->valid = false;
            return;
        }
    }

    bool hasPull = (v->type == VORTICLE_VORTEX) && (fabsf(v->inwardPull) > 1e-4f);
    if (effStrength == 0.0f && !hasPull)
    {
        out->valid = false;
        return;
    }

    out->position = v->position;
    out->radius = v->radius;
    out->direction = v->direction;
    out->type = (int)v->type;
    out->radiusSq = rSq;
    out->invRadius = 1.0f / v->radius;
    out->effStrength = effStrength;
    out->effInwardPull = hasPull ? (-v->inwardPull * temporalAtten) : 0.0f;
    out->rCore = 0.10f * v->radius;
    out->invRCore = 1.0f / out->rCore;
    out->hasInwardPull = hasPull;
    out->valid = true;
}

static inline void Wind_ExpandCompiledBounds(const WindVorticleCompiled *c,
                                             Vector3 *lo, Vector3 *hi)
{
    float r = c->radius;
    lo->x = fminf(lo->x, c->position.x - r);
    lo->y = fminf(lo->y, c->position.y - r);
    lo->z = fminf(lo->z, c->position.z - r);
    hi->x = fmaxf(hi->x, c->position.x + r);
    hi->y = fmaxf(hi->y, c->position.y + r);
    hi->z = fmaxf(hi->z, c->position.z + r);
}

static void Wind_RebuildOrdinaryCompiled(int count)
{
    Vector3 lo = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vector3 hi = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool any = false;
    for (int i = 0; i < count; ++i)
    {
        Wind_CompileOneVorticle(&s_vorticles[i], &s_vorticlesCompiled[i]);
        if (s_vorticlesCompiled[i].valid)
        {
            Wind_ExpandCompiledBounds(&s_vorticlesCompiled[i], &lo, &hi);
            any = true;
        }
    }
    s_ordinaryBoundsMin = any ? lo : (Vector3){0};
    s_ordinaryBoundsMax = any ? hi : (Vector3){0};
    s_hasActiveOrdinary = any;
}

static void Wind_RebuildMotionCompiled(void)
{
    Vector3 lo = {FLT_MAX, FLT_MAX, FLT_MAX};
    Vector3 hi = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool any = false;
    for (int i = 0; i < s_motionAirflowCount; ++i)
    {
        Wind_CompileOneVorticle(&s_motionAirflow[i], &s_motionCompiled[i]);
        if (s_motionCompiled[i].valid)
        {
            Wind_ExpandCompiledBounds(&s_motionCompiled[i], &lo, &hi);
            any = true;
        }
    }
    s_motionBoundsMin = any ? lo : (Vector3){0};
    s_motionBoundsMax = any ? hi : (Vector3){0};
    s_hasActiveMotion = any;
}

// -----------------------------------------------------------------------------
// Pool: dung lượng khả dụng, loại bỏ, cấp phát
// -----------------------------------------------------------------------------
static inline int Wind_OrdinaryCapacity(void)
{
    return MAX_VORTICLES - s_motionAirflowCount;
}

static inline int Wind_VisibleOrdinaryCount(void)
{
    int cap = Wind_OrdinaryCapacity();
    return s_activeCount < cap ? s_activeCount : cap;
}

static int Wind_FindShortestLife(void)
{
    int minIdx = 0;
    float minLife = s_vorticles[0].lifetime;
    for (int i = 1; i < s_activeCount; i++)
    {
        if (s_vorticles[i].lifetime < minLife)
        {
            minLife = s_vorticles[i].lifetime;
            minIdx = i;
        }
    }
    return minIdx;
}

static bool Wind_TrimOrdinary(int limit)
{
    if (limit < 0)
        limit = 0;
    bool trimmed = false;
    while (s_activeCount > limit)
    {
        int victim = Wind_FindShortestLife();
        s_vorticles[victim] = s_vorticles[s_activeCount - 1];
        s_vorticlesCompiled[victim] = s_vorticlesCompiled[s_activeCount - 1];
        s_activeCount--;
        s_combinedValid = false;
        trimmed = true;
    }
    return trimmed;
}

static int AllocVorticleSlot(bool *outRebuiltBounds)
{
    const int cap = Wind_OrdinaryCapacity();
    s_combinedValid = false;
    *outRebuiltBounds = false;
    if (s_activeCount < cap)
        return s_activeCount++;

    Wind_TrimOrdinary(cap);
    *outRebuiltBounds = true;
    return Wind_FindShortestLife();
}

// -----------------------------------------------------------------------------
// Vòng đời
// -----------------------------------------------------------------------------
static void Wind_RefreshMacroCache(void)
{
    s_macroBaseLen = Vector3Length(s_macroConfig.baseDirection);
    s_macroAmp = s_macroConfig.gustAmplitude * fmaxf(s_macroBaseLen, 2.0f);
}

void Wind_Init(void)
{
    memset(s_vorticles, 0, sizeof(s_vorticles));
    memset(s_vorticlesCompiled, 0, sizeof(s_vorticlesCompiled));
    s_activeCount = 0;
    s_hasActiveOrdinary = false;
    s_ordinaryBoundsMin = s_ordinaryBoundsMax = (Vector3){0};

    s_motionAirflowCount = 0;
    s_hasActiveMotion = false;
    s_motionBoundsMin = s_motionBoundsMax = (Vector3){0};

    s_combinedCount = 0;
    s_combinedValid = false;
    memset(&s_guidingGust, 0, sizeof(s_guidingGust));
    s_guidingTotalDist = 0.0f;
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainInvCellSizeXZ = (Vector2){0};
    s_terrainGrid.version = Wind_NextTerrainVersion();

    s_macroConfig = (WindMacroConfig){
        .baseDirection = (Vector3){1.5f, 0.0f, 0.8f},
        .gustAmplitude = 6.0f,
        .noiseScale = 0.14f,
        .noiseSpeed = 1.0f,
        .terrainLiftK = 1.2f,
        .heightGradientK = 1.0f,
    };
    Wind_RefreshMacroCache();
    s_baseMemoValid = false;
    s_initialized = true;
}

void Wind_Clear(void)
{
    if (s_activeCount > 0)
        memset(s_vorticles, 0, (size_t)s_activeCount * sizeof(VorticleData));
    s_activeCount = 0;
    s_hasActiveOrdinary = false;
    s_ordinaryBoundsMin = s_ordinaryBoundsMax = (Vector3){0};

    s_motionAirflowCount = 0;
    s_hasActiveMotion = false;
    s_motionBoundsMin = s_motionBoundsMax = (Vector3){0};

    s_combinedCount = 0;
    s_combinedValid = false;
    s_guidingGust.active = false;
    s_guidingGust.intensity = 0.0f;
}

void Wind_Unload(void)
{
    Wind_Clear();
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainInvCellSizeXZ = (Vector2){0};
    s_terrainGrid.version = Wind_NextTerrainVersion();
    memset(&s_guidingGust, 0, sizeof(s_guidingGust));
    s_guidingTotalDist = 0.0f;
    s_baseMemoValid = false;
    s_initialized = false;
}

static bool Wind_MacroConfigFinite(const WindMacroConfig *c)
{
    return Wind_IsFiniteVec3(c->baseDirection) &&
           isfinite(c->gustAmplitude) && isfinite(c->noiseScale) &&
           isfinite(c->noiseSpeed) && isfinite(c->terrainLiftK) &&
           isfinite(c->heightGradientK);
}

void Wind_SetMacro(const WindMacroConfig *cfg)
{
    if (!cfg || !Wind_MacroConfigFinite(cfg))
        return;
    s_macroConfig = *cfg;
    Wind_RefreshMacroCache();
    s_baseMemoValid = false;
}

WindMacroConfig Wind_GetMacro(void)
{
    return s_macroConfig;
}
int Wind_GetPublishedMotionAirflowCount(void) { return s_motionAirflowCount; }

// -----------------------------------------------------------------------------
// Terrain
// -----------------------------------------------------------------------------
void Wind_SetTerrainHeightQuery(TerrainHeightQueryFn queryFn, void *userData)
{
    s_terrainQuery = queryFn;
    s_terrainUserData = userData;
    s_baseMemoValid = false;
    s_terrainGrid.built = false;
    s_terrainGrid.active = false;
    s_terrainInvCellSizeXZ = (Vector2){0};
    s_terrainGrid.version = Wind_NextTerrainVersion();
}

void Wind_RebuildTerrainGrid(Vector2 centerXZ, Vector2 halfExtentXZ)
{
    unsigned int nextVersion = Wind_NextTerrainVersion();
    s_baseMemoValid = false;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainInvCellSizeXZ = (Vector2){0};
    s_terrainGrid.version = nextVersion;
    s_terrainGrid.built = true;

    if (s_terrainQuery == NULL ||
        !isfinite(centerXZ.x) || !isfinite(centerXZ.y) ||
        !isfinite(halfExtentXZ.x) || !isfinite(halfExtentXZ.y) ||
        halfExtentXZ.x <= 0.0f || halfExtentXZ.y <= 0.0f)
        return;

    s_terrainGrid.originXZ = (Vector2){centerXZ.x - halfExtentXZ.x,
                                       centerXZ.y - halfExtentXZ.y};
    s_terrainGrid.cellSizeXZ = (Vector2){
        (halfExtentXZ.x * 2.0f) / (float)(WIND_TERRAIN_GRID_SIZE - 1),
        (halfExtentXZ.y * 2.0f) / (float)(WIND_TERRAIN_GRID_SIZE - 1),
    };
    s_terrainInvCellSizeXZ = (Vector2){
        1.0f / s_terrainGrid.cellSizeXZ.x,
        1.0f / s_terrainGrid.cellSizeXZ.y,
    };

    int validCount = 0;
    for (int z = 0; z < WIND_TERRAIN_GRID_SIZE; ++z)
    {
        float worldZ = s_terrainGrid.originXZ.y +
                       (float)z * s_terrainGrid.cellSizeXZ.y;
        for (int x = 0; x < WIND_TERRAIN_GRID_SIZE; ++x)
        {
            int index = z * WIND_TERRAIN_GRID_SIZE + x;
            float worldX = s_terrainGrid.originXZ.x +
                           (float)x * s_terrainGrid.cellSizeXZ.x;
            float height = s_terrainQuery(worldX, worldZ, s_terrainUserData);
            if (isfinite(height))
            {
                s_terrainGrid.heights[index] = height;
                s_terrainGrid.valid[index] = 1;
                validCount++;
            }
        }
    }
    s_terrainGrid.active = validCount > 0;
}

const WindTerrainGrid *Wind_GetTerrainGrid(void)
{
    return &s_terrainGrid;
}

static bool Wind_SampleTerrainGrid(float worldX, float worldZ, float *outHeight)
{
    if (!s_terrainGrid.built || !s_terrainGrid.active || outHeight == NULL)
        return false;

    float gx = (worldX - s_terrainGrid.originXZ.x) * s_terrainInvCellSizeXZ.x;
    float gz = (worldZ - s_terrainGrid.originXZ.y) * s_terrainInvCellSizeXZ.y;
    const float gridMax = (float)(WIND_TERRAIN_GRID_SIZE - 1);
    if (!(gx >= 0.0f && gx <= gridMax && gz >= 0.0f && gz <= gridMax))
        return false;

    int x0 = (int)gx;
    int z0 = (int)gz;
    if (x0 >= WIND_TERRAIN_GRID_SIZE - 1)
        x0 = WIND_TERRAIN_GRID_SIZE - 2;
    if (z0 >= WIND_TERRAIN_GRID_SIZE - 1)
        z0 = WIND_TERRAIN_GRID_SIZE - 2;
    float fx = gx - (float)x0;
    float fz = gz - (float)z0;
    int i00 = z0 * WIND_TERRAIN_GRID_SIZE + x0;
    int i10 = i00 + 1;
    int i01 = i00 + WIND_TERRAIN_GRID_SIZE;
    int i11 = i01 + 1;
    if (!s_terrainGrid.valid[i00] || !s_terrainGrid.valid[i10] ||
        !s_terrainGrid.valid[i01] || !s_terrainGrid.valid[i11])
        return false;

    float h0 = s_terrainGrid.heights[i00] +
               (s_terrainGrid.heights[i10] - s_terrainGrid.heights[i00]) * fx;
    float h1 = s_terrainGrid.heights[i01] +
               (s_terrainGrid.heights[i11] - s_terrainGrid.heights[i01]) * fx;
    *outHeight = h0 + (h1 - h0) * fz;
    return true;
}

bool Wind_SampleTerrain(float worldX, float worldZ, float *outHeight)
{
    if (s_terrainGrid.built)
        return Wind_SampleTerrainGrid(worldX, worldZ, outHeight);
    if (s_terrainQuery == NULL || outHeight == NULL)
        return false;
    float height = s_terrainQuery(worldX, worldZ, s_terrainUserData);
    if (!isfinite(height))
        return false;
    *outHeight = height;
    return true;
}

typedef struct
{
    bool sampled;
    bool valid;
    float h;
} WindGroundSample;

static inline bool Wind_GroundHeight(WindGroundSample *g, float x, float z)
{
    if (!g->sampled)
    {
        g->sampled = true;
        g->h = 0.0f;
        g->valid = Wind_SampleTerrain(x, z, &g->h);
    }
    return g->valid;
}

// -----------------------------------------------------------------------------
// Quản lý mảng Vorticles (Active compaction)
// -----------------------------------------------------------------------------
static void Wind_StatsTick(float dt)
{
    if (s_statsEnabled < 0)
        s_statsEnabled = (getenv("WUXING_WIND_STATS") != NULL) ? 1 : 0;
    if (!s_statsEnabled)
        return;
    s_statsAccum += dt;
    s_stats.updates++;
    if (s_statsAccum < 5.0f)
        return;
    fprintf(stderr,
            "WIND_STATS: %.1fs updates=%u macroAt=%u evalFull=%u evalBg=%u memoHits=%u "
            "vorticles=%d motion=%d\n",
            (double)s_statsAccum, s_stats.updates, s_stats.macroCalls, s_stats.evalFull,
            s_stats.evalBackground, s_stats.memoHits, s_activeCount, s_motionAirflowCount);
    memset(&s_stats, 0, sizeof(s_stats));
    s_statsAccum = 0.0f;
}

void Wind_Update(float dt)
{
    if (!s_initialized)
        return;
    if (!(dt > 0.0f))
        return;
    Wind_StatsTick(dt);

    // 1. Cập nhật tiến trình đợt Gió Dẫn Đường (Guiding Wind)
    if (s_guidingGust.active)
    {
        s_guidingGust.elapsed += dt;
        s_guidingGust.progress = s_guidingGust.elapsed / s_guidingGust.duration;
        if (s_guidingGust.progress >= 1.0f)
        {
            s_guidingGust.active = false;
            s_guidingGust.intensity = 0.0f;
            s_guidingGust.progress = 1.0f;
        }
        else
        {
            float p = s_guidingGust.progress;
            float attack = fminf(p / 0.15f, 1.0f);
            float decay = (p > 0.60f) ? (1.0f - (p - 0.60f) / 0.40f) : 1.0f;
            s_guidingGust.intensity = attack * decay;
        }
    }

    if (s_activeCount <= 0)
        return;

    // 2+3. Trừ thời gian sống và dồn mảng liên tục trong MỘT lượt duyệt
    const int count = s_activeCount;
    int writeIdx = 0;
    for (int readIdx = 0; readIdx < count; readIdx++)
    {
        const VorticleData *src = &s_vorticles[readIdx];
        const float life = src->lifetime - dt;
        if (!src->active || !(life > 0.0f))
            continue;
        if (writeIdx != readIdx)
            s_vorticles[writeIdx] = *src;
        s_vorticles[writeIdx].lifetime = life;
        writeIdx++;
    }
    s_activeCount = writeIdx;
    s_combinedValid = false;

    Wind_TrimOrdinary(Wind_OrdinaryCapacity());
    Wind_RebuildOrdinaryCompiled(Wind_VisibleOrdinaryCount());
}

// -----------------------------------------------------------------------------
// Emitters
// -----------------------------------------------------------------------------
static inline bool Wind_EmitterArgsValid(Vector3 pos, float radius, float strength, float duration)
{
    return s_initialized &&
           duration > 0.0f && isfinite(duration) &&
           radius > 0.0f && isfinite(radius) &&
           isfinite(strength) && Wind_IsFiniteVec3(pos);
}

static int Wind_Emit(VorticleType type, Vector3 pos, Vector3 dir, float radius,
                     float strength, float inwardPull, float duration)
{
    bool rebuiltBounds = false;
    const int idx = AllocVorticleSlot(&rebuiltBounds);
    s_vorticles[idx] = (VorticleData){
        .position = pos,
        .direction = dir,
        .radius = radius,
        .strength = strength,
        .type = type,
        .lifetime = duration,
        .maxLifetime = duration,
        .inwardPull = inwardPull,
        .active = true,
    };

    if (rebuiltBounds)
    {
        Wind_RebuildOrdinaryCompiled(Wind_VisibleOrdinaryCount());
    }
    else
    {
        Wind_CompileOneVorticle(&s_vorticles[idx], &s_vorticlesCompiled[idx]);
        if (s_vorticlesCompiled[idx].valid)
        {
            if (!s_hasActiveOrdinary)
            {
                float r = s_vorticlesCompiled[idx].radius;
                s_ordinaryBoundsMin = (Vector3){pos.x - r, pos.y - r, pos.z - r};
                s_ordinaryBoundsMax = (Vector3){pos.x + r, pos.y + r, pos.z + r};
                s_hasActiveOrdinary = true;
            }
            else
            {
                Wind_ExpandCompiledBounds(&s_vorticlesCompiled[idx],
                                          &s_ordinaryBoundsMin,
                                          &s_ordinaryBoundsMax);
            }
        }
    }
    return idx;
}

int Wind_SpawnGust(Vector3 pos, Vector3 dir, float radius, float strength, float duration)
{
    if (!Wind_EmitterArgsValid(pos, radius, strength, duration))
        return -1;
    Vector3 normDir = Wind_NormalizeOr(dir, (Vector3){1.0f, 0.0f, 0.0f});
    return Wind_Emit(VORTICLE_LINEAR_GUST, pos, normDir, radius, strength, 0.0f, duration);
}

int Wind_SpawnRadialBlast(Vector3 pos, float radius, float strength, float duration)
{
    if (!Wind_EmitterArgsValid(pos, radius, strength, duration))
        return -1;
    return Wind_Emit(VORTICLE_RADIAL_BLAST, pos, (Vector3){0.0f, 1.0f, 0.0f},
                     radius, strength, 0.0f, duration);
}

int Wind_SpawnVortex(Vector3 pos, Vector3 axis, float radius, float strength, float inwardPull, float duration)
{
    if (!Wind_EmitterArgsValid(pos, radius, strength, duration) || !isfinite(inwardPull))
        return -1;
    Vector3 normAxis = Wind_NormalizeOr(axis, (Vector3){0.0f, 1.0f, 0.0f});
    return Wind_Emit(VORTICLE_VORTEX, pos, normAxis, radius, strength, inwardPull, duration);
}

int Wind_SpawnTurbulence(Vector3 pos, float radius, float strength, float noiseScale, float noiseSpeed, float duration)
{
    if (!Wind_EmitterArgsValid(pos, radius, strength, duration) ||
        !isfinite(noiseScale) || !isfinite(noiseSpeed))
        return -1;

    float attackTime = fminf(0.20f, duration * 0.25f);
    return Wind_Emit(VORTICLE_TURBULENCE, pos, (Vector3){noiseScale, noiseSpeed, attackTime},
                     radius, strength, 0.0f, duration);
}

// -----------------------------------------------------------------------------
// Evaluators
// -----------------------------------------------------------------------------
Vector3 Wind_GetMacroAt(Vector3 pos, float time)
{
    s_stats.macroCalls++;
    if (!s_initialized || s_macroBaseLen < 1e-4f)
        return (Vector3){0};

    if (s_macroConfig.gustAmplitude <= 1e-4f)
        return s_macroConfig.baseDirection;

    float s = s_macroConfig.noiseScale;
    float px = pos.x * s;
    float py = pos.y * s;
    float pz = pos.z * s;
    float t = time * s_macroConfig.noiseSpeed;

    float nx = Wind_NoiseScalar3D((Vector3){px - t, py + 17.3f, pz - t * 0.7f});
    float ny = Wind_NoiseScalar3D((Vector3){px + 37.1f, py - t * 0.8f, pz + 19.7f});
    float nz = Wind_NoiseScalar3D((Vector3){px - t * 0.6f, py + 53.9f, pz + t * 0.5f});

    const float amp = s_macroAmp;
    return (Vector3){
        s_macroConfig.baseDirection.x * (1.0f + s_macroConfig.gustAmplitude * nx * 0.5f) + nx * amp * 0.5f,
        s_macroConfig.baseDirection.y + ny * amp * 0.35f,
        s_macroConfig.baseDirection.z * (1.0f + s_macroConfig.gustAmplitude * nz * 0.5f) + nz * amp * 0.5f};
}

float Wind_HeightFactor(float hRel)
{
    if (hRel <= 0.0f)
        return 0.5f;
    float low = fminf(hRel * 0.4f, 1.0f); // 1.0f / 2.5f = 0.4f
    float high = (hRel > 2.5f) ? fminf((hRel - 2.5f) * (1.0f / 9.5f), 1.0f) : 0.0f;
    return 0.5f + 0.5f * low + 0.4f * high;
}

// Đánh giá cực nhanh từ struct đã tiền tính toán hằng số theo frame
static inline bool Wind_VorticleCompiledVelocity(const WindVorticleCompiled *v,
                                                 Vector3 pos, float time,
                                                 Vector3 *out)
{
    if (!v->valid)
        return false;

    const float radius = v->radius;
    const float dx = pos.x - v->position.x;
    const float adx = fabsf(dx);
    if (!(adx < radius))
        return false;
    const float dy = pos.y - v->position.y;
    const float ady = fabsf(dy);
    if (!(ady < radius))
        return false;
    const float dz = pos.z - v->position.z;
    const float adz = fabsf(dz);
    if (!(adz < radius))
        return false;

    const float distSq = adx * adx + ady * ady + adz * adz;
    if (!(distSq < v->radiusSq))
        return false;

    const float dist = sqrtf(distSq);
    const float spatialAtten = 1.0f - (dist * v->invRadius);

    Vector3 velocity = {0};
    switch (v->type)
    {
    case VORTICLE_LINEAR_GUST:
        velocity = Vector3Scale(v->direction, v->effStrength * spatialAtten);
        break;

    case VORTICLE_RADIAL_BLAST:
    {
        float s = v->effStrength * spatialAtten;
        if (dist > 1e-4f)
        {
            float scale = s / dist;
            velocity = (Vector3){dx * scale, dy * scale, dz * scale};
        }
        else
        {
            velocity = (Vector3){0.0f, s, 0.0f};
        }
        break;
    }

    case VORTICLE_VORTEX:
    {
        const Vector3 delta = {dx, dy, dz};
        Vector3 tangent = Vector3CrossProduct(v->direction, delta);
        float tanLen = Vector3Length(tangent);
        float coreFactor = (tanLen < v->rCore) ? (tanLen * v->invRCore) : 1.0f;
        if (tanLen > 1e-4f)
        {
            float scale = (v->effStrength * spatialAtten * coreFactor) / tanLen;
            velocity = Vector3Scale(tangent, scale);
        }
        if (v->hasInwardPull)
        {
            float proj = Vector3DotProduct(delta, v->direction);
            Vector3 radial = {
                dx - v->direction.x * proj,
                dy - v->direction.y * proj,
                dz - v->direction.z * proj,
            };
            float radDist = Vector3Length(radial);
            if (radDist > 1e-4f)
            {
                float pullScale = (v->effInwardPull * spatialAtten * coreFactor) / radDist;
                velocity.x += radial.x * pullScale;
                velocity.y += radial.y * pullScale;
                velocity.z += radial.z * pullScale;
            }
        }
        break;
    }

    case VORTICLE_TURBULENCE:
    {
        const float strength = v->effStrength * spatialAtten;
        const float ns = v->direction.x;
        const float t = time * v->direction.y;
        const float px = pos.x * ns;
        const float py = pos.y * ns;
        const float pz = pos.z * ns;
        const float nx = Wind_NoiseScalar3D((Vector3){px + t, py + 17.3f, pz - t * 0.7f});
        const float ny = Wind_NoiseScalar3D((Vector3){px + 37.1f, py - t * 0.8f, pz + 19.7f});
        const float nz = Wind_NoiseScalar3D((Vector3){px - t * 0.6f, py + 53.9f, pz + t * 0.5f});
        velocity = (Vector3){nx * strength, ny * strength, nz * strength};
        break;
    }

    default:
        return false;
    }

    *out = velocity;
    return true;
}

// Dành cho lời gọi trực tiếp Wind_EvaluateVorticleVelocity từ bên ngoài
static inline bool Wind_VorticleVelocity(const VorticleData *v, Vector3 pos, float time, Vector3 *out)
{
    WindVorticleCompiled compiled;
    Wind_CompileOneVorticle(v, &compiled);
    return Wind_VorticleCompiledVelocity(&compiled, pos, time, out);
}

Vector3 Wind_EvaluateVorticleVelocity(const VorticleData *vorticle, Vector3 pos, float time)
{
    Vector3 velocity = {0};
    if (vorticle != NULL)
        (void)Wind_VorticleVelocity(vorticle, pos, time, &velocity);
    return velocity;
}

static void Wind_AddGuidingWind(Vector3 pos, float time, WindGroundSample *ground, Vector3 *totalVel)
{
    // Kiểm tra độ cao Y trước để tránh tính hình học và sqrtf() khi nằm ngoài tầng 2.8m
    float dY = fabsf(pos.y - s_guidingGust.playerPos.y);
    if (!(dY < 2.8f))
        return;

    const float intensity = s_guidingGust.intensity;
    const Vector3 gDir = s_guidingGust.direction;
    const float gSpeed = s_guidingGust.speed;

    float toX = pos.x - s_guidingGust.playerPos.x;
    float toZ = pos.z - s_guidingGust.playerPos.z;
    float dLong = toX * gDir.x + toZ * gDir.z;

    if (!(dLong >= -1.5f && dLong <= s_guidingTotalDist + 3.0f))
        return;

    float perpX = toX - gDir.x * dLong;
    float perpZ = toZ - gDir.z * dLong;
    float dPerpSq = perpX * perpX + perpZ * perpZ;

    const float corridorRadius = 3.8f;
    if (!(dPerpSq < corridorRadius * corridorRadius))
        return;

    float dPerp = sqrtf(dPerpSq);
    float lateralAtten = 1.0f - (dPerp * (1.0f / 3.8f));
    lateralAtten = lateralAtten * lateralAtten;
    float vertAtten = 1.0f - dY * (1.0f / 2.8f);

    float corridorWeight = lateralAtten * vertAtten * intensity;
    if (!(corridorWeight > 0.001f))
        return;

    float waveLow = sinf(dLong * 0.20f - time * 3.5f) * 0.22f;
    float modulatedSpeed = gSpeed * (1.0f + waveLow) * corridorWeight;
    Vector3 guideVel = Vector3Scale(gDir, modulatedSpeed);

    float px = pos.x * 0.35f;
    float py = pos.y * 0.35f;
    float pz = pos.z * 0.35f;
    float nx = Wind_NoiseScalar3D((Vector3){px, py + time * 1.5f, pz});
    float ny = Wind_NoiseScalar3D((Vector3){px + 17.3f, py - time * 1.2f, pz + 19.7f});
    float nz = Wind_NoiseScalar3D((Vector3){px - 23.5f, py, pz + time * 1.6f});
    guideVel.x += nx * 0.8f * corridorWeight;
    guideVel.y += ny * 0.4f * corridorWeight;
    guideVel.z += nz * 0.8f * corridorWeight;

    if (s_terrainQuery != NULL && Wind_GroundHeight(ground, pos.x, pos.z))
    {
        const float h0 = ground->h;
        const float sampleDist = 2.4f;
        float h1 = 0.0f;
        if (Wind_SampleTerrain(pos.x + gDir.x * sampleDist, pos.z + gDir.z * sampleDist, &h1) && h1 > h0)
        {
            float dH = h1 - h0;
            float slopeLift = (dH * (1.0f / 2.4f)) * gSpeed * 0.45f * corridorWeight;
            if (slopeLift > 6.0f)
                slopeLift = 6.0f;
            guideVel.y += slopeLift;
        }
        const float hSafe = 1.2f;
        if (pos.y < h0 + hSafe)
        {
            float safePenetration = (h0 + hSafe - pos.y);
            guideVel.y += fminf(safePenetration * 3.0f * corridorWeight, 8.0f);
        }
    }

    *totalVel = Vector3Add(*totalVel, guideVel);
}

static inline bool Wind_InBounds(Vector3 p, Vector3 lo, Vector3 hi)
{
    return p.x >= lo.x && p.x <= hi.x &&
           p.y >= lo.y && p.y <= hi.y &&
           p.z >= lo.z && p.z <= hi.z;
}

typedef struct
{
    Vector3 pos;
    float time;
    Vector3 vel;             // totalVel sau bước 3
    WindGroundSample ground; // trạng thái lấy mẫu địa hình sau bước 3 (bước 5 có thể lấy mẫu tiếp)
} WindBaseMemo;
static WindBaseMemo s_baseMemo;

static Vector3 Wind_EvaluateVelocityChannels(Vector3 pos, float time, bool includeMotion)
{
    if (!s_initialized)
        return (Vector3){0};

    if (includeMotion)
        s_stats.evalFull++;
    else
        s_stats.evalBackground++;

    Vector3 totalVel;
    WindGroundSample ground = {0};

    if (s_baseMemoValid &&
        memcmp(&s_baseMemo.pos, &pos, sizeof(pos)) == 0 &&
        memcmp(&s_baseMemo.time, &time, sizeof(time)) == 0)
    {
        totalVel = s_baseMemo.vel;
        ground = s_baseMemo.ground;
        s_stats.memoHits++;
    }
    else
    {
        // 1. Thành phần gió vĩ mô (Macro Wind)
        totalVel = Wind_GetMacroAt(pos, time);

        const bool hasTerrain = (s_terrainQuery != NULL);

        // 2. Điều biến vận tốc theo cao độ (Atmospheric Boundary Layer Height Gradient)
        if (hasTerrain && s_macroConfig.heightGradientK > 0.0f)
        {
            if (Wind_GroundHeight(&ground, pos.x, pos.z))
            {
                float hRel = pos.y - ground.h;
                float factor = 1.0f + s_macroConfig.heightGradientK * (Wind_HeightFactor(hRel) - 1.0f);
                totalVel = Vector3Scale(totalVel, factor);
            }
        }

        // 3. Thành phần nâng địa hình (Terrain-Aware Lift)
        if (hasTerrain && s_macroConfig.terrainLiftK > 0.0f)
        {
            float speedXZSq = totalVel.x * totalVel.x + totalVel.z * totalVel.z;
            if (speedXZSq > 1e-6f && Wind_GroundHeight(&ground, pos.x, pos.z))
            {
                float speedXZ = sqrtf(speedXZSq);
                float invSpeedXZ = 1.0f / speedXZ;
                float dirX = totalVel.x * invSpeedXZ;
                float dirZ = totalVel.z * invSpeedXZ;

                const float sampleDist = 1.5f;
                float h1 = 0.0f;
                if (Wind_SampleTerrain(pos.x + dirX * sampleDist,
                                       pos.z + dirZ * sampleDist, &h1))
                {
                    float dH = h1 - ground.h;
                    if (dH > 0.0f)
                    {
                        float lift = (dH * (1.0f / 1.5f)) * speedXZ * s_macroConfig.terrainLiftK;
                        if (lift > 12.0f)
                            lift = 12.0f;
                        totalVel.y += lift;
                    }
                }
            }
        }

        s_baseMemo.pos = pos;
        s_baseMemo.time = time;
        s_baseMemo.vel = totalVel;
        s_baseMemo.ground = ground;
        s_baseMemoValid = true;
    } // hết nhánh tính lại bước 1-3

    // 4. Tổng hợp từ mảng Vorticles cục bộ với lọc nhanh theo AABB tổng
    Vector3 c;
    if (includeMotion && s_hasActiveMotion &&
        Wind_InBounds(pos, s_motionBoundsMin, s_motionBoundsMax))
    {
        for (int i = 0; i < s_motionAirflowCount; ++i)
        {
            if (Wind_VorticleCompiledVelocity(&s_motionCompiled[i], pos, time, &c))
            {
                totalVel.x += c.x;
                totalVel.y += c.y;
                totalVel.z += c.z;
            }
        }
    }
    if (s_hasActiveOrdinary &&
        Wind_InBounds(pos, s_ordinaryBoundsMin, s_ordinaryBoundsMax))
    {
        const int ordinaryCount = Wind_VisibleOrdinaryCount();
        for (int i = 0; i < ordinaryCount; i++)
        {
            if (Wind_VorticleCompiledVelocity(&s_vorticlesCompiled[i], pos, time, &c))
            {
                totalVel.x += c.x;
                totalVel.y += c.y;
                totalVel.z += c.z;
            }
        }
    }

    // 5. Đợt Gió Dẫn Đường toàn cục
    if (s_guidingGust.active && s_guidingGust.intensity > 0.001f)
        Wind_AddGuidingWind(pos, time, &ground, &totalVel);

    return totalVel;
}

Vector3 Wind_EvaluateVelocity(Vector3 pos, float time)
{
    return Wind_EvaluateVelocityChannels(pos, time, true);
}

Vector3 Wind_EvaluateBackgroundVelocity(Vector3 pos, float time)
{
    return Wind_EvaluateVelocityChannels(pos, time, false);
}

MediumProperties Wind_EvaluateBackgroundMedium(Vector3 pos, float time,
                                               Vector3 gravityMps2)
{
    return (MediumProperties){.densityKgM3 = 1.225f,
                              .dynamicViscosityPaS = 1.81e-5f,
                              .velocityMps = Wind_EvaluateBackgroundVelocity(pos, time),
                              .gravityMps2 = gravityMps2};
}

Vector3 Wind_EvaluateAcceleration(Vector3 pos, float time, Vector3 currentVel)
{
    Vector3 targetWindVel = Wind_EvaluateVelocity(pos, time);
    const float dragCoeff = 3.5f;
    Vector3 relVel = Vector3Subtract(targetWindVel, currentVel);
    return Vector3Scale(relVel, dragCoeff);
}

void Wind_SetMotionAirflow(const VorticleData *sources, int count)
{
    if (!sources || count < 0)
        count = 0;
    if (count > WIND_MAX_MOTION_AIRFLOW)
        count = WIND_MAX_MOTION_AIRFLOW;
    if (count > 0)
        memmove(s_motionAirflow, sources, (size_t)count * sizeof(*sources));
    s_motionAirflowCount = count;
    s_combinedValid = false;

    Wind_RebuildMotionCompiled();
    if (Wind_TrimOrdinary(Wind_OrdinaryCapacity()))
    {
        Wind_RebuildOrdinaryCompiled(Wind_VisibleOrdinaryCount());
    }
}

const VorticleData *Wind_GetActiveVorticles(int *outCount)
{
    if (!s_combinedValid)
    {
        const int ordinaryCount = Wind_VisibleOrdinaryCount();
        if (s_motionAirflowCount > 0)
            memcpy(s_combinedAirflow, s_motionAirflow,
                   (size_t)s_motionAirflowCount * sizeof(VorticleData));
        if (ordinaryCount > 0)
            memcpy(s_combinedAirflow + s_motionAirflowCount, s_vorticles,
                   (size_t)ordinaryCount * sizeof(VorticleData));
        s_combinedCount = ordinaryCount + s_motionAirflowCount;
        s_combinedValid = true;
    }
    if (outCount)
        *outCount = s_combinedCount;
    return s_combinedAirflow;
}

int Wind_GetActiveCount(void)
{
    return Wind_VisibleOrdinaryCount() + s_motionAirflowCount;
}

// -----------------------------------------------------------------------------
// Guiding Wind Implementation
// -----------------------------------------------------------------------------
void Wind_TriggerGuidingWind(Vector3 playerPos, Vector3 targetPos, float speed, float duration)
{
    if (!s_initialized)
        return;
    if (!Wind_IsFiniteVec3(playerPos) || !Wind_IsFiniteVec3(targetPos))
        return;

    Vector3 diff = Vector3Subtract(targetPos, playerPos);
    diff.y = 0.0f;
    float len = sqrtf(diff.x * diff.x + diff.z * diff.z);
    if (!isfinite(len))
        len = 0.0f;
    Vector3 dir = (len > 1e-4f)
                      ? Vector3Scale(diff, 1.0f / len)
                      : (Vector3){0.0f, 0.0f, 1.0f};

    float spd = (speed > 0.0f && isfinite(speed)) ? speed : 16.0f;
    float dur = (duration > 0.0f && isfinite(duration)) ? duration : 2.2f;

    s_guidingGust.active = true;
    s_guidingGust.playerPos = playerPos;
    s_guidingGust.targetPos = targetPos;
    s_guidingGust.direction = dir;
    s_guidingGust.speed = spd;
    s_guidingGust.duration = dur;
    s_guidingGust.elapsed = 0.0f;
    s_guidingGust.progress = 0.0f;
    s_guidingGust.intensity = 0.0f;

    s_guidingTotalDist = (len >= 1.0f) ? len : 24.0f;

    float corridorStep = 6.5f;
    int steps = (int)(len / corridorStep);
    if (steps > 4)
        steps = 4;
    if (steps < 2)
        steps = 2;

    for (int i = 0; i < steps; i++)
    {
        Vector3 p = Vector3Add(playerPos, Vector3Scale(dir, (float)i * corridorStep + 1.0f));
        Wind_SpawnGust(p, dir, 3.8f, spd * 0.6f, dur * 0.75f);
        Wind_SpawnTurbulence(p, 3.2f, spd * 0.25f, 0.22f, 2.0f, dur * 0.65f);
    }
}

void Wind_StopGuidingWind(void)
{
    s_guidingGust.active = false;
    s_guidingGust.intensity = 0.0f;
    s_guidingGust.progress = 1.0f;
}

bool Wind_IsGuidingWindActive(void)
{
    return s_guidingGust.active;
}

float Wind_GetGuidingWindProgress(void)
{
    return s_guidingGust.progress;
}

WindGuidingGust Wind_GetGuidingWindState(void)
{
    return s_guidingGust;
}

void Wind_AddDisplacement(Vector3 pos, Vector3 velocity, float radius, float duration)
{
    if (!s_initialized)
        return;
    if (!Wind_IsFiniteVec3(velocity))
        return;
    float spd = Vector3Length(velocity);
    if (!isfinite(spd) || spd < 0.01f || radius <= 0.0f || duration <= 0.0f)
        return;
    Vector3 dir = Vector3Scale(velocity, 1.0f / spd);
    Wind_SpawnGust(pos, dir, radius, spd, duration);
}

// -----------------------------------------------------------------------------
// Debug draw
// -----------------------------------------------------------------------------
#if !defined(CORE_HEADLESS_TEST) && !defined(HEADLESS_TEST)

#define WIND_DEBUG_RING_SEGMENTS 16
#define WIND_DEBUG_MAX_VORTICLES 96
#define WIND_DEBUG_RANGE_M 96.0f

static unsigned char Wind_DebugAlpha(float base, float t)
{
    float a = base * t;
    if (!(a > 0.0f))
        return 0;
    if (a > 255.0f)
        a = 255.0f;
    return (unsigned char)a;
}

static void Wind_DebugRing(Vector3 c, Vector3 u, Vector3 w, float r, Color col)
{
    static float s_cos[WIND_DEBUG_RING_SEGMENTS + 1];
    static float s_sin[WIND_DEBUG_RING_SEGMENTS + 1];
    static bool s_tableReady = false;
    if (!s_tableReady)
    {
        for (int i = 0; i <= WIND_DEBUG_RING_SEGMENTS; ++i)
        {
            float a = 6.28318530718f * (float)i / (float)WIND_DEBUG_RING_SEGMENTS;
            s_cos[i] = cosf(a);
            s_sin[i] = sinf(a);
        }
        s_tableReady = true;
    }
    Vector3 prev = {c.x + u.x * r, c.y + u.y * r, c.z + u.z * r};
    for (int i = 1; i <= WIND_DEBUG_RING_SEGMENTS; ++i)
    {
        Vector3 p = {
            c.x + (u.x * s_cos[i] + w.x * s_sin[i]) * r,
            c.y + (u.y * s_cos[i] + w.y * s_sin[i]) * r,
            c.z + (u.z * s_cos[i] + w.z * s_sin[i]) * r,
        };
        DrawLine3D(prev, p, col);
        prev = p;
    }
}

static void Wind_DebugSphere(Vector3 c, float r, Color col)
{
    const Vector3 ax = {1, 0, 0}, ay = {0, 1, 0}, az = {0, 0, 1};
    Wind_DebugRing(c, ax, ay, r, col);
    Wind_DebugRing(c, ax, az, r, col);
    Wind_DebugRing(c, ay, az, r, col);
}

static void Wind_DebugDrawVorticle(const VorticleData *v)
{
    float lifeNorm = (v->maxLifetime > 1e-4f) ? (v->lifetime / v->maxLifetime) : 1.0f;
    if (!(lifeNorm > 0.0f))
        return;
    if (lifeNorm > 1.0f)
        lifeNorm = 1.0f;

    switch (v->type)
    {
    case VORTICLE_LINEAR_GUST:
    {
        Vector3 gustEnd = Vector3Add(v->position, Vector3Scale(v->direction, v->radius * 0.8f));
        DrawLine3D(v->position, gustEnd, (Color){0, 255, 200, Wind_DebugAlpha(255.0f, lifeNorm)});
        Wind_DebugSphere(v->position, v->radius * 0.4f,
                         (Color){0, 200, 255, Wind_DebugAlpha(140.0f, lifeNorm)});
        break;
    }
    case VORTICLE_RADIAL_BLAST:
    {
        float curR = v->radius * (1.0f - lifeNorm * 0.4f);
        Wind_DebugSphere(v->position, curR,
                         (Color){255, 220, 40, Wind_DebugAlpha(200.0f, lifeNorm)});
        break;
    }
    case VORTICLE_VORTEX:
    {
        Vector3 axis = Wind_NormalizeOr(v->direction, (Vector3){0, 1, 0});
        Vector3 helper = (fabsf(axis.y) < 0.99f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
        Vector3 u = Vector3Normalize(Vector3CrossProduct(axis, helper));
        Vector3 w = Vector3CrossProduct(axis, u);
        float h = v->radius * 0.6f;
        Vector3 bottom = Vector3Subtract(v->position, Vector3Scale(axis, h));
        Vector3 top = Vector3Add(v->position, Vector3Scale(axis, h));
        Color col = (Color){255, 120, 20, Wind_DebugAlpha(180.0f, lifeNorm)};
        Wind_DebugRing(bottom, u, w, v->radius, col);
        Wind_DebugRing(top, u, w, v->radius, col);
        DrawLine3D(bottom, top, col);
        break;
    }
    case VORTICLE_TURBULENCE:
        Wind_DebugSphere(v->position, v->radius,
                         (Color){200, 120, 255, Wind_DebugAlpha(140.0f, lifeNorm)});
        break;
    }
}

static inline bool Wind_DebugInRange(const VorticleData *v, Vector3 anchor)
{
    float dx = v->position.x - anchor.x;
    float dy = v->position.y - anchor.y;
    float dz = v->position.z - anchor.z;
    return (dx * dx + dy * dy + dz * dz) <= (WIND_DEBUG_RANGE_M * WIND_DEBUG_RANGE_M);
}

void Wind_DrawDebug(Vector3 anchorPos)
{
    if (!s_initialized)
        return;

    Vector3 macroVel = Wind_GetMacroAt(anchorPos, (float)GetTime());
    Vector3 arrowStart = Vector3Add(anchorPos, (Vector3){0.0f, 2.2f, 0.0f});
    Vector3 arrowEnd = Vector3Add(arrowStart, Vector3Scale(macroVel, 1.2f));
    const Color arrowCol = (Color){0, 240, 255, 255};

    DrawLine3D(arrowStart, arrowEnd, arrowCol);
    {
        const float m = 0.10f;
        DrawLine3D((Vector3){arrowEnd.x - m, arrowEnd.y, arrowEnd.z}, (Vector3){arrowEnd.x + m, arrowEnd.y, arrowEnd.z}, arrowCol);
        DrawLine3D((Vector3){arrowEnd.x, arrowEnd.y - m, arrowEnd.z}, (Vector3){arrowEnd.x, arrowEnd.y + m, arrowEnd.z}, arrowCol);
        DrawLine3D((Vector3){arrowEnd.x, arrowEnd.y, arrowEnd.z - m}, (Vector3){arrowEnd.x, arrowEnd.y, arrowEnd.z + m}, arrowCol);
    }

    int drawn = 0;
    for (int i = 0; i < s_motionAirflowCount && drawn < WIND_DEBUG_MAX_VORTICLES; ++i)
    {
        const VorticleData *v = &s_motionAirflow[i];
        if (!v->active || !Wind_DebugInRange(v, anchorPos))
            continue;
        Wind_DebugDrawVorticle(v);
        drawn++;
    }
    const int ordinaryCount = Wind_VisibleOrdinaryCount();
    for (int i = 0; i < ordinaryCount && drawn < WIND_DEBUG_MAX_VORTICLES; ++i)
    {
        const VorticleData *v = &s_vorticles[i];
        if (!Wind_DebugInRange(v, anchorPos))
            continue;
        Wind_DebugDrawVorticle(v);
        drawn++;
    }
}
#else
void Wind_DrawDebug(Vector3 anchorPos)
{
    (void)anchorPos;
}
#endif
#include "core/wind/wind_system.h"
#include "core/force_field.h"
#include <math.h>
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

static VorticleData s_vorticles[MAX_VORTICLES];
static int s_activeCount = 0;
static VorticleData s_motionAirflow[WIND_MAX_MOTION_AIRFLOW];
static int s_motionAirflowCount = 0;

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
static unsigned int s_terrainVersionCounter = 0; // đơn điệu tăng, KHÔNG reset ở Init/Unload
static bool s_initialized = false;

// -----------------------------------------------------------------------------
// Helpers an toàn số học
// -----------------------------------------------------------------------------
static inline bool Wind_IsFiniteVec3(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static inline Vector3 Wind_NormalizeOr(Vector3 v, Vector3 fallback)
{
    float len = Vector3Length(v);
    if (isfinite(len) && len > 1e-4f)
        return Vector3Scale(v, 1.0f / len);
    return fallback;
}

// Version phải khác nhau qua MỌI lần Init/Unload/Rebuild, vì GPU packer cache theo version.
// Reset về 1 ở Init làm version trùng với lần upload cũ -> GPU giữ terrain của map trước.
static unsigned int Wind_NextTerrainVersion(void)
{
    s_terrainVersionCounter++;
    if (s_terrainVersionCounter == 0u)
        s_terrainVersionCounter = 1u; // bỏ qua 0 khi wrap
    return s_terrainVersionCounter;
}

// This hash-gradient noise is mirrored formula-for-formula in particle_gpu.comp.
// Keeping Wind's CPU evaluator on the same field prevents the CPU collision /
// event shadow simulation from drifting away from GPU particle trajectories.
static inline float Wind_Fract(float v)
{
    return v - floorf(v);
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

static inline Vector3 Wind_Hash3(Vector3 p)
{
    p.x = Wind_Fract(p.x * 0.1031f);
    p.y = Wind_Fract(p.y * 0.1030f);
    p.z = Wind_Fract(p.z * 0.0973f);
    float d = p.x * (p.y + 33.33f) +
              p.y * (p.x + 33.33f) +
              p.z * (p.z + 33.33f);
    p.x += d;
    p.y += d;
    p.z += d;
    return (Vector3){
        Wind_Fract((p.x + p.y) * p.z) * 2.0f - 1.0f,
        Wind_Fract((p.x + p.x) * p.y) * 2.0f - 1.0f,
        Wind_Fract((p.y + p.x) * p.x) * 2.0f - 1.0f,
    };
}

static float Wind_NoiseScalar3D(Vector3 p)
{
    Vector3 lattice = {floorf(p.x), floorf(p.y), floorf(p.z)};
    Vector3 f = {Wind_Fract(p.x), Wind_Fract(p.y), Wind_Fract(p.z)};
    Vector3 u = {
        f.x * f.x * (3.0f - 2.0f * f.x),
        f.y * f.y * (3.0f - 2.0f * f.y),
        f.z * f.z * (3.0f - 2.0f * f.z),
    };

    Vector3 g000 = Wind_Hash3(lattice);
    Vector3 g100 = Wind_Hash3((Vector3){lattice.x + 1.0f, lattice.y, lattice.z});
    Vector3 g010 = Wind_Hash3((Vector3){lattice.x, lattice.y + 1.0f, lattice.z});
    Vector3 g110 = Wind_Hash3((Vector3){lattice.x + 1.0f, lattice.y + 1.0f, lattice.z});
    Vector3 g001 = Wind_Hash3((Vector3){lattice.x, lattice.y, lattice.z + 1.0f});
    Vector3 g101 = Wind_Hash3((Vector3){lattice.x + 1.0f, lattice.y, lattice.z + 1.0f});
    Vector3 g011 = Wind_Hash3((Vector3){lattice.x, lattice.y + 1.0f, lattice.z + 1.0f});
    Vector3 g111 = Wind_Hash3((Vector3){lattice.x + 1.0f, lattice.y + 1.0f, lattice.z + 1.0f});

    float n000 = g000.x * f.x + g000.y * f.y + g000.z * f.z;
    float n100 = g100.x * (f.x - 1.0f) + g100.y * f.y + g100.z * f.z;
    float n010 = g010.x * f.x + g010.y * (f.y - 1.0f) + g010.z * f.z;
    float n110 = g110.x * (f.x - 1.0f) + g110.y * (f.y - 1.0f) + g110.z * f.z;
    float n001 = g001.x * f.x + g001.y * f.y + g001.z * (f.z - 1.0f);
    float n101 = g101.x * (f.x - 1.0f) + g101.y * f.y + g101.z * (f.z - 1.0f);
    float n011 = g011.x * f.x + g011.y * (f.y - 1.0f) + g011.z * (f.z - 1.0f);
    float n111 = g111.x * (f.x - 1.0f) + g111.y * (f.y - 1.0f) + g111.z * (f.z - 1.0f);

    float nx00 = n000 + (n100 - n000) * u.x;
    float nx10 = n010 + (n110 - n010) * u.x;
    float nx01 = n001 + (n101 - n001) * u.x;
    float nx11 = n011 + (n111 - n011) * u.x;
    float nxy0 = nx00 + (nx10 - nx00) * u.y;
    float nxy1 = nx01 + (nx11 - nx01) * u.y;
    return nxy0 + (nxy1 - nxy0) * u.z;
}

// -----------------------------------------------------------------------------
// Pool: dung lượng khả dụng, loại bỏ, cấp phát
// -----------------------------------------------------------------------------
// Motion airflow luôn đứng trước trong snapshot nên pool thường chỉ còn phần dư.
static inline int Wind_OrdinaryCapacity(void)
{
    return MAX_VORTICLES - s_motionAirflowCount; // >= MAX_VORTICLES - 144 > 0
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

// Bỏ các Vorticle sắp hết hạn nhất cho tới khi còn <= limit. Thường là no-op.
static void Wind_TrimOrdinary(int limit)
{
    if (limit < 0)
        limit = 0;
    while (s_activeCount > limit)
    {
        int victim = Wind_FindShortestLife();
        s_vorticles[victim] = s_vorticles[s_activeCount - 1];
        s_activeCount--;
        s_combinedValid = false;
    }
}

static int AllocVorticleSlot(void)
{
    const int cap = Wind_OrdinaryCapacity();
    s_combinedValid = false;
    if (s_activeCount < cap)
        return s_activeCount++;

    // Pool đầy: ghi đè phần tử có thời gian sống còn lại ngắn nhất (giữ nguyên chính sách cũ).
    Wind_TrimOrdinary(cap); // motion airflow vừa tăng thì thu pool về đúng cap trước
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
    s_activeCount = 0;
    s_motionAirflowCount = 0;
    s_combinedCount = 0;
    s_combinedValid = false;
    memset(&s_guidingGust, 0, sizeof(s_guidingGust));
    s_guidingTotalDist = 0.0f;
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainGrid.version = Wind_NextTerrainVersion();

    // Strong coherent gusts over roughly 7 m noise cells; no extra evaluations.
    s_macroConfig = (WindMacroConfig){
        .baseDirection = (Vector3){1.5f, 0.0f, 0.8f},
        .gustAmplitude = 6.0f,
        .noiseScale = 0.14f,
        .noiseSpeed = 1.0f,
        .terrainLiftK = 1.2f,
        .heightGradientK = 1.0f,
    };
    Wind_RefreshMacroCache();
    s_initialized = true;
}

void Wind_Clear(void)
{
    if (s_activeCount > 0)
        memset(s_vorticles, 0, (size_t)s_activeCount * sizeof(VorticleData));
    s_activeCount = 0;
    s_motionAirflowCount = 0;
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
    s_terrainGrid.version = Wind_NextTerrainVersion(); // khác mọi version GPU đã thấy
    memset(&s_guidingGust, 0, sizeof(s_guidingGust));
    s_guidingTotalDist = 0.0f;
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
    // Một giá trị NaN/Inf ở đây làm hỏng vận tốc của MỌI receiver (CPU lẫn GPU),
    // nên cấu hình không hữu hạn bị bỏ qua và giữ cấu hình hiện tại.
    if (!cfg || !Wind_MacroConfigFinite(cfg))
        return;
    s_macroConfig = *cfg;
    Wind_RefreshMacroCache();
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
    s_terrainGrid.built = false;
    s_terrainGrid.active = false;
    s_terrainGrid.version = Wind_NextTerrainVersion();
}

void Wind_RebuildTerrainGrid(Vector2 centerXZ, Vector2 halfExtentXZ)
{
    unsigned int nextVersion = Wind_NextTerrainVersion();
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
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

    int validCount = 0;
    for (int z = 0; z < WIND_TERRAIN_GRID_SIZE; ++z)
    {
        for (int x = 0; x < WIND_TERRAIN_GRID_SIZE; ++x)
        {
            int index = z * WIND_TERRAIN_GRID_SIZE + x;
            float worldX = s_terrainGrid.originXZ.x +
                           (float)x * s_terrainGrid.cellSizeXZ.x;
            float worldZ = s_terrainGrid.originXZ.y +
                           (float)z * s_terrainGrid.cellSizeXZ.y;
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

    float gx = (worldX - s_terrainGrid.originXZ.x) / s_terrainGrid.cellSizeXZ.x;
    float gz = (worldZ - s_terrainGrid.originXZ.y) / s_terrainGrid.cellSizeXZ.y;
    const float gridMax = (float)(WIND_TERRAIN_GRID_SIZE - 1);
    // Dạng "!(in range)" để NaN bị loại; (int)NaN là undefined behavior.
    if (!(gx >= 0.0f && gx <= gridMax && gz >= 0.0f && gz <= gridMax))
        return false;

    // gx, gz >= 0 nên cắt về int tương đương floorf, bỏ được hai lời gọi libm.
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

// Cao độ mặt đất tại (x, z) của một lần evaluate. Macro-height, terrain lift và
// guiding corridor cùng hỏi đúng một điểm; trước đây mỗi nơi tự nội suy lại
// (và nếu chưa bake grid thì gọi callback của game 2-3 lần mỗi receiver).
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
void Wind_Update(float dt)
{
    if (!s_initialized)
        return;
    // dt NaN làm lifetime = NaN -> Vorticle bất tử + vận tốc NaN; dt < 0 làm gió "sống lại".
    // dt == 0 (pause) vốn không đổi gì nên bỏ qua luôn.
    if (!(dt > 0.0f))
        return;

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
            // Phong bì khí động học: bùng nổ cực nhanh (attack), duy trì tốc độ (sustain),
            // và hạ dần khi phân rã ở phía xa (decay)
            float p = s_guidingGust.progress;
            float attack = fminf(p / 0.15f, 1.0f);
            float decay = (p > 0.60f) ? (1.0f - (p - 0.60f) / 0.40f) : 1.0f;
            s_guidingGust.intensity = attack * decay;
        }
    }

    if (s_activeCount <= 0)
        return;

    // 2+3. Trừ thời gian sống và dồn mảng liên tục trong MỘT lượt duyệt
    // (trước đây hai lượt riêng) để query sau đó duyệt tuần tự, không có lỗ.
    const int count = s_activeCount;
    int writeIdx = 0;
    for (int readIdx = 0; readIdx < count; readIdx++)
    {
        const VorticleData *src = &s_vorticles[readIdx];
        const float life = src->lifetime - dt;
        if (!src->active || !(life > 0.0f))
            continue; // hết hạn (hoặc lifetime NaN)
        if (writeIdx != readIdx)
            s_vorticles[writeIdx] = *src;
        s_vorticles[writeIdx].lifetime = life;
        writeIdx++;
    }
    s_activeCount = writeIdx;
    s_combinedValid = false;

    // Motion airflow có thể đã tăng kể từ lần Update trước.
    Wind_TrimOrdinary(Wind_OrdinaryCapacity());
}

// -----------------------------------------------------------------------------
// Emitters
// -----------------------------------------------------------------------------
// Tham số không hữu hạn bị từ chối ngay ở cửa: vị trí NaN làm các phép so sánh AABB
// luôn "pass" và đầu độc vận tốc cả field; duration = +Inf cho lifetime/maxLifetime = NaN.
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
    const int idx = AllocVorticleSlot();
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

    // direction.z carries a short attack envelope. This gives an opening
    // pressure blast a readable beat before the stronger turbulent wake.
    float attackTime = fminf(0.20f, duration * 0.25f);
    return Wind_Emit(VORTICLE_TURBULENCE, pos, (Vector3){noiseScale, noiseSpeed, attackTime},
                     radius, strength, 0.0f, duration);
}

// -----------------------------------------------------------------------------
// Evaluators
// -----------------------------------------------------------------------------
Vector3 Wind_GetMacroAt(Vector3 pos, float time)
{
    if (!s_initialized)
        return (Vector3){0};

    if (s_macroBaseLen < 1e-4f)
        return (Vector3){0};

    if (s_macroConfig.gustAmplitude <= 1e-4f)
    {
        return s_macroConfig.baseDirection;
    }

    // 3D hash-gradient noise đa chiều mô phỏng các xoáy loạn lưu chất lưu.
    float s = s_macroConfig.noiseScale;
    float spd = s_macroConfig.noiseSpeed;
    float nx = Wind_NoiseScalar3D((Vector3){pos.x * s - time * spd, pos.y * s + 17.3f, pos.z * s - time * spd * 0.7f});
    float ny = Wind_NoiseScalar3D((Vector3){pos.x * s + 37.1f, pos.y * s - time * spd * 0.8f, pos.z * s + 19.7f});
    float nz = Wind_NoiseScalar3D((Vector3){pos.x * s - time * spd * 0.6f, pos.y * s + 53.9f, pos.z * s + time * spd * 0.5f});

    const float amp = s_macroAmp;
    Vector3 turb = {
        s_macroConfig.baseDirection.x * (1.0f + s_macroConfig.gustAmplitude * nx * 0.5f) + nx * amp * 0.5f,
        s_macroConfig.baseDirection.y + ny * amp * 0.35f,
        s_macroConfig.baseDirection.z * (1.0f + s_macroConfig.gustAmplitude * nz * 0.5f) + nz * amp * 0.5f};
    return turb;
}

float Wind_HeightFactor(float hRel)
{
    // Mô hình Lớp biên Khí quyển (Atmospheric Boundary Layer):
    // hRel <= 0m (mặt đất): ma sát địa hình giảm tốc còn 50%
    // hRel = 2.5m (người chơi / tán cây thấp): đạt 100% tốc độ danh nghĩa
    // hRel >= 12.0m (trời cao / đỉnh đồi): dòng khí tự do tăng lên 140%
    if (hRel <= 0.0f)
        return 0.5f;
    float low = fminf(hRel / 2.5f, 1.0f);
    float high = (hRel > 2.5f) ? fminf((hRel - 2.5f) / 9.5f, 1.0f) : 0.0f;
    return 0.5f + 0.5f * low + 0.4f * high;
}

// Lõi tính một Vorticle. Trả về false khi không đóng góp (ngoài vùng ảnh hưởng, không active,
// kiểu lạ); khi đó *out không đổi. `static inline` để vòng lặp nóng không trả phí gọi hàm
// (hàm public không inline được khi build shared library có -fPIC).
// Mọi so sánh viết dạng "!(a < b)" nên NaN bị loại thay vì lọt qua.
static inline bool Wind_VorticleVelocity(const VorticleData *v, Vector3 pos, float time, Vector3 *out)
{
    if (!v->active)
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
    const float rSq = radius * radius;
    if (!(distSq < rSq) || rSq < 1e-6f)
        return false;

    const Vector3 delta = {dx, dy, dz};
    const float dist = sqrtf(distSq);
    const float spatialAtten = 1.0f - (dist / radius);
    const float temporalAtten = (v->maxLifetime > 1e-4f)
                                    ? (v->lifetime / v->maxLifetime)
                                    : 1.0f;
    const float weight = spatialAtten * temporalAtten;

    Vector3 velocity = {0};
    switch (v->type)
    {
    case VORTICLE_LINEAR_GUST:
        velocity = Vector3Scale(v->direction, v->strength * weight);
        break;

    case VORTICLE_RADIAL_BLAST:
    {
        Vector3 normOut = (dist > 1e-4f)
                              ? Vector3Scale(delta, 1.0f / dist)
                              : (Vector3){0.0f, 1.0f, 0.0f};
        velocity = Vector3Scale(normOut, v->strength * weight);
        break;
    }

    case VORTICLE_VORTEX:
    {
        Vector3 tangent = Vector3CrossProduct(v->direction, delta);
        float tanLen = Vector3Length(tangent);
        float rCore = 0.10f * radius;
        float coreFactor = (tanLen < rCore) ? (tanLen / rCore) : 1.0f;
        if (tanLen > 1e-4f)
        {
            Vector3 tanDir = Vector3Scale(tangent, 1.0f / tanLen);
            velocity = Vector3Scale(tanDir, v->strength * weight * coreFactor);
        }
        if (fabsf(v->inwardPull) > 1e-4f)
        {
            float proj = Vector3DotProduct(delta, v->direction);
            Vector3 closestOnAxis = Vector3Scale(v->direction, proj);
            Vector3 radial = Vector3Subtract(delta, closestOnAxis);
            float radDist = Vector3Length(radial);
            if (radDist > 1e-4f)
            {
                Vector3 radDir = Vector3Scale(radial, 1.0f / radDist);
                Vector3 pullVel = Vector3Scale(radDir,
                                               -v->inwardPull * weight * coreFactor);
                velocity = Vector3Add(velocity, pullVel);
            }
        }
        break;
    }

    case VORTICLE_TURBULENCE:
    {
        const float strength = v->strength * weight *
                               Wind_TurbulenceAttackWeight(v);
        // Frame đầu (attack = 0) hoặc strength = 0: 3 lần noise (24 hash) cho kết quả 0.
        if (strength == 0.0f)
            return false;
        const float ns = v->direction.x;
        const float spd = v->direction.y;
        const float px = pos.x * ns;
        const float py = pos.y * ns;
        const float pz = pos.z * ns;
        const float t = time * spd;
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

Vector3 Wind_EvaluateVorticleVelocity(const VorticleData *vorticle, Vector3 pos, float time)
{
    Vector3 velocity = {0};
    if (vorticle != NULL)
        (void)Wind_VorticleVelocity(vorticle, pos, time, &velocity);
    return velocity;
}

// Đóng góp của đợt Gió Dẫn Đường toàn cục (Ghost of Tsushima Windicator Engine).
// Chỉ tác động tập trung trong hành lang dòng khí dọc theo vệt gió, không kéo toàn bộ hạt trên màn hình.
static void Wind_AddGuidingWind(Vector3 pos, float time, WindGroundSample *ground, Vector3 *totalVel)
{
    const float intensity = s_guidingGust.intensity;
    const Vector3 gDir = s_guidingGust.direction;
    const float gSpeed = s_guidingGust.speed;

    // Tọa độ tương đối so với vị trí xuất phát đợt gió
    Vector3 toPos = Vector3Subtract(pos, s_guidingGust.playerPos);
    float dLong = toPos.x * gDir.x + toPos.z * gDir.z; // Khoảng cách dọc trục gió

    // Giới hạn phạm vi dọc hành lang: từ sau lưng người chơi 1.5m tới quá đích 3.0m
    if (!(dLong >= -1.5f && dLong <= s_guidingTotalDist + 3.0f))
        return;

    // Khoảng cách vuông góc tới trục tâm vệt gió (transverse distance)
    float perpX = toPos.x - gDir.x * dLong;
    float perpZ = toPos.z - gDir.z * dLong;
    float dPerpSq = perpX * perpX + perpZ * perpZ;

    const float corridorRadius = 3.8f; // Bán kính hành lang gió tập trung
    if (!(dPerpSq < corridorRadius * corridorRadius))
        return;

    float dPerp = sqrtf(dPerpSq);
    float lateralAtten = 1.0f - (dPerp / corridorRadius);
    lateralAtten = lateralAtten * lateralAtten; // Suy giảm bậc 2 êm dịu

    // Giới hạn chiều cao: trong khoảng 2.8m quanh độ cao xuất phát
    float dY = fabsf(pos.y - s_guidingGust.playerPos.y);
    float vertAtten = (dY < 2.8f) ? (1.0f - dY / 2.8f) : 0.0f;

    float corridorWeight = lateralAtten * vertAtten * intensity;
    if (!(corridorWeight > 0.001f))
        return;

    // Two-layer Perlin modulation
    // Tầng thấp (N_low): Sóng cuộn trôi dọc theo hướng gió chính
    float waveLow = sinf(dLong * 0.20f - time * 3.5f) * 0.22f;
    float modulatedSpeed = gSpeed * (1.0f + waveLow) * corridorWeight;
    Vector3 guideVel = Vector3Scale(gDir, modulatedSpeed);

    // Tầng cao (N_high): Vi mô nhiễu loạn 3D
    float nx = Wind_NoiseScalar3D((Vector3){pos.x * 0.35f, pos.y * 0.35f + time * 1.5f, pos.z * 0.35f});
    float ny = Wind_NoiseScalar3D((Vector3){pos.x * 0.35f + 17.3f, pos.y * 0.35f - time * 1.2f, pos.z * 0.35f + 19.7f});
    float nz = Wind_NoiseScalar3D((Vector3){pos.x * 0.35f - 23.5f, pos.y * 0.35f, pos.z * 0.35f + time * 1.6f});
    Vector3 flutter = (Vector3){nx * 0.8f * corridorWeight, ny * 0.4f * corridorWeight, nz * 0.8f * corridorWeight};
    guideVel = Vector3Add(guideVel, flutter);

    // Look-ahead Terrain Lift & Safe Clearance H_safe
    if (s_terrainQuery != NULL && Wind_GroundHeight(ground, pos.x, pos.z))
    {
        const float h0 = ground->h;
        const float sampleDist = 2.4f;
        float h1 = 0.0f;
        if (Wind_SampleTerrain(pos.x + gDir.x * sampleDist, pos.z + gDir.z * sampleDist, &h1) && h1 > h0)
        {
            float dH = h1 - h0;
            float slopeLift = (dH / sampleDist) * gSpeed * 0.45f * corridorWeight;
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

static Vector3 Wind_EvaluateVelocityChannels(Vector3 pos, float time, bool includeMotion)
{
    if (!s_initialized)
        return (Vector3){0};

    // 1. Thành phần gió vĩ mô (Macro Wind)
    Vector3 totalVel = Wind_GetMacroAt(pos, time);

    const bool hasTerrain = (s_terrainQuery != NULL);
    WindGroundSample ground = {0};

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
        float speedXZ = sqrtf(totalVel.x * totalVel.x + totalVel.z * totalVel.z);
        if (speedXZ > 1e-3f && Wind_GroundHeight(&ground, pos.x, pos.z))
        {
            float dirX = totalVel.x / speedXZ;
            float dirZ = totalVel.z / speedXZ;

            const float sampleDist = 1.5f; // Khoảng cách nhìn trước dọc hướng gió (m)
            float h1 = 0.0f;
            if (Wind_SampleTerrain(pos.x + dirX * sampleDist,
                                   pos.z + dirZ * sampleDist, &h1))
            {
                float dH = h1 - ground.h;
                if (dH > 0.0f)
                {
                    // Độ dốc dương -> sinh luồng nâng thẳng đứng
                    float lift = (dH / sampleDist) * speedXZ * s_macroConfig.terrainLiftK;
                    if (lift > 12.0f)
                        lift = 12.0f; // Kẹp giới hạn an toàn
                    totalVel.y += lift;
                }
            }
        }
    }

    // 4. Tổng hợp từ mảng Vorticles cục bộ (Brute-force O(N) với N <= 256)
    // Thứ tự cộng giữ nguyên (motion -> ordinary) để kết quả float không đổi.
    Vector3 c;
    if (includeMotion)
    {
        for (int i = 0; i < s_motionAirflowCount; ++i)
        {
            if (Wind_VorticleVelocity(&s_motionAirflow[i], pos, time, &c))
            {
                totalVel.x += c.x;
                totalVel.y += c.y;
                totalVel.z += c.z;
            }
        }
    }
    const int ordinaryCount = Wind_VisibleOrdinaryCount();
    for (int i = 0; i < ordinaryCount; i++)
    {
        if (Wind_VorticleVelocity(&s_vorticles[i], pos, time, &c))
        {
            totalVel.x += c.x;
            totalVel.y += c.y;
            totalVel.z += c.z;
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
    // Lực cản khí động học kéo vận tốc hạt tiến về vận tốc dòng khí
    // a = (v_wind - v_particle) * drag
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

    // Motion chiếm chỗ của pool thường: giữ invariant (2) bằng cách bỏ Vorticle sắp hết hạn
    // nhất, thay vì để Vorticle mới nhất bị cắt âm thầm ở cuối mảng.
    Wind_TrimOrdinary(Wind_OrdinaryCapacity());
}

const VorticleData *Wind_GetActiveVorticles(int *outCount)
{
    // Snapshot 13 KB chỉ dựng lại khi nguồn đổi; nhiều receiver gọi trong cùng frame
    // (GPU packer, vegetation, debug) dùng chung một bản.
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
    float len = Vector3Length(diff);
    if (!isfinite(len))
        len = 0.0f;
    Vector3 dir = Wind_NormalizeOr(diff, (Vector3){0.0f, 0.0f, 1.0f});

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

    // Khoảng cách tới đích (XZ) là hằng số của cả đợt gió: tính một lần thay vì mỗi lần evaluate.
    s_guidingTotalDist = sqrtf(diff.x * diff.x + diff.z * diff.z);
    if (s_guidingTotalDist < 1.0f)
        s_guidingTotalDist = 24.0f;

    // Spawn an aerodynamic corridor of Vorticles to propagate physical grass & particle deflection
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
//
// Phải gọi trong BeginMode3D/EndMode3D, trên thread render. Backend Vulkan của raylib
// thường giả lập immediate mode (rlBegin/rlVertex) qua batch buffer; DrawSphereWires(8,8)
// phát 480 đỉnh + push/pop ma trận, 256 Vorticle là ~120k đỉnh/frame và tràn batch.
// Ở đây mỗi cầu chỉ là 3 vòng 16 đoạn (96 đỉnh), không đụng matrix stack, có giới hạn
// khoảng cách + số lượng.
// -----------------------------------------------------------------------------
#if !defined(CORE_HEADLESS_TEST) && !defined(HEADLESS_TEST)

#define WIND_DEBUG_RING_SEGMENTS 16
#define WIND_DEBUG_MAX_VORTICLES 96
#define WIND_DEBUG_RANGE_M 96.0f

// Cast float -> unsigned char ngoài khoảng là UB; kẹp [0,255] (NaN -> 0).
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
        // Vẽ theo TRỤC THẬT của vortex (bản cũ dùng DrawCylinderWires luôn thẳng đứng).
        Vector3 axis = v->direction;
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

    // 1. Vector Gió Vĩ mô (Macro Wind) tại vị trí anchorPos
    Vector3 macroVel = Wind_GetMacroAt(anchorPos, (float)GetTime());
    Vector3 arrowStart = Vector3Add(anchorPos, (Vector3){0.0f, 2.2f, 0.0f});
    Vector3 arrowEnd = Vector3Add(arrowStart, Vector3Scale(macroVel, 1.2f));
    const Color arrowCol = (Color){0, 240, 255, 255};

    DrawLine3D(arrowStart, arrowEnd, arrowCol);
    { // dấu thập 3 nét thay cho DrawSphereWires(4,4) = 144 đỉnh + matrix push
        const float m = 0.10f;
        DrawLine3D((Vector3){arrowEnd.x - m, arrowEnd.y, arrowEnd.z}, (Vector3){arrowEnd.x + m, arrowEnd.y, arrowEnd.z}, arrowCol);
        DrawLine3D((Vector3){arrowEnd.x, arrowEnd.y - m, arrowEnd.z}, (Vector3){arrowEnd.x, arrowEnd.y + m, arrowEnd.z}, arrowCol);
        DrawLine3D((Vector3){arrowEnd.x, arrowEnd.y, arrowEnd.z - m}, (Vector3){arrowEnd.x, arrowEnd.y, arrowEnd.z + m}, arrowCol);
    }

    // 2. Vorticle đang hoạt động: nguồn từ Motion trước, rồi pool thường.
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
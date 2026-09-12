#include "core/wind/wind_system.h"
#include "core/force_field.h"
#include <math.h>
#include <string.h>

static VorticleData           s_vorticles[MAX_VORTICLES];
static int                    s_activeCount = 0;
static WindMacroConfig        s_macroConfig;
static TerrainHeightQueryFn   s_terrainQuery = NULL;
static void                  *s_terrainUserData = NULL;
static WindTerrainGrid        s_terrainGrid;
static bool                   s_initialized = false;

// This hash-gradient noise is mirrored formula-for-formula in particle_gpu.comp.
// Keeping Wind's CPU evaluator on the same field prevents the CPU collision /
// event shadow simulation from drifting away from GPU particle trajectories.
static inline float Wind_Fract(float v) {
    return v - floorf(v);
}

static float Wind_TurbulenceAttackWeight(const VorticleData *v) {
    float attackTime = v->direction.z;
    if (attackTime <= 1e-4f)
        return 1.0f;
    float age = fmaxf(0.0f, v->maxLifetime - v->lifetime);
    float t = fminf(age / attackTime, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static Vector3 Wind_Hash3(Vector3 p) {
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

static float Wind_NoiseScalar3D(Vector3 p) {
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
// Vòng đời
// -----------------------------------------------------------------------------
void Wind_Init(void) {
    memset(s_vorticles, 0, sizeof(s_vorticles));
    s_activeCount = 0;
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainGrid.version = 1;

    // Cấu hình gió vĩ mô mặc định (gió đêm thoang thoảng quét qua đấu trường)
    s_macroConfig = (WindMacroConfig){
        .baseDirection = (Vector3){ 1.5f, 0.0f, 0.8f },
        .gustAmplitude = 0.6f,
        .noiseScale    = 0.06f,
        .noiseSpeed    = 1.0f,
        .terrainLiftK  = 1.2f,
    };
    s_initialized = true;
}

void Wind_Clear(void) {
    for (int i = 0; i < s_activeCount; i++) {
        s_vorticles[i].active = false;
    }
    s_activeCount = 0;
}

void Wind_Unload(void) {
    Wind_Clear();
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_initialized = false;
}

void Wind_SetMacro(const WindMacroConfig *cfg) {
    if (!cfg) return;
    s_macroConfig = *cfg;
}

WindMacroConfig Wind_GetMacro(void) {
    return s_macroConfig;
}

void Wind_SetTerrainHeightQuery(TerrainHeightQueryFn queryFn, void *userData) {
    s_terrainQuery = queryFn;
    s_terrainUserData = userData;
    s_terrainGrid.built = false;
    s_terrainGrid.active = false;
    s_terrainGrid.version++;
}

void Wind_RebuildTerrainGrid(Vector2 centerXZ, Vector2 halfExtentXZ) {
    unsigned int nextVersion = s_terrainGrid.version + 1u;
    memset(&s_terrainGrid, 0, sizeof(s_terrainGrid));
    s_terrainGrid.version = nextVersion;
    s_terrainGrid.built = true;

    if (s_terrainQuery == NULL || halfExtentXZ.x <= 0.0f || halfExtentXZ.y <= 0.0f)
        return;

    s_terrainGrid.originXZ = (Vector2){centerXZ.x - halfExtentXZ.x,
                                       centerXZ.y - halfExtentXZ.y};
    s_terrainGrid.cellSizeXZ = (Vector2){
        (halfExtentXZ.x * 2.0f) / (float)(WIND_TERRAIN_GRID_SIZE - 1),
        (halfExtentXZ.y * 2.0f) / (float)(WIND_TERRAIN_GRID_SIZE - 1),
    };

    int validCount = 0;
    for (int z = 0; z < WIND_TERRAIN_GRID_SIZE; ++z) {
        for (int x = 0; x < WIND_TERRAIN_GRID_SIZE; ++x) {
            int index = z * WIND_TERRAIN_GRID_SIZE + x;
            float worldX = s_terrainGrid.originXZ.x +
                           (float)x * s_terrainGrid.cellSizeXZ.x;
            float worldZ = s_terrainGrid.originXZ.y +
                           (float)z * s_terrainGrid.cellSizeXZ.y;
            float height = s_terrainQuery(worldX, worldZ, s_terrainUserData);
            if (isfinite(height)) {
                s_terrainGrid.heights[index] = height;
                s_terrainGrid.valid[index] = 1;
                validCount++;
            }
        }
    }
    s_terrainGrid.active = validCount > 0;
}

const WindTerrainGrid *Wind_GetTerrainGrid(void) {
    return &s_terrainGrid;
}

static bool Wind_SampleTerrainGrid(float worldX, float worldZ, float *outHeight) {
    if (!s_terrainGrid.built || !s_terrainGrid.active || outHeight == NULL)
        return false;

    float gx = (worldX - s_terrainGrid.originXZ.x) / s_terrainGrid.cellSizeXZ.x;
    float gz = (worldZ - s_terrainGrid.originXZ.y) / s_terrainGrid.cellSizeXZ.y;
    float gridMax = (float)(WIND_TERRAIN_GRID_SIZE - 1);
    if (gx < 0.0f || gz < 0.0f || gx > gridMax || gz > gridMax)
        return false;

    int x0 = (int)floorf(gx);
    int z0 = (int)floorf(gz);
    if (x0 >= WIND_TERRAIN_GRID_SIZE - 1) x0 = WIND_TERRAIN_GRID_SIZE - 2;
    if (z0 >= WIND_TERRAIN_GRID_SIZE - 1) z0 = WIND_TERRAIN_GRID_SIZE - 2;
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

static bool Wind_SampleTerrain(float worldX, float worldZ, float *outHeight) {
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

// -----------------------------------------------------------------------------
// Quản lý mảng Vorticles (Ring-buffer / Active compaction)
// -----------------------------------------------------------------------------
void Wind_Update(float dt) {
    if (!s_initialized || s_activeCount <= 0) return;

    // 1. Cập nhật thời gian sống
    for (int i = 0; i < s_activeCount; i++) {
        s_vorticles[i].lifetime -= dt;
        if (s_vorticles[i].lifetime <= 0.0f) {
            s_vorticles[i].active = false;
        }
    }

    // 2. Dồn mảng (Compact contiguous array) để tối ưu cache L1 khi query
    int writeIdx = 0;
    for (int readIdx = 0; readIdx < s_activeCount; readIdx++) {
        if (s_vorticles[readIdx].active) {
            if (writeIdx != readIdx) {
                s_vorticles[writeIdx] = s_vorticles[readIdx];
            }
            writeIdx++;
        }
    }
    s_activeCount = writeIdx;
}

static int AllocVorticleSlot(void) {
    if (s_activeCount < MAX_VORTICLES) {
        int idx = s_activeCount;
        s_activeCount++;
        return idx;
    }

    // Pool đầy: tìm phần tử có thời gian sống còn lại ngắn nhất để ghi đè
    int minIdx = 0;
    float minLife = s_vorticles[0].lifetime;
    for (int i = 1; i < MAX_VORTICLES; i++) {
        if (s_vorticles[i].lifetime < minLife) {
            minLife = s_vorticles[i].lifetime;
            minIdx = i;
        }
    }
    return minIdx;
}

// -----------------------------------------------------------------------------
// Emitters
// -----------------------------------------------------------------------------
int Wind_SpawnGust(Vector3 pos, Vector3 dir, float radius, float strength, float duration) {
    if (!s_initialized || duration <= 0.0f || radius <= 0.0f) return -1;
    int idx = AllocVorticleSlot();

    float len = Vector3Length(dir);
    Vector3 normDir = (len > 1e-4f) ? Vector3Scale(dir, 1.0f / len) : (Vector3){ 1.0f, 0.0f, 0.0f };

    s_vorticles[idx] = (VorticleData){
        .position    = pos,
        .direction   = normDir,
        .radius      = radius,
        .strength    = strength,
        .type        = VORTICLE_LINEAR_GUST,
        .lifetime    = duration,
        .maxLifetime = duration,
        .inwardPull  = 0.0f,
        .active      = true,
    };
    return idx;
}

int Wind_SpawnRadialBlast(Vector3 pos, float radius, float strength, float duration) {
    if (!s_initialized || duration <= 0.0f || radius <= 0.0f) return -1;
    int idx = AllocVorticleSlot();

    s_vorticles[idx] = (VorticleData){
        .position    = pos,
        .direction   = (Vector3){ 0.0f, 1.0f, 0.0f },
        .radius      = radius,
        .strength    = strength,
        .type        = VORTICLE_RADIAL_BLAST,
        .lifetime    = duration,
        .maxLifetime = duration,
        .inwardPull  = 0.0f,
        .active      = true,
    };
    return idx;
}

int Wind_SpawnVortex(Vector3 pos, Vector3 axis, float radius, float strength, float inwardPull, float duration) {
    if (!s_initialized || duration <= 0.0f || radius <= 0.0f) return -1;
    int idx = AllocVorticleSlot();

    float len = Vector3Length(axis);
    Vector3 normAxis = (len > 1e-4f) ? Vector3Scale(axis, 1.0f / len) : (Vector3){ 0.0f, 1.0f, 0.0f };

    s_vorticles[idx] = (VorticleData){
        .position    = pos,
        .direction   = normAxis,
        .radius      = radius,
        .strength    = strength,
        .type        = VORTICLE_VORTEX,
        .lifetime    = duration,
        .maxLifetime = duration,
        .inwardPull  = inwardPull,
        .active      = true,
    };
    return idx;
}

int Wind_SpawnTurbulence(Vector3 pos, float radius, float strength, float noiseScale, float noiseSpeed, float duration) {
    if (!s_initialized || duration <= 0.0f || radius <= 0.0f) return -1;
    int idx = AllocVorticleSlot();

    // direction.z carries a short attack envelope. This gives an opening
    // pressure blast a readable beat before the stronger turbulent wake.
    float attackTime = fminf(0.20f, duration * 0.25f);
    s_vorticles[idx] = (VorticleData){
        .position    = pos,
        .direction   = (Vector3){ noiseScale, noiseSpeed, attackTime },
        .radius      = radius,
        .strength    = strength,
        .type        = VORTICLE_TURBULENCE,
        .lifetime    = duration,
        .maxLifetime = duration,
        .inwardPull  = 0.0f,
        .active      = true,
    };
    return idx;
}

// -----------------------------------------------------------------------------
// Evaluators
// -----------------------------------------------------------------------------
Vector3 Wind_GetMacroAt(Vector3 pos, float time) {
    if (!s_initialized) return (Vector3){ 0 };

    float baseLen = Vector3Length(s_macroConfig.baseDirection);
    if (baseLen < 1e-4f) return (Vector3){ 0 };

    if (s_macroConfig.gustAmplitude <= 1e-4f) {
        return s_macroConfig.baseDirection;
    }

    // 3D hash-gradient noise đa chiều mô phỏng các xoáy loạn lưu chất lưu.
    float s = s_macroConfig.noiseScale;
    float spd = s_macroConfig.noiseSpeed;
    float nx = Wind_NoiseScalar3D((Vector3){pos.x * s - time * spd, pos.y * s + 17.3f, pos.z * s - time * spd * 0.7f});
    float ny = Wind_NoiseScalar3D((Vector3){pos.x * s + 37.1f, pos.y * s - time * spd * 0.8f, pos.z * s + 19.7f});
    float nz = Wind_NoiseScalar3D((Vector3){pos.x * s - time * spd * 0.6f, pos.y * s + 53.9f, pos.z * s + time * spd * 0.5f});

    float amp = s_macroConfig.gustAmplitude * fmaxf(baseLen, 2.0f);
    Vector3 turb = {
        s_macroConfig.baseDirection.x * (1.0f + s_macroConfig.gustAmplitude * nx * 0.5f) + nx * amp * 0.5f,
        s_macroConfig.baseDirection.y + ny * amp * 0.35f,
        s_macroConfig.baseDirection.z * (1.0f + s_macroConfig.gustAmplitude * nz * 0.5f) + nz * amp * 0.5f
    };
    return turb;
}

Vector3 Wind_EvaluateVorticleVelocity(const VorticleData *v, Vector3 pos, float time) {
    Vector3 velocity = {0};
    if (v == NULL || !v->active)
        return velocity;

    Vector3 delta = Vector3Subtract(pos, v->position);
    float distSq = Vector3LengthSqr(delta);
    float rSq = v->radius * v->radius;
    if (distSq >= rSq || rSq < 1e-6f)
        return velocity;

    float dist = sqrtf(distSq);
    float spatialAtten = 1.0f - (dist / v->radius);
    float temporalAtten = (v->maxLifetime > 1e-4f)
        ? (v->lifetime / v->maxLifetime) : 1.0f;
    float weight = spatialAtten * temporalAtten;

    if (v->type == VORTICLE_LINEAR_GUST) {
        velocity = Vector3Scale(v->direction, v->strength * weight);
    } else if (v->type == VORTICLE_RADIAL_BLAST) {
        Vector3 normOut = (dist > 1e-4f)
            ? Vector3Scale(delta, 1.0f / dist)
            : (Vector3){0.0f, 1.0f, 0.0f};
        velocity = Vector3Scale(normOut, v->strength * weight);
    } else if (v->type == VORTICLE_VORTEX) {
        Vector3 tangent = Vector3CrossProduct(v->direction, delta);
        float tanLen = Vector3Length(tangent);
        float rCore = 0.10f * v->radius;
        float coreFactor = (tanLen < rCore) ? (tanLen / rCore) : 1.0f;
        if (tanLen > 1e-4f) {
            Vector3 tanDir = Vector3Scale(tangent, 1.0f / tanLen);
            velocity = Vector3Scale(tanDir, v->strength * weight * coreFactor);
        }
        if (fabsf(v->inwardPull) > 1e-4f) {
            float proj = Vector3DotProduct(delta, v->direction);
            Vector3 closestOnAxis = Vector3Scale(v->direction, proj);
            Vector3 radial = Vector3Subtract(delta, closestOnAxis);
            float radDist = Vector3Length(radial);
            if (radDist > 1e-4f) {
                Vector3 radDir = Vector3Scale(radial, 1.0f / radDist);
                Vector3 pullVel = Vector3Scale(radDir,
                    -v->inwardPull * weight * coreFactor);
                velocity = Vector3Add(velocity, pullVel);
            }
        }
    } else if (v->type == VORTICLE_TURBULENCE) {
        float ns = v->direction.x;
        float spd = v->direction.y;
        float px = pos.x * ns;
        float py = pos.y * ns;
        float pz = pos.z * ns;
        float t = time * spd;
        float nx = Wind_NoiseScalar3D((Vector3){px + t, py + 17.3f, pz - t * 0.7f});
        float ny = Wind_NoiseScalar3D((Vector3){px + 37.1f, py - t * 0.8f, pz + 19.7f});
        float nz = Wind_NoiseScalar3D((Vector3){px - t * 0.6f, py + 53.9f, pz + t * 0.5f});
        float strength = v->strength * weight *
                         Wind_TurbulenceAttackWeight(v);
        velocity = (Vector3){nx * strength, ny * strength, nz * strength};
    }
    return velocity;
}

Vector3 Wind_EvaluateVelocity(Vector3 pos, float time) {
    if (!s_initialized) return (Vector3){ 0 };

    // 1. Thành phần gió vĩ mô (Macro Wind)
    Vector3 totalVel = Wind_GetMacroAt(pos, time);

    // 2. Thành phần nâng địa hình (Terrain-Aware Lift)
    if (s_terrainQuery != NULL && s_macroConfig.terrainLiftK > 0.0f) {
        float speedXZ = sqrtf(totalVel.x * totalVel.x + totalVel.z * totalVel.z);
        if (speedXZ > 1e-3f) {
            float dirX = totalVel.x / speedXZ;
            float dirZ = totalVel.z / speedXZ;

            const float sampleDist = 1.5f; // Khoảng cách nhìn trước dọc hướng gió (m)
            float h0 = 0.0f, h1 = 0.0f;
            bool hasH0 = Wind_SampleTerrain(pos.x, pos.z, &h0);
            bool hasH1 = Wind_SampleTerrain(pos.x + dirX * sampleDist,
                                            pos.z + dirZ * sampleDist, &h1);
            float dH = h1 - h0;

            if (hasH0 && hasH1 && dH > 0.0f) {
                // Độ dốc dương -> sinh luồng nâng thẳng đứng
                float lift = (dH / sampleDist) * speedXZ * s_macroConfig.terrainLiftK;
                if (lift > 12.0f) lift = 12.0f; // Kẹp giới hạn an toàn
                totalVel.y += lift;
            }
        }
    }

    // 3. Tổng hợp từ mảng Vorticles cục bộ (Brute-force O(N) với N <= 256)
    for (int i = 0; i < s_activeCount; i++) {
        Vector3 localVelocity = Wind_EvaluateVorticleVelocity(&s_vorticles[i], pos, time);
        totalVel = Vector3Add(totalVel, localVelocity);
    }

    return totalVel;
}

Vector3 Wind_EvaluateAcceleration(Vector3 pos, float time, Vector3 currentVel) {
    Vector3 targetWindVel = Wind_EvaluateVelocity(pos, time);
    // Lực cản khí động học kéo vận tốc hạt tiến về vận tốc dòng khí
    // a = (v_wind - v_particle) * drag
    const float dragCoeff = 3.5f;
    Vector3 relVel = Vector3Subtract(targetWindVel, currentVel);
    return Vector3Scale(relVel, dragCoeff);
}

const VorticleData* Wind_GetActiveVorticles(int *outCount) {
    if (outCount) *outCount = s_activeCount;
    return s_vorticles;
}

int Wind_GetActiveCount(void) {
    return s_activeCount;
}

#if !defined(CORE_HEADLESS_TEST) && !defined(HEADLESS_TEST)
void Wind_DrawDebug(Vector3 anchorPos) {
    if (!s_initialized) return;

    // 1. Vector Gió Vĩ mô (Macro Wind) tại vị trí anchorPos
    Vector3 macroVel = Wind_GetMacroAt(anchorPos, (float)GetTime());
    Vector3 arrowStart = Vector3Add(anchorPos, (Vector3){ 0.0f, 2.2f, 0.0f });
    Vector3 arrowEnd = Vector3Add(arrowStart, Vector3Scale(macroVel, 1.2f));

    DrawLine3D(arrowStart, arrowEnd, (Color){ 0, 240, 255, 255 });
    DrawSphereWires(arrowEnd, 0.10f, 4, 4, (Color){ 120, 255, 255, 255 });

    // 2. Các hạt gió cục bộ Vorticles đang hoạt động
    for (int i = 0; i < s_activeCount; i++) {
        const VorticleData *v = &s_vorticles[i];
        float lifeNorm = (v->maxLifetime > 1e-4f) ? (v->lifetime / v->maxLifetime) : 1.0f;

        if (v->type == VORTICLE_LINEAR_GUST) {
            Vector3 gustEnd = Vector3Add(v->position, Vector3Scale(v->direction, v->radius * 0.8f));
            DrawLine3D(v->position, gustEnd, (Color){ 0, 255, 200, (unsigned char)(255 * lifeNorm) });
            DrawSphereWires(v->position, v->radius * 0.4f, 6, 6, (Color){ 0, 200, 255, (unsigned char)(140 * lifeNorm) });

        } else if (v->type == VORTICLE_RADIAL_BLAST) {
            float curR = v->radius * (1.0f - lifeNorm * 0.4f);
            DrawSphereWires(v->position, curR, 8, 8, (Color){ 255, 220, 40, (unsigned char)(200 * lifeNorm) });

        } else if (v->type == VORTICLE_VORTEX) {
            DrawCylinderWires(v->position, v->radius, v->radius, v->radius * 1.2f, 8,
                              (Color){ 255, 120, 20, (unsigned char)(180 * lifeNorm) });
        }
    }
}
#else
void Wind_DrawDebug(Vector3 anchorPos) {
    (void)anchorPos;
}
#endif

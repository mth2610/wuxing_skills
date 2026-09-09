#include "core/wind/wind_system.h"
#include "core/force_field.h"
#include <math.h>
#include <string.h>

static VorticleData           s_vorticles[MAX_VORTICLES];
static int                    s_activeCount = 0;
static WindMacroConfig        s_macroConfig;
static TerrainHeightQueryFn   s_terrainQuery = NULL;
static void                  *s_terrainUserData = NULL;
static bool                   s_initialized = false;

// -----------------------------------------------------------------------------
// Vòng đời
// -----------------------------------------------------------------------------
void Wind_Init(void) {
    memset(s_vorticles, 0, sizeof(s_vorticles));
    s_activeCount = 0;
    s_terrainQuery = NULL;
    s_terrainUserData = NULL;

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

// -----------------------------------------------------------------------------
// Evaluators
// -----------------------------------------------------------------------------
Vector3 Wind_GetMacroAt(Vector3 pos, float time) {
    if (!s_initialized) return (Vector3){ 0 };

    float baseLen = Vector3Length(s_macroConfig.baseDirection);
    if (baseLen < 1e-4f) return (Vector3){ 0 };

    // Sóng nhiễu Perlin cuộn theo không gian và thời gian
    float s = s_macroConfig.noiseScale;
    float spd = s_macroConfig.noiseSpeed;
    float n = Noise_Perlin3D(pos.x * s - time * spd, pos.y * s, pos.z * s - time * spd * 0.7f);

    // w_macro = w_base * (1.0 + A * noise)
    float factor = 1.0f + s_macroConfig.gustAmplitude * n;
    if (factor < 0.0f) factor = 0.0f;

    return Vector3Scale(s_macroConfig.baseDirection, factor);
}

Vector3 Wind_EvaluateVelocity(Vector3 pos, float time) {
    if (!s_initialized) return (Vector3){ 0 };

    // 1. Thành phần gió vĩ mô (Macro Wind)
    Vector3 totalVel = Wind_GetMacroAt(pos, time);

    // 2. Thành phần nâng địa hình (Terrain-Aware Lift)
    if (s_terrainQuery != NULL) {
        float speedXZ = sqrtf(totalVel.x * totalVel.x + totalVel.z * totalVel.z);
        if (speedXZ > 1e-3f) {
            float dirX = totalVel.x / speedXZ;
            float dirZ = totalVel.z / speedXZ;

            const float sampleDist = 1.5f; // Khoảng cách nhìn trước dọc hướng gió (m)
            float h0 = s_terrainQuery(pos.x, pos.z, s_terrainUserData);
            float h1 = s_terrainQuery(pos.x + dirX * sampleDist, pos.z + dirZ * sampleDist, s_terrainUserData);
            float dH = h1 - h0;

            if (dH > 0.0f) {
                // Độ dốc dương -> sinh luồng nâng thẳng đứng
                float lift = (dH / sampleDist) * speedXZ * s_macroConfig.terrainLiftK;
                if (lift > 12.0f) lift = 12.0f; // Kẹp giới hạn an toàn
                totalVel.y += lift;
            }
        }
    }

    // 3. Tổng hợp từ mảng Vorticles cục bộ (Brute-force O(N) với N <= 256)
    for (int i = 0; i < s_activeCount; i++) {
        const VorticleData *v = &s_vorticles[i];
        Vector3 delta = Vector3Subtract(pos, v->position);
        float distSq = Vector3LengthSqr(delta);
        float rSq = v->radius * v->radius;

        if (distSq >= rSq || rSq < 1e-6f) continue;

        float dist = sqrtf(distSq);
        float spatialAtten = 1.0f - (dist / v->radius); // Tuyến tính từ tâm ra biên
        float temporalAtten = (v->maxLifetime > 1e-4f) ? (v->lifetime / v->maxLifetime) : 1.0f;
        float weight = spatialAtten * temporalAtten;

        if (v->type == VORTICLE_LINEAR_GUST) {
            // Luồng gió thẳng
            Vector3 gust = Vector3Scale(v->direction, v->strength * weight);
            totalVel = Vector3Add(totalVel, gust);

        } else if (v->type == VORTICLE_RADIAL_BLAST) {
            // Xung kích tỏa tròn
            Vector3 normOut = (dist > 1e-4f)
                ? Vector3Scale(delta, 1.0f / dist)
                : (Vector3){ 0.0f, 1.0f, 0.0f };
            Vector3 blast = Vector3Scale(normOut, v->strength * weight);
            totalVel = Vector3Add(totalVel, blast);

        } else if (v->type == VORTICLE_VORTEX) {
            // Lốc xoáy quanh trục
            Vector3 tangent = Vector3CrossProduct(v->direction, delta);
            float tanLen = Vector3Length(tangent);
            if (tanLen > 1e-4f) {
                Vector3 tanDir = Vector3Scale(tangent, 1.0f / tanLen);
                Vector3 rotVel = Vector3Scale(tanDir, v->strength * weight);
                totalVel = Vector3Add(totalVel, rotVel);
            }

            // Lực hút/đẩy xuyên tâm vuông góc trục
            if (fabsf(v->inwardPull) > 1e-4f) {
                float proj = Vector3DotProduct(delta, v->direction);
                Vector3 closestOnAxis = Vector3Scale(v->direction, proj);
                Vector3 radial = Vector3Subtract(delta, closestOnAxis);
                float radDist = Vector3Length(radial);
                if (radDist > 1e-4f) {
                    Vector3 radDir = Vector3Scale(radial, 1.0f / radDist);
                    // inwardPull > 0: hút vào tâm (-radDir); < 0: đẩy ra (+radDir)
                    Vector3 pullVel = Vector3Scale(radDir, -v->inwardPull * weight);
                    totalVel = Vector3Add(totalVel, pullVel);
                }
            }
        }
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

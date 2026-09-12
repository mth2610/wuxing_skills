#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "raylib.h"
#include "raymath.h"

static inline Vector3 Vector3Add(Vector3 a, Vector3 b) {
    return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) {
    return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline Vector3 Vector3Scale(Vector3 v, float scale) {
    return (Vector3){v.x * scale, v.y * scale, v.z * scale};
}

static inline float Vector3Length(Vector3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float Vector3LengthSqr(Vector3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
    return (Vector3){a.y * b.z - a.z * b.y,
                     a.z * b.x - a.x * b.z,
                     a.x * b.y - a.y * b.x};
}

static inline float Vector3DotProduct(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

#define CORE_HEADLESS_TEST 1
#include "core/force_field.c"
#include "core/wind/wind_system.c"

static int s_failed = 0;
static int s_passed = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (cond) { \
            s_passed++; \
            printf("[PASS] %s\n", msg); \
        } else { \
            s_failed++; \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__); \
        } \
    } while (0)

#define TEST_NEAR(a, b, eps, msg) \
    do { \
        float diff = fabsf((a) - (b)); \
        if (diff <= (eps)) { \
            s_passed++; \
            printf("[PASS] %s (val=%.3f, expected=%.3f)\n", msg, (double)(a), (double)(b)); \
        } else { \
            s_failed++; \
            printf("[FAIL] %s (val=%.3f, expected=%.3f, diff=%.3f, eps=%.3f, line %d)\n", \
                   msg, (double)(a), (double)(b), (double)diff, (double)(eps), __LINE__); \
        } \
    } while (0)

// Mock terrain height: dốc nghiêng theo trục X (+0.5m mỗi 1m)
static float MockTerrainSlope(float x, float z, void *userData) {
    (void)z;
    (void)userData;
    return x * 0.5f;
}

static float TestFract(float v) {
    return v - floorf(v);
}

// Numeric mirror of particle_gpu.comp's non-sine hash/noise path. This test
// compares observable Wind output, not production helper internals.
static Vector3 TestGpuHash3(Vector3 p) {
    p.x = TestFract(p.x * 0.1031f);
    p.y = TestFract(p.y * 0.1030f);
    p.z = TestFract(p.z * 0.0973f);
    float d = p.x * (p.y + 33.33f) +
              p.y * (p.x + 33.33f) +
              p.z * (p.z + 33.33f);
    p.x += d;
    p.y += d;
    p.z += d;
    return (Vector3){
        TestFract((p.x + p.y) * p.z) * 2.0f - 1.0f,
        TestFract((p.x + p.x) * p.y) * 2.0f - 1.0f,
        TestFract((p.y + p.x) * p.x) * 2.0f - 1.0f,
    };
}

static float TestGpuNoiseScalar(Vector3 p) {
    Vector3 i = {floorf(p.x), floorf(p.y), floorf(p.z)};
    Vector3 f = {TestFract(p.x), TestFract(p.y), TestFract(p.z)};
    Vector3 u = {
        f.x * f.x * (3.0f - 2.0f * f.x),
        f.y * f.y * (3.0f - 2.0f * f.y),
        f.z * f.z * (3.0f - 2.0f * f.z),
    };
    float corners[2][2][2];
    for (int z = 0; z < 2; ++z) {
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                Vector3 lattice = {i.x + (float)x, i.y + (float)y, i.z + (float)z};
                Vector3 grad = TestGpuHash3(lattice);
                corners[z][y][x] = grad.x * (f.x - (float)x) +
                                   grad.y * (f.y - (float)y) +
                                   grad.z * (f.z - (float)z);
            }
        }
    }
    float nx00 = corners[0][0][0] + (corners[0][0][1] - corners[0][0][0]) * u.x;
    float nx10 = corners[0][1][0] + (corners[0][1][1] - corners[0][1][0]) * u.x;
    float nx01 = corners[1][0][0] + (corners[1][0][1] - corners[1][0][0]) * u.x;
    float nx11 = corners[1][1][0] + (corners[1][1][1] - corners[1][1][0]) * u.x;
    float nxy0 = nx00 + (nx10 - nx00) * u.y;
    float nxy1 = nx01 + (nx11 - nx01) * u.y;
    return nxy0 + (nxy1 - nxy0) * u.z;
}

static bool FileContains(const char *path, const char *needle) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char buffer[65536];
    size_t count = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[count] = '\0';
    return strstr(buffer, needle) != NULL;
}

int main(void) {
    printf("=== RUNNING WIND & VORTICLES SYSTEM TEST (Ghost of Tsushima) ===\n");

    // 1. Khởi tạo
    Wind_Init();
    TEST_CHECK(Wind_GetActiveCount() == 0, "Initial active count is 0");

    WindMacroConfig macro = Wind_GetMacro();
    TEST_CHECK(macro.baseDirection.x != 0.0f || macro.baseDirection.z != 0.0f, "Default macro wind exists");

    // CPU collision/event shadowing and GPU compute must sample the same wind.
    {
        WindMacroConfig parityMacro = {
            .baseDirection = {1.7f, 0.2f, -0.8f},
            .gustAmplitude = 0.55f,
            .noiseScale = 0.13f,
            .noiseSpeed = 0.9f,
            .terrainLiftK = 0.0f,
        };
        Vector3 pos = {2.3f, 1.1f, -4.7f};
        float time = 1.35f;
        Wind_SetMacro(&parityMacro);

        float nx = TestGpuNoiseScalar((Vector3){pos.x * parityMacro.noiseScale - time * parityMacro.noiseSpeed,
                                                pos.y * parityMacro.noiseScale + 17.3f,
                                                pos.z * parityMacro.noiseScale - time * parityMacro.noiseSpeed * 0.7f});
        float ny = TestGpuNoiseScalar((Vector3){pos.x * parityMacro.noiseScale + 37.1f,
                                                pos.y * parityMacro.noiseScale - time * parityMacro.noiseSpeed * 0.8f,
                                                pos.z * parityMacro.noiseScale + 19.7f});
        float nz = TestGpuNoiseScalar((Vector3){pos.x * parityMacro.noiseScale - time * parityMacro.noiseSpeed * 0.6f,
                                                pos.y * parityMacro.noiseScale + 53.9f,
                                                pos.z * parityMacro.noiseScale + time * parityMacro.noiseSpeed * 0.5f});
        float baseLen = Vector3Length(parityMacro.baseDirection);
        float amp = parityMacro.gustAmplitude * fmaxf(baseLen, 2.0f);
        Vector3 expected = {
            parityMacro.baseDirection.x * (1.0f + parityMacro.gustAmplitude * nx * 0.5f) + nx * amp * 0.5f,
            parityMacro.baseDirection.y + ny * amp * 0.35f,
            parityMacro.baseDirection.z * (1.0f + parityMacro.gustAmplitude * nz * 0.5f) + nz * amp * 0.5f,
        };
        Vector3 actual = Wind_GetMacroAt(pos, time);
        TEST_NEAR(actual.x, expected.x, 0.0002f, "CPU macro X matches GPU noise field");
        TEST_NEAR(actual.y, expected.y, 0.0002f, "CPU macro Y matches GPU noise field");
        TEST_NEAR(actual.z, expected.z, 0.0002f, "CPU macro Z matches GPU noise field");
        TEST_CHECK(FileContains("core/particles/shaders/gpu/particle_gpu.comp",
                                "p = fract(p * vec3(0.1031, 0.1030, 0.0973))"),
                   "GPU wind keeps the mirrored non-sine hash constants");
        TEST_CHECK(FileContains("core/particles/shaders/gpu/particle_gpu.comp",
                                "f * f * (3.0 - 2.0 * f)"),
                   "GPU wind keeps the mirrored cubic interpolation");
    }

    // Tắt macro wind để kiểm tra riêng từng Vorticle một cách chính xác
    WindMacroConfig zeroMacro = {0};
    Wind_SetMacro(&zeroMacro);
    TEST_CHECK(Vector3Length(Wind_GetMacroAt((Vector3){0, 0, 0}, 0.0f)) == 0.0f, "Zero macro wind verified");

    // 2. Test Linear Gust (Vệt chém kiếm / luồng gió thẳng)
    {
        Wind_Clear();
        Vector3 pos = { 0.0f, 1.0f, 0.0f };
        Vector3 dir = { 1.0f, 0.0f, 0.0f }; // hướng +X
        float radius = 4.0f;
        float strength = 10.0f;
        float duration = 1.0f;

        int id = Wind_SpawnGust(pos, dir, radius, strength, duration);
        TEST_CHECK(id >= 0, "Spawn linear gust returns valid ID");
        TEST_CHECK(Wind_GetActiveCount() == 1, "Active count is 1");

        // Vận tốc tại tâm = 10.0 theo +X
        Vector3 vCenter = Wind_EvaluateVelocity(pos, 0.0f);
        TEST_NEAR(vCenter.x, 10.0f, 0.05f, "Gust velocity at center is 10.0 m/s");
        TEST_NEAR(vCenter.y, 0.0f, 0.05f, "Gust Y is 0.0");
        TEST_NEAR(vCenter.z, 0.0f, 0.05f, "Gust Z is 0.0");

        // Vận tốc tại khoảng cách r/2 (2m theo trục X)
        Vector3 vHalf = Wind_EvaluateVelocity((Vector3){ 2.0f, 1.0f, 0.0f }, 0.0f);
        TEST_NEAR(vHalf.x, 5.0f, 0.05f, "Gust velocity at r/2 is 5.0 m/s (50% attenuation)");

        // Vận tốc ngoài bán kính (5m > 4m)
        Vector3 vOutside = Wind_EvaluateVelocity((Vector3){ 5.0f, 1.0f, 0.0f }, 0.0f);
        TEST_NEAR(vOutside.x, 0.0f, 0.01f, "Gust velocity outside radius is 0.0 m/s");
    }

    // 3. Test Radial Blast (Chưởng nổ tỏa tròn)
    {
        Wind_Clear();
        Vector3 center = { 0.0f, 0.0f, 0.0f };
        float radius = 5.0f;
        float strength = 20.0f;
        float duration = 0.5f;

        Wind_SpawnRadialBlast(center, radius, strength, duration);
        TEST_CHECK(Wind_GetActiveCount() == 1, "Radial blast spawned");

        // Điểm tại (+2.5, 0, 0): hướng đẩy phải là +X với độ lớn = 20 * (1 - 2.5/5) = 10.0
        Vector3 vRight = Wind_EvaluateVelocity((Vector3){ 2.5f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vRight.x, 10.0f, 0.05f, "Radial blast pushes +X to the right");
        TEST_NEAR(vRight.y, 0.0f, 0.05f, "Radial blast Y is 0 to the right");

        // Điểm tại (-2.5, 0, 0): hướng đẩy phải là -X với độ lớn = -10.0
        Vector3 vLeft = Wind_EvaluateVelocity((Vector3){ -2.5f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vLeft.x, -10.0f, 0.05f, "Radial blast pushes -X to the left");

        // Điểm tại (0, 2.5, 0): hướng đẩy phải là +Y với độ lớn = 10.0
        Vector3 vUp = Wind_EvaluateVelocity((Vector3){ 0.0f, 2.5f, 0.0f }, 0.0f);
        TEST_NEAR(vUp.y, 10.0f, 0.05f, "Radial blast pushes +Y upward");
    }

    // 4. Test Vortex (Lốc xoáy quanh trục + lực hút xuyên tâm)
    {
        Wind_Clear();
        Vector3 center = { 0.0f, 0.0f, 0.0f };
        Vector3 axis = { 0.0f, 1.0f, 0.0f }; // trục thẳng đứng +Y
        float radius = 4.0f;
        float strength = 12.0f;
        float inwardPull = 6.0f; // hút vào tâm
        float duration = 1.0f;

        Wind_SpawnVortex(center, axis, radius, strength, inwardPull, duration);

        // Khảo sát tại điểm (2, 0, 0) (nửa bán kính):
        // Tangent = Axis(0,1,0) x Delta(2,0,0) = (0, 0, -2) -> hướng -Z
        // Inward pull = hút về gốc -> hướng -X
        // Weight tại r/2 = 0.5
        // V_tan = 12.0 * 0.5 = 6.0 theo -Z
        // V_in  = 6.0 * 0.5 = 3.0 theo -X
        Vector3 vVortex = Wind_EvaluateVelocity((Vector3){ 2.0f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vVortex.z, -6.0f, 0.05f, "Vortex tangential velocity is -6.0 m/s (-Z)");
        TEST_NEAR(vVortex.x, -3.0f, 0.05f, "Vortex inward pull velocity is -3.0 m/s (-X)");
        TEST_NEAR(vVortex.y, 0.0f, 0.05f, "Vortex Y velocity is 0.0");
    }

    // 5. Test Turbulence CPU/GPU parity
    {
        Wind_Clear();
        Wind_SetMacro(&zeroMacro);
        Vector3 pos = {0.5f, 0.25f, -0.75f};
        float time = 0.65f;
        float radius = 4.0f;
        float strength = 7.0f;
        float noiseScale = 0.8f;
        float noiseSpeed = 1.2f;
        Wind_SpawnTurbulence((Vector3){0}, radius, strength,
                             noiseScale, noiseSpeed, 2.0f);

        float px = pos.x * noiseScale;
        float py = pos.y * noiseScale;
        float pz = pos.z * noiseScale;
        float t = time * noiseSpeed;
        float weight = 1.0f - Vector3Length(pos) / radius;
        Vector3 expected = {
            TestGpuNoiseScalar((Vector3){px + t, py + 17.3f, pz - t * 0.7f}) * strength * weight,
            TestGpuNoiseScalar((Vector3){px + 37.1f, py - t * 0.8f, pz + 19.7f}) * strength * weight,
            TestGpuNoiseScalar((Vector3){px - t * 0.6f, py + 53.9f, pz + t * 0.5f}) * strength * weight,
        };
        Vector3 actual = Wind_EvaluateVelocity(pos, time);
        TEST_NEAR(actual.x, expected.x, 0.0002f, "CPU turbulence X matches GPU noise field");
        TEST_NEAR(actual.y, expected.y, 0.0002f, "CPU turbulence Y matches GPU noise field");
        TEST_NEAR(actual.z, expected.z, 0.0002f, "CPU turbulence Z matches GPU noise field");
    }

    // 6. Test Vòng đời & Tự thu hồi (Decay & Lifetime)
    {
        Wind_Clear();
        Wind_SpawnGust((Vector3){0,0,0}, (Vector3){1,0,0}, 4.0f, 10.0f, 1.0f);
        TEST_CHECK(Wind_GetActiveCount() == 1, "Gust spawned for lifetime test");

        // Cập nhật 0.5s -> vận tốc giảm 1 nửa do temporal attenuation
        Wind_Update(0.5f);
        TEST_CHECK(Wind_GetActiveCount() == 1, "Still active after 0.5s");
        Vector3 vMid = Wind_EvaluateVelocity((Vector3){0,0,0}, 0.0f);
        TEST_NEAR(vMid.x, 5.0f, 0.05f, "Velocity halved after 50% lifetime elapsed");

        // Cập nhật tiếp 0.6s (tổng 1.1s > 1.0s) -> hết hạn, tự dọn dẹp
        Wind_Update(0.6f);
        TEST_CHECK(Wind_GetActiveCount() == 0, "Vorticle expired and active count returned to 0");
    }

    // 7. Test Giới hạn Bộ nhớ Ring-buffer (256 slots)
    {
        Wind_Clear();
        for (int i = 0; i < 300; i++) {
            Wind_SpawnRadialBlast((Vector3){(float)i, 0, 0}, 2.0f, 5.0f, 1.0f);
        }
        TEST_CHECK(Wind_GetActiveCount() == MAX_VORTICLES, "Active count capped at MAX_VORTICLES (256)");
        TEST_CHECK(FileContains("core/particles/gpu/particle_gpu_backend.c",
                                "#define MAX_GPU_VORTICLES MAX_VORTICLES"),
                   "GPU backend accepts the full CPU vorticle budget");
        TEST_CHECK(FileContains("core/particles/shaders/gpu/particle_gpu.comp",
                                "#define MAX_GPU_VORTICLES 256"),
                   "Compute shader accepts the full CPU vorticle budget");
    }

    // 8. Test Terrain-Aware Fluid Lift
    {
        Wind_Clear();
        WindMacroConfig slopeMacro = {
            .baseDirection = (Vector3){ 2.0f, 0.0f, 0.0f }, // gió thổi mạnh về +X
            .gustAmplitude = 0.0f,
            .noiseScale    = 0.05f,
            .noiseSpeed    = 1.0f,
            .terrainLiftK  = 1.5f,
        };
        Wind_SetMacro(&slopeMacro);
        Wind_SetTerrainHeightQuery(MockTerrainSlope, NULL);

        // Gió theo +X gặp dốc dương (dH/dx = 0.5)
        // lift = (dH / sampleDist) * speedXZ * terrainLiftK
        // dH = (x + 1.5) * 0.5 - x * 0.5 = 0.75
        // slope = 0.75 / 1.5 = 0.5
        // lift = 0.5 * 2.0 * 1.5 = 1.5 m/s
        Vector3 vLift = Wind_EvaluateVelocity((Vector3){ 0.0f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vLift.x, 2.0f, 0.05f, "Macro wind X is 2.0 m/s");
        TEST_NEAR(vLift.y, 1.5f, 0.05f, "Terrain lift generated upward velocity Y = 1.5 m/s");

        // Tháo callback địa hình
        Wind_SetTerrainHeightQuery(NULL, NULL);
        Vector3 vNoLift = Wind_EvaluateVelocity((Vector3){ 0.0f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vNoLift.y, 0.0f, 0.01f, "Terrain lift removed when query is NULL");
    }

    // 9. Test Đa tầng Vorticles (Interacting Micro-Vortices Superposition)
    {
        Wind_Clear();
        WindMacroConfig zeroMacro = {0};
        Wind_SetMacro(&zeroMacro);

        // Kích phát 2 Vorticles tương tác (1 radial blast + 1 vortex)
        Wind_SpawnRadialBlast((Vector3){0, 0, 0}, 4.0f, 6.0f, 1.0f);
        Wind_SpawnVortex((Vector3){0, 0, 0}, (Vector3){0, 1, 0}, 4.0f, 4.0f, 2.0f, 1.0f);
        TEST_CHECK(Wind_GetActiveCount() == 2, "Active count is 2 for interacting vorticles");

        // Điểm khảo sát chịu cả lực đẩy tâm và xoáy lốc
        Vector3 vCombined = Wind_EvaluateVelocity((Vector3){ 2.0f, 0.0f, 0.0f }, 0.0f);
        TEST_NEAR(vCombined.x, 2.0f, 0.05f, "Superposition of radial blast and inward pull");
        TEST_NEAR(vCombined.z, -2.0f, 0.05f, "Superposition preserves vortex rotation");
    }

    // 10. Dọn dẹp
    Wind_Unload();
    TEST_CHECK(Wind_GetActiveCount() == 0, "Cleaned up after unload");

    printf("\nTEST RESULT: %d PASSED, %d FAILED\n", s_passed, s_failed);
    return (s_failed == 0) ? 0 : 1;
}

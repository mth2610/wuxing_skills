#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "raylib.h"
typedef struct Camera3D { int unused; } Camera3D;
#include "core/gas/gas_system.h"

#ifndef PI
#define PI 3.1415926535f
#endif

typedef int VC_MaterialId;
typedef struct VFX_ElementMaterial {
    Color body;
    Color glow;
} VFX_ElementMaterial;
typedef struct {
    GasPriority priority;
    float radius;
    float height;
    float expandDuration;
    float decayDuration;
    float intensity;
    float ringsPerSecond;
    float outwardSpeed;
    float lift;
} VFX_GasShockwaveConfig;

#define TEST_MAX_INJECTIONS 512
#define TEST_SHOCKWAVE_SPOKES 16

static int s_mockNextGas = 1;
static GasVolumeHandle s_mockAliveGas;
static int s_mockInjectionCount;
static int s_mockDestroyCount;
static GasVolumeDesc s_mockLastVolume;
static GasInjection s_mockInjections[TEST_MAX_INJECTIONS];

static Vector3 Vector3Add(Vector3 a, Vector3 b) {
    return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue) {
    (void)key;
    *value = defaultValue;
    return true;
}

const VFX_ElementMaterial *VFX_Material(VC_MaterialId id) {
    (void)id;
    static const VFX_ElementMaterial material = {
        {82, 24, 128, 255}, {164, 64, 255, 255}
    };
    return &material;
}

GasVolumeDesc GasVolume_Preset(GasKind kind) {
    GasVolumeDesc desc = {0};
    desc.kind = kind;
    desc.densityScale = 1.0f;
    desc.emissionGain = kind == GAS_SMOKE ? 0.0f : 2.0f;
    return desc;
}

GasVolumeHandle GasVolume_Create(const GasVolumeDesc *desc) {
    if (desc == NULL) return GAS_VOLUME_INVALID;
    s_mockLastVolume = *desc;
    s_mockAliveGas = s_mockNextGas++;
    return s_mockAliveGas;
}

void GasVolume_Destroy(GasVolumeHandle handle) {
    if (handle == s_mockAliveGas) {
        s_mockAliveGas = GAS_VOLUME_INVALID;
        ++s_mockDestroyCount;
    }
}

bool GasVolume_IsAlive(GasVolumeHandle handle) {
    return handle != GAS_VOLUME_INVALID && handle == s_mockAliveGas;
}

void GasVolume_Inject(GasVolumeHandle handle, const GasInjection *injection) {
    if (!GasVolume_IsAlive(handle) || injection == NULL || injection->density <= 0.0f)
        return;
    if (s_mockInjectionCount < TEST_MAX_INJECTIONS)
        s_mockInjections[s_mockInjectionCount] = *injection;
    ++s_mockInjectionCount;
}

#include "core/composition/common/vc_gas_shockwave.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static int RunForHalfSecond(float fps) {
    VFX_GasShockwaveConfig config = VFX_GasShockwave_DefaultConfig();
    config.expandDuration = 0.8f;
    config.decayDuration = 0.5f;
    config.ringsPerSecond = 18.0f;
    int handle = VFX_GasShockwave_Spawn((Vector3){0}, 0, &config);
    if (handle == 0) return -1;
    int before = s_mockInjectionCount;
    for (int i = 0; i < (int)(fps * 0.5f); ++i)
        VC_GasShockwave_Update(1.0f / fps);
    int emitted = s_mockInjectionCount - before;
    VFX_KillGasShockwave(handle);
    return emitted;
}

static float MeanRadius(int first, int count, Vector3 center) {
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        GasInjection source = s_mockInjections[first + i];
        float x = source.position.x - center.x;
        float z = source.position.z - center.z;
        total += sqrtf(x * x + z * z);
    }
    return total / (float)count;
}

int main(void) {
    int at30 = RunForHalfSecond(30.0f);
    int at60 = RunForHalfSecond(60.0f);
    int at120 = RunForHalfSecond(120.0f);
    CHECK(at30 >= 8 * TEST_SHOCKWAVE_SPOKES,
          "shockwave must lay several complete rings during expansion");
    CHECK(abs(at30 - at60) <= TEST_SHOCKWAVE_SPOKES &&
          abs(at60 - at120) <= TEST_SHOCKWAVE_SPOKES,
          "shockwave injection must be frame-rate independent to one ring batch");

    VFX_GasShockwaveConfig config = VFX_GasShockwave_DefaultConfig();
    config.expandDuration = 0.8f;
    config.decayDuration = 0.4f;
    config.ringsPerSecond = 15.0f;
    Vector3 center = {2.0f, 1.0f, -3.0f};
    int first = s_mockInjectionCount;
    int handle = VFX_GasShockwave_Spawn(center, 0, &config);
    CHECK(handle != 0, "shockwave must return a lifecycle handle");
    CHECK(s_mockLastVolume.kind == GAS_ENERGY,
          "shockwave must use emissive simulated energy gas");
    CHECK(s_mockLastVolume.size.x > config.radius * 2.0f &&
          s_mockLastVolume.size.z > config.radius * 2.0f,
          "shockwave volume must contain its final ring with simulation margin");

    for (int i = 0; i < 5; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount - first == TEST_SHOCKWAVE_SPOKES,
          "one ring event must cover the full circumference with sixteen sources");

    bool hasPosX = false, hasNegX = false, hasPosZ = false, hasNegZ = false;
    float minRadius = 1000.0f, maxRadius = -1000.0f;
    float minSourceSize = 1000.0f, maxSourceSize = -1000.0f;
    for (int i = first; i < first + TEST_SHOCKWAVE_SPOKES; ++i) {
        GasInjection source = s_mockInjections[i];
        float rx = source.position.x - center.x;
        float rz = source.position.z - center.z;
        float sourceRadius = sqrtf(rx * rx + rz * rz);
        float radialVelocity = rx * source.velocity.x + rz * source.velocity.z;
        CHECK(radialVelocity > 0.0f,
              "every shockwave source must push gas radially outward");
        CHECK(source.velocity.y > 0.0f,
              "energy smoke must lift gently instead of sinking into the ground");
        CHECK(source.reaction > source.density,
              "shockwave must be reaction-led and visibly emissive");
        hasPosX |= rx > 0.01f;
        hasNegX |= rx < -0.01f;
        hasPosZ |= rz > 0.01f;
        hasNegZ |= rz < -0.01f;
        if (sourceRadius < minRadius) minRadius = sourceRadius;
        if (sourceRadius > maxRadius) maxRadius = sourceRadius;
        if (source.radius < minSourceSize) minSourceSize = source.radius;
        if (source.radius > maxSourceSize) maxSourceSize = source.radius;
    }
    CHECK(hasPosX && hasNegX && hasPosZ && hasNegZ,
          "one ring event must cover all four quadrants around the click point");
    CHECK(maxRadius - minRadius > config.radius * 0.015f,
          "multi-band noise must break the perfectly circular wavefront");
    CHECK(maxSourceSize - minSourceSize > config.radius * 0.01f,
          "fine grain must vary source thickness around the ring");
    float earlyRadius = MeanRadius(first, TEST_SHOCKWAVE_SPOKES, center);

    for (int i = 0; i < 24; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
    int lastBatch = s_mockInjectionCount - TEST_SHOCKWAVE_SPOKES;
    float lateRadius = MeanRadius(lastBatch, TEST_SHOCKWAVE_SPOKES, center);
    CHECK(lateRadius > earlyRadius + config.radius * 0.45f,
          "successive ring batches must expand visibly away from the origin");

    int beforeStop = s_mockInjectionCount;
    VFX_GasShockwave_Stop(handle);
    for (int i = 0; i < 10; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount == beforeStop,
          "Stop must end shockwave feeding immediately");
    CHECK(GasVolume_IsAlive(s_mockAliveGas),
          "Stop must preserve the already-laid energy smoke");
    for (int i = 0; i < 30; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
    CHECK(!GasVolume_IsAlive(s_mockAliveGas),
          "shockwave must release its volume after dissipation");
    CHECK(s_mockDestroyCount >= 4, "every test shockwave must release its gas handle");

    puts("gas_shockwave_runtime_test: PASS");
    return 0;
}

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
    GasKind kind;
    GasPriority priority;
    float radius;
    float height;
    float emitDuration;
    float decayDuration;
    float intensity;
    float pulsesPerSecond;
    Vector3 wind;
    float detailStrength;
    float shadowStrength;
    float backgroundAdapt;
    bool usePresetPalette;
} VFX_GasPlumeConfig;

static int s_mockNextGas = 1;
static GasVolumeHandle s_mockAliveGas;
static int s_mockInjectionCount;
static int s_mockDestroyCount;
static GasVolumeDesc s_mockLastVolume;
static GasInjection s_mockLastInjection;
static bool s_mockHasInjection;
#define MOCK_INJECTION_CAPACITY 64
static GasInjection s_mockInjections[MOCK_INJECTION_CAPACITY];
static int s_mockStoredInjectionCount;

static Vector3 Vector3Add(Vector3 a, Vector3 b) {
    return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vector3 Vector3Scale(Vector3 value, float scale) {
    return (Vector3){value.x * scale, value.y * scale, value.z * scale};
}

static Vector3 VC_RingPointXZ(Vector3 center, float radius, float angle) {
    return (Vector3){center.x + cosf(angle) * radius,
                     center.y,
                     center.z + sinf(angle) * radius};
}

static Vector3 VC_TangentXZ(float angle, float up) {
    return (Vector3){-sinf(angle), up, cosf(angle)};
}

bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue) {
    (void)key;
    *value = defaultValue;
    return true;
}

const VFX_ElementMaterial *VFX_Material(VC_MaterialId id) {
    (void)id;
    static const VFX_ElementMaterial material = {
        {180, 56, 22, 255}, {255, 150, 40, 255}
    };
    return &material;
}

GasVolumeDesc GasVolume_Preset(GasKind kind) {
    GasVolumeDesc desc = {0};
    desc.kind = kind;
    desc.densityScale = 1.0f;
    desc.emissionGain = kind == GAS_SMOKE ? 0.0f : 2.0f;
    desc.bodyColor = kind == GAS_ENERGY ? (Color){28, 38, 78, 255} :
                     (kind == GAS_FIRE ? (Color){72, 35, 22, 255} :
                                         (Color){214, 218, 224, 255});
    desc.emissionColor = kind == GAS_ENERGY ? (Color){78, 176, 255, 255} :
                         (kind == GAS_FIRE ? (Color){255, 104, 18, 255} :
                                             (Color){0, 0, 0, 255});
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
    if (GasVolume_IsAlive(handle) && injection != NULL && injection->density > 0.0f) {
        s_mockLastInjection = *injection;
        s_mockHasInjection = true;
        if (s_mockStoredInjectionCount < MOCK_INJECTION_CAPACITY)
            s_mockInjections[s_mockStoredInjectionCount++] = *injection;
        ++s_mockInjectionCount;
    }
}

#include "core/composition/common/vc_gas_plume.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static int RunForOneSecond(float fps) {
    VFX_GasPlumeConfig config = VFX_GasPlume_DefaultConfig(GAS_FIRE);
    config.emitDuration = 2.0f;
    config.decayDuration = 1.0f;
    config.pulsesPerSecond = 12.0f;
    int handle = VFX_GasPlume_Spawn((Vector3){0}, 0, &config);
    if (handle == 0) return -1;
    int before = s_mockInjectionCount;
    int frames = (int)fps;
    for (int i = 0; i < frames; ++i) VC_GasPlume_Update(1.0f / fps);
    int emitted = s_mockInjectionCount - before;
    VFX_KillGasPlume(handle);
    return emitted;
}

int main(void) {
    int at30 = RunForOneSecond(30.0f);
    int at60 = RunForOneSecond(60.0f);
    int at120 = RunForOneSecond(120.0f);
    CHECK(at30 >= 11 && at30 <= 12, "30 fps plume must emit at the authored rate");
    CHECK(abs(at30 - at60) <= 1 && abs(at60 - at120) <= 1,
          "actual plume injection must be frame-rate independent");

    VFX_GasPlumeConfig grounded = VFX_GasPlume_DefaultConfig(GAS_FIRE);
    Vector3 requestedPosition = {1.0f, 2.0f, 3.0f};
    s_mockHasInjection = false;
    s_mockStoredInjectionCount = 0;
    int groundedHandle = VFX_GasPlume_Spawn(requestedPosition, 0, &grounded);
    CHECK(groundedHandle != 0, "grounded plume must spawn");
    for (int i = 0; i < 60 && s_mockStoredInjectionCount < 12; ++i)
        VC_GasPlume_Update(1.0f / 60.0f);
    CHECK(s_mockStoredInjectionCount >= 12,
          "grounded plume must feed enough micro-sources to measure its footprint");
    float minX = 1000.0f, maxX = -1000.0f;
    float minZ = 1000.0f, maxZ = -1000.0f;
    float meanX = 0.0f, meanZ = 0.0f;
    for (int i = 0; i < s_mockStoredInjectionCount; ++i) {
        GasInjection injection = s_mockInjections[i];
        float dx = injection.position.x - requestedPosition.x;
        float dz = injection.position.z - requestedPosition.z;
        float radialOffset = sqrtf(dx * dx + dz * dz);
        float radialVelocity = dx * injection.velocity.x +
                               dz * injection.velocity.z;
        float circulation = dz * injection.velocity.x -
                            dx * injection.velocity.z;
        CHECK(fabsf(injection.position.y - requestedPosition.y) < 0.0001f,
              "micro-sources must remain grounded at the requested world height");
        CHECK(radialOffset <= grounded.radius * 0.30f,
              "micro-source orbit must stay inside the authored plume foot");
        CHECK(fabsf(radialVelocity) <= 0.0001f,
              "micro-source horizontal velocity must be tangent to its orbit");
        CHECK(fabsf(circulation) >= grounded.radius * grounded.radius * 0.01f,
              "micro-source orbit must inject measurable circulation");
        if (injection.position.x < minX) minX = injection.position.x;
        if (injection.position.x > maxX) maxX = injection.position.x;
        if (injection.position.z < minZ) minZ = injection.position.z;
        if (injection.position.z > maxZ) maxZ = injection.position.z;
        meanX += dx;
        meanZ += dz;
    }
    meanX /= (float)s_mockStoredInjectionCount;
    meanZ /= (float)s_mockStoredInjectionCount;
    CHECK(maxX - minX >= grounded.radius * 0.25f &&
          maxZ - minZ >= grounded.radius * 0.25f,
          "micro-sources must open an asymmetric two-axis footprint");
    CHECK(sqrtf(meanX * meanX + meanZ * meanZ) <= grounded.radius * 0.08f,
          "golden-angle source sequence must keep the plume centered on average");
    float volumeMinY = s_mockLastVolume.center.y - s_mockLastVolume.size.y * 0.5f;
    CHECK(volumeMinY < requestedPosition.y,
          "gas volume must reserve space below the source so its sphere is not clipped upward");
    CHECK(grounded.pulsesPerSecond >= 24.0f,
          "default fire feed must be continuous enough to avoid descending pulse lobes");
    VFX_KillGasPlume(groundedHandle);

    VFX_GasPlumeConfig optical = VFX_GasPlume_DefaultConfig(GAS_ENERGY);
    optical.detailStrength = 1.75f;
    optical.shadowStrength = -1.0f;
    optical.backgroundAdapt = 0.65f;
    optical.usePresetPalette = true;
    int opticalHandle = VFX_GasPlume_Spawn((Vector3){0}, 0, &optical);
    CHECK(opticalHandle != 0, "authored optical plume must spawn");
    CHECK(fabsf(s_mockLastVolume.detailStrength - 1.75f) < 0.0001f &&
          fabsf(s_mockLastVolume.shadowStrength + 1.0f) < 0.0001f &&
          fabsf(s_mockLastVolume.backgroundAdapt - 0.65f) < 0.0001f,
          "plume config must forward bounded optical controls to the gas volume");
    CHECK(s_mockLastVolume.bodyColor.r == 28 &&
          s_mockLastVolume.emissionColor.b == 255,
          "preset-palette mode must retain the selected GasKind identity");
    VFX_KillGasPlume(opticalHandle);

    /* Phase-0 capture control: one fixture can exercise each real GasKind path
     * without adding three near-identical sandbox entries. The override must
     * select the complete preset, not merely recolor a FIRE injection. */
    s_gasPlumeKindOverride = (float)GAS_SMOKE;
    s_mockHasInjection = false;
    VFX_GasPlumeConfig forced = VFX_GasPlume_DefaultConfig(GAS_FIRE);
    int forcedHandle = VFX_GasPlume_Spawn((Vector3){0}, 0, &forced);
    CHECK(forcedHandle != 0, "diagnostic kind override must still spawn");
    CHECK(s_mockLastVolume.kind == GAS_SMOKE,
          "diagnostic kind override must select the smoke volume preset");
    CHECK(fabsf(s_mockLastVolume.size.x - 2.2f) < 0.0001f,
          "diagnostic kind override must select the complete smoke plume defaults");
    CHECK(s_mockLastVolume.bodyColor.r == 214 && s_mockLastVolume.bodyColor.g == 218,
          "diagnostic kind override must preserve the preset palette");
    for (int i = 0; i < 20 && !s_mockHasInjection; ++i)
        VC_GasPlume_Update(1.0f / 60.0f);
    CHECK(s_mockHasInjection && s_mockLastInjection.reaction == 0.0f,
          "forced smoke must use smoke injection semantics, not fire reaction");
    VFX_KillGasPlume(forcedHandle);
    s_gasPlumeKindOverride = -1.0f;

    VFX_GasPlumeConfig config = VFX_GasPlume_DefaultConfig(GAS_SMOKE);
    config.emitDuration = 5.0f;
    config.decayDuration = 0.5f;
    int handle = VFX_GasPlume_Spawn((Vector3){0}, 0, &config);
    CHECK(handle != 0, "plume spawn must return a handle");
    for (int i = 0; i < 20; ++i) VC_GasPlume_Update(1.0f / 60.0f);
    int beforeStop = s_mockInjectionCount;
    VFX_GasPlume_Stop(handle);
    for (int i = 0; i < 10; ++i) VC_GasPlume_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount == beforeStop, "Stop must end gas feeding immediately");
    CHECK(GasVolume_IsAlive(s_mockAliveGas), "Stop must preserve the dissipating volume");
    for (int i = 0; i < 30; ++i) VC_GasPlume_Update(1.0f / 60.0f);
    CHECK(!GasVolume_IsAlive(s_mockAliveGas), "plume must release gas after decay");
    CHECK(s_mockDestroyCount >= 5, "every test plume must release its gas handle");

    puts("gas_plume_runtime_test: PASS");
    return 0;
}

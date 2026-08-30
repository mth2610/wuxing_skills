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
    float emitDuration;
    float decayDuration;
    float intensity;
    float pulsesPerSecond;
    float speed;
    float turbulence;
    float lift;
    unsigned int seed;
} VFX_FlameJetConfig;

#define TEST_MAX_INJECTIONS 1024
#define TEST_FLAME_JET_LOBES 4

static int s_mockNextGas = 1;
static GasVolumeHandle s_mockAliveGas;
static int s_mockInjectionCount;
static int s_mockDestroyCount;
static GasVolumeDesc s_mockLastVolume;
static GasInjection s_mockInjections[TEST_MAX_INJECTIONS];

static Vector3 Vector3Add(Vector3 a, Vector3 b) {
    return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vector3 Vector3Subtract(Vector3 a, Vector3 b) {
    return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vector3 Vector3Scale(Vector3 v, float scale) {
    return (Vector3){v.x * scale, v.y * scale, v.z * scale};
}

static float Vector3Length(Vector3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Vector3 Vector3Normalize(Vector3 v) {
    float length = Vector3Length(v);
    return length > 0.000001f ? Vector3Scale(v, 1.0f / length) : (Vector3){0};
}

static Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
    return (Vector3){a.y * b.z - a.z * b.y,
                     a.z * b.x - a.x * b.z,
                     a.x * b.y - a.y * b.x};
}

static float Vector3DotProduct(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue) {
    (void)key;
    *value = defaultValue;
    return true;
}

const VFX_ElementMaterial *VFX_Material(VC_MaterialId id) {
    (void)id;
    static const VFX_ElementMaterial material = {
        {78, 31, 18, 255}, {255, 105, 20, 255}
    };
    return &material;
}

GasVolumeDesc GasVolume_Preset(GasKind kind) {
    GasVolumeDesc desc = {0};
    desc.kind = kind;
    desc.densityScale = 1.0f;
    desc.emissionGain = kind == GAS_FIRE ? 4.0f : 0.0f;
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

#include "core/composition/common/vc_flame_jet.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static int RunForHalfSecond(float fps) {
    VFX_FlameJetConfig config = VFX_FlameJet_DefaultConfig();
    config.emitDuration = 0.8f;
    config.decayDuration = 0.35f;
    config.pulsesPerSecond = 24.0f;
    int handle = VFX_FlameJet_Spawn((Vector3){0.0f, 0.6f, 0.0f},
                                    (Vector3){4.0f, 0.8f, 0.4f}, 0, &config);
    if (handle == 0) return -1;
    int before = s_mockInjectionCount;
    for (int i = 0; i < (int)(fps * 0.5f); ++i)
        VC_FlameJet_Update(1.0f / fps);
    int emitted = s_mockInjectionCount - before;
    VFX_KillFlameJet(handle);
    return emitted;
}

int main(void) {
    int at30 = RunForHalfSecond(30.0f);
    int at60 = RunForHalfSecond(60.0f);
    int at120 = RunForHalfSecond(120.0f);
    CHECK(at30 >= 10 * TEST_FLAME_JET_LOBES,
          "flame jet must lay a continuous multi-lobe body during its first half second");
    CHECK(abs(at30 - at60) <= TEST_FLAME_JET_LOBES &&
          abs(at60 - at120) <= TEST_FLAME_JET_LOBES,
          "flame jet injection must be frame-rate independent to one pulse batch");

    VFX_FlameJetConfig config = VFX_FlameJet_DefaultConfig();
    config.emitDuration = 0.7f;
    config.decayDuration = 0.4f;
    config.pulsesPerSecond = 20.0f;
    Vector3 start = {1.0f, 0.7f, -2.0f};
    Vector3 end = {4.8f, 1.0f, -1.1f};
    Vector3 axis = Vector3Normalize(Vector3Subtract(end, start));
    float length = Vector3Length(Vector3Subtract(end, start));
    int first = s_mockInjectionCount;
    int handle = VFX_FlameJet_Spawn(start, end, 0, &config);
    CHECK(handle != 0, "flame jet must return a lifecycle handle");
    CHECK(s_mockLastVolume.kind == GAS_FIRE,
          "flame jet must use simulated fire rather than energy gas");

    Vector3 volumeMin = Vector3Subtract(s_mockLastVolume.center,
                                        Vector3Scale(s_mockLastVolume.size, 0.5f));
    Vector3 volumeMax = Vector3Add(s_mockLastVolume.center,
                                   Vector3Scale(s_mockLastVolume.size, 0.5f));
    CHECK(start.x >= volumeMin.x && start.y >= volumeMin.y && start.z >= volumeMin.z &&
          end.x <= volumeMax.x && end.y <= volumeMax.y && end.z <= volumeMax.z,
          "flame jet volume must contain both authored endpoints");

    for (int i = 0; i < 4; ++i) VC_FlameJet_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount - first >= TEST_FLAME_JET_LOBES,
          "the first pulse must populate the full jet instead of growing from one blob");

    float firstAlong = 1000.0f, lastAlong = -1000.0f;
    float baseRadius = 0.0f, tipRadius = 0.0f;
    float largestLateral = 0.0f;
    for (int i = 0; i < TEST_FLAME_JET_LOBES; ++i) {
        GasInjection source = s_mockInjections[first + i];
        Vector3 relative = Vector3Subtract(source.position, start);
        float along = Vector3DotProduct(relative, axis);
        Vector3 lateral = Vector3Subtract(relative, Vector3Scale(axis, along));
        float forwardSpeed = Vector3DotProduct(source.velocity, axis);
        CHECK(forwardSpeed > config.speed * 0.65f,
              "every lobe must travel primarily toward the target");
        CHECK(source.reaction > source.density,
              "flame jet must be reaction-led so its core reads as fire");
        if (along < firstAlong) { firstAlong = along; baseRadius = source.radius; }
        if (along > lastAlong) { lastAlong = along; tipRadius = source.radius; }
        float lateralDistance = Vector3Length(lateral);
        if (lateralDistance > largestLateral) largestLateral = lateralDistance;
    }
    CHECK(firstAlong < length * 0.22f && lastAlong > length * 0.78f,
          "one pulse must span from the nozzle to the flame front");
    CHECK(tipRadius > baseRadius * 1.45f,
          "flame jet must widen toward its front instead of reading as a cylinder");
    CHECK(largestLateral > config.radius * 0.025f,
          "turbulence must break the perfectly straight centreline");

    int beforeStop = s_mockInjectionCount;
    VFX_FlameJet_Stop(handle);
    for (int i = 0; i < 8; ++i) VC_FlameJet_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount == beforeStop,
          "Stop must end flame feeding immediately");
    CHECK(GasVolume_IsAlive(s_mockAliveGas),
          "Stop must preserve the hot smoke already in the volume");
    for (int i = 0; i < 30; ++i) VC_FlameJet_Update(1.0f / 60.0f);
    CHECK(!GasVolume_IsAlive(s_mockAliveGas),
          "flame jet must release its volume after the cooling tail");
    CHECK(s_mockDestroyCount >= 4, "every test flame jet must release its gas handle");

    puts("flame_jet_runtime_test: PASS");
    return 0;
}

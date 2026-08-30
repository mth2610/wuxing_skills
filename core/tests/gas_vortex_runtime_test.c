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
    float emitDuration;
    float decayDuration;
    float intensity;
    float pulsesPerSecond;
    float angularSpeed;
    float lift;
} VFX_GasVortexConfig;

#define TEST_MAX_INJECTIONS 160

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
        {38, 46, 105, 255}, {92, 190, 255, 255}
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

#include "core/composition/common/vc_gas_vortex.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static int RunForOneSecond(float fps) {
    VFX_GasVortexConfig config = VFX_GasVortex_DefaultConfig();
    config.emitDuration = 2.0f;
    config.decayDuration = 1.0f;
    config.pulsesPerSecond = 30.0f;
    int handle = VFX_GasVortex_Spawn((Vector3){0}, 0, &config);
    if (handle == 0) return -1;
    int before = s_mockInjectionCount;
    for (int i = 0; i < (int)fps; ++i) VC_GasVortex_Update(1.0f / fps);
    int emitted = s_mockInjectionCount - before;
    VFX_KillGasVortex(handle);
    return emitted;
}

int main(void) {
    int at30 = RunForOneSecond(30.0f);
    int at60 = RunForOneSecond(60.0f);
    int at120 = RunForOneSecond(120.0f);
    CHECK(at30 >= 29 && at30 <= 30, "vortex must emit at its authored rate");
    CHECK(abs(at30 - at60) <= 1 && abs(at60 - at120) <= 1,
          "vortex injection must be frame-rate independent");

    VFX_GasVortexConfig config = VFX_GasVortex_DefaultConfig();
    config.emitDuration = 3.0f;
    config.decayDuration = 0.4f;
    Vector3 center = {2.0f, 1.0f, -3.0f};
    int first = s_mockInjectionCount;
    int handle = VFX_GasVortex_Spawn(center, 0, &config);
    CHECK(handle != 0, "vortex must return a lifecycle handle");
    CHECK(s_mockLastVolume.kind == GAS_ENERGY,
          "vortex must use the emissive energy-gas preset");
    CHECK(s_mockLastVolume.size.x > config.radius * 2.0f &&
          s_mockLastVolume.size.z > config.radius * 2.0f,
          "vortex volume must contain its orbit with simulation margin");

    for (int i = 0; i < 20; ++i) VC_GasVortex_Update(1.0f / 60.0f);
    int count = s_mockInjectionCount - first;
    CHECK(count >= 8, "vortex must lay enough moving sources to read as a coil");

    float minX = 1000.0f;
    float maxX = -1000.0f;
    float minZ = 1000.0f;
    float maxZ = -1000.0f;
    float minY = 1000.0f;
    float maxY = -1000.0f;
    for (int i = first; i < s_mockInjectionCount && i < TEST_MAX_INJECTIONS; ++i) {
        GasInjection source = s_mockInjections[i];
        float rx = source.position.x - center.x;
        float rz = source.position.z - center.z;
        float orbitRadius = sqrtf(rx * rx + rz * rz);
        float tangential = -rz * source.velocity.x + rx * source.velocity.z;
        float radial = rx * source.velocity.x + rz * source.velocity.z;
        CHECK(orbitRadius > config.radius * 0.45f &&
              orbitRadius < config.radius * 0.75f,
              "every source must sit on the authored orbit, not at plume center");
        CHECK(tangential > 0.0f, "every source velocity must turn around the vortex");
        CHECK(radial < 0.0f, "every source velocity must pull gas into the vortex core");
        CHECK(source.velocity.y > 0.0f, "the vortex must still lift simulated gas");
        CHECK(source.radius >= config.radius * 0.24f,
              "moving sources must overlap by several mobile-grid cells");
        CHECK(source.reaction > source.density,
              "energy vortex must be reaction-led and visibly emissive");
        if (source.position.x < minX) minX = source.position.x;
        if (source.position.x > maxX) maxX = source.position.x;
        if (source.position.z < minZ) minZ = source.position.z;
        if (source.position.z > maxZ) maxZ = source.position.z;
        if (source.position.y < minY) minY = source.position.y;
        if (source.position.y > maxY) maxY = source.position.y;
    }
    CHECK(maxX - minX > config.radius * 0.5f &&
          maxZ - minZ > config.radius * 0.5f,
          "successive sources must travel around the ring instead of stacking vertically");
    CHECK(maxY - minY > config.height * 0.05f,
          "the orbit must climb into a helix instead of remaining a flat ring");

    int beforeStop = s_mockInjectionCount;
    VFX_GasVortex_Stop(handle);
    for (int i = 0; i < 10; ++i) VC_GasVortex_Update(1.0f / 60.0f);
    CHECK(s_mockInjectionCount == beforeStop, "Stop must end vortex feeding immediately");
    CHECK(GasVolume_IsAlive(s_mockAliveGas), "Stop must preserve the dissipating gas");
    for (int i = 0; i < 30; ++i) VC_GasVortex_Update(1.0f / 60.0f);
    CHECK(!GasVolume_IsAlive(s_mockAliveGas), "vortex must release its volume after decay");
    CHECK(s_mockDestroyCount >= 4, "every test vortex must release its gas handle");

    puts("gas_vortex_runtime_test: PASS");
    return 0;
}

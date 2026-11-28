#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "raylib.h"
typedef struct Camera3D { int unused; } Camera3D;
#include "core/gas/gas_system.h"
#include "core/composition/common/vc_params.h"

#ifndef PI
#define PI 3.1415926535f
#endif

typedef int VC_MaterialId;
typedef struct VFX_ElementMaterial {
    Color body;
    Color glow;
} VFX_ElementMaterial;
typedef enum {
    VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT,
    VFX_GAS_SHOCKWAVE_PRESET_DUST_SURGE,
    VFX_GAS_SHOCKWAVE_PRESET_THERMAL_BLOOM,
    VFX_GAS_SHOCKWAVE_PRESET_VORTEX_WAKE,
    VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE,
    VFX_GAS_SHOCKWAVE_PRESET_COUNT
} VFX_GasShockwavePreset;

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
    VFX_GasShockwavePreset preset;
} VFX_GasShockwaveConfig;

#define TEST_MAX_INJECTIONS 512
#define TEST_SHOCKWAVE_SPOKES 16

static int s_mockNextGas = 1;
static GasVolumeHandle s_mockAliveGas;
static int s_mockInjectionCount;
static int s_mockDestroyCount;
static float s_mockVisibility;
static bool s_mockRejectCreate;
static GasVolumeDesc s_mockLastVolume;
static GasInjection s_mockInjections[TEST_MAX_INJECTIONS];

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
    if (desc == NULL || s_mockRejectCreate) return GAS_VOLUME_INVALID;
    s_mockLastVolume = *desc;
    s_mockVisibility = 1.0f;
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

void GasVolume_SetVisibility(GasVolumeHandle handle, float visibility) {
    if (GasVolume_IsAlive(handle)) s_mockVisibility = visibility;
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
    s_mockInjectionCount = 0;
    for (int i = 0; i < (int)(fps * 0.5f); ++i)
        VC_GasShockwave_Update(1.0f / fps);
    int emitted = s_mockInjectionCount;
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

static int TestPresets(void) {
    const GasKind kinds[] = {GAS_ENERGY, GAS_SMOKE, GAS_FIRE, GAS_ENERGY, GAS_ENERGY};
    for (int p = 0; p < VFX_GAS_SHOCKWAVE_PRESET_COUNT; ++p) {
        VFX_GasShockwaveConfig config;
        VFX_GasShockwave_ApplyPreset(&config, (VFX_GasShockwavePreset)p);
        CHECK(config.preset == (VFX_GasShockwavePreset)p &&
              VFX_GasShockwavePreset_Name(config.preset)[0] != '\0',
              "all guided variants must apply a named configuration");
        s_mockInjectionCount = 0;
        int handle = VFX_GasShockwave_Spawn((Vector3){0}, 0, &config);
        CHECK(handle && s_mockLastVolume.kind == kinds[p], "preset must choose its gas material");
        config.intensity = 0.0f;
        CHECK(VFX_GasShockwave_Spawn((Vector3){0}, 0, &config) == 0 &&
              GasVolume_IsAlive(s_mockAliveGas), "zero intensity must not replace an active gas volume");
        for (int i = 0; i < 60; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
        CHECK(s_mockInjectionCount > 4 * TEST_SHOCKWAVE_SPOKES, "preset must emit from its snapshot");
        if (p == VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE) {
            bool returnedInward = false;
            float previous = 0.0f;
            for (int i = 0; i < s_mockInjectionCount; i += TEST_SHOCKWAVE_SPOKES) {
                float radius = MeanRadius(i, TEST_SHOCKWAVE_SPOKES, (Vector3){0});
                if (previous - radius > config.radius * 0.5f) returnedInward = true;
                previous = radius;
            }
            CHECK(returnedInward, "the second pulse must launch inside the first pulse");
        }
        VFX_KillGasShockwave(handle);
    }
    return 0;
}

static int TestHitchAndAdmission(void) {
    VFX_GasShockwaveConfig config = VFX_GasShockwave_DefaultConfig();
    config.ringsPerSecond = 48.0f;
    s_mockInjectionCount = 0;
    int handle = VFX_GasShockwave_Spawn((Vector3){0}, 0, &config);
    CHECK(handle != 0, "initial cast must be admitted");
    s_mockRejectCreate = true;
    CHECK(VFX_GasShockwave_Spawn((Vector3){0}, 0, &config) == 0,
          "failed admission must return an invalid shockwave handle");
    s_mockRejectCreate = false;
    VC_GasShockwave_Update(0.25f);
    CHECK(s_mockInjectionCount == 4 * TEST_SHOCKWAVE_SPOKES,
          "an admitted cast must keep feeding with a bounded catch-up budget");
    float first = MeanRadius(0, TEST_SHOCKWAVE_SPOKES, (Vector3){0});
    float last = MeanRadius(3 * TEST_SHOCKWAVE_SPOKES, TEST_SHOCKWAVE_SPOKES, (Vector3){0});
    CHECK(first > config.radius * 0.4f && last > first + config.radius * 0.09f,
          "a hitch must preserve distinct radii and skip the oldest overdue rings");
    VFX_KillGasShockwave(handle);
    return 0;
}

int main(void) {
    int at30 = RunForHalfSecond(30.0f);
    GasInjection frame30[TEST_MAX_INJECTIONS];
    CHECK(at30 > 0 && at30 <= TEST_MAX_INJECTIONS, "30 Hz test source budget");
    for (int i = 0; i < at30; ++i) frame30[i] = s_mockInjections[i];
    int at60 = RunForHalfSecond(60.0f);
    CHECK(at30 == at60, "30 Hz and 60 Hz must emit the same scheduled rings");
    for (int i = 0; i < at30; ++i) {
        CHECK(frame30[i].position.x == s_mockInjections[i].position.x &&
              frame30[i].position.z == s_mockInjections[i].position.z &&
              frame30[i].density == s_mockInjections[i].density,
              "scheduled ring positions and density must be frame-rate independent");
    }
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
    s_mockInjectionCount = 0;
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
    CHECK(s_mockVisibility > 0.0f && s_mockVisibility < 1.0f,
          "Stop must fade optical depth before destroying the wake");
    for (int i = 0; i < 30; ++i) VC_GasShockwave_Update(1.0f / 60.0f);
    CHECK(!GasVolume_IsAlive(s_mockAliveGas),
          "shockwave must release its volume after dissipation");
    CHECK(s_mockDestroyCount >= 4, "every test shockwave must release its gas handle");

    CHECK(TestPresets() == 0, "named presets must run through the shared lifecycle");
    CHECK(TestHitchAndAdmission() == 0, "a hitch or rejected cast must preserve a valid front");
    puts("gas_shockwave_runtime_test: PASS");
    return 0;
}

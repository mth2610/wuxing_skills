#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
typedef struct Camera3D { int unused; } Camera3D;
#include "core/gas/gas_system.h"

typedef int VC_MaterialId;
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

static int s_spawnCount;
static int s_killCount;
static GasKind s_spawnKinds[8];
static VFX_GasPlumeConfig s_lastConfig;
static int s_nextPlumeHandle = 1;

#define LOG_INFO 1
static void TraceLog(int level, const char *format, ...) {
    (void)level;
    (void)format;
}

bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue) {
    (void)key;
    *value = defaultValue;
    return true;
}

VFX_GasPlumeConfig VFX_GasPlume_DefaultConfig(GasKind kind) {
    VFX_GasPlumeConfig config = {0};
    config.kind = kind;
    config.priority = GAS_PRIORITY_CAST;
    return config;
}

int VFX_GasPlume_Spawn(Vector3 pos, VC_MaterialId mat,
                       const VFX_GasPlumeConfig *config) {
    (void)pos;
    (void)mat;
    if (config == NULL) return 0;
    if (s_spawnCount < 8) s_spawnKinds[s_spawnCount] = config->kind;
    ++s_spawnCount;
    s_lastConfig = *config;
    return s_nextPlumeHandle++;
}

void VFX_KillGasPlume(int handle) {
    if (handle != 0) ++s_killCount;
}

#include "core/composition/common/vc_gas_material_lab.inl"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_material_lab_test: %s (line %d)\n", message, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void) {
    int handle = VFX_ComposeGasMaterialLab((Vector3){1.0f, 2.0f, 3.0f}, 0);
    CHECK(handle != 0, "lab must return a lifecycle handle");
    CHECK(s_spawnCount == 1 && s_spawnKinds[0] == GAS_SMOKE,
          "the first one-second stage must be smoke");
    CHECK(s_lastConfig.usePresetPalette,
          "the diagnostic must show the real kind palette by default");
    CHECK(s_lastConfig.radius >= 5.5f && s_lastConfig.height >= 14.0f,
          "the diagnostic must be substantially larger than gameplay gas");

    VC_GasMaterialLab_Update(0.99f);
    CHECK(s_spawnCount == 1, "smoke must remain active before the first boundary");
    VC_GasMaterialLab_Update(0.02f);
    CHECK(s_spawnCount == 2 && s_spawnKinds[1] == GAS_FIRE,
          "the second one-second stage must be fire");
    VC_GasMaterialLab_Update(1.0f);
    CHECK(s_spawnCount == 3 && s_spawnKinds[2] == GAS_ENERGY,
          "the third one-second stage must be energy");
    VC_GasMaterialLab_Update(1.0f);
    CHECK(s_spawnCount == 4 && s_spawnKinds[3] == GAS_SMOKE,
          "the lab must loop deterministically back to smoke");
    CHECK(s_killCount == 3, "each stage boundary must retire the previous volume");

    s_gasMaterialLabStageOverride = (float)GAS_ENERGY;
    VC_GasMaterialLab_Update(0.01f);
    CHECK(s_spawnKinds[s_spawnCount - 1] == GAS_ENERGY,
          "stage override must freeze the requested material");
    int beforeTuningChange = s_spawnCount;
    s_gasMaterialLabDetail = 1.6f;
    VC_GasMaterialLab_Update(0.01f);
    CHECK(s_spawnCount == beforeTuningChange + 1,
          "live optical tuning must rebuild the frozen diagnostic stage");
    CHECK(fabsf(s_lastConfig.detailStrength - 1.6f) < 0.0001f,
          "rebuilt stage must receive the edited optical control");

    VFX_KillGasMaterialLab(handle);
    CHECK(s_killCount == 6, "stage changes and lab kill must release their gas volumes");
    puts("gas_material_lab_test: PASS");
    return 0;
}

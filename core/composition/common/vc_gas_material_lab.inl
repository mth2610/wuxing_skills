// GAS MATERIAL LAB — deterministic optical showcase for the mobile gas core.
//
// This is intentionally a diagnostic composition, not a gameplay attack. It
// reuses VFX_GasPlume_Spawn so the lab and shipping effects cannot drift onto
// different injection paths. One admitted gas volume cycles every second:
// frames 40/90/140 at 60 Hz therefore show smoke/fire/energy respectively in
// the standard bright-background matrix.

#include "core/tuning.h"

#define GAS_MATERIAL_LAB_TAG_BASE 0x6B000
#define GAS_MATERIAL_LAB_STAGE_COUNT 3

typedef struct {
    bool active;
    int serial;
    int gasPlumeHandle;
    int stage;
    Vector3 position;
    VC_MaterialId material;
    float elapsed;
    float appliedStageDuration;
    float appliedMaterialPalette;
    float appliedDetail;
    float appliedShadow;
    float appliedBackgroundAdapt;
    float appliedScale;
} VC_GasMaterialLab;

static VC_GasMaterialLab s_gasMaterialLab;
static int s_gasMaterialLabSerial;
static bool s_gasMaterialLabTuningReady;
static float s_gasMaterialLabStageDuration = 1.0f;
static float s_gasMaterialLabStageOverride = -1.0f;
static float s_gasMaterialLabMaterialPalette = 0.0f;
static float s_gasMaterialLabDetail = 0.0f;
static float s_gasMaterialLabShadow = 0.0f;
static float s_gasMaterialLabBackgroundAdapt = 0.0f;
static float s_gasMaterialLabScale = 2.5f;

static void GasMaterialLab_EnsureTuning(void)
{
    if (s_gasMaterialLabTuningReady) return;
    s_gasMaterialLabTuningReady = true;
    Tuning_RegisterFloat("gaslab_stage_duration", &s_gasMaterialLabStageDuration, 1.0f);
    Tuning_RegisterFloat("gaslab_stage_override", &s_gasMaterialLabStageOverride, -1.0f);
    Tuning_RegisterFloat("gaslab_material_palette", &s_gasMaterialLabMaterialPalette, 0.0f);
    Tuning_RegisterFloat("gaslab_detail", &s_gasMaterialLabDetail, 0.0f);
    Tuning_RegisterFloat("gaslab_shadow", &s_gasMaterialLabShadow, 0.0f);
    Tuning_RegisterFloat("gaslab_background_adapt", &s_gasMaterialLabBackgroundAdapt, 0.0f);
    Tuning_RegisterFloat("gaslab_scale", &s_gasMaterialLabScale, 2.5f);
}

static int GasMaterialLab_Handle(const VC_GasMaterialLab *lab)
{
    return GAS_MATERIAL_LAB_TAG_BASE + (lab->serial & 0x0FFF);
}

static VC_GasMaterialLab *GasMaterialLab_Find(int handle)
{
    if (!s_gasMaterialLab.active || handle != GasMaterialLab_Handle(&s_gasMaterialLab))
        return NULL;
    return &s_gasMaterialLab;
}

static int GasMaterialLab_ResolveStage(const VC_GasMaterialLab *lab)
{
    int override = (int)floorf(s_gasMaterialLabStageOverride + 0.5f);
    if (s_gasMaterialLabStageOverride >= 0.0f &&
        override >= GAS_SMOKE && override <= GAS_ENERGY)
        return override;
    float duration = fmaxf(s_gasMaterialLabStageDuration, 0.25f);
    return (int)floorf(lab->elapsed / duration) % GAS_MATERIAL_LAB_STAGE_COUNT;
}

static bool GasMaterialLab_SpawnStage(VC_GasMaterialLab *lab, int stage)
{
    if (lab->gasPlumeHandle != 0) {
        VFX_KillGasPlume(lab->gasPlumeHandle);
        lab->gasPlumeHandle = 0;
    }

    GasKind kind = (GasKind)stage;
    VFX_GasPlumeConfig config = VFX_GasPlume_DefaultConfig(kind);
    float scale = fmaxf(s_gasMaterialLabScale, 0.25f);
    config.priority = GAS_PRIORITY_ULTIMATE;
    config.radius = 2.4f * scale;
    config.height = 6.0f * scale;
    config.emitDuration = fmaxf(s_gasMaterialLabStageDuration, 0.25f) + 1.0f;
    config.decayDuration = 0.35f;
    config.intensity = 1.0f;
    config.detailStrength = s_gasMaterialLabDetail;
    config.shadowStrength = s_gasMaterialLabShadow;
    config.backgroundAdapt = s_gasMaterialLabBackgroundAdapt;
    config.usePresetPalette = s_gasMaterialLabMaterialPalette < 0.5f;

    int plume = VFX_GasPlume_Spawn(lab->position, lab->material, &config);
    if (plume == 0) return false;
    lab->gasPlumeHandle = plume;
    lab->stage = stage;
    lab->appliedStageDuration = s_gasMaterialLabStageDuration;
    lab->appliedMaterialPalette = s_gasMaterialLabMaterialPalette;
    lab->appliedDetail = s_gasMaterialLabDetail;
    lab->appliedShadow = s_gasMaterialLabShadow;
    lab->appliedBackgroundAdapt = s_gasMaterialLabBackgroundAdapt;
    lab->appliedScale = s_gasMaterialLabScale;
    TraceLog(LOG_INFO, "GasMaterialLab: stage %d (%s)", stage,
             stage == GAS_SMOKE ? "smoke" :
             (stage == GAS_FIRE ? "fire" : "energy"));
    return true;
}

int VFX_ComposeGasMaterialLab(Vector3 pos, VC_MaterialId mat)
{
    GasMaterialLab_EnsureTuning();
    if (s_gasMaterialLab.active)
        VFX_KillGasPlume(s_gasMaterialLab.gasPlumeHandle);

    ++s_gasMaterialLabSerial;
    if (s_gasMaterialLabSerial <= 0) s_gasMaterialLabSerial = 1;
    s_gasMaterialLab = (VC_GasMaterialLab){
        .active = true,
        .serial = s_gasMaterialLabSerial,
        .gasPlumeHandle = 0,
        .stage = -1,
        .position = pos,
        .material = mat,
        .elapsed = 0.0f
    };
    if (!GasMaterialLab_SpawnStage(&s_gasMaterialLab, GAS_SMOKE)) {
        s_gasMaterialLab = (VC_GasMaterialLab){0};
        return 0;
    }
    return GasMaterialLab_Handle(&s_gasMaterialLab);
}

void VFX_KillGasMaterialLab(int handle)
{
    VC_GasMaterialLab *lab = GasMaterialLab_Find(handle);
    if (lab == NULL) return;
    VFX_KillGasPlume(lab->gasPlumeHandle);
    *lab = (VC_GasMaterialLab){0};
}

static void VC_GasMaterialLab_Update(float dt)
{
    VC_GasMaterialLab *lab = &s_gasMaterialLab;
    if (!lab->active || dt <= 0.0f) return;
    lab->elapsed += dt;
    int desiredStage = GasMaterialLab_ResolveStage(lab);
    bool settingsChanged =
        lab->appliedStageDuration != s_gasMaterialLabStageDuration ||
        lab->appliedMaterialPalette != s_gasMaterialLabMaterialPalette ||
        lab->appliedDetail != s_gasMaterialLabDetail ||
        lab->appliedShadow != s_gasMaterialLabShadow ||
        lab->appliedBackgroundAdapt != s_gasMaterialLabBackgroundAdapt ||
        lab->appliedScale != s_gasMaterialLabScale;
    if (desiredStage != lab->stage || settingsChanged)
        GasMaterialLab_SpawnStage(lab, desiredStage);
}

/* GasSystem owns the actual depth-aware screen-space draw. */
static void VC_GasMaterialLab_Draw3D(Camera3D camera)
{
    (void)camera;
}

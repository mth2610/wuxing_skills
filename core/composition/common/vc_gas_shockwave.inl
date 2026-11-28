// One-shot pressure fronts; gas advection carries the wake after injection.

#include "core/tuning.h"

#define GAS_SHOCKWAVE_TAG_BASE 0x6C000
#define GAS_SHOCKWAVE_SPOKES 16
#define GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME 4

typedef struct {
    const char *name;
    VFX_GasShockwaveConfig config;
    GasKind kind;
    float sourceHeight;       // fraction of wave radius
    float sourceWidth;        // fraction of wave radius
    float density;
    float heat;
    float reaction;
    float opticalDensity;
    float emissionGain;       // multiplier of the gas material preset
    float turbulence;
    float densityDecay;
    float reactionDecay;
    float residualSmoke;
} GasShockwaveStyle;

static const GasShockwaveStyle s_gasShockwaveStyles[VFX_GAS_SHOCKWAVE_PRESET_COUNT] = {
    [VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT] = {
        .name = "Pressure Front", .kind = GAS_ENERGY,
        .config = {GAS_PRIORITY_CAST, 2.8f, 3.0f, 0.72f, 1.7f, 1.0f, 18.0f, 1.0f, 0.42f,
                   VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT},
        .sourceHeight = 0.14f, .sourceWidth = 0.10f, .density = 0.65f,
        .heat = 0.30f, .reaction = 1.90f, .opticalDensity = 2.10f,
        .emissionGain = 2.35f, .turbulence = 1.8f, .densityDecay = 0.92f,
        .reactionDecay = 1.35f, .residualSmoke = 0.52f
    },
    [VFX_GAS_SHOCKWAVE_PRESET_DUST_SURGE] = {
        .name = "Dust Surge", .kind = GAS_SMOKE,
        .config = {GAS_PRIORITY_CAST, 3.35f, 2.0f, 0.92f, 1.75f, 1.0f, 18.0f, 1.45f, 0.20f,
                   VFX_GAS_SHOCKWAVE_PRESET_DUST_SURGE},
        .sourceHeight = 0.075f, .sourceWidth = 0.135f, .density = 1.15f,
        .heat = 0.04f, .opticalDensity = 1.85f,
        .turbulence = 3.1f, .densityDecay = 0.72f, .reactionDecay = 1.4f
    },
    [VFX_GAS_SHOCKWAVE_PRESET_THERMAL_BLOOM] = {
        .name = "Thermal Bloom", .kind = GAS_FIRE,
        .config = {GAS_PRIORITY_CAST, 2.65f, 4.25f, 0.83f, 1.25f, 1.0f, 22.0f, 0.75f, 1.35f,
                   VFX_GAS_SHOCKWAVE_PRESET_THERMAL_BLOOM},
        .sourceHeight = 0.12f, .sourceWidth = 0.145f, .density = 1.10f,
        .heat = 1.50f, .reaction = 1.75f, .opticalDensity = 1.90f,
        .emissionGain = 1.45f, .turbulence = 3.6f, .densityDecay = 1.10f,
        .reactionDecay = 1.5f, .residualSmoke = 0.40f
    },
    [VFX_GAS_SHOCKWAVE_PRESET_VORTEX_WAKE] = {
        .name = "Vortex Wake", .kind = GAS_ENERGY,
        .config = {GAS_PRIORITY_CAST, 3.0f, 3.9f, 1.10f, 1.60f, 1.0f, 20.0f, 0.65f, 0.90f,
                   VFX_GAS_SHOCKWAVE_PRESET_VORTEX_WAKE},
        .sourceHeight = 0.16f, .sourceWidth = 0.115f, .density = 0.92f,
        .heat = 0.38f, .reaction = 1.55f, .opticalDensity = 2.0f,
        .emissionGain = 2.10f, .turbulence = 3.8f, .densityDecay = 0.84f,
        .reactionDecay = 1.15f, .residualSmoke = 0.61f
    },
    [VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE] = {
        .name = "Double Pulse", .kind = GAS_ENERGY,
        .config = {GAS_PRIORITY_CAST, 3.05f, 2.8f, 1.12f, 1.20f, 1.0f, 24.0f, 0.65f, 0.30f,
                   VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE},
        .sourceHeight = 0.125f, .sourceWidth = 0.085f, .density = 0.76f,
        .heat = 0.24f, .reaction = 1.85f, .opticalDensity = 2.50f,
        .emissionGain = 2.30f, .turbulence = 1.15f, .densityDecay = 0.86f,
        .reactionDecay = 1.0f, .residualSmoke = 0.48f
    }
};

typedef struct {
    bool emitting;
    int serial;
    unsigned int ringIndex;
    GasVolumeHandle gasHandle;
    Vector3 center;
    VFX_GasShockwaveConfig config;
    double elapsed;
    double endTime;
    double ringInterval;
    double firstRingTime;
    float densityMultiplier;
    float reactionMultiplier;
} VC_GasShockwave;

static VC_GasShockwave s_gasShockwave;
static unsigned int s_gasShockwaveSerial;
static bool s_gasShockwaveTuningReady;
static float s_gasShockwaveRateMul = 1.0f;
static float s_gasShockwaveRadiusMul = 1.0f;
static float s_gasShockwaveOutwardMul = 1.0f;
static float s_gasShockwaveLiftMul = 1.0f;
static float s_gasShockwaveDensityMul = 1.0f;
static float s_gasShockwaveEmissionMul = 1.0f;

static float GasShockwave_Clamp(float value, float min, float max)
{
    return fminf(max, fmaxf(min, value));
}

static float GasShockwave_SmoothStep(float edge0, float edge1, float value)
{
    float t = GasShockwave_Clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static const GasShockwaveStyle *GasShockwave_Style(VFX_GasShockwavePreset preset)
{
    if ((unsigned int)preset >= VFX_GAS_SHOCKWAVE_PRESET_COUNT)
        preset = VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT;
    return &s_gasShockwaveStyles[preset];
}

VFX_GasShockwaveConfig VFX_GasShockwave_DefaultConfig(void)
{
    return GasShockwave_Style(VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT)->config;
}

void VFX_GasShockwave_ApplyPreset(VFX_GasShockwaveConfig *config,
                                 VFX_GasShockwavePreset preset)
{
    if (config != NULL) *config = GasShockwave_Style(preset)->config;
}

const char *VFX_GasShockwavePreset_Name(VFX_GasShockwavePreset preset)
{
    return GasShockwave_Style(preset)->name;
}

VFX_GasShockwaveConfig *VFX_GasShockwave_GetLiveConfig(void)
{
    static VFX_GasShockwaveConfig config;
    static bool initialized;
    if (!initialized) {
        config = VFX_GasShockwave_DefaultConfig();
        initialized = true;
    }
    return &config;
}

int VFX_GasShockwave_GetParams(VFX_GasShockwaveConfig *config,
                              VFX_ParamDef *out, int max)
{
    if (config == NULL || out == NULL || max <= 0) return 0;
    VFX_ParamDef params[] = {
        {"Radius m", "Geometry", VFX_PARAM_FLOAT, &config->radius,
         .minFloat = 0.30f, .maxFloat = 8.0f, .stepFloat = 0.05f},
        {"Volume height m", "Geometry", VFX_PARAM_FLOAT, &config->height,
         .minFloat = 0.50f, .maxFloat = 8.0f, .stepFloat = 0.05f},
        {"Expansion s", "Timing", VFX_PARAM_FLOAT, &config->expandDuration,
         .minFloat = 0.12f, .maxFloat = 3.0f, .stepFloat = 0.01f},
        {"Wake duration s", "Timing", VFX_PARAM_FLOAT, &config->decayDuration,
         .minFloat = 0.10f, .maxFloat = 5.0f, .stepFloat = 0.05f},
        {"Rings per s", "Timing", VFX_PARAM_FLOAT, &config->ringsPerSecond,
         .minFloat = 6.0f, .maxFloat = 48.0f, .stepFloat = 1.0f},
        {"Intensity", "Emission", VFX_PARAM_FLOAT, &config->intensity,
         .minFloat = 0.0f, .maxFloat = 1.0f, .stepFloat = 0.05f},
        {"Outward speed m/s", "Motion", VFX_PARAM_FLOAT, &config->outwardSpeed,
         .minFloat = 0.05f, .maxFloat = 4.0f, .stepFloat = 0.05f},
        {"Lift m/s", "Motion", VFX_PARAM_FLOAT, &config->lift,
         .minFloat = 0.05f, .maxFloat = 4.0f, .stepFloat = 0.05f}
    };
    int count = (int)(sizeof(params) / sizeof(params[0]));
    if (count > max) count = max;
    for (int i = 0; i < count; ++i) out[i] = params[i];
    return count;
}

static void GasShockwave_EnsureTuning(void)
{
    if (s_gasShockwaveTuningReady) return;
    s_gasShockwaveTuningReady = true;
    Tuning_RegisterFloat("gasshock_rate", &s_gasShockwaveRateMul, 1.0f);
    Tuning_RegisterFloat("gasshock_radius", &s_gasShockwaveRadiusMul, 1.0f);
    Tuning_RegisterFloat("gasshock_outward", &s_gasShockwaveOutwardMul, 1.0f);
    Tuning_RegisterFloat("gasshock_lift", &s_gasShockwaveLiftMul, 1.0f);
    Tuning_RegisterFloat("gasshock_density", &s_gasShockwaveDensityMul, 1.0f);
    Tuning_RegisterFloat("gasshock_emission", &s_gasShockwaveEmissionMul, 1.0f);
}

static VC_GasShockwave *GasShockwave_Find(int handle)
{
    if (handle != s_gasShockwave.serial || !GasVolume_IsAlive(s_gasShockwave.gasHandle))
        return NULL;
    return &s_gasShockwave;
}

static void GasShockwave_EmitRing(const VC_GasShockwave *wave, double eventTime)
{
    const VFX_GasShockwaveConfig *config = &wave->config;
    const GasShockwaveStyle *style = GasShockwave_Style(config->preset);
    float progress = (float)(eventTime / config->expandDuration);
    float pulse = 1.0f;
    float reach = 1.0f;
    if (config->preset == VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE) {
        const float secondStart = 0.49f;
        bool second = progress >= secondStart;
        progress = second ? (progress - secondStart) / (1.0f - secondStart) :
                            progress / secondStart;
        reach = second ? 0.72f : 1.0f;
        pulse = second ? 1.28f : 1.0f;
    }

    float envelope = GasShockwave_SmoothStep(0.0f, 0.15f, progress) *
                     (1.0f - GasShockwave_SmoothStep(0.72f, 1.0f, progress));
    float inverse = 1.0f - progress;
    float expansion = 1.0f - inverse * inverse;
    if (config->preset == VFX_GAS_SHOCKWAVE_PRESET_THERMAL_BLOOM)
        expansion = progress * (1.6f - 0.6f * progress);

    float radius = config->radius;
    float ringRadius = radius * reach * (0.10f + 0.90f * expansion);
    float pulseMass = 10.0f * (float)wave->ringInterval * config->intensity * envelope * pulse;
    float phaseOffset = (wave->ringIndex & 1u) ? PI / GAS_SHOCKWAVE_SPOKES : 0.0f;
    float ringPhase = (float)wave->ringIndex;
    float cosine = cosf(phaseOffset);
    float sine = sinf(phaseOffset);
    const float stepCos = 0.923879533f;
    const float stepSin = 0.382683432f;

    for (int spoke = 0; spoke < GAS_SHOCKWAVE_SPOKES; ++spoke) {
        float angle = phaseOffset + (2.0f * PI / GAS_SHOCKWAVE_SPOKES) * spoke;
        float broad = sinf(angle * 3.0f + ringPhase * 0.41f);
        float fine = sinf(angle * 7.0f - ringPhase * 0.91f);
        float grain = 0.5f + 0.5f * sinf(angle * 11.0f + ringPhase * 2.17f);
        float radiusFactor = 1.0f + 0.052f * broad + 0.026f * fine;
        float elevation = style->sourceHeight + 0.025f * fine;
        float tangential = config->outwardSpeed * (radiusFactor - 1.0f);
        float strength = 0.78f + 0.32f * grain;
        float width = style->sourceWidth;
        float lift = config->lift * (0.82f + 0.30f * grain);

        switch (config->preset) {
            case VFX_GAS_SHOCKWAVE_PRESET_DUST_SURGE:
                radiusFactor += 0.09f * broad;
                elevation += 0.03f * grain * expansion;
                lift *= 0.60f + 0.70f * grain;
                break;
            case VFX_GAS_SHOCKWAVE_PRESET_THERMAL_BLOOM: {
                float petal = 0.5f + 0.5f * cosf(angle * 5.0f + 0.4f);
                elevation += progress * (0.27f + 0.34f * petal);
                radiusFactor += (petal - 0.5f) * 0.26f;
                lift *= 0.65f + 0.90f * petal;
                strength *= 0.55f + 0.80f * petal;
                break;
            }
            case VFX_GAS_SHOCKWAVE_PRESET_VORTEX_WAKE: {
                float crest = 0.5f + 0.5f * sinf(angle * 3.0f - progress * 7.5f);
                radiusFactor += (crest - 0.5f) * 0.36f;
                elevation += progress * (0.19f + 0.29f * crest);
                tangential += 1.95f * config->outwardSpeed;
                lift *= 0.52f + 1.22f * crest;
                strength *= 0.10f + 1.60f * crest * crest;
                break;
            }
            case VFX_GAS_SHOCKWAVE_PRESET_DOUBLE_PULSE:
                width *= reach;
                elevation += (1.0f - reach) * 0.22f;
                break;
            default:
                break;
        }

        float localRadius = ringRadius * radiusFactor;
        float outward = config->outwardSpeed * (0.86f + 0.25f * grain);
        GasInjection injection = {
            .position = {wave->center.x + cosine * localRadius,
                         wave->center.y + fminf(radius * elevation, config->height * 0.75f),
                         wave->center.z + sine * localRadius},
            .radius = radius * width * (0.86f + 0.28f * grain),
            .velocity = {cosine * outward - sine * tangential, lift,
                         sine * outward + cosine * tangential},
            .density = style->density * pulseMass * strength * wave->densityMultiplier,
            .temperature = style->heat * pulseMass * strength,
            .reaction = style->reaction * pulseMass * strength * wave->reactionMultiplier
        };
        GasVolume_Inject(wave->gasHandle, &injection);
        float nextCosine = cosine * stepCos - sine * stepSin;
        sine = sine * stepCos + cosine * stepSin;
        cosine = nextCosine;
    }
}

static float GasShockwave_PositiveOr(float value, float fallback)
{
    return isfinite(value) && value > 0.0f ? value : fallback;
}

int VFX_GasShockwave_Spawn(Vector3 pos, VC_MaterialId mat,
                          const VFX_GasShockwaveConfig *requested)
{
    GasShockwave_EnsureTuning();
    const GasShockwaveStyle *style = GasShockwave_Style(
        requested ? requested->preset : VFX_GAS_SHOCKWAVE_PRESET_PRESSURE_FRONT);
    VFX_GasShockwaveConfig config = requested ? *requested : style->config;
    config.preset = style->config.preset;
    config.intensity = GasShockwave_Clamp(config.intensity, 0.0f, 1.0f);
    if (config.intensity == 0.0f || !(s_gasShockwaveRateMul > 0.0f)) return 0;

    config.radius = GasShockwave_PositiveOr(config.radius, style->config.radius) *
                    GasShockwave_Clamp(s_gasShockwaveRadiusMul, 0.1f, 3.0f);
    config.height = GasShockwave_PositiveOr(config.height, style->config.height);
    config.expandDuration = fminf(60.0f, GasShockwave_PositiveOr(config.expandDuration,
                                                               style->config.expandDuration));
    config.decayDuration = GasShockwave_PositiveOr(config.decayDuration, style->config.decayDuration);
    config.outwardSpeed = GasShockwave_PositiveOr(config.outwardSpeed, style->config.outwardSpeed) *
                          GasShockwave_Clamp(s_gasShockwaveOutwardMul, 0.0f, 4.0f);
    config.lift = GasShockwave_PositiveOr(config.lift, style->config.lift) *
                  GasShockwave_Clamp(s_gasShockwaveLiftMul, 0.0f, 4.0f);
    float requestedRate = GasShockwave_PositiveOr(config.ringsPerSecond, style->config.ringsPerSecond);
    float ringRate = GasShockwave_Clamp(requestedRate * s_gasShockwaveRateMul, 1.0f, 48.0f);
    /* At least one event in a short burst. Cap the work per frame separately. */
    double ringInterval = 1.0 / ringRate;
    double firstRingTime = fmin(0.5 * ringInterval, 0.25 * config.expandDuration);

    const VFX_ElementMaterial *element = VFX_Material(mat);
    GasVolumeDesc volume = GasVolume_Preset(style->kind);
    volume.priority = config.priority;
    float belowSource = config.radius * 0.19f;
    float diameter = config.radius * 2.82f + fminf(config.outwardSpeed * 0.28f, config.radius * 0.30f);
    volume.center = (Vector3){pos.x, pos.y + (config.height - belowSource) * 0.5f, pos.z};
    volume.size = (Vector3){diameter, config.height + belowSource, diameter};
    volume.lifetime = config.expandDuration + config.decayDuration;
    volume.bodyColor = element->body;
    volume.emissionColor = element->glow;
    volume.densityScale = style->opticalDensity;
    volume.emissionGain *= style->emissionGain;
    volume.buoyancy = style->kind == GAS_FIRE ? 1.8f : 0.30f;
    volume.smokeWeight = 0.0f;
    volume.turbulence = style->turbulence;
    volume.densityDissipation = style->densityDecay;
    volume.temperatureDissipation = 1.2f;
    volume.reactionDissipation = style->reactionDecay;
    volume.residualSmoke = style->residualSmoke;

    GasVolumeHandle gasHandle = GasVolume_Create(&volume);
    if (gasHandle == GAS_VOLUME_INVALID) return 0;

    s_gasShockwaveSerial = (s_gasShockwaveSerial % 0x0FFFu) + 1u;
    s_gasShockwave = (VC_GasShockwave){
        .emitting = true,
        .serial = GAS_SHOCKWAVE_TAG_BASE + (int)s_gasShockwaveSerial,
        .gasHandle = gasHandle,
        .center = pos,
        .config = config,
        .endTime = (double)config.expandDuration + config.decayDuration,
        .ringInterval = ringInterval,
        .firstRingTime = firstRingTime,
        .densityMultiplier = GasShockwave_Clamp(s_gasShockwaveDensityMul, 0.0f, 4.0f),
        .reactionMultiplier = GasShockwave_Clamp(s_gasShockwaveEmissionMul, 0.0f, 4.0f)
    };
    return s_gasShockwave.serial;
}

void VFX_ComposeGasShockwave(Vector3 pos, VC_MaterialId mat,
                            const VFX_GasShockwaveConfig *config)
{
    (void)VFX_GasShockwave_Spawn(pos, mat, config);
}

void VFX_GasShockwave_Stop(int handle)
{
    VC_GasShockwave *wave = GasShockwave_Find(handle);
    if (wave == NULL || !wave->emitting) return;
    wave->emitting = false;
    wave->endTime = fmin(wave->endTime, wave->elapsed + wave->config.decayDuration);
}

void VFX_KillGasShockwave(int handle)
{
    VC_GasShockwave *wave = GasShockwave_Find(handle);
    if (wave == NULL) return;
    GasVolume_Destroy(wave->gasHandle);
    *wave = (VC_GasShockwave){0};
}

static void VC_GasShockwave_Update(float dt)
{
    VC_GasShockwave *wave = &s_gasShockwave;
    if (wave->gasHandle == GAS_VOLUME_INVALID || !isfinite(dt) || dt <= 0.0f) return;
    wave->elapsed += fminf(dt, 0.25f);
    if (!GasVolume_IsAlive(wave->gasHandle) || wave->elapsed >= wave->endTime) {
        GasVolume_Destroy(wave->gasHandle);
        *wave = (VC_GasShockwave){0};
        return;
    }

    if (wave->emitting) {
        double feedTime = fmin(wave->elapsed, wave->config.expandDuration);
        double scheduled = (feedTime - wave->firstRingTime) / wave->ringInterval;
        if (scheduled >= 0.0) {
            unsigned int due = (unsigned int)scheduled + 1u;
            /* After a hitch, recover at the latest scheduled front. Avoid
             * piling old rings on top of each other at the current radius. */
            if (due > wave->ringIndex + GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME)
                wave->ringIndex = due - GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME;
            while (wave->ringIndex < due) {
                double eventTime = wave->firstRingTime + wave->ringIndex * wave->ringInterval;
                if (eventTime < wave->config.expandDuration) GasShockwave_EmitRing(wave, eventTime);
                ++wave->ringIndex;
            }
        }
        if (wave->elapsed >= wave->config.expandDuration) wave->emitting = false;
    }

    float fadeDuration = fminf(0.56f, wave->config.decayDuration);
    float remaining = (float)(wave->endTime - wave->elapsed);
    if (remaining < fadeDuration)
        GasVolume_SetVisibility(wave->gasHandle, GasShockwave_SmoothStep(0.0f, fadeDuration, remaining));
}

/* GasSystem owns compositing. The archetype dispatcher pairs Update and Draw3D. */
static void VC_GasShockwave_Draw3D(Camera3D camera)
{
    (void)camera;
}

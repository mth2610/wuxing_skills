// Volumetric gas plume — an authored emitter over core/gas.
//
// The gas grid is the shape. This archetype only owns lifecycle and a
// frame-rate-independent sequence of injection events; it never draws proxy
// sprites or geometry over the raymarched volume.

#include "core/tuning.h"

#define GAS_PLUME_TAG_BASE 0x6A000
#define GAS_PLUME_MAX_PULSES_PER_FRAME 3

typedef struct {
    bool active;
    bool emitting;
    int serial;
    GasVolumeHandle gasHandle;
    Vector3 position;
    VC_MaterialId material;
    VFX_GasPlumeConfig config;
    float elapsed;
    float endTime;
    float pulseAccumulator;
    unsigned int randomState;
} VC_GasPlume;

/* GasSystem v1 owns one volume, so the authored layer mirrors that capacity
 * instead of pretending to pool effects the renderer cannot admit. */
static VC_GasPlume s_gasPlume;
static int s_gasPlumeSerial;
static bool s_gasPlumeTuningReady;
static float s_gasPlumeRateMul = 1.0f;
static float s_gasPlumeSpreadMul = 1.0f;
static float s_gasPlumeLiftMul = 1.0f;
static float s_gasPlumeDensityMul = 1.0f;
static float s_gasPlumeEmissionMul = 1.0f;

static float GasPlume_Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float GasPlume_SmoothStep(float edge0, float edge1, float value)
{
    float width = edge1 - edge0;
    if (width <= 0.0001f) return value >= edge1 ? 1.0f : 0.0f;
    float t = GasPlume_Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}

static float GasPlume_Envelope(const VC_GasPlume *plume)
{
    float duration = plume->config.emitDuration;
    float fade = fminf(0.25f, duration * 0.25f);
    if (!plume->emitting || fade <= 0.0001f || plume->elapsed >= duration)
        return 0.0f;
    float fadeIn = GasPlume_SmoothStep(0.0f, fade, plume->elapsed);
    float fadeOut = 1.0f - GasPlume_SmoothStep(duration - fade, duration,
                                               plume->elapsed);
    return fadeIn * fadeOut;
}

static float GasPlume_Random01(VC_GasPlume *plume)
{
    plume->randomState = plume->randomState * 1664525u + 1013904223u;
    return (float)((plume->randomState >> 8) & 0x00FFFFFFu) / 16777215.0f;
}

static void GasPlume_EnsureTuning(void)
{
    if (s_gasPlumeTuningReady) return;
    s_gasPlumeTuningReady = true;
    Tuning_RegisterFloat("gasplume_rate", &s_gasPlumeRateMul, 1.0f);
    Tuning_RegisterFloat("gasplume_spread", &s_gasPlumeSpreadMul, 1.0f);
    Tuning_RegisterFloat("gasplume_lift", &s_gasPlumeLiftMul, 1.0f);
    Tuning_RegisterFloat("gasplume_density", &s_gasPlumeDensityMul, 1.0f);
    Tuning_RegisterFloat("gasplume_emission", &s_gasPlumeEmissionMul, 1.0f);
}

static int GasPlume_Handle(const VC_GasPlume *plume)
{
    return GAS_PLUME_TAG_BASE + (plume->serial & 0x0FFF);
}

static VC_GasPlume *GasPlume_Find(int handle)
{
    if (!s_gasPlume.active || handle != GasPlume_Handle(&s_gasPlume)) return NULL;
    return &s_gasPlume;
}

VFX_GasPlumeConfig VFX_GasPlume_DefaultConfig(GasKind kind)
{
    VFX_GasPlumeConfig config = {0};
    config.kind = kind;
    config.priority = GAS_PRIORITY_CAST;
    config.radius = 0.9f;
    config.height = 3.2f;
    config.emitDuration = 1.8f;
    config.decayDuration = 2.2f;
    config.intensity = 1.0f;
    config.pulsesPerSecond = 24.0f;
    if (kind == GAS_SMOKE) {
        config.radius = 1.1f;
        config.height = 3.8f;
        config.emitDuration = 2.4f;
        config.decayDuration = 3.0f;
        config.pulsesPerSecond = 20.0f;
    } else if (kind == GAS_ENERGY) {
        config.radius = 1.0f;
        config.height = 2.4f;
        config.emitDuration = 1.4f;
        config.decayDuration = 1.8f;
        config.pulsesPerSecond = 30.0f;
    }
    return config;
}

static void GasPlume_EmitPulse(VC_GasPlume *plume, float envelope)
{
    if (envelope <= 0.0f) return;
    float intensity = GasPlume_Clamp01(plume->config.intensity) * envelope;
    float angle = GasPlume_Random01(plume) * 2.0f * PI;
    float sourceRadius = plume->config.kind == GAS_ENERGY ? 0.16f : 0.22f;
    float referenceRate = plume->config.kind == GAS_ENERGY ? 14.0f :
                          (plume->config.kind == GAS_FIRE ? 12.0f : 10.0f);
    float pulseMass = referenceRate / fmaxf(plume->config.pulsesPerSecond, 1.0f);
    float sideSpeed = plume->config.radius *
                      (0.32f + 0.30f * GasPlume_Random01(plume)) *
                      s_gasPlumeSpreadMul;
    float lift = fmaxf(0.6f, plume->config.height * 0.36f) * s_gasPlumeLiftMul;

    GasInjection injection = {0};
    injection.position = plume->position;
    injection.radius = plume->config.radius *
                       (sourceRadius + 0.10f * GasPlume_Random01(plume));
    injection.velocity = Vector3Add(plume->config.wind, (Vector3){
        cosf(angle) * sideSpeed,
        lift,
        sinf(angle) * sideSpeed
    });
    float densityCoefficient = plume->config.kind == GAS_FIRE ? 0.62f : 0.48f;
    injection.density = densityCoefficient * pulseMass * intensity *
                        s_gasPlumeDensityMul;

    if (plume->config.kind == GAS_FIRE) {
        injection.temperature = 0.95f * pulseMass * intensity;
        injection.reaction = 1.20f * pulseMass * intensity *
                             s_gasPlumeEmissionMul;
    } else if (plume->config.kind == GAS_ENERGY) {
        injection.temperature = 0.45f * pulseMass * intensity;
        injection.reaction = 0.85f * pulseMass * intensity * s_gasPlumeEmissionMul;
    } else {
        injection.temperature = 0.30f * pulseMass * intensity;
        injection.reaction = 0.0f;
    }
    GasVolume_Inject(plume->gasHandle, &injection);
}

int VFX_GasPlume_Spawn(Vector3 pos, VC_MaterialId mat,
                       const VFX_GasPlumeConfig *requested)
{
    GasPlume_EnsureTuning();
    VFX_GasPlumeConfig config = requested != NULL
        ? *requested : VFX_GasPlume_DefaultConfig(GAS_FIRE);
    if (config.kind < GAS_SMOKE || config.kind > GAS_ENERGY)
        config.kind = GAS_SMOKE;
    VFX_GasPlumeConfig defaults = VFX_GasPlume_DefaultConfig(config.kind);
    if (config.radius <= 0.0f) config.radius = defaults.radius;
    if (config.height <= 0.0f) config.height = defaults.height;
    if (config.emitDuration <= 0.0f) config.emitDuration = defaults.emitDuration;
    if (config.decayDuration <= 0.0f) config.decayDuration = defaults.decayDuration;
    if (config.pulsesPerSecond <= 0.0f)
        config.pulsesPerSecond = defaults.pulsesPerSecond;
    config.intensity = GasPlume_Clamp01(config.intensity);

    const VFX_ElementMaterial *element = VFX_Material(mat);
    GasVolumeDesc volume = GasVolume_Preset(config.kind);
    volume.priority = config.priority;
    /* Keep the authored height above the source, but reserve a shallow buffer
     * below it. Otherwise the spherical source is cut in half by grid y=0 and
     * its visible density centroid starts above the requested world point. */
    float belowSource = config.radius * 0.45f;
    volume.center = Vector3Add(pos, (Vector3){
        0.0f, (config.height - belowSource) * 0.5f, 0.0f
    });
    volume.size = (Vector3){config.radius * 2.0f, config.height + belowSource,
                            config.radius * 2.0f};
    volume.lifetime = config.emitDuration + config.decayDuration;
    volume.bodyColor = element->body;
    if (config.kind != GAS_SMOKE) volume.emissionColor = element->glow;
    float fireOpticalGain = config.kind == GAS_FIRE ? 1.35f : 1.0f;
    if (config.kind == GAS_FIRE) volume.densityScale = 2.6f;
    volume.emissionGain *= config.intensity * fireOpticalGain *
                           s_gasPlumeEmissionMul;

    GasVolumeHandle gasHandle = GasVolume_Create(&volume);
    if (gasHandle == GAS_VOLUME_INVALID) return 0;

    s_gasPlumeSerial++;
    if (s_gasPlumeSerial <= 0) s_gasPlumeSerial = 1;
    s_gasPlume = (VC_GasPlume){
        .active = true,
        .emitting = true,
        .serial = s_gasPlumeSerial,
        .gasHandle = gasHandle,
        .position = pos,
        .material = mat,
        .config = config,
        .elapsed = 0.0f,
        .endTime = config.emitDuration + config.decayDuration,
        .pulseAccumulator = 0.0f,
        .randomState = 0x9E3779B9u ^ (unsigned int)s_gasPlumeSerial * 747796405u
    };
    return GasPlume_Handle(&s_gasPlume);
}

/* Fixture/discovery entry point. The explicit Spawn name remains the lifecycle
 * API used by gameplay; Compose keeps this stateful archetype discoverable by
 * sync_vfx_test.py's one-entry-per-.inl convention. */
int VFX_ComposeGasPlume(Vector3 pos, VC_MaterialId mat,
                        const VFX_GasPlumeConfig *config)
{
    return VFX_GasPlume_Spawn(pos, mat, config);
}

void VFX_GasPlume_SetIntensity(int handle, float intensity01)
{
    VC_GasPlume *plume = GasPlume_Find(handle);
    if (plume != NULL) plume->config.intensity = GasPlume_Clamp01(intensity01);
}

void VFX_GasPlume_Stop(int handle)
{
    VC_GasPlume *plume = GasPlume_Find(handle);
    if (plume == NULL || !plume->emitting) return;
    plume->emitting = false;
    float decayEnd = plume->elapsed + plume->config.decayDuration;
    if (decayEnd < plume->endTime) plume->endTime = decayEnd;
}

void VFX_KillGasPlume(int handle)
{
    VC_GasPlume *plume = GasPlume_Find(handle);
    if (plume == NULL) return;
    GasVolume_Destroy(plume->gasHandle);
    *plume = (VC_GasPlume){0};
}

static void VC_GasPlume_Update(float dt)
{
    VC_GasPlume *plume = &s_gasPlume;
    if (!plume->active || dt <= 0.0f) return;
    plume->elapsed += dt;
    if (!GasVolume_IsAlive(plume->gasHandle) || plume->elapsed >= plume->endTime) {
        GasVolume_Destroy(plume->gasHandle);
        *plume = (VC_GasPlume){0};
        return;
    }
    if (plume->elapsed >= plume->config.emitDuration) plume->emitting = false;

    float pulseRate = plume->config.pulsesPerSecond * s_gasPlumeRateMul;
    if (!plume->emitting || pulseRate <= 0.0f) return;
    plume->pulseAccumulator += dt * pulseRate;
    int emitted = 0;
    while (plume->pulseAccumulator >= 1.0f &&
           emitted < GAS_PLUME_MAX_PULSES_PER_FRAME) {
        plume->pulseAccumulator -= 1.0f;
        GasPlume_EmitPulse(plume, GasPlume_Envelope(plume));
        ++emitted;
    }
    if (emitted == GAS_PLUME_MAX_PULSES_PER_FRAME && plume->pulseAccumulator > 1.0f)
        plume->pulseAccumulator = 1.0f;
}

/* Drawing is owned by GasSystem's depth-aware screen-space compositor. */
static void VC_GasPlume_Draw3D(Camera3D camera)
{
    (void)camera;
}

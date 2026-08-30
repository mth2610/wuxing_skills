// PRIMARY. Volumetric flame jet — a directed fire body over core/gas.
//
// Four overlapping lobes populate the whole start->end segment on every
// fixed-rate pulse. The authored cone supplies an immediate readable attack;
// the gas solver owns the secondary billow, buoyant smoke hand-off and decay.
// No proxy sprites or ribbon are layered over the raymarched fire.

#include "core/tuning.h"

#define FLAME_JET_TAG_BASE 0x6D000
#define FLAME_JET_LOBES 4
#define FLAME_JET_MAX_PULSES_PER_FRAME 3

typedef struct {
    bool active;
    bool emitting;
    int serial;
    GasVolumeHandle gasHandle;
    Vector3 start;
    Vector3 end;
    Vector3 direction;
    Vector3 side;
    Vector3 crossUp;
    float length;
    VC_MaterialId material;
    VFX_FlameJetConfig config;
    float elapsed;
    float endTime;
    float pulseAccumulator;
    unsigned int randomState;
} VC_FlameJet;

/* GasSystem v1 admits one volume, so this primary mirrors that capacity. */
static VC_FlameJet s_flameJet;
static int s_flameJetSerial;
static bool s_flameJetTuningReady;
static float s_flameJetRateMul = 1.0f;
static float s_flameJetSpeedMul = 1.0f;
static float s_flameJetTurbulenceMul = 1.0f;
static float s_flameJetDensityMul = 1.0f;
static float s_flameJetEmissionMul = 1.0f;

static float FlameJet_Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float FlameJet_SmoothStep(float edge0, float edge1, float value)
{
    float width = edge1 - edge0;
    if (width <= 0.0001f) return value >= edge1 ? 1.0f : 0.0f;
    float t = FlameJet_Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}

static float FlameJet_Envelope(const VC_FlameJet *jet)
{
    float duration = jet->config.emitDuration;
    float fadeInDuration = fminf(0.10f, duration * 0.22f);
    float fadeOutDuration = fminf(0.18f, duration * 0.28f);
    if (!jet->emitting || jet->elapsed >= duration) return 0.0f;
    float fadeIn = FlameJet_SmoothStep(0.0f, fadeInDuration, jet->elapsed);
    float fadeOut = 1.0f - FlameJet_SmoothStep(duration - fadeOutDuration,
                                               duration, jet->elapsed);
    return fadeIn * fadeOut;
}

static float FlameJet_RandomSigned(VC_FlameJet *jet)
{
    jet->randomState = jet->randomState * 1664525u + 1013904223u;
    float unit = (float)((jet->randomState >> 8) & 0x00FFFFFFu) / 16777215.0f;
    return unit * 2.0f - 1.0f;
}

static void FlameJet_EnsureTuning(void)
{
    if (s_flameJetTuningReady) return;
    s_flameJetTuningReady = true;
    Tuning_RegisterFloat("flamejet_rate", &s_flameJetRateMul, 1.0f);
    Tuning_RegisterFloat("flamejet_speed", &s_flameJetSpeedMul, 1.0f);
    Tuning_RegisterFloat("flamejet_turbulence", &s_flameJetTurbulenceMul, 1.0f);
    Tuning_RegisterFloat("flamejet_density", &s_flameJetDensityMul, 1.0f);
    Tuning_RegisterFloat("flamejet_emission", &s_flameJetEmissionMul, 1.0f);
}

static int FlameJet_Handle(const VC_FlameJet *jet)
{
    return FLAME_JET_TAG_BASE + (jet->serial & 0x0FFF);
}

static VC_FlameJet *FlameJet_Find(int handle)
{
    if (!s_flameJet.active || handle != FlameJet_Handle(&s_flameJet)) return NULL;
    return &s_flameJet;
}

VFX_FlameJetConfig VFX_FlameJet_DefaultConfig(void)
{
    VFX_FlameJetConfig config = {0};
    config.priority = GAS_PRIORITY_CAST;
    config.radius = 0.62f;
    config.emitDuration = 0.85f;
    config.decayDuration = 1.35f;
    config.intensity = 1.0f;
    config.pulsesPerSecond = 20.0f;
    config.speed = 4.8f;
    config.turbulence = 1.10f;
    config.lift = 0.55f;
    config.seed = 0xF1A6E37u;
    return config;
}

static void FlameJet_EmitPulse(VC_FlameJet *jet, float envelope, float pulseRate)
{
    if (envelope <= 0.0f || pulseRate <= 0.0f) return;
    float intensity = FlameJet_Clamp01(jet->config.intensity) * envelope;
    float pulseMass = 16.0f / pulseRate;

    for (int lobe = 0; lobe < FLAME_JET_LOBES; ++lobe) {
        float baseT = ((float)lobe + 0.42f) / (float)FLAME_JET_LOBES;
        float t = FlameJet_Clamp01(baseT + FlameJet_RandomSigned(jet) * 0.035f);
        float cone = 0.24f + 0.42f * t;
        float sideNoise = FlameJet_RandomSigned(jet);
        float upNoise = FlameJet_RandomSigned(jet);
        float jitter = jet->config.radius * (0.060f + 0.24f * t) *
                       jet->config.turbulence * s_flameJetTurbulenceMul;
        Vector3 position = Vector3Add(jet->start,
                                     Vector3Scale(jet->direction, jet->length * t));
        position = Vector3Add(position, Vector3Scale(jet->side, sideNoise * jitter));
        position = Vector3Add(position, Vector3Scale(jet->crossUp, upNoise * jitter));

        float forwardSpeed = jet->config.speed * (1.08f - 0.18f * t) *
                             s_flameJetSpeedMul;
        float turbulentSpeed = jet->config.turbulence *
                               (0.45f + 0.65f * t) *
                               s_flameJetTurbulenceMul;
        GasInjection injection = {0};
        injection.position = position;
        injection.radius = jet->config.radius * cone *
                           (0.82f + 0.24f * fabsf(sideNoise));
        injection.velocity = Vector3Add(
            Vector3Scale(jet->direction, forwardSpeed),
            Vector3Add(Vector3Scale(jet->side, sideNoise * turbulentSpeed),
                       Vector3Add(Vector3Scale(jet->crossUp, upNoise * turbulentSpeed),
                                  (Vector3){0.0f, jet->config.lift *
                                                   (0.30f + 0.70f * t), 0.0f})));
        /* Reaction leads the attack; density lasts longer and becomes the dark
         * cooling body after reaction/temperature dissipate. */
        injection.density = 0.38f * pulseMass * intensity *
                            (1.0f - 0.18f * t) * s_flameJetDensityMul;
        injection.temperature = 0.92f * pulseMass * intensity *
                                (1.0f - 0.12f * t);
        injection.reaction = 1.18f * pulseMass * intensity *
                             (1.0f - 0.10f * t) * s_flameJetEmissionMul;
        GasVolume_Inject(jet->gasHandle, &injection);
    }
}

int VFX_FlameJet_Spawn(Vector3 start, Vector3 end, VC_MaterialId mat,
                       const VFX_FlameJetConfig *requested)
{
    FlameJet_EnsureTuning();
    VFX_FlameJetConfig defaults = VFX_FlameJet_DefaultConfig();
    VFX_FlameJetConfig config = requested != NULL ? *requested : defaults;
    if (config.radius <= 0.0f) config.radius = defaults.radius;
    if (config.emitDuration <= 0.0f) config.emitDuration = defaults.emitDuration;
    if (config.decayDuration <= 0.0f) config.decayDuration = defaults.decayDuration;
    if (config.pulsesPerSecond <= 0.0f)
        config.pulsesPerSecond = defaults.pulsesPerSecond;
    if (config.speed <= 0.0f) config.speed = defaults.speed;
    if (config.turbulence < 0.0f) config.turbulence = defaults.turbulence;
    if (config.lift < 0.0f) config.lift = defaults.lift;
    config.intensity = FlameJet_Clamp01(config.intensity);

    Vector3 delta = Vector3Subtract(end, start);
    float length = Vector3Length(delta);
    if (length < 0.20f) return 0;
    Vector3 direction = Vector3Scale(delta, 1.0f / length);
    Vector3 side = Vector3CrossProduct(direction, (Vector3){0.0f, 1.0f, 0.0f});
    if (Vector3Length(side) < 0.01f)
        side = Vector3CrossProduct(direction, (Vector3){1.0f, 0.0f, 0.0f});
    side = Vector3Normalize(side);
    Vector3 crossUp = Vector3Normalize(Vector3CrossProduct(side, direction));

    float margin = config.radius * 1.35f;
    float rise = config.radius * 2.4f + config.lift * config.decayDuration * 0.45f;
    Vector3 minimum = {
        fminf(start.x, end.x) - margin,
        fminf(start.y, end.y) - margin * 0.60f,
        fminf(start.z, end.z) - margin
    };
    Vector3 maximum = {
        fmaxf(start.x, end.x) + margin,
        fmaxf(start.y, end.y) + margin + rise,
        fmaxf(start.z, end.z) + margin
    };

    const VFX_ElementMaterial *element = VFX_Material(mat);
    GasVolumeDesc volume = GasVolume_Preset(GAS_FIRE);
    volume.priority = config.priority;
    volume.center = Vector3Scale(Vector3Add(minimum, maximum), 0.5f);
    volume.size = Vector3Subtract(maximum, minimum);
    volume.lifetime = config.emitDuration + config.decayDuration;
    /* A bright material body makes the whole density field one orange tube.
     * Keep the absorptive carrier dark; reaction remains material-coloured and
     * hot, then burns away first to reveal a smoky cooling edge. */
    volume.bodyColor = (Color){
        (unsigned char)fmaxf(18.0f, (float)element->body.r * 0.30f),
        (unsigned char)fmaxf(10.0f, (float)element->body.g * 0.22f),
        (unsigned char)fmaxf(6.0f, (float)element->body.b * 0.18f),
        255
    };
    volume.emissionColor = element->glow;
    volume.densityScale = 1.65f;
    volume.emissionGain *= config.intensity * 1.25f * s_flameJetEmissionMul;
    volume.buoyancy = 2.6f;
    volume.smokeWeight = 0.10f;
    volume.densityDissipation = 0.48f;
    volume.temperatureDissipation = 1.30f;
    volume.reactionDissipation = 1.48f;

    GasVolumeHandle gasHandle = GasVolume_Create(&volume);
    if (gasHandle == GAS_VOLUME_INVALID) return 0;

    s_flameJetSerial++;
    if (s_flameJetSerial <= 0) s_flameJetSerial = 1;
    s_flameJet = (VC_FlameJet){
        .active = true,
        .emitting = true,
        .serial = s_flameJetSerial,
        .gasHandle = gasHandle,
        .start = start,
        .end = end,
        .direction = direction,
        .side = side,
        .crossUp = crossUp,
        .length = length,
        .material = mat,
        .config = config,
        .elapsed = 0.0f,
        .endTime = config.emitDuration + config.decayDuration,
        .pulseAccumulator = 0.0f,
        .randomState = (config.seed != 0u ? config.seed : defaults.seed) ^
                       (unsigned int)s_flameJetSerial * 747796405u
    };
    return FlameJet_Handle(&s_flameJet);
}

void VFX_ComposeFlameJet(Vector3 start, Vector3 end, VC_MaterialId mat,
                         const VFX_FlameJetConfig *config)
{
    (void)VFX_FlameJet_Spawn(start, end, mat, config);
}

void VFX_FlameJet_SetIntensity(int handle, float intensity01)
{
    VC_FlameJet *jet = FlameJet_Find(handle);
    if (jet != NULL) jet->config.intensity = FlameJet_Clamp01(intensity01);
}

void VFX_FlameJet_Stop(int handle)
{
    VC_FlameJet *jet = FlameJet_Find(handle);
    if (jet == NULL || !jet->emitting) return;
    jet->emitting = false;
    float decayEnd = jet->elapsed + jet->config.decayDuration;
    if (decayEnd < jet->endTime) jet->endTime = decayEnd;
}

void VFX_KillFlameJet(int handle)
{
    VC_FlameJet *jet = FlameJet_Find(handle);
    if (jet == NULL) return;
    GasVolume_Destroy(jet->gasHandle);
    *jet = (VC_FlameJet){0};
}

static void VC_FlameJet_Update(float dt)
{
    VC_FlameJet *jet = &s_flameJet;
    if (!jet->active || dt <= 0.0f) return;
    jet->elapsed += dt;
    if (!GasVolume_IsAlive(jet->gasHandle) || jet->elapsed >= jet->endTime) {
        GasVolume_Destroy(jet->gasHandle);
        *jet = (VC_FlameJet){0};
        return;
    }
    if (jet->elapsed >= jet->config.emitDuration) jet->emitting = false;

    float pulseRate = jet->config.pulsesPerSecond * s_flameJetRateMul;
    if (!jet->emitting || pulseRate <= 0.0f) return;
    jet->pulseAccumulator += dt * pulseRate;
    int emitted = 0;
    while (jet->pulseAccumulator >= 1.0f &&
           emitted < FLAME_JET_MAX_PULSES_PER_FRAME) {
        jet->pulseAccumulator -= 1.0f;
        FlameJet_EmitPulse(jet, FlameJet_Envelope(jet), pulseRate);
        ++emitted;
    }
    if (emitted == FLAME_JET_MAX_PULSES_PER_FRAME &&
        jet->pulseAccumulator > 1.0f)
        jet->pulseAccumulator = 1.0f;
}

/* Drawing is owned by GasSystem's depth-aware screen-space compositor. */
static void VC_FlameJet_Draw3D(Camera3D camera)
{
    (void)camera;
}

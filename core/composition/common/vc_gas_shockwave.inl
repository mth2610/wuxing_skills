// Volumetric energy-smoke shockwave — an expanding torus over core/gas.
//
// Each fixed-rate event injects a complete ring of sources. Their radial
// velocity moves the simulated gas front outward while a small lift keeps the
// smoke from clipping into the floor. The gas grid, not proxy geometry, owns
// the soft roll-up and dissipation behind the bright front.

#include "core/tuning.h"

#define GAS_SHOCKWAVE_TAG_BASE 0x6C000
#define GAS_SHOCKWAVE_SPOKES 16
#define GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME 2

typedef struct {
    bool active;
    bool emitting;
    int serial;
    int ringIndex;
    GasVolumeHandle gasHandle;
    Vector3 center;
    VC_MaterialId material;
    VFX_GasShockwaveConfig config;
    float elapsed;
    float endTime;
    float ringAccumulator;
} VC_GasShockwave;

/* GasSystem v1 admits one volume, so the composition mirrors that capacity. */
static VC_GasShockwave s_gasShockwave;
static int s_gasShockwaveSerial;
static bool s_gasShockwaveTuningReady;
static float s_gasShockwaveRateMul = 1.0f;
static float s_gasShockwaveRadiusMul = 1.0f;
static float s_gasShockwaveOutwardMul = 1.0f;
static float s_gasShockwaveLiftMul = 1.0f;
static float s_gasShockwaveDensityMul = 1.0f;
static float s_gasShockwaveEmissionMul = 1.0f;

static float GasShockwave_Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float GasShockwave_SmoothStep(float edge0, float edge1, float value)
{
    float width = edge1 - edge0;
    if (width <= 0.0001f) return value >= edge1 ? 1.0f : 0.0f;
    float t = GasShockwave_Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}

static float GasShockwave_Envelope(const VC_GasShockwave *wave)
{
    float progress = GasShockwave_Clamp01(wave->elapsed /
                                          fmaxf(wave->config.expandDuration, 0.001f));
    float fadeIn = GasShockwave_SmoothStep(0.0f, 0.14f, progress);
    float fadeOut = 1.0f - GasShockwave_SmoothStep(0.78f, 1.0f, progress);
    return wave->emitting ? fadeIn * fadeOut : 0.0f;
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

static int GasShockwave_Handle(const VC_GasShockwave *wave)
{
    return GAS_SHOCKWAVE_TAG_BASE + (wave->serial & 0x0FFF);
}

static VC_GasShockwave *GasShockwave_Find(int handle)
{
    if (!s_gasShockwave.active || handle != GasShockwave_Handle(&s_gasShockwave))
        return NULL;
    return &s_gasShockwave;
}

VFX_GasShockwaveConfig VFX_GasShockwave_DefaultConfig(void)
{
    VFX_GasShockwaveConfig config = {0};
    config.priority = GAS_PRIORITY_CAST;
    config.radius = 2.8f;
    config.height = 3.0f;
    config.expandDuration = 0.72f;
    config.decayDuration = 1.7f;
    config.intensity = 1.0f;
    config.ringsPerSecond = 18.0f;
    config.outwardSpeed = 1.0f;
    config.lift = 0.42f;
    return config;
}

static void GasShockwave_EmitRing(VC_GasShockwave *wave, float envelope,
                                  float ringRate)
{
    if (envelope <= 0.0f || ringRate <= 0.0f) return;

    float progress = GasShockwave_Clamp01(wave->elapsed /
                                          fmaxf(wave->config.expandDuration, 0.001f));
    float inverse = 1.0f - progress;
    float eased = 1.0f - inverse * inverse;
    float ringRadius = wave->config.radius * (0.08f + 0.92f * eased) *
                       s_gasShockwaveRadiusMul;
    float pulseMass = 10.0f / ringRate;
    float intensity = GasShockwave_Clamp01(wave->config.intensity) * envelope;
    float phaseOffset = (wave->ringIndex & 1) ? PI / (float)GAS_SHOCKWAVE_SPOKES
                                              : 0.0f;

    for (int spoke = 0; spoke < GAS_SHOCKWAVE_SPOKES; ++spoke) {
        float angle = phaseOffset + 2.0f * PI * (float)spoke /
                                    (float)GAS_SHOCKWAVE_SPOKES;
        float cosine = cosf(angle);
        float sine = sinf(angle);
        float ringPhase = (float)wave->ringIndex;
        /* Two non-harmonic edge bands break the mathematical circle without
         * turning sixteen sources into sixteen obvious beads. A faster grain
         * varies thickness, energy and velocity on top of the broad wobble. */
        float broadNoise = 0.055f * sinf(angle * 3.0f + ringPhase * 0.73f) +
                           0.030f * sinf(angle * 7.0f - ringPhase * 0.91f);
        float heightNoise = 0.035f * sinf(angle * 5.0f + ringPhase * 1.17f) +
                            0.018f * sinf(angle * 9.0f - ringPhase * 0.57f);
        float grain = 0.5f + 0.5f * sinf(angle * 11.0f + ringPhase * 2.17f);
        float localRingRadius = ringRadius * (1.0f + broadNoise);
        float localOutwardSpeed = wave->config.outwardSpeed *
                                  (0.86f + 0.25f * grain) *
                                  s_gasShockwaveOutwardMul;
        float tangentialSpeed = wave->config.outwardSpeed * broadNoise * 1.8f;

        GasInjection injection = {0};
        injection.position = Vector3Add(wave->center, (Vector3){
            cosine * localRingRadius,
            wave->config.radius * (0.14f + heightNoise),
            sine * localRingRadius
        });
        injection.radius = wave->config.radius * (0.075f + 0.018f * grain);
        injection.velocity = (Vector3){
            cosine * localOutwardSpeed - sine * tangentialSpeed,
            wave->config.lift * (0.82f + 0.30f * grain) * s_gasShockwaveLiftMul,
            sine * localOutwardSpeed + cosine * tangentialSpeed
        };
        injection.density = 0.45f * pulseMass * intensity *
                            (0.78f + 0.32f * grain) *
                            s_gasShockwaveDensityMul;
        injection.temperature = 0.22f * pulseMass * intensity;
        injection.reaction = 2.0f * pulseMass * intensity *
                             (0.85f + 0.30f * grain) *
                             s_gasShockwaveEmissionMul;
        GasVolume_Inject(wave->gasHandle, &injection);
    }
    ++wave->ringIndex;
}

int VFX_GasShockwave_Spawn(Vector3 pos, VC_MaterialId mat,
                           const VFX_GasShockwaveConfig *requested)
{
    GasShockwave_EnsureTuning();
    VFX_GasShockwaveConfig defaults = VFX_GasShockwave_DefaultConfig();
    VFX_GasShockwaveConfig config = requested != NULL ? *requested : defaults;
    if (config.radius <= 0.0f) config.radius = defaults.radius;
    if (config.height <= 0.0f) config.height = defaults.height;
    if (config.expandDuration <= 0.0f)
        config.expandDuration = defaults.expandDuration;
    if (config.decayDuration <= 0.0f) config.decayDuration = defaults.decayDuration;
    if (config.ringsPerSecond <= 0.0f)
        config.ringsPerSecond = defaults.ringsPerSecond;
    if (config.outwardSpeed <= 0.0f) config.outwardSpeed = defaults.outwardSpeed;
    if (config.lift <= 0.0f) config.lift = defaults.lift;
    config.intensity = GasShockwave_Clamp01(config.intensity);

    const VFX_ElementMaterial *element = VFX_Material(mat);
    GasVolumeDesc volume = GasVolume_Preset(GAS_ENERGY);
    volume.priority = config.priority;
    float belowSource = config.radius * 0.13f;
    /* The authored injection orbit reaches radius, then the solver keeps
     * advecting the laid gas during decay. Leave enough horizontal margin for
     * that residual momentum so the torus fades before touching a box face. */
    float diameterWithMargin = config.radius * 2.75f;
    volume.center = Vector3Add(pos, (Vector3){
        0.0f, (config.height - belowSource) * 0.5f, 0.0f
    });
    volume.size = (Vector3){diameterWithMargin, config.height + belowSource,
                            diameterWithMargin};
    volume.lifetime = config.expandDuration + config.decayDuration;
    volume.bodyColor = element->body;
    volume.emissionColor = element->glow;
    /* Keep the front translucent enough that the near half of a low-angle
     * torus does not erase the far half. Emission carries the visual energy;
     * density supplies the smoky body and soft depth. */
    volume.densityScale = 1.35f;
    volume.emissionGain *= config.intensity * 4.35f * s_gasShockwaveEmissionMul;
    volume.buoyancy = 0.35f;
    volume.smokeWeight = 0.0f;
    volume.densityDissipation = 0.68f;
    volume.temperatureDissipation = 1.1f;
    volume.reactionDissipation = 0.92f;

    GasVolumeHandle gasHandle = GasVolume_Create(&volume);
    if (gasHandle == GAS_VOLUME_INVALID) return 0;

    s_gasShockwaveSerial++;
    if (s_gasShockwaveSerial <= 0) s_gasShockwaveSerial = 1;
    s_gasShockwave = (VC_GasShockwave){
        .active = true,
        .emitting = true,
        .serial = s_gasShockwaveSerial,
        .ringIndex = 0,
        .gasHandle = gasHandle,
        .center = pos,
        .material = mat,
        .config = config,
        .elapsed = 0.0f,
        .endTime = config.expandDuration + config.decayDuration,
        .ringAccumulator = 0.0f
    };
    return GasShockwave_Handle(&s_gasShockwave);
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
    float decayEnd = wave->elapsed + wave->config.decayDuration;
    if (decayEnd < wave->endTime) wave->endTime = decayEnd;
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
    if (!wave->active || dt <= 0.0f) return;
    wave->elapsed += dt;
    if (!GasVolume_IsAlive(wave->gasHandle) || wave->elapsed >= wave->endTime) {
        GasVolume_Destroy(wave->gasHandle);
        *wave = (VC_GasShockwave){0};
        return;
    }
    if (wave->elapsed >= wave->config.expandDuration) wave->emitting = false;

    float ringRate = wave->config.ringsPerSecond * s_gasShockwaveRateMul;
    if (!wave->emitting || ringRate <= 0.0f) return;
    wave->ringAccumulator += dt * ringRate;
    int emitted = 0;
    while (wave->ringAccumulator >= 1.0f &&
           emitted < GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME) {
        wave->ringAccumulator -= 1.0f;
        GasShockwave_EmitRing(wave, GasShockwave_Envelope(wave), ringRate);
        ++emitted;
    }
    if (emitted == GAS_SHOCKWAVE_MAX_RINGS_PER_FRAME &&
        wave->ringAccumulator > 1.0f)
        wave->ringAccumulator = 1.0f;
}

/* Drawing is owned by GasSystem's depth-aware screen-space compositor. */
static void VC_GasShockwave_Draw3D(Camera3D camera)
{
    (void)camera;
}

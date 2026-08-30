// Volumetric energy vortex — orbiting sources over core/gas.
//
// Unlike GasPlume's fixed source, this archetype writes a moving ring of hot,
// reactive gas into the volume. Tangential velocity establishes rotation,
// inward velocity tightens the ring, and a smaller vertical component turns
// the result into a rising corkscrew instead of another straight column.

#include "core/tuning.h"

#define GAS_VORTEX_TAG_BASE 0x6B000
#define GAS_VORTEX_MAX_PULSES_PER_FRAME 3

typedef struct {
    bool active;
    bool emitting;
    int serial;
    GasVolumeHandle gasHandle;
    Vector3 center;
    VC_MaterialId material;
    VFX_GasVortexConfig config;
    float elapsed;
    float endTime;
    float pulseAccumulator;
    float sourcePhase;
} VC_GasVortex;

/* GasSystem v1 admits one volume, so the composition mirrors that capacity. */
static VC_GasVortex s_gasVortex;
static int s_gasVortexSerial;
static bool s_gasVortexTuningReady;
static float s_gasVortexRateMul = 1.0f;
static float s_gasVortexOrbitMul = 1.0f;
static float s_gasVortexSwirlMul = 1.0f;
static float s_gasVortexLiftMul = 1.0f;
static float s_gasVortexDensityMul = 1.0f;
static float s_gasVortexEmissionMul = 1.0f;

static float GasVortex_Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float GasVortex_SmoothStep(float edge0, float edge1, float value)
{
    float width = edge1 - edge0;
    if (width <= 0.0001f) return value >= edge1 ? 1.0f : 0.0f;
    float t = GasVortex_Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}

static float GasVortex_Envelope(const VC_GasVortex *vortex)
{
    float duration = vortex->config.emitDuration;
    float fade = fminf(0.28f, duration * 0.22f);
    if (!vortex->emitting || fade <= 0.0001f || vortex->elapsed >= duration)
        return 0.0f;
    float fadeIn = GasVortex_SmoothStep(0.0f, fade, vortex->elapsed);
    float fadeOut = 1.0f - GasVortex_SmoothStep(duration - fade, duration,
                                                vortex->elapsed);
    return fadeIn * fadeOut;
}

static void GasVortex_EnsureTuning(void)
{
    if (s_gasVortexTuningReady) return;
    s_gasVortexTuningReady = true;
    Tuning_RegisterFloat("gasvortex_rate", &s_gasVortexRateMul, 1.0f);
    Tuning_RegisterFloat("gasvortex_orbit", &s_gasVortexOrbitMul, 1.0f);
    Tuning_RegisterFloat("gasvortex_swirl", &s_gasVortexSwirlMul, 1.0f);
    Tuning_RegisterFloat("gasvortex_lift", &s_gasVortexLiftMul, 1.0f);
    Tuning_RegisterFloat("gasvortex_density", &s_gasVortexDensityMul, 1.0f);
    Tuning_RegisterFloat("gasvortex_emission", &s_gasVortexEmissionMul, 1.0f);
}

static int GasVortex_Handle(const VC_GasVortex *vortex)
{
    return GAS_VORTEX_TAG_BASE + (vortex->serial & 0x0FFF);
}

static VC_GasVortex *GasVortex_Find(int handle)
{
    if (!s_gasVortex.active || handle != GasVortex_Handle(&s_gasVortex))
        return NULL;
    return &s_gasVortex;
}

VFX_GasVortexConfig VFX_GasVortex_DefaultConfig(void)
{
    VFX_GasVortexConfig config = {0};
    config.priority = GAS_PRIORITY_CAST;
    config.radius = 1.35f;
    config.height = 3.4f;
    config.emitDuration = 2.6f;
    config.decayDuration = 2.0f;
    config.intensity = 1.0f;
    config.pulsesPerSecond = 30.0f;
    config.angularSpeed = 5.2f;
    config.lift = 1.15f;
    return config;
}

static void GasVortex_EmitPulse(VC_GasVortex *vortex, float envelope,
                                float pulseRate)
{
    if (envelope <= 0.0f || pulseRate <= 0.0f) return;

    float intensity = GasVortex_Clamp01(vortex->config.intensity) * envelope;
    float angle = vortex->sourcePhase;
    float climb01 = GasVortex_Clamp01(vortex->elapsed /
                                      fmaxf(vortex->config.emitDuration, 0.001f));
    float cosine = cosf(angle);
    float sine = sinf(angle);
    /* A narrowing, climbing orbit authors the source path as a funnel helix.
     * The gas solver supplies the secondary roll-up and dissipation. */
    float orbitRadius = vortex->config.radius * (0.64f - 0.20f * climb01) *
                        s_gasVortexOrbitMul;
    float sourceHeight = vortex->config.radius *
                         (0.20f + 0.07f * sinf(angle * 2.0f)) +
                         vortex->config.height * 0.55f * climb01;
    float pulseMass = 15.0f / pulseRate;
    float swirlSpeed = vortex->config.radius * vortex->config.angularSpeed *
                       0.55f * s_gasVortexSwirlMul;
    float inwardSpeed = vortex->config.radius * 0.85f;

    GasInjection injection = {0};
    injection.position = Vector3Add(vortex->center, (Vector3){
        cosine * orbitRadius,
        sourceHeight,
        sine * orbitRadius
    });
    /* The source travels about one grid cell between pulses. A quarter-radius
     * footprint covers roughly three cells on the mobile grid, so successive
     * deposits overlap into a continuous coil instead of isolated sub-voxel
     * sparks that advection erases before the next frame. */
    injection.radius = vortex->config.radius * 0.25f;
    injection.velocity = (Vector3){
        -sine * swirlSpeed - cosine * inwardSpeed,
        vortex->config.lift * s_gasVortexLiftMul,
         cosine * swirlSpeed - sine * inwardSpeed
    };
    injection.density = 0.62f * pulseMass * intensity * s_gasVortexDensityMul;
    injection.temperature = 0.72f * pulseMass * intensity;
    injection.reaction = 1.25f * pulseMass * intensity * s_gasVortexEmissionMul;
    GasVolume_Inject(vortex->gasHandle, &injection);

    /* Advance per emitted pulse rather than per rendered frame. The spatial
     * coil therefore has the same pitch at 30, 60, and 120 fps. */
    vortex->sourcePhase += vortex->config.angularSpeed / pulseRate;
    if (vortex->sourcePhase >= 2.0f * PI)
        vortex->sourcePhase = fmodf(vortex->sourcePhase, 2.0f * PI);
}

int VFX_GasVortex_Spawn(Vector3 pos, VC_MaterialId mat,
                        const VFX_GasVortexConfig *requested)
{
    GasVortex_EnsureTuning();
    VFX_GasVortexConfig defaults = VFX_GasVortex_DefaultConfig();
    VFX_GasVortexConfig config = requested != NULL ? *requested : defaults;
    if (config.radius <= 0.0f) config.radius = defaults.radius;
    if (config.height <= 0.0f) config.height = defaults.height;
    if (config.emitDuration <= 0.0f) config.emitDuration = defaults.emitDuration;
    if (config.decayDuration <= 0.0f) config.decayDuration = defaults.decayDuration;
    if (config.pulsesPerSecond <= 0.0f)
        config.pulsesPerSecond = defaults.pulsesPerSecond;
    if (config.angularSpeed <= 0.0f) config.angularSpeed = defaults.angularSpeed;
    if (config.lift <= 0.0f) config.lift = defaults.lift;
    config.intensity = GasVortex_Clamp01(config.intensity);

    const VFX_ElementMaterial *element = VFX_Material(mat);
    GasVolumeDesc volume = GasVolume_Preset(GAS_ENERGY);
    volume.priority = config.priority;
    float belowSource = config.radius * 0.18f;
    float diameterWithMargin = config.radius * 2.25f;
    volume.center = Vector3Add(pos, (Vector3){
        0.0f, (config.height - belowSource) * 0.5f, 0.0f
    });
    volume.size = (Vector3){diameterWithMargin, config.height + belowSource,
                            diameterWithMargin};
    volume.lifetime = config.emitDuration + config.decayDuration;
    volume.bodyColor = element->body;
    volume.emissionColor = element->glow;
    volume.densityScale = 2.2f;
    volume.emissionGain *= config.intensity * 1.6f * s_gasVortexEmissionMul;
    volume.buoyancy = 0.65f;
    volume.smokeWeight = 0.0f;
    volume.densityDissipation = 0.30f;
    volume.temperatureDissipation = 0.72f;
    volume.reactionDissipation = 0.65f;

    GasVolumeHandle gasHandle = GasVolume_Create(&volume);
    if (gasHandle == GAS_VOLUME_INVALID) return 0;

    s_gasVortexSerial++;
    if (s_gasVortexSerial <= 0) s_gasVortexSerial = 1;
    s_gasVortex = (VC_GasVortex){
        .active = true,
        .emitting = true,
        .serial = s_gasVortexSerial,
        .gasHandle = gasHandle,
        .center = pos,
        .material = mat,
        .config = config,
        .elapsed = 0.0f,
        .endTime = config.emitDuration + config.decayDuration,
        .pulseAccumulator = 0.0f,
        .sourcePhase = 0.0f
    };
    return GasVortex_Handle(&s_gasVortex);
}

int VFX_ComposeGasVortex(Vector3 pos, VC_MaterialId mat,
                         const VFX_GasVortexConfig *config)
{
    return VFX_GasVortex_Spawn(pos, mat, config);
}

void VFX_GasVortex_SetIntensity(int handle, float intensity01)
{
    VC_GasVortex *vortex = GasVortex_Find(handle);
    if (vortex != NULL) vortex->config.intensity = GasVortex_Clamp01(intensity01);
}

void VFX_GasVortex_Stop(int handle)
{
    VC_GasVortex *vortex = GasVortex_Find(handle);
    if (vortex == NULL || !vortex->emitting) return;
    vortex->emitting = false;
    float decayEnd = vortex->elapsed + vortex->config.decayDuration;
    if (decayEnd < vortex->endTime) vortex->endTime = decayEnd;
}

void VFX_KillGasVortex(int handle)
{
    VC_GasVortex *vortex = GasVortex_Find(handle);
    if (vortex == NULL) return;
    GasVolume_Destroy(vortex->gasHandle);
    *vortex = (VC_GasVortex){0};
}

static void VC_GasVortex_Update(float dt)
{
    VC_GasVortex *vortex = &s_gasVortex;
    if (!vortex->active || dt <= 0.0f) return;
    vortex->elapsed += dt;
    if (!GasVolume_IsAlive(vortex->gasHandle) || vortex->elapsed >= vortex->endTime) {
        GasVolume_Destroy(vortex->gasHandle);
        *vortex = (VC_GasVortex){0};
        return;
    }
    if (vortex->elapsed >= vortex->config.emitDuration) vortex->emitting = false;

    float pulseRate = vortex->config.pulsesPerSecond * s_gasVortexRateMul;
    if (!vortex->emitting || pulseRate <= 0.0f) return;
    vortex->pulseAccumulator += dt * pulseRate;
    int emitted = 0;
    while (vortex->pulseAccumulator >= 1.0f &&
           emitted < GAS_VORTEX_MAX_PULSES_PER_FRAME) {
        vortex->pulseAccumulator -= 1.0f;
        GasVortex_EmitPulse(vortex, GasVortex_Envelope(vortex), pulseRate);
        ++emitted;
    }
    if (emitted == GAS_VORTEX_MAX_PULSES_PER_FRAME &&
        vortex->pulseAccumulator > 1.0f)
        vortex->pulseAccumulator = 1.0f;
}

/* Drawing is owned by GasSystem's depth-aware screen-space compositor. */
static void VC_GasVortex_Draw3D(Camera3D camera)
{
    (void)camera;
}

// ── GENERALIZED SEISMIC WAVE (HỆ THỔ: ĐỊA CHẤN BA) ──────────────────────────
//
// Dynamic Geological Ground Deformation System:
// A true terrain-deforming seismic wave propagating from caster socket to target.
//
// 1. True Terrain Deformation (Không vẽ đè lên địa hình):
//    - Directly deforms the actual ground height via MapManager_SetGroundDisplacementHook.
//    - Any map surface (including the arena floor mesh in default_arena) queries
//      MapManager_GetGroundHeightAt and physically heaves up in a continuous 3D
//      Rayleigh solitary ground wave.
//    - Characters, enemies, dummies, and props standing along the path physically
//      ride up and down on the undulating earth surface.
//    - Zero overlay meshes, zero floating rock slabs.
//
// 2. Particulates & Fluid Air Motion (Bụi và viên đất nhỏ văng ra):
//    - Traveling dust billows (VFX_ComposeImpactDustVariant) kicked up along the crest.
//    - Small soil pellets / dirt chips (VFX_ComposeDebrisShards, radius ~0.04-0.06m)
//      tossed upward and forward in the wave's surge.
//    - Aerodynamic wind impulse (Wind_SpawnRadialBlast / Wind_SpawnGust) parting air and grass.
//    - Terminal geological impact at target with expanding ground wave ring, dust ring,
//      and radial soil bursts.
//
#ifndef VC_SEISMIC_WAVE_INL
#define VC_SEISMIC_WAVE_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/color_gradient.h"
#include "core/vfx_render.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "environment/environment_system.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VC_SEISMIC_WAVE_MAX_INSTANCES 16

typedef struct {
    bool active;
    int id;
    VFX_SeismicWaveConfig config;
    float timer;
    float travelDuration;
    float totalDuration;
    float distTotal;
    Vector3 fwdNorm;
    Vector3 rightNorm;
    float frontDist;       // Current wavefront position (meters along path)
    float dustTimer;
    float debrisTimer;
    float windTimer;
    bool impactTriggered;
    float impactTimer;
} VC_SeismicWaveInstance;

static VC_SeismicWaveInstance s_seismicWaveInstances[VC_SEISMIC_WAVE_MAX_INSTANCES];
static VFX_SeismicWaveConfig s_liveSeismicWaveConfig;
static bool s_seismicWaveInit = false;
static int s_seismicWaveNextId = 1;

// ── Physical Wave Displacement Mathematical Field ───────────────────────────

static float SeismicWave_CalcDisplacement(float s, float w, float frontDist,
                                         const VFX_SeismicWaveConfig *cfg, float totalDist)
{
    float pLen = fmaxf(cfg->packetLength, 1.0f);
    float sRear = frontDist - pLen;
    if (s > frontDist || s < sRear) return 0.0f; // strictly outside active heaving packet

    // Normalized progression through active packet [0 = rear, 1 = leading front]
    float p = (s - sRear) / pLen;
    if (p <= 0.0f || p >= 1.0f) return 0.0f;

    // Smooth bell envelope: strictly 0 at rear and 0 at front with zero derivative at both ends
    float sinP = sinf(PI * p);
    float envelope = sinP * sqrtf(sinP);

    // Wave phase within packet:
    float wl = fmaxf(cfg->wavelength, 0.6f);
    float deltaS = frontDist - s; // distance behind leading front (meters)
    float u = deltaS / wl;        // wavelengths behind front

    // Primary leading crest (peaked at u = 0.38)
    float d0 = (u - 0.38f) / 0.28f;
    float crest0 = 1.0f * expf(-0.5f * d0 * d0);

    // Secondary trailing ripple (peaked at u = 1.45)
    float crest1 = 0.0f;
    if (cfg->rippleCount >= 2) {
        float d1 = (u - 1.45f) / 0.32f;
        crest1 = 0.58f * expf(-0.5f * d1 * d1);
    }

    // Tertiary trailing ripple
    float crest2 = 0.0f;
    if (cfg->rippleCount >= 3) {
        float d2 = (u - 2.50f) / 0.36f;
        crest2 = 0.34f * expf(-0.5f * d2 * d2);
    }

    // Intermediate depression trough
    float dt0 = (u - 0.92f) / 0.26f;
    float trough0 = -0.15f * expf(-0.5f * dt0 * dt0);

    float waveShape = crest0 + crest1 + crest2 + trough0;

    // Lateral cross-section profile:
    // Cosine profile ensures displacement and lateral slope smoothly hit exactly 0.0 at +/- halfWidth
    float halfW = fmaxf(cfg->waveWidth * 0.5f, 0.2f);
    if (fabsf(w) >= halfW) return 0.0f;
    float q = w / halfW; // in (-1, 1)
    float latProfile = 0.5f * (1.0f + cosf(PI * q));

    // Longitudinal boundary fades (near cast origin and near target)
    float sFade = Clamp(s / 1.0f, 0.0f, 1.0f);
    float eFade = Clamp((totalDist - s) / 0.6f, 0.0f, 1.0f);

    float dY = cfg->waveAmplitude * waveShape * envelope * latProfile * sFade * eFade;
    return fmaxf(dY, -0.12f * cfg->waveAmplitude);
}

// ── Global Ground Displacement Query (Queried by MapManager_GetGroundHeightAt) ───

float VC_SeismicWave_GetGroundDisplacementAt(float x, float z)
{
    float totalDY = 0.0f;
    for (int i = 0; i < VC_SEISMIC_WAVE_MAX_INSTANCES; i++) {
        const VC_SeismicWaveInstance *inst = &s_seismicWaveInstances[i];
        if (!inst->active) continue;

        // Vector from source to (x, z)
        float dx = x - inst->config.source.x;
        float dz = z - inst->config.source.z;

        // Project onto longitudinal and lateral axes
        float s = dx * inst->fwdNorm.x + dz * inst->fwdNorm.z;
        float w = dx * inst->rightNorm.x + dz * inst->rightNorm.z;

        float dY = SeismicWave_CalcDisplacement(s, w, inst->frontDist, &inst->config, inst->distTotal);
        if (fabsf(dY) > fabsf(totalDY)) {
            totalDY = dY;
        }
    }
    return totalDY;
}

// ── Primary Ground Wave Uniform Query (Queried by Map Ground & Vegetation Shaders) ───

bool VC_SeismicWave_GetPrimaryWaveData(Vector4 *outOriginDir, Vector4 *outWaveParams, Vector4 *outWaveProfile)
{
    if (outOriginDir) *outOriginDir = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
    if (outWaveParams) *outWaveParams = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
    if (outWaveProfile) *outWaveProfile = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};

    const VC_SeismicWaveInstance *best = NULL;
    float maxAmp = 0.0f;
    for (int i = 0; i < VC_SEISMIC_WAVE_MAX_INSTANCES; i++) {
        const VC_SeismicWaveInstance *inst = &s_seismicWaveInstances[i];
        if (inst->active && inst->config.waveAmplitude > maxAmp) {
            maxAmp = inst->config.waveAmplitude;
            best = inst;
        }
    }

    if (!best || maxAmp <= 0.001f) return false;

    if (outOriginDir) {
        *outOriginDir = (Vector4){
            best->config.source.x,
            best->config.source.z,
            best->fwdNorm.x,
            best->fwdNorm.z
        };
    }
    if (outWaveParams) {
        *outWaveParams = (Vector4){
            best->frontDist,
            best->distTotal,
            best->config.waveAmplitude,
            best->config.waveWidth
        };
    }
    if (outWaveProfile) {
        *outWaveProfile = (Vector4){
            best->config.wavelength,
            best->config.packetLength,
            (float)best->config.rippleCount,
            1.0f
        };
    }
    return true;
}

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_SeismicWaveConfig VFX_SeismicWave_DefaultConfig(void)
{
    VFX_SeismicWaveConfig cfg;
    memset(&cfg, 0, sizeof(VFX_SeismicWaveConfig));
    cfg.preset = VFX_SEISMIC_PRESET_ROLLING_SWELL;
    VFX_SeismicWave_ApplyPreset(&cfg, cfg.preset);
    return cfg;
}

VFX_SeismicWaveConfig *VFX_SeismicWave_GetLiveConfig(void)
{
    if (!s_seismicWaveInit) {
        s_liveSeismicWaveConfig = VFX_SeismicWave_DefaultConfig();
        s_seismicWaveInit = true;
    }
    return &s_liveSeismicWaveConfig;
}

void VFX_SeismicWave_ApplyPreset(VFX_SeismicWaveConfig *cfg, VFX_SeismicWavePreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;

    // Common baseline settings
    cfg->travelSpeed = 14.0f;
    cfg->waveWidth = 3.2f;
    cfg->waveAmplitude = 0.85f;
    cfg->wavelength = 2.4f;
    cfg->packetLength = 5.6f;
    cfg->rippleCount = 2;
    cfg->slabCount = 0; // Pure ground deformation: no overlay slabs
    cfg->emitDust = true;
    cfg->dustRate = 32.0f;
    cfg->emitDebris = true;
    cfg->debrisChips = 4;
    cfg->emitFissure = false;
    cfg->windImpulse = true;
    cfg->impactBurst = true;
    cfg->impactRadius = 2.8f;
    cfg->terminalPillar = false;
    cfg->matId = VC_MAT_EARTH;
    cfg->soilColor = (Color){150, 122, 90, 255};
    cfg->rockColor = (Color){110, 92, 75, 255};
    cfg->veinColor = (Color){255, 185, 70, 255};
    cfg->veinGlow = 0.0f;
    cfg->ambientGain = 0.38f;
    cfg->specularGain = 0.9f;

    switch (preset)
    {
    case VFX_SEISMIC_PRESET_ROLLING_SWELL:
        cfg->style = VFX_SEISMIC_STYLE_ROLLING_EARTH;
        cfg->travelSpeed = 12.0f;
        cfg->waveWidth = 3.2f;
        cfg->waveAmplitude = 0.80f;
        cfg->wavelength = 2.4f;
        cfg->packetLength = 5.5f;
        cfg->rippleCount = 2;
        cfg->dustRate = 30.0f;
        cfg->impactRadius = 2.6f;
        break;

    case VFX_SEISMIC_PRESET_JAGGED_FRACTURE:
        cfg->style = VFX_SEISMIC_STYLE_FRACTURED_SLABS;
        cfg->travelSpeed = 15.0f;
        cfg->waveWidth = 3.4f;
        cfg->waveAmplitude = 0.95f;
        cfg->wavelength = 2.0f;
        cfg->packetLength = 5.8f;
        cfg->rippleCount = 2;
        cfg->dustRate = 36.0f;
        cfg->impactRadius = 3.0f;
        break;

    case VFX_SEISMIC_PRESET_TECTONIC_PULSE:
        cfg->style = VFX_SEISMIC_STYLE_FRACTURED_SLABS;
        cfg->travelSpeed = 10.0f;
        cfg->waveWidth = 4.2f;
        cfg->waveAmplitude = 1.30f;
        cfg->wavelength = 3.2f;
        cfg->packetLength = 7.0f;
        cfg->rippleCount = 1;
        cfg->dustRate = 42.0f;
        cfg->impactRadius = 3.8f;
        break;

    case VFX_SEISMIC_PRESET_RAPID_TREMOR:
        cfg->style = VFX_SEISMIC_STYLE_ROLLING_EARTH;
        cfg->travelSpeed = 20.0f;
        cfg->waveWidth = 2.4f;
        cfg->waveAmplitude = 0.55f;
        cfg->wavelength = 1.4f;
        cfg->packetLength = 4.2f;
        cfg->rippleCount = 3;
        cfg->dustRate = 28.0f;
        cfg->impactRadius = 2.2f;
        break;

    case VFX_SEISMIC_PRESET_JADE_CHASM:
        cfg->style = VFX_SEISMIC_STYLE_JADE_CRYSTAL;
        cfg->travelSpeed = 14.0f;
        cfg->waveWidth = 3.2f;
        cfg->waveAmplitude = 0.85f;
        cfg->wavelength = 2.2f;
        cfg->packetLength = 5.4f;
        cfg->rippleCount = 2;
        cfg->dustRate = 30.0f;
        cfg->impactRadius = 2.8f;
        break;

    case VFX_SEISMIC_PRESET_MAGMA_FISSURE:
        cfg->style = VFX_SEISMIC_STYLE_MAGMA_BASALT;
        cfg->matId = VC_MAT_FIRE;
        cfg->travelSpeed = 13.0f;
        cfg->waveWidth = 3.4f;
        cfg->waveAmplitude = 0.90f;
        cfg->wavelength = 2.4f;
        cfg->packetLength = 5.6f;
        cfg->rippleCount = 2;
        cfg->dustRate = 34.0f;
        cfg->impactRadius = 3.2f;
        break;

    case VFX_SEISMIC_PRESET_CUSTOM:
    default:
        break;
    }
}

const char *VFX_SeismicWavePreset_Name(VFX_SeismicWavePreset preset)
{
    static const char *const names[] = {
        "Rolling Swell", "Jagged Fracture", "Tectonic Pulse",
        "Rapid Tremor", "Jade Chasm", "Magma Fissure", "Custom"
    };
    return (preset >= 0 && preset < VFX_SEISMIC_PRESET_COUNT) ? names[preset] : "Unknown";
}

const char *VFX_SeismicWaveStyle_Name(VFX_SeismicWaveStyle style)
{
    static const char *const names[] = {
        "Rolling Earth", "Fractured Slabs", "Jade Crystal", "Magma Basalt"
    };
    return (style >= 0 && style < VFX_SEISMIC_STYLE_COUNT) ? names[style] : "Unknown";
}

// ── Sandbox Inspector Parameter Metadata ─────────────────────────────────────

int VFX_SeismicWave_GetParams(VFX_SeismicWaveConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *const presetNames[] = {
        "Rolling Swell", "Jagged Fracture", "Tectonic Pulse",
        "Rapid Tremor", "Jade Chasm", "Magma Fissure", "Custom"
    };
    static const char *const styleNames[] = {
        "Rolling Earth", "Fractured Slabs", "Jade Crystal", "Magma Basalt"
    };

#define SEISMIC_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define SEISMIC_PARAM_ENUM(pname, pgroup, val, enames, ecount) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .enumNames = enames, .enumCount = ecount, \
        .minInt = 0, .maxInt = (ecount) - 1, .stepFloat = 1.0f \
    }; } while(0)

#define SEISMIC_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define SEISMIC_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = 1, \
        .stepFloat = 1.0f \
    }; } while(0)

    SEISMIC_PARAM_ENUM("Preset", "Overview", cfg->preset, presetNames, VFX_SEISMIC_PRESET_COUNT);
    SEISMIC_PARAM_ENUM("Style", "Overview", cfg->style, styleNames, VFX_SEISMIC_STYLE_COUNT);

    SEISMIC_PARAM_FLOAT("Travel Speed", "Dynamics", cfg->travelSpeed, 4.0f, 40.0f, 1.0f);
    SEISMIC_PARAM_FLOAT("Wave Width", "Dynamics", cfg->waveWidth, 1.0f, 7.0f, 0.2f);
    SEISMIC_PARAM_FLOAT("Amplitude", "Dynamics", cfg->waveAmplitude, 0.2f, 2.5f, 0.05f);
    SEISMIC_PARAM_FLOAT("Wavelength", "Dynamics", cfg->wavelength, 0.8f, 6.0f, 0.2f);
    SEISMIC_PARAM_FLOAT("Packet Length", "Dynamics", cfg->packetLength, 2.0f, 12.0f, 0.5f);
    SEISMIC_PARAM_INT("Ripple Count", "Dynamics", cfg->rippleCount, 1, 4);

    SEISMIC_PARAM_BOOL("Emit Dust", "Particulates", cfg->emitDust);
    SEISMIC_PARAM_FLOAT("Dust Rate", "Particulates", cfg->dustRate, 5.0f, 60.0f, 2.0f);
    SEISMIC_PARAM_BOOL("Emit Soil Pellets", "Particulates", cfg->emitDebris);
    SEISMIC_PARAM_INT("Pellet Count", "Particulates", cfg->debrisChips, 0, 16);
    SEISMIC_PARAM_BOOL("Wind Impulse", "Particulates", cfg->windImpulse);

    SEISMIC_PARAM_BOOL("Impact Burst", "Terminal Impact", cfg->impactBurst);
    SEISMIC_PARAM_FLOAT("Impact Radius", "Terminal Impact", cfg->impactRadius, 1.0f, 6.0f, 0.25f);

#undef SEISMIC_PARAM_ENUM
#undef SEISMIC_PARAM_FLOAT
#undef SEISMIC_PARAM_INT
#undef SEISMIC_PARAM_BOOL

    return n;
}

// ── Spawn & Lifecycle API ───────────────────────────────────────────────────

int VFX_ComposeSeismicWaveEx(Vector3 source, Vector3 target, const VFX_SeismicWaveConfig *cfg)
{
    if (!cfg) cfg = VFX_SeismicWave_GetLiveConfig();
    EarthFx_InitShared();

    // Register our displacement and wave param hooks with MapManager so the entire terrain deforms
    MapManager_SetGroundDisplacementHook(VC_SeismicWave_GetGroundDisplacementAt);
    MapManager_SetGroundWaveParamsHook(VC_SeismicWave_GetPrimaryWaveData);

    float distXZ = sqrtf((target.x - source.x) * (target.x - source.x) +
                         (target.z - source.z) * (target.z - source.z));
    if (distXZ < 0.25f) return 0; // target too close to source

    // Find available instance slot
    VC_SeismicWaveInstance *inst = NULL;
    for (int i = 0; i < VC_SEISMIC_WAVE_MAX_INSTANCES; i++) {
        if (!s_seismicWaveInstances[i].active) {
            inst = &s_seismicWaveInstances[i];
            break;
        }
    }
    if (!inst) {
        float oldestTime = -1.0f;
        for (int i = 0; i < VC_SEISMIC_WAVE_MAX_INSTANCES; i++) {
            if (s_seismicWaveInstances[i].timer > oldestTime) {
                oldestTime = s_seismicWaveInstances[i].timer;
                inst = &s_seismicWaveInstances[i];
            }
        }
    }
    if (!inst) return 0;

    memset(inst, 0, sizeof(VC_SeismicWaveInstance));
    inst->active = true;
    inst->id = s_seismicWaveNextId++;
    inst->config = *cfg;
    inst->config.source = source;
    inst->config.target = target;
    inst->distTotal = distXZ;

    float speed = fmaxf(cfg->travelSpeed, 1.0f);
    inst->travelDuration = distXZ / speed;
    inst->totalDuration = inst->travelDuration + 0.90f; // brief linger for particulate settling

    Vector3 fwd = Vector3Subtract(target, source);
    inst->fwdNorm = (Vector3){fwd.x / distXZ, 0.0f, fwd.z / distXZ};
    inst->rightNorm = (Vector3){-inst->fwdNorm.z, 0.0f, inst->fwdNorm.x};

    inst->frontDist = 0.0f;
    inst->dustTimer = 0.0f;
    inst->debrisTimer = 0.0f;
    inst->windTimer = 0.0f;
    inst->impactTriggered = false;
    inst->impactTimer = 0.0f;

    return inst->id;
}

int VFX_ComposeSeismicWave(const VFX_SeismicWaveConfig *cfg)
{
    if (!cfg) cfg = VFX_SeismicWave_GetLiveConfig();
    return VFX_ComposeSeismicWaveEx(cfg->source, cfg->target, cfg);
}

void VFX_KillSeismicWave(int handle)
{
    for (int i = 0; i < VC_SEISMIC_WAVE_MAX_INSTANCES; i++) {
        if (s_seismicWaveInstances[i].active && s_seismicWaveInstances[i].id == handle) {
            s_seismicWaveInstances[i].active = false;
            break;
        }
    }
}

// ── Physical Simulation & Update ────────────────────────────────────────────

void VC_SeismicWave_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    bool anyActive = false;

    for (int instIdx = 0; instIdx < VC_SEISMIC_WAVE_MAX_INSTANCES; instIdx++) {
        VC_SeismicWaveInstance *inst = &s_seismicWaveInstances[instIdx];
        if (!inst->active) continue;

        anyActive = true;
        inst->timer += dt;
        if (inst->timer >= inst->totalDuration) {
            inst->active = false;
            continue;
        }

        const VFX_SeismicWaveConfig *cfg = &inst->config;

        // Advance wavefront position
        inst->frontDist = fminf(inst->timer * cfg->travelSpeed, inst->distTotal);

        Vector3 frontPos = Vector3Add(cfg->source, Vector3Scale(inst->fwdNorm, inst->frontDist));
        frontPos.y = MapManager_GetGroundHeightAt(frontPos.x, frontPos.z);

        // 1. Moving Wind Impulse (parts vegetation along wavefront)
        if (cfg->windImpulse && inst->frontDist < inst->distTotal) {
            inst->windTimer += dt;
            if (inst->windTimer >= 0.06f) {
                inst->windTimer = 0.0f;
                float wSpeed = fminf(cfg->travelSpeed * 0.45f, 8.0f);
                Wind_SpawnRadialBlast(frontPos, cfg->waveWidth * 1.3f, 5.0f, 0.16f);
                Wind_SpawnGust(frontPos, inst->fwdNorm, cfg->waveWidth * 1.5f, wSpeed, 0.14f);
            }
        }

        // 2. Particulate Dust Puffs along Moving Crest
        if (cfg->emitDust && inst->frontDist < inst->distTotal) {
            inst->dustTimer += dt;
            float dustInterval = 1.0f / fmaxf(cfg->dustRate, 5.0f);
            if (inst->dustTimer >= dustInterval) {
                inst->dustTimer = 0.0f;
                VFX_ComposeImpactDustVariant(frontPos, VC_MAT_EARTH, cfg->waveWidth * 0.45f, 0.35f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            }
        }

        // 3. Small Soil Pellets / Dirt Chips kicked up along the heaving front
        if (cfg->emitDebris && inst->frontDist < inst->distTotal) {
            inst->debrisTimer += dt;
            if (inst->debrisTimer >= 0.07f) {
                inst->debrisTimer = 0.0f;
                // Soil chips fly upward and slightly forward in the surge
                Vector3 clodVel = (Vector3){
                    inst->fwdNorm.x * 1.6f,
                    2.2f,
                    inst->fwdNorm.z * 1.6f
                };
                VFX_ComposeDebrisShards(frontPos, clodVel, VC_MAT_EARTH, 0.045f, 4);
            }
        }

        // 4. Terminal Impact Eruption at Target
        if (inst->frontDist >= inst->distTotal) {
            inst->impactTimer += dt;
            if (!inst->impactTriggered) {
                inst->impactTriggered = true;
                Vector3 tgt = cfg->target;
                tgt.y = MapManager_GetGroundHeightAt(tgt.x, tgt.z);

                // Expansive Ground Shockwave Ring
                if (cfg->impactBurst) {
                    VFX_ComposeGroundWave(tgt, VC_MAT_EARTH, cfg->impactRadius * 1.2f, 0.35f, VFX_GroundHeightFromMap, NULL);
                }

                // Small Soil Burst
                if (cfg->emitDebris) {
                    VFX_ComposeDebrisShards(tgt, (Vector3){0.0f, 4.0f, 0.0f}, VC_MAT_EARTH, 0.05f, 8);
                }

                // Expanding Dust Ring & Clouds
                if (cfg->emitDust) {
                    VFX_ComposeGroundDustRing(tgt, VC_MAT_EARTH, cfg->impactRadius * 1.1f, 0.55f);
                    VFX_ComposeImpactDustVariant(tgt, VC_MAT_EARTH, cfg->impactRadius * 1.2f, 0.50f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                }

                // Radial Wind Shockwave
                if (cfg->windImpulse) {
                    Wind_SpawnRadialBlast(tgt, cfg->impactRadius * 2.2f, 7.5f, 0.35f);
                }
            }
        }
    }

    MapManager_SetGroundDisplacementHook(VC_SeismicWave_GetGroundDisplacementAt);
    MapManager_SetGroundWaveParamsHook(VC_SeismicWave_GetPrimaryWaveData);
}

// ── Rendering & Shadow Casting Pass ─────────────────────────────────────────
//
// The actual ground surface itself deforms physically in 3D through MapManager_GetGroundHeightAt.
// Particulates (dust, soil chips, ground wave ring) are rendered into their respective particle
// and VFX passes. No separate geometry is drawn over the terrain.

void VC_SeismicWave_DrawShadowPass(void)
{
    // Zero separate meshes: terrain casts its own smart shadows
}

void VC_SeismicWave_Draw3D(Camera3D cam)
{
    (void)cam;
    // Zero separate overlay meshes: terrain itself undulates
}

#endif // VC_SEISMIC_WAVE_INL

// ── GRANULAR MATERIALS (GPU Gems 3 Ch.29 DEM: Sand, Gravel, Loam & Coupled Dust) ──
#ifndef VC_GRANULAR_SAND_INL
#define VC_GRANULAR_SAND_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/granular/granular_sim.h"
#include "core/motion/motion_fields.h"
#include "core/map_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    bool active;
    VFX_GranularSandConfig config;
    float timer;
    float dustAcc;
    Vector3 dustOrigin;
} VC_GranularSandInstance;

static VC_GranularSandInstance s_granularSandInst = {0};
static VFX_GranularSandConfig s_liveGranularSandConfig;
static bool s_liveGranularSandInit = false;

static float GranularSand_Hash01(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffu) / 16777216.0f;
}

VFX_GranularSandConfig VFX_GranularSand_DefaultConfig(void)
{
    VFX_GranularSandConfig cfg = {0};
    cfg.preset        = VFX_GRANULAR_PRESET_SAND_GEYSER;
    cfg.materialId    = GRANULAR_MAT_DESERT_SAND;
    cfg.grainCount    = 2560;
    cfg.grainRadius   = 0.018f;
    cfg.spreadRadius  = 0.45f;
    cfg.launchSpeed   = 6.4f;
    cfg.swirlSpeed    = 3.2f;
    cfg.lifetime      = 4.2f;
    cfg.dustIntensity = 1.15f;
    cfg.source        = (Vector3){0.0f, 0.0f, 0.0f};
    cfg.target        = (Vector3){5.0f, 0.0f, 1.8f};
    return cfg;
}

VFX_GranularSandConfig *VFX_GranularSand_GetLiveConfig(void)
{
    if (!s_liveGranularSandInit) {
        s_liveGranularSandConfig = VFX_GranularSand_DefaultConfig();
        s_liveGranularSandInit = true;
    }
    return &s_liveGranularSandConfig;
}

void VFX_GranularSand_ApplyPreset(VFX_GranularSandConfig *cfg, VFX_GranularPreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;
    switch (preset) {
    case VFX_GRANULAR_PRESET_SAND_GEYSER:
        cfg->materialId    = GRANULAR_MAT_DESERT_SAND;
        cfg->grainCount    = 2560;
        cfg->grainRadius   = 0.018f;
        cfg->spreadRadius  = 0.45f;
        cfg->launchSpeed   = 6.4f;
        cfg->swirlSpeed    = 3.2f;
        cfg->lifetime      = 4.2f;
        cfg->dustIntensity = 1.15f;
        break;
    case VFX_GRANULAR_PRESET_DUNE_AVALANCHE:
        cfg->materialId    = GRANULAR_MAT_DESERT_SAND;
        cfg->grainCount    = 2560;
        cfg->grainRadius   = 0.018f;
        cfg->spreadRadius  = 0.72f;
        cfg->launchSpeed   = 8.5f;
        cfg->swirlSpeed    = 0.8f;
        cfg->lifetime      = 4.4f;
        cfg->dustIntensity = 1.25f;
        break;
    case VFX_GRANULAR_PRESET_QUICKSAND_VORTEX:
        cfg->materialId    = GRANULAR_MAT_LOESS_SOIL;
        cfg->grainCount    = 2048;
        cfg->grainRadius   = 0.020f;
        cfg->spreadRadius  = 1.25f;
        cfg->launchSpeed   = 2.6f;
        cfg->swirlSpeed    = 6.2f;
        cfg->lifetime      = 4.6f;
        cfg->dustIntensity = 1.05f;
        break;
    case VFX_GRANULAR_PRESET_SAND_BLAST_JET:
        cfg->materialId    = GRANULAR_MAT_DESERT_SAND;
        cfg->grainCount    = 2048;
        cfg->grainRadius   = 0.017f;
        cfg->spreadRadius  = 0.38f;
        cfg->launchSpeed   = 12.0f;
        cfg->swirlSpeed    = 1.5f;
        cfg->lifetime      = 3.8f;
        cfg->dustIntensity = 1.10f;
        break;
    case VFX_GRANULAR_PRESET_BOULDER_CRUSH_TALUS:
        cfg->materialId    = GRANULAR_MAT_COARSE_GRAVEL;
        cfg->grainCount    = 1024;
        cfg->grainRadius   = 0.062f;
        cfg->spreadRadius  = 0.95f;
        cfg->launchSpeed   = 5.2f;
        cfg->swirlSpeed    = 0.0f;
        cfg->lifetime      = 4.8f;
        cfg->dustIntensity = 0.90f;
        break;
    case VFX_GRANULAR_PRESET_HOURGLASS_PILLAR:
        cfg->materialId    = GRANULAR_MAT_GOLD_IRON_SAND;
        cfg->grainCount    = 1536;
        cfg->grainRadius   = 0.035f;
        cfg->spreadRadius  = 0.55f;
        cfg->launchSpeed   = 2.6f;
        cfg->swirlSpeed    = 3.4f;
        cfg->lifetime      = 4.8f;
        cfg->dustIntensity = 0.55f;
        break;
    default:
        break;
    }
}

const char *VFX_GranularSandPreset_Name(VFX_GranularPreset preset)
{
    static const char *const names[VFX_GRANULAR_PRESET_COUNT] = {
        "Sand Geyser",
        "Dune Avalanche",
        "Quicksand Vortex",
        "Sand Blast Jet",
        "Boulder Crush Talus",
        "Hourglass Pillar"
    };
    return (preset >= 0 && preset < VFX_GRANULAR_PRESET_COUNT) ? names[preset] : "Unknown";
}

int VFX_GranularSand_GetParams(VFX_GranularSandConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;
    static const char *const presetNames[] = {
        "Sand Geyser", "Dune Avalanche", "Quicksand Vortex",
        "Sand Blast Jet", "Boulder Crush Talus", "Hourglass Pillar"
    };
    static const char *const matNames[] = {
        "Desert Sand", "Coarse Gravel", "Loess Soil", "Gold Iron Sand", "Volcanic Ash"
    };

    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Preset", .group = "Preset", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->preset, .minInt = 0, .maxInt = VFX_GRANULAR_PRESET_COUNT - 1,
        .stepFloat = 1.0f, .enumNames = presetNames, .enumCount = VFX_GRANULAR_PRESET_COUNT
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Material", .group = "Material", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->materialId, .minInt = 0, .maxInt = GRANULAR_MAT_COUNT - 1,
        .stepFloat = 1.0f, .enumNames = matNames, .enumCount = GRANULAR_MAT_COUNT
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Grain count", .group = "DEM", .type = VFX_PARAM_INT,
        .valPtr = &cfg->grainCount, .minInt = 128, .maxInt = GRANULAR_GPU_MAX_GRAINS, .stepFloat = 128.0f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Grain radius m", .group = "DEM", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->grainRadius, .minFloat = 0.02f, .maxFloat = 0.09f, .stepFloat = 0.004f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Spread radius m", .group = "Shape", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->spreadRadius, .minFloat = 0.2f, .maxFloat = 2.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Launch speed", .group = "Motion", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->launchSpeed, .minFloat = 1.0f, .maxFloat = 18.0f, .stepFloat = 0.5f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Swirl rad/s", .group = "Motion", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->swirlSpeed, .minFloat = 0.0f, .maxFloat = 10.0f, .stepFloat = 0.25f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Dust coupling", .group = "Optics", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->dustIntensity, .minFloat = 0.0f, .maxFloat = 1.5f, .stepFloat = 0.05f
    };
    return n;
}

int VFX_ComposeGranularSandEx(Vector3 source, Vector3 target, const VFX_GranularSandConfig *cfg)
{
    if (!cfg) cfg = VFX_GranularSand_GetLiveConfig();
    GranularSim_Init();

    static GranularGrainState s_spawnBuf[GRANULAR_GPU_MAX_GRAINS];
    int count = cfg->grainCount;
    if (count < 64) count = 64;
    if (count > GRANULAR_GPU_MAX_GRAINS) count = GRANULAR_GPU_MAX_GRAINS;

    Vector3 dir = Vector3Subtract(target, source);
    dir.y = 0.0f;
    float distXZ = Vector3Length(dir);
    Vector3 fwd = distXZ > 0.05f ? Vector3Scale(dir, 1.0f / distXZ) : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = {-fwd.z, 0.0f, fwd.x};

    Vector3 groundPoint = source;
    Vector3 groundNormal = {0.0f, 1.0f, 0.0f};
    Vector3 surfPos = {0}, surfNorm = {0};
    if (MapManager_SampleGroundSurfaceAt(source.x, source.z, &surfPos, &surfNorm)) {
        groundPoint = surfPos;
        groundNormal = surfNorm;
    } else {
        groundPoint.y = MapManager_GetGroundHeightAt(source.x, source.z);
    }

    for (int i = 0; i < count; i++) {
        unsigned int seedBase = (unsigned int)i * 747796405u + 2891336453u;
        float u0 = GranularSand_Hash01(seedBase + 1u);
        float u1 = GranularSand_Hash01(seedBase + 2u);
        float u2 = GranularSand_Hash01(seedBase + 3u);
        float u3 = GranularSand_Hash01(seedBase + 4u);

        float angle = u0 * 2.0f * PI;
        float radial = sqrtf(u1) * cfg->spreadRadius;
        Vector3 pos = groundPoint;
        Vector3 vel = {0.0f, 0.0f, 0.0f};

        switch (cfg->preset) {
        case VFX_GRANULAR_PRESET_SAND_GEYSER:
            pos.x += cosf(angle) * radial * 0.45f;
            pos.z += sinf(angle) * radial * 0.45f;
            pos.y += 0.05f + u2 * 0.55f;
            vel.x = cosf(angle) * (0.8f + 1.6f * u1) - sinf(angle) * cfg->swirlSpeed * 0.35f;
            vel.z = sinf(angle) * (0.8f + 1.6f * u1) + cosf(angle) * cfg->swirlSpeed * 0.35f;
            vel.y = cfg->launchSpeed * (0.65f + 0.55f * u3);
            break;

        case VFX_GRANULAR_PRESET_DUNE_AVALANCHE:
            pos.x += fwd.x * (u2 * 0.8f - 0.3f) + right.x * ((u0 - 0.5f) * cfg->spreadRadius * 2.0f);
            pos.z += fwd.z * (u2 * 0.8f - 0.3f) + right.z * ((u0 - 0.5f) * cfg->spreadRadius * 2.0f);
            pos.y += 0.12f + u1 * 0.95f;
            vel = Vector3Add(Vector3Scale(fwd, cfg->launchSpeed * (0.70f + 0.45f * u3)),
                             (Vector3){0.0f, 2.2f + 1.8f * u2, 0.0f});
            break;

        case VFX_GRANULAR_PRESET_QUICKSAND_VORTEX:
            pos.x += cosf(angle) * (0.35f + radial);
            pos.z += sinf(angle) * (0.35f + radial);
            pos.y += 0.08f + u2 * 0.65f;
            vel.x = -sinf(angle) * cfg->swirlSpeed * 0.8f - cosf(angle) * 0.9f;
            vel.z =  cosf(angle) * cfg->swirlSpeed * 0.8f - sinf(angle) * 0.9f;
            vel.y = cfg->launchSpeed * (0.35f + 0.5f * u3);
            break;

        case VFX_GRANULAR_PRESET_SAND_BLAST_JET:
            pos.x += fwd.x * (u2 * 0.6f) + right.x * ((u0 - 0.5f) * cfg->spreadRadius);
            pos.z += fwd.z * (u2 * 0.6f) + right.z * ((u0 - 0.5f) * cfg->spreadRadius);
            pos.y += 0.45f + (u1 - 0.5f) * 0.35f;
            vel = Vector3Add(Vector3Scale(fwd, cfg->launchSpeed * (0.78f + 0.35f * u3)),
                             (Vector3){(u0 - 0.5f) * 2.0f, 1.4f + (u1 - 0.5f) * 1.4f, (u2 - 0.5f) * 2.0f});
            break;

        case VFX_GRANULAR_PRESET_BOULDER_CRUSH_TALUS:
            pos.x += cosf(angle) * radial * 0.75f;
            pos.z += sinf(angle) * radial * 0.75f;
            pos.y += 1.4f + u2 * 1.6f;
            vel.x = cosf(angle) * cfg->launchSpeed * 0.45f * u1;
            vel.z = sinf(angle) * cfg->launchSpeed * 0.45f * u1;
            vel.y = -2.0f - 2.5f * u3;
            break;

        case VFX_GRANULAR_PRESET_HOURGLASS_PILLAR:
            pos.x += cosf(angle) * radial * 0.55f;
            pos.z += sinf(angle) * radial * 0.55f;
            pos.y += 0.35f + u2 * 2.4f;
            vel.x = -sinf(angle) * cfg->swirlSpeed * 0.45f;
            vel.z =  cosf(angle) * cfg->swirlSpeed * 0.45f;
            vel.y = cfg->launchSpeed * (0.25f + 0.35f * u3);
            break;

        default:
            break;
        }

        s_spawnBuf[i] = (GranularGrainState){
            .position   = pos,
            .velocity   = vel,
            .radius     = cfg->grainRadius * (0.78f + 0.44f * u3),
            .lifetime   = cfg->lifetime * (0.85f + 0.30f * u2),
            .seed       = u0,
            .contactAO  = 0.0f,
            .materialId = (uint32_t)cfg->materialId,
            .flags      = 0u
        };
    }

    int spawned = GranularSim_SpawnCluster(s_spawnBuf, count, groundPoint, groundNormal);
    s_granularSandInst.active = (spawned > 0);
    s_granularSandInst.config = *cfg;
    s_granularSandInst.config.source = groundPoint;
    s_granularSandInst.config.target = target;
    s_granularSandInst.timer = 0.0f;
    s_granularSandInst.dustAcc = 0.0f;
    s_granularSandInst.dustOrigin = groundPoint;

    if (cfg->preset == VFX_GRANULAR_PRESET_QUICKSAND_VORTEX) {
        MotionFields_SpawnStaticVortex(groundPoint, (Vector3){0.0f, 1.0f, 0.0f},
                                       cfg->spreadRadius * 1.6f, cfg->swirlSpeed,
                                       2.5f, 1.8f, 0.1f, 0.4f);
    } else if (cfg->preset == VFX_GRANULAR_PRESET_DUNE_AVALANCHE) {
        Vector3 pts[5];
        for (int k = 0; k < 5; k++) {
            float t = (float)k / 4.0f;
            pts[k] = Vector3Lerp(groundPoint, target, t);
            pts[k].y = MapManager_GetGroundHeightAt(pts[k].x, pts[k].z) + 0.45f * sinf(t * PI);
        }
        MotionPath mp;
        if (MotionPath_Build(&mp, pts, 5)) {
            MotionFields_SpawnMovingGuide(&mp, fmaxf(cfg->launchSpeed, 4.0f),
                                          1.6f, 2.2f, 2.0f, 1.5f, 1.6f);
        }
    }

    if (cfg->dustIntensity > 0.05f) {
        VFX_ComposeImpactDustVariant(groundPoint, VC_MAT_EARTH,
                                     fmaxf(1.0f, cfg->spreadRadius * 1.4f),
                                     Clamp(cfg->dustIntensity, 0.1f, 1.0f),
                                     VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    }
    return spawned > 0 ? 1 : 0;
}

int VFX_ComposeGranularSand(Vector3 pos, VFX_GranularPreset preset)
{
    VFX_GranularSandConfig cfg = *VFX_GranularSand_GetLiveConfig();
    if (preset >= 0 && preset < VFX_GRANULAR_PRESET_COUNT && preset != cfg.preset) {
        VFX_GranularSand_ApplyPreset(&cfg, preset);
    }
    Vector3 target = Vector3Add(pos, (Vector3){5.0f, 0.0f, 1.8f});
    return VFX_ComposeGranularSandEx(pos, target, &cfg);
}

void VFX_KillGranularSand(int handle)
{
    (void)handle;
    s_granularSandInst.active = false;
    GranularSim_Reset();
}

static void VC_GranularSand_Update(float dt)
{
    if (s_granularSandInst.active) {
        s_granularSandInst.timer += dt;
        const VFX_GranularSandConfig *cfg = &s_granularSandInst.config;

        if (cfg->preset == VFX_GRANULAR_PRESET_QUICKSAND_VORTEX && s_granularSandInst.timer < 1.8f) {
            GranularSim_ApplyForceImpulse(cfg->source, cfg->spreadRadius * 1.6f,
                                          (Vector3){0.0f, 2.5f, 0.0f}, cfg->swirlSpeed);
        } else if (cfg->preset == VFX_GRANULAR_PRESET_HOURGLASS_PILLAR && s_granularSandInst.timer < 0.75f) {
            GranularSim_ApplyForceImpulse(Vector3Add(cfg->source, (Vector3){0.0f, 1.2f, 0.0f}),
                                          cfg->spreadRadius * 2.2f,
                                          (Vector3){0.0f, 9.5f, 0.0f}, cfg->swirlSpeed);
        }

        // Coupled fine-dust entrainment during active saltation & avalanche window
        if (cfg->dustIntensity > 0.05f && s_granularSandInst.timer > 0.15f && s_granularSandInst.timer < 1.85f) {
            s_granularSandInst.dustAcc += dt * 2.4f * cfg->dustIntensity;
            if (s_granularSandInst.dustAcc >= 1.0f) {
                s_granularSandInst.dustAcc -= 1.0f;
                VFX_ComposeImpactDustVariant(s_granularSandInst.dustOrigin, VC_MAT_EARTH,
                                             cfg->spreadRadius * 1.1f,
                                             Clamp(cfg->dustIntensity * 0.55f, 0.15f, 0.85f),
                                             VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            }
        }
    }

    GranularSim_Update(dt);
    if (GranularSim_GetActiveCount() <= 0) {
        s_granularSandInst.active = false;
    }
}

static void VC_GranularSand_Draw3D(Camera3D cam)
{
    GranularSim_DrawBody(cam);
}

#endif // VC_GRANULAR_SAND_INL

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
    bool burstFired;
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
    cfg.preset              = VFX_GRANULAR_PRESET_WUXIA_LEAF_PALM_STRIKE;
    cfg.materialId          = GRANULAR_MAT_DRY_LEAF;
    cfg.grainCount          = 2048;
    cfg.grainRadius         = 0.036f;
    cfg.spreadRadius        = 1.25f;
    cfg.launchSpeed         = 10.5f;
    cfg.swirlSpeed          = 6.8f;
    cfg.lifetime            = 6.5f;
    cfg.dustIntensity       = 0.45f;
    cfg.source              = (Vector3){0.0f, 0.0f, 0.0f};
    cfg.target              = (Vector3){7.5f, 0.0f, 2.2f};
    cfg.secondaryMaterialId = GRANULAR_MAT_DRY_TWIG;
    cfg.secondaryRatio      = 0.25f;
    cfg.bedRadius           = 3.4f;
    cfg.chargeDuration      = 0.75f;
    cfg.waveSpeed           = 9.5f;
    cfg.tertiaryMaterialId  = GRANULAR_MAT_LOESS_SOIL;
    cfg.tertiaryRatio       = 0.0f;
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
    cfg->secondaryRatio = 0.0f;
    cfg->tertiaryRatio = 0.0f;
    cfg->bedRadius = 2.5f;
    cfg->chargeDuration = 0.0f;
    cfg->waveSpeed = 8.0f;
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
    case VFX_GRANULAR_PRESET_WUXIA_LEAF_PALM_STRIKE:
        cfg->materialId          = GRANULAR_MAT_DRY_LEAF;
        cfg->secondaryMaterialId = GRANULAR_MAT_DRY_TWIG;
        cfg->secondaryRatio      = 0.25f;
        cfg->grainCount          = 2048;
        cfg->grainRadius         = 0.036f;
        cfg->spreadRadius        = 1.25f;
        cfg->bedRadius           = 3.4f;
        cfg->chargeDuration      = 0.75f;
        cfg->waveSpeed           = 9.5f;
        cfg->launchSpeed         = 10.5f;
        cfg->swirlSpeed          = 6.8f;
        cfg->lifetime            = 6.5f;
        cfg->dustIntensity       = 0.45f;
        break;
    case VFX_GRANULAR_PRESET_BLOSSOM_QI_TEMPEST:
        cfg->materialId          = GRANULAR_MAT_FLOWER_PETAL;
        cfg->secondaryMaterialId = GRANULAR_MAT_FRESH_LEAF;
        cfg->secondaryRatio      = 0.30f;
        cfg->grainCount          = 2048;
        cfg->grainRadius         = 0.032f;
        cfg->spreadRadius        = 1.40f;
        cfg->bedRadius           = 3.2f;
        cfg->chargeDuration      = 0.55f;
        cfg->waveSpeed           = 8.5f;
        cfg->launchSpeed         = 8.8f;
        cfg->swirlSpeed          = 8.4f;
        cfg->lifetime            = 6.8f;
        cfg->dustIntensity       = 0.25f;
        break;
    case VFX_GRANULAR_PRESET_FROST_SHARD_BLIZZARD:
        cfg->materialId          = GRANULAR_MAT_SNOW_POWDER;
        cfg->secondaryMaterialId = GRANULAR_MAT_ICE_SHARD;
        cfg->secondaryRatio      = 0.35f;
        cfg->grainCount          = 2560;
        cfg->grainRadius         = 0.026f;
        cfg->spreadRadius        = 1.15f;
        cfg->bedRadius           = 3.0f;
        cfg->chargeDuration      = 0.50f;
        cfg->waveSpeed           = 12.0f;
        cfg->launchSpeed         = 12.5f;
        cfg->swirlSpeed          = 7.2f;
        cfg->lifetime            = 5.8f;
        cfg->dustIntensity       = 0.35f;
        break;
    case VFX_GRANULAR_PRESET_EARTH_SHATTER_WAVE:
        cfg->materialId          = GRANULAR_MAT_COARSE_GRAVEL;
        cfg->secondaryMaterialId = GRANULAR_MAT_LOESS_SOIL;
        cfg->secondaryRatio      = 0.45f;
        cfg->grainCount          = 1536;
        cfg->grainRadius         = 0.044f;
        cfg->spreadRadius        = 1.35f;
        cfg->bedRadius           = 3.2f;
        cfg->chargeDuration      = 0.40f;
        cfg->waveSpeed           = 10.5f;
        cfg->launchSpeed         = 9.2f;
        cfg->swirlSpeed          = 3.5f;
        cfg->lifetime            = 5.5f;
        cfg->dustIntensity       = 1.10f;
        break;
    case VFX_GRANULAR_PRESET_DESERT_SAND_TEMPEST:
        cfg->materialId          = GRANULAR_MAT_DESERT_SAND;
        cfg->secondaryMaterialId = GRANULAR_MAT_LOESS_SOIL;
        cfg->secondaryRatio      = 0.18f;
        cfg->tertiaryMaterialId  = GRANULAR_MAT_COARSE_GRAVEL;
        cfg->tertiaryRatio       = 0.12f;
        cfg->grainCount          = 2800;
        cfg->grainRadius         = 0.024f;
        cfg->spreadRadius        = 1.35f;
        cfg->bedRadius           = 3.0f;
        cfg->launchSpeed         = 3.8f;
        cfg->swirlSpeed          = 9.5f;
        cfg->lifetime            = 6.5f;
        cfg->dustIntensity       = 0.85f;
        break;
    case VFX_GRANULAR_PRESET_AUTUMN_GALE_WHIRLWIND:
        cfg->materialId          = GRANULAR_MAT_DRY_LEAF;
        cfg->secondaryMaterialId = GRANULAR_MAT_DRY_TWIG;
        cfg->secondaryRatio      = 0.22f;
        cfg->tertiaryMaterialId  = GRANULAR_MAT_FRESH_LEAF;
        cfg->tertiaryRatio       = 0.14f;
        cfg->grainCount          = 2048;
        cfg->grainRadius         = 0.036f;
        cfg->spreadRadius        = 1.45f;
        cfg->bedRadius           = 3.2f;
        cfg->launchSpeed         = 3.6f;
        cfg->swirlSpeed          = 8.8f;
        cfg->lifetime            = 6.8f;
        cfg->dustIntensity       = 0.35f;
        break;
    case VFX_GRANULAR_PRESET_CRYSTAL_METAL_STORM:
        cfg->materialId          = GRANULAR_MAT_METAL_SHARD;
        cfg->secondaryMaterialId = GRANULAR_MAT_CRYSTAL_SHARD;
        cfg->secondaryRatio      = 0.35f;
        cfg->tertiaryMaterialId  = GRANULAR_MAT_GOLD_IRON_SAND;
        cfg->tertiaryRatio       = 0.25f;
        cfg->grainCount          = 2048;
        cfg->grainRadius         = 0.032f;
        cfg->spreadRadius        = 1.35f;
        cfg->bedRadius           = 2.8f;
        cfg->launchSpeed         = 4.0f;
        cfg->swirlSpeed          = 10.2f;
        cfg->lifetime            = 6.2f;
        cfg->dustIntensity       = 0.40f;
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
        "Hourglass Pillar",
        "Wuxia Leaf Palm Strike",
        "Blossom Qi Tempest",
        "Frost Shard Blizzard",
        "Earth Shatter Wave",
        "Desert Sand Tempest",
        "Autumn Gale Whirlwind",
        "Crystal Metal Storm"
    };
    return (preset >= 0 && preset < VFX_GRANULAR_PRESET_COUNT) ? names[preset] : "Unknown";
}

int VFX_GranularSand_GetParams(VFX_GranularSandConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;
    static const char *const presetNames[] = {
        "Sand Geyser", "Dune Avalanche", "Quicksand Vortex",
        "Sand Blast Jet", "Boulder Crush Talus", "Hourglass Pillar",
        "Wuxia Leaf Palm Strike", "Blossom Qi Tempest",
        "Frost Shard Blizzard", "Earth Shatter Wave",
        "Desert Sand Tempest", "Autumn Gale Whirlwind", "Crystal Metal Storm"
    };
    static const char *const matNames[] = {
        "Desert Sand", "Coarse Gravel", "Loess Soil", "Gold Iron Sand", "Volcanic Ash",
        "Dry Leaf", "Fresh Leaf", "Dry Twig", "Flower Petal", "Ice Shard", "Snow Powder",
        "Crystal Shard", "Metal Shard"
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
        .name = "Secondary Mat", .group = "Material", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->secondaryMaterialId, .minInt = 0, .maxInt = GRANULAR_MAT_COUNT - 1,
        .stepFloat = 1.0f, .enumNames = matNames, .enumCount = GRANULAR_MAT_COUNT
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Secondary Ratio", .group = "Material", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->secondaryRatio, .minFloat = 0.0f, .maxFloat = 0.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Tertiary Mat", .group = "Material", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->tertiaryMaterialId, .minInt = 0, .maxInt = GRANULAR_MAT_COUNT - 1,
        .stepFloat = 1.0f, .enumNames = matNames, .enumCount = GRANULAR_MAT_COUNT
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Tertiary Ratio", .group = "Material", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->tertiaryRatio, .minFloat = 0.0f, .maxFloat = 0.35f, .stepFloat = 0.05f
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

    bool isMultiPhaseWuxia = (cfg->preset >= VFX_GRANULAR_PRESET_WUXIA_LEAF_PALM_STRIKE &&
                              cfg->preset <= VFX_GRANULAR_PRESET_EARTH_SHATTER_WAVE);
    bool isCycloneTempest = (cfg->preset >= VFX_GRANULAR_PRESET_DESERT_SAND_TEMPEST &&
                             cfg->preset <= VFX_GRANULAR_PRESET_CRYSTAL_METAL_STORM);

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
        uint32_t grainMat = (uint32_t)cfg->materialId;
        uint32_t grainFlags = 0u;

        if (isMultiPhaseWuxia) {
            // Scatter a natural multi-material bed from around caster along the strike corridor
            float alongT = u2;
            float corridorLen = fmaxf(distXZ, 5.5f);
            float bedR = cfg->bedRadius > 0.5f ? cfg->bedRadius : 3.2f;
            float lat = (u1 - 0.5f) * 2.0f * bedR * (0.65f + 0.55f * alongT);
            float forwardDist = -1.2f + alongT * (corridorLen + 1.8f);
            pos.x = groundPoint.x + fwd.x * forwardDist + right.x * lat;
            pos.z = groundPoint.z + fwd.z * forwardDist + right.z * lat;
            pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z) + cfg->grainRadius * (1.0f + 0.3f * u3);
            grainFlags = 1u; // Resting ground bed ready for Shields entrainment

            if (cfg->secondaryRatio > 0.01f && u3 < cfg->secondaryRatio) {
                grainMat = (uint32_t)cfg->secondaryMaterialId;
            } else if (cfg->tertiaryRatio > 0.01f && u3 < cfg->secondaryRatio + cfg->tertiaryRatio) {
                grainMat = (uint32_t)cfg->tertiaryMaterialId;
            } else if (cfg->preset == VFX_GRANULAR_PRESET_WUXIA_LEAF_PALM_STRIKE && u3 > 0.86f) {
                grainMat = GRANULAR_MAT_FRESH_LEAF;
            } else if (cfg->preset == VFX_GRANULAR_PRESET_EARTH_SHATTER_WAVE && u3 > 0.85f) {
                grainMat = GRANULAR_MAT_DRY_TWIG;
            }
        } else if (isCycloneTempest) {
            float baseR = 0.28f;
            float topR = cfg->spreadRadius > 0.5f ? cfg->spreadRadius : 1.40f;
            float height = 3.55f;
            float hn = u1 * u1;
            float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
            float r = coneR * (0.20f + 0.82f * u2);
            pos.x += cosf(angle) * r;
            pos.z += sinf(angle) * r;
            pos.y += 0.05f + hn * height;
            vel.x = -sinf(angle) * cfg->swirlSpeed * 0.72f;
            vel.z =  cosf(angle) * cfg->swirlSpeed * 0.72f;
            vel.y = cfg->launchSpeed * (0.65f + 0.55f * u3);

            if (cfg->secondaryRatio > 0.01f && u3 < cfg->secondaryRatio) {
                grainMat = (uint32_t)cfg->secondaryMaterialId;
            } else if (cfg->tertiaryRatio > 0.01f && u3 < cfg->secondaryRatio + cfg->tertiaryRatio) {
                grainMat = (uint32_t)cfg->tertiaryMaterialId;
            } else if (cfg->preset == VFX_GRANULAR_PRESET_DESERT_SAND_TEMPEST && u3 > 0.92f) {
                grainMat = GRANULAR_MAT_DRY_TWIG;
            }
        } else {
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
        }

        s_spawnBuf[i] = (GranularGrainState){
            .position   = pos,
            .velocity   = vel,
            .radius     = cfg->grainRadius * (0.78f + 0.44f * u3),
            .lifetime   = cfg->lifetime * (0.85f + 0.30f * u2),
            .seed       = u0,
            .contactAO  = isMultiPhaseWuxia ? 0.25f : 0.0f,
            .materialId = grainMat,
            .flags      = grainFlags
        };
    }

    GranularSim_ClearWaves();
    int spawned = GranularSim_SpawnCluster(s_spawnBuf, count, groundPoint, groundNormal);
    s_granularSandInst.active = (spawned > 0);
    s_granularSandInst.config = *cfg;
    s_granularSandInst.config.source = groundPoint;
    s_granularSandInst.config.target = target;
    s_granularSandInst.timer = 0.0f;
    s_granularSandInst.dustAcc = 0.0f;
    s_granularSandInst.dustOrigin = groundPoint;
    s_granularSandInst.burstFired = false;

    if (isMultiPhaseWuxia) {
        // Phase 1 (Anticipation / Tu Luc): inward spiral gather + tremor lift around caster
        float chargeDur = cfg->chargeDuration > 0.1f ? cfg->chargeDuration : 0.65f;
        GranularWaveDesc chargeWave = {
            .kind         = GRANULAR_WAVE_SPIRAL_CONVERGE,
            .origin       = Vector3Add(groundPoint, (Vector3){0.0f, 0.25f, 0.0f}),
            .direction    = {0.0f, 1.0f, 0.0f},
            .radius       = cfg->bedRadius > 0.5f ? cfg->bedRadius : 3.2f,
            .swirlSpeed   = cfg->swirlSpeed,
            .inwardPull   = 5.2f,
            .liftAccel    = 4.8f,
            .analyticLock = 0.78f,
            .duration     = chargeDur + 0.15f,
            .attackTime   = 0.12f,
            .fadeTime     = 0.15f
        };
        GranularSim_SpawnWave(&chargeWave);
    } else if (isCycloneTempest) {
        GranularWaveDesc funnel = {
            .kind       = GRANULAR_WAVE_CONICAL_FUNNEL,
            .origin     = groundPoint,
            .direction  = {0.0f, 1.0f, 0.0f},
            .radius     = 0.28f,
            .topRadius  = cfg->spreadRadius > 0.5f ? cfg->spreadRadius : 1.40f,
            .length     = 3.55f,
            .axialSpeed = cfg->launchSpeed,
            .swirlSpeed = cfg->swirlSpeed,
            .duration   = fmaxf(cfg->lifetime - 1.2f, 2.5f),
            .attackTime = 0.15f,
            .fadeTime   = 0.65f,
            .loopTop    = true
        };
        GranularSim_SpawnWave(&funnel);
    } else if (cfg->preset == VFX_GRANULAR_PRESET_QUICKSAND_VORTEX) {
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

    if (cfg->dustIntensity > 0.05f && !isMultiPhaseWuxia) {
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
    Vector3 target = Vector3Add(pos, (Vector3){7.5f, 0.0f, 2.2f});
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
        bool isMultiPhaseWuxia = (cfg->preset >= VFX_GRANULAR_PRESET_WUXIA_LEAF_PALM_STRIKE &&
                                  cfg->preset <= VFX_GRANULAR_PRESET_EARTH_SHATTER_WAVE);

        if (isMultiPhaseWuxia && !s_granularSandInst.burstFired &&
            s_granularSandInst.timer >= cfg->chargeDuration) {
            s_granularSandInst.burstFired = true;

            Vector3 dir = Vector3Subtract(cfg->target, cfg->source);
            dir.y = 0.0f;
            float distXZ = Vector3Length(dir);
            Vector3 fwd = distXZ > 0.05f ? Vector3Scale(dir, 1.0f / distXZ) : (Vector3){1.0f, 0.0f, 0.0f};
            float jetLen = fmaxf(distXZ + 2.5f, 8.5f);

            // Phase 2 (Burst / Chuong Luc): travelling shockfront + helical vortex jet
            GranularWaveDesc jetWave = {
                .kind         = GRANULAR_WAVE_TRAVELLING_JET,
                .origin       = Vector3Add(cfg->source, (Vector3){0.0f, 0.55f, 0.0f}),
                .direction    = fwd,
                .radius       = cfg->spreadRadius > 0.3f ? cfg->spreadRadius : 1.25f,
                .topRadius    = (cfg->spreadRadius > 0.3f ? cfg->spreadRadius : 1.25f) * 2.1f,
                .length       = jetLen,
                .axialSpeed   = cfg->launchSpeed,
                .waveSpeed    = cfg->waveSpeed > 1.0f ? cfg->waveSpeed : 9.5f,
                .swirlSpeed   = cfg->swirlSpeed,
                .liftAccel    = 9.5f,
                .analyticLock = 0.85f,
                .duration     = 1.65f,
                .attackTime   = 0.06f,
                .fadeTime     = 0.45f
            };
            GranularSim_SpawnWave(&jetWave);

            // Coupled spatial MotionFields MovingGuide along the strike path
            Vector3 pts[5];
            for (int k = 0; k < 5; k++) {
                float t = (float)k / 4.0f;
                pts[k] = Vector3Lerp(cfg->source, cfg->target, t);
                pts[k].y = MapManager_GetGroundHeightAt(pts[k].x, pts[k].z) + 0.65f + 0.55f * sinf(t * PI);
            }
            MotionPath mp;
            if (MotionPath_Build(&mp, pts, 5)) {
                MotionFields_SpawnMovingGuide(&mp, fmaxf(cfg->launchSpeed, 6.0f),
                                              2.0f, 2.8f, cfg->swirlSpeed, 2.2f, 1.6f);
            }

            if (cfg->dustIntensity > 0.05f) {
                VFX_ComposeImpactDustVariant(cfg->source, VC_MAT_EARTH,
                                             fmaxf(1.2f, cfg->spreadRadius * 1.3f),
                                             Clamp(cfg->dustIntensity, 0.1f, 1.0f),
                                             VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            }
        }

        if (cfg->preset == VFX_GRANULAR_PRESET_QUICKSAND_VORTEX && s_granularSandInst.timer < 1.8f) {
            GranularSim_ApplyForceImpulse(cfg->source, cfg->spreadRadius * 1.6f,
                                          (Vector3){0.0f, 2.5f, 0.0f}, cfg->swirlSpeed);
        } else if (cfg->preset == VFX_GRANULAR_PRESET_HOURGLASS_PILLAR && s_granularSandInst.timer < 0.75f) {
            GranularSim_ApplyForceImpulse(Vector3Add(cfg->source, (Vector3){0.0f, 1.2f, 0.0f}),
                                          cfg->spreadRadius * 2.2f,
                                          (Vector3){0.0f, 9.5f, 0.0f}, cfg->swirlSpeed);
        }

        // Coupled fine-dust entrainment during active saltation & avalanche window
        if (!isMultiPhaseWuxia && cfg->dustIntensity > 0.05f && s_granularSandInst.timer > 0.15f && s_granularSandInst.timer < 1.85f) {
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

    if (GranularSim_GetActiveCount() <= 0) {
        s_granularSandInst.active = false;
    }
}

static void VC_GranularSand_Draw3D(Camera3D cam)
{
    (void)cam;
}

#endif // VC_GRANULAR_SAND_INL

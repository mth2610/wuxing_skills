// ── GENERALIZED GUIDED EARTH (HỆ THỔ) ─────────────────────────────────────────
//
// Generalized Physical Earth VFX System:
// Phase 1 (Emergence): Ground eruption with fissures and radial vegetation blast,
//                      orbital converge, or levitation.
// Phase 2 (Flight):    Ballistic parabolic arc (with authored apex, homing, or spiral swirl),
//                      tumbling low-poly faceted rock geometry with authored CPU lighting,
//                      and aerodynamic wind wake / motion field parting the grass.
// Phase 3 (Impact):    Violent physical shatter into bouncing debris chips (vc_debris_shards.inl),
//                      particulate dust puffs (vc_impact_dust.inl), radial fissure streaks,
//                      conformal ground wave (vc_ground_wave.inl), and powerful radial
//                      wind shockwave flattening surrounding vegetation.
//
#ifndef VC_GUIDED_EARTH_INL
#define VC_GUIDED_EARTH_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/color_gradient.h"
#include "core/vfx_render.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VC_GUIDED_EARTH_MAX_INSTANCES 16
#define VC_GUIDED_EARTH_MAX_ROCKS     24

static const Vector3 k_earthKeyLight = {-0.38f, 0.85f, 0.36f};

typedef struct {
    Vector3 pos;
    Vector3 prevPos;
    Vector3 localOffset;
    Vector3 spinAxis;
    float spinAngle;
    float spinSpeed;
    float radius;
    float massKg;
    float wakeTimer;
    unsigned int seed;
    bool hasImpacted;
    RockMeshData mesh;
    bool meshBuilt;
} VC_GuidedRock;

typedef struct {
    bool active;
    int id;
    VFX_GuidedEarthConfig config;
    float timer;
    float flightDuration;
    int phase; // 0 = EMERGE, 1 = FLIGHT, 2 = IMPACT, 3 = FINISHED
    int rockCount;
    VC_GuidedRock rocks[VC_GUIDED_EARTH_MAX_ROCKS];
    float emergeProgress;
    float flightProgress;
    Vector3 curTarget;
    bool emergeWindTriggered;
    bool craterSpawned;
} VC_GuidedEarthInstance;

static VC_GuidedEarthInstance s_guidedEarthInstances[VC_GUIDED_EARTH_MAX_INSTANCES];
static VFX_GuidedEarthConfig s_liveGuidedEarthConfig;
static bool s_guidedEarthInit = false;
static int s_guidedEarthNextId = 1;

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_GuidedEarthConfig VFX_GuidedEarth_DefaultConfig(void)
{
    VFX_GuidedEarthConfig cfg = {0};
    cfg.preset = VFX_EARTH_PRESET_BOULDER_LOB;
    cfg.style = VFX_EARTH_STYLE_BOULDER;
    cfg.spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
    cfg.trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
    cfg.impactMode = VFX_EARTH_IMPACT_SHATTER;

    cfg.source = (Vector3){0.0f, 0.0f, 0.0f};
    cfg.target = (Vector3){6.0f, 0.0f, 4.0f};

    // Geometry & Counts
    cfg.rockCount = 1;
    cfg.rockRadius = 0.95f;
    cfg.rockSquashY = 0.82f;
    cfg.rockSquashZ = 0.88f;
    cfg.jaggedness = 0.35f;
    cfg.clusterSpread = 0.0f;

    // Emergence
    cfg.emergeDuration = 0.65f;
    cfg.emergeHeight = 1.2f;
    cfg.emergeGroundDust = false;
    cfg.emergeCracks = true;

    // Flight & Parabolic Motion
    cfg.travelSpeed = 16.0f;
    cfg.arcHeight = 4.2f;
    cfg.gravityScale = 1.0f;
    cfg.swirlSpeed = 0.0f;
    cfg.tumbleSpeed = 120.0f;
    cfg.emitDustTrail = false;
    cfg.dustRate = 0.0f;
    cfg.emitTrailRibbon = false;
    cfg.ribbonWidth = 0.35f;

    // Impact
    cfg.shatterChips = 28;
    cfg.shatterVelocity = 6.5f;
    cfg.impactDustRadius = 2.4f;
    cfg.impactShake = 0.0f; // Disabled per user instruction
    cfg.impactCraterCrack = true;
    cfg.impactShockRing = true;

    // Material Optics
    cfg.matId = VC_MAT_EARTH;
    cfg.rockBaseColor = (Color){145, 118, 90, 255};
    cfg.veinColor = (Color){255, 140, 40, 255};
    cfg.veinIntensity = 0.0f;
    cfg.ambientGain = 0.28f;
    cfg.specularGain = 0.85f;

    return cfg;
}

VFX_GuidedEarthConfig *VFX_GuidedEarth_GetLiveConfig(void)
{
    if (!s_guidedEarthInit) {
        s_liveGuidedEarthConfig = VFX_GuidedEarth_DefaultConfig();
        s_guidedEarthInit = true;
    }
    return &s_liveGuidedEarthConfig;
}

void VFX_GuidedEarth_ApplyPreset(VFX_GuidedEarthConfig *cfg, VFX_GuidedEarthPreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;

    switch (preset)
    {
    case VFX_EARTH_PRESET_BOULDER_LOB:
        cfg->style = VFX_EARTH_STYLE_BOULDER;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 1;
        cfg->rockRadius = 0.95f;
        cfg->rockSquashY = 0.82f;
        cfg->rockSquashZ = 0.88f;
        cfg->jaggedness = 0.35f;
        cfg->clusterSpread = 0.0f;
        cfg->emergeDuration = 0.65f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = true;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 4.2f;
        cfg->swirlSpeed = 0.0f;
        cfg->tumbleSpeed = 120.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 30;
        cfg->shatterVelocity = 6.5f;
        cfg->impactDustRadius = 2.4f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){145, 118, 90, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_ROCK_BARRAGE:
        cfg->style = VFX_EARTH_STYLE_JAGGED_SLATE;
        cfg->spawnMode = VFX_EARTH_SPAWN_INSTANT_LAUNCH;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 6;
        cfg->rockRadius = 0.32f;
        cfg->rockSquashY = 0.45f;
        cfg->rockSquashZ = 0.70f;
        cfg->jaggedness = 0.45f;
        cfg->clusterSpread = 0.75f;
        cfg->emergeDuration = 0.0f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->travelSpeed = 26.0f;
        cfg->arcHeight = 1.8f;
        cfg->swirlSpeed = 2.5f;
        cfg->tumbleSpeed = 280.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 16;
        cfg->shatterVelocity = 4.5f;
        cfg->impactDustRadius = 1.4f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = false;
        cfg->rockBaseColor = (Color){125, 105, 85, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_METEOR_CRUSH:
        cfg->style = VFX_EARTH_STYLE_MAGMA_ROCK;
        cfg->spawnMode = VFX_EARTH_SPAWN_LEVITATE_RISE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_EARTH_IMPACT_CRATER_CRACK;
        cfg->rockCount = 4;
        cfg->rockRadius = 0.78f;
        cfg->rockSquashY = 0.90f;
        cfg->rockSquashZ = 0.95f;
        cfg->jaggedness = 0.50f;
        cfg->clusterSpread = 0.60f;
        cfg->emergeDuration = 0.40f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->travelSpeed = 22.0f;
        cfg->arcHeight = 7.5f;
        cfg->swirlSpeed = 1.0f;
        cfg->tumbleSpeed = 160.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 36;
        cfg->shatterVelocity = 8.0f;
        cfg->impactDustRadius = 3.2f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){50, 44, 42, 255};
        cfg->veinColor = (Color){255, 110, 25, 255};
        cfg->veinIntensity = 1.6f;
        break;

    case VFX_EARTH_PRESET_MUD_SLUG:
        cfg->style = VFX_EARTH_STYLE_MUD_CHUNK;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_CRATER_CRACK;
        cfg->rockCount = 3;
        cfg->rockRadius = 0.55f;
        cfg->rockSquashY = 0.75f;
        cfg->rockSquashZ = 0.85f;
        cfg->jaggedness = 0.12f;
        cfg->clusterSpread = 0.45f;
        cfg->emergeDuration = 0.50f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->travelSpeed = 14.0f;
        cfg->arcHeight = 2.5f;
        cfg->swirlSpeed = 0.0f;
        cfg->tumbleSpeed = 80.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 12;
        cfg->shatterVelocity = 3.8f;
        cfg->impactDustRadius = 1.8f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = false;
        cfg->rockBaseColor = (Color){98, 76, 54, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_JADE_SHARDS:
        cfg->style = VFX_EARTH_STYLE_JADE_CRYSTAL;
        cfg->spawnMode = VFX_EARTH_SPAWN_ORBITAL_CONVERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_SPIRAL_BARREL;
        cfg->impactMode = VFX_EARTH_IMPACT_PIERCE_EMBED;
        cfg->rockCount = 8;
        cfg->rockRadius = 0.22f;
        cfg->rockSquashY = 0.30f;
        cfg->rockSquashZ = 0.30f;
        cfg->jaggedness = 0.20f;
        cfg->clusterSpread = 0.85f;
        cfg->emergeDuration = 0.55f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->travelSpeed = 30.0f;
        cfg->arcHeight = 1.5f;
        cfg->swirlSpeed = 12.0f;
        cfg->tumbleSpeed = 360.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 20;
        cfg->shatterVelocity = 5.0f;
        cfg->impactDustRadius = 1.2f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = false;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){85, 205, 140, 255};
        cfg->veinColor = (Color){180, 255, 215, 255};
        cfg->veinIntensity = 1.2f;
        break;

    case VFX_EARTH_PRESET_SAND_SWARM:
        cfg->style = VFX_EARTH_STYLE_GRAVEL_SWARM;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 16;
        cfg->rockRadius = 0.09f;
        cfg->rockSquashY = 0.85f;
        cfg->rockSquashZ = 0.85f;
        cfg->jaggedness = 0.40f;
        cfg->clusterSpread = 1.35f;
        cfg->emergeDuration = 0.25f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->travelSpeed = 20.0f;
        cfg->arcHeight = 1.4f;
        cfg->swirlSpeed = 3.5f;
        cfg->tumbleSpeed = 400.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 8;
        cfg->shatterVelocity = 3.2f;
        cfg->impactDustRadius = 2.0f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = false;
        cfg->impactShockRing = false;
        cfg->rockBaseColor = (Color){205, 175, 125, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_CUSTOM:
    default:
        break;
    }
}

// ── Naming Helpers ───────────────────────────────────────────────────────────

const char *VFX_GuidedEarthPreset_Name(VFX_GuidedEarthPreset preset)
{
    static const char *const names[] = {
        "Custom", "Boulder Lob", "Rock Barrage", "Meteor Crush",
        "Mud Slug", "Jade Shards", "Sand Swarm"
    };
    return (preset >= 0 && preset < VFX_EARTH_PRESET_COUNT) ? names[preset] : "Unknown";
}

const char *VFX_EarthRockStyle_Name(VFX_EarthRockStyle style)
{
    static const char *const names[] = {
        "Boulder", "Jagged Slate", "Mud Chunk", "Gravel Swarm",
        "Jade Crystal", "Magma Rock"
    };
    return (style >= 0 && style < VFX_EARTH_STYLE_COUNT) ? names[style] : "Unknown";
}

const char *VFX_EarthSpawnMode_Name(VFX_EarthSpawnMode mode)
{
    static const char *const names[] = {
        "Ground Emerge", "Orbital Converge", "Levitate Rise", "Instant Launch"
    };
    return (mode >= 0 && mode < VFX_EARTH_SPAWN_COUNT) ? names[mode] : "Unknown";
}

const char *VFX_EarthTrajectory_Name(VFX_EarthTrajectoryMode traj)
{
    static const char *const names[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Barrel", "High Catapult"
    };
    return (traj >= 0 && traj < VFX_EARTH_TRAJ_COUNT) ? names[traj] : "Unknown";
}

const char *VFX_EarthImpact_Name(VFX_EarthImpactMode impact)
{
    static const char *const names[] = {
        "Shatter Debris", "Crater & Cracks", "Quake Shockwave", "Pierce Embed"
    };
    return (impact >= 0 && impact < VFX_EARTH_IMPACT_COUNT) ? names[impact] : "Unknown";
}

// ── Inspector Parameter Exposure ─────────────────────────────────────────────

int VFX_GuidedEarth_GetParams(VFX_GuidedEarthConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *const presetNames[] = {
        "Custom", "Boulder Lob", "Rock Barrage", "Meteor Crush",
        "Mud Slug", "Jade Shards", "Sand Swarm"
    };
    static const char *const styleNames[] = {
        "Boulder", "Jagged Slate", "Mud Chunk", "Gravel Swarm",
        "Jade Crystal", "Magma Rock"
    };
    static const char *const spawnNames[] = {
        "Ground Emerge", "Orbital Converge", "Levitate Rise", "Instant Launch"
    };
    static const char *const trajNames[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Barrel", "High Catapult"
    };
    static const char *const impactNames[] = {
        "Shatter Debris", "Crater & Cracks", "Quake Shockwave", "Pierce Embed"
    };

#define EARTH_PARAM_ENUM(pname, pgroup, val, enames, ecount) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = (ecount) - 1, \
        .stepFloat = 1.0f, .enumNames = enames, .enumCount = ecount \
    }; } while(0)

#define EARTH_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define EARTH_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define EARTH_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    // Preset & Architecture
    EARTH_PARAM_ENUM("Preset", "Preset", cfg->preset, presetNames, VFX_EARTH_PRESET_COUNT);
    EARTH_PARAM_ENUM("Rock Style", "Preset", cfg->style, styleNames, VFX_EARTH_STYLE_COUNT);
    EARTH_PARAM_ENUM("Spawn Mode", "Preset", cfg->spawnMode, spawnNames, VFX_EARTH_SPAWN_COUNT);
    EARTH_PARAM_ENUM("Trajectory", "Preset", cfg->trajectoryMode, trajNames, VFX_EARTH_TRAJ_COUNT);
    EARTH_PARAM_ENUM("Impact Mode", "Preset", cfg->impactMode, impactNames, VFX_EARTH_IMPACT_COUNT);

    // Physical Geometry & Formation
    EARTH_PARAM_INT("Rock count", "Geometry", cfg->rockCount, 1, VC_GUIDED_EARTH_MAX_ROCKS);
    EARTH_PARAM_FLOAT("Rock radius m", "Geometry", cfg->rockRadius, 0.05f, 2.5f, 0.05f);
    EARTH_PARAM_FLOAT("Squash Y", "Geometry", cfg->rockSquashY, 0.2f, 2.0f, 0.05f);
    EARTH_PARAM_FLOAT("Squash Z", "Geometry", cfg->rockSquashZ, 0.2f, 2.0f, 0.05f);
    EARTH_PARAM_FLOAT("Jaggedness", "Geometry", cfg->jaggedness, 0.0f, 1.0f, 0.05f);
    EARTH_PARAM_FLOAT("Cluster spread m", "Geometry", cfg->clusterSpread, 0.0f, 3.0f, 0.05f);

    // Phase 1: Emergence
    EARTH_PARAM_FLOAT("Emerge time s", "Emergence", cfg->emergeDuration, 0.0f, 3.0f, 0.05f);
    EARTH_PARAM_BOOL("Launch cracks", "Emergence", cfg->emergeCracks);

    // Phase 2: Flight & Parabolic Motion
    EARTH_PARAM_FLOAT("Travel speed m/s", "Flight", cfg->travelSpeed, 2.0f, 60.0f, 1.0f);
    EARTH_PARAM_FLOAT("Arc height m", "Flight", cfg->arcHeight, 0.2f, 20.0f, 0.2f);
    EARTH_PARAM_FLOAT("Gravity scale", "Flight", cfg->gravityScale, 0.0f, 3.0f, 0.1f);
    EARTH_PARAM_FLOAT("Swirl speed rad/s", "Flight", cfg->swirlSpeed, 0.0f, 25.0f, 0.5f);
    EARTH_PARAM_FLOAT("Tumble speed deg/s", "Flight", cfg->tumbleSpeed, 0.0f, 720.0f, 10.0f);

    // Phase 3: Impact & Aftermath
    EARTH_PARAM_INT("Shatter chips", "Impact", cfg->shatterChips, 0, 48);
    EARTH_PARAM_FLOAT("Shatter vel m/s", "Impact", cfg->shatterVelocity, 1.0f, 15.0f, 0.5f);
    EARTH_PARAM_FLOAT("Impact dust radius", "Impact", cfg->impactDustRadius, 0.5f, 6.0f, 0.1f);
    EARTH_PARAM_BOOL("Crater cracks", "Impact", cfg->impactCraterCrack);
    EARTH_PARAM_BOOL("Shockwave ring", "Impact", cfg->impactShockRing);

    // Material Optics & Shading
    EARTH_PARAM_FLOAT("Vein intensity", "Optics", cfg->veinIntensity, 0.0f, 4.0f, 0.1f);
    EARTH_PARAM_FLOAT("Ambient floor", "Optics", cfg->ambientGain, 0.10f, 0.60f, 0.02f);
    EARTH_PARAM_FLOAT("Specular gain", "Optics", cfg->specularGain, 0.0f, 3.0f, 0.05f);

#undef EARTH_PARAM_ENUM
#undef EARTH_PARAM_FLOAT
#undef EARTH_PARAM_INT
#undef EARTH_PARAM_BOOL

    return n;
}

// ── Instance Spawning & Execution ────────────────────────────────────────────

int VFX_ComposeGuidedEarthEx(Vector3 source, Vector3 target, const VFX_GuidedEarthConfig *cfg)
{
    EarthFx_InitShared();
    if (!cfg) cfg = VFX_GuidedEarth_GetLiveConfig();

    VC_GuidedEarthInstance *inst = NULL;
    for (int i = 0; i < VC_GUIDED_EARTH_MAX_INSTANCES; i++) {
        if (!s_guidedEarthInstances[i].active) {
            inst = &s_guidedEarthInstances[i];
            break;
        }
    }
    if (!inst) return 0; // Pool exhausted

    memset(inst, 0, sizeof(VC_GuidedEarthInstance));
    inst->active = true;
    inst->id = s_guidedEarthNextId++;
    inst->config = *cfg;
    inst->config.source = source;
    inst->config.target = target;
    inst->curTarget = target;
    inst->timer = 0.0f;
    inst->phase = (cfg->emergeDuration > 0.001f) ? 0 : 1; // 0=EMERGE, 1=FLIGHT
    inst->emergeWindTriggered = false;

    float distXZ = sqrtf((target.x - source.x) * (target.x - source.x) +
                         (target.z - source.z) * (target.z - source.z));
    float speed = fmaxf(cfg->travelSpeed, 1.0f);
    inst->flightDuration = fmaxf(distXZ / speed, 0.15f);

    int count = Clamp(cfg->rockCount, 1, VC_GUIDED_EARTH_MAX_ROCKS);
    inst->rockCount = count;

    for (int i = 0; i < count; i++) {
        VC_GuidedRock *r = &inst->rocks[i];
        r->seed = 10007u * (unsigned int)(inst->id + 1) + (unsigned int)(i * 977);
        r->wakeTimer = 0.0f;
        r->hasImpacted = false;

        float sizeVar = (i == 0) ? 1.0f : (0.45f + 0.35f * ((float)(i % 5) / 4.0f));
        r->radius = cfg->rockRadius * sizeVar;

        // Cluster offset
        if (i == 0) {
            r->localOffset = Vector3Zero();
        } else {
            float angle = ((float)i / (float)count) * 2.0f * PI;
            float rad = cfg->clusterSpread * (0.4f + 0.6f * ((float)(i % 3) / 2.0f));
            r->localOffset = (Vector3){cosf(angle) * rad, sinf(angle) * rad * 0.7f, ((float)i / (float)count) * 0.4f};
        }

        // Tumble rotation axis
        float ax = sinf((float)i * 1.7f);
        float ay = cosf((float)i * 2.3f);
        float az = sinf((float)i * 3.1f);
        r->spinAxis = Vector3Normalize((Vector3){ax, ay, az});
        if (Vector3LengthSqr(r->spinAxis) < 0.001f) r->spinAxis = (Vector3){0.0f, 1.0f, 0.0f};
        r->spinAngle = (float)GetRandomValue(0, 360);
        r->spinSpeed = cfg->tumbleSpeed * (0.75f + 0.5f * ((float)(i % 4) / 3.0f));

        // Procedural rock low-poly geometry
        int subdiv = (i == 0 && cfg->rockCount <= 4) ? 1 : 0;
        ProceduralMesh_BuildRock(&r->mesh, Vector3Zero(), r->radius, cfg->jaggedness, (int)r->seed, subdiv);
        r->meshBuilt = (r->mesh.faceCount > 0);

        r->pos = source;
        r->prevPos = source;
    }

    return inst->id;
}

int VFX_ComposeGuidedEarth(const VFX_GuidedEarthConfig *cfg)
{
    if (!cfg) cfg = VFX_GuidedEarth_GetLiveConfig();
    return VFX_ComposeGuidedEarthEx(cfg->source, cfg->target, cfg);
}

void VFX_KillGuidedEarth(int handle)
{
    for (int i = 0; i < VC_GUIDED_EARTH_MAX_INSTANCES; i++) {
        if (s_guidedEarthInstances[i].active && s_guidedEarthInstances[i].id == handle) {
            s_guidedEarthInstances[i].active = false;
            break;
        }
    }
}

// ── Physical Simulation & Phase Evolution ────────────────────────────────────

static void GuidedEarth_TriggerImpact(VC_GuidedEarthInstance *inst, VC_GuidedRock *rock)
{
    if (rock->hasImpacted) return;
    rock->hasImpacted = true;

    const VFX_GuidedEarthConfig *cfg = &inst->config;
    Vector3 impactPos = rock->pos;
    impactPos.y = VFX_GroundHeightFromMap(impactPos.x, impactPos.z, NULL);

    // 1. Motion Field / Wind shockwave on vegetation (flattens grass and flower stalks outward)
    Wind_SpawnRadialBlast(impactPos, cfg->rockRadius * 5.5f, 11.0f, 0.65f);
    Wind_SpawnTurbulence(impactPos, cfg->rockRadius * 4.0f, 6.5f, 0.30f, 3.5f, 0.50f);

    // 2. Physical tumbling debris chips under real 9.81 m/s^2 gravity
    if (cfg->shatterChips > 0) {
        Vector3 baseVel = (Vector3){0.0f, cfg->shatterVelocity * 0.65f, 0.0f};
        VFX_ComposeDebrisShards(impactPos, baseVel, cfg->matId, rock->radius * 0.38f, cfg->shatterChips);
    }

    // 3. Particulate expanding dust cloud (soft, non-emissive)
    if (cfg->impactDustRadius > 0.05f) {
        VFX_ComposeImpactDustVariant(impactPos, cfg->matId, cfg->impactDustRadius, 0.75f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    }

    // 4. Radial crater fissure streaks (first rock or major impact)
    if (cfg->impactCraterCrack && !inst->craterSpawned) {
        inst->craterSpawned = true;
        for (int k = 0; k < 3; k++) {
            float angle = ((float)k / 3.0f) * 2.0f * PI + ((float)rock->seed * 0.01f);
            float dist = cfg->rockRadius * (2.2f + 0.8f * ((float)k / 3.0f));
            Vector3 end = Vector3Add(impactPos, (Vector3){cosf(angle) * dist, 0.0f, sinf(angle) * dist});
            end.y = VFX_GroundHeightFromMap(end.x, end.z, NULL);
            VFX_ComposeFissureStreak(impactPos, end, cfg->rockRadius * 0.28f, 1.0f, inst->timer);
        }
    }

    // 5. Ground wave seismic shock ring
    if (cfg->impactShockRing) {
        VFX_ComposeGroundWave(impactPos, cfg->matId, cfg->rockRadius * 2.8f, 0.5f, VFX_GroundHeightFromMap, NULL);
    }
}

void VC_GuidedEarth_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        const VFX_GuidedEarthConfig *cfg = &inst->config;

        // Vector frame along trajectory
        Vector3 fwd = Vector3Subtract(inst->curTarget, cfg->source);
        float distXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
        Vector3 fwdNorm = (distXZ > 0.001f) ? Vector3Normalize(fwd) : (Vector3){0.0f, 0.0f, 1.0f};
        Vector3 worldUp = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 rightNorm = Vector3Normalize(Vector3CrossProduct(fwdNorm, worldUp));
        Vector3 upNorm = Vector3CrossProduct(rightNorm, fwdNorm);

        // ── Phase 0: EMERGENCE ───────────────────────────────────────────────
        if (inst->phase == 0) {
            float emergeDur = fmaxf(cfg->emergeDuration, 0.01f);
            inst->emergeProgress = Clamp(inst->timer / emergeDur, 0.0f, 1.0f);
            float ease = inst->emergeProgress * inst->emergeProgress * (3.0f - 2.0f * inst->emergeProgress);

            // Motion Field / Wind blast pushing surrounding vegetation away on eruption
            if (!inst->emergeWindTriggered) {
                inst->emergeWindTriggered = true;
                Wind_SpawnRadialBlast(cfg->source, cfg->rockRadius * 3.6f, 5.5f, 0.45f);
                Wind_SpawnTurbulence(cfg->source, cfg->rockRadius * 2.8f, 3.5f, 0.25f, 2.5f, 0.35f);
            }

            for (int i = 0; i < inst->rockCount; i++) {
                VC_GuidedRock *r = &inst->rocks[i];
                r->spinAngle += r->spinSpeed * 0.4f * dt;

                if (cfg->spawnMode == VFX_EARTH_SPAWN_GROUND_EMERGE) {
                    float groundY = VFX_GroundHeightFromMap(cfg->source.x, cfg->source.z, NULL);
                    float startY = groundY - r->radius * 1.3f;
                    float endY = groundY + cfg->emergeHeight + r->localOffset.y;
                    r->pos = (Vector3){
                        cfg->source.x + r->localOffset.x,
                        Math_Mix(startY, endY, ease),
                        cfg->source.z + r->localOffset.z
                    };
                } else if (cfg->spawnMode == VFX_EARTH_SPAWN_ORBITAL_CONVERGE) {
                    float expand = (1.0f - ease) * (cfg->clusterSpread * 3.5f + 1.2f);
                    float orbAngle = ease * PI * 1.5f + ((float)i / (float)inst->rockCount) * 2.0f * PI;
                    r->pos = (Vector3){
                        cfg->source.x + cosf(orbAngle) * expand,
                        cfg->source.y + 1.0f + sinf(orbAngle * 2.0f) * 0.4f,
                        cfg->source.z + sinf(orbAngle) * expand
                    };
                } else if (cfg->spawnMode == VFX_EARTH_SPAWN_LEVITATE_RISE) {
                    float bob = sinf(inst->timer * 8.0f + (float)i) * 0.06f;
                    r->pos = (Vector3){
                        cfg->source.x + r->localOffset.x,
                        cfg->source.y + 0.8f + ease * 0.5f + bob,
                        cfg->source.z + r->localOffset.z
                    };
                } else {
                    r->pos = cfg->source;
                }
                r->prevPos = r->pos;
            }

            // Launch ground cracks during emergence
            if (cfg->emergeCracks && cfg->spawnMode == VFX_EARTH_SPAWN_GROUND_EMERGE) {
                for (int k = 0; k < 2; k++) {
                    float angle = ((float)k / 2.0f) * PI + 0.45f;
                    float crackR = cfg->rockRadius * 1.8f * inst->emergeProgress;
                    Vector3 endP = Vector3Add(cfg->source, (Vector3){cosf(angle) * crackR, 0.0f, sinf(angle) * crackR});
                    endP.y = VFX_GroundHeightFromMap(endP.x, endP.z, NULL);
                    VFX_ComposeFissureStreak(cfg->source, endP, cfg->rockRadius * 0.22f, inst->emergeProgress, inst->timer);
                }
            }

            if (inst->emergeProgress >= 1.0f) {
                inst->phase = 1; // Transition to FLIGHT
            }
            continue;
        }

        // ── Phase 1: FLIGHT (PARABOLIC TRAJECTORY) ───────────────────────────
        if (inst->phase == 1) {
            float flightElapsed = inst->timer - cfg->emergeDuration;
            inst->flightProgress = Clamp(flightElapsed / fmaxf(inst->flightDuration, 0.01f), 0.0f, 1.0f);
            float tau = inst->flightProgress;

            // Height offset calculation
            float arcY = 0.0f;
            if (cfg->trajectoryMode == VFX_EARTH_TRAJ_HIGH_CATAPULT) {
                arcY = cfg->arcHeight * sinf(tau * PI) * (1.0f + 0.55f * (1.0f - tau));
            } else {
                // Parabolic Arc: 4 * H * tau * (1 - tau)
                arcY = 4.0f * cfg->arcHeight * tau * (1.0f - tau);
            }

            // Gravity influence
            if (cfg->gravityScale > 0.01f) {
                float gDrop = 0.5f * (9.81f * cfg->gravityScale) * (flightElapsed * flightElapsed * 0.15f);
                arcY = fmaxf(0.0f, arcY - gDrop * tau);
            }

            // Barrel Roll / Swirl
            float swirlAngle = (cfg->swirlSpeed > 0.01f) ? (inst->timer * cfg->swirlSpeed) : 0.0f;

            bool allImpacted = true;
            for (int i = 0; i < inst->rockCount; i++) {
                VC_GuidedRock *r = &inst->rocks[i];
                if (r->hasImpacted) continue;
                allImpacted = false;

                // Per-rock trailing progress for cluster formation
                float rockTau = Clamp(tau - r->localOffset.z * 0.18f, 0.0f, 1.0f);
                Vector3 basePos = Vector3Lerp(cfg->source, inst->curTarget, rockTau);
                basePos.y += arcY;

                // Lateral cluster spread + corkscrew swirl
                Vector3 spread = Vector3Zero();
                if (cfg->clusterSpread > 0.001f || cfg->swirlSpeed > 0.01f) {
                    float a = swirlAngle + ((float)i / (float)inst->rockCount) * 2.0f * PI;
                    float rollR = cfg->clusterSpread * (0.6f + 0.4f * sinf(rockTau * PI));
                    spread = Vector3Add(
                        Vector3Scale(rightNorm, cosf(a) * rollR + r->localOffset.x),
                        Vector3Scale(upNorm, sinf(a) * rollR + r->localOffset.y)
                    );
                }

                r->prevPos = r->pos;
                r->pos = Vector3Add(basePos, spread);

                // Tumbling rotation
                r->spinAngle += r->spinSpeed * dt;

                // Motion Field / Aerodynamic Wake on Vegetation as the boulder flies
                r->wakeTimer += dt;
                if (r->wakeTimer >= 0.045f) {
                    r->wakeTimer = 0.0f;
                    float wakeRadius = r->radius * 2.8f;
                    float wakeSpeed = fminf(cfg->travelSpeed * 0.40f, 8.0f);
                    Wind_SpawnGust(r->pos, fwdNorm, wakeRadius, wakeSpeed, 0.18f);

                    float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
                    if (r->pos.y - groundY < wakeRadius * 1.6f) {
                        Vector3 groundPt = (Vector3){r->pos.x, groundY, r->pos.z};
                        Wind_SpawnRadialBlast(groundPt, wakeRadius * 1.1f, wakeSpeed * 0.65f, 0.14f);
                    }
                }

                // Check Ground Collision or End of Trajectory
                float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
                if (rockTau >= 1.0f || (rockTau > 0.5f && r->pos.y <= groundY + r->radius * 0.3f)) {
                    GuidedEarth_TriggerImpact(inst, r);
                }
            }

            if (allImpacted || tau >= 1.0f) {
                inst->phase = 2; // Transition to IMPACT / CLEANUP
            }
            continue;
        }

        // ── Phase 2: IMPACT CLEANUP ──────────────────────────────────────────
        if (inst->phase == 2) {
            for (int i = 0; i < inst->rockCount; i++) {
                if (!inst->rocks[i].hasImpacted) {
                    GuidedEarth_TriggerImpact(inst, &inst->rocks[i]);
                }
            }
            inst->active = false; // Finished lifecycle
        }
    }
}

// ── Faceted Rock Rendering with Authored Lighting ────────────────────────────

static void GuidedEarth_DrawRockFaceted(const VC_GuidedRock *rock, const VFX_GuidedEarthConfig *cfg)
{
    if (!rock->meshBuilt || rock->mesh.faceCount <= 0) return;

    rlPushMatrix();
    rlTranslatef(rock->pos.x, rock->pos.y, rock->pos.z);
    rlRotatef(rock->spinAngle, rock->spinAxis.x, rock->spinAxis.y, rock->spinAxis.z);
    rlScalef(1.0f, cfg->rockSquashY, cfg->rockSquashZ);

    rlCheckRenderBatchLimit(rock->mesh.faceCount * 3);
    rlBegin(RL_TRIANGLES);

    Color base = cfg->rockBaseColor;
    float amb = cfg->ambientGain;
    float specGain = cfg->specularGain;

    for (int f = 0; f < rock->mesh.faceCount; f++) {
        Vector3 norm = rock->mesh.faceNormals[f];
        Vector3 worldNorm = Vector3RotateByAxisAngle(norm, rock->spinAxis, rock->spinAngle * DEG2RAD);
        float ndl = Vector3DotProduct(worldNorm, k_earthKeyLight);
        if (ndl < 0.0f) ndl = 0.0f;
        float shade = amb + (1.0f - amb) * ndl;
        float glint = powf(ndl, 22.0f) * specGain;

        float r = (float)base.r * shade + 255.0f * glint;
        float g = (float)base.g * shade + 255.0f * glint;
        float b = (float)base.b * shade + 255.0f * glint;
        Color faceCol = (Color){
            (unsigned char)Clamp(r, 0.0f, 255.0f),
            (unsigned char)Clamp(g, 0.0f, 255.0f),
            (unsigned char)Clamp(b, 0.0f, 255.0f),
            255
        };
        rlColor4ub(faceCol.r, faceCol.g, faceCol.b, 255);

        for (int k = 0; k < 3; k++) {
            Vector3 v = rock->mesh.verts[rock->mesh.faceVertIdx[f][k]];
            rlVertex3f(v.x, v.y, v.z);
        }
    }
    rlEnd();

    // Emissive vein accents (magma, jade, glowing qi lines)
    if (cfg->veinIntensity > 0.05f) {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
        rlCheckRenderBatchLimit(rock->mesh.faceCount * 2);
        rlBegin(RL_LINES);
        Color vCol = cfg->veinColor;
        unsigned char alpha = (unsigned char)Clamp(90.0f * cfg->veinIntensity, 0.0f, 255.0f);
        rlColor4ub(vCol.r, vCol.g, vCol.b, alpha);
        for (int f = 0; f < rock->mesh.faceCount; f += 2) {
            Vector3 v0 = rock->mesh.verts[rock->mesh.faceVertIdx[f][0]];
            Vector3 v1 = rock->mesh.verts[rock->mesh.faceVertIdx[f][1]];
            rlVertex3f(v0.x, v0.y, v0.z);
            rlVertex3f(v1.x, v1.y, v1.z);
        }
        rlEnd();
    }

    rlPopMatrix();
}

static Mesh s_earthShadowMesh = {0};
static Material s_earthShadowMat = {0};
static bool s_earthShadowInit = false;

static void GuidedEarth_InitShadowMesh(void)
{
    if (s_earthShadowInit) return;
    s_earthShadowMesh = ProceduralMesh_BuildRockTemplateMesh(1.0f, 0.28f, 1337, 1);
    s_earthShadowMat = LoadMaterialDefault();
    s_earthShadowInit = true;
}

void VC_GuidedEarth_DrawShadowPass(void)
{
    GuidedEarth_InitShadowMesh();
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0 || s_earthShadowMesh.vertexCount == 0) return;
    s_earthShadowMat.shader = depthShader;

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->hasImpacted) continue;

            float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
            if (r->pos.y < groundY - r->radius * 0.45f) continue;

            Matrix matScale = MatrixScale(r->radius, r->radius * inst->config.rockSquashY, r->radius * inst->config.rockSquashZ);
            Matrix matRot = MatrixRotate(r->spinAxis, r->spinAngle * DEG2RAD);
            Matrix matTrans = MatrixTranslate(r->pos.x, r->pos.y, r->pos.z);
            Matrix xf = MatrixMultiply(MatrixMultiply(matScale, matRot), matTrans);

            DrawMesh(s_earthShadowMesh, s_earthShadowMat, xf);
        }
    }
}

void VC_GuidedEarth_Draw3D(Camera3D cam)
{
    (void)cam;

    // 1. Draw Smart Directional Contact Shadows projected on terrain
    Environment_BeginShadowBatch();
    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->hasImpacted) continue;

            float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
            if (r->pos.y < groundY - r->radius * 0.4f) continue;

            float sWidth = r->radius * 1.15f;
            float sHeight = r->radius * (1.0f + inst->config.rockSquashY * 0.25f);
            Environment_DrawSmartShadow(r->pos, ENV_SHAPE_SPHERE, sWidth, sHeight);
        }
    }
    Environment_EndShadowBatch();

    // 2. Draw 3D Faceted Rock Geometry with Authored Lighting
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->hasImpacted) continue;
            GuidedEarth_DrawRockFaceted(r, &inst->config);
        }
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

#endif // VC_GUIDED_EARTH_INL

// ── GENERALIZED GUIDED LIQUID (HỆ THỦY: DẪN LỰC THỦY QUÁI) ───────────────────
//
// High-Capacity Physical Liquid Simulation (Thousands of Fluid Particles):
// Phase 1 (Emission): Configurable particle release modes (Burst, Stream Jet,
//                      Surround Ring, Orbital Converge, Ground Gush, Cone Spray)
//                      and parametric geometric shapes (Sphere, Ring, Cylinder,
//                      Helix, Disc) with tunable emission radius, inner radius,
//                      stagger delays and up to 2,048 fluid droplets.
// Phase 2 (Flight):    Integrated with ForceField and MotionField systems.
//                      Physical guided trajectories (Ballistic Arc, Homing Curve,
//                      Spiral Corkscrew, High Catapult, Straight Jet, Serpentine
//                      Wave) driven by attraction/pull forces, forward propulsion,
//                      vortex swirl, turbulent noise, viscous drag, and aerodynamic
//                      wind wake parting surrounding grass and foliage.
// Phase 3 (Impact):    Target arrival interaction (Worthington Splash Crown,
//                      Terrain-Conformal Expanding Puddle, Vortex Maelstrom,
//                      Explosive Hydraulic Burst, Deluge Cascade, Piercing Drill).
//                      True downward gravity cascade ($9.81 m/s^2$) when striking
//                      airborne targets, terrain surface bouncing restitution,
//                      conformal ground decals, and vegetation blast.
// Rendering:           Full dual-backend support: True 3D Analytical Metaball
//                      Raymarching and Screen Space Fluid (SSF), with high-fidelity
//                      Beer-Lambert absorption, chromatic dispersion refraction,
//                      dual-lobe specular highlights, and ultra-fast batched
//                      quad droplet rendering for thousands of water particles.
//
#ifndef VC_GUIDED_LIQUID_INL
#define VC_GUIDED_LIQUID_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/motion/motion_body.h"
#include "core/motion/motion_recipe.h"
#include "core/motion/motion_fields.h"
#include "core/path_spline.h"
#include "core/liquid/liquid_surface.h"
#include "core/liquid/liquid_metaball3d.h"
#include "core/liquid/liquid_motion.h"
#include "core/liquid/liquid_impact.h"
#include "core/particles/particle_system.h"
#include "core/decals/decal_system.h"
#include "core/wind/wind_system.h"
#include "core/map_manager.h"
#include "core/vfx_render.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#ifndef PHYSICS_GRAVITY_MPS2
#define PHYSICS_GRAVITY_MPS2 9.81f
#endif

#define VC_GUIDED_LIQUID_MAX_INSTANCES 8
#define VC_GUIDED_LIQUID_MAX_DROPLETS  2048
#define VC_GUIDED_LIQUID_MACRO_SURFACE_BLOBS 48

typedef enum {
    DROPLET_STATE_IDLE = 0,
    DROPLET_STATE_EMERGING,
    DROPLET_STATE_HOVERING,
    DROPLET_STATE_FLYING,
    DROPLET_STATE_CASCADING, // Water struck target; raining downward with gravity onto ground
    DROPLET_STATE_SETTLED,   // Hit terrain and merged into ground puddle
    DROPLET_STATE_DEAD
} VC_DropletState;

typedef struct {
    Vector3 pos;
    Vector3 prevPos;
    Vector3 launchPos;
    Vector3 targetPos;
    Vector3 localOffset;
    Vector3 velocity;
    float radius;
    float baseRadius;
    VC_DropletState state;
    float launchDelay;
    float flightProgress;
    float age;
    float flightDuration;
    unsigned int seed;
    float swirlPhase;
    bool wakeWindFired;
    int bounceCount;
} VC_LiquidDroplet;

typedef struct {
    bool active;
    int id;
    VFX_GuidedLiquidConfig config;
    float timer;
    float flightDuration;
    Vector3 curTarget;
    int dropletCount;
    VC_LiquidDroplet droplets[VC_GUIDED_LIQUID_MAX_DROPLETS];

    // Physics Fields
    MotionFieldHandle motionField;
    bool motionFieldCreated;

    // Environmental Interaction
    bool emergeWindTriggered;
    bool impactTriggered;
    Vector3 impactPos;
    Vector3 impactNormal;
    float impactTimer;

    // Ground-Conformal Puddle State
    Vector3 groundPuddlePos;
    float puddleRadius;
    float puddleMaxRadius;
    float puddleTimer;
    float puddleDuration;
    bool decalSpawned;

    // Material Optical Properties
    LiquidDesc material;
} VC_GuidedLiquidInstance;

static VC_GuidedLiquidInstance s_guidedLiquidInstances[VC_GUIDED_LIQUID_MAX_INSTANCES];
static VFX_GuidedLiquidConfig s_liveGuidedLiquidConfig;
static bool s_guidedLiquidInit = false;
static int s_guidedLiquidNextId = 1;

// ── Deterministic RNG Helpers ────────────────────────────────────────────────

static inline unsigned int GuidedLiquid_Hash(unsigned int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return x;
}

static inline float GuidedLiquid_RandomFloat(unsigned int *seed)
{
    *seed = GuidedLiquid_Hash(*seed + 19937u);
    return (float)(*seed % 10000u) / 10000.0f;
}

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_GuidedLiquidConfig VFX_GuidedLiquid_DefaultConfig(void)
{
    VFX_GuidedLiquidConfig cfg;
    memset(&cfg, 0, sizeof(VFX_GuidedLiquidConfig));

    cfg.preset = VFX_LIQUID_PRESET_WATER_ORB;
    cfg.spawnMode = VFX_LIQUID_SPAWN_BURST;
    cfg.emissionShape = VFX_LIQUID_SHAPE_SPHERE;
    cfg.trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
    cfg.impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
    cfg.renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
    cfg.liquidProfile = 0; // LIQUID_MOTION_WATER

    // Phase 1: High-Capacity Emission & Geometry (Thousands of particles)
    cfg.particleCount = 512;
    cfg.emissionRadius = 0.45f;
    cfg.emissionInnerRadius = 0.0f;
    cfg.coneAngleDeg = 20.0f;
    cfg.dropletRadius = 0.16f;
    cfg.blobVariation = 0.35f;
    cfg.emergeDuration = 0.30f;
    cfg.hoverDuration = 0.12f;
    cfg.launchStaggerDelay = 0.001f;

    // Phase 2: Guiding Forces & Flight
    cfg.source = (Vector3){6.0f, 1.2f, 4.4f};
    cfg.target = (Vector3){12.0f, 0.0f, 4.4f};
    cfg.travelSpeed = 16.0f;
    cfg.arcHeight = 2.8f;
    cfg.arcVariation = 0.18f;
    cfg.guideRadius = 1.2f;
    cfg.pullForceN = 5.5f;
    cfg.forwardForceN = 1.4f;
    cfg.swirlSpeed = 8.0f;
    cfg.turbulenceSpeed = 2.2f;
    cfg.fluidDrag = 1.1f;
    cfg.gravityScale = 1.0f;
    cfg.emitMistTrail = true;
    cfg.emitQiRibbon = false;
    cfg.ribbonWidth = 0.08f;

    // Phase 3: Impact & Aftermath
    cfg.splashDropletCount = 32;
    cfg.splashVelocity = 6.0f;
    cfg.puddleRadius = 1.8f;
    cfg.puddleDuration = 2.8f;
    cfg.impactCrown = true;
    cfg.impactShockRing = true;
    cfg.impactDecal = true;
    cfg.impactMistRadius = 1.6f;
    cfg.impactShake = 0.15f;
    cfg.windBlastRadius = 3.2f;

    // Optics & Appearance
    cfg.liquidBodyColor = (Color){22, 105, 175, 230};
    cfg.liquidGlowColor = (Color){160, 230, 255, 255};
    cfg.liquidFoamColor = (Color){240, 250, 255, 255};
    cfg.ior = 1.333f;
    cfg.specularPower = 64.0f;
    cfg.foamAmount = 0.20f;
    cfg.threshold = 0.52f;

    return cfg;
}

VFX_GuidedLiquidConfig *VFX_GuidedLiquid_GetLiveConfig(void)
{
    if (!s_guidedLiquidInit) {
        s_liveGuidedLiquidConfig = VFX_GuidedLiquid_DefaultConfig();
        const char *presetEnv = getenv("WUXING_LIQUID_PRESET");
        if (presetEnv) {
            int p = atoi(presetEnv);
            if (p >= 0 && p < VFX_LIQUID_PRESET_COUNT) {
                VFX_GuidedLiquid_ApplyPreset(&s_liveGuidedLiquidConfig, (VFX_GuidedLiquidPreset)p);
            }
        }
        const char *backendEnv = getenv("WUXING_LIQUID_BACKEND");
        if (backendEnv) {
            int b = atoi(backendEnv);
            if (b >= 0 && b < VFX_LIQUID_BACKEND_COUNT) {
                s_liveGuidedLiquidConfig.renderBackend = (VFX_LiquidRenderBackend)b;
            }
        }
        s_guidedLiquidInit = true;
    }
    return &s_liveGuidedLiquidConfig;
}

void VFX_GuidedLiquid_ApplyPreset(VFX_GuidedLiquidConfig *cfg, VFX_GuidedLiquidPreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;

    switch (preset)
    {
    case VFX_LIQUID_PRESET_WATER_ORB:
        cfg->spawnMode = VFX_LIQUID_SPAWN_BURST;
        cfg->emissionShape = VFX_LIQUID_SHAPE_SPHERE;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
        cfg->renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
        cfg->particleCount = 512;
        cfg->emissionRadius = 0.45f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 20.0f;
        cfg->dropletRadius = 0.16f;
        cfg->blobVariation = 0.35f;
        cfg->emergeDuration = 0.30f;
        cfg->hoverDuration = 0.12f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 2.8f;
        cfg->arcVariation = 0.18f;
        cfg->guideRadius = 1.2f;
        cfg->pullForceN = 5.5f;
        cfg->forwardForceN = 1.4f;
        cfg->swirlSpeed = 8.0f;
        cfg->turbulenceSpeed = 2.2f;
        cfg->fluidDrag = 1.1f;
        cfg->gravityScale = 1.0f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = false;
        cfg->splashDropletCount = 32;
        cfg->splashVelocity = 6.0f;
        cfg->puddleRadius = 1.8f;
        cfg->puddleDuration = 2.8f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 1.6f;
        cfg->impactShake = 0.15f;
        cfg->windBlastRadius = 3.2f;
        cfg->liquidBodyColor = (Color){22, 105, 175, 230};
        cfg->liquidGlowColor = (Color){160, 230, 255, 255};
        cfg->liquidFoamColor = (Color){240, 250, 255, 255};
        cfg->threshold = 0.52f;
        break;

    case VFX_LIQUID_PRESET_TORRENT_JET:
        cfg->spawnMode = VFX_LIQUID_SPAWN_STREAM;
        cfg->emissionShape = VFX_LIQUID_SHAPE_CYLINDER;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_JET_STREAM;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPLOSIVE_BURST;
        cfg->renderBackend = VFX_LIQUID_BACKEND_SSF;
        cfg->particleCount = 1024;
        cfg->emissionRadius = 0.25f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 15.0f;
        cfg->dropletRadius = 0.12f;
        cfg->blobVariation = 0.25f;
        cfg->emergeDuration = 0.15f;
        cfg->hoverDuration = 0.05f;
        cfg->launchStaggerDelay = 0.0015f;
        cfg->travelSpeed = 32.0f;
        cfg->arcHeight = 0.4f;
        cfg->arcVariation = 0.05f;
        cfg->guideRadius = 0.9f;
        cfg->pullForceN = 8.0f;
        cfg->forwardForceN = 4.0f;
        cfg->swirlSpeed = 16.0f;
        cfg->turbulenceSpeed = 3.5f;
        cfg->fluidDrag = 0.8f;
        cfg->gravityScale = 0.25f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = true;
        cfg->ribbonWidth = 0.06f;
        cfg->splashDropletCount = 48;
        cfg->splashVelocity = 8.5f;
        cfg->puddleRadius = 2.4f;
        cfg->puddleDuration = 3.2f;
        cfg->impactCrown = false;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 2.8f;
        cfg->impactShake = 0.30f;
        cfg->windBlastRadius = 4.0f;
        cfg->liquidBodyColor = (Color){15, 120, 210, 240};
        cfg->liquidGlowColor = (Color){180, 245, 255, 255};
        cfg->liquidFoamColor = (Color){255, 255, 255, 255};
        cfg->threshold = 0.48f;
        break;

    case VFX_LIQUID_PRESET_VORTEX_DRAGON:
        cfg->spawnMode = VFX_LIQUID_SPAWN_BURST;
        cfg->emissionShape = VFX_LIQUID_SHAPE_HELIX;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW;
        cfg->impactMode = VFX_LIQUID_IMPACT_VORTEX_MAELSTROM;
        cfg->renderBackend = VFX_LIQUID_BACKEND_HYBRID;
        cfg->particleCount = 1024;
        cfg->emissionRadius = 0.70f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 30.0f;
        cfg->dropletRadius = 0.14f;
        cfg->blobVariation = 0.40f;
        cfg->emergeDuration = 0.40f;
        cfg->hoverDuration = 0.18f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 22.0f;
        cfg->arcHeight = 3.5f;
        cfg->arcVariation = 0.25f;
        cfg->guideRadius = 1.6f;
        cfg->pullForceN = 6.5f;
        cfg->forwardForceN = 2.0f;
        cfg->swirlSpeed = 20.0f;
        cfg->turbulenceSpeed = 4.0f;
        cfg->fluidDrag = 1.0f;
        cfg->gravityScale = 0.8f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = true;
        cfg->ribbonWidth = 0.10f;
        cfg->splashDropletCount = 40;
        cfg->splashVelocity = 7.0f;
        cfg->puddleRadius = 3.0f;
        cfg->puddleDuration = 4.2f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 2.5f;
        cfg->impactShake = 0.25f;
        cfg->windBlastRadius = 4.5f;
        cfg->liquidBodyColor = (Color){10, 85, 160, 235};
        cfg->liquidGlowColor = (Color){140, 220, 255, 255};
        cfg->liquidFoamColor = (Color){230, 248, 255, 255};
        cfg->threshold = 0.50f;
        break;

    case VFX_LIQUID_PRESET_ORBITAL_DELUGE:
        cfg->spawnMode = VFX_LIQUID_SPAWN_ORBITAL_CONVERGE;
        cfg->emissionShape = VFX_LIQUID_SHAPE_RING;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_LIQUID_IMPACT_DELUGE_CASCADE;
        cfg->renderBackend = VFX_LIQUID_BACKEND_SSF;
        cfg->particleCount = 2048;
        cfg->emissionRadius = 2.5f;
        cfg->emissionInnerRadius = 1.5f;
        cfg->coneAngleDeg = 45.0f;
        cfg->dropletRadius = 0.15f;
        cfg->blobVariation = 0.45f;
        cfg->emergeDuration = 0.60f;
        cfg->hoverDuration = 0.25f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 24.0f;
        cfg->arcHeight = 9.0f;
        cfg->arcVariation = 0.35f;
        cfg->guideRadius = 2.2f;
        cfg->pullForceN = 5.0f;
        cfg->forwardForceN = 1.8f;
        cfg->swirlSpeed = 12.0f;
        cfg->turbulenceSpeed = 3.0f;
        cfg->fluidDrag = 1.1f;
        cfg->gravityScale = 1.6f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = false;
        cfg->splashDropletCount = 64;
        cfg->splashVelocity = 8.0f;
        cfg->puddleRadius = 4.0f;
        cfg->puddleDuration = 5.0f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 3.5f;
        cfg->impactShake = 0.35f;
        cfg->windBlastRadius = 5.5f;
        cfg->liquidBodyColor = (Color){20, 95, 170, 220};
        cfg->liquidGlowColor = (Color){150, 215, 255, 255};
        cfg->liquidFoamColor = (Color){240, 250, 255, 255};
        cfg->threshold = 0.52f;
        break;

    case VFX_LIQUID_PRESET_SPRING_GUSH:
        cfg->spawnMode = VFX_LIQUID_SPAWN_GROUND_GUSH;
        cfg->emissionShape = VFX_LIQUID_SHAPE_DISC;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
        cfg->renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
        cfg->particleCount = 768;
        cfg->emissionRadius = 1.2f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 35.0f;
        cfg->dropletRadius = 0.18f;
        cfg->blobVariation = 0.35f;
        cfg->emergeDuration = 0.40f;
        cfg->hoverDuration = 0.12f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 18.0f;
        cfg->arcHeight = 4.2f;
        cfg->arcVariation = 0.20f;
        cfg->guideRadius = 1.4f;
        cfg->pullForceN = 5.5f;
        cfg->forwardForceN = 1.5f;
        cfg->swirlSpeed = 6.0f;
        cfg->turbulenceSpeed = 2.2f;
        cfg->fluidDrag = 1.2f;
        cfg->gravityScale = 1.0f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = false;
        cfg->splashDropletCount = 36;
        cfg->splashVelocity = 5.5f;
        cfg->puddleRadius = 2.4f;
        cfg->puddleDuration = 3.2f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 2.0f;
        cfg->impactShake = 0.20f;
        cfg->windBlastRadius = 3.5f;
        cfg->liquidBodyColor = (Color){30, 115, 185, 230};
        cfg->liquidGlowColor = (Color){175, 235, 255, 255};
        cfg->liquidFoamColor = (Color){250, 255, 255, 255};
        cfg->threshold = 0.50f;
        break;

    case VFX_LIQUID_PRESET_SURGING_TIDE:
        cfg->spawnMode = VFX_LIQUID_SPAWN_SURROUND_RING;
        cfg->emissionShape = VFX_LIQUID_SHAPE_DISC;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SERPENTINE_WAVE;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPANDING_PUDDLE;
        cfg->renderBackend = VFX_LIQUID_BACKEND_SSF;
        cfg->particleCount = 1024;
        cfg->emissionRadius = 2.0f;
        cfg->emissionInnerRadius = 0.8f;
        cfg->coneAngleDeg = 40.0f;
        cfg->dropletRadius = 0.18f;
        cfg->blobVariation = 0.40f;
        cfg->emergeDuration = 0.35f;
        cfg->hoverDuration = 0.10f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 0.8f;
        cfg->arcVariation = 0.10f;
        cfg->guideRadius = 1.8f;
        cfg->pullForceN = 6.5f;
        cfg->forwardForceN = 1.8f;
        cfg->swirlSpeed = 5.0f;
        cfg->turbulenceSpeed = 2.5f;
        cfg->fluidDrag = 1.3f;
        cfg->gravityScale = 0.8f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = false;
        cfg->splashDropletCount = 32;
        cfg->splashVelocity = 5.0f;
        cfg->puddleRadius = 4.8f;
        cfg->puddleDuration = 5.5f;
        cfg->impactCrown = false;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 2.2f;
        cfg->impactShake = 0.15f;
        cfg->windBlastRadius = 4.8f;
        cfg->liquidBodyColor = (Color){20, 100, 165, 225};
        cfg->liquidGlowColor = (Color){155, 225, 255, 255};
        cfg->liquidFoamColor = (Color){245, 252, 255, 255};
        cfg->threshold = 0.50f;
        break;

    case VFX_LIQUID_PRESET_CUSTOM:
    default:
        break;
    }
}

// ── Naming Helpers ───────────────────────────────────────────────────────────

const char *VFX_GuidedLiquidPreset_Name(VFX_GuidedLiquidPreset preset)
{
    static const char *const names[] = {
        "Custom", "Water Orb", "Torrent Jet", "Vortex Dragon",
        "Orbital Deluge", "Spring Gush", "Surging Tide"
    };
    return (preset >= 0 && preset < VFX_LIQUID_PRESET_COUNT) ? names[preset] : "Unknown";
}

const char *VFX_LiquidSpawnMode_Name(VFX_LiquidSpawnMode mode)
{
    static const char *const names[] = {
        "Burst Ejection", "Stream Jet", "Surround Ring", "Orbital Converge", "Ground Gush", "Cone Spray"
    };
    return (mode >= 0 && mode < VFX_LIQUID_SPAWN_COUNT) ? names[mode] : "Unknown";
}

const char *VFX_LiquidEmissionShape_Name(VFX_LiquidEmissionShape shape)
{
    static const char *const names[] = {
        "Volume Sphere", "Toroidal Ring", "Cylinder Column", "Double Helix", "Planar Disc"
    };
    return (shape >= 0 && shape < VFX_LIQUID_SHAPE_COUNT) ? names[shape] : "Unknown";
}

const char *VFX_LiquidTrajectory_Name(VFX_LiquidTrajectoryMode traj)
{
    static const char *const names[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Corkscrew", "High Catapult", "Jet Stream", "Serpentine Wave"
    };
    return (traj >= 0 && traj < VFX_LIQUID_TRAJ_COUNT) ? names[traj] : "Unknown";
}

const char *VFX_LiquidImpact_Name(VFX_LiquidImpactMode impact)
{
    static const char *const names[] = {
        "Splash Crown", "Expanding Puddle", "Vortex Maelstrom", "Explosive Burst", "Deluge Cascade", "Piercing Drill"
    };
    return (impact >= 0 && impact < VFX_LIQUID_IMPACT_COUNT) ? names[impact] : "Unknown";
}

const char *VFX_LiquidBackend_Name(VFX_LiquidRenderBackend backend)
{
    static const char *const names[] = {
        "Metaball 3D", "Screen Space Fluid", "Hybrid"
    };
    return (backend >= 0 && backend < VFX_LIQUID_BACKEND_COUNT) ? names[backend] : "Unknown";
}

// ── Inspector Parameter Exposure ─────────────────────────────────────────────

int VFX_GuidedLiquid_GetParams(VFX_GuidedLiquidConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *const presetNames[] = {
        "Custom", "Water Orb", "Torrent Jet", "Vortex Dragon",
        "Orbital Deluge", "Spring Gush", "Surging Tide"
    };
    static const char *const spawnNames[] = {
        "Burst Ejection", "Stream Jet", "Surround Ring", "Orbital Converge", "Ground Gush", "Cone Spray"
    };
    static const char *const shapeNames[] = {
        "Volume Sphere", "Toroidal Ring", "Cylinder Column", "Double Helix", "Planar Disc"
    };
    static const char *const trajNames[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Corkscrew", "High Catapult", "Jet Stream", "Serpentine Wave"
    };
    static const char *const impactNames[] = {
        "Splash Crown", "Expanding Puddle", "Vortex Maelstrom", "Explosive Burst", "Deluge Cascade", "Piercing Drill"
    };
    static const char *const backendNames[] = {
        "Metaball 3D", "Screen Space Fluid", "Hybrid"
    };
    static const char *const profileNames[] = {
        "Water", "Poison", "Mud", "Lava", "Metal"
    };

#define LIQUID_PARAM_ENUM(pname, pgroup, val, enames, ecount) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = (ecount) - 1, \
        .stepFloat = 1.0f, .enumNames = enames, .enumCount = ecount \
    }; } while(0)

#define LIQUID_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define LIQUID_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define LIQUID_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    // Preset & Architecture
    LIQUID_PARAM_ENUM("Preset", "Preset", cfg->preset, presetNames, VFX_LIQUID_PRESET_COUNT);
    LIQUID_PARAM_ENUM("Render Backend", "Preset", cfg->renderBackend, backendNames, VFX_LIQUID_BACKEND_COUNT);
    LIQUID_PARAM_ENUM("Liquid Profile", "Preset", cfg->liquidProfile, profileNames, 5);

    // Phase 1: Emission & Shape
    LIQUID_PARAM_ENUM("Spawn Mode", "Emission", cfg->spawnMode, spawnNames, VFX_LIQUID_SPAWN_COUNT);
    LIQUID_PARAM_ENUM("Emission Shape", "Emission", cfg->emissionShape, shapeNames, VFX_LIQUID_SHAPE_COUNT);
    LIQUID_PARAM_INT("Droplet Count", "Emission", cfg->particleCount, 32, VC_GUIDED_LIQUID_MAX_DROPLETS);
    LIQUID_PARAM_FLOAT("Emission Radius m", "Emission", cfg->emissionRadius, 0.05f, 5.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Inner Radius m", "Emission", cfg->emissionInnerRadius, 0.0f, 4.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Cone Angle deg", "Emission", cfg->coneAngleDeg, 5.0f, 90.0f, 5.0f);
    LIQUID_PARAM_FLOAT("Droplet Radius m", "Emission", cfg->dropletRadius, 0.04f, 0.60f, 0.01f);
    LIQUID_PARAM_FLOAT("Blob Variation", "Emission", cfg->blobVariation, 0.0f, 0.8f, 0.05f);
    LIQUID_PARAM_FLOAT("Emerge Duration s", "Emission", cfg->emergeDuration, 0.0f, 3.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Hover Duration s", "Emission", cfg->hoverDuration, 0.0f, 2.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Launch Stagger s", "Emission", cfg->launchStaggerDelay, 0.0f, 0.05f, 0.001f);

    // Phase 2: Guiding Forces & Flight
    LIQUID_PARAM_ENUM("Trajectory", "Trajectory", cfg->trajectoryMode, trajNames, VFX_LIQUID_TRAJ_COUNT);
    LIQUID_PARAM_FLOAT("Travel Speed m/s", "Trajectory", cfg->travelSpeed, 3.0f, 50.0f, 1.0f);
    LIQUID_PARAM_FLOAT("Arc Height m", "Trajectory", cfg->arcHeight, 0.0f, 15.0f, 0.2f);
    LIQUID_PARAM_FLOAT("Arc Variation", "Trajectory", cfg->arcVariation, 0.0f, 1.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Guide Radius m", "Forces", cfg->guideRadius, 0.3f, 4.0f, 0.1f);
    LIQUID_PARAM_FLOAT("Pull Force N", "Forces", cfg->pullForceN, 0.1f, 15.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Forward Force N", "Forces", cfg->forwardForceN, 0.05f, 8.0f, 0.2f);
    LIQUID_PARAM_FLOAT("Swirl Speed rad/s", "Forces", cfg->swirlSpeed, 0.0f, 25.0f, 1.0f);
    LIQUID_PARAM_FLOAT("Turbulence m/s", "Forces", cfg->turbulenceSpeed, 0.0f, 8.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Fluid Drag", "Forces", cfg->fluidDrag, 0.1f, 5.0f, 0.1f);
    LIQUID_PARAM_FLOAT("Gravity Scale", "Forces", cfg->gravityScale, 0.0f, 2.5f, 0.1f);
    LIQUID_PARAM_BOOL("Emit Mist Trail", "Trajectory", cfg->emitMistTrail);
    LIQUID_PARAM_BOOL("Emit Qi Ribbon", "Trajectory", cfg->emitQiRibbon);

    // Phase 3: Impact & Interaction
    LIQUID_PARAM_ENUM("Impact Mode", "Impact", cfg->impactMode, impactNames, VFX_LIQUID_IMPACT_COUNT);
    LIQUID_PARAM_INT("Splash Droplets", "Impact", cfg->splashDropletCount, 0, 128);
    LIQUID_PARAM_FLOAT("Splash Speed m/s", "Impact", cfg->splashVelocity, 1.0f, 15.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Puddle Radius m", "Impact", cfg->puddleRadius, 0.0f, 6.0f, 0.2f);
    LIQUID_PARAM_FLOAT("Puddle Duration s", "Impact", cfg->puddleDuration, 0.5f, 8.0f, 0.5f);
    LIQUID_PARAM_BOOL("Impact Crown", "Impact", cfg->impactCrown);
    LIQUID_PARAM_BOOL("Shock Ring", "Impact", cfg->impactShockRing);
    LIQUID_PARAM_BOOL("Spawn Decal", "Impact", cfg->impactDecal);
    LIQUID_PARAM_FLOAT("Mist Radius m", "Impact", cfg->impactMistRadius, 0.0f, 5.0f, 0.2f);
    LIQUID_PARAM_FLOAT("Screen Shake", "Impact", cfg->impactShake, 0.0f, 1.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Wind Blast m", "Impact", cfg->windBlastRadius, 0.0f, 8.0f, 0.5f);

    // Optics & Appearance
    LIQUID_PARAM_FLOAT("Refraction IOR", "Optics", cfg->ior, 1.0f, 2.0f, 0.02f);
    LIQUID_PARAM_FLOAT("Specular Power", "Optics", cfg->specularPower, 8.0f, 128.0f, 8.0f);
    LIQUID_PARAM_FLOAT("Foam Amount", "Optics", cfg->foamAmount, 0.0f, 1.0f, 0.05f);
    LIQUID_PARAM_FLOAT("Metaball Threshold", "Optics", cfg->threshold, 0.20f, 0.85f, 0.02f);

    return n;
}

// ── Geometric Emission Helpers ───────────────────────────────────────────────

static Vector3 GuidedLiquid_CalculateSpawnOffset(VFX_LiquidEmissionShape shape, float radius,
                                                float innerRadius, float coneAngleDeg,
                                                int index, int count, unsigned int *seed,
                                                Vector3 forwardDir)
{
    if (count <= 0) return Vector3Zero();
    float frac = (count > 1) ? ((float)index / (float)(count - 1)) : 0.5f;

    switch (shape)
    {
    case VFX_LIQUID_SHAPE_SPHERE: {
        // Fibonacci spherical spiral volume
        float phi = acosf(1.0f - 2.0f * ((float)index + 0.5f) / (float)count);
        float theta = PI * (1.0f + sqrtf(5.0f)) * (float)index;
        float r = radius * powf(frac, 0.40f);
        return (Vector3){r * sinf(phi) * cosf(theta), r * cosf(phi), r * sinf(phi) * sinf(theta)};
    }
    case VFX_LIQUID_SHAPE_RING: {
        // Toroidal perimeter ring in horizontal plane
        float angle = ((float)index / (float)count) * 2.0f * PI;
        float rRange = fmaxf(0.01f, radius - innerRadius);
        float r = innerRadius + rRange * (0.6f + 0.4f * GuidedLiquid_RandomFloat(seed));
        return (Vector3){r * cosf(angle), 0.06f * sinf(angle * 4.0f), r * sinf(angle)};
    }
    case VFX_LIQUID_SHAPE_CYLINDER: {
        // High-density vertical water column / jet nozzle
        float angle = ((float)index / (float)count) * 2.0f * PI;
        float r = radius * sqrtf((float)((index % 9) + 1) / 9.0f);
        float h = (radius * 2.5f) * (frac - 0.5f);
        return (Vector3){r * cosf(angle), h, r * sinf(angle)};
    }
    case VFX_LIQUID_SHAPE_HELIX: {
        // Double-helix spiral strands
        int strand = index % 2;
        float angle = ((float)index / (float)count) * 6.0f * PI + (float)strand * PI;
        float r = radius * 0.85f;
        float h = (radius * 3.5f) * (frac - 0.5f);
        return (Vector3){r * cosf(angle), h, r * sinf(angle)};
    }
    case VFX_LIQUID_SHAPE_DISC: {
        // Planar horizontal circular disc
        float angle = ((float)index / (float)count) * 2.0f * PI;
        float r = radius * sqrtf(frac);
        return (Vector3){r * cosf(angle), 0.02f, r * sinf(angle)};
    }
    default:
        return Vector3Zero();
    }
}

static inline float GuidedLiquid_Smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Physical Trajectory Evaluation ───────────────────────────────────────────

static Vector3 GuidedLiquid_CalculateTrajectoryPos(VFX_LiquidTrajectoryMode mode, Vector3 p0,
                                                  Vector3 pTarget, float progress, float arcHeight,
                                                  float swirlSpeed, float timer, unsigned int seed)
{
    progress = fmaxf(0.0f, fminf(1.0f, progress));

    // Linear baseline interpolation
    Vector3 basePos = Vector3Lerp(p0, pTarget, progress);
    Vector3 fwd = Vector3Subtract(pTarget, p0);
    float distXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    Vector3 fwdNorm = (distXZ > 0.001f) ? (Vector3){fwd.x / distXZ, 0.0f, fwd.z / distXZ} : (Vector3){0, 0, 1};
    Vector3 rightNorm = {-fwdNorm.z, 0.0f, fwdNorm.x};
    Vector3 upNorm = {0.0f, 1.0f, 0.0f};

    switch (mode)
    {
    case VFX_LIQUID_TRAJ_PARABOLIC_ARC: {
        float arcY = 4.0f * arcHeight * progress * (1.0f - progress);
        basePos.y += arcY;
        break;
    }
    case VFX_LIQUID_TRAJ_HIGH_CATAPULT: {
        float arcY = arcHeight * powf(sinf(progress * PI), 0.70f);
        basePos.y += arcY;
        break;
    }
    case VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW: {
        float arcY = 4.0f * arcHeight * progress * (1.0f - progress);
        basePos.y += arcY;
        float spiralAngle = swirlSpeed * timer + progress * 14.0f + (float)(seed % 100) * 0.06f;
        float spiralRad = 0.35f * sinf(progress * PI);
        basePos = Vector3Add(basePos, Vector3Scale(rightNorm, cosf(spiralAngle) * spiralRad));
        basePos = Vector3Add(basePos, Vector3Scale(upNorm, sinf(spiralAngle) * spiralRad));
        break;
    }
    case VFX_LIQUID_TRAJ_JET_STREAM: {
        float arcY = arcHeight * 0.25f * sinf(progress * PI);
        basePos.y += arcY;
        float eddyAngle = timer * 18.0f + progress * 24.0f;
        float eddyRad = 0.08f * sinf(progress * PI);
        basePos = Vector3Add(basePos, Vector3Scale(rightNorm, cosf(eddyAngle) * eddyRad));
        break;
    }
    case VFX_LIQUID_TRAJ_SERPENTINE_WAVE: {
        float waveLat = sinf(progress * 4.0f * PI - timer * 8.0f) * (arcHeight * 0.5f + 0.2f);
        basePos = Vector3Add(basePos, Vector3Scale(rightNorm, waveLat));
        float arcY = arcHeight * 0.4f * sinf(progress * PI);
        basePos.y += arcY;
        break;
    }
    case VFX_LIQUID_TRAJ_GUIDED_HOMING:
    default: {
        float arcY = 4.0f * arcHeight * progress * (1.0f - progress);
        basePos.y += arcY;
        break;
    }
    }

    return basePos;
}

// ── Target Impact & Ground Cascade Dispatch ──────────────────────────────────

static void GuidedLiquid_TriggerImpact(VC_GuidedLiquidInstance *inst, Vector3 impactPoint, Vector3 normal)
{
    if (!inst) return;
    const VFX_GuidedLiquidConfig *cfg = &inst->config;

    if (!inst->impactTriggered) {
        inst->impactTriggered = true;
        inst->impactPos = impactPoint;
        inst->impactNormal = normal;
        inst->impactTimer = 0.0f;

        // Anchor ground puddle strictly to terrain height at target
        float groundY = MapManager_GetGroundHeightAt(inst->curTarget.x, inst->curTarget.z);
        inst->groundPuddlePos = (Vector3){inst->curTarget.x, groundY + 0.015f, inst->curTarget.z};

        // Initialize ground puddle
        inst->puddleRadius = 0.10f;
        inst->puddleMaxRadius = cfg->puddleRadius;
        inst->puddleTimer = 0.0f;
        inst->puddleDuration = fmaxf(0.5f, cfg->puddleDuration);

        // Environmental wind shockwave flattening surrounding grass
        if (cfg->windBlastRadius > 0.05f) {
            Wind_SpawnRadialBlast(inst->groundPuddlePos, cfg->windBlastRadius, 14.0f, 0.45f);
            Wind_SpawnTurbulence(inst->groundPuddlePos, cfg->windBlastRadius * 0.75f, 6.0f, 0.25f, 3.5f, 0.35f);
        }

        // Conformal ground wave shock ring
        if (cfg->impactShockRing) {
            VFX_ComposeGroundWave(inst->groundPuddlePos, VC_MAT_WATER, cfg->dropletRadius * 4.2f, 0.45f, VFX_GroundHeightFromMap, NULL);
        }

        // Spawn conformal wet puddle decal
        if (cfg->impactDecal && !inst->decalSpawned) {
            inst->decalSpawned = true;
            VFX_ComposeDecal(inst->groundPuddlePos, VC_MAT_WATER, cfg->puddleRadius, 0.85f, 1.0f);
        }
    }
}

bool VFX_GuidedLiquid_IsAnyActive(void)
{
    for (int i = 0; i < VC_GUIDED_LIQUID_MAX_INSTANCES; i++) {
        if (s_guidedLiquidInstances[i].active) return true;
    }
    return false;
}

// ── Public Composition Entry Points ──────────────────────────────────────────

int VFX_ComposeGuidedLiquidEx(Vector3 source, Vector3 target, const VFX_GuidedLiquidConfig *cfg)
{
    if (!s_guidedLiquidInit) {
        VFX_GuidedLiquid_GetLiveConfig();
    }

    const VFX_GuidedLiquidConfig *activeCfg = cfg ? cfg : VFX_GuidedLiquid_GetLiveConfig();
    if (activeCfg->preset == VFX_LIQUID_PRESET_VORTEX_DRAGON) {
        VFX_WaterDragonConfig dragonCfg = VFX_WaterDragon_DefaultConfig();
        dragonCfg.travelSpeed = activeCfg->travelSpeed;
        dragonCfg.arcHeight = activeCfg->arcHeight;
        dragonCfg.spiralSpeed = activeCfg->swirlSpeed;
        dragonCfg.puddleRadius = activeCfg->puddleRadius;
        dragonCfg.puddleDuration = activeCfg->puddleDuration;
        return VFX_ComposeWaterDragonEx(source, target, &dragonCfg);
    }

    // Find available instance slot
    VC_GuidedLiquidInstance *inst = NULL;
    int instIdx = -1;
    for (int i = 0; i < VC_GUIDED_LIQUID_MAX_INSTANCES; i++) {
        if (!s_guidedLiquidInstances[i].active) {
            inst = &s_guidedLiquidInstances[i];
            instIdx = i;
            break;
        }
    }
    if (!inst) {
        // Pool exhaustion: steal oldest
        instIdx = 0;
        inst = &s_guidedLiquidInstances[0];
        if (inst->motionField != MOTION_FIELD_INVALID) {
            MotionFields_Stop(inst->motionField);
            inst->motionField = MOTION_FIELD_INVALID;
        }
    }

    memset(inst, 0, sizeof(VC_GuidedLiquidInstance));
    inst->active = true;
    inst->id = s_guidedLiquidNextId++;
    if (s_guidedLiquidNextId <= 0) s_guidedLiquidNextId = 1;

    // Copy authored config
    if (cfg) {
        inst->config = *cfg;
    } else {
        inst->config = *VFX_GuidedLiquid_GetLiveConfig();
    }
    inst->config.source = source;
    inst->config.target = target;
    inst->curTarget = target;

    // Optical material description
    LiquidMotionProfile profile = (LiquidMotionProfile)inst->config.liquidProfile;
    inst->material = LiquidSurface_ProfileDesc(profile);
    inst->material.body = inst->config.liquidBodyColor;
    inst->material.glow = inst->config.liquidGlowColor;
    inst->material.soft = inst->config.liquidFoamColor;
    inst->material.ior = inst->config.ior;
    inst->material.foam = inst->config.foamAmount;

    // Compute trajectory distance and flight time
    Vector3 fwd = Vector3Subtract(target, source);
    float dist = Vector3Length(fwd);
    float speed = fmaxf(3.0f, inst->config.travelSpeed);
    float flightTime = (dist > 0.05f) ? (dist / speed) : 0.5f;
    inst->flightDuration = flightTime;

    // Initialize high-density fluid droplets (hundreds to thousands of particles)
    int count = inst->config.particleCount;
    if (count < 32) count = 32;
    if (count > VC_GUIDED_LIQUID_MAX_DROPLETS) count = VC_GUIDED_LIQUID_MAX_DROPLETS;
    inst->dropletCount = count;

    unsigned int rng = (unsigned int)inst->id * 1337u + 42u;
    Vector3 fwdNorm = (dist > 0.001f) ? Vector3Scale(fwd, 1.0f / dist) : (Vector3){0, 0, 1};

    for (int i = 0; i < count; i++) {
        VC_LiquidDroplet *d = &inst->droplets[i];
        d->seed = rng + (unsigned int)i * 101u;
        d->age = 0.0f;
        d->flightProgress = 0.0f;
        d->flightDuration = flightTime;
        d->wakeWindFired = false;
        d->bounceCount = 0;
        d->swirlPhase = GuidedLiquid_RandomFloat(&d->seed) * 2.0f * PI;

        // Size variation across particles
        float var = inst->config.blobVariation;
        float rScale = (1.0f - var * 0.5f) + var * GuidedLiquid_RandomFloat(&d->seed);
        d->baseRadius = inst->config.dropletRadius * rScale;
        d->radius = d->baseRadius;

        // Staggered launch delay & geometry setup
        if (inst->config.spawnMode == VFX_LIQUID_SPAWN_STREAM) {
            d->launchDelay = (float)i * inst->config.launchStaggerDelay;
            // Nozzle stream: tightly centered around emitter nozzle
            float nozzleR = fminf(inst->config.emissionRadius, 0.16f);
            float nAngle = GuidedLiquid_RandomFloat(&d->seed) * 2.0f * PI;
            float nDist = nozzleR * sqrtf(GuidedLiquid_RandomFloat(&d->seed));
            Vector3 rightNorm = {-fwdNorm.z, 0.0f, fwdNorm.x};
            d->localOffset = Vector3Add(Vector3Scale(rightNorm, cosf(nAngle) * nDist),
                                        Vector3Scale((Vector3){0, 1, 0}, sinf(nAngle) * nDist));
            d->launchPos = Vector3Add(source, d->localOffset);
            d->targetPos = target;
            if (d->launchDelay > 0.0001f) {
                d->state = DROPLET_STATE_IDLE;
            } else {
                d->state = DROPLET_STATE_FLYING;
            }
        } else {
            // Burst / Ground gush / Orbital converge: gather into authored emission shape
            d->launchDelay = (float)(i % 16) * (inst->config.launchStaggerDelay * 0.15f);
            d->localOffset = GuidedLiquid_CalculateSpawnOffset(
                inst->config.emissionShape, inst->config.emissionRadius,
                inst->config.emissionInnerRadius, inst->config.coneAngleDeg,
                i, count, &d->seed, fwdNorm
            );
            d->launchPos = Vector3Add(source, d->localOffset);
            d->targetPos = target;
            if (d->launchDelay > 0.001f) {
                d->state = DROPLET_STATE_IDLE;
            } else if (inst->config.emergeDuration > 0.01f) {
                d->state = DROPLET_STATE_EMERGING;
                if (inst->config.spawnMode == VFX_LIQUID_SPAWN_GROUND_GUSH) {
                    d->launchPos.y = MapManager_GetGroundHeightAt(d->launchPos.x, d->launchPos.z) - 0.4f;
                }
            } else if (inst->config.hoverDuration > 0.01f) {
                d->state = DROPLET_STATE_HOVERING;
            } else {
                d->state = DROPLET_STATE_FLYING;
            }
        }
        d->pos = d->launchPos;
        d->prevPos = d->pos;
        d->velocity = Vector3Zero();
    }

    // Physical Motion Field integration
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_MOVING_GUIDE,
        .origin = source,
        .axis = fwd,
        .radiusM = inst->config.guideRadius,
        .speedMps = speed,
        .swirlSpeedMps = inst->config.swirlSpeed,
        .turbulenceSpeedMps = inst->config.turbulenceSpeed,
        .durationSec = flightTime + inst->config.emergeDuration + inst->config.hoverDuration + 3.0f,
        .attackSec = 0.05f,
        .fadeSec = 0.25f,
        .guidance = GUIDE_BALANCED,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    FieldDesc fieldDesc;
    if (MotionFieldRecipe_Build(&r, &fieldDesc)) {
        inst->motionField = MotionFields_CreateField(&fieldDesc);
        inst->motionFieldCreated = true;
    } else {
        inst->motionField = MOTION_FIELD_INVALID;
        inst->motionFieldCreated = false;
    }

    return inst->id;
}

int VFX_ComposeGuidedLiquid(const VFX_GuidedLiquidConfig *cfg)
{
    const VFX_GuidedLiquidConfig *c = cfg ? cfg : VFX_GuidedLiquid_GetLiveConfig();
    return VFX_ComposeGuidedLiquidEx(c->source, c->target, c);
}

void VFX_KillGuidedLiquid(int handle)
{
    for (int i = 0; i < VC_GUIDED_LIQUID_MAX_INSTANCES; i++) {
        if (s_guidedLiquidInstances[i].active && s_guidedLiquidInstances[i].id == handle) {
            if (s_guidedLiquidInstances[i].motionField != MOTION_FIELD_INVALID) {
                MotionFields_Stop(s_guidedLiquidInstances[i].motionField);
                s_guidedLiquidInstances[i].motionField = MOTION_FIELD_INVALID;
            }
            s_guidedLiquidInstances[i].active = false;
            break;
        }
    }
}

// ── Physical Simulation & State Update ───────────────────────────────────────

void VC_GuidedLiquid_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    for (int instIdx = 0; instIdx < VC_GUIDED_LIQUID_MAX_INSTANCES; instIdx++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        const VFX_GuidedLiquidConfig *cfg = &inst->config;

        // Vector frame along trajectory
        Vector3 fwd = Vector3Subtract(inst->curTarget, cfg->source);
        float distXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
        Vector3 fwdNorm = (distXZ > 0.001f) ? (Vector3){fwd.x / distXZ, 0.0f, fwd.z / distXZ} : (Vector3){0, 0, 1};

        // Emergence wind impulse
        if (!inst->emergeWindTriggered && inst->timer >= 0.05f) {
            inst->emergeWindTriggered = true;
            Wind_SpawnRadialBlast(cfg->source, cfg->emissionRadius * 2.2f, 4.0f, 0.35f);
        }

        // Advance moving guide position for MotionField
        float guideProgress = 0.0f;
        float launchPhase = cfg->emergeDuration + cfg->hoverDuration;
        if (inst->timer > launchPhase && inst->flightDuration > 0.001f) {
            guideProgress = Clamp((inst->timer - launchPhase) / inst->flightDuration, 0.0f, 1.0f);
        }
        Vector3 guidePos = GuidedLiquid_CalculateTrajectoryPos(
            cfg->trajectoryMode, cfg->source, inst->curTarget,
            guideProgress, cfg->arcHeight, cfg->swirlSpeed, inst->timer, (unsigned int)inst->id
        );
        if (inst->motionField != MOTION_FIELD_INVALID) {
            FieldTransform xf = {
                .position = guidePos,
                .axisX = (Vector3){1.0f, 0.0f, 0.0f},
                .axisY = (Vector3){0.0f, 1.0f, 0.0f},
                .axisZ = (Vector3){0.0f, 0.0f, 1.0f}
            };
            MotionFields_SetTransform(inst->motionField, &xf);
        }

        // Update high-density fluid droplets
        int activeDropletCount = 0;

        for (int i = 0; i < inst->dropletCount; i++) {
            VC_LiquidDroplet *d = &inst->droplets[i];
            d->age += dt;
            d->prevPos = d->pos;

            switch (d->state)
            {
            case DROPLET_STATE_IDLE: {
                if (inst->timer >= d->launchDelay) {
                    d->age = 0.0f;
                    if (cfg->spawnMode == VFX_LIQUID_SPAWN_STREAM) {
                        d->state = DROPLET_STATE_FLYING;
                        d->flightProgress = 0.0f;
                    } else if (cfg->emergeDuration > 0.01f) {
                        d->state = DROPLET_STATE_EMERGING;
                        if (cfg->spawnMode == VFX_LIQUID_SPAWN_GROUND_GUSH) {
                            float groundY = MapManager_GetGroundHeightAt(d->launchPos.x, d->launchPos.z);
                            d->launchPos.y = groundY - 0.4f;
                        }
                    } else if (cfg->hoverDuration > 0.01f) {
                        d->state = DROPLET_STATE_HOVERING;
                    } else {
                        d->state = DROPLET_STATE_FLYING;
                        d->flightProgress = 0.0f;
                    }
                    d->pos = d->launchPos;
                    d->prevPos = d->pos;
                }
                break;
            }

            case DROPLET_STATE_EMERGING: {
                activeDropletCount++;
                float emergeT = Clamp(d->age / fmaxf(0.01f, cfg->emergeDuration), 0.0f, 1.0f);

                if (cfg->spawnMode == VFX_LIQUID_SPAWN_GROUND_GUSH) {
                    float groundY = MapManager_GetGroundHeightAt(d->launchPos.x, d->launchPos.z);
                    d->pos.x = d->launchPos.x;
                    d->pos.z = d->launchPos.z;
                    d->pos.y = (groundY - 0.4f) + (cfg->source.y - groundY + 0.4f) * emergeT;
                } else if (cfg->spawnMode == VFX_LIQUID_SPAWN_ORBITAL_CONVERGE) {
                    float angle = d->swirlPhase + emergeT * 6.0f;
                    float curRad = cfg->emissionRadius * (1.0f - emergeT * 0.75f);
                    d->pos.x = cfg->source.x + cosf(angle) * curRad;
                    d->pos.z = cfg->source.z + sinf(angle) * curRad;
                    d->pos.y = cfg->source.y + 0.15f * sinf(emergeT * PI);
                } else {
                    d->pos = Vector3Lerp(d->launchPos, Vector3Add(cfg->source, Vector3Scale(d->localOffset, 0.5f)), emergeT);
                }

                if (d->age >= cfg->emergeDuration) {
                    d->state = (cfg->hoverDuration > 0.01f) ? DROPLET_STATE_HOVERING : DROPLET_STATE_FLYING;
                    d->age = 0.0f;
                    d->flightProgress = 0.0f;
                }
                break;
            }

            case DROPLET_STATE_HOVERING: {
                activeDropletCount++;
                float hoverT = Clamp(d->age / fmaxf(0.01f, cfg->hoverDuration), 0.0f, 1.0f);
                float bobY = 0.08f * sinf(hoverT * 2.0f * PI + d->swirlPhase);
                d->pos = Vector3Add(cfg->source, d->localOffset);
                d->pos.y += bobY;

                if (d->age >= cfg->hoverDuration) {
                    d->state = DROPLET_STATE_FLYING;
                    d->age = 0.0f;
                    d->flightProgress = 0.0f;
                }
                break;
            }

            case DROPLET_STATE_FLYING: {
                activeDropletCount++;
                d->flightProgress = Clamp(d->age / fmaxf(0.01f, d->flightDuration), 0.0f, 1.0f);

                // Per-droplet varied arc height
                float dropArc = cfg->arcHeight * (1.0f + cfg->arcVariation * (GuidedLiquid_RandomFloat(&d->seed) - 0.5f));

                // Calculate base trajectory position
                Vector3 trajPos = GuidedLiquid_CalculateTrajectoryPos(
                    cfg->trajectoryMode, cfg->source, inst->curTarget,
                    d->flightProgress, dropArc, cfg->swirlSpeed, inst->timer, d->seed
                );

                // Add local clustering offset with guide attraction (converges smoothly to stream axis)
                float clusterDamp = (1.0f - d->flightProgress) * (1.0f - d->flightProgress);
                Vector3 desiredPos = Vector3Add(trajPos, Vector3Scale(d->localOffset, clusterDamp));

                // Compute instantaneous velocity and blend towards guide
                Vector3 toDesired = Vector3Subtract(desiredPos, d->pos);
                Vector3 pullAcc = Vector3Scale(toDesired, cfg->pullForceN);
                d->velocity = Vector3Add(d->velocity, Vector3Scale(pullAcc, dt));
                d->velocity = Vector3Scale(d->velocity, 1.0f / (1.0f + cfg->fluidDrag * dt));
                d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, dt));

                // Tight convergence to path
                d->pos = Vector3Lerp(d->pos, desiredPos, Clamp(dt * 14.0f, 0.0f, 1.0f));

                // Aerodynamic wind wake parting foliage
                if (!d->wakeWindFired && d->flightProgress >= 0.35f && d->flightProgress <= 0.85f && (i % 8 == 0)) {
                    Wind_SpawnGust(d->pos, fwdNorm, cfg->dropletRadius * 3.5f, fminf(cfg->travelSpeed * 0.6f, 12.0f), 0.18f);
                    d->wakeWindFired = true;
                }

                // Check for target arrival or stray ground collision
                float groundY = MapManager_GetGroundHeightAt(d->pos.x, d->pos.z);
                float distToTarget = Vector3Distance(d->pos, inst->curTarget);
                bool arrivedAtTarget = (d->flightProgress >= 0.95f || distToTarget < 0.65f);

                if (arrivedAtTarget) {
                    // Transition to downward gravity cascade
                    d->state = DROPLET_STATE_CASCADING;
                    d->age = 0.0f;
                    float scatterSpeed = cfg->splashVelocity * (0.4f + 0.6f * GuidedLiquid_RandomFloat(&d->seed));
                    float scatterAngle = GuidedLiquid_RandomFloat(&d->seed) * 2.0f * PI;

                    // Waterfall downward gravity cascade + radial burst scatter
                    d->velocity.x = cosf(scatterAngle) * scatterSpeed + fwdNorm.x * cfg->travelSpeed * 0.12f;
                    d->velocity.z = sinf(scatterAngle) * scatterSpeed + fwdNorm.z * cfg->travelSpeed * 0.12f;
                    d->velocity.y = -(2.5f + 4.5f * GuidedLiquid_RandomFloat(&d->seed)); // Downward rush!

                    GuidedLiquid_TriggerImpact(inst, inst->curTarget, (Vector3){0, 1, 0});
                } else if (d->flightProgress > 0.15f && d->pos.y <= groundY + 0.02f) {
                    // Stray droplet hit ground mid-flight: settle and dissipate without triggering master impact
                    d->pos.y = groundY + 0.015f;
                    d->state = DROPLET_STATE_DEAD;
                }
                break;
            }

            case DROPLET_STATE_CASCADING: {
                activeDropletCount++;

                // Real downward gravitational acceleration ($9.81 m/s^2$)
                d->velocity.y -= PHYSICS_GRAVITY_MPS2 * cfg->gravityScale * dt;
                d->velocity.x *= 1.0f / (1.0f + 0.35f * dt);
                d->velocity.z *= 1.0f / (1.0f + 0.35f * dt);
                d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, dt));

                // Check terrain collision
                float groundY = MapManager_GetGroundHeightAt(d->pos.x, d->pos.z);
                if (d->pos.y <= groundY + 0.02f) {
                    d->pos.y = groundY + 0.015f;
                    d->bounceCount++;

                    // Bounce with water restitution or settle into puddle
                    if (d->bounceCount == 1 && fabsf(d->velocity.y) > 2.0f) {
                        d->velocity.y = -d->velocity.y * 0.22f;
                        d->velocity.x *= 0.45f;
                        d->velocity.z *= 0.45f;
                    } else {
                        d->state = DROPLET_STATE_SETTLED;
                        d->age = 0.0f;
                        d->velocity = Vector3Zero();
                        inst->puddleRadius = fminf(inst->puddleMaxRadius, inst->puddleRadius + 0.0015f);
                    }
                }
                break;
            }

            case DROPLET_STATE_SETTLED: {
                d->age += dt;
                if (d->age >= 0.18f) {
                    d->state = DROPLET_STATE_DEAD;
                } else {
                    activeDropletCount++;
                    // Smoothly fade out as droplet dissolves into ground puddle body
                    float mergeFade = 1.0f - (d->age / 0.18f);
                    d->radius = d->baseRadius * mergeFade * 0.75f;
                }
                break;
            }

            case DROPLET_STATE_DEAD:
            default:
                break;
            }
        }

        // Update ground puddle expansion and fading
        if (inst->impactTriggered) {
            inst->impactTimer += dt;
            if (inst->puddleRadius < inst->puddleMaxRadius) {
                float growSpeed = inst->puddleMaxRadius / fmaxf(0.25f, cfg->puddleDuration * 0.35f);
                inst->puddleRadius = fminf(inst->puddleMaxRadius, inst->puddleRadius + growSpeed * dt);
            }
            inst->puddleTimer += dt;
        }

        // Cleanup instance when finished
        bool puddleExpired = inst->impactTriggered && (inst->puddleTimer >= inst->puddleDuration);
        bool allDead = (activeDropletCount == 0 && puddleExpired);
        if (allDead || inst->timer > (inst->flightDuration + cfg->emergeDuration + cfg->hoverDuration + inst->puddleDuration + 3.0f)) {
            if (inst->motionField != MOTION_FIELD_INVALID) {
                MotionFields_Stop(inst->motionField);
                inst->motionField = MOTION_FIELD_INVALID;
            }
            inst->active = false;
        }
    }
}

// ── Screen-Space Surface Submission (SSF Pass) ───────────────────────────────

void VC_GuidedLiquid_SubmitSurface(void)
{
    for (int instIdx = 0; instIdx < VC_GUIDED_LIQUID_MAX_INSTANCES; instIdx++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[instIdx];
        if (!inst->active) continue;

        const VFX_GuidedLiquidConfig *cfg = &inst->config;
        if (cfg->renderBackend != VFX_LIQUID_BACKEND_SSF &&
            cfg->renderBackend != VFX_LIQUID_BACKEND_HYBRID) {
            continue;
        }

        LiquidSurface_BindMaterial(&inst->material);
        // Blending reconstruction radius scaled to fuse adjacent droplets smoothly
        LiquidSurface_SetReconstructionRadius(cfg->dropletRadius * 1.15f);

        // Solid core anchor for spherical hover/emerge
        if (cfg->emissionShape == VFX_LIQUID_SHAPE_SPHERE &&
            (inst->timer < (cfg->emergeDuration + cfg->hoverDuration))) {
            LiquidSurface_RegisterParticle(cfg->source, cfg->dropletRadius * 1.4f);
        }

        // Submit fluid droplets up to LIQUID_SURFACE_MAX_PARTICLES (384)
        if (inst->dropletCount <= LIQUID_SURFACE_MAX_PARTICLES) {
            for (int i = 0; i < inst->dropletCount; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (d->state != DROPLET_STATE_EMERGING &&
                    d->state != DROPLET_STATE_HOVERING &&
                    d->state != DROPLET_STATE_FLYING &&
                    d->state != DROPLET_STATE_CASCADING) continue;
                LiquidSurface_RegisterParticle(d->pos, d->radius * 1.35f);
            }
        } else {
            // Stratified sampling across the entire volume so SSF covers center to surface
            int maxSSF = LIQUID_SURFACE_MAX_PARTICLES;
            for (int k = 0; k < maxSSF; k++) {
                int idx = (k * (inst->dropletCount - 1)) / (maxSSF - 1);
                const VC_LiquidDroplet *d = &inst->droplets[idx];
                if (d->state != DROPLET_STATE_EMERGING &&
                    d->state != DROPLET_STATE_HOVERING &&
                    d->state != DROPLET_STATE_FLYING &&
                    d->state != DROPLET_STATE_CASCADING) continue;
                LiquidSurface_RegisterParticle(d->pos, d->radius * 1.50f);
            }
        }

        // Submit terrain-conformal expanding puddle with physical liquid meniscus dome thickness
        if (inst->impactTriggered && inst->puddleRadius > 0.05f) {
            float alphaFade = 1.0f - Clamp(inst->puddleTimer / inst->puddleDuration, 0.0f, 1.0f);
            float curRad = inst->puddleRadius * sqrtf(alphaFade);
            if (curRad > 0.05f) {
                float puddleThickness = fminf(0.10f, 0.035f + curRad * 0.05f);
                LiquidSurface_RegisterEllipsoid(
                    (Vector3){inst->groundPuddlePos.x, inst->groundPuddlePos.y + puddleThickness * 0.5f, inst->groundPuddlePos.z},
                    (Vector3){curRad, puddleThickness, curRad}
                );
            }
        }
    }
}

static Texture2D GuidedLiquid_GetDropletTexture(void)
{
    static Texture2D s_dropletTex = {0};
    if (s_dropletTex.id != 0) return s_dropletTex;

    const int N = 128;
    Image img = GenImageColor(N, N, BLANK);
    Color *pixels = (Color *)img.data;
    float half = (float)N * 0.5f;
    float invHalf = 1.0f / half;

    // Key sun light direction from top-right in camera space
    Vector3 sunL = Vector3Normalize((Vector3){0.38f, 0.72f, 0.58f});
    Vector3 bounceL = Vector3Normalize((Vector3){-0.35f, -0.65f, 0.67f});
    Vector3 viewV = {0.0f, 0.0f, 1.0f};
    Vector3 halfSun = Vector3Normalize(Vector3Add(sunL, viewV));

    for (int y = 0; y < N; y++) {
        float py = ((float)y + 0.5f - half) * invHalf;
        for (int x = 0; x < N; x++) {
            float px = ((float)x + 0.5f - half) * invHalf;
            float r2 = px * px + py * py;
            if (r2 >= 1.0f) {
                pixels[y * N + x] = (Color){0, 0, 0, 0};
                continue;
            }

            float r = sqrtf(r2);
            // Smooth, anti-aliased circular alpha edge boundary (ZERO hard corners)
            float edgeAlpha = 1.0f - GuidedLiquid_Smoothstep(0.72f, 1.0f, r);

            // 3D spherical dome normal (surface normal of water droplet)
            float nz = sqrtf(1.0f - r2);
            Vector3 norm = {px, py, nz};

            // Photorealistic water lighting components
            float nDotL = fmaxf(0.0f, Vector3DotProduct(norm, sunL));
            float nDotH = fmaxf(0.0f, Vector3DotProduct(norm, halfSun));
            float specSun = powf(nDotH, 28.0f);

            float nDotBounce = fmaxf(0.0f, Vector3DotProduct(norm, bounceL));
            float specBounce = powf(nDotBounce, 12.0f) * 0.22f;

            // Translucent core absorption (Beer-Lambert cyan water body)
            float coreDiffuse = 0.70f + 0.30f * nDotL;

            // Dielectric Fresnel edge rim (glancing angle reflection)
            float fresnel = powf(1.0f - nz, 2.4f) * 0.60f;

            // Composite glossy water droplet RGBA
            float red   = Clamp(180.0f * coreDiffuse + 255.0f * specSun + 130.0f * fresnel + 45.0f * specBounce, 0.0f, 255.0f);
            float green = Clamp(228.0f * coreDiffuse + 255.0f * specSun + 175.0f * fresnel + 85.0f * specBounce, 0.0f, 255.0f);
            float blue  = Clamp(255.0f * coreDiffuse + 255.0f * specSun + 235.0f * fresnel + 140.0f * specBounce, 0.0f, 255.0f);
            float alpha = Clamp((0.68f + 0.32f * specSun + 0.20f * fresnel) * edgeAlpha * 255.0f, 0.0f, 255.0f);

            pixels[y * N + x] = (Color){(unsigned char)red, (unsigned char)green, (unsigned char)blue, (unsigned char)alpha};
        }
    }

    s_dropletTex = LoadTextureFromImage(img);
    SetTextureFilter(s_dropletTex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_dropletTex, TEXTURE_WRAP_CLAMP);
    UnloadImage(img);
    return s_dropletTex;
}

// ── 3D Geometry Pass (Metaball 3D & High-Performance Droplet Rendering) ───────

void VC_GuidedLiquid_Draw3D(Camera3D cam)
{
    Texture2D dropletTex = GuidedLiquid_GetDropletTexture();

    // Camera basis vectors for strictly camera-facing quad alignment
    Vector3 camFwd = Vector3Subtract(cam.target, cam.position);
    float camDist = Vector3Length(camFwd);
    if (camDist > 0.001f) camFwd = Vector3Scale(camFwd, 1.0f / camDist);
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(camFwd, cam.up));
    Vector3 camUp = Vector3CrossProduct(camRight, camFwd);

    for (int instIdx = 0; instIdx < VC_GUIDED_LIQUID_MAX_INSTANCES; instIdx++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[instIdx];
        if (!inst->active) continue;

        const VFX_GuidedLiquidConfig *cfg = &inst->config;

        // 1. True 3D Analytical Metaball Raymarching (for Macro Body Core)
        if (cfg->renderBackend == VFX_LIQUID_BACKEND_METABALL3D ||
            cfg->renderBackend == VFX_LIQUID_BACKEND_HYBRID)
        {
            LiquidMetaball3D_BeginBody(&inst->material, cfg->threshold);

            // Solid core anchor for spherical hover/emerge
            if (cfg->emissionShape == VFX_LIQUID_SHAPE_SPHERE &&
                (inst->timer < (cfg->emergeDuration + cfg->hoverDuration))) {
                LiquidMetaball3D_RegisterBlob(cfg->source, cfg->dropletRadius * 1.6f);
            }

            // Register all active droplets of the fluid body into LiquidMetaball3D.
            // When dropletCount exceeds 64, stratified seed sampling ensures the initial 64
            // blobs span the entire volume (from center to surface), and subsequent blobs
            // merge their volume into the nearest seed blob.
            if (inst->dropletCount <= LIQUID_METABALL3D_MAX_BLOBS) {
                for (int i = 0; i < inst->dropletCount; i++) {
                    const VC_LiquidDroplet *d = &inst->droplets[i];
                    if (d->state == DROPLET_STATE_EMERGING ||
                        d->state == DROPLET_STATE_HOVERING ||
                        d->state == DROPLET_STATE_FLYING ||
                        d->state == DROPLET_STATE_CASCADING) {
                        LiquidMetaball3D_RegisterBlob(d->pos, d->radius * 1.35f);
                    }
                }
            } else {
                // Stratified seed pass: 64 blobs evenly distributed across all radial layers & azimuths
                int maxBlobs = LIQUID_METABALL3D_MAX_BLOBS;
                for (int k = 0; k < maxBlobs; k++) {
                    int idx = (k * (inst->dropletCount - 1)) / (maxBlobs - 1);
                    const VC_LiquidDroplet *d = &inst->droplets[idx];
                    if (d->state == DROPLET_STATE_EMERGING ||
                        d->state == DROPLET_STATE_HOVERING ||
                        d->state == DROPLET_STATE_FLYING ||
                        d->state == DROPLET_STATE_CASCADING) {
                        LiquidMetaball3D_RegisterBlob(d->pos, d->radius * 1.35f);
                    }
                }
                // Volume consolidation pass: remaining droplets merge into nearest seed blob
                int mergeStride = (inst->dropletCount > 512) ? 4 : 1;
                for (int i = 0; i < inst->dropletCount; i += mergeStride) {
                    const VC_LiquidDroplet *d = &inst->droplets[i];
                    if (d->state == DROPLET_STATE_EMERGING ||
                        d->state == DROPLET_STATE_HOVERING ||
                        d->state == DROPLET_STATE_FLYING ||
                        d->state == DROPLET_STATE_CASCADING) {
                        float vScale = (float)mergeStride;
                        float effectiveRad = d->radius * cbrtf(vScale) * 1.35f;
                        LiquidMetaball3D_RegisterBlob(d->pos, effectiveRad);
                    }
                }
            }

            // Worthington splash crown rim blobs (expand from ground level and collapse)
            if (inst->impactTriggered && cfg->impactCrown && inst->impactTimer < 0.35f) {
                float crownT = inst->impactTimer / 0.35f;
                float crownRad = cfg->dropletRadius * (1.2f + 2.0f * crownT);
                float crownH = cfg->dropletRadius * 2.0f * sinf(crownT * PI);
                int rimPoints = 8;
                for (int p = 0; p < rimPoints; p++) {
                    float angle = ((float)p / (float)rimPoints) * 2.0f * PI;
                    Vector3 rimPos = {
                        inst->groundPuddlePos.x + cosf(angle) * crownRad,
                        inst->groundPuddlePos.y + crownH * (0.8f + 0.3f * sinf((float)p * 2.0f)),
                        inst->groundPuddlePos.z + sinf(angle) * crownRad
                    };
                    LiquidMetaball3D_RegisterBlob(rimPos, cfg->dropletRadius * 0.38f * (1.0f - crownT * 0.5f));
                }
            }

            // Flat terrain-conformal puddle in 3D Metaball (concentric small blobs at ground level)
            if (inst->impactTriggered && inst->puddleRadius > 0.10f) {
                float alphaFade = 1.0f - Clamp(inst->puddleTimer / inst->puddleDuration, 0.0f, 1.0f);
                float curRad = inst->puddleRadius * sqrtf(alphaFade);
                if (curRad > 0.10f) {
                    LiquidMetaball3D_RegisterBlob(inst->groundPuddlePos, fminf(0.25f, curRad * 0.45f));
                    int ringBlobs = 8;
                    for (int rb = 0; rb < ringBlobs; rb++) {
                        float theta = ((float)rb / (float)ringBlobs) * 2.0f * PI;
                        float rx = inst->groundPuddlePos.x + cosf(theta) * curRad * 0.55f;
                        float rz = inst->groundPuddlePos.z + sinf(theta) * curRad * 0.55f;
                        float ry = MapManager_GetGroundHeightAt(rx, rz) + 0.04f;
                        LiquidMetaball3D_RegisterBlob((Vector3){rx, ry, rz}, fminf(0.20f, curRad * 0.35f));
                    }
                }
            }

            LiquidMetaball3D_EndBody();
        }

        // 2. High-Performance Batched Water Droplet Quad Rendering (Secondary Cascading Splash Spray)
        // For coherent macro liquid phases (EMERGING, HOVERING, FLYING), the fluid body is 100% rendered
        // by the fluid surface backends (Metaball 3D or SSF) without any planar quad slicing artifacts.
        // Billboard quads are strictly reserved for detached cascading splash/mist spray droplets
        // after impact.
        int cascadeCount = 0;
        for (int i = 0; i < inst->dropletCount; i++) {
            if (inst->droplets[i].state == DROPLET_STATE_CASCADING) {
                cascadeCount++;
            }
        }
        if (cascadeCount == 0) continue;

        rlPushMatrix();
        rlDisableBackfaceCulling();
        rlEnableDepthTest();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ALPHA);

        int batchChunk = 256;
        for (int bStart = 0; bStart < inst->dropletCount; bStart += batchChunk) {
            int bEnd = (bStart + batchChunk < inst->dropletCount) ? bStart + batchChunk : inst->dropletCount;

            rlSetTexture(dropletTex.id);
            rlBegin(RL_QUADS);
            for (int i = bStart; i < bEnd; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (d->state != DROPLET_STATE_CASCADING) continue;

                // Project velocity onto camera view plane to eliminate depth foreshortening & squashing
                Vector3 vCam = Vector3Subtract(d->velocity, Vector3Scale(camFwd, Vector3DotProduct(d->velocity, camFwd)));
                float speedCam = Vector3Length(vCam);

                Vector3 uLong, uTrans;
                float stretch = 1.0f;
                if (speedCam > 0.35f) {
                    uLong = Vector3Scale(vCam, 1.0f / speedCam);
                    uTrans = Vector3Normalize(Vector3CrossProduct(uLong, camFwd));
                    stretch = Clamp(1.0f + speedCam * 0.035f, 1.0f, 2.2f);
                } else {
                    uLong = camUp;
                    uTrans = camRight;
                    stretch = 1.0f;
                }

                float r = d->radius;
                Vector3 extX = Vector3Scale(uTrans, r);
                Vector3 extY = Vector3Scale(uLong, r * stretch);

                Color col = cfg->liquidBodyColor;
                float fade = 1.0f;
                if (d->state == DROPLET_STATE_SETTLED) {
                    fade = 1.0f - Clamp(d->age / 0.18f, 0.0f, 1.0f);
                }

                col.a = (unsigned char)(col.a * fade * 0.85f);

                Vector3 p0 = Vector3Subtract(Vector3Subtract(d->pos, extX), extY);
                Vector3 p1 = Vector3Subtract(Vector3Add(d->pos, extX), extY);
                Vector3 p2 = Vector3Add(Vector3Add(d->pos, extX), extY);
                Vector3 p3 = Vector3Add(Vector3Subtract(d->pos, extX), extY);

                rlColor4ub(col.r, col.g, col.b, col.a);
                rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p0.x, p0.y, p0.z);
                rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
                rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p2.x, p2.y, p2.z);
                rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
            }
            rlEnd();
        }

        rlSetTexture(0);
        EndBlendMode();
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
        rlPopMatrix();
    }
}

void VC_GuidedLiquid_DrawShadowPass(void)
{
    // Directional shadow pass: Liquids are translucent dielectrics and cast soft ground decals
}

#endif // VC_GUIDED_LIQUID_INL

// ── GENERALIZED GUIDED LIQUID (HỆ THỦY: DẪN LỰC THỦY QUÁI) ───────────────────
//
// One liquid model, every liquid. The effect is a *physical* body of droplets:
//
//   Medium    `core/liquid/liquid_medium.h` supplies the SI identity of the
//             liquid (density, dynamic viscosity, surface tension, yield
//             stress). Everything below is derived from those four numbers, so
//             water, poison, mud, lava and mercury differ by physics rather
//             than by separate tuning tables.
//
//   Guide     The guide is a moving FRAME, never a spline position. Its state
//             comes from `core/liquid/liquid_ballistic.h` (exact projectile
//             solution: apex-authored or speed-authored), so at every instant
//             the frame has a real velocity and a real acceleration — gravity.
//             Helical and serpentine steering are added as analytic terms with
//             matching first and second derivatives, not as a sine wobble on a
//             lerped position.
//
//   Motion    The frame owns a typed `FieldDesc` (core/motion) that moves with
//             it and publishes `frameVelocityMps`: inside its radius the medium
//             (qi current) travels with the guide, plus a Rankine swirl and
//             curl-noise turbulence. Every droplet is integrated as a real body
//             (`MotionBody_AdvanceFieldVelocity`): gravity + buoyancy, quadratic
//             aerodynamic drag against the sampled medium, turbulence as a
//             Newton force. Droplet mass comes from its volume and the liquid's
//             density, so heavy liquids ignore the current and water does not.
//
//   Cohesion  Droplets hold their lane inside the moving frame through a
//             critically damped, force-budgeted spring (the skill's binding
//             actuator). Neighbour droplets additionally exchange a surface
//             tension attraction (2*pi*sigma*r, negligible for a metre-scale
//             blob — as it is in reality — dominant for the fine spray) and an
//             incompressibility separation impulse. Internal shear decays at the
//             liquid's own rate r^2/nu, and a Bingham yield makes mud and lava
//             stop flowing while water keeps spreading.
//
//   Impact    The body's real incoming velocity is handed to
//             `LiquidImpact_SpawnWater()` (core owns the crown/volume seeding),
//             and settling droplets feed a puddle whose spreading slows with the
//             medium's yield stress.
//
// Rendering: Metaball 3D raymarching or Screen-Space Fluid for the coherent
// mass; billboard quads are reserved for detached spray. Deliberately no
// per-frame RNG on the flight path: a droplet's arc is fixed at spawn.
//
#ifndef VC_GUIDED_LIQUID_INL
#define VC_GUIDED_LIQUID_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/motion/motion_body.h"
#include "core/motion/motion_recipe.h"
#include "core/motion/motion_fields.h"
#include "core/path_spline.h"
#include "core/liquid/liquid_medium.h"
#include "core/liquid/liquid_ballistic.h"
#include "core/liquid/liquid_surface.h"
#include "core/liquid/liquid_metaball3d.h"
#include "core/liquid/liquid_motion.h"
#include "core/liquid/liquid_impact.h"
#include "core/particles/particle_system.h"
#include "core/decals/decal_system.h"
#include "core/wind/wind_system.h"
#include "core/map_manager.h"
#include "core/vfx_render.h"
#include "environment/env_shadow.h"
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

#define VC_GUIDED_LIQUID_MAX_INSTANCES 8
#define VC_GUIDED_LIQUID_MAX_DROPLETS  2048
#define VC_GUIDED_LIQUID_MACRO_SURFACE_BLOBS 48
#define VC_GUIDED_LIQUID_TRAIL_POINTS 24
#define VC_GUIDED_LIQUID_TRAIL_LIFE_S 0.55f

/* Neighbour grid for surface tension / incompressibility. Fixed storage, shared
 * by every instance because only one body is walked at a time. */
#define VC_GUIDED_LIQUID_GRID_DIM 9
#define VC_GUIDED_LIQUID_GRID_CELLS (VC_GUIDED_LIQUID_GRID_DIM * VC_GUIDED_LIQUID_GRID_DIM * \
                                     VC_GUIDED_LIQUID_GRID_DIM)

typedef enum {
    DROPLET_STATE_IDLE = 0,
    DROPLET_STATE_EMERGING,
    DROPLET_STATE_HOVERING,
    DROPLET_STATE_FLYING,
    DROPLET_STATE_CASCADING, // Body broke on the receiver; ballistic spray
    DROPLET_STATE_SETTLED,   // Merged into the ground puddle
    DROPLET_STATE_DEAD
} VC_DropletState;

typedef struct {
    Vector3 pos;
    Vector3 prevPos;
    Vector3 velocity;
    Vector3 launchPos;
    Vector3 laneOffset;   // Body-frame offset, in the guide basis at launch
    float radius;         // Physical radius (metres): mass, drag, contact
    float drawRadius;     // Visual kernel radius: the density-field splat
    float baseRadius;
    float volumeM3;       // Conserved across merge/settle
    float massKg;
    VC_DropletState state;
    float launchDelay;
    float age;
    float swirlPhase;
    unsigned int seed;
    bool wakeWindFired;
    int bounceCount;
    MotionReceiver receiver;
} VC_LiquidDroplet;

/* Moving guide frame with a real derivative. */
typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 acc;
    Vector3 right;
    Vector3 up;
    Vector3 tangent;
} VC_LiquidGuideFrame;

typedef struct {
    bool active;
    int id;
    VFX_GuidedLiquidConfig config;
    LiquidMediumDesc medium;
    float timer;
    int dropletCount;
    VC_LiquidDroplet droplets[VC_GUIDED_LIQUID_MAX_DROPLETS];

    // Target the guide was solved for (set once; homing still tracks it)
    Vector3 curTarget;

    // Launched trajectory (exact projectile solution, fixed at spawn)
    LiquidBallisticSolution launch;
    bool launchSolved;
    float launchDelay;      // emerge + hover
    bool launched;

    // Guide frame state (integrated for homing, analytic otherwise)
    VC_LiquidGuideFrame frame;

    // Binding actuator: critically damped spring towards each droplet's lane
    float bindOmega;
    float bindAccelBudget;
    float pressureOmega;

    // Measured body state (mass-weighted centre of mass)
    Vector3 comPos;
    Vector3 comVel;
    float bodyMassKg;
    float bodyVolumeM3;
    float bodyRadiusM;

    /* Physical droplet radius for this body: the authored `dropletRadius` is a
     * ceiling (a sparse spray keeps it), but a body packed into the emitter
     * cannot exceed the emitter's own volume. */
    float dropletRadiusM;

    // Motion field (medium inside the frame: frame velocity + swirl + turbulence)
    MotionFieldHandle mediumField;
    bool fieldAlive;
    float fieldRadius;

    // Environmental interaction
    bool emergeWindTriggered;
    bool impactTriggered;
    Vector3 impactPos;
    Vector3 impactNormal;
    Vector3 impactVelocity;
    float impactSpeed;
    float impactTimer;

    // Ground-conformal wet moisture zone (replaces decal and puddle)
    Vector3 groundWetPos;
    float wetRadius;
    float wetMaxRadius;
    float wetTimer;
    float wetDuration;
    float wetVolumeM3;
    float wetThickness;

    // Ground-conformal puddle (legacy aliases maintained for compatibility)
    Vector3 groundPuddlePos;
    float puddleRadius;
    float puddleMaxRadius;
    float puddleTimer;
    float puddleDuration;
    float puddleThickness;
    float puddleVolumeM3;
    bool decalSpawned;

    // Material optics
    LiquidDesc material;

    /* Wake: recent mass-centre positions, newest first. One buffer feeds both
     * the mist trail (`emitMistTrail`) and the qi ribbon (`emitQiRibbon`), so
     * the two decorative paths can never disagree about where the body was. */
    Vector3 trailPos[VC_GUIDED_LIQUID_TRAIL_POINTS];
    float trailAge[VC_GUIDED_LIQUID_TRAIL_POINTS];
    int trailCount;
    float trailTimer;

    // Airborne spray emitted by the last impact (capped by splashDropletCount)
    int splashSpawned;
} VC_GuidedLiquidInstance;

static VC_GuidedLiquidInstance s_guidedLiquidInstances[VC_GUIDED_LIQUID_MAX_INSTANCES];
static VFX_GuidedLiquidConfig s_liveGuidedLiquidConfig;
static bool s_guidedLiquidInit = false;
static int s_guidedLiquidNextId = 1;

static int s_glGridHead[VC_GUIDED_LIQUID_GRID_CELLS];
static int s_glGridNext[VC_GUIDED_LIQUID_MAX_DROPLETS];

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

/* Deterministic [-0.5,0.5] from a stored seed, WITHOUT advancing it. Used where
 * a value must stay constant for the droplet's whole life. */
static inline float GuidedLiquid_StableSigned(unsigned int seed)
{
    return (float)(GuidedLiquid_Hash(seed) % 10000u) / 10000.0f - 0.5f;
}

// ── Medium Resolution ────────────────────────────────────────────────────────

static LiquidMediumDesc GuidedLiquid_ResolveMedium(const VFX_GuidedLiquidConfig *cfg)
{
    int profile = cfg->liquidProfile;
    if (profile < 0 || profile >= LIQUID_MEDIUM_COUNT) profile = LIQUID_MEDIUM_WATER;
    LiquidMediumDesc m = LiquidMedium_Get((LiquidMediumId)profile);
    return LiquidMedium_Override(m, cfg->mediumDensityKgM3, cfg->mediumViscosityPaS,
                                 cfg->mediumSurfaceTensionNm);
}

/* Real body description for one droplet: mass and frontal area both follow from
 * its volume, so a bigger blob is harder for the current to carry. */
static MotionBodyProfile GuidedLiquid_MakeBody(const VFX_GuidedLiquidConfig *cfg,
                                               const LiquidMediumDesc *medium, float radius,
                                               float *outMass)
{
    MotionBodyProfile body = (MotionBodyProfile){0};
    float mass = LiquidMedium_DropletMassKg(medium, radius);
    if (!(mass > 1e-6f)) mass = 1e-6f;
    body.inverseMassKg = 1.0f / mass;
    body.gravityScale = cfg->gravityScale;
    body.aerodynamicAreaM2 = LiquidMedium_DropletAreaM2(radius);
    body.aerodynamicDragCoefficient = medium->dragCoefficient;
    body.airDensityKgM3 = LIQUID_AIR_DENSITY_KG_M3;
    body.densityKgM3 = medium->densityKgM3; /* enables buoyancy: rho_air/rho */
    /* The quadratic-drag term is scaled by susceptibility; leaving it 0 would
     * make the droplet match the airflow instantly instead of feeling drag. */
    body.windSusceptibility = 1.0f;
    body.windCouplingHz = 4.5f;
    if (outMass) *outMass = mass;
    return body;
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
    cfg.liquidProfile = LIQUID_MEDIUM_WATER;

    // Phase 1: Emission & Geometry
    cfg.particleCount = 512;
    cfg.emissionRadius = 0.45f;
    cfg.emissionInnerRadius = 0.0f;
    cfg.coneAngleDeg = 20.0f;
    cfg.dropletRadius = 0.16f;
    cfg.blobVariation = 0.35f;
    cfg.emergeDuration = 0.0f;
    cfg.hoverDuration = 0.0f;
    cfg.launchStaggerDelay = 0.0f;

    // Phase 2: Guiding Forces & Flight
    cfg.source = (Vector3){6.0f, 1.2f, 4.4f};
    cfg.target = (Vector3){12.0f, 0.0f, 4.4f};
    cfg.travelSpeed = 16.0f;
    cfg.arcHeight = 2.8f;
    cfg.arcVariation = 0.18f;
    cfg.guideRadius = 1.2f;
    cfg.pullForceN = 5.5f;
    cfg.forwardForceN = 1.4f;
    cfg.swirlSpeed = 8.0f;   // rad/s of helical swirl
    cfg.turbulenceSpeed = 2.2f;
    cfg.fluidDrag = 1.1f;
    cfg.gravityScale = 1.0f;
    cfg.emitMistTrail = true;
    cfg.emitQiRibbon = false;
    cfg.ribbonWidth = 0.08f;

    // Phase 3: Impact & Aftermath (Expanding Wet Moisture replaces Decal and Puddle)
    cfg.splashDropletCount = 32;
    cfg.splashVelocity = 6.0f;
    cfg.wetRadius = 1.8f;
    cfg.wetDuration = 3.5f;
    cfg.puddleRadius = cfg.wetRadius;
    cfg.puddleDuration = cfg.wetDuration;
    cfg.impactCrown = true;
    cfg.impactShockRing = true;
    cfg.impactDecal = false;
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

    // Physical medium: 0 = measured profile values
    cfg.mediumDensityKgM3 = 0.0f;
    cfg.mediumViscosityPaS = 0.0f;
    cfg.mediumSurfaceTensionNm = 0.0f;
    cfg.cohesionScale = 0.0f;
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

    // Medium overrides default to "measured values for the profile".
    cfg->mediumDensityKgM3 = 0.0f;
    cfg->mediumViscosityPaS = 0.0f;
    cfg->mediumSurfaceTensionNm = 0.0f;
    cfg->cohesionScale = 1.0f;
    cfg->liquidProfile = LIQUID_MEDIUM_WATER;
    cfg->gravityScale = 1.0f;
    cfg->emitQiRibbon = false;
    cfg->ribbonWidth = 0.08f;

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
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.0f;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 2.8f;
        cfg->arcVariation = 0.18f;
        cfg->guideRadius = 1.2f;
        cfg->pullForceN = 5.5f;
        cfg->forwardForceN = 1.4f;
        cfg->swirlSpeed = 8.0f;
        cfg->turbulenceSpeed = 2.2f;
        cfg->fluidDrag = 1.1f;
        cfg->emitMistTrail = true;
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
        cfg->particleCount = 512;
        cfg->emissionRadius = 0.20f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 10.0f;
        cfg->dropletRadius = 0.11f;
        cfg->blobVariation = 0.25f;
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.004f;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 0.6f;
        cfg->arcVariation = 0.05f;
        cfg->guideRadius = 1.0f;
        cfg->pullForceN = 8.0f;
        cfg->forwardForceN = 3.0f;
        cfg->swirlSpeed = 12.0f;
        cfg->turbulenceSpeed = 2.0f;
        cfg->fluidDrag = 0.8f;
        cfg->gravityScale = 0.5f;
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
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
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
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
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
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
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
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
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

    case VFX_LIQUID_PRESET_MOLTEN_GEYSER:
        /* Lava: a Bingham plastic. Low restitution, very high viscosity and a
         * real yield stress mean the splash barely rebounds and the puddle
         * creeps instead of spreading. */
        cfg->spawnMode = VFX_LIQUID_SPAWN_GROUND_GUSH;
        cfg->emissionShape = VFX_LIQUID_SHAPE_CYLINDER;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_LIQUID_IMPACT_DELUGE_CASCADE;
        cfg->renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
        cfg->liquidProfile = LIQUID_MEDIUM_LAVA;
        cfg->particleCount = 640;
        cfg->emissionRadius = 0.9f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 40.0f;
        cfg->dropletRadius = 0.22f;
        cfg->blobVariation = 0.30f;
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.002f;
        cfg->travelSpeed = 11.0f;
        cfg->arcHeight = 6.5f;
        cfg->arcVariation = 0.15f;
        cfg->guideRadius = 1.5f;
        cfg->pullForceN = 4.0f;
        cfg->forwardForceN = 1.0f;
        cfg->swirlSpeed = 2.0f;
        cfg->turbulenceSpeed = 0.8f;
        cfg->fluidDrag = 3.0f;
        cfg->cohesionScale = 1.6f;
        cfg->gravityScale = 0.9f;
        cfg->emitMistTrail = true;
        cfg->splashDropletCount = 18;
        cfg->splashVelocity = 3.0f;
        cfg->puddleRadius = 2.6f;
        cfg->puddleDuration = 5.5f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 1.8f;
        cfg->impactShake = 0.25f;
        cfg->windBlastRadius = 3.0f;
        cfg->liquidBodyColor = (Color){196, 62, 12, 245};
        cfg->liquidGlowColor = (Color){255, 186, 74, 255};
        cfg->liquidFoamColor = (Color){255, 228, 150, 255};
        cfg->threshold = 0.46f;
        break;

    case VFX_LIQUID_PRESET_MERCURIAL_ORB:
        /* Mercury: 13.5x water density, tiny viscosity, huge surface tension.
         * The current cannot shear it and capillarity pulls it into beads. */
        cfg->spawnMode = VFX_LIQUID_SPAWN_ORBITAL_CONVERGE;
        cfg->emissionShape = VFX_LIQUID_SHAPE_SPHERE;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_GUIDED_HOMING;
        cfg->impactMode = VFX_LIQUID_IMPACT_PIERCING_DRILL;
        cfg->renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
        cfg->liquidProfile = LIQUID_MEDIUM_LIQUID_METAL;
        cfg->particleCount = 384;
        cfg->emissionRadius = 1.8f;
        cfg->emissionInnerRadius = 0.6f;
        cfg->coneAngleDeg = 25.0f;
        cfg->dropletRadius = 0.13f;
        cfg->blobVariation = 0.22f;
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.001f;
        cfg->travelSpeed = 26.0f;
        cfg->arcHeight = 1.6f;
        cfg->arcVariation = 0.05f;
        cfg->guideRadius = 0.8f;
        cfg->pullForceN = 9.0f;
        cfg->forwardForceN = 3.0f;
        cfg->swirlSpeed = 10.0f;
        cfg->turbulenceSpeed = 1.2f;
        cfg->fluidDrag = 0.4f;
        cfg->cohesionScale = 2.0f;
        cfg->emitMistTrail = false;
        cfg->splashDropletCount = 10;
        cfg->splashVelocity = 2.5f;
        cfg->puddleRadius = 1.4f;
        cfg->puddleDuration = 4.5f;
        cfg->impactCrown = false;
        cfg->impactShockRing = true;
        cfg->impactDecal = true;
        cfg->impactMistRadius = 0.8f;
        cfg->impactShake = 0.30f;
        cfg->windBlastRadius = 2.4f;
        cfg->liquidBodyColor = (Color){168, 176, 190, 250};
        cfg->liquidGlowColor = (Color){236, 240, 255, 255};
        cfg->liquidFoamColor = (Color){255, 255, 255, 255};
        cfg->threshold = 0.56f;
        break;

    case VFX_LIQUID_PRESET_WHIP_LASH:
        /* Thủy Tiên Lash: a thin serpentine lash, not a ball. Small emission
         * radius + strong swirl + high turbulence makes the mass shear into a
         * travelling S-curve, and the qi ribbon traces where it has been. */
        cfg->spawnMode = VFX_LIQUID_SPAWN_BURST;
        cfg->emissionShape = VFX_LIQUID_SHAPE_HELIX;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SERPENTINE_WAVE;
        cfg->impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
        cfg->renderBackend = VFX_LIQUID_BACKEND_METABALL3D;
        cfg->particleCount = 384;
        cfg->emissionRadius = 0.22f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 14.0f;
        cfg->dropletRadius = 0.10f;
        cfg->blobVariation = 0.30f;
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.002f;
        cfg->travelSpeed = 24.0f;
        cfg->arcHeight = 1.1f;
        cfg->arcVariation = 0.10f;
        cfg->guideRadius = 0.55f;
        cfg->pullForceN = 8.0f;
        cfg->forwardForceN = 3.2f;
        cfg->swirlSpeed = 18.0f;
        cfg->turbulenceSpeed = 4.5f;
        cfg->fluidDrag = 0.9f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = true;
        cfg->ribbonWidth = 0.055f;
        cfg->splashDropletCount = 24;
        cfg->splashVelocity = 5.5f;
        cfg->puddleRadius = 1.2f;
        cfg->puddleDuration = 2.2f;
        cfg->impactCrown = true;
        cfg->impactShockRing = true;
        cfg->impactMistRadius = 1.0f;
        cfg->impactShake = 0.10f;
        cfg->windBlastRadius = 2.2f;
        cfg->liquidBodyColor = (Color){26, 122, 190, 232};
        cfg->liquidGlowColor = (Color){186, 240, 255, 255};
        cfg->liquidFoamColor = (Color){245, 252, 255, 255};
        cfg->threshold = 0.52f;
        break;

    case VFX_LIQUID_PRESET_RAIN_LANCE:
        /* Xuyên Vân Thương: a piercing lance. Tight cone, almost no arc, high
         * speed and a small coherent body — the impact is a needle, not a
         * splash, so the crown is off and the mist carries the energy. */
        cfg->spawnMode = VFX_LIQUID_SPAWN_CONE_SPRAY;
        cfg->emissionShape = VFX_LIQUID_SHAPE_CYLINDER;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_GUIDED_HOMING;
        cfg->impactMode = VFX_LIQUID_IMPACT_PIERCING_DRILL;
        cfg->renderBackend = VFX_LIQUID_BACKEND_SSF;
        cfg->particleCount = 448;
        cfg->emissionRadius = 0.16f;
        cfg->emissionInnerRadius = 0.0f;
        cfg->coneAngleDeg = 7.0f;
        cfg->dropletRadius = 0.085f;
        cfg->blobVariation = 0.18f;
        cfg->emergeDuration = 0.0f;
        cfg->hoverDuration = 0.0f;
        cfg->launchStaggerDelay = 0.0f;
        cfg->travelSpeed = 34.0f;
        cfg->arcHeight = 0.35f;
        cfg->arcVariation = 0.03f;
        cfg->guideRadius = 0.45f;
        cfg->pullForceN = 9.5f;
        cfg->forwardForceN = 4.5f;
        cfg->swirlSpeed = 6.0f;
        cfg->turbulenceSpeed = 1.0f;
        cfg->fluidDrag = 0.5f;
        cfg->cohesionScale = 1.6f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = false;
        cfg->splashDropletCount = 14;
        cfg->splashVelocity = 9.0f;
        cfg->puddleRadius = 1.0f;
        cfg->puddleDuration = 1.8f;
        cfg->impactCrown = false;
        cfg->impactShockRing = true;
        cfg->impactMistRadius = 1.4f;
        cfg->impactShake = 0.18f;
        cfg->windBlastRadius = 2.6f;
        cfg->liquidBodyColor = (Color){16, 108, 190, 240};
        cfg->liquidGlowColor = (Color){200, 246, 255, 255};
        cfg->liquidFoamColor = (Color){255, 255, 255, 255};
        cfg->threshold = 0.50f;
        break;

    case VFX_LIQUID_PRESET_TIDAL_CRESCENT:
        /* Hải Nguyệt Trảm: a crescent wave blade. Wide ring emission collapsed
         * into a low, fast surge that spreads into the largest puddle of the
         * set; no crown, because the wave arrives as a wall of water. */
        cfg->spawnMode = VFX_LIQUID_SPAWN_SURROUND_RING;
        cfg->emissionShape = VFX_LIQUID_SHAPE_RING;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_LIQUID_IMPACT_DELUGE_CASCADE;
        cfg->renderBackend = VFX_LIQUID_BACKEND_HYBRID;
        cfg->particleCount = 1024;
        cfg->emissionRadius = 1.7f;
        cfg->emissionInnerRadius = 1.0f;
        cfg->coneAngleDeg = 30.0f;
        cfg->dropletRadius = 0.13f;
        cfg->blobVariation = 0.35f;
        cfg->emergeDuration = 0.12f;
        cfg->hoverDuration = 0.10f;
        cfg->launchStaggerDelay = 0.0f;
        cfg->travelSpeed = 18.0f;
        cfg->arcHeight = 1.3f;
        cfg->arcVariation = 0.12f;
        cfg->guideRadius = 1.9f;
        cfg->pullForceN = 6.0f;
        cfg->forwardForceN = 2.6f;
        cfg->swirlSpeed = 9.0f;
        cfg->turbulenceSpeed = 2.0f;
        cfg->fluidDrag = 1.0f;
        cfg->emitMistTrail = true;
        cfg->emitQiRibbon = true;
        cfg->ribbonWidth = 0.14f;
        cfg->splashDropletCount = 40;
        cfg->splashVelocity = 6.5f;
        cfg->puddleRadius = 4.6f;
        cfg->puddleDuration = 5.5f;
        cfg->impactCrown = false;
        cfg->impactShockRing = true;
        cfg->impactMistRadius = 3.2f;
        cfg->impactShake = 0.28f;
        cfg->windBlastRadius = 6.0f;
        cfg->liquidBodyColor = (Color){14, 96, 168, 226};
        cfg->liquidGlowColor = (Color){164, 232, 255, 255};
        cfg->liquidFoamColor = (Color){240, 250, 255, 255};
        cfg->threshold = 0.50f;
        break;

    case VFX_LIQUID_PRESET_CUSTOM:
    default:
        break;
    }

    cfg->wetRadius = cfg->puddleRadius > 0.1f ? cfg->puddleRadius : 1.8f;
    cfg->wetDuration = cfg->puddleDuration > 0.5f ? cfg->puddleDuration : 3.5f;
    cfg->puddleRadius = cfg->wetRadius;
    cfg->puddleDuration = cfg->wetDuration;
    cfg->impactDecal = false;
}

// ── Naming Helpers ───────────────────────────────────────────────────────────

const char *VFX_GuidedLiquidPreset_Name(VFX_GuidedLiquidPreset preset)
{
    static const char *const names[VFX_LIQUID_PRESET_COUNT] = {
        "Custom", "Water Orb", "Torrent Jet", "Vortex Dragon",
        "Orbital Deluge", "Spring Gush", "Surging Tide",
        "Molten Geyser", "Mercurial Orb", "Whip Lash", "Rain Lance",
        "Tidal Crescent"
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

    static const char *const presetNames[VFX_LIQUID_PRESET_COUNT] = {
        "Custom", "Water Orb", "Torrent Jet", "Vortex Dragon",
        "Orbital Deluge", "Spring Gush", "Surging Tide",
        "Molten Geyser", "Mercurial Orb"
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
    LIQUID_PARAM_ENUM("Liquid Profile", "Preset", cfg->liquidProfile, profileNames, LIQUID_MEDIUM_COUNT);

    // Phase 1: Emission & Shape
    LIQUID_PARAM_ENUM("Spawn Mode", "Emission", cfg->spawnMode, spawnNames, VFX_LIQUID_SPAWN_COUNT);
    LIQUID_PARAM_ENUM("Emission Shape", "Emission", cfg->emissionShape, shapeNames, VFX_LIQUID_SHAPE_COUNT);
    LIQUID_PARAM_INT("Droplet Count", "Emission", cfg->particleCount, 8, VC_GUIDED_LIQUID_MAX_DROPLETS);
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
    LIQUID_PARAM_FLOAT("Swirl rad/s", "Forces", cfg->swirlSpeed, 0.0f, 25.0f, 1.0f);
    LIQUID_PARAM_FLOAT("Turbulence m/s", "Forces", cfg->turbulenceSpeed, 0.0f, 8.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Gravity Scale", "Forces", cfg->gravityScale, 0.0f, 2.5f, 0.1f);
    LIQUID_PARAM_BOOL("Emit Mist Trail", "Trajectory", cfg->emitMistTrail);
    LIQUID_PARAM_BOOL("Emit Qi Ribbon", "Trajectory", cfg->emitQiRibbon);

    // Physical medium overrides (0 = measured value for the profile)
    LIQUID_PARAM_FLOAT("Density kg/m3", "Medium", cfg->mediumDensityKgM3, 0.0f, 14000.0f, 50.0f);
    LIQUID_PARAM_FLOAT("Viscosity Pa.s", "Medium", cfg->mediumViscosityPaS, 0.0f, 200.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Surface Tension N/m", "Medium", cfg->mediumSurfaceTensionNm, 0.0f, 1.0f, 0.01f);
    LIQUID_PARAM_FLOAT("Cohesion Scale", "Medium", cfg->cohesionScale, 0.2f, 3.0f, 0.05f);

    // Phase 3: Impact & Interaction (Expanding Wet Moisture replaces Decal and Puddle)
    LIQUID_PARAM_ENUM("Impact Mode", "Impact", cfg->impactMode, impactNames, VFX_LIQUID_IMPACT_COUNT);
    LIQUID_PARAM_INT("Splash Droplets", "Impact", cfg->splashDropletCount, 0, 128);
    LIQUID_PARAM_FLOAT("Splash Speed m/s", "Impact", cfg->splashVelocity, 1.0f, 15.0f, 0.5f);
    LIQUID_PARAM_FLOAT("Wet Radius m", "Impact", cfg->wetRadius, 0.0f, 6.0f, 0.2f);
    LIQUID_PARAM_FLOAT("Wet Duration s", "Impact", cfg->wetDuration, 0.5f, 8.0f, 0.5f);
    LIQUID_PARAM_BOOL("Impact Crown", "Impact", cfg->impactCrown);
    LIQUID_PARAM_BOOL("Shock Ring", "Impact", cfg->impactShockRing);
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

// ── Emission Geometry ────────────────────────────────────────────────────────
//
// Every shape samples its OWN volume uniformly (or its own surface, for the
// planar shapes). The previous power-law radial spread packed half the droplets
// into a shell and left the core empty, which is why a 2,048-droplet body
// rendered as a hollow cage.

static Vector3 GuidedLiquid_EmissionOffset(const VFX_GuidedLiquidConfig *cfg, int index, int count,
                                           unsigned int *seed, const VC_LiquidGuideFrame *frame)
{
    if (count <= 0) return Vector3Zero();
    const float golden = 2.39996323f; /* golden angle, low-discrepancy azimuth */
    float u = ((float)index + 0.5f) / (float)count;
    float radius = fmaxf(0.01f, cfg->emissionRadius);
    float inner = Clamp(cfg->emissionInnerRadius, 0.0f, radius);

    switch (cfg->emissionShape)
    {
    case VFX_LIQUID_SHAPE_RING: {
        /* Solid torus: major radius between inner and outer, tube filled by
         * area so the ring reads as a volume instead of a wire. */
        float major = 0.5f * (radius + inner);
        float minor = fmaxf(0.02f, 0.5f * (radius - inner));
        float angle = u * 2.0f * PI;
        float tubeAngle = GuidedLiquid_RandomFloat(seed) * 2.0f * PI;
        float tubeR = minor * sqrtf(GuidedLiquid_RandomFloat(seed));
        Vector3 radial = {cosf(angle), 0.0f, sinf(angle)};
        Vector3 up = {0.0f, 1.0f, 0.0f};
        return Vector3Add(Vector3Scale(radial, major + cosf(tubeAngle) * tubeR),
                          Vector3Scale(up, sinf(tubeAngle) * tubeR));
    }
    case VFX_LIQUID_SHAPE_CYLINDER: {
        float r = radius * sqrtf(u);
        float angle = u * golden * 6.0f;
        float halfHeight = radius * 1.25f;
        float y = halfHeight * (2.0f * GuidedLiquid_RandomFloat(seed) - 1.0f);
        return (Vector3){cosf(angle) * r, y, sinf(angle) * r};
    }
    case VFX_LIQUID_SHAPE_HELIX: {
        int strand = index & 1;
        float angle = u * 6.0f * PI + (float)strand * PI;
        float tube = radius * 0.16f * sqrtf(GuidedLiquid_RandomFloat(seed));
        float h = radius * 3.5f * (u - 0.5f);
        Vector3 radial = {cosf(angle), 0.0f, sinf(angle)};
        Vector3 tangent = {-sinf(angle), 0.0f, cosf(angle)};
        return Vector3Add(Vector3Scale(radial, radius * 0.85f + tube),
                          Vector3Add(Vector3Scale(tangent, tube * 0.5f), (Vector3){0.0f, h, 0.0f}));
    }
    case VFX_LIQUID_SHAPE_DISC: {
        /* Annulus: uniform area between inner and outer radius. */
        float r2 = inner * inner + (radius * radius - inner * inner) * u;
        float angle = u * golden * 8.0f;
        float r = sqrtf(fmaxf(0.0f, r2));
        return (Vector3){cosf(angle) * r, 0.0f, sinf(angle) * r};
    }
    case VFX_LIQUID_SHAPE_SPHERE:
    default: {
        /* Uniform inside the sphere: r = R * cbrt(u) is what makes the density
         * constant. Using u^(0.4) instead put 60% of the droplets in the outer
         * third and left the interior empty. */
        float cosTheta = 1.0f - 2.0f * u;
        float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
        float azimuth = u * golden * 4.0f;
        float r = radius * cbrtf(fmaxf(0.0f, GuidedLiquid_RandomFloat(seed)));
        Vector3 offset = {sinTheta * cosf(azimuth) * r, cosTheta * r, sinTheta * sinf(azimuth) * r};
        (void)frame;
        return offset;
    }
    }
}

/* Cone spray: uniform solid angle inside the authored half-angle, around the
 * launch direction. This is the mode that made `coneAngleDeg` meaningful. */
static Vector3 GuidedLiquid_ConeOffset(const VFX_GuidedLiquidConfig *cfg, int index, int count,
                                       unsigned int *seed, const VC_LiquidGuideFrame *frame)
{
    float halfAngle = Clamp(cfg->coneAngleDeg, 1.0f, 89.0f) * (PI / 180.0f);
    float cosMax = cosf(halfAngle);
    float u = ((float)index + 0.5f) / (float)fmaxf(1, count);
    float cosTheta = 1.0f - (1.0f - cosMax) * u;
    float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
    float azimuth = GuidedLiquid_RandomFloat(seed) * 2.0f * PI;
    float r = fmaxf(0.02f, cfg->emissionRadius);
    Vector3 dir = Vector3Add(Vector3Scale(frame->tangent, cosTheta),
                             Vector3Add(Vector3Scale(frame->right, sinTheta * cosf(azimuth)),
                                        Vector3Scale(frame->up, sinTheta * sinf(azimuth))));
    return Vector3Scale(dir, r);
}

// ── Guide Frame ───────────────────────────────���──────────────────────────────

static void GuidedLiquid_BuildBasis(Vector3 tangent, VC_LiquidGuideFrame *frame)
{
    Vector3 t = tangent;
    if (!isfinite(t.x) || !isfinite(t.y) || !isfinite(t.z) || Vector3Length(t) < 1e-4f) {
        t = (Vector3){0.0f, 0.0f, 1.0f};
    } else {
        t = Vector3Normalize(t);
    }
    Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3CrossProduct(worldUp, t);
    if (Vector3Length(right) < 1e-4f) right = (Vector3){1.0f, 0.0f, 0.0f};
    right = Vector3Normalize(right);
    Vector3 up = Vector3Normalize(Vector3CrossProduct(t, right));
    frame->right = right;
    frame->up = up;
    frame->tangent = t;
}

/* Exact target point of a droplet's lane at guide time `t`. The lane itself
 * rotates: the binding actuator makes the body spin at `swirlSpeed`, which is
 * a real circular motion (the target's own velocity is omega x offset), not a
 * decorative sine. */
static Vector3 GuidedLiquid_LaneTarget(const VC_GuidedLiquidInstance *inst,
                                       const VC_LiquidDroplet *d,
                                       const VC_LiquidGuideFrame *frame, float timeSec,
                                       Vector3 *outLaneVelocity)
{
    /* Squash/stretch the body along its direction of travel by the ratio of
     * external acceleration to the binding stiffness. A liquid in an
     * accelerating field is not a rigid lattice. */
    float omega2 = inst->bindOmega * inst->bindOmega;
    float strain = 0.0f;
    if (omega2 > 1e-3f) {
        float aExt = Vector3Length(inst->frame.acc);
        strain = Clamp(aExt / omega2 / fmaxf(0.05f, inst->config.guideRadius), 0.0f, 0.55f);
    }

    float swirl = inst->config.swirlSpeed;
    /* The swirl clock starts at EMISSION, not at launch: a forming body that is
     * held for emerge+hover must still be churning, otherwise the whole
     * pre-launch phase is a freeze-frame. Callers pass the instance clock so the
     * lane angle is continuous across the emergence -> hover -> flight handover
     * (clamping to 0 here snapped every lane back by `swirl * launchDelay`). */
    float angle = swirl * timeSec;
    Vector3 lane = d->laneOffset;
    if (fabsf(angle) > 1e-5f && Vector3LengthSqr(lane) > 1e-8f) {
        lane = Vector3RotateByAxisAngle(lane, frame->tangent, angle);
    }

    float along = Vector3DotProduct(lane, frame->tangent);
    float transverseScale = 1.0f - strain * 0.45f;
    Vector3 offset = Vector3Add(
        Vector3Scale(frame->tangent, along * (1.0f + strain)),
        Vector3Scale(Vector3Subtract(lane, Vector3Scale(frame->tangent, along)),
                     transverseScale));

    if (outLaneVelocity) {
        /* d/dt of the lane in the moving frame: rigid rotation about the
         * body's own axis. */
        *outLaneVelocity = swirl != 0.0f
                               ? Vector3CrossProduct(Vector3Scale(frame->tangent, swirl),
                                                     Vector3Scale(offset, transverseScale))
                               : Vector3Zero();
    }

    /* Physical turbulence perturbation: turbulence speed drives divergence-free
     * 3D sinusoidal eddies around each droplet's assigned lane. */
    if (inst->config.turbulenceSpeed > 0.01f) {
        float turbAmp = fminf(inst->config.turbulenceSpeed * 0.075f, inst->config.guideRadius * 0.40f);
        float pX = timeSec * 3.7f + (float)(d->seed & 255) * 0.0245f;
        float pY = timeSec * 4.3f + (float)((d->seed >> 8) & 255) * 0.0245f;
        float pZ = timeSec * 3.1f + (float)((d->seed >> 16) & 255) * 0.0245f;
        Vector3 turbDisp = {
            sinf(pX) * turbAmp,
            cosf(pY) * turbAmp * 0.8f,
            sinf(pZ) * turbAmp
        };
        offset = Vector3Add(offset, turbDisp);
        if (outLaneVelocity) {
            Vector3 turbVel = {
                cosf(pX) * turbAmp * 3.7f,
                -sinf(pY) * turbAmp * 4.3f * 0.8f,
                cosf(pZ) * turbAmp * 3.1f
            };
            *outLaneVelocity = Vector3Add(*outLaneVelocity, turbVel);
        }
    }

    return Vector3Add(frame->pos, offset);
}

static void GuidedLiquid_SampleGuideFrame(VC_GuidedLiquidInstance *inst, float t)
{
    const VFX_GuidedLiquidConfig *cfg = &inst->config;
    VC_LiquidGuideFrame *out = &inst->frame;

    Vector3 gravity = {0.0f, -LIQUID_GRAVITY_MPS2 * cfg->gravityScale, 0.0f};
    if (!inst->launchSolved) {
        out->pos = Vector3Add(cfg->source, Vector3Scale(gravity, 0.5f * t * t));
        out->vel = Vector3Scale(gravity, t);
        out->acc = gravity;
        GuidedLiquid_BuildBasis(Vector3Subtract(inst->curTarget, cfg->source), out);
        return;
    }

    float duration = fmaxf(0.05f, inst->launch.durationSec);
    float tc = Clamp(t, 0.0f, duration);
    LiquidBallistic_Sample(&inst->launch, tc, &out->pos, &out->vel, &out->acc);
    if (t > duration) {
        /* Arrived: the frame stops. Below it the body is handed to the
         * cascade, so holding the frame is what makes the break readable. */
        LiquidBallistic_Sample(&inst->launch, duration, &out->pos, NULL, NULL);
        out->vel = Vector3Zero();
        out->acc = Vector3Zero();
    }

    Vector3 baseTangent = inst->launch.horizontalDir;
    if (Vector3Length(out->vel) > 0.1f) baseTangent = out->vel;
    GuidedLiquid_BuildBasis(baseTangent, out);

    /* Helical steering: a real circular motion (centripetal acceleration
     * omega^2 * r) whose radius grows in and out with the flight. */
    if (cfg->trajectoryMode == VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW) {
        Vector3 baseTangentFlat = inst->launch.horizontalDir;
        Vector3 right = {baseTangentFlat.z, 0.0f, -baseTangentFlat.x};
        Vector3 up = {0.0f, 1.0f, 0.0f};
        float omega = Clamp(cfg->swirlSpeed, 0.0f, 25.0f);
        float helixR = Clamp(cfg->guideRadius * 0.35f, 0.10f, 1.2f);
        float theta = omega * tc;
        float r = helixR * sinf(PI * tc / duration);
        float dr = helixR * (PI / duration) * cosf(PI * tc / duration);
        float ddr = -helixR * (PI / duration) * (PI / duration) * sinf(PI * tc / duration);
        Vector3 e = Vector3Add(Vector3Scale(right, cosf(theta)), Vector3Scale(up, sinf(theta)));
        Vector3 de = Vector3Add(Vector3Scale(right, -sinf(theta)), Vector3Scale(up, cosf(theta)));
        out->pos = Vector3Add(out->pos, Vector3Scale(e, r));
        out->vel = Vector3Add(out->vel,
                              Vector3Add(Vector3Scale(e, dr), Vector3Scale(de, r * omega)));
        out->acc = Vector3Add(out->acc,
                              Vector3Add(Vector3Scale(e, ddr - r * omega * omega),
                                         Vector3Scale(de, 2.0f * dr * omega)));
    }

    /* Serpentine steering: transverse harmonic motion along the line of
     * flight. Its acceleration is -A*Omega^2*sin(), i.e. a real centripetal
     * oscillation, not a phase-shifted copy of the position. */
    if (cfg->trajectoryMode == VFX_LIQUID_TRAJ_SERPENTINE_WAVE) {
        Vector3 baseTangentFlat = inst->launch.horizontalDir;
        Vector3 right = {baseTangentFlat.z, 0.0f, -baseTangentFlat.x};
        float amplitude = fmaxf(0.15f, cfg->arcHeight * 0.35f);
        float wavelength = fmaxf(0.6f, (inst->launch.horizontalSpeedMps * duration) / 2.5f);
        float omega = fmaxf(0.8f, Clamp(cfg->swirlSpeed, 0.0f, 25.0f) * 0.6f);
        float rate = (2.0f * PI / wavelength) * inst->launch.horizontalSpeedMps - omega;
        float phase = rate * tc;
        out->pos = Vector3Add(out->pos, Vector3Scale(right, amplitude * sinf(phase)));
        out->vel = Vector3Add(out->vel, Vector3Scale(right, amplitude * rate * cosf(phase)));
        out->acc = Vector3Add(out->acc,
                              Vector3Scale(right, -amplitude * rate * rate * sinf(phase)));
    }
}

/* Guided homing integrates a controlled body: thrust towards the target with
 * critical damping plus real gravity, bounded by its acceleration budget. */
static void GuidedLiquid_IntegrateHoming(VC_GuidedLiquidInstance *inst, float dt)
{
    const VFX_GuidedLiquidConfig *cfg = &inst->config;
    Vector3 gravity = {0.0f, -LIQUID_GRAVITY_MPS2 * cfg->gravityScale, 0.0f};
    float flight = fmaxf(0.25f, inst->launchSolved ? inst->launch.durationSec : 0.8f);
    float omega = Clamp(6.0f / (0.6f * flight), 1.5f, 12.0f);
    Vector3 toTarget = Vector3Subtract(inst->curTarget, inst->frame.pos);
    Vector3 acc = Vector3Add(Vector3Scale(toTarget, omega * omega),
                             Vector3Scale(inst->frame.vel, -2.0f * omega));
    acc = Vector3Add(acc, gravity);
    float budget = fmaxf(4.0f * LIQUID_GRAVITY_MPS2, cfg->travelSpeed * 3.0f);
    float len = Vector3Length(acc);
    if (len > budget) acc = Vector3Scale(acc, budget / len);

    inst->frame.acc = acc;
    inst->frame.vel = Vector3Add(inst->frame.vel, Vector3Scale(acc, dt));
    inst->frame.pos = Vector3Add(inst->frame.pos, Vector3Scale(inst->frame.vel, dt));
    GuidedLiquid_BuildBasis(inst->frame.vel, &inst->frame);

    if (Vector3Distance(inst->frame.pos, inst->curTarget) < 0.25f) {
        inst->frame.vel = Vector3Scale(inst->frame.vel, 0.4f);
    }
}

// ── Impact & Settlement ──────────────────────────────────────────────────────

/* Element material for the aftermath. A lava or mud body must not leave a blue
 * water shock ring and a wet decal behind. */
static VC_MaterialId GuidedLiquid_ImpactMaterial(int liquidProfile)
{
    switch (liquidProfile) {
    case LIQUID_MEDIUM_LAVA:         return VC_MAT_FIRE;
    case LIQUID_MEDIUM_MUD:          return VC_MAT_EARTH;
    case LIQUID_MEDIUM_POISON:       return VC_MAT_POISON;
    case LIQUID_MEDIUM_LIQUID_METAL: return VC_MAT_METAL;
    case LIQUID_MEDIUM_WATER:
    default:                         return VC_MAT_WATER;
    }
}

static void GuidedLiquid_TriggerImpact(VC_GuidedLiquidInstance *inst, Vector3 point, Vector3 normal,
                                       Vector3 incomingVelocity)
{
    if (!inst || inst->impactTriggered) return;
    const VFX_GuidedLiquidConfig *cfg = &inst->config;

    inst->impactTriggered = true;
    inst->impactPos = point;
    inst->impactNormal = Vector3Length(normal) > 1e-4f ? Vector3Normalize(normal) : (Vector3){0, 1, 0};
    inst->impactVelocity = incomingVelocity;
    inst->impactSpeed = Vector3Length(incomingVelocity);
    inst->impactTimer = 0.0f;
    inst->splashSpawned = 0;

    float groundY = MapManager_GetGroundHeightAt(point.x, point.z);
    inst->groundWetPos = (Vector3){point.x, groundY + 0.008f, point.z};
    inst->groundPuddlePos = inst->groundWetPos;

    float desiredWetRadius = cfg->wetRadius > 0.1f ? cfg->wetRadius : (cfg->puddleRadius > 0.1f ? cfg->puddleRadius : fmaxf(1.8f, inst->bodyRadiusM * 2.2f));
    float desiredWetDuration = cfg->wetDuration > 0.5f ? cfg->wetDuration : (cfg->puddleDuration > 0.5f ? cfg->puddleDuration : 3.5f);

    inst->wetRadius = fmaxf(0.12f, inst->bodyRadiusM * 0.40f);
    inst->wetMaxRadius = desiredWetRadius;
    inst->wetTimer = 0.0f;
    inst->wetDuration = desiredWetDuration;
    inst->wetVolumeM3 = 0.0f;
    inst->wetThickness = fmaxf(0.008f, fminf(0.04f, inst->bodyRadiusM * 0.15f));

    inst->puddleRadius = inst->wetRadius;
    inst->puddleMaxRadius = inst->wetMaxRadius;
    inst->puddleTimer = 0.0f;
    inst->puddleDuration = inst->wetDuration;
    inst->puddleVolumeM3 = 0.0f;
    inst->puddleThickness = inst->wetThickness;

    // Environmental wind shockwave (pressure front, then turbulence wake)
    if (cfg->windBlastRadius > 0.05f) {
        float blastSpeed = Clamp(inst->impactSpeed * 0.75f, 4.0f, 14.0f);
        Wind_SpawnRadialBlast(inst->groundWetPos, cfg->windBlastRadius, blastSpeed, 0.45f);
        Wind_SpawnTurbulence(inst->groundWetPos, cfg->windBlastRadius * 0.75f, 6.0f, 0.25f, 3.5f, 0.35f);
    }

    if (cfg->impactShake > 0.01f) {
        CameraFX_Shake(cfg->impactShake);
    }

    VC_MaterialId impactMat = GuidedLiquid_ImpactMaterial(cfg->liquidProfile);

    if (cfg->impactShockRing) {
        float ringRadius = fmaxf(cfg->dropletRadius * 4.2f, inst->bodyRadiusM * 1.5f);
        VFX_ComposeGroundWave(inst->groundWetPos, impactMat, ringRadius, 0.45f,
                              VFX_GroundHeightFromMap, NULL);
    }

    // Ground decals removed: replaced by ground-conforming expanding wet moisture mesh.

    if (cfg->impactMistRadius > 0.15f) {
        VFX_ComposeMistVeilEx(inst->groundWetPos, impactMat,
                              cfg->impactMistRadius, 0.55f + 0.35f * inst->wetDuration * 0.1f);
    }

    /* Hand the real incoming velocity to the engine's liquid impact: it owns
     * normal reflection, tangential retention and the crown volume. The
     * composition keeps rendering its own body, so this is an external body. */
    LiquidImpactEvent event = {0};
    event.hitPoint = point;
    event.hitNormal = inst->impactNormal;
    event.impulseDirection = Vector3Length(incomingVelocity) > 0.1f
                                 ? Vector3Normalize(incomingVelocity)
                                 : inst->impactNormal;
    event.initialVelocity = incomingVelocity;
    event.force01 = Clamp(inst->impactSpeed / 22.0f, 0.05f, 1.0f);
    event.scale = Clamp(inst->bodyRadiusM * 1.4f, 0.4f, 2.6f);
    event.bodyColor = inst->material.body;
    event.glowColor = inst->material.glow;
    event.softColor = inst->material.soft;
    event.motionProfile = (LiquidMotionProfile)Clamp(inst->config.liquidProfile, 0,
                                                     LIQUID_MEDIUM_COUNT - 1);
    event.forceFieldOnly = true;
    event.externalBody = true;
    event.backend = LIQUID_IMPACT_BACKEND_FORCE_FIELD;
    LiquidImpact_SpawnWater(&event);
}

bool VFX_GuidedLiquid_IsAnyActive(void)
{
    for (int i = 0; i < VC_GUIDED_LIQUID_MAX_INSTANCES; i++) {
        if (s_guidedLiquidInstances[i].active) return true;
    }
    return false;
}

// ── Public Composition Entry Points ──────────────────────────────────────────

static bool GuidedLiquid_IsJetLike(const VFX_GuidedLiquidConfig *cfg)
{
    return cfg->spawnMode == VFX_LIQUID_SPAWN_STREAM ||
           cfg->spawnMode == VFX_LIQUID_SPAWN_CONE_SPRAY ||
           cfg->trajectoryMode == VFX_LIQUID_TRAJ_JET_STREAM ||
           cfg->launchStaggerDelay > 0.0005f;
}

VFX_GuidedLiquidConfig VFX_GuidedLiquid_PresetConfig(VFX_GuidedLiquidPreset preset)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_DefaultConfig();
    VFX_GuidedLiquid_ApplyPreset(&cfg, preset);
    return cfg;
}

/* Uniform spatial scale for skill integration.
 *
 * Every length in the config scales linearly, so the shape of the motion is
 * preserved: a 2x lash is the same lash, twice as wide and twice as tall. The
 * speed scales with sqrt(scale) so the flight time grows only as sqrt(scale)
 * instead of linearly — a 4x boulder of water does not take 4x as long to
 * arrive, which would read as slow motion rather than as a bigger attack. */
static void GuidedLiquid_ScaleConfig(VFX_GuidedLiquidConfig *cfg, float scale)
{
    if (!cfg || scale <= 0.0f) return;
    if (fabsf(scale - 1.0f) < 1e-4f) return;

    cfg->emissionRadius *= scale;
    cfg->emissionInnerRadius *= scale;
    cfg->dropletRadius *= scale;
    cfg->guideRadius *= scale;
    cfg->arcHeight *= scale;
    cfg->ribbonWidth *= scale;
    cfg->wetRadius *= scale;
    cfg->puddleRadius *= scale;
    cfg->impactMistRadius *= scale;
    cfg->windBlastRadius *= scale;
    cfg->splashVelocity *= sqrtf(scale);
    cfg->travelSpeed *= sqrtf(scale);
    cfg->particleCount = (int)Clamp((float)cfg->particleCount * scale, 32.0f,
                                    (float)VC_GUIDED_LIQUID_MAX_DROPLETS);
}

int VFX_ComposeGuidedLiquidPreset(Vector3 source, Vector3 target, VFX_GuidedLiquidPreset preset)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_PresetConfig(preset);
    return VFX_ComposeGuidedLiquidEx(source, target, &cfg);
}

int VFX_ComposeGuidedLiquidScaled(Vector3 source, Vector3 target, VFX_GuidedLiquidPreset preset,
                                  float scale)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_PresetConfig(preset);
    GuidedLiquid_ScaleConfig(&cfg, scale);
    return VFX_ComposeGuidedLiquidEx(source, target, &cfg);
}

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

    // Find an available instance slot
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
        instIdx = 0;
        inst = &s_guidedLiquidInstances[0];
        if (inst->fieldAlive) {
            MotionFields_Stop(inst->mediumField);
            inst->fieldAlive = false;
        }
    }

    memset(inst, 0, sizeof(VC_GuidedLiquidInstance));
    (void)instIdx;
    inst->active = true;
    inst->id = s_guidedLiquidNextId++;
    if (s_guidedLiquidNextId <= 0) s_guidedLiquidNextId = 1;

    inst->config = cfg ? *cfg : *VFX_GuidedLiquid_GetLiveConfig();
    inst->config.source = source;
    inst->config.target = target;
    inst->curTarget = target;
    inst->medium = GuidedLiquid_ResolveMedium(&inst->config);

    // Optical material description
    int profileIndex = Clamp(inst->config.liquidProfile, 0, LIQUID_MEDIUM_COUNT - 1);
    inst->material = LiquidSurface_ProfileDesc((LiquidMotionProfile)profileIndex);
    inst->material.body = inst->config.liquidBodyColor;
    inst->material.glow = inst->config.liquidGlowColor;
    inst->material.soft = inst->config.liquidFoamColor;
    inst->material.ior = inst->config.ior;
    inst->material.foam = inst->config.foamAmount;
    if (inst->config.specularPower > 1.0f) {
        inst->material.roughnessScale = Clamp(sqrtf(2.0f / (inst->config.specularPower + 2.0f)) * 2.5f, 0.05f, 2.0f);
    }

    // ── Exact launch solution ────────────────────────────────────────────────
    float gravity = LIQUID_GRAVITY_MPS2 * fmaxf(0.05f, inst->config.gravityScale);
    float apex = inst->config.arcHeight;
    bool homing = inst->config.trajectoryMode == VFX_LIQUID_TRAJ_GUIDED_HOMING;
    switch (inst->config.trajectoryMode) {
    case VFX_LIQUID_TRAJ_PARABOLIC_ARC:
        inst->launchSolved = LiquidBallistic_SolveApex(source, target, apex, gravity, &inst->launch);
        break;
    case VFX_LIQUID_TRAJ_HIGH_CATAPULT:
        inst->launchSolved = LiquidBallistic_SolveApex(source, target, apex * 1.6f, gravity, &inst->launch);
        break;
    case VFX_LIQUID_TRAJ_JET_STREAM:
        inst->launchSolved = LiquidBallistic_SolveApex(source, target, apex, gravity, &inst->launch);
        break;
    case VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW:
        inst->launchSolved = LiquidBallistic_SolveSpeed(source, target, inst->config.travelSpeed, gravity,
                                                        &inst->launch);
        break;
    case VFX_LIQUID_TRAJ_SERPENTINE_WAVE:
    case VFX_LIQUID_TRAJ_GUIDED_HOMING:
    default:
        inst->launchSolved = LiquidBallistic_SolveSpeed(source, target, inst->config.travelSpeed, gravity,
                                                        &inst->launch);
        break;
    }
    if (!inst->launchSolved) {
        inst->launchSolved = LiquidBallistic_SolveApex(source, target, 0.6f, gravity, &inst->launch);
    }
    bool jetLike = GuidedLiquid_IsJetLike(&inst->config);
    if (jetLike) {
        inst->launchDelay = 0.0f;
    } else {
        inst->launchDelay = fmaxf(0.0f, inst->config.emergeDuration + inst->config.hoverDuration);
    }
    inst->launched = false;

    // Binding actuator gains, derived from the real body (mass-aware, bounded)
    LiquidMediumDesc *medium = &inst->medium;
    float meanRadius = fmaxf(0.02f, inst->config.dropletRadius);
    float meanMass = LiquidMedium_DropletMassKg(medium, meanRadius);
    BodyPhysicalProperties referenceBody = BodyPhysicalProperties_Sphere(
        fmaxf(meanMass, 1e-5f), fmaxf(medium->densityKgM3, 1.0f), fmaxf(medium->dragCoefficient, 0.01f));
    GuideTuning tuning = GuideTuning_Derive(
        &referenceBody, fmaxf(0.25f, inst->config.guideRadius),
        fmaxf(1.0f, inst->config.travelSpeed), inst->config.swirlSpeed,
        inst->config.turbulenceSpeed, LIQUID_GRAVITY_MPS2 * inst->config.gravityScale,
        GUIDE_BALANCED);
    float omega = tuning.settlingTimeSec > 0.0f ? 6.0f / tuning.settlingTimeSec : 12.0f;
    float cohesion = inst->config.cohesionScale > 0.0f ? inst->config.cohesionScale : 1.0f;
    inst->bindOmega = Clamp(omega, 6.0f, 45.0f) * sqrtf(Clamp(cohesion, 0.2f, 3.0f));
    inst->bindAccelBudget = fmaxf(6.0f * LIQUID_GRAVITY_MPS2,
                                  2.0f * inst->bindOmega * inst->bindOmega *
                                      fmaxf(0.1f, inst->config.guideRadius));
    inst->pressureOmega = 4.0f * inst->bindOmega;

    // ── Droplets ─────────────────────────────────────────────────────────────
    int count = inst->config.particleCount;
    if (count < 8) count = 8;
    if (count > VC_GUIDED_LIQUID_MAX_DROPLETS) count = VC_GUIDED_LIQUID_MAX_DROPLETS;
    inst->dropletCount = count;

    unsigned int rng = (unsigned int)inst->id * 1337u + 42u;
    Vector3 fwd = Vector3Subtract(target, source);
    VC_LiquidGuideFrame seedFrame = {0};
    seedFrame.pos = source;
    GuidedLiquid_BuildBasis(fwd, &seedFrame);

    /* Pass 1: placement. The emission offsets are pure geometry, so they are
     * generated before any mass exists — the emitter's own size then decides how
     * much liquid a droplet can be. */
    float offsetSqSum = 0.0f;
    for (int i = 0; i < count; i++) {
        VC_LiquidDroplet *d = &inst->droplets[i];
        memset(&d->receiver, 0, sizeof(d->receiver));
        d->seed = rng + (unsigned int)i * 101u;
        d->age = 0.0f;
        d->bounceCount = 0;
        d->wakeWindFired = false;
        d->swirlPhase = GuidedLiquid_RandomFloat(&d->seed) * 2.0f * PI;

        if (inst->config.spawnMode == VFX_LIQUID_SPAWN_CONE_SPRAY) {
            d->laneOffset = GuidedLiquid_ConeOffset(&inst->config, i, count, &d->seed, &seedFrame);
        } else {
            d->laneOffset = GuidedLiquid_EmissionOffset(&inst->config, i, count, &d->seed, &seedFrame);
        }
        if (inst->config.arcVariation > 0.005f) {
            float frac = (count > 1) ? ((float)i / (float)(count - 1) - 0.5f) * 2.0f : 0.0f;
            d->laneOffset.y += frac * inst->config.arcVariation * fmaxf(0.8f, inst->config.arcHeight * 0.45f);
        }
        offsetSqSum += Vector3LengthSqr(d->laneOffset);

        if (jetLike) {
            /* Nozzle: a continuous pressure-driven jet leaves the emitter at a
             * fixed cadence. Each droplet then flies ballistically — there is
             * no single frame that could carry a stream. */
            float nozzleR = fminf(inst->config.emissionRadius, 0.18f);
            Vector3 nozzle = {cosf(d->swirlPhase) * nozzleR * 0.5f, sinf(d->swirlPhase) * nozzleR * 0.5f, 0.0f};
            d->launchPos = Vector3Add(source, Vector3Add(
                Vector3Add(Vector3Scale(seedFrame.right, nozzle.x), Vector3Scale(seedFrame.up, nozzle.y)),
                Vector3Scale(seedFrame.tangent, -0.05f)));
            float stagger = inst->config.launchStaggerDelay > 1e-5f
                                ? inst->config.launchStaggerDelay
                                : (inst->config.emergeDuration > 0.01f
                                       ? inst->config.emergeDuration / (float)count
                                       : (inst->config.spawnMode == VFX_LIQUID_SPAWN_STREAM ? 0.004f : 0.0015f));
            d->launchDelay = (float)i * stagger;
            d->state = DROPLET_STATE_IDLE;
        } else {
            float stagger = inst->config.launchStaggerDelay;
            if (stagger > 1e-5f) {
                // Continuous / staggered release across all spawn modes
                d->launchPos = Vector3Add(source, d->laneOffset);
                d->launchDelay = (float)i * stagger;
                d->state = DROPLET_STATE_IDLE;
            } else if (inst->config.emergeDuration > 0.01f) {
                d->launchPos = Vector3Add(source, Vector3Scale(d->laneOffset, 0.05f));
                d->launchDelay = 0.0f;
                d->state = DROPLET_STATE_EMERGING;
            } else if (inst->config.hoverDuration > 0.01f) {
                d->launchPos = Vector3Add(source, d->laneOffset);
                d->launchDelay = 0.0f;
                d->state = DROPLET_STATE_HOVERING;
            } else {
                d->launchPos = Vector3Add(source, d->laneOffset);
                d->launchDelay = 0.0f;
                d->state = DROPLET_STATE_FLYING;
            }
            if (inst->config.spawnMode == VFX_LIQUID_SPAWN_GROUND_GUSH) {
                float groundY = MapManager_GetGroundHeightAt(d->launchPos.x, d->launchPos.z);
                d->launchPos.y = fminf(d->launchPos.y, groundY - 0.35f);
            }
        }
        if (inst->config.spawnMode != VFX_LIQUID_SPAWN_GROUND_GUSH) {
            float gY = MapManager_GetGroundHeightAt(d->launchPos.x, d->launchPos.z);
            if (d->launchPos.y < gY + 0.04f) {
                d->launchPos.y = gY + 0.04f;
            }
        }
        d->pos = d->launchPos;
        d->prevPos = d->pos;
        d->velocity = Vector3Zero();
    }

    /* Pass 2: incompressible size. A liquid body of `count` droplets that
     * occupies the emitter's volume cannot also keep an arbitrarily large blob
     * radius: N spheres must fit inside that volume at a monodisperse packing
     * fraction (0.64 = random close packing). The authored `dropletRadius` is
     * the ceiling, so a sparse spray keeps the authored blob, but a body packed
     * into a 0.45 m orb drops from 0.16 m blobs to ~0.05 m ones — otherwise the
     * same orb would carry 8.8 t of water (23 t/m^3) and every force derived
     * from that mass (drag, Weber breakup, contact) would be wrong by the same
     * factor that made the body blow itself apart on the ground. */
    float meanOffsetSq = offsetSqSum / (float)count;
    /* A uniform ball has <r^2> = (3/5)R^2, so its radius is r_rms/sqrt(3/5).
     * Any shape works: a spray on a cone shell, a torus and a disc all get their
     * own r_rms, so the derived volume follows the shape that was authored. */
    float emitterRadius = sqrtf(meanOffsetSq) / sqrtf(3.0f / 5.0f);
    if (!(emitterRadius > 1e-3f)) emitterRadius = 1e-3f;
    float emitterVolumeM3 = LiquidMedium_SphereVolumeM3(emitterRadius);
    const float kPackingFraction = 0.64f;
    float packedRadius = cbrtf(3.0f * kPackingFraction * emitterVolumeM3 /
                               (4.0f * PI * (float)count));
    /* Sizing: for a discrete cluster (N <= 64), keep the authored dropletRadius
     * so N=8, 16, 32 visibly produce 8, 16, 32 distinct droplets instead of
     * artificially inflating/deflating them. Only for high-density bodies (N > 64)
     * does packing fraction clamp the physical radius to conserve liquid mass. */
    float physicalRadius = inst->config.dropletRadius;
    if (count > 64 && packedRadius < physicalRadius) {
        physicalRadius = fmaxf(0.005f, packedRadius);
    }
    inst->dropletRadiusM = physicalRadius;

    /* The visual kernel is the splat of a density field. For a cohesive body
     * packed inside the emitter, it reaches ~one mean spacing so adjacent droplets
     * merge into a continuous surface. But for a dispersed emitter (large
     * emissionRadius or wide spray), droplets must NEVER inflate to bridge empty
     * space: their size is strictly bounded by the authored dropletRadius. */
    float meanSpacing = cbrtf(fmaxf(emitterVolumeM3, 1e-9f) / (float)count);
    float visualCap = fmaxf(physicalRadius, inst->config.dropletRadius) * 1.25f;
    float kernelRadius = (count <= 64) ? (physicalRadius * 1.15f)
                                       : fminf(fmaxf(physicalRadius, meanSpacing * 1.15f), visualCap);

    for (int i = 0; i < count; i++) {
        VC_LiquidDroplet *d = &inst->droplets[i];
        float var = inst->config.blobVariation;
        float rScale = (1.0f - var * 0.5f) + var * GuidedLiquid_RandomFloat(&d->seed);
        d->baseRadius = physicalRadius * rScale;
        d->radius = d->baseRadius;
        d->drawRadius = kernelRadius * rScale;
        d->volumeM3 = LiquidMedium_SphereVolumeM3(d->baseRadius);
        d->massKg = inst->medium.densityKgM3 * d->volumeM3;
    }

    // ── Motion field: the medium the body flies through ──────────────────────
    FieldDesc field = MotionField_Default();
    field.volume.coreFraction = 0.25f;
    field.receiverMask = MOTION_RECEIVER_ALL_COMPONENTS;
    field.flow.enabled = true;
    field.flow.addBackgroundVelocity = false;
    field.forceLawCount = 0;

    /* Dimension forces against the body's actual mass. GuideTuning_Derive calculates
     * the exact Newton force needed for the body's mass and speed. The authored
     * pullForceN (default 5.5) scales proportionally around the physical optimum. */
    float pullMult = inst->config.pullForceN > 0.01f ? (inst->config.pullForceN / 5.5f) : 1.0f;
    float fwdMult = inst->config.forwardForceN > 0.01f ? (inst->config.forwardForceN / 1.4f) : 1.0f;
    float pullForce = fmaxf(tuning.maxForceNewtons * pullMult, 40.0f);
    float forwardForce = fmaxf(tuning.forwardForceNewtons * fwdMult, 20.0f);
    float stiffness = fmaxf(tuning.stiffnessNPerM * pullMult, pullForce / fmaxf(0.1f, inst->config.guideRadius));

    if (jetLike) {
        field.transform = FieldTransform_Identity();
        field.transform.position = source;
        field.flow.axis = Vector3LengthSqr(fwd) > 1e-4f ? Vector3Normalize(fwd) : (Vector3){0, 0, 1};
        field.flow.procedural.swirlSpeedMps =
            Clamp(inst->config.swirlSpeed * inst->config.guideRadius, 0.0f, 40.0f);
        field.flow.procedural.turbulenceSpeedMps = fmaxf(0.0f, inst->config.turbulenceSpeed);
        field.flow.procedural.eddyLengthM = fmaxf(0.10f, inst->config.guideRadius * 0.3f);

        MotionPath path;
        Vector3 points[33];
        float dur = fmaxf(0.05f, inst->launch.durationSec);
        for (int p = 0; p < 33; p++) {
            float frac = (float)p / 32.0f;
            Vector3 pt;
            LiquidBallistic_Sample(&inst->launch, frac * dur, &pt, NULL, NULL);
            points[p] = Vector3Subtract(pt, source);
        }
        MotionPath_Build(&path, points, 33);
        field.volume.shape = FIELD_PATH_TUBE;
        field.volume.path = path;
        field.volume.radiusM = fmaxf(1.0f, inst->config.guideRadius * 1.5f);
        field.flow.followSpeedMps = inst->config.travelSpeed;
        field.preservePathLanes = true;
        field.rotatePathLanes = true;
        field.forceLaws[field.forceLawCount++] = (ForceLaw){
            .type = FORCE_LAW_PATH_GUIDE,
            .magnitudeNewtons = pullForce,
            .forwardForceNewtons = forwardForce
        };
    } else {
        field.transform = (FieldTransform){
            .position = source,
            .axisX = seedFrame.right, .axisY = seedFrame.up, .axisZ = seedFrame.tangent,
            .frameVelocityMps = {0},
            .angularVelocityRadPerSec = {seedFrame.tangent.x * inst->config.swirlSpeed,
                                         seedFrame.tangent.y * inst->config.swirlSpeed,
                                         seedFrame.tangent.z * inst->config.swirlSpeed}};
        field.flow.axis = seedFrame.tangent;
        field.flow.procedural.swirlSpeedMps =
            Clamp(inst->config.swirlSpeed * inst->config.guideRadius, 0.0f, 40.0f);
        field.flow.procedural.turbulenceSpeedMps = fmaxf(0.0f, inst->config.turbulenceSpeed);
        field.flow.procedural.eddyLengthM = fmaxf(0.10f, inst->config.guideRadius * 0.3f);

        field.volume.shape = FIELD_SPHERE;
        field.volume.radiusM = fmaxf(0.25f, inst->config.guideRadius);
        field.preserveSphereOffsets = true;
        field.forceLaws[field.forceLawCount++] = (ForceLaw){
            .type = FORCE_LAW_MOVING_GUIDE,
            .magnitudeNewtons = pullForce,
            .springStiffnessNPerM = stiffness
        };
    }

    if (inst->config.turbulenceSpeed > 0.01f) {
        float turbAcc = inst->config.turbulenceSpeed * 4.0f;
        float turbForce = fmaxf(tuning.turbulenceForceNewtons, turbAcc * meanMass * 5.0f);
        field.forceLaws[field.forceLawCount++] = (ForceLaw){
            .type = FORCE_LAW_CURL_FORCE,
            .magnitudeNewtons = turbForce,
            .procedural = {.turbulenceSpeedMps = inst->config.turbulenceSpeed,
                           .eddyLengthM = field.flow.procedural.eddyLengthM}};
    }

    float totalStagger = jetLike ? ((float)count * fmaxf(0.0002f, inst->config.launchStaggerDelay)) : 0.0f;
    if (jetLike && inst->config.emergeDuration > totalStagger) totalStagger = inst->config.emergeDuration;
    field.lifetime = (FieldLifetime){
        .startDelaySec = 0.0f,
        .durationSec = inst->launchDelay + totalStagger +
                       (inst->launchSolved ? inst->launch.durationSec : 0.8f) + 1.0f,
        .attackSec = 0.05f, .fadeSec = 0.25f};

    inst->mediumField = MotionFields_CreateField(&field);
    inst->fieldAlive = inst->mediumField != MOTION_FIELD_INVALID;
    inst->fieldRadius = field.volume.radiusM;

    if (homing) {
        inst->frame.pos = source;
        inst->frame.vel = inst->launchSolved
                              ? (Vector3){inst->launch.horizontalDir.x * inst->launch.horizontalSpeedMps,
                                          inst->launch.verticalSpeedMps,
                                          inst->launch.horizontalDir.z * inst->launch.horizontalSpeedMps}
                              : (Vector3){0, 0, 1};
        inst->frame.acc = (Vector3){0, -gravity, 0};
        GuidedLiquid_BuildBasis(inst->frame.vel, &inst->frame);
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
            if (s_guidedLiquidInstances[i].fieldAlive) {
                MotionFields_Stop(s_guidedLiquidInstances[i].mediumField);
                s_guidedLiquidInstances[i].fieldAlive = false;
            }
            s_guidedLiquidInstances[i].active = false;
            break;
        }
    }
}

// ── Neighbour Interaction (surface tension + incompressibility) ──────────────

static inline int GuidedLiquid_CellIndex(Vector3 p, Vector3 min, float invCell)
{
    int x = (int)((p.x - min.x) * invCell);
    int y = (int)((p.y - min.y) * invCell);
    int z = (int)((p.z - min.z) * invCell);
    if (x < 0) x = 0; else if (x >= VC_GUIDED_LIQUID_GRID_DIM) x = VC_GUIDED_LIQUID_GRID_DIM - 1;
    if (y < 0) y = 0; else if (y >= VC_GUIDED_LIQUID_GRID_DIM) y = VC_GUIDED_LIQUID_GRID_DIM - 1;
    if (z < 0) z = 0; else if (z >= VC_GUIDED_LIQUID_GRID_DIM) z = VC_GUIDED_LIQUID_GRID_DIM - 1;
    return (z * VC_GUIDED_LIQUID_GRID_DIM + y) * VC_GUIDED_LIQUID_GRID_DIM + x;
}

// ── Physical Simulation & State Update ───────────────────────────────────────

void VC_GuidedLiquid_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
    /* One bounded step per frame, matching the particle backend contract: the
     * spring gains below are clamped so this is stable at 30 FPS. */
    float h = fminf(dt, 1.0f / 30.0f);

    for (int instIdx = 0; instIdx < VC_GUIDED_LIQUID_MAX_INSTANCES; instIdx++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        const VFX_GuidedLiquidConfig *cfg = &inst->config;
        LiquidMediumDesc *medium = &inst->medium;
        bool jetLike = GuidedLiquid_IsJetLike(cfg);

        float launchT = inst->timer - inst->launchDelay;
        if (launchT >= 0.0f) {
            if (!inst->launched) {
                inst->launched = true;
                inst->emergeWindTriggered = false;
            }
            if (cfg->trajectoryMode == VFX_LIQUID_TRAJ_GUIDED_HOMING && inst->launched) {
                GuidedLiquid_IntegrateHoming(inst, h);
            } else {
                GuidedLiquid_SampleGuideFrame(inst, launchT);
            }
        } else {
            GuidedLiquid_SampleGuideFrame(inst, 0.0f);
            inst->frame.vel = Vector3Zero();
            inst->frame.acc = Vector3Zero();
        }

        // Emergence wind impulse (the body displaces air as it forms)
        if (!inst->emergeWindTriggered && inst->timer >= 0.05f && !inst->launched) {
            inst->emergeWindTriggered = true;
            Wind_SpawnRadialBlast(cfg->source, cfg->emissionRadius * 2.2f,
                                  Clamp(1.0f + cfg->emissionRadius * 2.0f, 1.0f, 5.0f), 0.35f);
        }

        // Move the medium with the frame. Its published frame velocity is what
        // drags the droplets, so no positional transport is needed anywhere.
        if (inst->fieldAlive && !jetLike) {
            FieldTransform xf = {
                .position = inst->frame.pos,
                .axisX = inst->frame.right,
                .axisY = inst->frame.up,
                .axisZ = inst->frame.tangent,
                .frameVelocityMps = inst->frame.vel,
                .angularVelocityRadPerSec = Vector3Scale(inst->frame.tangent, cfg->swirlSpeed)};
            MotionFields_SetTransform(inst->mediumField, &xf);
        }

        // Body centre of mass (mass weighted) — the frame every droplet shears against.
        Vector3 com = Vector3Zero();
        Vector3 comVel = Vector3Zero();
        float totalMass = 0.0f;
        float totalVolume = 0.0f;
        for (int i = 0; i < inst->dropletCount; i++) {
            VC_LiquidDroplet *d = &inst->droplets[i];
            if (d->state != DROPLET_STATE_HOVERING && d->state != DROPLET_STATE_FLYING) continue;
            com = Vector3Add(com, Vector3Scale(d->pos, d->massKg));
            comVel = Vector3Add(comVel, Vector3Scale(d->velocity, d->massKg));
            totalMass += d->massKg;
            totalVolume += d->volumeM3;
        }
        if (totalMass > 1e-6f) {
            inst->comPos = Vector3Scale(com, 1.0f / totalMass);
            inst->comVel = Vector3Scale(comVel, 1.0f / totalMass);
        } else {
            inst->comPos = inst->frame.pos;
            inst->comVel = inst->frame.vel;
        }
        inst->bodyMassKg = totalMass;
        inst->bodyVolumeM3 = totalVolume;
        inst->bodyRadiusM = LiquidMedium_SphereRadiusM(fmaxf(totalVolume, 1e-6f));

        /* Wake history. Sampled on a fixed interval rather than per frame so the
         * ribbon and mist trail keep the same density on a 30 Hz phone and a
         * 144 Hz desktop. Only a body that is actually moving leaves a wake. */
        for (int t = 0; t < inst->trailCount; t++) inst->trailAge[t] += dt;
        while (inst->trailCount > 0 &&
               inst->trailAge[inst->trailCount - 1] > VC_GUIDED_LIQUID_TRAIL_LIFE_S) {
            inst->trailCount--;
        }
        inst->trailTimer += dt;
        bool wakeWanted = (cfg->emitMistTrail || cfg->emitQiRibbon) && !inst->impactTriggered &&
                          totalMass > 1e-6f;
        if (wakeWanted && inst->trailTimer >= 0.035f &&
            Vector3Length(inst->comVel) > 1.5f) {
            inst->trailTimer = 0.0f;
            int last = inst->trailCount < VC_GUIDED_LIQUID_TRAIL_POINTS
                           ? inst->trailCount
                           : VC_GUIDED_LIQUID_TRAIL_POINTS - 1;
            for (int t = last; t > 0; t--) {
                inst->trailPos[t] = inst->trailPos[t - 1];
                inst->trailAge[t] = inst->trailAge[t - 1];
            }
            inst->trailPos[0] = inst->comPos;
            inst->trailAge[0] = 0.0f;
            inst->trailCount = last + 1;
        }

        /* Neighbour grid over the coherent mass. Cell size follows the largest
         * droplet so a 27-cell scan can never miss a contact pair. */
        Vector3 boundsMin = {1e30f, 1e30f, 1e30f};
        Vector3 boundsMax = {-1e30f, -1e30f, -1e30f};
        float maxR = 0.0f;
        for (int i = 0; i < inst->dropletCount; i++) {
            VC_LiquidDroplet *d = &inst->droplets[i];
            if (d->state != DROPLET_STATE_HOVERING && d->state != DROPLET_STATE_FLYING) continue;
            boundsMin.x = fminf(boundsMin.x, d->pos.x); boundsMin.y = fminf(boundsMin.y, d->pos.y);
            boundsMin.z = fminf(boundsMin.z, d->pos.z);
            boundsMax.x = fmaxf(boundsMax.x, d->pos.x); boundsMax.y = fmaxf(boundsMax.y, d->pos.y);
            boundsMax.z = fmaxf(boundsMax.z, d->pos.z);
            maxR = fmaxf(maxR, d->radius);
        }
        bool gridValid = (boundsMax.x > boundsMin.x);
        float invCell = 0.0f;
        if (gridValid) {
            Vector3 extent = Vector3Subtract(boundsMax, boundsMin);
            float span = fmaxf(extent.x, fmaxf(extent.y, extent.z));
            float cell = fmaxf(2.2f * maxR, span / (float)VC_GUIDED_LIQUID_GRID_DIM);
            invCell = 1.0f / fmaxf(cell, 1e-4f);
            for (int c = 0; c < VC_GUIDED_LIQUID_GRID_CELLS; c++) s_glGridHead[c] = -1;
            for (int i = 0; i < inst->dropletCount; i++) {
                VC_LiquidDroplet *d = &inst->droplets[i];
                s_glGridNext[i] = -1;
                if (d->state != DROPLET_STATE_HOVERING && d->state != DROPLET_STATE_FLYING) continue;
                int cell = GuidedLiquid_CellIndex(d->pos, boundsMin, invCell);
                s_glGridNext[i] = s_glGridHead[cell];
                s_glGridHead[cell] = i;
            }
        }

        float nu = LiquidMedium_KinematicViscosityM2S(medium);
        float yieldSpeed = LiquidMedium_YieldSpeedMps(medium);
        float sigma = fmaxf(0.0f, medium->surfaceTensionNm);
        /* Timestep-stable gains: an explicit spring must satisfy omega*h < ~1. */
        float bindOmega = fminf(inst->bindOmega, 0.55f / h);
        float pressureOmega = fminf(inst->pressureOmega, 0.45f / h);
        if (pressureOmega < bindOmega) pressureOmega = bindOmega;

        int activeDroplets = 0;

        for (int i = 0; i < inst->dropletCount; i++) {
            VC_LiquidDroplet *d = &inst->droplets[i];
            d->age += dt;
            d->prevPos = d->pos;

            switch (d->state)
            {
            case DROPLET_STATE_IDLE: {
                /* A waiting droplet is still part of the body: counting it keeps
                 * the instance alive across the frame it leaves IDLE. */
                activeDroplets++;
                if (inst->timer >= d->launchDelay) {
                    d->age = 0.0f;
                    d->pos = d->launchPos;
                    d->prevPos = d->pos;
                    /* Sub-frame release offset.
                     *
                     * A staggered launch is evaluated once per frame, so every
                     * droplet whose delay falls inside the frame leaves at the
                     * same instant: a 0.002 s cadence at 60 Hz emits a packet of
                     * eight droplets every 16.7 ms. Those packets are
                     * `speed * dt` apart — 0.4 m for a 24 m/s jet — so a
                     * continuous stream rendered as a row of beads. Advancing
                     * the newborn droplet by the part of the frame it has
                     * already been alive restores the authored cadence exactly,
                     * at any frame rate. */
                    float releaseOvershoot = inst->timer - d->launchDelay;
                    if (releaseOvershoot < 0.0f) releaseOvershoot = 0.0f;
                    if (releaseOvershoot > dt) releaseOvershoot = dt;
                    if (jetLike) {
                        d->state = DROPLET_STATE_FLYING;
                        Vector3 launchVel = inst->launchSolved
                                                ? (Vector3){inst->launch.horizontalDir.x * inst->launch.horizontalSpeedMps,
                                                            inst->launch.verticalSpeedMps,
                                                            inst->launch.horizontalDir.z * inst->launch.horizontalSpeedMps}
                                                : Vector3Scale(inst->frame.tangent, fmaxf(3.0f, cfg->travelSpeed));
                        Vector3 spread = Vector3Scale(d->laneOffset, 0.25f);
                        d->velocity = Vector3Add(launchVel, spread);
                        d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, releaseOvershoot));
                        d->prevPos = d->pos;
                    } else if (d->launchDelay > 0.0f) {
                        d->state = DROPLET_STATE_FLYING;
                        d->velocity = inst->frame.vel;
                        d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, releaseOvershoot));
                        d->prevPos = d->pos;
                    } else if (cfg->emergeDuration > 0.01f) {
                        d->state = DROPLET_STATE_EMERGING;
                    } else if (cfg->hoverDuration > 0.01f) {
                        d->state = DROPLET_STATE_HOVERING;
                    } else {
                        d->state = DROPLET_STATE_FLYING;
                        d->velocity = inst->frame.vel;
                    }
                }
                break;
            }

            case DROPLET_STATE_EMERGING: {
                activeDroplets++;
                float emergeT = Clamp(d->age / fmaxf(0.01f, cfg->emergeDuration), 0.0f, 1.0f);
                /* Inflow boundary: the source feeds the body at a known rate.
                 * The velocity is the analytic derivative of that feed, so the
                 * hand-over to force integration has no discontinuity. */
                float s = emergeT * emergeT * (3.0f - 2.0f * emergeT);
                float ds = 6.0f * emergeT * (1.0f - emergeT) / fmaxf(0.01f, cfg->emergeDuration);
                /* Same lane the body will keep after the handover, so the inflow
                 * lands exactly where the binding expects it (no snap). */
                Vector3 laneVelocity = Vector3Zero();
                Vector3 target = GuidedLiquid_LaneTarget(inst, d, &inst->frame, inst->timer, &laneVelocity);
                Vector3 delta = Vector3Subtract(target, d->launchPos);
                d->pos = Vector3Add(d->launchPos, Vector3Scale(delta, s));
                d->velocity = Vector3Add(Vector3Scale(delta, ds), laneVelocity);
                if (d->age >= cfg->emergeDuration) {
                    if (cfg->hoverDuration > 0.01f) {
                        d->state = DROPLET_STATE_HOVERING;
                    } else {
                        d->state = DROPLET_STATE_FLYING;
                        d->velocity = inst->frame.vel;
                    }
                    d->age = 0.0f;
                }
                break;
            }

            case DROPLET_STATE_HOVERING: {
                activeDroplets++;
                Vector3 laneVelocity = Vector3Zero();
                Vector3 laneTarget = GuidedLiquid_LaneTarget(inst, d, &inst->frame, inst->timer,
                                                             &laneVelocity);
                d->pos = laneTarget;
                d->velocity = laneVelocity;
                if (inst->timer >= inst->launchDelay) {
                    d->state = DROPLET_STATE_FLYING;
                    d->velocity = Vector3Add(inst->frame.vel, laneVelocity);
                }
                break;
            }

            case DROPLET_STATE_FLYING: {
                activeDroplets++;
                if (jetLike) {
                    // Free ballistic jet droplet: no body binding.
                    MotionBodyProfile body = GuidedLiquid_MakeBody(cfg, medium, d->radius, NULL);
                    BodyPhysicalProperties props = MotionBody_GetPhysicalProperties(&body);
                    MediumProperties air = {.densityKgM3 = LIQUID_AIR_DENSITY_KG_M3,
                                            .dynamicViscosityPaS = LIQUID_AIR_DYNAMIC_VISCOSITY_PAS,
                                            .velocityMps = {0},
                                            .gravityMps2 = {0, -LIQUID_GRAVITY_MPS2 * cfg->gravityScale, 0}};
                    ReceiverConstraints constraints = {.mode = RECEIVER_FREE, .permittedAxes = {1, 1, 1}};
                    FieldSample sample = {0};
                    MotionFields_SampleSpatialBodyAtOffset(d->pos, d->velocity, &props, &air, &constraints,
                                                           h, 0.0f, MOTION_RECEIVER_ALL_COMPONENTS, &d->receiver,
                                                           &sample);
                    d->velocity = MotionBody_AdvanceFieldVelocity(d->velocity, &body, Vector3Zero(),
                                                                  Vector3Zero(), &sample, Vector3Zero(), h);
                    d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, h));
                } else {
                    // Coherent body: binding + viscosity + neighbours + field.
                    Vector3 laneVelocity = Vector3Zero();
                    Vector3 laneTarget = GuidedLiquid_LaneTarget(inst, d, &inst->frame, inst->timer,
                                                                 &laneVelocity);
                    laneVelocity = Vector3Add(inst->frame.vel, laneVelocity);
                    Vector3 rel = Vector3Subtract(d->velocity, laneVelocity);
                    Vector3 accel = inst->fieldAlive
                                        ? Vector3Subtract(Vector3Scale(Vector3Subtract(laneTarget, d->pos),
                                                                       bindOmega * 0.25f),
                                                          Vector3Scale(rel, 0.5f * bindOmega))
                                        : Vector3Subtract(Vector3Scale(Vector3Subtract(laneTarget, d->pos),
                                                                       bindOmega * bindOmega),
                                                          Vector3Scale(rel, 2.0f * bindOmega));

                    // Internal shear decay at nu/r^2: lava's kinematic
                    // viscosity is ~38,000x water's, so it damps deformation
                    // almost instantly, while water barely damps. Below the
                    // Bingham yield speed the liquid behaves as a solid.
                    float deform = Vector3Length(Vector3Subtract(d->velocity, inst->comVel));
                    if (nu > 1e-9f) {
                        float lambda = nu / fmaxf(1e-6f, d->radius * d->radius);
                        if (yieldSpeed > 1e-4f && deform < yieldSpeed) lambda += 8.0f;
                        lambda = fminf(lambda, 12.0f);
                        accel = Vector3Subtract(accel, Vector3Scale(Vector3Subtract(d->velocity, inst->comVel),
                                                                    lambda));
                    }

                    /* Contact and capillary bridges are accumulated separately
                     * so the contact response can be bounded as a WHOLE. A
                     * per-pair cap is not enough: a droplet on the surface of a
                     * dense body has 20+ neighbours on one side, and 20 x 2g
                     * downhill is a 40g field that no binding spring can hold,
                     * which is how the bottom of the emission sphere used to
                     * pour onto the floor before launch. */
                    Vector3 contactAccel = Vector3Zero();
                    Vector3 capillaryAccel = Vector3Zero();

                    // Surface tension + incompressibility against neighbours.
                    if (gridValid) {
                        int cx, cy, cz;
                        int base = GuidedLiquid_CellIndex(d->pos, boundsMin, invCell);
                        cx = base % VC_GUIDED_LIQUID_GRID_DIM;
                        cy = (base / VC_GUIDED_LIQUID_GRID_DIM) % VC_GUIDED_LIQUID_GRID_DIM;
                        cz = base / (VC_GUIDED_LIQUID_GRID_DIM * VC_GUIDED_LIQUID_GRID_DIM);
                        float invMass = d->massKg > 1e-9f ? 1.0f / d->massKg : 0.0f;
                        for (int dz = -1; dz <= 1; dz++)
                        for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int nx = cx + dx, ny = cy + dy, nz = cz + dz;
                            if (nx < 0 || ny < 0 || nz < 0 ||
                                nx >= VC_GUIDED_LIQUID_GRID_DIM ||
                                ny >= VC_GUIDED_LIQUID_GRID_DIM ||
                                nz >= VC_GUIDED_LIQUID_GRID_DIM) continue;
                            int cell = (nz * VC_GUIDED_LIQUID_GRID_DIM + ny) * VC_GUIDED_LIQUID_GRID_DIM + nx;
                            for (int j = s_glGridHead[cell]; j >= 0; j = s_glGridNext[j]) {
                                if (j == i) continue;
                                const VC_LiquidDroplet *o = &inst->droplets[j];
                                Vector3 delta = Vector3Subtract(o->pos, d->pos);
                                float dist = Vector3Length(delta);
                                float rSum = d->radius + o->radius;
                                if (dist < 1e-5f || dist > rSum * 1.6f) continue;
                                Vector3 n = Vector3Scale(delta, 1.0f / dist);
                                float gap = dist - rSum;
                                if (gap < 0.0f) {
                                    /* Contact, not a bulk modulus: the body is
                                     * a bound isosurface, not a conserved
                                     * volume, so a real incompressibility
                                     * pressure (K_water ~ 2.2 GPa) cannot be
                                     * integrated explicitly. This is a bounded
                                     * numerical contact that only stops visible
                                     * interpenetration. */
                                    float overlap = -gap / fmaxf(rSum, 1e-5f);
                                    float push = overlap * pressureOmega * pressureOmega * 0.5f;
                                    contactAccel = Vector3Subtract(contactAccel, Vector3Scale(n, push));
                                } else if (sigma > 0.0f) {
                                    /* Capillary bridge: 2*pi*sigma*r_neck / mass. */
                                    float neck = 2.0f * PI * sigma * fminf(d->radius, o->radius) * invMass;
                                    float falloff = 1.0f - gap / (rSum * 0.6f);
                                    if (falloff > 0.0f) capillaryAccel = Vector3Add(capillaryAccel,
                                                                                    Vector3Scale(n, neck * falloff));
                                }
                            }
                        }
                    }

                    /* Contact budget: the body's own binding must stay dominant.
                     * Two gravities is enough to separate interpenetrating blobs
                     * within a frame or two, and small enough that the guide can
                     * always hold the shape. */
                    float contactLen = Vector3Length(contactAccel);
                    float contactBudget = 2.0f * LIQUID_GRAVITY_MPS2 *
                                          fmaxf(1.0f, inst->config.gravityScale);
                    if (contactLen > contactBudget) {
                        contactAccel = Vector3Scale(contactAccel, contactBudget / contactLen);
                    }
                    accel = Vector3Add(accel, Vector3Add(contactAccel, capillaryAccel));

                    float accelLen = Vector3Length(accel);
                    if (accelLen > inst->bindAccelBudget) {
                        accel = Vector3Scale(accel, inst->bindAccelBudget / accelLen);
                    }

                    MotionBodyProfile body = GuidedLiquid_MakeBody(cfg, medium, d->radius, NULL);
                    // Inside the guided liquid body, droplets ride the fluid medium.
                    // Aerodynamic drag in dry air applies to detached cascading spray;
                    // within the liquid volume, airflow relaxation couples droplets directly
                    // to the motion field's turbulence and swirl.
                    body.aerodynamicAreaM2 = 0.0f;
                    body.aerodynamicDragCoefficient = 0.0f;
                    BodyPhysicalProperties props = MotionBody_GetPhysicalProperties(&body);
                    MediumProperties air = {.densityKgM3 = LIQUID_AIR_DENSITY_KG_M3,
                                            .dynamicViscosityPaS = LIQUID_AIR_DYNAMIC_VISCOSITY_PAS,
                                            .velocityMps = {0},
                                            .gravityMps2 = {0, -LIQUID_GRAVITY_MPS2 * cfg->gravityScale, 0}};
                    ReceiverConstraints constraints = {.mode = RECEIVER_FREE, .permittedAxes = {1, 1, 1}};
                    FieldSample sample = {0};
                    if (inst->fieldAlive) {
                        float reach = inst->fieldRadius + d->radius;
                        if (Vector3DistanceSqr(d->pos, inst->frame.pos) <= reach * reach) {
                            MotionFields_SampleSpatialBodyAtOffset(d->pos, d->velocity, &props, &air,
                                                                   &constraints, h, 0.0f,
                                                                   MOTION_RECEIVER_ALL_COMPONENTS, &d->receiver,
                                                                   &sample);
                        }
                    }
                    d->velocity = MotionBody_AdvanceFieldVelocity(d->velocity, &body, accel, Vector3Zero(),
                                                                  &sample, Vector3Zero(), h);
                    d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, h));
                }

                // Aerodynamic wake parting foliage along the flight.
                float flightProg = jetLike
                                       ? (inst->launch.durationSec > 0.01f ? Clamp(d->age / inst->launch.durationSec, 0.0f, 1.0f) : 1.0f)
                                       : (inst->launchSolved && inst->launch.durationSec > 0.01f ? Clamp(launchT / inst->launch.durationSec, 0.0f, 1.0f) : 1.0f);
                if (!d->wakeWindFired && flightProg >= 0.30f && flightProg <= 0.85f && (i % 8 == 0)) {
                    Vector3 dir = Vector3Length(d->velocity) > 0.2f ? Vector3Normalize(d->velocity)
                                                                    : inst->frame.tangent;
                    Wind_SpawnGust(d->pos, dir, d->radius * 3.5f,
                                   fminf(Vector3Length(d->velocity) * 0.6f, 12.0f), 0.18f);
                    d->wakeWindFired = true;
                }

                bool arrived = false;
                if (jetLike) {
                    /* A jet droplet has no shared frame: it is done when it
                     * reaches the target, or when its solved flight expires. */
                    float arrivalRadius = fmaxf(0.35f, d->radius * 2.0f);
                    arrived = Vector3Distance(d->pos, inst->curTarget) <= arrivalRadius ||
                              d->age >= inst->launch.durationSec;
                } else if (d->launchDelay > 0.0f) {
                    float arrivalRadius = fmaxf(0.35f, d->radius * 2.0f);
                    arrived = Vector3Distance(d->pos, inst->curTarget) <= arrivalRadius ||
                              d->age >= inst->launch.durationSec;
                } else {
                    /* The frame is exactly on the target at durationSec and
                     * every droplet rides that frame, so arrival is a time. */
                    arrived = (launchT >= inst->launch.durationSec) ||
                              (Vector3Distance(inst->comPos, inst->curTarget) <= fmaxf(0.5f, inst->bodyRadiusM * 1.5f));
                }

                float groundY = MapManager_GetGroundHeightAt(d->pos.x, d->pos.z);
                bool grazing = d->pos.y - d->radius <= groundY;
                float comGroundY = MapManager_GetGroundHeightAt(inst->comPos.x, inst->comPos.z);
                bool bodyImpacted = (launchT > 0.15f * (inst->launchSolved ? inst->launch.durationSec : 0.8f)) &&
                                    (inst->comPos.y <= comGroundY + inst->bodyRadiusM * 0.35f);

                if (arrived || bodyImpacted || grazing) {
                    Vector3 incoming = d->velocity;
                    float speed = Vector3Length(incoming);
                    if (grazing && incoming.y > 0.0f) {
                        incoming.y = 0.0f;
                        speed = Vector3Length(incoming);
                    }

                    /* Fragmentation: a droplet moving faster than its Weber
                     * breakup speed cannot stay whole, so the visible radius
                     * follows the fraction of volume the airspeed can hold. */
                    float stable = LiquidMedium_StableFraction(medium, speed, d->radius * 2.0f);
                    /* `stable` is a VOLUME fraction, so it enters the radius
                     * as its cube root. The remaining volume is not destroyed:
                     * it leaves as the mist this effect already emits. */
                    d->radius = fmaxf(0.01f, d->baseRadius * cbrtf(fmaxf(stable, 1e-6f)));

                    d->state = DROPLET_STATE_CASCADING;
                    d->age = 0.0f;

                    /* The burst keeps the body's momentum and adds a radial
                     * component bounded by the authored splash speed. */
                    float scatter = fminf(speed * 0.25f, fmaxf(0.0f, cfg->splashVelocity));
                    float angle = d->swirlPhase + (float)(i & 31) * (2.0f * PI / 32.0f);
                    float radial = 0.55f + 0.45f * GuidedLiquid_StableSigned(d->seed);
                    d->velocity.x += cosf(angle) * scatter * radial;
                    d->velocity.z += sinf(angle) * scatter * radial;
                    d->velocity.y = (grazing && !arrived && !bodyImpacted)
                                        ? fminf(fabsf(d->velocity.y), 1.5f)
                                        : fmaxf(-fabsf(d->velocity.y) - 0.5f, -scatter * 1.2f);

                    if (arrived || bodyImpacted) {
                        /* First arrival hands the body's real incoming velocity
                         * to the engine's impact model. It is guarded, so the
                         * crown is seeded once for the whole body. */
                        Vector3 impactPt = arrived ? inst->curTarget : inst->comPos;
                        GuidedLiquid_TriggerImpact(inst, impactPt, (Vector3){0, 1, 0}, incoming);
                    }

                    /* `splashDropletCount` is the airborne-spray budget. A
                     * 512-droplet body atomising in full was 512 billboards of
                     * 4 cm water — confetti, not a splash — and it was the
                     * reason the parameter existed but did nothing. Droplets
                     * over budget hand their conserved volume to the wet zone
                     * and stop existing as spray, so the puddle still gets all
                     * the water. */
                    if (cfg->splashDropletCount <= 0 ||
                        inst->splashSpawned >= cfg->splashDropletCount) {
                        d->state = DROPLET_STATE_SETTLED;
                        d->age = 0.0f;
                        d->velocity = Vector3Zero();
                        inst->puddleVolumeM3 += d->volumeM3;
                        inst->wetVolumeM3 += d->volumeM3;
                    } else {
                        inst->splashSpawned++;
                    }
                }
                break;
            }

            case DROPLET_STATE_CASCADING: {
                activeDroplets++;
                /* Detached spray: gravity + quadratic air drag only. There is
                 * no guide frame around it any more, so no medium field. */
                MotionBodyProfile body = GuidedLiquid_MakeBody(cfg, medium, d->radius, NULL);
                FieldSample freeAir = {0};
                d->velocity = MotionBody_AdvanceFieldVelocity(d->velocity, &body, Vector3Zero(),
                                                              Vector3Zero(), &freeAir, Vector3Zero(), h);
                d->pos = Vector3Add(d->pos, Vector3Scale(d->velocity, h));

                float airspeed = Vector3Length(d->velocity);
                d->radius = fmaxf(0.008f, d->baseRadius *
                                  cbrtf(fmaxf(LiquidMedium_StableFraction(medium, airspeed,
                                                                         d->baseRadius * 2.0f),
                                              1e-6f)));

                float groundY = MapManager_GetGroundHeightAt(d->pos.x, d->pos.z);
                float restY = groundY + d->radius;
                if (d->pos.y <= restY) {
                    d->pos.y = restY;
                    d->bounceCount++;
                    float impactSpeed = -d->velocity.y;
                    if (impactSpeed > 0.0f) {
                        d->velocity.y = impactSpeed * medium->restitution;
                        d->velocity.x *= medium->contactFriction;
                        d->velocity.z *= medium->contactFriction;
                    }
                    /* Below the capillary rebound / Bingham yield speed the
                     * droplet wets the surface instead of bouncing — the same
                     * threshold that makes mud and lava pile up. */
                    float stickSpeed = fmaxf(0.30f, LiquidMedium_YieldSpeedMps(medium));
                    if (impactSpeed < stickSpeed || d->bounceCount > 3) {
                        d->state = DROPLET_STATE_SETTLED;
                        d->age = 0.0f;
                        d->velocity = Vector3Zero();
                        /* The liquid that reached the ground is the droplet's
                         * conserved volume. A shrunken radius only means the
                         * rest atomised into the mist this effect already
                         * sheds — scaling the puddle by r^3 there would delete
                         * 99% of the body and the puddle would never form. */
                        inst->puddleVolumeM3 += d->volumeM3;
                        inst->wetVolumeM3 += d->volumeM3;
                    }
                }
                break;
            }

            case DROPLET_STATE_SETTLED: {
                activeDroplets++;
                /* Its volume already belongs to the wet zone; the droplet only
                 * exists for the merge frame so the surface does not pop. */
                if (d->age >= 0.16f) {
                    d->state = DROPLET_STATE_DEAD;
                } else {
                    d->radius = d->baseRadius * (1.0f - d->age / 0.16f) * 0.7f;
                }
                break;
            }

            case DROPLET_STATE_DEAD:
            default:
                break;
            }
        }

        /* Wet moisture spreading: volume conservation + percolation front expansion. */
        if (inst->impactTriggered) {
            inst->impactTimer += dt;
            inst->puddleTimer += dt;
            inst->wetTimer += dt;

            float thickness = fmaxf(0.005f, inst->wetThickness);
            float volumeRadius = inst->wetVolumeM3 > 1e-6f
                                     ? sqrtf(inst->wetVolumeM3 / (PI * thickness))
                                     : 0.0f;
            float spreadT = fminf(1.0f, inst->wetTimer / fmaxf(0.35f, inst->wetDuration * 0.25f));
            float percolation = 1.0f - expf(-3.2f * spreadT);
            float targetRadius = fmaxf(inst->wetMaxRadius * percolation, volumeRadius);
            targetRadius = fminf(targetRadius, inst->wetMaxRadius * 1.35f);

            if (targetRadius > inst->wetRadius) {
                float frontSpeed = sqrtf(LIQUID_GRAVITY_MPS2 * thickness) * 2.5f;
                float yieldSpeed = LiquidMedium_YieldSpeedMps(medium);
                if (yieldSpeed > 1e-3f) frontSpeed = fminf(frontSpeed, yieldSpeed);
                inst->wetRadius = fminf(targetRadius, inst->wetRadius + frontSpeed * dt);
            }
            inst->puddleRadius = inst->wetRadius;
            inst->puddleTimer = inst->wetTimer;
        }

        /* Instance lifetime: the body is over when no droplet is left and the
         * wet moisture has fully dried out. */
        bool wetExpired = !inst->impactTriggered || inst->wetTimer >= inst->wetDuration;
        float totalStagger = jetLike
                                 ? ((float)inst->dropletCount * fmaxf(0.0002f, cfg->launchStaggerDelay))
                                 : ((cfg->launchStaggerDelay > 1e-5f) ? (float)inst->dropletCount * cfg->launchStaggerDelay : 0.0f);
        float deadline = inst->launchDelay + totalStagger + fmaxf(0.2f, inst->launch.durationSec) +
                         3.0f * inst->wetDuration + 6.0f;
        if ((activeDroplets == 0 && wetExpired) || inst->timer > deadline) {
            if (inst->fieldAlive) {
                MotionFields_Stop(inst->mediumField);
                inst->mediumField = MOTION_FIELD_INVALID;
                inst->fieldAlive = false;
            }
            inst->active = false;
        }
    }
}

// ── Screen-Space Fluid Submission ────────────────────────────────────────────
//
// The SSF backends fuse overlapping spheres into one surface, which is exactly
// what a coherent liquid is. The body caps its submission at
// LIQUID_SURFACE_MAX_PARTICLES, so an over-dense body is decimated by a stride
// over its ALIVE droplets (never over raw indices: dead slots would waste the
// budget and the surface would thin out as the body lands).

/* Which droplets the COHERENT BODY renderer may include.
 *
 * Only the guided mass: a droplet that has left the body (CASCADING) is
 * detached spray and belongs to the billboard path, and a SETTLED droplet's
 * volume already lives in the wet zone. Feeding them to the raymarcher used to
 * scatter ~64 sub-centimetre blobs over a several-metre box: the march step is
 * derived from the smallest blob, so each of those blobs got a handful of
 * samples and rendered as a hard-edged cyan square with a white core instead of
 * a droplet. The splash looked like confetti stuck in the air. */
static inline bool GuidedLiquid_IsBodyDrawable(VC_DropletState state)
{
    return state == DROPLET_STATE_EMERGING || state == DROPLET_STATE_HOVERING ||
           state == DROPLET_STATE_FLYING;
}

/* Visible size: the density-field kernel, shrunk by whatever fraction the
 * physical radius has lost (Weber breakup) so the surface still thins out. */
static inline float GuidedLiquid_VisibleRadius(const VC_LiquidDroplet *d)
{
    float base = d->baseRadius > 1e-6f ? d->baseRadius : 1e-6f;
    return d->drawRadius * Clamp(d->radius / base, 0.05f, 1.0f);
}

static inline float GuidedLiquid_Smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / fmaxf(1e-6f, edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

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
        // Reconstruction radius fuses adjacent droplets into one meniscus.
        LiquidSurface_SetReconstructionRadius(fmaxf(0.02f, inst->dropletRadiusM) * 1.15f);

        int alive = 0;
        for (int i = 0; i < inst->dropletCount; i++) {
            if (GuidedLiquid_IsBodyDrawable(inst->droplets[i].state)) alive++;
        }

        int budget = LIQUID_SURFACE_MAX_PARTICLES;
        float maxSurfRad = fmaxf(0.08f, cfg->dropletRadius * 1.50f);
        if (alive > 0 && alive <= budget) {
            for (int i = 0; i < inst->dropletCount; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (!GuidedLiquid_IsBodyDrawable(d->state)) continue;
                float r = fminf(GuidedLiquid_VisibleRadius(d) * 1.25f, maxSurfRad);
                LiquidSurface_RegisterParticle(d->pos, r);
            }
        } else if (alive > 0) {
            int stride = (alive + budget - 1) / budget;
            int registered = 0;
            for (int i = 0; i < inst->dropletCount && registered < budget; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (!GuidedLiquid_IsBodyDrawable(d->state)) continue;
                /* Hashed subset, not `i % stride`: emission offsets are built in
                 * index order, so a strided slice is a structured sub-lattice
                 * and leaves coherent gaps in the surface. */
                if (stride > 1 &&
                    (GuidedLiquid_Hash((unsigned int)i + (unsigned int)inst->id * 7919u) %
                     (unsigned int)stride) != 0) continue;
                float r = fminf(GuidedLiquid_VisibleRadius(d) * 1.35f, maxSurfRad);
                LiquidSurface_RegisterParticle(d->pos, r);
                registered++;
            }
        }

        // Puddles removed in favor of ground-conforming expanding wet moisture mesh.
    }
}

// ── Droplet Billboard Texture ────────────────────────────────────────────────

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
            float edgeAlpha = 1.0f - GuidedLiquid_Smoothstep(0.72f, 1.0f, r);

            float nz = sqrtf(1.0f - r2);
            Vector3 norm = {px, py, nz};

            float nDotL = fmaxf(0.0f, Vector3DotProduct(norm, sunL));
            float nDotH = fmaxf(0.0f, Vector3DotProduct(norm, halfSun));
            float specSun = powf(nDotH, 28.0f);
            float nDotBounce = fmaxf(0.0f, Vector3DotProduct(norm, bounceL));
            float specBounce = powf(nDotBounce, 12.0f) * 0.22f;
            float coreDiffuse = 0.70f + 0.30f * nDotL;
            float fresnel = powf(1.0f - nz, 2.4f) * 0.60f;

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

// ── Ground-Conforming Expanding Wet Moisture Pass ────────────────────────────
//
// Replaces ground decals and puddle blob rings. Conforms directly to the map
// terrain height, expanding organically via percolation dynamics, darkening the
// substrate and providing physical liquid specular sheen before drying out.

static void GuidedLiquid_DrawWetMoisture(const VC_GuidedLiquidInstance *inst, Camera3D cam)
{
    if (!inst || !inst->impactTriggered || inst->wetRadius < 0.05f) return;
    if (inst->wetTimer >= inst->wetDuration) return;

    float lifeFade = 1.0f - Clamp(inst->wetTimer / fmaxf(0.5f, inst->wetDuration), 0.0f, 1.0f);
    if (lifeFade <= 1e-4f) return;
    float dryAlpha = lifeFade * lifeFade;

    int liquidProfile = inst->config.liquidProfile;
    Color coreColor, midColor, waveColor, rimColor;

    switch (liquidProfile) {
    case LIQUID_MEDIUM_LAVA:
        coreColor = (Color){245, 95, 25, (unsigned char)(220.0f * dryAlpha)};
        midColor  = (Color){175, 55, 18, (unsigned char)(185.0f * dryAlpha)};
        waveColor = (Color){255, 160, 40, (unsigned char)(235.0f * dryAlpha)};
        rimColor  = (Color){70, 20, 10, 0};
        break;
    case LIQUID_MEDIUM_MUD:
        coreColor = (Color){32, 22, 15, (unsigned char)(210.0f * dryAlpha)};
        midColor  = (Color){48, 35, 22, (unsigned char)(170.0f * dryAlpha)};
        waveColor = (Color){65, 48, 30, (unsigned char)(190.0f * dryAlpha)};
        rimColor  = (Color){50, 38, 25, 0};
        break;
    case LIQUID_MEDIUM_POISON:
        coreColor = (Color){18, 68, 30, (unsigned char)(210.0f * dryAlpha)};
        midColor  = (Color){32, 105, 45, (unsigned char)(175.0f * dryAlpha)};
        waveColor = (Color){55, 185, 75, (unsigned char)(230.0f * dryAlpha)};
        rimColor  = (Color){35, 100, 45, 0};
        break;
    case LIQUID_MEDIUM_LIQUID_METAL:
        coreColor = (Color){200, 210, 225, (unsigned char)(225.0f * dryAlpha)};
        midColor  = (Color){150, 168, 190, (unsigned char)(180.0f * dryAlpha)};
        waveColor = (Color){230, 240, 255, (unsigned char)(240.0f * dryAlpha)};
        rimColor  = (Color){90, 105, 125, 0};
        break;
    case LIQUID_MEDIUM_WATER:
    default:
        coreColor = (Color){20, 65, 120, (unsigned char)(220.0f * dryAlpha)};
        midColor  = (Color){38, 120, 195, (unsigned char)(185.0f * dryAlpha)};
        waveColor = (Color){65, 185, 255, (unsigned char)(235.0f * dryAlpha)};
        rimColor  = (Color){30, 85, 140, 0};
        break;
    }

    const int kSlices = 32;
    const int kRings = 4;
    static const float ringFracs[4] = {0.0f, 0.35f, 0.75f, 1.0f};

    Vector3 center = inst->groundWetPos;
    float baseRad = inst->wetRadius;

    Vector3 ringVerts[4][32];
    Color ringColors[4][32];

    Vector3 camDir = Vector3Subtract(cam.position, center);
    float camDist = Vector3Length(camDir);
    if (camDist > 1e-4f) camDir = Vector3Scale(camDir, 1.0f / camDist);
    Vector3 sunDir = Vector3Normalize((Vector3){0.45f, 0.85f, 0.25f});
    Vector3 halfVec = Vector3Normalize(Vector3Add(camDir, sunDir));
    float ndotH = fmaxf(0.0f, halfVec.y);
    float specGain = powf(ndotH, 16.0f) * 0.65f * dryAlpha;

    for (int r = 0; r < kRings; r++) {
        float f = ringFracs[r];
        float elevationOffset = 0.024f + 0.006f * (1.0f - f);
        for (int s = 0; s < kSlices; s++) {
            float angle = (float)s / (float)kSlices * 2.0f * PI;
            float lobe = 1.0f + 0.10f * sinf(3.0f * angle + 0.4f) + 0.06f * cosf(5.0f * angle + 1.1f);
            float rad = baseRad * f * lobe;
            float px = center.x + cosf(angle) * rad;
            float pz = center.z + sinf(angle) * rad;
            float py = MapManager_GetGroundHeightAt(px, pz) + elevationOffset;

            ringVerts[r][s] = (Vector3){px, py, pz};

            Color c;
            if (r == 0) {
                c = coreColor;
            } else if (r == 1) {
                c = (Color){
                    (unsigned char)Clamp((float)coreColor.r * 0.5f + (float)midColor.r * 0.5f + specGain * 140.0f, 0.0f, 255.0f),
                    (unsigned char)Clamp((float)coreColor.g * 0.5f + (float)midColor.g * 0.5f + specGain * 165.0f, 0.0f, 255.0f),
                    (unsigned char)Clamp((float)coreColor.b * 0.5f + (float)midColor.b * 0.5f + specGain * 190.0f, 0.0f, 255.0f),
                    (unsigned char)((float)coreColor.a * 0.5f + (float)midColor.a * 0.5f)
                };
            } else if (r == 2) {
                // Expanding wave front with specular highlight
                float wavePulse = 0.85f + 0.15f * sinf(baseRad * 4.0f - inst->wetTimer * 6.0f);
                c = (Color){
                    (unsigned char)Clamp((float)waveColor.r * wavePulse + specGain * 100.0f, 0.0f, 255.0f),
                    (unsigned char)Clamp((float)waveColor.g * wavePulse + specGain * 120.0f, 0.0f, 255.0f),
                    (unsigned char)Clamp((float)waveColor.b * wavePulse + specGain * 140.0f, 0.0f, 255.0f),
                    (unsigned char)Clamp((float)waveColor.a * wavePulse, 0.0f, 255.0f)
                };
            } else {
                c = rimColor;
            }
            ringColors[r][s] = c;
        }
    }

    rlPushMatrix();
    rlSetTexture(0);
    rlDisableBackfaceCulling();
    rlEnableDepthTest();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    rlBegin(RL_TRIANGLES);

    // Center fan (ring 0 to ring 1)
    for (int s = 0; s < kSlices; s++) {
        int next = (s + 1) % kSlices;
        Vector3 c0 = ringVerts[0][s];
        Vector3 v1 = ringVerts[1][s];
        Vector3 v2 = ringVerts[1][next];

        rlColor4ub(ringColors[0][s].r, ringColors[0][s].g, ringColors[0][s].b, ringColors[0][s].a);
        rlVertex3f(c0.x, c0.y, c0.z);

        rlColor4ub(ringColors[1][s].r, ringColors[1][s].g, ringColors[1][s].b, ringColors[1][s].a);
        rlVertex3f(v1.x, v1.y, v1.z);

        rlColor4ub(ringColors[1][next].r, ringColors[1][next].g, ringColors[1][next].b, ringColors[1][next].a);
        rlVertex3f(v2.x, v2.y, v2.z);
    }

    // Concentric quads between rings (ring 1 -> 2, ring 2 -> 3)
    for (int r = 1; r < kRings - 1; r++) {
        for (int s = 0; s < kSlices; s++) {
            int next = (s + 1) % kSlices;

            Vector3 v00 = ringVerts[r][s];
            Vector3 v01 = ringVerts[r][next];
            Vector3 v10 = ringVerts[r + 1][s];
            Vector3 v11 = ringVerts[r + 1][next];

            Color c00 = ringColors[r][s];
            Color c01 = ringColors[r][next];
            Color c10 = ringColors[r + 1][s];
            Color c11 = ringColors[r + 1][next];

            // Tri 1
            rlColor4ub(c00.r, c00.g, c00.b, c00.a); rlVertex3f(v00.x, v00.y, v00.z);
            rlColor4ub(c10.r, c10.g, c10.b, c10.a); rlVertex3f(v10.x, v10.y, v10.z);
            rlColor4ub(c01.r, c01.g, c01.b, c01.a); rlVertex3f(v01.x, v01.y, v01.z);

            // Tri 2
            rlColor4ub(c01.r, c01.g, c01.b, c01.a); rlVertex3f(v01.x, v01.y, v01.z);
            rlColor4ub(c10.r, c10.g, c10.b, c10.a); rlVertex3f(v10.x, v10.y, v10.z);
            rlColor4ub(c11.r, c11.g, c11.b, c11.a); rlVertex3f(v11.x, v11.y, v11.z);
        }
    }

    rlEnd();

    rlSetTexture(0);
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    rlPopMatrix();
}

// ── 3D Geometry Pass ─────────────────────────────────────────────────────────
//
// The coherent phases are a single liquid body, so they are rendered by the
// metaball raymarcher (or SSF above). Billboard quads are reserved for the
// DETACHED spray after the body breaks, because drawing them over a coherent
// body is what previously sliced the orb into visible flat cards.

/* ── Wake: qi ribbon + mist trail ────────────────────────────────────────────
 *
 * Both are pure camera-facing geometry built from the shared trail history.
 * They exist because `emitQiRibbon` and `emitMistTrail` were authored on every
 * preset but never read by any draw path: a preset could promise a ribbon and
 * the renderer would ignore it. One history buffer keeps them agreeing about
 * where the body has been, and both fade over the same window so a fast body
 * leaves a single stroke instead of a row of unrelated puffs. */
static void GuidedLiquid_DrawTrail(const VC_GuidedLiquidInstance *inst, Vector3 camRight,
                                   Texture2D tex)
{
    if (inst->trailCount < 2) return;
    const VFX_GuidedLiquidConfig *cfg = &inst->config;
    bool wantRibbon = cfg->emitQiRibbon;
    bool wantMist = cfg->emitMistTrail;
    if (!wantRibbon && !wantMist) return;

    float life = VC_GUIDED_LIQUID_TRAIL_LIFE_S;

    rlPushMatrix();
    rlSetTexture(tex.id);
    rlDisableBackfaceCulling();
    rlEnableDepthTest();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    if (wantRibbon) {
        float halfW = fmaxf(0.012f, cfg->ribbonWidth) * 0.5f;
        Color glow = cfg->liquidGlowColor;
        rlBegin(RL_TRIANGLES);
        for (int t = 0; t + 1 < inst->trailCount; t++) {
            float f0 = 1.0f - Clamp(inst->trailAge[t] / life, 0.0f, 1.0f);
            float f1 = 1.0f - Clamp(inst->trailAge[t + 1] / life, 0.0f, 1.0f);
            if (f0 <= 0.0f && f1 <= 0.0f) continue;

            Vector3 p0 = inst->trailPos[t];
            Vector3 p1 = inst->trailPos[t + 1];
            Vector3 s0 = Vector3Scale(camRight, halfW * f0);
            Vector3 s1 = Vector3Scale(camRight, halfW * f1);

            unsigned char a0 = (unsigned char)(glow.a * f0 * f0 * 0.55f);
            unsigned char a1 = (unsigned char)(glow.a * f1 * f1 * 0.55f);

            Vector3 v0 = Vector3Subtract(p0, s0), v1 = Vector3Add(p0, s0);
            Vector3 v2 = Vector3Add(p1, s1), v3 = Vector3Subtract(p1, s1);

            rlColor4ub(glow.r, glow.g, glow.b, a0);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v0.x, v0.y, v0.z);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v1.x, v1.y, v1.z);
            rlColor4ub(glow.r, glow.g, glow.b, a1);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v2.x, v2.y, v2.z);

            rlColor4ub(glow.r, glow.g, glow.b, a0);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v0.x, v0.y, v0.z);
            rlColor4ub(glow.r, glow.g, glow.b, a1);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v2.x, v2.y, v2.z);
            rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v3.x, v3.y, v3.z);
        }
        rlEnd();
    }

    if (wantMist) {
        float baseR = fmaxf(0.05f, inst->bodyRadiusM) * 0.85f;
        Color mist = cfg->liquidFoamColor;
        rlBegin(RL_QUADS);
        for (int t = 0; t < inst->trailCount; t++) {
            float f = 1.0f - Clamp(inst->trailAge[t] / life, 0.0f, 1.0f);
            if (f <= 0.0f) continue;
            float r = baseR * (0.55f + 1.55f * (1.0f - f));
            float a = f * f * 0.20f;
            if (a <= 0.004f) continue;
            unsigned char ac = (unsigned char)Clamp(mist.a * a, 0.0f, 255.0f);
            Vector3 p = inst->trailPos[t];
            Vector3 s = Vector3Scale(camRight, r);
            Vector3 u = (Vector3){0.0f, r, 0.0f};
            Vector3 p0 = Vector3Subtract(Vector3Subtract(p, s), u);
            Vector3 p1 = Vector3Subtract(Vector3Add(p, s), u);
            Vector3 p2 = Vector3Add(Vector3Add(p, s), u);
            Vector3 p3 = Vector3Add(Vector3Subtract(p, s), u);
            rlColor4ub(mist.r, mist.g, mist.b, ac);
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

void VC_GuidedLiquid_Draw3D(Camera3D cam)
{
    Texture2D dropletTex = GuidedLiquid_GetDropletTexture();

    Vector3 camFwd = Vector3Subtract(cam.target, cam.position);
    float camDist = Vector3Length(camFwd);
    if (camDist > 0.001f) camFwd = Vector3Scale(camFwd, 1.0f / camDist);
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(camFwd, cam.up));
    Vector3 camUp = Vector3CrossProduct(camRight, camFwd);

    for (int instIdx = 0; instIdx < VC_GUIDED_LIQUID_MAX_INSTANCES; instIdx++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[instIdx];
        if (!inst->active) continue;

        const VFX_GuidedLiquidConfig *cfg = &inst->config;

        if ((cfg->renderBackend == VFX_LIQUID_BACKEND_METABALL3D ||
            cfg->renderBackend == VFX_LIQUID_BACKEND_HYBRID) && getenv("WUXING_GL_NOMB") == NULL)
        {
            // Reserve blob slots for the crown only when impact is active (puddles removed)
            int crownBlobs = (cfg->impactCrown && inst->impactTriggered && inst->impactTimer < 0.35f) ? 8 : 0;

            LiquidMetaball3D_BeginBody(&inst->material, cfg->threshold);

            /* Register EVERY coherent droplet and let the engine fuse the
             * overflow. `LiquidMetaball3D_RegisterBlob` is volume-preserving
             * when the body is full: it merges with the nearest blob and moves
             * that blob to the volume-weighted centroid, which is exactly the
             * right behaviour for both shapes a guided liquid can take — a
             * packed orb fuses evenly, and a jet fuses along its own line.
             *
             * Subsampling here was the bug behind two separate artefacts:
             * `i % stride` picked a structured sub-lattice (visible lobes), and
             * a hashed subset of a *stream* is a row of beads, because the
             * droplets are spread along the path rather than through a volume.
             * No subset can be correct for both, and 512 registrations per
             * frame cost nothing next to the raymarch.
             *
             * The kernel is not the visible radius. A Wyvill blob reaches the
             * 0.52 threshold at 0.443r of its own radius, so a blob registered
             * at its intended visible size renders at 44% of it — which is why
             * every surface here used to be lumpy and every jet a row of beads.
             * Dividing by the isosurface factor makes one droplet render at its
             * intended size and makes fused neighbours overlap enough to close
             * the gap between merge centroids. */
            const float kIsoSurface = 0.443f;
            int registered = 0;
            for (int i = 0; i < inst->dropletCount; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (!GuidedLiquid_IsBodyDrawable(d->state)) continue;
                LiquidMetaball3D_RegisterBlob(d->pos,
                                              GuidedLiquid_VisibleRadius(d) * 1.15f / kIsoSurface);
                registered++;
            }
            (void)registered;

            // Worthington crown: rim blobs rising and collapsing from the
            // contact point, driven by the real impact timer.
            if (cfg->impactCrown && inst->impactTriggered && inst->impactTimer < 0.35f) {
                float crownT = inst->impactTimer / 0.35f;
                float crownRad = cfg->dropletRadius * (1.2f + 2.0f * crownT);
                float crownH = cfg->dropletRadius * 2.0f * sinf(crownT * PI);
                for (int p = 0; p < crownBlobs; p++) {
                    float angle = ((float)p / (float)crownBlobs) * 2.0f * PI;
                    Vector3 rimPos = {
                        inst->groundWetPos.x + cosf(angle) * crownRad,
                        inst->groundWetPos.y + crownH * (0.8f + 0.3f * sinf((float)p * 2.0f)),
                        inst->groundWetPos.z + sinf(angle) * crownRad};
                    LiquidMetaball3D_RegisterBlob(rimPos, cfg->dropletRadius * 0.38f * (1.0f - crownT * 0.5f));
                }
            }

            LiquidMetaball3D_EndBody();
        }

        GuidedLiquid_DrawTrail(inst, camRight, dropletTex);

        // Ground-conforming expanding wet moisture (renders for all backends)
        GuidedLiquid_DrawWetMoisture(inst, cam);

        // Detached spray only: camera-facing quads, velocity-stretched inside
        // the view plane, batched 256 quads per texture bind.
        int cascadeCount = 0;
        for (int i = 0; i < inst->dropletCount; i++) {
            if (inst->droplets[i].state == DROPLET_STATE_CASCADING) cascadeCount++;
        }
        if (cascadeCount == 0) continue;

        rlPushMatrix();
        rlDisableBackfaceCulling();
        rlEnableDepthTest();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ALPHA);

        const int batchChunk = 256;
        for (int bStart = 0; bStart < inst->dropletCount; bStart += batchChunk) {
            int bEnd = (bStart + batchChunk < inst->dropletCount) ? bStart + batchChunk : inst->dropletCount;

            rlSetTexture(dropletTex.id);
            rlBegin(RL_QUADS);
            for (int i = bStart; i < bEnd; i++) {
                const VC_LiquidDroplet *d = &inst->droplets[i];
                if (d->state != DROPLET_STATE_CASCADING) continue;

                // Project velocity into the view plane so the streak never
                // foreshortens into a disc.
                Vector3 vCam = Vector3Subtract(d->velocity,
                                               Vector3Scale(camFwd, Vector3DotProduct(d->velocity, camFwd)));
                float speedCam = Vector3Length(vCam);

                Vector3 uLong, uTrans;
                float stretch;
                if (speedCam > 0.35f) {
                    uLong = Vector3Scale(vCam, 1.0f / speedCam);
                    uTrans = Vector3Normalize(Vector3CrossProduct(uLong, camFwd));
                    stretch = Clamp(1.0f + speedCam * 0.035f, 1.0f, 2.2f);
                } else {
                    uLong = camUp;
                    uTrans = camRight;
                    stretch = 1.0f;
                }

                float r = GuidedLiquid_VisibleRadius(d);
                Vector3 extX = Vector3Scale(uTrans, r);
                Vector3 extY = Vector3Scale(uLong, r * stretch);

                Color col = cfg->liquidBodyColor;
                col.a = (unsigned char)(col.a * 0.85f);

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

static Mesh s_guidedLiquidShadowMesh = {0};
static Material s_guidedLiquidShadowMat = {0};
static bool s_guidedLiquidShadowInit = false;

static void GuidedLiquid_InitShadowMesh(void)
{
    if (s_guidedLiquidShadowInit) return;
    s_guidedLiquidShadowMesh = GenMeshSphere(1.0f, 16, 16);
    s_guidedLiquidShadowMat = LoadMaterialDefault();
    s_guidedLiquidShadowInit = true;
}

void VC_GuidedLiquid_DrawShadowPass(void)
{
    if (!VFX_GuidedLiquid_IsAnyActive()) return;

    GuidedLiquid_InitShadowMesh();
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0) return;
    s_guidedLiquidShadowMat.shader = depthShader;

    for (int i = 0; i < VC_GUIDED_LIQUID_MAX_INSTANCES; i++) {
        VC_GuidedLiquidInstance *inst = &s_guidedLiquidInstances[i];
        if (!inst->active) continue;

        if (!inst->impactTriggered) {
            /* Ground the body only once it is low enough for its shadow to read
             * as contact rather than as a detached black hole. A water orb is
             * translucent, so a full-strength hard ellipse far below a high arc
             * is both wrong and ugly; the radius shrinks to nothing by 3.5 m. */
            float height = inst->comPos.y - MapManager_GetGroundHeightAt(inst->comPos.x, inst->comPos.z);
            float groundFade = 1.0f - Clamp((height - 1.5f) / 2.0f, 0.0f, 1.0f);
            float rad = fmaxf(0.35f, inst->bodyRadiusM) * groundFade;
            if (rad > 0.05f) {
                Matrix xf = MatrixIdentity();
                xf.m0 = rad; xf.m5 = rad; xf.m10 = rad;
                xf.m12 = inst->comPos.x;
                xf.m13 = inst->comPos.y;
                xf.m14 = inst->comPos.z;
                DrawMesh(s_guidedLiquidShadowMesh, s_guidedLiquidShadowMat, xf);
            }
        } else {
            /* A wet patch is a film of water, not an occluder. The old code
             * drew the expanding puddle as a flattened sphere into the shadow
             * map, so a 1.8 m radius puddle cast a 1.8 m hard-edged shadow and
             * the aftermath read as a hole punched in the floor rather than as
             * wet ground. Only the brief moment where the crown still stands
             * above the surface keeps any shadow, and it is small. */
            float crownFade = 1.0f - Clamp(inst->impactTimer / 0.45f, 0.0f, 1.0f);
            float rad = inst->bodyRadiusM * 0.85f * crownFade;
            if (rad > 0.05f) {
                Matrix xf = MatrixIdentity();
                xf.m0 = rad; xf.m5 = rad * 0.55f; xf.m10 = rad;
                xf.m12 = inst->groundWetPos.x;
                xf.m13 = inst->groundWetPos.y + inst->bodyRadiusM * 0.25f;
                xf.m14 = inst->groundWetPos.z;
                DrawMesh(s_guidedLiquidShadowMesh, s_guidedLiquidShadowMat, xf);
            }
        }
    }
}

#endif // VC_GUIDED_LIQUID_INL


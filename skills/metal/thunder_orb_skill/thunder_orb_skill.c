#include "thunder_orb_skill.h"

#include "core/vfx_proc_ray.h"
#include "core/vfx_light.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/particle_system.h"
#include "core/tuning.h"
#include "entities/entities.h"

#include "raylib.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

extern Camera3D camera;

#ifndef PI
#define PI 3.14159265f
#endif

// ── constants (real-world-scaled: 1 unit = 1 meter) ──────────────────────

// Pool-size constants (size static arrays) — stay #define, not tunable.
#define THUNDER_ORB_FLIGHT_RAYS        7
#define THUNDER_ORB_SURFACE_ARCS       3
#define THUNDER_ORB_ARC_PER_STRIKE    3
#define THUNDER_ORB_ARC_SLOTS        (THUNDER_ORB_RAIN_SLOTS * THUNDER_ORB_ARC_PER_STRIKE)
#define THUNDER_ORB_RAIN_SLOTS        8
// Balance constants (damage), not visual shape/feel — left as #define.
#define THUNDER_ORB_BASE_IMPACT_DMG    45.0f
#define THUNDER_ORB_BASE_RAIN_DMG      12.0f

// Sandbox-tunable physics + size knobs (see RegisterSkillTunables in
// core/skill_manager.h). Grouped by phase ("flight"/"impact"/"rain") in the
// sandbox UI. Flight speed is now a SkillCurve (core/skill_curve.h) sampled
// over elapsed flight TIME (never over fraction-of-distance-to-target — see
// SkillHelper_StepCurveFlight's contract in core/skill_helper.h), capped by
// flight_max_duration/flight_max_range so a cast can never fly farther/longer
// than its own limit regardless of how far the target is. Loaded from
// skills/metal/thunder_orb_skill/thunder_orb_skill.tuning on init if present.
static SkillCurve s_flightSpeedCurve;     // orb travel speed over flight time (m/s)
static float s_flightMaxDuration = 3.0f;  // hard cap on flight time (s)
static float s_flightMaxRange = 15.0f;    // hard cap on flight distance (m)
static float s_impactKnockback = 2.8f;    // melee-hit knockback impulse (m/s)
static float s_rainStrikeKnockback = 0.8f; // rain-bolt knockback impulse (m/s)
static float s_orbRadius = 0.12f;         // orb core collision/visual radius (m)
static float s_rayLenMax = 0.55f;         // flight-phase lightning ray length, upper bound (m)
static float s_rainRadius = 1.3f;         // rain-strike scatter radius around impact point (m)
static float s_impactFlashRadius = 1.8f;  // impact light-flash radius (m)
static float s_rainLightRadius = 0.8f;    // per-rain-bolt ground light radius (m)

// One fully-configurable, always-additive tunable "extra force" mix per
// phase (core/skill_helper.h's SkillForceMix — all 8 force types
// simultaneously available, each with its own strength; 0 = that type
// contributes nothing). Flight's affects the orb's own path AND (via
// s_flightParticleField) its trailing plasma particles. Rain's affects
// ground-strike sparks, which have no other force applied today (raw
// ballistic velocity), so it's purely additive there. Every component
// defaults to 0 strength (no behavior change) until dialed up.
static SkillForceMix s_flightForce;
static SkillForceMix s_rainForce;
static ForceField s_flightParticleField; // rebuilt each frame from s_flightForce
static ForceField s_rainSparkField;      // rebuilt each strike from s_rainForce

// Per-phase over-lifetime curves (core/skill_curve.h + core/particle_system.h's
// radiusCurve/speedCurve/alphaCurve) — how a particle's size/velocity/opacity
// evolve across its OWN lifetime (t=0 at spawn, t=1 at death), seeded flat at
// 1.0 (no change from today's behavior) so these are no-ops until shaped in
// the sandbox. No impact-phase curves here — TriggerImpact() has no local
// particle spawn of its own (delegates entirely to the shared
// SpawnImpactEffect preset), so there's nothing local to shape.
static SkillCurve s_flightRadiusCurve, s_flightSpeedParticleCurve, s_flightAlphaCurve;
static SkillCurve s_rainRadiusCurve, s_rainSpeedCurve, s_rainAlphaCurve;

// Remaining per-spawn-site shape/feel knobs — everything that visibly
// changes how dense, fast, or long-lived an effect reads. Pool-size
// constants that size a static array (THUNDER_ORB_FLIGHT_RAYS,
// THUNDER_ORB_RAIN_SLOTS, ...) stay #define — making those runtime-tunable
// would need dynamic allocation, which this project's static-array
// convention doesn't use.
static float s_rayLenMin = 0.24f;
static float s_rayScaleMin = 0.55f, s_rayScaleMax = 1.0f;
static float s_surfArcLifeMin = 0.06f, s_surfArcLifeMax = 0.14f;
static float s_orbParticleRadius = 0.035f;
static float s_orbParticleLifetime = 0.18f;
static float s_orbParticleSpeedMin = 0.6f, s_orbParticleSpeedMax = 1.4f;
static float s_orbCore1RadiusMult = 0.7f, s_orbCore1Lifetime = 0.09f;
static float s_orbCore2RadiusMult = 1.6f, s_orbCore2Lifetime = 0.13f;

static float s_impactFlashLifetime = 0.15f;

static float s_rainDuration = 5.0f;
static float s_rainYOrigin = 4.2f;
static float s_rainBoltScale = 1.2f;
static float s_rainBoltLifetimeMin = 0.28f, s_rainBoltLifetimeMax = 0.5f;
static float s_sparkRadius = 0.022f;
static float s_sparkCount = 10.0f;
static float s_sparkOutSpeedMin = 0.3f, s_sparkOutSpeedMax = 1.3f;
static float s_sparkUpSpeedMin = 1.4f, s_sparkUpSpeedMax = 3.2f;
static float s_sparkLifetimeMin = 0.25f, s_sparkLifetimeMax = 0.5f;
static float s_arcLife = 0.12f;
static float s_arcRadiusMin = 0.15f, s_arcRadiusMax = 0.45f;

// 12 original named tunables + 30 shape/feel-range tunables - 2 old
// single-float alpha entries + 2 phases x 3 over-lifetime curves
// (radius/speed/alpha) + 2 phases x 1 force mix x
// SKILL_FORCE_MIX_TUNABLE_COUNT(29) = 12 + 30 - 2 + 6 + 58 = 104
#define THUNDER_ORB_TUNABLE_COUNT 104

// ── types ──────────────────────────────────────────────────────────────────

typedef struct {
    int     rayId;
    bool    active;
    float   lifeLeft;
    Vector3 from;
    Vector3 to;
} ArcBolt;

typedef struct {
    int     rayId;
    bool    active;
    float   aliveTimer;
    float   lifeInit;    // initial aliveTimer — for the flash→afterglow envelope
    float   gapTimer;
    Vector3 skyOrigin;   // top endpoint (fixed)
    Vector3 groundPoint; // bottom endpoint (fixed)
} RainSlot;

// Short-lived arc crawling across the orb surface (ball-lightning texture)
typedef struct {
    int     rayId;
    bool    active;
    float   lifeLeft;
    Vector3 dirA, dirB;  // unit dirs from orb center — endpoints follow the orb
} SurfaceArc;

typedef enum { PHASE_INACTIVE = 0, PHASE_FLIGHT, PHASE_RAIN } ThunderOrbPhase;

typedef struct {
    ThunderOrbPhase phase;
    int             ownerAgentId;
    SkillParams     params;

    // flight
    Vector3 orbPos;
    Vector3 orbTarget;
    Vector3 flightStartPos;
    Vector3 flightDir;        // unit direction, fixed at cast time
    float   flightTargetDist; // distance from flightStartPos to orbTarget
    float   flightElapsed;
    float   flightTraveled;   // distance covered so far along flightDir
    Vector3 flightWindVel;    // damped velocity accumulated from the tunable wind ForceLayer
    int     flightRayIds[THUNDER_ORB_FLIGHT_RAYS];
    Vector3 flightRayDirs[THUNDER_ORB_FLIGHT_RAYS];
    float   flightRayLengths[THUNDER_ORB_FLIGHT_RAYS];
    float   flightRayScales[THUNDER_ORB_FLIGHT_RAYS];
    SurfaceArc surfArcs[THUNDER_ORB_SURFACE_ARCS];

    // rain
    Vector3   impactPos;
    float     rainTimer;
    Vector3   lastEnemyPos;
    float     lastEnemyRadius;
    RainSlot  rainSlots[THUNDER_ORB_RAIN_SLOTS];
    ArcBolt   arcBolts[THUNDER_ORB_ARC_SLOTS];
} ThunderOrbState;

static ThunderOrbState s = { 0 };

// ── helpers ───────────────────────────────────────────────────────────────

static float RandRange(float lo, float hi) {
    return lo + (GetRandomValue(0, 10000) / 10000.0f) * (hi - lo);
}

// Rebuilds a phase's tiny ForceField from its current tunable ForceLayer
// values right before use, so dragging flight_extra_curl/lift or
// rain_extra_curl/lift in the sandbox takes effect immediately (not baked in
// once at Init).
static void RebuildFlightParticleField(void) {
    ForceField_Clear(&s_flightParticleField);
    SkillForceMix_AddLayers(&s_flightForce, &s_flightParticleField);
}
static void RebuildRainSparkField(void) {
    ForceField_Clear(&s_rainSparkField);
    SkillForceMix_AddLayers(&s_rainForce, &s_rainSparkField);
}

// Fibonacci sphere — evenly distributes n points across the unit sphere
static Vector3 FibSphereDir(int i, int n) {
    float phi   = acosf(1.0f - 2.0f * (i + 0.5f) / (float)n);
    float theta = PI * (1.0f + sqrtf(5.0f)) * (float)i;
    return (Vector3){ sinf(phi)*cosf(theta), cosf(phi), sinf(phi)*sinf(theta) };
}

static void EmitOrbParticles(Vector3 pos) {
    RebuildFlightParticleField();
    ParticleConfig p = { 0 };
    p.colorStart = (Color){ 255, 240, 180, 220 };
    p.colorEnd   = (Color){ 180, 220, 255,   0 };
    p.radius     = s_orbParticleRadius;
    p.lifetime   = s_orbParticleLifetime;
    p.forceField = &s_flightParticleField;
    p.radiusCurve = &s_flightRadiusCurve;
    p.speedCurve = &s_flightSpeedParticleCurve;
    p.alphaCurve = &s_flightAlphaCurve;
    for (int i = 0; i < 3; i++) {
        float a = RandRange(0.0f, 2.0f * PI);
        float b = RandRange(-PI * 0.5f, PI * 0.5f);
        float spd = RandRange(s_orbParticleSpeedMin, s_orbParticleSpeedMax);
        p.position = pos;
        p.velocity = (Vector3){ cosf(b)*cosf(a)*spd, cosf(b)*sinf(a)*spd*0.4f, sinf(b)*spd };
        SpawnParticle(p);
    }
}

// Layered hot core: small white-hot center + wide violet halo, refreshed each
// frame with very short lifetimes so the orb reads as a solid ball of plasma.
static void EmitOrbCore(Vector3 pos) {
    RebuildFlightParticleField();
    ParticleConfig p = { 0 };
    p.position = pos;
    p.velocity = (Vector3){ 0 };
    p.forceField = &s_flightParticleField;
    p.radiusCurve = &s_flightRadiusCurve;
    p.speedCurve = &s_flightSpeedParticleCurve;
    p.alphaCurve = &s_flightAlphaCurve;

    p.colorStart = (Color){ 255, 255, 255, 255 };
    p.colorEnd   = (Color){ 210, 190, 255,   0 };
    p.radius     = s_orbRadius * s_orbCore1RadiusMult;
    p.lifetime   = s_orbCore1Lifetime;
    SpawnParticle(p);

    p.colorStart = (Color){ 150, 110, 255, 110 };
    p.colorEnd   = (Color){  70,  30, 200,   0 };
    p.radius     = s_orbRadius * s_orbCore2RadiusMult;
    p.lifetime   = s_orbCore2Lifetime;
    SpawnParticle(p);
}

static ProcRayConfig SurfArcConfig(void) {
    ProcRayConfig c = ProcRay_BoltLightningConfig();
    c.thickness      = 0.006f;
    c.amplitudeRatio = 0.30f; // wander off the chord — reads as crawling on the surface
    c.branchCount    = 0;
    c.taperTip       = 1.0f;
    return c;
}

static Vector3 RandUnitDir(void) {
    float a = RandRange(0.0f, 2.0f * PI);
    float c = RandRange(-1.0f, 1.0f);
    float sq = sqrtf(1.0f - c * c);
    return (Vector3){ sq * cosf(a), c, sq * sinf(a) };
}

static void RespawnSurfaceArc(SurfaceArc *arc) {
    arc->dirA     = RandUnitDir();
    arc->dirB     = RandUnitDir();
    arc->lifeLeft = RandRange(s_surfArcLifeMin, s_surfArcLifeMax);
    arc->rayId    = SpawnProcBolt(SurfArcConfig(), 0.5f);
    arc->active   = arc->rayId >= 0;
}

static void KillAllFlightRays(void) {
    for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) ProcRay_Kill(s.flightRayIds[i]);
    for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++)
        if (s.surfArcs[i].active) { ProcBolt_Kill(s.surfArcs[i].rayId); s.surfArcs[i].active = false; }
}

static void KillAllRainBolts(void) {
    for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++)
        if (s.rainSlots[i].active) { ProcBolt_Kill(s.rainSlots[i].rayId); s.rainSlots[i].active = false; }
    for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++)
        if (s.arcBolts[i].active) { ProcBolt_Kill(s.arcBolts[i].rayId); s.arcBolts[i].active = false; }
}

static void SpawnArcBurst(Vector3 groundPoint) {
    ProcRayConfig arcCfg = ProcRay_BoltLightningConfig();
    arcCfg.thickness      = 0.008f;
    arcCfg.amplitudeRatio = 0.25f; // more wandering for the short arcs
    arcCfg.branchCount    = 0;     // ground crawlers stay simple strands
    arcCfg.taperTip       = 0.3f;  // die out into the ground

    for (int k = 0; k < THUNDER_ORB_ARC_PER_STRIKE; k++) {
        // find a free arc slot
        int slot = -1;
        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++) {
            if (!s.arcBolts[i].active) { slot = i; break; }
        }
        if (slot < 0) break;

        float angle = RandRange(0.0f, 2.0f * PI);
        float dist  = RandRange(s_arcRadiusMin, s_arcRadiusMax);
        Vector3 tip = { groundPoint.x + cosf(angle)*dist, 0.0f, groundPoint.z + sinf(angle)*dist };

        s.arcBolts[slot].from     = groundPoint;
        s.arcBolts[slot].to       = tip;
        s.arcBolts[slot].lifeLeft = s_arcLife;
        s.arcBolts[slot].rayId    = SpawnProcBolt(arcCfg, 0.6f);
        s.arcBolts[slot].active   = true;
        ProcBolt_Update(s.arcBolts[slot].rayId, groundPoint, tip, 0.6f, 0.0f);
    }
}

// ── rain bolt ─────────────────────────────────────────────────────────────

// Fast upward sparks where the bolt hits the ground — sells the impact energy
static void EmitStrikeSparks(Vector3 groundPoint) {
    RebuildRainSparkField();
    ParticleConfig p = { 0 };
    p.colorStart = (Color){ 255, 250, 255, 255 };
    p.colorEnd   = (Color){ 140,  90, 255,   0 };
    p.radius     = s_sparkRadius;
    p.forceField = &s_rainSparkField;
    p.radiusCurve = &s_rainRadiusCurve;
    p.speedCurve = &s_rainSpeedCurve;
    p.alphaCurve = &s_rainAlphaCurve;
    int count = (int)s_sparkCount;
    for (int i = 0; i < count; i++) {
        float a   = RandRange(0.0f, 2.0f * PI);
        float out = RandRange(s_sparkOutSpeedMin, s_sparkOutSpeedMax);
        p.position = groundPoint;
        p.velocity = (Vector3){ cosf(a) * out, RandRange(s_sparkUpSpeedMin, s_sparkUpSpeedMax), sinf(a) * out };
        p.lifetime = RandRange(s_sparkLifetimeMin, s_sparkLifetimeMax);
        SpawnParticle(p);
    }
}

static void SpawnRainBolt(RainSlot *slot) {
    float angle = RandRange(0.0f, 2.0f * PI);
    float dist  = RandRange(0.0f, s_rainRadius);
    float px    = s.impactPos.x + cosf(angle) * dist;
    float pz    = s.impactPos.z + sinf(angle) * dist;

    // sky entry offset sideways so channels slant like real strikes
    slot->skyOrigin   = (Vector3){ px + RandRange(-0.45f, 0.45f),
                                   s_rainYOrigin,
                                   pz + RandRange(-0.45f, 0.45f) };
    slot->groundPoint = (Vector3){ px, 0.0f, pz };
    slot->lifeInit    = RandRange(s_rainBoltLifetimeMin, s_rainBoltLifetimeMax);
    slot->aliveTimer  = slot->lifeInit;
    slot->active      = true;

    slot->rayId = SpawnProcBolt(ProcRay_BoltLightningConfig(), s_rainBoltScale);
    // generate waypoints now — the bolt is drawn this same frame
    ProcBolt_Update(slot->rayId, slot->skyOrigin, slot->groundPoint,
                    s_rainBoltScale, 0.0f);
    ProcBolt_SetBrightness(slot->rayId, 1.9f); // leader flash frame
    EmitStrikeSparks(slot->groundPoint);

    // damage enemy if bolt lands nearby
    if (Vector3Distance(slot->groundPoint, s.lastEnemyPos) < s.lastEnemyRadius + 0.6f) {
        Entity_ApplyAoEDamage(slot->groundPoint, 0.6f, THUNDER_ORB_BASE_RAIN_DMG, s_rainStrikeKnockback);
        AddFloatingText(s.lastEnemyPos, "12", (Color){ 200, 200, 255, 255 }, 20.0f, 0.6f);
        AddKnockbackToEnemy(Vector3Scale(
            Vector3Normalize(Vector3Subtract(s.lastEnemyPos, slot->groundPoint)), s_rainStrikeKnockback));
    }

    // Flash at sky entry
    VFXLight_Spawn(slot->skyOrigin, (Color){ 200, 200, 255, 255 },
                   1.2f, 0.08f, VFX_PRIORITY_HIGH_ULTIMATE);
    // decal + light at ground strike point (no SpawnImpactEffect — avoids CameraFX_Shake)
    SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, slot->groundPoint, 0.35f, 3.5f);
    VFXLight_Spawn(slot->groundPoint, ELEMENT_COLOR_METAL,
                   s_rainLightRadius, slot->aliveTimer, VFX_PRIORITY_LOW);

    SpawnArcBurst(slot->groundPoint);
}

// ── impact transition ─────────────────────────────────────────────────────

static void TriggerImpact(Vector3 pos) {
    s.impactPos = pos;
    float impactDmg = s.params.damage > 0.0f ? s.params.damage : THUNDER_ORB_BASE_IMPACT_DMG;
    Entity_ApplyAoEDamage(pos, s_rainRadius * 0.5f, impactDmg, 2.2f);
    SpawnImpactEffect(pos, EFFECT_PRESET_LIGHTNING_IMPACT, 1.5f);
    SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, pos, 1.2f, 6.0f);
    VFXLight_Spawn(pos, (Color){ 200, 200, 255, 255 },
                   s_impactFlashRadius, s_impactFlashLifetime,
                   VFX_PRIORITY_HIGH_ULTIMATE);

    KillAllFlightRays();

    s.phase     = PHASE_RAIN;
    s.rainTimer = 0.0f;
    for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++) {
        s.rainSlots[i].active   = false;
        s.rainSlots[i].gapTimer = RandRange(0.0f, 0.3f); // stagger first strikes
        s.rainSlots[i].rayId    = -1;
    }
}

// ── public API ────────────────────────────────────────────────────────────

void InitThunderOrbSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s = (ThunderOrbState){ 0 };

    // One-time migration: the old scheme persisted a single "flight_speed"
    // key; the new curve scheme persists "flight_speed_t0".."_t4" instead, so
    // an existing .tuning file's old key would otherwise be silently ignored
    // and any prior sandbox tuning lost. Seed the curve flat at whatever the
    // legacy key says (falls back to the original 3.8 default if absent).
    float legacyFlightSpeed = 3.8f;
    {
        const char *legacyKey = "flight_speed";
        Tuning_LoadFloatsFromPath("skills/metal/thunder_orb_skill/thunder_orb_skill.tuning",
                                   &legacyKey, &legacyFlightSpeed, 1);
    }
    SkillCurve_SetConstant(&s_flightSpeedCurve, legacyFlightSpeed);

    // Seed every per-phase radius/speed/alpha curve flat at 1.0 (no change
    // from today's behavior) before building tunable entries below.
    SkillCurve_SetConstant(&s_flightRadiusCurve, 1.0f);
    SkillCurve_SetConstant(&s_flightSpeedParticleCurve, 1.0f);
    SkillCurve_SetConstant(&s_flightAlphaCurve, 1.0f);
    SkillCurve_SetConstant(&s_rainRadiusCurve, 1.0f);
    SkillCurve_SetConstant(&s_rainSpeedCurve, 1.0f);
    SkillCurve_SetConstant(&s_rainAlphaCurve, 1.0f);

    int skillIndex = Skill_GetIndexByName("THUNDER_ORB");
    // Built as a sequence of assignments (not a single literal) so each
    // phase's named entries stay contiguous with that phase's force slots —
    // see the identical pattern/rationale in fire_skill.c's InitFireSkill.
    static SkillTunableEntry s_thunderOrbTunables[THUNDER_ORB_TUNABLE_COUNT];
    int tn = 0;

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_max_duration", &s_flightMaxDuration, 0.3f, 8.0f, 3.0f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_max_range", &s_flightMaxRange, 1.0f, 30.0f, 15.0f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_speed", NULL, 0.5f, 15.0f, 3.8f, "flight", &s_flightSpeedCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_radius", &s_orbRadius, 0.01f, 0.5f, 0.12f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_len_min", &s_rayLenMin, 0.02f, 2.0f, 0.24f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_len_max", &s_rayLenMax, 0.05f, 2.0f, 0.55f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_scale_min", &s_rayScaleMin, 0.0f, 2.0f, 0.55f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_scale_max", &s_rayScaleMax, 0.0f, 2.0f, 1.0f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"surf_arc_life_min", &s_surfArcLifeMin, 0.01f, 1.0f, 0.06f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"surf_arc_life_max", &s_surfArcLifeMax, 0.01f, 1.0f, 0.14f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_radius", &s_orbParticleRadius, 0.0f, 0.3f, 0.035f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_lifetime", &s_orbParticleLifetime, 0.02f, 1.0f, 0.18f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_speed_min", &s_orbParticleSpeedMin, 0.0f, 5.0f, 0.6f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_speed_max", &s_orbParticleSpeedMax, 0.0f, 5.0f, 1.4f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core1_radius_mult", &s_orbCore1RadiusMult, 0.0f, 5.0f, 0.7f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core1_lifetime", &s_orbCore1Lifetime, 0.02f, 1.0f, 0.09f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core2_radius_mult", &s_orbCore2RadiusMult, 0.0f, 5.0f, 1.6f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core2_lifetime", &s_orbCore2Lifetime, 0.02f, 1.0f, 0.13f, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightRadiusCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightSpeedParticleCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "flight", &s_flightAlphaCurve};
    tn += SkillForceMix_MakeTunables(&s_flightForce, "flight_force_", "flight", &s_thunderOrbTunables[tn]);

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_knockback", &s_impactKnockback, 0.0f, 10.0f, 2.8f, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_flash_radius", &s_impactFlashRadius, 0.1f, 5.0f, 1.8f, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_flash_lifetime", &s_impactFlashLifetime, 0.02f, 2.0f, 0.15f, "impact"};

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_duration", &s_rainDuration, 0.5f, 15.0f, 5.0f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_y_origin", &s_rainYOrigin, 0.5f, 10.0f, 4.2f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_scale", &s_rainBoltScale, 0.1f, 5.0f, 1.2f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_lifetime_min", &s_rainBoltLifetimeMin, 0.02f, 2.0f, 0.28f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_lifetime_max", &s_rainBoltLifetimeMax, 0.02f, 2.0f, 0.5f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_strike_knockback", &s_rainStrikeKnockback, 0.0f, 10.0f, 0.8f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_radius", &s_rainRadius, 0.1f, 5.0f, 1.3f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_light_radius", &s_rainLightRadius, 0.05f, 3.0f, 0.8f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_radius", &s_sparkRadius, 0.0f, 0.2f, 0.022f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_count", &s_sparkCount, 0.0f, 40.0f, 10.0f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_out_speed_min", &s_sparkOutSpeedMin, 0.0f, 5.0f, 0.3f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_out_speed_max", &s_sparkOutSpeedMax, 0.0f, 5.0f, 1.3f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_up_speed_min", &s_sparkUpSpeedMin, 0.0f, 8.0f, 1.4f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_up_speed_max", &s_sparkUpSpeedMax, 0.0f, 8.0f, 3.2f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_lifetime_min", &s_sparkLifetimeMin, 0.02f, 2.0f, 0.25f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_lifetime_max", &s_sparkLifetimeMax, 0.02f, 2.0f, 0.5f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_life", &s_arcLife, 0.02f, 2.0f, 0.12f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_radius_min", &s_arcRadiusMin, 0.0f, 2.0f, 0.15f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_radius_max", &s_arcRadiusMax, 0.0f, 2.0f, 0.45f, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "rain", &s_rainRadiusCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "rain", &s_rainSpeedCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "rain", &s_rainAlphaCurve};
    tn += SkillForceMix_MakeTunables(&s_rainForce, "rain_force_", "rain", &s_thunderOrbTunables[tn]);

    SkillTunables_LoadPersisted(
        "skills/metal/thunder_orb_skill/thunder_orb_skill.tuning",
        s_thunderOrbTunables, tn);
    RegisterSkillTunables(skillIndex, s_thunderOrbTunables, tn);
}

void CastThunderOrbSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    if (s.phase == PHASE_FLIGHT) KillAllFlightRays();
    if (s.phase == PHASE_RAIN)   KillAllRainBolts();

    s = (ThunderOrbState){ 0 };
    s.phase        = PHASE_FLIGHT;
    s.ownerAgentId = agentId;
    s.params       = params;
    s.orbPos       = startPos;
    s.orbTarget    = target;

    // Fixed direction/target-distance at cast time — flight speed is driven
    // by SkillHelper_StepCurveFlight over elapsed TIME (s_flightSpeedCurve),
    // never over fraction-of-distance-to-target, and is hard-capped by
    // s_flightMaxDuration/s_flightMaxRange so a far-away target can't make
    // the orb fly farther than its own limit.
    s.flightStartPos   = startPos;
    Vector3 toTarget    = Vector3Subtract(target, startPos);
    s.flightTargetDist = Vector3Length(toTarget);
    s.flightDir = (s.flightTargetDist > 0.0001f)
                      ? Vector3Scale(toTarget, 1.0f / s.flightTargetDist)
                      : (Vector3){ 0, 0, 1 };

    // Rays spread evenly in 3D (Fibonacci sphere), each with random length+scale
    for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) {
        s.flightRayDirs[i]    = FibSphereDir(i, THUNDER_ORB_FLIGHT_RAYS);
        s.flightRayLengths[i] = RandRange(s_rayLenMin, s_rayLenMax);
        s.flightRayScales[i]  = RandRange(s_rayScaleMin, s_rayScaleMax);
        s.flightRayIds[i]     = SpawnProcRay(ProcRay_LightningConfig(), s.flightRayScales[i]);
        ProcRay_SetPhase(s.flightRayIds[i], (float)i * (2.0f * PI / (float)THUNDER_ORB_FLIGHT_RAYS));
    }
    for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++) RespawnSurfaceArc(&s.surfArcs[i]);
}

void UpdateThunderOrbSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (s.phase == PHASE_FLIGHT) {
        // collision with enemy
        if (Vector3Distance(s.orbPos, enemyPos) < s_orbRadius + enemyRadius) {
            if (GetRandomValue(1, 100) <= 25)
                AddFloatingText(enemyPos, "CRIT!", YELLOW, 28.0f, 1.0f);
            else
                AddFloatingText(enemyPos, "HIT", (Color){ 200, 200, 255, 255 }, 22.0f, 0.7f);
            AddKnockbackToEnemy(Vector3Scale(
                Vector3Normalize(Vector3Subtract(enemyPos, s.orbPos)), s_impactKnockback));
            TriggerImpact(s.orbPos);
            return;
        }

        bool arrived = false;
        SkillHelper_StepCurveFlight(&s_flightSpeedCurve, s.flightElapsed, dt,
                                     s_flightMaxDuration, s_flightMaxRange, s.flightTargetDist,
                                     &s.flightTraveled, &arrived);
        s.flightElapsed += dt;
        s.orbPos = Vector3Add(s.flightStartPos, Vector3Scale(s.flightDir, s.flightTraveled));

        // Optional tunable extra-force mix (every component defaults to 0
        // strength = no effect) — nudges the orb off its straight path.
        RebuildFlightParticleField();
        if (s_flightParticleField.layerCount > 0) {
            Vector3 accel = ForceField_Evaluate(&s_flightParticleField, s.orbPos, s.flightWindVel,
                                                 s.flightElapsed, (Vector3){ 0 }, (Vector3){ 0 });
            s.flightWindVel = Vector3Scale(Vector3Add(s.flightWindVel, Vector3Scale(accel, dt)), 0.9f);
            s.orbPos = Vector3Add(s.orbPos, Vector3Scale(s.flightWindVel, dt));
        }

        if (arrived) {
            TriggerImpact(s.orbPos);
            return;
        }

        // flickering light — plasma is never steady. Radius cut ~4x from a
        // straight ÷100 (was 0.7-1.1) — at this light's continuous
        // every-frame respawn, bloom amplification made even that "already
        // rescaled" radius read as a huge screen-covering blob at typical
        // camera distance in the new 18m arena.
        VFXLight_Spawn(s.orbPos, (Color){ 220, 230, 255, 200 },
                       RandRange(0.15f, 0.25f), 0.07f, VFX_PRIORITY_LOW);
        EmitOrbCore(s.orbPos);
        EmitOrbParticles(s.orbPos);

        for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) {
            ProcRay_Update(s.flightRayIds[i], s.orbPos,
                           s.flightRayDirs[i], s.flightRayLengths[i],
                           s.flightRayScales[i], dt);
            ProcRay_SetBrightness(s.flightRayIds[i], RandRange(0.65f, 1.15f));
        }

        // surface arcs crawl on the orb shell, respawning at new spots
        for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++) {
            SurfaceArc *arc = &s.surfArcs[i];
            if (arc->active) {
                arc->lifeLeft -= dt;
                if (arc->lifeLeft <= 0.0f) {
                    ProcBolt_Kill(arc->rayId);
                    arc->active = false;
                }
            }
            if (!arc->active) RespawnSurfaceArc(arc);
            if (arc->active) {
                float r = s_orbRadius * 0.95f;
                Vector3 a = Vector3Add(s.orbPos, Vector3Scale(arc->dirA, r));
                Vector3 b = Vector3Add(s.orbPos, Vector3Scale(arc->dirB, r));
                ProcBolt_Update(arc->rayId, a, b, 0.5f, dt);
            }
        }

    } else if (s.phase == PHASE_RAIN) {
        s.lastEnemyPos    = enemyPos;
        s.lastEnemyRadius = enemyRadius;
        s.rainTimer += dt;
        if (s.rainTimer >= s_rainDuration) {
            KillAllRainBolts();
            s.phase = PHASE_INACTIVE;
            return;
        }

        Entity_ApplyAoEDamage(s.impactPos, s_rainRadius,
                               s.params.damage * 0.08f * dt,
                               0.6f);

        for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++) {
            RainSlot *slot = &s.rainSlots[i];
            if (slot->active) {
                slot->aliveTimer -= dt;
                if (slot->aliveTimer <= 0.0f) {
                    ProcBolt_Kill(slot->rayId);
                    slot->active   = false;
                    slot->gapTimer = RandRange(0.05f, 0.15f);
                } else {
                    ProcBolt_Update(slot->rayId, slot->skyOrigin, slot->groundPoint,
                                    s_rainBoltScale, dt);
                    // strike lifecycle: blinding flash ~70ms, then flickering afterglow decay
                    float age = slot->lifeInit - slot->aliveTimer;
                    float b   = (age < 0.07f)
                                    ? 1.9f
                                    : (0.45f + 0.75f * (slot->aliveTimer / slot->lifeInit))
                                          * RandRange(0.8f, 1.1f);
                    ProcBolt_SetBrightness(slot->rayId, b);
                }
            } else {
                slot->gapTimer -= dt;
                if (slot->gapTimer <= 0.0f) SpawnRainBolt(slot);
            }
        }

        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++) {
            ArcBolt *arc = &s.arcBolts[i];
            if (!arc->active) continue;
            arc->lifeLeft -= dt;
            if (arc->lifeLeft <= 0.0f) {
                ProcBolt_Kill(arc->rayId);
                arc->active = false;
            } else {
                ProcBolt_Update(arc->rayId, arc->from, arc->to, 0.6f, dt);
                ProcBolt_SetBrightness(arc->rayId, arc->lifeLeft / s_arcLife);
            }
        }
    }
}

void DrawThunderOrbSkill(void) {
    if (s.phase == PHASE_FLIGHT) {
        for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++)
            ProcRay_Draw(s.flightRayIds[i], camera);
        for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++)
            if (s.surfArcs[i].active)
                ProcBolt_Draw(s.surfArcs[i].rayId, camera);
    } else if (s.phase == PHASE_RAIN) {
        for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++)
            if (s.rainSlots[i].active)
                ProcBolt_Draw(s.rainSlots[i].rayId, camera);
        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++)
            if (s.arcBolts[i].active)
                ProcBolt_Draw(s.arcBolts[i].rayId, camera);
    }
}

void UnloadThunderOrbSkill(void) {
    if (s.phase == PHASE_FLIGHT) KillAllFlightRays();
    if (s.phase == PHASE_RAIN)   KillAllRainBolts();
    s = (ThunderOrbState){ 0 };
}

bool IsThunderOrbSkillCoiling(void) { return false; }

int GetThunderOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    if (s.phase != PHASE_FLIGHT || maxProjectiles < 1) return 0;
    outProjectiles[0].position = s.orbPos;
    outProjectiles[0].radius   = s_orbRadius;
    outProjectiles[0].active   = true;
    return 1;
}

void DeactivateThunderOrbProjectile(int index) {
    if (index == 0 && s.phase == PHASE_FLIGHT) TriggerImpact(s.orbPos);
}

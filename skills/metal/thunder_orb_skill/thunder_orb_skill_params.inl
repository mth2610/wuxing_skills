// Included at file scope in thunder_orb_skill.c — all tunable state for thunder_orb.
// Static declarations here have internal linkage (same as if in thunder_orb_skill.c directly).
// Do not #include from any other translation unit.

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
static SkillCurve s_flightRadiusCurve, s_flightSpeedParticleCurve, s_flightAlphaCurve, s_flightEmissiveCurve;
static SkillCurve s_rainRadiusCurve, s_rainSpeedCurve, s_rainAlphaCurve, s_rainEmissiveCurve;

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

// Decal params — impact (orb hits ground) and rain-strike (each bolt hits ground)
static float s_impactDecalScaleStart = 0.35f, s_impactDecalScaleEnd = 0.35f;
static float s_impactDecalLifetime   = 3.5f;
static float s_impactDecalRotSpeed   = 0.0f;
static float s_impactDecalYOffset    = 0.02f;

static float s_rainDecalScaleStart   = 1.2f,  s_rainDecalScaleEnd   = 1.2f;
static float s_rainDecalLifetime     = 6.0f;
static float s_rainDecalRotSpeed     = 0.0f;
static float s_rainDecalYOffset      = 0.02f;

// 12 original named tunables + 30 shape/feel-range tunables - 2 old
// single-float alpha entries + 2 phases x 3 over-lifetime curves
// (radius/speed/alpha) + 2 phases x 1 force mix x
// SKILL_FORCE_MIX_TUNABLE_COUNT(29) + 10 decal tunables
// = 12 + 30 - 2 + 6 + 58 + 10 = 114
#define THUNDER_ORB_TUNABLE_COUNT 116

// Included at file scope in fire_skill.c — all tunable state for FireSkill.
// Static declarations here have internal linkage (same as if in fire_skill.c directly).
// Do not #include from any other translation unit.

// --- ForceFields (rebuilt from tunables before each use, not baked at Init) ---
static ForceField s_fireImpactField;   // tia lửa va chạm: rơi xuống + drag 2.5
static ForceField s_fireDisperseField; // quầng lửa bốc: curl + bốc lên + drag 3.5
static ForceField s_flameBodyField;    // thân rồng lửa (core): curl nhẹ + bốc lên + drag 9.5
static ForceField s_flameAuraField;    // thân rồng lửa (aura): curl nhẹ + bốc lên + drag 5.2
static ForceField s_fireBurstField;    // tia lửa bắn khi cast: chỉ drag 2.5

// --- Physics (m/s², compare against real gravity 9.81f) ---
static float s_fireImpactGravity  = 1.8f;  // embers falling accel
static float s_fireImpactDrag     = 2.5f;  // drag rate (1/s), not spatial
static float s_fireDisperseRise   = 2.6f;  // outward flare buoyancy accel
static float s_fireDisperseCurl   = 0.5f;  // curl swirl accel
static float s_flameBodyCurl      = 1.2f;  // dragon-body curl accel
static float s_flameBodyRise      = 0.8f;  // dragon-body buoyancy accel

// --- Flight ---
// Travel speed is a SkillCurve sampled at headProgress [0,1], not real-world distance.
static SkillCurve s_fireTravelSpeedCurve;
static float s_fireFlightMaxDuration = 3.0f;  // hard cap on time-to-reach headProgress==1.0 (s)
static float s_flameSpawnRate        = 750.0f; // flame particles spawned/s along body

// --- Size/radius (meters, before *sizeScale) ---
static float s_castFlashRadius      = 0.8f;    // flash at cast origin
static float s_burstRadiusMax       = 0.065f;  // cast burst spark radius (upper bound)
static float s_impactFlash1Radius   = 0.6f;    // impact static-core flash, primary
static float s_impactFlash2Radius   = 0.33f;   // impact static-core flash, secondary
static float s_impactSparkRadiusMax = 0.022f;  // falling-ember spark radius (upper bound)
static float s_disperseRadiusMax    = 0.065f;  // outward flare ember radius (upper bound)
static float s_ribbonWidthMax       = 0.4f;    // dragon-body ribbon width at its widest (tail)
static float s_dragonHeadScale      = 0.0012f; // pixel-to-world-meters ratio for the head billboard

// --- Per-phase over-lifetime curves & force mixes ---
// Curves: sampled at t=age/lifetime; multiplies radius/speed/alpha per particle per frame.
// Seeded flat at 1.0 (no-op) — shape them in the sandbox to get bloom/pop/fade effects.
// ForceMixes: all 8 ForceTypes simultaneously available, each defaults to 0 (no effect).
static SkillCurve    s_castRadiusCurve,    s_castSpeedCurve,    s_castAlphaCurve;
static SkillCurve    s_flyRadiusCurve,     s_flySpeedCurve,     s_flyAlphaCurve;
static SkillCurve    s_impactRadiusCurve,  s_impactSpeedCurve,  s_impactAlphaCurve;
static SkillCurve    s_disperseRadiusCurve, s_disperseSpeedCurve, s_disperseAlphaCurve;
static SkillForceMix s_castForce, s_flyForce, s_impactForce, s_disperseForce;

// --- Cast phase ---
static float s_castBurstCountMin    = 8.0f,   s_castBurstCountMax    = 14.0f;
static float s_castBurstSpeedXZMin  = -2.0f,  s_castBurstSpeedXZMax  = 3.0f;  // m/s
static float s_castBurstSpeedYMin   = 1.0f,   s_castBurstSpeedYMax   = 4.0f;  // m/s
static float s_castBurstRadiusMin   = 0.025f;
static float s_castBurstLifetimeMin = 0.3f,   s_castBurstLifetimeMax = 0.8f;
static float s_castFlashLifetime    = 0.25f;

// --- Fly phase ---
static float s_flyCoreRadiusMin     = 0.03f,  s_flyCoreRadiusMax     = 0.06f;
static float s_flyCoreRadiusRandMin = 0.8f,   s_flyCoreRadiusRandMax = 1.2f;
static float s_flyCoreLifetimeMin   = 0.2f,   s_flyCoreLifetimeMax   = 0.4f;
static float s_flyCoreRadiusMult    = 1.8f;   // core particle radius = taper-rad * this
static float s_flyAuraRadiusMult    = 4.8f;   // aura particle radius = taper-rad * this
static float s_flyAuraLifetimeMin   = 0.35f,  s_flyAuraLifetimeMax   = 0.65f;
static float s_flyOutwardSpeedMin   = 0.1f,   s_flyOutwardSpeedMax   = 0.4f;  // m/s, before *sizeScale
static float s_flyBackwardSpeedMin  = 1.6f,   s_flyBackwardSpeedMax  = 3.4f;  // m/s, before *sizeScale

// --- Impact phase ---
static float s_impactSparkCountMin    = 12.0f, s_impactSparkCountMax    = 18.0f;
static float s_impactSparkSpeedMin    = 1.6f,  s_impactSparkSpeedMax    = 4.2f; // m/s
static float s_impactSparkRadiusMin   = 0.008f;
static float s_impactSparkLifetimeMin = 0.3f,  s_impactSparkLifetimeMax = 0.7f;
static float s_impactFlash1Lifetime   = 0.40f;
static float s_impactFlash2Lifetime   = 0.25f;

// --- Disperse phase ---
static float s_disperseCountMin    = 14.0f, s_disperseCountMax    = 22.0f;
static float s_disperseSpeedMin    = 0.8f,  s_disperseSpeedMax    = 2.6f;  // m/s, horizontal
static float s_disperseRadiusMin   = 0.025f;
static float s_disperseLifetimeMin = 0.6f,  s_disperseLifetimeMax = 1.3f;

// 21 original named tunables + 40 shape/feel-range tunables (count/speed/
// lifetime/radius-min per spawn site) - 4 old single-float alpha entries
// + 4 phases x 3 over-lifetime curves (radius/speed/alpha) + 4 phases x 1
// force mix x SKILL_FORCE_MIX_TUNABLE_COUNT(29) = 21 + 40 - 4 + 12 + 116 = 185
#define FIRE_SKILL_TUNABLE_COUNT 185

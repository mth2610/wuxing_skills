// Included at file scope in tube_skill.c — all tunable state for tube_skill.
// Static declarations here have internal linkage (same as if in tube_skill.c directly).
// Do not #include from any other translation unit.

// Sandbox-tunable physics + size knobs. Real-world-scaled: 1 unit = 1 meter.
// Loaded from skills/water/water_stream/tube_skill.tuning on init if present.
static float s_tubeBaseRadius = 0.12f;   // tube visual/collision radius (m)

// Splash force field (water hits surface)
static float s_splashGravity  = 6.5f;   // downward gravity for splash particles (m/s²)
static float s_splashNoise    = 0.25f;  // Perlin noise strength for splash turbulence
static float s_splashDrag     = 3.0f;   // splash drag coefficient (unitless)

// Mist force field (head-of-tube ambient spray)
static float s_mistGravity    = 3.25f;  // downward gravity for mist particles (m/s²)
static float s_mistNoise      = 0.15f;  // Perlin noise strength for mist drift
static float s_mistDrag       = 2.0f;   // mist drag coefficient (unitless)

// Impact VFX
static float s_impactDistortRadius   = 0.85f;  // screen-distort wave radius (m)
static float s_impactDistortStrength = 0.7f;   // distort intensity (unitless 0..1)
static float s_impactDistortLife     = 0.6f;   // distort lifetime (s)
static float s_impactDistortSpeed    = 1.5f;   // distort wave expand speed (m/s)

static float s_impactDecalScale  = 0.03f;  // caustic decal radius (m)
static float s_impactDecalLife   = 4.0f;   // decal lifetime (s)

static float s_impactLightRadius = 0.55f;  // flash light radius (m)
static float s_impactLightLife   = 0.5f;   // flash light lifetime (s)

// Impact particle burst
static float s_burstSpeedMin   = 1.2f;   // particle burst min speed (m/s)
static float s_burstSpeedMax   = 2.5f;   // particle burst max speed (m/s)
static float s_burstRadiusMin  = 0.03f;  // particle min radius (m)
static float s_burstRadiusMax  = 0.08f;  // particle max radius (m)
static float s_burstLifeMin    = 0.6f;   // particle min lifetime (s)
static float s_burstLifeMax    = 1.2f;   // particle max lifetime (s)
static float s_burstUpwardBias = 1.0f;   // upward velocity bias (m/s)

// Head mist particles
static float s_mistVelXZ       = 0.2f;   // mist XZ spread half-range (m/s)
static float s_mistVelYMax     = 0.5f;   // mist upward velocity max (m/s)
static float s_mistRadiusMin   = 0.02f;  // mist min radius (m)
static float s_mistRadiusMax   = 0.05f;  // mist max radius (m)
static float s_mistLifeMin     = 0.2f;   // mist min lifetime (s)
static float s_mistLifeMax     = 0.5f;   // mist max lifetime (s)

// Per-phase over-lifetime curves — flat 1.0 (no-op until shaped in sandbox)
static SkillCurve s_mistRadiusCurve, s_mistSpeedCurve, s_mistAlphaCurve;
static SkillCurve s_mistEmissiveCurve;

// Tunable extra-force mix for the mist particles
static SkillForceMix s_mistForce;
static ForceField    s_mistFieldActive; // rebuilt from s_mistForce + mist gravity stack

// mist scalars(11) + mist curves(3) + mist force mix(29)
// + splash scalars(3) + impact scalars(15) = 61
#define TUBE_TUNABLE_COUNT 62

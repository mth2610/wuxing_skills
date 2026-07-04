// Included at file scope in water_sphere_skill.c — all tunable state for water_sphere.
// Static declarations here have internal linkage (same as if in water_sphere_skill.c directly).
// Do not #include from any other translation unit.

// Sandbox-tunable physics + size knobs. Real-world-scaled: 1 unit = 1 meter.
// Loaded from skills/water/water_sphere_skill/water_sphere_skill.tuning on init
// if present.
static float s_sphereBaseRadius = 0.15f;  // visual + collision radius (m)
static float s_sphereSpeed      = 1.8f;   // projectile travel speed (m/s)
static float s_shakeEnable      = 1.0f;   // 1=on, 0=off

// Splash force field components
static float s_splashGravity    = 6.0f;   // downward gravity for splash particles (m/s²)
static float s_splashNoise      = 0.45f;  // Perlin noise strength for splash turbulence
static float s_splashDrag       = 3.0f;   // drag coefficient (unitless)

// Impact VFX
static float s_impactDistortRadius   = 0.9f;   // screen-distort wave radius (m)
static float s_impactDistortStrength = 0.8f;   // distort intensity (unitless 0..1)
static float s_impactDistortLife     = 0.6f;   // distort lifetime (s)
static float s_impactDistortSpeed    = 1.6f;   // distort wave expand speed (m/s)

static float s_impactDecalScale  = 0.05f;  // caustic decal radius (m)
static float s_impactDecalLife   = 3.5f;   // decal lifetime (s)

static float s_impactLightRadius = 0.75f;  // flash light radius (m)
static float s_impactLightLife   = 0.5f;   // flash light lifetime (s)

// Impact particle burst
static float s_burstSpeedMin    = 1.5f;   // particle burst min speed (m/s)
static float s_burstSpeedMax    = 3.2f;   // particle burst max speed (m/s)
static float s_burstRadiusMin   = 0.04f;  // particle min radius (m)
static float s_burstRadiusMax   = 0.12f;  // particle max radius (m)
static float s_burstLifeMin     = 0.5f;   // particle min lifetime (s)
static float s_burstLifeMax     = 1.0f;   // particle max lifetime (s)
static float s_burstUpwardBias  = 1.2f;   // upward velocity bias (m/s)

// Flight drip particles
static float s_dripVelXZ        = 0.2f;   // drip XZ spread half-range (m/s)
static float s_dripVelY         = -0.5f;  // drip downward velocity (m/s)
static float s_dripRadiusMin    = 0.03f;  // drip min radius (m)
static float s_dripRadiusMax    = 0.06f;  // drip max radius (m)
static float s_dripLifetime     = 0.4f;   // drip lifetime (s)

// Flight VFX light
static float s_flightLightRadiusMult = 4.0f; // light radius = base_radius * mult

// Impact arrival threshold
static float s_arrivalRadius    = 0.2f;   // detonation distance threshold (m)

// Per-phase over-lifetime curves — flat 1.0 (no-op until shaped in sandbox)
static SkillCurve s_flightRadiusCurve, s_flightSpeedCurve, s_flightAlphaCurve;
static SkillCurve s_flightEmissiveCurve;

// Tunable extra-force mix for flight-phase drip particles
static SkillForceMix s_flightForce;
static ForceField    s_dripField; // rebuilt from s_flightForce + gravity stack each frame

// flight scalars(9) + flight curves(4) + flight force mix(29)
// + splash scalars(3) + impact scalars(15) = 60
#define WATER_SPHERE_TUNABLE_COUNT 61

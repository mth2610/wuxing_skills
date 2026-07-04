// Included at file scope in hoa_long_phong_ba_skill.c — all tunable state for hoa_long_phong_ba.
// Static declarations here have internal linkage (same as if in hoa_long_phong_ba_skill.c directly).
// Do not #include from any other translation unit.

// Real-world-scaled (1 unit = 1 m).  All spatial magic numbers below were
// divided by 100 from the original 1cm-scale values.

// Pool-size and geometry constants — not tunable (would need dynamic alloc).
static float s_baseRadius           = 0.065f;  // orb visual/collision radius (m)

// Gather phase
static float s_gatherRDistMin       = 0.06f;   // particle spawn ring inner radius (m)
static float s_gatherRDistMax       = 0.18f;   // particle spawn ring outer radius (m)
static float s_gatherHeightRange    = 0.15f;   // random height band (m)
static float s_gatherHeightBase     = 0.05f;   // base height above ground (m)
static float s_gatherSpeed          = 0.35f;   // gather particle speed toward center (m/s)
static float s_gatherParticleRadius = 0.024f;  // gather particle radius (m) [1.5+0.6]*4/100

// Travel phase
static float s_travelBackSpeed      = 0.30f;   // back-trail velocity magnitude (m/s)
static float s_travelWindStrength   = 0.80f;   // wind layer strength in travelField (m/s²)
static float s_travelJitter         = 0.15f;   // particle position/velocity jitter (m)
static float s_travelParticleRadius = 0.028f;  // travel particle radius (m) [2.0+0.8]*4/100
static float s_sparkBackSpeed       = 0.65f;   // accent spark back-velocity (m/s)
static float s_sparkJitter          = 0.20f;   // accent spark velocity jitter (m/s)
static float s_sparkUpBias          = 0.12f;   // downward bias on accent sparks (m/s)

// Light/distort
static float s_castLightRadius      = 0.40f;   // gather-phase VFXLight radius (m)
static float s_impactLightRadius    = 0.90f;   // impact VFXLight radius (m)
static float s_impactDistortRadius  = 0.90f;   // ScreenDistort world radius (m)

// Impact & AoE
static float s_aoeRadius            = 0.48f;   // ApplyAoEDamage radius (m)

// Geyser impact burst
static float s_geyserSpinSpeedMin   = 0.40f;   // geyser spin speed min (m/s)  [40/100]
static float s_geyserSpinSpeedMax   = 0.80f;   // geyser spin speed max (m/s)  [80/100]
static float s_geyserUpSpeedMin     = 0.75f;   // geyser up speed min  (m/s)  [75/100]
static float s_geyserUpSpeedMax     = 1.75f;   // geyser up speed max  (m/s) [175/100]
static float s_geyserPosYRange      = 0.05f;   // geyser particle spawn y offset (m)
static float s_geyserParticleRadius = 0.05f;   // geyser particle radius (m) [3.5+1.5]*4/100 avg

// Impact spark
static float s_impactSparkSpeedMin  = 0.55f;   // impact spark speed min (m/s)  [55/100]
static float s_impactSparkSpeedMax  = 1.45f;   // impact spark speed max (m/s) [145/100]
static float s_impactSparkUpBase    = 0.35f;   // up-velocity base on sparks (m/s) [35/100]

// Aftermath (continuous vortex)
static float s_aftermathUpSpeedMin  = 0.60f;   // aftermath up speed min (m/s)
static float s_aftermathUpSpeedMax  = 1.50f;   // aftermath up speed max (m/s)
static float s_aftermathSpinMin     = 0.25f;   // aftermath spin speed min (m/s)
static float s_aftermathSpinMax     = 0.60f;   // aftermath spin speed max (m/s)
static float s_aftermathRingRadius  = 0.13f;   // aftermath spawn ring max radius (m)
static float s_aftermathYJitter     = 0.02f;   // aftermath spawn y jitter (m)
static float s_aftermathParticleRadius = 0.038f; // aftermath particle radius (m)

// Over-lifetime curves (flat defaults = no change from base behavior)
static SkillCurve s_travelRadiusCurve, s_travelSpeedCurve, s_travelAlphaCurve, s_travelEmissiveCurve;
static SkillCurve s_geyserRadiusCurve, s_geyserSpeedCurve, s_geyserAlphaCurve, s_geyserEmissiveCurve;

// Force mixes — one per logical phase (merged into base fields each frame via RebuildXxxField)
static SkillForceMix s_travelForceMix;
static SkillForceMix s_geyserForceMix;

// Geyser field base layer params — tunable, rebuilt each frame
static float s_geyserVortexStrength = 11.0f;  // vortex tangential accel (m/s²)
static float s_geyserVortexRadius   = 1.6f;   // vortex falloff radius (m)
static float s_geyserUpStrength     = 3.9f;   // upward buoyancy accel (m/s²)
static float s_geyserCurlStrength   = 1.8f;   // curl-noise accel (m/s²)
static float s_geyserDrag           = 1.8f;   // drag coefficient (dimensionless)

// Travel field base layer params — tunable, rebuilt each frame
static float s_travelCurlStrength   = 1.45f;  // curl-noise accel (m/s²)
static float s_travelDrag           = 2.8f;   // drag coefficient (dimensionless)

#define HOA_LONG_TUNABLE_COUNT 216

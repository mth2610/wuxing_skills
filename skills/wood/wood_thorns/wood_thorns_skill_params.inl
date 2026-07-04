// Included at file scope in wood_thorns_skill.c — all tunable state for wood_thorns_skill.
// Static declarations here have internal linkage (same as if in wood_thorns_skill.c directly).
// Do not #include from any other translation unit.

// Sandbox-tunable spatial knobs (real-meter scale).
// See RegisterSkillTunables below and wood_thorns_skill_tunables.inl.
static float s_thornSpacing     = 0.35f;  // inter-thorn step along path (m)
static float s_thornMaxHeight   = 0.46f;  // thorn tip height at full scale (m)
static float s_thornBaseRadius  = 0.065f; // base radius at full scale (m)
static float s_aoeRadius        = 0.25f;  // hit-test radius (m)
static float s_decalScale       = 0.78f;  // crack decal half-size = base_radius * decalScale
static float s_lightRadius      = 0.70f;  // VFXLight radius per thorn (m)
static float s_shakeTrauma      = 0.28f;  // camera-shake magnitude (unitless 0..1)
static float s_shakeEnable      = 1.0f;   // 1=on, 0=off (toggle in sandbox)
static float s_distortRadius    = 0.60f;  // ScreenDistort world radius (m)
static float s_sideJitterMax    = 0.12f;  // perpendicular spawn jitter half-range (m)

// Dust burst
static float s_dustSpeedOutMin  = 0.35f;  // outward speed range (m/s)
static float s_dustSpeedOutMax  = 0.75f;
static float s_dustSpeedUpMin   = 0.40f;
static float s_dustSpeedUpMax   = 0.90f;
static float s_dustRadiusMin    = 0.030f; // particle radius range (m)
static float s_dustRadiusMax    = 0.060f;
static float s_dustLifeMin      = 0.60f;
static float s_dustLifeMax      = 1.20f;

// Holding phase mist
static float s_mistSpeedOutMin  = 0.05f;
static float s_mistSpeedOutMax  = 0.15f;
static float s_mistSpeedUpMin   = 0.15f;
static float s_mistSpeedUpMax   = 0.35f;
static float s_mistRadiusMin    = 0.12f;
static float s_mistRadiusMax    = 0.28f;
static float s_mistLifeMin      = 0.80f;
static float s_mistLifeMax      = 1.60f;

// Per-phase over-lifetime curves — default flat (no-op until shaped in sandbox).
static SkillCurve s_castRadiusCurve, s_castSpeedCurve, s_castAlphaCurve;
static SkillCurve s_castEmissiveCurve;
static SkillCurve s_holdRadiusCurve, s_holdSpeedCurve, s_holdAlphaCurve;
static SkillCurve s_holdEmissiveCurve;

// Per-phase force mixes (all layers default to 0 strength — no behavior change).
static SkillForceMix s_castForce;
static SkillForceMix s_holdForce;
static ForceField    s_castField;
static ForceField    s_holdField;

// 25 float tunables + 2 phases × 4 curves + 2 phases × SKILL_FORCE_MIX_TUNABLE_COUNT(29)
// = 25 + 8 + 58 = 91
#define WOOD_THORNS_TUNABLE_COUNT 96

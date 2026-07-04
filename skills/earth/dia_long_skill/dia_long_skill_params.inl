// Included at file scope in dia_long_skill.c — all tunable state for dia_long.
// Static declarations here have internal linkage (same as if in dia_long_skill.c directly).
// Do not #include from any other translation unit.

// Pass 1: VERT_SPACING rescaled 55.0 cm → 0.55 m
static float s_vertSpacing = 0.55f;
static float s_shakeEnable = 1.0f;   // 1=on, 0=off

#define CAST_TIME         0.5f
#define RISE_TIME         0.22f
#define HEAD_RISE_TIME    0.30f
#define VERT_STAGGER      0.07f // eruption wave speed along the spine
#define ACTIVE_TIME       2.2f
#define COLLAPSE_TIME     0.9f
#define DOT_TICK          0.5f

// Pass 5: sandbox-tunable geometry knobs
// Vertebra heights (meters): base + arc delta, randomised ±15% per vertebra
static float s_vertHeightBase  = 0.44f;  // Pass 1: was 44.0f cm
static float s_vertHeightArc   = 0.30f;  // Pass 1: was 30.0f cm (arc along path)
// Head height (meters)
static float s_headHeight      = 0.72f;  // Pass 1: was 72.0f cm
// Fissure mesh dimensions (meters)
static float s_fissureWidth    = 0.22f;  // Pass 1: was 22.0f cm
static float s_fissureDepth    = 0.18f;  // Pass 1: was 18.0f cm
// Decal radii (meters)
static float s_crackDecalRadius    = 0.46f;  // Pass 1: was 46.0f cm (base, +0..14 cm jitter → 0..0.14)
static float s_shatterDecalRadiusV = 0.34f;  // Pass 1: was 34.0f cm, vertebra aftermath
static float s_shatterDecalRadiusH = 0.55f;  // Pass 1: was 55.0f cm, head aftermath
static float s_runeDecalRadius     = 0.60f;  // Pass 1: was 60.0f cm, windup rune
static float s_lavaDecalRadius     = 0.65f;  // Pass 1: was 65.0f cm, head lava
// VFX light radii (meters)
static float s_castLightRadius    = 0.70f;   // Pass 1: was 70.0f cm
static float s_eruptLightRadius   = 0.60f;   // Pass 1: was 60.0f cm, per vertebra
static float s_headLightRadius    = 1.50f;   // Pass 1: was 150.0f cm
// AoE damage radii (meters)
static float s_vertAoERadius      = 0.32f;   // Pass 1: was 32.0f cm
static float s_headAoERadius      = 0.60f;   // Pass 1: was 60.0f cm
static float s_dotAoERadius       = 0.55f;   // Pass 1: was 55.0f cm
// Projectile query radius (meters)
static float s_projectileRadius   = 0.45f;   // Pass 1: was 45.0f cm
// Screen distort radius (meters)
static float s_distortRadius      = 1.30f;   // Pass 1: was 130.0f cm
// Emitter rate (not spatial — particles/sec, unchanged)
static float s_headEmitterRate    = 24.0f;
// Particle speeds (m/s): eruption burst outward / upward
static float s_burstSpeedOut      = 0.70f;   // Pass 1: was 70.0f cm/s
static float s_burstSpeedUpMin    = 0.60f;   // Pass 1: was 60 cm/s
static float s_burstSpeedUpMax    = 1.30f;   // Pass 1: was 130 cm/s
// Ember drift speeds (m/s): active-phase vertebra motes
static float s_emberSpeedXZ       = 0.12f;   // Pass 1: was 12 cm/s
static float s_emberSpeedUpMin    = 0.18f;   // Pass 1: was 18 cm/s
static float s_emberSpeedUpMax    = 0.40f;   // Pass 1: was 40 cm/s
// Particle radii (meters)
static float s_burstRadiusMin     = 0.018f;  // Pass 1: was 1.8 cm (18/10 cm)
static float s_burstRadiusMax     = 0.042f;  // Pass 1: was 4.2 cm (42/10 cm)
static float s_emberRadiusMin     = 0.008f;  // Pass 1: was 0.8 cm  (8/10 cm)
static float s_emberRadiusMax     = 0.016f;  // Pass 1: was 1.6 cm (16/10 cm)

// Per-phase over-lifetime curves (flat → no-op until shaped in sandbox)
static SkillCurve s_castRadiusCurve, s_castSpeedCurve, s_castAlphaCurve, s_castEmissiveCurve;
static SkillCurve s_eruptRadiusCurve, s_eruptSpeedCurve, s_eruptAlphaCurve, s_eruptEmissiveCurve;
static SkillCurve s_activeRadiusCurve, s_activeSpeedCurve, s_activeAlphaCurve, s_activeEmissiveCurve;

// One tunable force mix per phase
static SkillForceMix s_castForce;
static SkillForceMix s_eruptForce;
static SkillForceMix s_activeForce;
static ForceField    s_castField;
static ForceField    s_eruptField;
static ForceField    s_activeField;

// 3 phases × (3 curves + 1 ForceMix×29) + ~40 scalar tunables
// = 3×32 + 40 = 136
#define DIA_LONG_TUNABLE_COUNT 144

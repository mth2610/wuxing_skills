// Included at file scope in stone_prison_skill.c — all tunable state for stone_prison.
// Static declarations here have internal linkage (same as if in stone_prison_skill.c directly).
// Do not #include from any other translation unit.

// Pass 1 — meter-rescaled tunables (÷100 from old 1cm-scale values).
// Loaded from .tuning file on Init; defaults below are the canonical source of truth.
static float s_shakeEnable         = 1.0f;    // 1=on, 0=off
static float s_pillarHeight        = 0.56f;   // pillar height (m)
static float s_pillarRadius        = 0.085f;  // pillar base radius (m)

// Casting dust
static float s_castDustVelOutward  = 0.20f;   // dust inward velocity (m/s)
static float s_castDustVelYMin     = 0.30f;   // dust upward vel min (m/s)
static float s_castDustVelYMax     = 0.60f;   // dust upward vel max (m/s)
static float s_castDustRadiusMin   = 0.015f;  // dust particle min radius (m)
static float s_castDustRadiusMax   = 0.035f;  // dust particle max radius (m)
static float s_castLightRadius     = 0.65f;   // cast warning VFXLight radius (m)

// Rising burst
static float s_riseVelOutward      = 0.60f;   // rising burst outward XZ (m/s)
static float s_riseVelYMin         = 0.50f;   // rising burst Y min (m/s)
static float s_riseVelYMax         = 0.90f;   // rising burst Y max (m/s)
static float s_riseParticleRadiusMin = 0.020f; // rising burst particle min radius (m)
static float s_riseParticleRadiusMax = 0.050f; // rising burst particle max radius (m)
static float s_riseLightRadius     = 1.30f;   // rising VFXLight radius (m)
static float s_risePillarYSink     = 0.15f;   // pillar Y sink-below-ground during rise (m)

// ScreenDistort radii
static float s_castDistortRadius   = 0.90f;   // cast ScreenDistort wave radius (m)
static float s_explodeDistortRadius= 1.30f;   // explode ScreenDistort wave radius (m)

// Holding sparks
static float s_sparkYOffset        = 0.01f;   // spark spawn Y above ground (m)
static float s_sparkVelYMin        = 0.20f;   // spark upward vel min (m/s)
static float s_sparkVelYMax        = 0.45f;   // spark upward vel max (m/s)
static float s_sparkRadiusMin      = 0.008f;  // spark min radius (m)
static float s_sparkRadiusMax      = 0.018f;  // spark max radius (m)

// Explosion
static float s_explodeVelOutward   = 1.10f;   // explosion burst outward XZ (m/s)
static float s_explodeVelYMin      = 0.40f;   // explosion burst Y min (m/s)
static float s_explodeVelYMax      = 0.90f;   // explosion burst Y max (m/s)
static float s_explodeParticleRadiusMin = 0.020f; // explosion particle min radius (m)
static float s_explodeParticleRadiusMax = 0.050f; // explosion particle max radius (m)
static float s_explodeLightRadius  = 1.40f;   // explosion VFXLight radius (m)

// Per-phase over-lifetime curves — flat 1.0 until shaped in sandbox
static SkillCurve s_castRadiusCurve,    s_castSpeedCurve,    s_castAlphaCurve,    s_castEmissiveCurve;
static SkillCurve s_riseRadiusCurve,    s_riseSpeedCurve,    s_riseAlphaCurve,    s_riseEmissiveCurve;
static SkillCurve s_holdRadiusCurve,    s_holdSpeedCurve,    s_holdAlphaCurve,    s_holdEmissiveCurve;
static SkillCurve s_explodeRadiusCurve, s_explodeSpeedCurve, s_explodeAlphaCurve, s_explodeEmissiveCurve;

// Per-phase extra force mixes
static SkillForceMix s_castForce;
static ForceField    s_castFieldActive;
static SkillForceMix s_riseForce;
static ForceField    s_riseFieldActive;
static SkillForceMix s_holdForce;
static ForceField    s_holdFieldActive;
static SkillForceMix s_explodeForce;
static ForceField    s_explodeFieldActive;

// scalars: (1+6+1+5+2+5+1+1+5+1+5) = 33
// curves:  4 phases × 4 = 16
// force:   4 phases × 29 = 116
// total = 33 + 16 + 116 = 165
#define STONE_PRISON_TUNABLE_COUNT 166

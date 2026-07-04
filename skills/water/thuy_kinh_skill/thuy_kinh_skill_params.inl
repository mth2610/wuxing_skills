// Included at file scope in thuy_kinh_skill.c — all tunable state for thuy_kinh.
// Static declarations here have internal linkage (same as if in thuy_kinh_skill.c directly).
// Do not #include from any other translation unit.

// Pass 1 — meter-rescaled tunables (÷100 from old 1cm-scale values).
// Loaded from .tuning file on Init, so all defaults below are the canonical
// source of truth — do NOT duplicate them inside the .inl file.
static float s_shakeEnable        = 1.0f;    // 1=on, 0=off
static float s_shieldRadius       = 0.55f;   // dome radius (m)

// Dissolve rain burst
static float s_rainVelOutward     = 0.22f;   // dissolve burst outward XZ speed (m/s)
static float s_rainVelUp          = 0.08f;   // dissolve burst upward speed (m/s)
static float s_rainRadiusMin      = 0.012f;  // dissolve droplet min radius (m)
static float s_rainRadiusMax      = 0.026f;  // dissolve droplet max radius (m)

// Gather phase
static float s_gatherParticleYMin = 0.02f;   // gather particle spawn Y min (m)
static float s_gatherParticleYMax = 0.30f;   // gather particle spawn Y max (m)
static float s_gatherTargetY      = 0.18f;   // gather convergence Y (m)
static float s_gatherSpeed        = 1.30f;   // gather particle inward speed (m/s)
static float s_gatherRadiusMin    = 0.010f;  // gather particle min radius (m)
static float s_gatherRadiusMax    = 0.022f;  // gather particle max radius (m)

// Cast VFX
static float s_castLightY         = 0.20f;   // VFXLight spawn Y (m)
static float s_castLightRadius    = 0.70f;   // VFXLight radius at cast (m)

// Ribbon
static float s_ribbonLen          = 0.026f;  // trail length parameter (m)
static float s_ribbonThick        = 0.012f;  // trail thickness (m)

// Bloom burst
static float s_bloomCrownY        = 0.03f;   // crown splash spawn Y (m)
static float s_bloomVelOutward    = 0.55f;   // crown splash outward XZ speed (m/s)
static float s_bloomVelUpMin      = 0.70f;   // crown splash Y vel min (m/s)
static float s_bloomVelUpMax      = 1.30f;   // crown splash Y vel max (m/s)
static float s_bloomCrownRadiusMin= 0.015f;  // crown splash droplet min radius (m)
static float s_bloomCrownRadiusMax= 0.032f;  // crown splash droplet max radius (m)
static float s_bloomLightRadius   = 1.30f;   // active ward VFXLight radius (m)
static float s_bloomSurgeKnockback= 1.10f;   // protective surge knockback impulse (m/s)

// Ribbon drip
static float s_dripVelXZ          = 0.15f;   // drip lateral velocity half-range (m/s)
static float s_dripVelY           = -0.10f;  // drip downward velocity (m/s)
static float s_dripRadiusMin      = 0.010f;  // drip droplet min radius (m)
static float s_dripRadiusMax      = 0.020f;  // drip droplet max radius (m)
static float s_ribbonFloorY       = 0.04f;   // ribbon tip Y floor, keep above ground (m)

// Mist
static float s_mistYMin           = 0.02f;   // mist spawn Y min (m)
static float s_mistYMax           = 0.12f;   // mist spawn Y max (m)
static float s_mistVelXZ          = 0.18f;   // mist tangential speed (m/s)
static float s_mistVelYMin        = 0.24f;   // mist upward speed min (m/s)
static float s_mistVelYMax        = 0.50f;   // mist upward speed max (m/s)
static float s_mistRadiusMin      = 0.016f;  // mist particle min radius (m)
static float s_mistRadiusMax      = 0.034f;  // mist particle max radius (m)

// Force fields
static float s_rainGravity        = 4.20f;   // dissolve-rain downward gravity (m/s²)
static float s_swirlVortex        = 2.20f;   // mist vortex strength (m/s²)
static float s_swirlPull          = 0.28f;   // mist central pull strength (m/s²)
static float s_swirlDrag          = 0.30f;   // mist drag coefficient (unitless)

// Per-phase over-lifetime curves — flat 1.0 until shaped in sandbox
static SkillCurve s_gatherRadiusCurve, s_gatherSpeedCurve, s_gatherAlphaCurve;
static SkillCurve s_gatherEmissiveCurve;
static SkillCurve s_mistRadiusCurve,   s_mistSpeedCurve,   s_mistAlphaCurve;
static SkillCurve s_mistEmissiveCurve;
static SkillCurve s_dissolveRadiusCurve, s_dissolveSpeedCurve, s_dissolveAlphaCurve;
static SkillCurve s_dissolveEmissiveCurve;

// Per-phase extra force mixes (sandbox-tunable)
static SkillForceMix s_gatherForce;
static ForceField    s_gatherFieldActive;
static SkillForceMix s_mistForce;
static ForceField    s_mistFieldActive;
static SkillForceMix s_dissolveForce;
static ForceField    s_dissolveFieldActive;

// dome(1) + gather(6+3+1+29=39) + cast(2) + ribbon(3) + drip(4) + bloom(8)
// + mist(7+3+1+29=40) + swirl(3) + dissolve(5+3+1+29=38) = 138
#define THUY_KINH_TUNABLE_COUNT 139

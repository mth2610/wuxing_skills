#ifndef CORE_LIQUID_MEDIUM_H
#define CORE_LIQUID_MEDIUM_H

/* Physical identity of a liquid, in SI units.
 *
 * Why this exists next to `liquid_motion.h`: `LiquidMotionDesc` is an ARTISTIC
 * envelope (splash counts, settle damping, lifetime). It cannot answer "what is
 * this liquid", so every caller that needed a second liquid had to invent new
 * magic numbers. Four SI constants are enough to distinguish any Newtonian or
 * Bingham liquid, and every behaviour the guided-liquid sim needs can be
 * DERIVED from them instead of authored:
 *
 *   rho   density            -> inertia, and therefore drag/gravity response
 *   mu    dynamic viscosity  -> shear resistance, deformation decay rate
 *   sigma surface tension    -> capillarity, coalescence, aerodynamic breakup
 *   tau_y yield stress       -> Bingham plastics (mud, lava) stop flowing
 *
 * Derived groups are the standard ones (Reynolds, Weber, Ohnesorge, Morton,
 * Bond, capillary length/time), so a new liquid is authored as measured
 * constants, not as tuning guesses. Air constants are sea-level 20 C.
 *
 * Deliberately header-only and dependency-free (no raylib, no allocation) so a
 * headless test can assert the arithmetic — see core/tests/liquid_medium_test.c.
 */

#include <math.h>
#include <stdbool.h>

#define LIQUID_AIR_DENSITY_KG_M3 1.2041f
#define LIQUID_AIR_DYNAMIC_VISCOSITY_PAS 1.825e-5f
#define LIQUID_GRAVITY_MPS2 9.80665f
/* Free-droplet aerodynamic breakup threshold, Weber number. Water in air is
 * observed to shatter just above this value, which is why rain at terminal
 * speed is round and a jet at 30 m/s atomises. */
#define LIQUID_WEBER_CRITICAL 12.0f

typedef enum {
    LIQUID_MEDIUM_WATER = 0,
    LIQUID_MEDIUM_POISON,
    LIQUID_MEDIUM_MUD,
    LIQUID_MEDIUM_LAVA,
    LIQUID_MEDIUM_LIQUID_METAL,
    LIQUID_MEDIUM_COUNT
} LiquidMediumId;

typedef struct LiquidMediumDesc {
    float densityKgM3;         /* rho  — kg/m^3 */
    float dynamicViscosityPaS; /* mu   — Pa*s */
    float surfaceTensionNm;    /* sigma — N/m */
    float yieldStressPa;       /* tau_y — Pa, 0 for a Newtonian liquid */
    float dragCoefficient;     /* Cd of a free sphere in air */
    float restitution;         /* normal restitution against terrain, 0..1 */
    float contactFriction;     /* fraction of tangential momentum retained, 0..1 */
} LiquidMediumDesc;

static inline float LiquidMedium_Clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/* Measured constants at ~20 C (lava: ~1200 C, liquid metal: mercury).
 * These are reference values, not tuning knobs: the guided-liquid sim still
 * exposes per-effect overrides for anything an art direction needs. */
static inline LiquidMediumDesc LiquidMedium_Get(LiquidMediumId id)
{
    switch (id) {
    case LIQUID_MEDIUM_POISON:
        /* Surfactant-laden ichor: denser than water, far more viscous, and a
         * lower surface tension (that is why it coats and creeps). */
        return (LiquidMediumDesc){1180.0f, 0.85f, 0.048f, 0.0f, 0.52f, 0.10f, 0.72f};
    case LIQUID_MEDIUM_MUD:
        /* A concentrated clay slurry: a Bingham plastic, hence the yield
         * stress. It piles up instead of levelling. */
        return (LiquidMediumDesc){1680.0f, 6.0f, 0.050f, 35.0f, 0.60f, 0.02f, 0.92f};
    case LIQUID_MEDIUM_LAVA:
        /* Basaltic melt. Viscosity is temperature-driven and varies by orders of
         * magnitude; this is a hot, still-flowing reference. */
        return (LiquidMediumDesc){2600.0f, 100.0f, 0.35f, 180.0f, 0.58f, 0.02f, 0.86f};
    case LIQUID_MEDIUM_LIQUID_METAL:
        /* Mercury. Very dense, very low viscosity, very high surface tension:
         * the combination is what makes it bead instead of splash. */
        return (LiquidMediumDesc){13530.0f, 1.526e-3f, 0.485f, 0.0f, 0.42f, 0.30f, 0.30f};
    case LIQUID_MEDIUM_WATER:
    default:
        return (LiquidMediumDesc){998.2f, 1.002e-3f, 0.0728f, 0.0f, 0.47f, 0.24f, 0.52f};
    }
}

static inline bool LiquidMedium_IsValid(const LiquidMediumDesc *m)
{
    return m && isfinite(m->densityKgM3) && m->densityKgM3 > 0.0f &&
           isfinite(m->dynamicViscosityPaS) && m->dynamicViscosityPaS >= 0.0f &&
           isfinite(m->surfaceTensionNm) && m->surfaceTensionNm >= 0.0f &&
           isfinite(m->yieldStressPa) && m->yieldStressPa >= 0.0f &&
           isfinite(m->dragCoefficient) && m->dragCoefficient >= 0.0f &&
           isfinite(m->restitution) && m->restitution >= 0.0f &&
           m->restitution <= 1.0f && isfinite(m->contactFriction) &&
           m->contactFriction >= 0.0f && m->contactFriction <= 1.0f;
}

/* Per-effect overrides. A non-positive override keeps the measured value, so a
 * caller can expose "density / viscosity / surface tension" sliders that fall
 * back to the profile until touched. */
static inline LiquidMediumDesc LiquidMedium_Override(LiquidMediumDesc base, float densityKgM3,
                                                     float dynamicViscosityPaS,
                                                     float surfaceTensionNm)
{
    if (isfinite(densityKgM3) && densityKgM3 > 0.0f) base.densityKgM3 = densityKgM3;
    if (isfinite(dynamicViscosityPaS) && dynamicViscosityPaS > 0.0f)
        base.dynamicViscosityPaS = dynamicViscosityPaS;
    if (isfinite(surfaceTensionNm) && surfaceTensionNm > 0.0f)
        base.surfaceTensionNm = surfaceTensionNm;
    if (base.densityKgM3 <= 0.0f) base.densityKgM3 = 998.2f;
    if (base.dynamicViscosityPaS < 0.0f) base.dynamicViscosityPaS = 0.0f;
    if (base.surfaceTensionNm < 0.0f) base.surfaceTensionNm = 0.0f;
    return base;
}

/* ── Droplet geometry ────────────────────────────────────────────────────────
 * A sphere is the relaxed shape of a liquid, so radius alone fixes volume,
 * mass and frontal area. Everything below stays in SI. */
static inline float LiquidMedium_SphereVolumeM3(float radiusM)
{
    if (!(radiusM > 0.0f) || !isfinite(radiusM)) return 0.0f;
    return (4.0f / 3.0f) * 3.1415926536f * radiusM * radiusM * radiusM;
}

static inline float LiquidMedium_SphereRadiusM(float volumeM3)
{
    if (!(volumeM3 > 0.0f) || !isfinite(volumeM3)) return 0.0f;
    return cbrtf(volumeM3 * 0.2387324146f); /* (3/(4*pi))^(1/3) */
}

static inline float LiquidMedium_DropletMassKg(const LiquidMediumDesc *m, float radiusM)
{
    if (!m) return 0.0f;
    return m->densityKgM3 * LiquidMedium_SphereVolumeM3(radiusM);
}

static inline float LiquidMedium_DropletAreaM2(float radiusM)
{
    if (!(radiusM > 0.0f)) return 0.0f;
    return 3.1415926536f * radiusM * radiusM;
}

/* ── Dimensionless groups ────────────────────────────────────────────────────
 * All take the characteristic length (droplet diameter unless stated). */
static inline float LiquidMedium_KinematicViscosityM2S(const LiquidMediumDesc *m)
{
    if (!m || m->densityKgM3 <= 0.0f) return 0.0f;
    return m->dynamicViscosityPaS / m->densityKgM3;
}

/* Re = rho*v*L/mu. Small Re -> Stokes (linear) drag, large Re -> Newton
 * (quadratic) drag. */
static inline float LiquidMedium_Reynolds(const LiquidMediumDesc *m, float speedMps, float lengthM)
{
    if (!m || m->dynamicViscosityPaS <= 0.0f) return 0.0f;
    return m->densityKgM3 * fabsf(speedMps) * lengthM / m->dynamicViscosityPaS;
}

/* We = rho*v^2*L/sigma. Destabilising inertia over restoring capillarity;
 * We > LIQUID_WEBER_CRITICAL means a free droplet atomises. */
static inline float LiquidMedium_Weber(const LiquidMediumDesc *m, float speedMps, float lengthM)
{
    if (!m || m->surfaceTensionNm <= 0.0f) return 0.0f;
    return m->densityKgM3 * speedMps * speedMps * lengthM / m->surfaceTensionNm;
}

/* Oh = mu/sqrt(rho*sigma*L). Viscosity damped against capillarity; separates a
 * water splash (Oh ~ 1e-3) from a lava splash (Oh ~ 1) at the same size. */
static inline float LiquidMedium_Ohnesorge(const LiquidMediumDesc *m, float lengthM)
{
    if (!m || m->surfaceTensionNm <= 0.0f || lengthM <= 0.0f) return 0.0f;
    float denom = sqrtf(m->densityKgM3 * m->surfaceTensionNm * lengthM);
    return denom > 0.0f ? m->dynamicViscosityPaS / denom : 0.0f;
}

/* Mo = g*mu^4/(rho*sigma^3). Property-only number: how much gravity, viscosity
 * and capillarity interact for this liquid regardless of scale. */
static inline float LiquidMedium_Morton(const LiquidMediumDesc *m)
{
    if (!m || m->surfaceTensionNm <= 0.0f) return 0.0f;
    float sigma3 = m->surfaceTensionNm * m->surfaceTensionNm * m->surfaceTensionNm;
    if (sigma3 <= 0.0f) return 0.0f;
    float mu2 = m->dynamicViscosityPaS * m->dynamicViscosityPaS;
    return LIQUID_GRAVITY_MPS2 * mu2 * mu2 / (m->densityKgM3 * sigma3);
}

/* Capillary length sqrt(sigma/(rho*g)): the size below which surface tension
 * wins over gravity (water: 2.7 mm). A blob larger than this cannot hold a
 * spherical shape by capillarity alone — which is why a guided body needs its
 * own binding actuator, and why a puddle has a minimum film thickness. */
static inline float LiquidMedium_CapillaryLengthM(const LiquidMediumDesc *m)
{
    if (!m || m->densityKgM3 <= 0.0f || m->surfaceTensionNm <= 0.0f) return 0.0f;
    return sqrtf(m->surfaceTensionNm / (m->densityKgM3 * LIQUID_GRAVITY_MPS2));
}

/* Bond number (rho*g*L^2/sigma) with L the droplet diameter. Bo >> 1 means
 * gravity flattens the droplet. */
static inline float LiquidMedium_Bond(const LiquidMediumDesc *m, float lengthM)
{
    if (!m || m->surfaceTensionNm <= 0.0f) return 0.0f;
    return m->densityKgM3 * LIQUID_GRAVITY_MPS2 * lengthM * lengthM / m->surfaceTensionNm;
}

/* Time for capillarity to restore a distorted droplet: sqrt(rho*r^3/sigma). */
static inline float LiquidMedium_CapillaryTimeS(const LiquidMediumDesc *m, float radiusM)
{
    if (!m || m->surfaceTensionNm <= 0.0f || radiusM <= 0.0f) return 0.0f;
    return sqrtf(m->densityKgM3 * radiusM * radiusM * radiusM / m->surfaceTensionNm);
}

/* Time for internal shear to diffuse across the droplet: r^2/nu. */
static inline float LiquidMedium_ViscousTimeS(const LiquidMediumDesc *m, float radiusM)
{
    float nu = LiquidMedium_KinematicViscosityM2S(m);
    if (nu <= 0.0f || radiusM <= 0.0f) return 0.0f;
    return radiusM * radiusM / nu;
}

/* Speed at which aerodynamic pressure equals the capillary restoring pressure
 * (We = Wecrit): the atomisation threshold for a droplet of this diameter. */
static inline float LiquidMedium_BreakupSpeedMps(const LiquidMediumDesc *m, float diameterM)
{
    if (!m || m->surfaceTensionNm <= 0.0f || m->densityKgM3 <= 0.0f || diameterM <= 0.0f)
        return 0.0f;
    return sqrtf(LIQUID_WEBER_CRITICAL * m->surfaceTensionNm / (m->densityKgM3 * diameterM));
}

/* Newton-regime terminal speed of a sphere in air:
 * v_t = sqrt(4*rho*g*D / (3*Cd*rho_air)). Used as the fall-speed ceiling for
 * detached spray, so rain does not accelerate past physics. */
static inline float LiquidMedium_TerminalSpeedMps(const LiquidMediumDesc *m, float diameterM)
{
    if (!m || diameterM <= 0.0f || m->dragCoefficient <= 0.0f) return 0.0f;
    return sqrtf(4.0f * m->densityKgM3 * LIQUID_GRAVITY_MPS2 * diameterM /
                 (3.0f * m->dragCoefficient * LIQUID_AIR_DENSITY_KG_M3));
}

/* Velocity scale of the yield stress: sqrt(tau_y/rho). Below it a Bingham
 * plastic behaves as a solid — the reason mud piles up instead of levelling. */
static inline float LiquidMedium_YieldSpeedMps(const LiquidMediumDesc *m)
{
    if (!m || m->yieldStressPa <= 0.0f || m->densityKgM3 <= 0.0f) return 0.0f;
    return sqrtf(m->yieldStressPa / m->densityKgM3);
}

/* Fraction of a free droplet's volume that survives at this airspeed:
 * 1 below the breakup speed, falling as 1/We above it. Callers use it to shed
 * mass as mist/spray instead of silently keeping an impossible body. */
static inline float LiquidMedium_StableFraction(const LiquidMediumDesc *m, float speedMps,
                                                float diameterM)
{
    float weCrit = LIQUID_WEBER_CRITICAL;
    float we = LiquidMedium_Weber(m, speedMps, diameterM);
    if (we <= weCrit) return 1.0f;
    return LiquidMedium_Clamp01(weCrit / we);
}

#endif /* CORE_LIQUID_MEDIUM_H */

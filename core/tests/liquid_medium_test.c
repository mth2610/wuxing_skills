/* Headless test — core/liquid/liquid_medium.h (SI liquid identity + derived
 * dimensionless groups). Pure arithmetic, no raylib, no GPU.
 *
 * These are the numbers every guided-liquid behaviour is derived from, so a
 * wrong constant here silently retunes every liquid at once (drag, deformation
 * damping, atomisation, puddle spreading). The assertions are therefore
 * physical facts, not implementation echoes. */
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "core/liquid/liquid_medium.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define CHECK_NEAR(a, b, tol, name) do { \
    g_checks++; \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) <= (tol)) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [%.9g vs %.9g]\n", name, (double)_a, (double)_b); g_failures++; } \
} while (0)

int main(void)
{
    LiquidMediumDesc water = LiquidMedium_Get(LIQUID_MEDIUM_WATER);
    LiquidMediumDesc mercury = LiquidMedium_Get(LIQUID_MEDIUM_LIQUID_METAL);
    LiquidMediumDesc mud = LiquidMedium_Get(LIQUID_MEDIUM_MUD);
    LiquidMediumDesc lava = LiquidMedium_Get(LIQUID_MEDIUM_LAVA);

    /* ── Measured constants ─────────────────────────────────────────────── */
    CHECK_NEAR(water.densityKgM3, 998.2f, 0.1f, "water density is 998.2 kg/m3");
    CHECK(water.yieldStressPa == 0.0f, "water is Newtonian (no yield stress)");
    CHECK(LiquidMedium_IsValid(&water), "water descriptor is valid");
    CHECK(LiquidMedium_IsValid(&mercury), "mercury descriptor is valid");
    CHECK(LiquidMedium_IsValid(&mud) && LiquidMedium_IsValid(&lava),
          "Bingham liquids are valid");
    CHECK(mercury.densityKgM3 > 13.0f * water.densityKgM3,
          "liquid metal is >13x denser than water");
    CHECK(lava.dynamicViscosityPaS > 1000.0f * water.dynamicViscosityPaS,
          "lava is orders of magnitude more viscous than water");
    CHECK(mud.yieldStressPa > 0.0f && lava.yieldStressPa > mud.yieldStressPa,
          "mud and lava carry a yield stress, lava the larger one");

    /* ── Overrides: 0 keeps the measured value (the config contract) ────── */
    LiquidMediumDesc overridden = LiquidMedium_Override(water, 0.0f, 0.0f, 0.0f);
    CHECK_NEAR(overridden.densityKgM3, water.densityKgM3, 1e-3f,
               "zero override keeps the profile density");
    overridden = LiquidMedium_Override(water, 1200.0f, 0.5f, 0.09f);
    CHECK_NEAR(overridden.densityKgM3, 1200.0f, 1e-3f, "positive density override applies");
    CHECK_NEAR(overridden.dynamicViscosityPaS, 0.5f, 1e-6f, "positive viscosity override applies");
    CHECK_NEAR(overridden.surfaceTensionNm, 0.09f, 1e-6f, "positive surface tension override applies");

    LiquidMediumDesc invalid = {0};
    CHECK(!LiquidMedium_IsValid(&invalid), "zero-density medium is rejected");

    /* ── Geometry: sphere volume/mass/area round-trip ───────────────────── */
    float r = 0.16f;
    float volume = LiquidMedium_SphereVolumeM3(r);
    CHECK_NEAR(volume, (4.0f / 3.0f) * 3.1415926536f * r * r * r, 1e-6f,
               "sphere volume is 4/3 pi r^3");
    CHECK_NEAR(LiquidMedium_SphereRadiusM(volume), r, 1e-5f,
               "sphere radius inverts the volume");
    CHECK_NEAR(LiquidMedium_DropletMassKg(&water, r), water.densityKgM3 * volume, 1e-3f,
               "droplet mass is density times volume");
    CHECK_NEAR(LiquidMedium_DropletAreaM2(r), 3.1415926536f * r * r, 1e-6f,
               "frontal area is pi r^2");
    CHECK(LiquidMedium_DropletMassKg(&mercury, r) > 13.0f * LiquidMedium_DropletMassKg(&water, r),
          "a mercury droplet of the same size is >13x heavier");

    /* ── Reynolds: water at 1 m/s over 1 mm is ~1000 ────────────────────── */
    float re = LiquidMedium_Reynolds(&water, 1.0f, 1.0e-3f);
    CHECK(re > 950.0f && re < 1050.0f, "water Re(1 m/s, 1 mm) is ~1000");
    CHECK_NEAR(LiquidMedium_Reynolds(&water, 0.0f, 1e-3f), 0.0f, 1e-6f,
               "zero speed gives zero Reynolds number");

    /* ── Weber / atomisation threshold ──────────────────────────────────── */
    float d = 0.002f;
    float breakup = LiquidMedium_BreakupSpeedMps(&water, d);
    CHECK(breakup > 0.0f, "water breakup speed is positive");
    CHECK_NEAR(LiquidMedium_Weber(&water, breakup, d), LIQUID_WEBER_CRITICAL, 1e-2f,
               "breakup speed sits exactly at the critical Weber number");
    CHECK_NEAR(LiquidMedium_StableFraction(&water, breakup * 0.5f, d), 1.0f, 1e-6f,
               "below breakup speed the whole droplet survives");
    CHECK(LiquidMedium_StableFraction(&water, breakup * 4.0f, d) < 0.30f,
               "well above breakup speed only a small fraction survives");
    CHECK(LiquidMedium_StableFraction(&lava, breakup * 4.0f, d) >
              LiquidMedium_StableFraction(&water, breakup * 4.0f, d),
          "lava resists aerodynamic breakup better than water");

    /* ── Capillary length: water ~2.7 mm ────────────────────────────────── */
    float capLen = LiquidMedium_CapillaryLengthM(&water);
    CHECK(capLen > 2.5e-3f && capLen < 2.9e-3f, "water capillary length is ~2.7 mm");
    /* sigma/sqrt(rho*g): mercury's much larger density beats its 6.7x larger
     * surface tension, so its capillary length is actually shorter. */
    CHECK(LiquidMedium_CapillaryLengthM(&mercury) < capLen,
          "mercury capillary length is shorter than water's (density wins)");

    /* ── Ohnesorge separates water from lava at equal size ──────────────── */
    float ohWater = LiquidMedium_Ohnesorge(&water, d);
    float ohLava = LiquidMedium_Ohnesorge(&lava, d);
    CHECK(ohWater < 5.0e-2f, "water Ohnesorge number is small (capillary dominated)");
    CHECK(ohLava > 10.0f * ohWater, "lava Ohnesorge number is far larger than water's");

    /* ── Morton is property-only: mercury < water ───────────────────────── */
    CHECK(LiquidMedium_Morton(&mercury) < LiquidMedium_Morton(&water),
          "Morton number of mercury is below water's");

    /* ── Bond: a metre-scale blob is gravity dominated ──────────────────── */
    CHECK(LiquidMedium_Bond(&water, 2.0f * r) > 1.0f,
          "a 32 cm water blob is Bond > 1 (gravity beats capillarity)");

    /* ── Times ─────────────────────────────────────────────────────────── */
    CHECK(LiquidMedium_CapillaryTimeS(&water, r) > 0.0f, "capillary time is positive");
    /* tau = r^2/nu with nu = mu/rho: lava's kinematic viscosity is ~38,000x
     * water's, so internal shear in a lava blob relaxes almost instantly —
     * which is precisely why it keeps its shape instead of splashing. */
    CHECK(LiquidMedium_ViscousTimeS(&lava, r) < LiquidMedium_ViscousTimeS(&water, r),
          "lava's internal shear relaxes far faster than water's (high nu)");

    /* ── Terminal speed / yield speed ───────────────────────────────────── */
    float vt = LiquidMedium_TerminalSpeedMps(&water, d);
    CHECK(vt > 4.0f && vt < 20.0f, "2 mm water droplet terminal speed is rain-like");
    CHECK(LiquidMedium_TerminalSpeedMps(&water, 2.0f * d) > vt,
          "bigger droplets fall faster (quadratic drag, Newton regime)");
    CHECK_NEAR(LiquidMedium_YieldSpeedMps(&water), 0.0f, 1e-6f,
               "water has no yield velocity");
    CHECK(LiquidMedium_YieldSpeedMps(&lava) > LiquidMedium_YieldSpeedMps(&mud),
          "lava yields faster than mud (tau_y/rho ordering)");

    printf("\n%s: %d checks, %d failures\n",
           g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

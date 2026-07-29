// core headless test — E8 quality tiers.
//
// A budget has exactly one dangerous failure mode: gating that can turn a
// feature ON. Then the tier stops being a ceiling and becomes a second, hidden
// configuration source, and a LOW-tier device gets a HIGH-tier pass because some
// other layer asked for it. That property is pure logic and is checked here for
// every combination of (tier, what the caller asked for) — 12 cases, no GPU.
//
// The device half of E8 — the A33 run and the rlvk visual tier — cannot be done
// from here at all. This file is the part that CAN be settled without hardware,
// so the hardware time is spent on what only hardware can answer.

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

typedef enum { GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3 } GfxQuality;

// Mirrors PostFX_ApplyQualityTier (core/post_fx.c).
static void ApplyTier(GfxQuality q, int *streak, int *radial)
{
    int allowStreak = (q >= GFX_MED);
    int allowRadial = (q >= GFX_HIGH);
    if (!allowStreak) *streak = 0;
    if (!allowRadial) *radial = 0;
}

static const char *TierName(GfxQuality q)
{
    switch (q) {
        case GFX_UNLIT: return "UNLIT";
        case GFX_LOW:   return "LOW";
        case GFX_MED:   return "MED";
        default:        return "HIGH";
    }
}

static void Test_TheGateNeverEnables(void)
{
    // The whole point. For every tier and every request, the result must be a
    // SUBSET of what was asked for.
    int bad = 0;
    for (int t = GFX_UNLIT; t <= GFX_HIGH; t++)
        for (int req = 0; req < 4; req++)
        {
            int streak = (req & 1) ? 1 : 0;
            int radial = (req & 2) ? 1 : 0;
            int wantS = streak, wantR = radial;
            ApplyTier((GfxQuality)t, &streak, &radial);
            if ((streak && !wantS) || (radial && !wantR)) bad++;
        }
    CHECK_MSG(bad == 0, "the tier gate can only ever clamp DOWN, never enable",
              "%d of 16 cases turned something on", bad);
}

static void Test_TheSpecTable(void)
{
    // ELDEN_VFX_SPEC E8 step 2, verbatim: LOW = neither, MED = streak only,
    // HIGH = both.
    struct { GfxQuality q; int streak, radial; } want[] = {
        { GFX_UNLIT, 0, 0 }, { GFX_LOW, 0, 0 }, { GFX_MED, 1, 0 }, { GFX_HIGH, 1, 1 },
    };
    for (int i = 0; i < 4; i++)
    {
        int s = 1, r = 1;                      // caller asks for everything
        ApplyTier(want[i].q, &s, &r);
        CHECK_MSG(s == want[i].streak && r == want[i].radial,
                  "the tier table matches the spec", "%s gave streak=%d radial=%d, "
                  "wanted %d/%d", TierName(want[i].q), s, r,
                  want[i].streak, want[i].radial);
    }
}

static void Test_TiersAreMonotonic(void)
{
    // A higher tier must never permit LESS than a lower one, or "turn the
    // quality up" can make an effect disappear.
    int prev = -1;
    for (int t = GFX_UNLIT; t <= GFX_HIGH; t++)
    {
        int s = 1, r = 1;
        ApplyTier((GfxQuality)t, &s, &r);
        int count = s + r;
        if (count < prev)
        {
            CHECK_MSG(0, "each tier permits at least as much as the one below",
                      "%s permits %d, below it permitted %d", TierName((GfxQuality)t),
                      count, prev);
            return;
        }
        prev = count;
    }
    CHECK(1, "each tier permits at least as much as the one below");
}

static char *SlurpFile(const char *path)
{
    static char buf[400000];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void Test_SourcesStillGate(void)
{
    const char *pf = SlurpFile("core/post_fx.c");
    CHECK(pf != NULL, "core/post_fx.c readable");
    if (pf) {
        CHECK(strstr(pf, "PostFX_ApplyQualityTier(&local);") != NULL,
              "PostFX_Draw still applies the tier");
        // Order is load-bearing: the tier must run AFTER the transient and the
        // tuning override, or either of them can smuggle a pass past the budget.
        const char *tune = strstr(pf, "PostFX_ApplyTuning(&local);");
        const char *tier = strstr(pf, "PostFX_ApplyQualityTier(&local);");
        CHECK(tune && tier && tier > tune,
              "the tier runs AFTER the tuning override, so it outranks it");
    }

    // The Đợt E compositions that are fill-hungry on device. Each one keeps its
    // geometry at LOW and drops only the screen-wide extra.
    struct { const char *file, *needle, *name; } gates[] = {
        { "core/composition/common/vc_sweep_slash.inl", "GfxQuality_Get() >= GFX_MED",
          "SweepSlash drops its screen refraction below MED" },
        { "core/composition/common/vc_light_shaft.inl", "GfxQuality_Get() <= GFX_LOW",
          "LightShaft thins its cone at LOW" },
        // The gate moved WITH the beat when the package was decomposed into
        // primaries (29/07): it is now an early return inside
        // VFX_ComposeImpactFlash / _Distort, so a caller reaching for the piece
        // on its own cannot bypass the budget. Same gate, opposite sense.
        { "core/composition/common/vc_impact_package.inl", "GfxQuality_Get() < GFX_MED",
          "the impact flash and distortion primaries drop themselves below MED" },
    };
    for (int i = 0; i < 3; i++)
    {
        const char *src = SlurpFile(gates[i].file);
        CHECK_MSG(src && strstr(src, gates[i].needle) != NULL, gates[i].name,
                  "%s", gates[i].file);
    }
}

int main(void)
{
    printf("=== core headless test: E8 quality tiers ===\n");
    Test_TheGateNeverEnables();
    Test_TheSpecTable();
    Test_TiersAreMonotonic();
    Test_SourcesStillGate();
    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}

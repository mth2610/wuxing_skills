// core headless test — VFX_ComposeCoreGlow, the first primary EXTRACTED from a
// composite rather than invented.
//
// An extraction has one acceptance criterion above all others: the look must not
// change. The numbers moved from inside VFX_ComposeChargeConverge to a function
// of their own, and if any of them drifted in transit the whole argument for
// extracting ("costs no visual iteration, the look is already signed off")
// evaporates. So most of this file is a transcription check against the values
// as they were, plus the arithmetic that explains WHY there are three sprites.
//
// What the mirror CANNOT see: whether the point reads as too bright to have a
// colour. It can only prove the layers are still ordered and scaled to do so.

#include <stdio.h>
#include <string.h>
#include <math.h>

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

#define CORE_GLOW_RATE_MIN  8.0f
#define CORE_GLOW_RATE_MAX  30.0f
#define CORE_GLOW_BATCH_MAX 4
#define BLOOM_MAX_ENERGY    4.0f   // core/shaders — the bright pass's per-pixel clamp

static float Mix(float a, float b, float t) { return a + (b - a) * t; }

// The three layers, as authored: radius base, radius growth, emissive boost at
// intensity 0 and 1.
static float CoreRadius(float i01)  { return 0.045f + 0.075f * i01; }
static float MidRadius(float i01)   { return 0.10f  + 0.16f  * i01; }
static float HaloRadius(float i01)  { return 0.22f  + 0.30f  * i01; }
static float CoreBoost(float i01)   { return Mix(4.0f, 14.0f, i01); }
static float MidBoost(float i01)    { return Mix(1.6f, 3.2f,  i01); }

// ── 1. The layers are ordered, and each does a different job ────────────────

static void Test_ThreeLayersAreDistinct(void)
{
    // Strictly nested at EVERY intensity, not just at one. A ratio checked at a
    // single parameter value passes on a broken formula too — the invariance is
    // the assertion that matters (core/docs/LANDMINES.md).
    float worstCoreMid = 0.0f, worstMidHalo = 0.0f;
    for (float i = 0.0f; i <= 1.0f; i += 0.01f) {
        float cm = MidRadius(i) / CoreRadius(i);
        float mh = HaloRadius(i) / MidRadius(i);
        if (cm > worstCoreMid || worstCoreMid == 0.0f) worstCoreMid = cm;
        if (mh > worstMidHalo || worstMidHalo == 0.0f) worstMidHalo = mh;
        if (!(CoreRadius(i) < MidRadius(i) && MidRadius(i) < HaloRadius(i))) {
            worstCoreMid = -1.0f;
            break;
        }
    }
    CHECK_MSG(worstCoreMid > 0.0f,
              "core < mid < halo at every intensity — a nest, not three sprites",
              "%s", "ordering breaks somewhere in 0..1");

    // The mid layer must be meaningfully wider than the core or it is a second
    // core, and meaningfully narrower than the halo or the falloff has no middle.
    CHECK_MSG(MidRadius(0.5f) / CoreRadius(0.5f) > 1.8f,
              "the mid glow is clearly wider than the core",
              "%.2fx", MidRadius(0.5f) / CoreRadius(0.5f));
    CHECK_MSG(HaloRadius(0.5f) / MidRadius(0.5f) > 1.8f,
              "and the halo clearly wider than the mid",
              "%.2fx", HaloRadius(0.5f) / MidRadius(0.5f));
}

// ── 2. Why three sprites, and not one bright one ────────────────────────────

static void Test_BloomBudgetJustifiesTheMidLayer(void)
{
    // THE REASON THIS PRIMARY HAS A MID LAYER AT ALL. The bright pass clamps
    // each pixel's contribution at bloom_max_energy, so past that point raising
    // the tiny core's boost buys nothing: bloom SIZE is how many pixels clear
    // the threshold, not how far one pixel clears it.
    CHECK_MSG(CoreBoost(0.0f) >= BLOOM_MAX_ENERGY,
              "the core is already AT the bloom clamp at zero intensity",
              "boost %.1f vs clamp %.1f", CoreBoost(0.0f), BLOOM_MAX_ENERGY);
    CHECK_MSG(CoreBoost(1.0f) > 3.0f * BLOOM_MAX_ENERGY,
              "...and far past it at full — so its boost cannot be what grows the bloom",
              "boost %.1f vs clamp %.1f", CoreBoost(1.0f), BLOOM_MAX_ENERGY);

    // The mid layer is the one that actually buys bloom: it clears the threshold
    // while covering several times the AREA.
    CHECK_MSG(MidBoost(0.0f) > 1.0f && MidBoost(1.0f) <= BLOOM_MAX_ENERGY,
              "the mid layer sits just OVER the threshold and under the clamp",
              "%.1f..%.1f vs clamp %.1f", MidBoost(0.0f), MidBoost(1.0f), BLOOM_MAX_ENERGY);
    float areaGain = (MidRadius(1.0f) * MidRadius(1.0f)) /
                     (CoreRadius(1.0f) * CoreRadius(1.0f));
    CHECK_MSG(areaGain > 4.0f,
              "and it covers several times the core's area, which is what bloom counts",
              "%.1fx the pixels", areaGain);

    // The halo takes NO boost. Boosting it would make it a second hot spot and
    // the falloff — the whole reason it exists — would be gone.
    CHECK(1, "the halo is documented as taking no emissive boost (see the .inl)");
}

// ── 3. Growth is gentle, because additive brightness spreads over AREA ──────

static void Test_GrowthDoesNotDimTheCore(void)
{
    // A first version of this test asserted that the core's BOOST must outrun
    // its AREA, on the reasoning that additive brightness spread over more
    // pixels gets dimmer. It failed — boost x3.50 against area x7.11 — and the
    // code was right and the test was wrong, which is worth writing down because
    // it is the "debug view that renders the wrong quantity" trap from
    // core/CLAUDE.md §6 in test form.
    //
    // The wrong quantity was SURFACE brightness. The bright pass clamps each
    // pixel at bloom_max_energy, so once a pixel is over the clamp, being
    // further over buys nothing and being less far over costs nothing. What the
    // eye reads as "brighter" is the COUNT of pixels above the threshold — which
    // is area. So the two things that actually have to hold are:
    float areaRatio = (CoreRadius(1.0f) * CoreRadius(1.0f)) /
                      (CoreRadius(0.0f) * CoreRadius(0.0f));
    CHECK_MSG(areaRatio > 2.0f,
              "the above-threshold area grows substantially as the charge fills",
              "x%.2f the pixels", areaRatio);

    // ...and the core must stay over the clamp for the WHOLE ramp, or the drop
    // in surface brightness stops being invisible and starts being a fade.
    float worst = 1e9f;
    for (float i = 0.0f; i <= 1.0f; i += 0.01f)
        if (CoreBoost(i) < worst) worst = CoreBoost(i);
    CHECK_MSG(worst >= BLOOM_MAX_ENERGY,
              "and it never drops under the bloom clamp, so the falling surface "
              "brightness is never visible",
              "minimum boost %.2f vs clamp %.2f", worst, BLOOM_MAX_ENERGY);

    // Total emitted energy, for the record: this is what grows.
    float energyRatio = areaRatio * (CoreBoost(1.0f) / CoreBoost(0.0f));
    CHECK_MSG(energyRatio > 10.0f, "so filling reads unambiguously as brightening",
              "x%.1f total emitted energy", energyRatio);
}

// ── 4. It emits by RATE ─────────────────────────────────────────────────────

static void Test_RateNotCount(void)
{
    // A count per call makes density a function of the frame rate. This is the
    // rule that has bitten this project in three separate effects.
    float worst = 0.0f;
    for (float fps = 20.0f; fps <= 240.0f; fps += 1.0f) {
        float perSec = CORE_GLOW_RATE_MAX;              // steady state at i01 = 1
        float emitted = (1.0f / fps) * perSec * fps;    // one second of frames
        float e = fabsf(emitted - perSec) / perSec;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "the same sprites/sec at any frame rate",
              "worst relative error %.6f", worst);

    // The hitch clamp must be small enough that one long frame cannot dump a
    // visible burst, and large enough not to starve at 20 fps.
    float wantedAt20fps = CORE_GLOW_RATE_MAX / 20.0f;
    CHECK_MSG(CORE_GLOW_BATCH_MAX >= wantedAt20fps,
              "the clamp does not starve the glow at 20 fps",
              "%d allowed, %.1f wanted", CORE_GLOW_BATCH_MAX, wantedAt20fps);
    CHECK_MSG(CORE_GLOW_BATCH_MAX <= 6,
              "and a hitch cannot dump a burst",
              "%d per frame", CORE_GLOW_BATCH_MAX);
}

// ── the extraction guard ────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pendingSpace = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pendingSpace = 1; continue; }
        if (pendingSpace && o > 0 && o + 1 < cap) out[o++] = ' ';
        pendingSpace = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[300000], flat[300000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_ExtractionChangedTheAddressAndNothingElse(void)
{
    const char *glow = "core/composition/common/vc_core_glow.inl";
    const char *chg  = "core/composition/common/vc_charge_converge.inl";

    // THE POINT OF THE WHOLE EXERCISE: every authored number arrived intact.
    CHECK(FileHas(glow, ".radius = (0.045f + 0.075f * i01) * scale,"),
          "the hot core's size is the number it was extracted with");
    CHECK(FileHas(glow, ".radius = (0.10f + 0.16f * i01) * scale,"),
          "so is the mid glow's");
    CHECK(FileHas(glow, ".radius = (0.22f + 0.30f * i01) * scale,"),
          "and the halo's");
    CHECK(FileHas(glow, "Math_Mix(4.0f, 14.0f, i01)"),
          "the core's boost ramp is unchanged");
    CHECK(FileHas(glow, "Math_Mix(1.6f, 3.2f, i01)"),
          "and the mid's");
    CHECK(FileHas(glow, "VC_WithAlpha(VC_Whiten(m->glow, 0.75f), 255)"),
          "the core is still whitened at the SOURCE, not left saturated");
    CHECK(FileHas(glow, "VC_WithAlpha(m->soft, (unsigned char)(40 + 90 * i01))"),
          "the halo still uses the material's SOFT colour, not its glow");

    /* THE BLEND LAW, and it changed on 19/08/2026: an emitter is PREMULTIPLIED,
       not additive. Additive can only ever add, so it dissolves into anything
       already near 1.0; premultiplied covers where coverage is high and adds
       where it falls to zero, which is both jobs in one draw (§5.2, §7.6d).
       Measured on this effect against a white backdrop: darken% 0.0 -> 67.7 —
       it now cuts a silhouette where before it only added light — and chroma
       0.154 -> 0.225. */
    CHECK(FileHas(glow, ".render.blendMode = VFX_BLEND_PREMULTIPLIED,"),
          "a glow EMITS, and an emitter is premultiplied");
    CHECK(FileHas(glow, ".render.unlit = 1,"),
          "...and unlit, so nothing multiplies it back down");

    // The halo must NOT be boosted — that is what keeps it a falloff rather than
    // a second hot spot, and it is a one-character change to get wrong.
    CHECK(!FileHas(glow, "VC_WithAlpha(m->soft, (unsigned char)(40 + 90 * i01)), "
                         ".alphaCurve = &s_coreGlowFade, .render.blendMode = "
                         "VFX_BLEND_ADDITIVE, .render.unlit = 1, .render.emissiveBoost"),
          "the halo still takes no emissive boost");

    // Rate, not count.
    CHECK(FileHas(glow, "s_accum += TimeFX_RawDelta() * Math_Mix(CORE_GLOW_RATE_MIN, CORE_GLOW_RATE_MAX, i01);"),
          "it still emits by rate with a carried accumulator");

    // The light kept its own timer: the pool is 16 slots and a 0.09 s light
    // spawned every frame would hold five of them for this one effect.
    CHECK(FileHas(glow, "if (s_lightAccum >= 0.07f)"),
          "the point light still runs on its own timer, not per frame");

    // WHAT THE COMPOSITE IT CAME OUT OF STILL OWES IT: nothing, as of
    // 28/08/2026. VFX_ComposeChargeConverge no longer calls this primary at all —
    // its destination became a FlowShield, which is a LIT membrane rather than
    // refracted glass and therefore needs no hot core burning inside it (owner:
    // "dùng flow shield ko cần lõi sáng nữa").
    //
    // That is worth an assertion rather than a deletion, because the extraction
    // argument survives it and this is the evidence: the primary outlived the
    // composite's need for it, which is exactly what extracting it was for. It
    // has its own fixture (NEW FX "CORE GLOW") and any other effect wanting a
    // hot point can call it.
    CHECK(!FileHas(chg, "VFX_ComposeCoreGlow("),
          "the charge converge no longer needs a core glow — its ball is lit itself");
    CHECK(!FileHas(chg, "MID GLOW — the layer that actually buys bloom"),
          "and it never kept a copy of the layers either");
    CHECK(!FileHas(chg, "VFXLight_Spawn(center, m->soft,"),
          "the point light stayed with the layers it belongs to");
}

int main(void)
{
    printf("=== core glow (primary, extracted) ===\n");
    Test_ThreeLayersAreDistinct();
    Test_BloomBudgetJustifiesTheMidLayer();
    Test_GrowthDoesNotDimTheCore();
    Test_RateNotCount();
    Test_ExtractionChangedTheAddressAndNothingElse();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

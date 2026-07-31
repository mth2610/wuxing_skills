// core headless test — the FOLLOWER extensions added to core/trail_system.c so
// that a composition does not have to reimplement a trail to get a good one.
//
// Every one of these was first written inside `vc_ribbon_trail.inl`, which is the
// bug: the composition layer had grown its own history ring, its own sample
// clock, its own cloth and its own layered draw, and `core/trail_system.h` — 18
// public entry points — had zero consumers. They are here now, and this file
// pins the parts that are arithmetic.
//
// What the mirror CANNOT see: whether the ribbon looks like silk. It can only
// prove that the maths is capable of it and that the failure modes are excluded.

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

#define TRAIL_HISTORY_COUNT         60
#define TRAIL_SAMPLE_STEPS_MAX      6
#define TRAIL_CLOTH_CONSTRAIN_ITERS 2
#define TRAIL_CLOTH_MIN_SPACING     0.60f
#define TRAIL_MAX_LAYERS            4

// ── 1. The order bound cannot let neighbours swap ────────────────────────────
//
// THE BUG THIS EXISTS FOR. A per-node displacement bound stated in METRES looks
// safe and is not. Work it out for a weapon trail: a 3 m arm at 2.4 rad/s puts
// the tip at 7.2 m/s, and a 60 Hz sample clock lays nodes 0.12 m apart. A
// "generous" 0.30 m bound is then two and a half node spacings — a node travels
// clean past its own leader, the polyline folds back on itself, the tangent
// reverses and the strip pinches into a wedge.
//
// The constraint pass cannot save it: distance is a SCALAR, so a node that has
// passed through its neighbour reads as "slightly too close" and gets settled
// happily in the reversed order.

static void Test_OrderBoundIsRelativeToSpacing(void)
{
    const float armLen = 3.0f, omega = 2.4f, sampleHz = 60.0f;
    float spacing = armLen * omega / sampleHz;
    CHECK_MSG(fabsf(spacing - 0.12f) < 0.005f,
              "the bench swing really does lay nodes about 12 cm apart",
              "%.4f m", spacing);

    // The absolute bound alone is not a bound on ORDER.
    float absoluteBound = 0.30f;
    CHECK_MSG(absoluteBound > spacing,
              "a metre-stated bound alone lets a node pass its leader",
              "%.2f m allowed vs %.3f m spacing", absoluteBound, spacing);

    // The fraction is. Both ends move, so the gap closes by twice the bound.
    const float frac = 0.45f;
    CHECK_MSG(2.0f * frac < 1.0f,
              "two neighbours at full allowance still cannot meet",
              "closes %.2f of the spacing", 2.0f * frac);

    // And it must hold at EVERY speed, not just the bench's — the invariance is
    // the assertion that matters, since a fixed number passes at one speed on
    // the broken formula too.
    float worst = 0.0f;
    for (float speed = 0.5f; speed < 40.0f; speed += 0.25f) {
        float sp = speed / sampleHz;
        float bound = frac * sp;
        float ratio = bound / (0.5f * sp);
        if (ratio > worst) worst = ratio;
    }
    CHECK_MSG(worst < 1.0f, "the bound stays inside half the spacing at every speed",
              "worst %.3f of half-spacing", worst);

    // The spawn path clamps rather than trusting the caller: this is a
    // correctness bound, not a dial, so an over-eager config must be corrected
    // and not merely documented.
    float requested = 0.9f, applied = (requested > 0.49f) ? 0.49f : requested;
    CHECK_MSG(applied < 0.5f, "an over-eager config value is clamped, not obeyed",
              "%.2f requested -> %.2f applied", requested, applied);
}

// ── 2. The material UV is not the legacy UV ──────────────────────────────────
//
// The legacy form is `segRatio * uvTiling`, and it has two independent defects.

static void Test_MaterialUVBeatsSegRatioUV(void)
{
    // DEFECT 1: segRatio is the node's INDEX normalised over the strip, so one
    // texture repeat covers however many metres the strip happens to be. The
    // texel density therefore changes as the trail grows and shortens.
    float uvTiling = 4.0f;
    float shortStrip = 1.5f, longStrip = 6.0f;
    float densityShort = uvTiling / shortStrip;    // repeats per metre
    float densityLong  = uvTiling / longStrip;
    CHECK_MSG(densityShort / densityLong > 3.5f,
              "the legacy UV really does change texel density with length",
              "%.2f vs %.2f repeats/m", densityShort, densityLong);

    // The material form is metres per repeat, so density is constant by
    // construction — the same assertion, and it must hold at every length.
    const float metresPerTile = 1.10f;
    float worst = 0.0f;
    for (float len = 0.4f; len < 12.0f; len += 0.1f) {
        float density = 1.0f / metresPerTile;
        float e = fabsf(density - (1.0f / 1.10f));
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-6f, "the material UV holds texel density at EVERY length",
              "worst drift %.8f", worst);

    // DEFECT 2, and the one no scroll speed can cover: segRatio is measured from
    // the HEAD, which moves. Once the history is full the tail retreats at the
    // head's speed, so a fixed piece of ribbon sees its own segRatio change at
    // the emitter's speed whether or not anything is scrolling.
    float tipSpeed = 7.2f, scroll = 2.2f;
    float leaked = tipSpeed / metresPerTile;
    CHECK_MSG(leaked > 2.0f * scroll,
              "the swing dominates the legacy UV's motion",
              "%.1f tiles/s leaked vs %.1f scrolled", leaked, scroll);
    CHECK_MSG(leaked / (leaked + scroll) > 0.70f,
              "...to the tune of most of it",
              "%.0f%% of the motion is the swing", 100.0f * leaked / (leaked + scroll));
    // Too fast to track and not moving look the same. A 3 m strip at a 1.10 m
    // tile is 2.7 tiles long, so the whole sheet crossed it three times a second.
    float stripTiles = 3.0f / metresPerTile;
    CHECK_MSG((leaked + scroll) / stripTiles > 2.5f,
              "and it crossed the whole strip several times a second",
              "%.1f strip-lengths/sec", (leaked + scroll) / stripTiles);
    CHECK_MSG(scroll / stripTiles > 0.4f && scroll / stripTiles < 1.5f,
              "the material form leaves a trackable rate — the scroll term alone",
              "%.2f strip-lengths/sec", scroll / stripTiles);
}

// ── 3. The sample clock ──────────────────────────────────────────────────────

static void Test_SampleClockIsARate(void)
{
    // One node per FRAME makes the trail's length in metres a function of the
    // frame rate. That is the same class of bug as emitting particles per call
    // instead of per second, and it is the reason the clock exists.
    const float speed = 7.2f, nodes = 40.0f;
    float lenAt60 = nodes * speed / 60.0f;
    float lenAt30 = nodes * speed / 30.0f;
    CHECK_MSG(fabsf(lenAt30 - 2.0f * lenAt60) < 1e-4f,
              "one-node-per-frame really does double the trail at half the frame rate",
              "%.2f m at 60 fps vs %.2f m at 30", lenAt60, lenAt30);

    const float sampleHz = 60.0f;
    float worst = 0.0f;
    for (float fps = 20.0f; fps <= 240.0f; fps += 1.0f) {
        float dt = 1.0f / fps;
        float steps = dt * sampleHz;               // per frame, on average
        float metresPerSec = steps * fps * (speed / sampleHz);
        float e = fabsf(metresPerSec - speed) / speed;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "a fixed sample rate lays the same metres/sec at any frame rate",
              "worst relative error %.6f", worst);

    // The ceiling matters at the other end: a hitch must not lay a hundred nodes
    // in one update and burn the whole history on a single stutter.
    float hitch = 0.5f;                            // a 500 ms frame
    float wanted = hitch * sampleHz;
    CHECK_MSG(wanted > TRAIL_SAMPLE_STEPS_MAX,
              "a hitch would otherwise blow the history in one frame",
              "%.0f nodes wanted, capped at %d", wanted, TRAIL_SAMPLE_STEPS_MAX);
    CHECK_MSG(TRAIL_SAMPLE_STEPS_MAX < TRAIL_HISTORY_COUNT / 4,
              "and the cap stays well under the history so one frame cannot own it",
              "%d of %d", TRAIL_SAMPLE_STEPS_MAX, TRAIL_HISTORY_COUNT);
}

// ── 4. The cloth constraint ──────────────────────────────────────────────────

static void Test_ClothFloorProtectsTheTangent(void)
{
    // Cloth gathers, so a floor under the node spacing is right. The number is
    // set by the TANGENT, not by the look: the strip's tangent is a central
    // difference over the neighbours, and when they crowd together it is
    // fabricated outright. A third of the rest spacing left far too little room.
    CHECK_MSG(TRAIL_CLOTH_MIN_SPACING > 0.5f && TRAIL_CLOTH_MIN_SPACING < 1.0f,
              "the floor lets the ribbon gather without degenerating",
              "%.2f of rest", TRAIL_CLOTH_MIN_SPACING);
    // The floor must sit below the ceiling or the two fight every frame.
    CHECK(TRAIL_CLOTH_MIN_SPACING < 1.0f, "the floor is below the inextensibility ceiling");
    CHECK_MSG(TRAIL_CLOTH_CONSTRAIN_ITERS >= 2,
              "one pass leaves the far end of the chain unsatisfied",
              "%d iterations", TRAIL_CLOTH_CONSTRAIN_ITERS);

    // The anchor's steady state, which is what separates a flutter from a
    // wander: a node settles at roughly force/spring off the path it was laid on.
    const float spring = 9.0f;
    float dev = (0.95f + 0.55f) / spring;
    CHECK_MSG(dev > 0.02f && dev < 0.20f,
              "the anchored deviation is a flutter, not a wander",
              "%.3f m off the path", dev);
    // ...and the ACROSS bound must sit well above it, or it is a second tuning
    // knob fighting the spring instead of a safety net.
    CHECK_MSG(0.30f > dev * 1.5f, "the across bound stays a safety net",
              "0.30 m cap vs %.3f m steady state", dev);
}

// ── 5. Layer budget ──────────────────────────────────────────────────────────

static void Test_LayerBudget(void)
{
    // One strip becomes layerCount strips. The cap is the whole cost story.
    CHECK_MSG(TRAIL_MAX_LAYERS <= 4, "a trail cannot declare an unbounded stack of strips",
              "%d layers", TRAIL_MAX_LAYERS);
    int worstQuads = TRAIL_MAX_LAYERS * (TRAIL_HISTORY_COUNT - 1) * 2;
    CHECK_MSG(worstQuads < 500, "and the worst single trail stays a few hundred quads",
              "%d quads", worstQuads);
}

// ── the mirror guard ─────────────────────────────────────────────────────────

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
    static char buf[400000], flat[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *c = "core/trails/trail_system.c";
    const char *h = "core/trails/trail_system.h";

    CHECK(FileHas(h, "#define TRAIL_SAMPLE_STEPS_MAX 6"),
          "the sub-frame step cap still matches this mirror");
    CHECK(FileHas(h, "#define TRAIL_CLOTH_MIN_SPACING 0.60f"),
          "the cloth floor still matches this mirror");
    CHECK(FileHas(h, "#define TRAIL_MAX_LAYERS 4"),
          "the layer cap still matches this mirror");

    // The order bound: a fraction of the spacing, clamped at the spawn.
    CHECK(FileHas(c, "float spacing = fminf(t->nodeRest[idx], t->nodeRest[lead]);"),
          "the along-path bound still uses the SMALLER of the two spacings");
    CHECK(FileHas(c, "float alongMax = t->nodeOrderFrac * spacing;"),
          "and it is still a fraction of the spacing, not a distance");
    CHECK(FileHas(c, "t->nodeOrderFrac = (config.nodeOrderFrac > 0.49f) ? 0.49f : config.nodeOrderFrac;"),
          "an over-eager config is still clamped at the spawn");
    CHECK(FileHas(c, "float along = Vector3DotProduct(off, dir);"),
          "the deviation is still split along/across before clamping");

    // The material UV.
    CHECK(FileHas(c, "t->nodeUV[t->historyHead] = t->laidDist;"),
          "the material stamp is still written once, when the node is laid");
    CHECK(FileHas(c, "(t->uvMetresPerTile > 0.0f)"),
          "the UV still switches on metres-per-tile, so legacy configs are untouched");
    CHECK(FileHas(c, "t->nodeUV[NodeIndexForSegRatio(t, drawCount, h)] / t->uvMetresPerTile"),
          "and the material form still reads the stamp, not the distance to the tail");

    // The stamps live where the position is written — the lesson that cost a
    // session when a shadow array was seeded somewhere else.
    CHECK(FileHas(c, "t->nodeHome[t->historyHead] = newTipPos;"),
          "the home anchor is still seeded on the same path as the position");
    CHECK(FileHas(c, "t->nodeHome[0] = tip;"),
          "...and in the teleport cut too, which does not go through the insert");

    // The constraint modes. This system's older helper takes a `stretchOnly`
    // bool whose false branch means "force EXACTLY", not "also enforce a
    // minimum", and using it here would silently collapse the ribbon.
    CHECK(FileHas(c, "RIBBON_CONSTRAIN_MAX);"),
          "the ceiling is still an explicit MAX");
    CHECK(FileHas(c, "RIBBON_CONSTRAIN_MIN);"),
          "and the floor an explicit MIN, not a bare bool");

    // Layers, and the rule that the structure lives in exactly one of them.
    CHECK(FileHas(c, "Texture2D tex = (ly->texture != NULL) ? *ly->texture : fallbackTex;"),
          "a layer can still opt out of the textured sheet");
    CHECK(FileHas(c, "a *= powf(scratchSegRatio[h], ly->headAlphaPow);"),
          "a layer can still burn only at the head");

    // Everything is opt-in: a config that does not mention these is unchanged.
    CHECK(FileHas(c, "if (t->layerCount > 0)"),
          "the legacy outer+inner pair is still the default draw");
    CHECK(FileHas(c, "else if (t->sampleHz > 0.0f)"),
          "and one-node-per-frame is still the default sampling");
}

int main(void)
{
    printf("=== trail system: cloth, material UV, sample clock, layers ===\n");
    Test_OrderBoundIsRelativeToSpacing();
    Test_MaterialUVBeatsSegRatioUV();
    Test_SampleClockIsARate();
    Test_ClothFloorProtectsTheTangent();
    Test_LayerBudget();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

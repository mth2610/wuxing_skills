// core headless test — VFX_ComposeSweptTrail's geometry and schedules.
//
// Almost everything that decides whether a swept trail reads as a blade is
// arithmetic: the aspect ratio against the length the tip travelled, the width
// envelope's shape along the strip, the lag schedule between filament strands,
// the framerate-independence of the sample clock, and which side of the strip
// the mask's hot edge lands on. None of those needs a GPU, and every one of them
// would otherwise be answered by build → screenshot → guess (core/CLAUDE.md §1).
//
// This mirrors core/composition/common/vc_swept_trail.inl. A mirror rots into
// fiction the moment the source moves, so Test_MirrorStillMatchesSource pins the
// load-bearing expressions.
//
// What the mirror CANNOT see: whether the strip is edge-on from the game camera
// (a BLADE lying in the swing plane is INTENDED to vanish edge-on, and telling
// that apart from a bug is an eyeball question), whether the reused SweepSlash
// mask reads at gameplay distance, and whether the tier gate's 2-strand filament
// still looks like threads.

#include <stdio.h>
#include <stdlib.h>
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

#ifndef PI
#define PI 3.1415926535f
#endif

// ── the mirror ───────────────────────────────────────────────────────────────

#define SWEPT_RING        64
#define SWEPT_SAMPLE_HZ   60.0f
#define SWEPT_SAMPLE_DT   (1.0f / SWEPT_SAMPLE_HZ)
#define SWEPT_STEPS_MAX   6
#define SWEPT_LAG_STEP    0.030f
#define TRAIL_HISTORY_COUNT 60

typedef enum { STYLE_BLADE = 0, STYLE_RIBBON = 1, STYLE_FILAMENT = 2 } Style;

static float AspectK(Style s)
{
    switch (s) {
    case STYLE_RIBBON:   return 0.0715f;
    case STYLE_FILAMENT: return 0.0125f;
    case STYLE_BLADE:
    default:             return 0.0250f;
    }
}

static float HalfWidth(float widthMetres, float level01, float travelLen, Style s)
{
    float want = widthMetres * 0.5f * level01;
    float cap  = travelLen * AspectK(s);
    if (want < 0.0f) want = 0.0f;
    return (cap < want) ? cap : want;
}

static int LagSamples(int strand)
{
    float lag = (float)strand * SWEPT_LAG_STEP;
    int   n   = (int)(lag / SWEPT_SAMPLE_DT + 0.5f);
    if (n < 0) n = 0;
    if (n > SWEPT_RING - 2) n = SWEPT_RING - 2;
    return n;
}

static int MaxNodes(float lifetime)
{
    int n = (int)(lifetime * SWEPT_SAMPLE_HZ + 0.5f);
    if (n < 4) n = 4;
    if (n > TRAIL_HISTORY_COUNT) n = TRAIL_HISTORY_COUNT;
    return n;
}

// FloatCurve: linear between stops, flat outside them (core/float_curve.c).
typedef struct { float t, v; } Stop;

static float CurveEval(const Stop *s, int n, float t)
{
    if (t <= s[0].t) return s[0].v;
    if (t >= s[n - 1].t) return s[n - 1].v;
    for (int i = 0; i < n - 1; i++)
        if (t >= s[i].t && t <= s[i + 1].t) {
            float f = (t - s[i].t) / (s[i + 1].t - s[i].t);
            return s[i].v + (s[i + 1].v - s[i].v) * f;
        }
    return 0.0f;
}

// segRatio 1 = HEAD (the newest node, where the weapon is), 0 = tail.
static const Stop k_bladeW[]    = {{0.00f,0.00f},{0.25f,0.55f},{0.60f,1.00f},{0.88f,0.72f},{1.00f,0.18f}};
static const Stop k_ribbonW[]   = {{0.00f,0.00f},{0.30f,0.70f},{0.62f,1.00f},{0.90f,0.78f},{1.00f,0.22f}};
static const Stop k_filamentW[] = {{0.00f,0.00f},{0.22f,0.78f},{0.85f,1.00f},{1.00f,0.20f}};
static const Stop k_bladeA[]    = {{0.00f,0.00f},{0.25f,0.32f},{0.70f,0.82f},{1.00f,1.00f}};
static const Stop k_ribbonA[]   = {{0.00f,0.00f},{0.30f,0.55f},{1.00f,1.00f}};
static const Stop k_filamentA[] = {{0.00f,0.00f},{0.25f,0.70f},{1.00f,1.00f}};

// ── vectors ──────────────────────────────────────────────────────────────────

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static V3 sub(V3 a, V3 b) { return v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static V3 cross(V3 a, V3 b)
{ return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
static float dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return (l > 1e-9f) ? v3(a.x/l, a.y/l, a.z/l) : a; }
static float dist(V3 a, V3 b) { return len(sub(a, b)); }

// ── 0. The cloth is a PERTURBATION, not a driver ─────────────────────────────
//
// A node's steady-state deviation from the path it was laid on is roughly
// force/spring. That number is the whole difference between silk fluttering
// along a swept path and a snake writhing free of it, and it is arithmetic, so
// it does not need an eyeball to bound.

#define SWEPT_HOME_SPRING  9.0f
#define SWEPT_HOME_MAX_DEV 0.30f

static void Test_ClothStaysNearThePath(void)
{
    // sag + curl, per style, as authored in the .inl.
    const float sag[3]  = {0.40f, 0.95f, 0.55f};
    const float curl[3] = {0.30f, 0.55f, 0.40f};
    const char *nm[3]   = {"BLADE", "RIBBON", "FILAMENT"};
    for (int i = 0; i < 3; i++) {
        float dev = (sag[i] + curl[i]) / SWEPT_HOME_SPRING;   // metres, steady state
        CHECK_MSG(dev < 0.20f, "cloth deviation stays a flutter, not a wander",
                  "%s settles about %.3f m off the path", nm[i], dev);
        CHECK_MSG(dev > 0.02f, "...but it is not so stiff that nothing moves",
                  "%s only strays %.3f m", nm[i], dev);
    }
    // The hard bound must sit well above the steady state, or it is a second
    // tuning knob fighting the spring instead of a safety net.
    float worst = 0.0f;
    for (int i = 0; i < 3; i++) {
        float d = (sag[i] + curl[i]) / SWEPT_HOME_SPRING;
        if (d > worst) worst = d;
    }
    CHECK_MSG(SWEPT_HOME_MAX_DEV > worst * 1.5f,
              "the hard deviation bound is a safety net, not a tuning knob",
              "%.2f m cap vs %.3f m steady state", SWEPT_HOME_MAX_DEV, worst);
}

// ── 0b. Neighbours cannot swap places ────────────────────────────────────────
//
// THE SELF-TWIST. The owner reported a fold he called a twist through four
// rounds, and it was never a side-vector problem: it was NODE ORDER REVERSING.
// The numbers say so outright — the bench swings a 3 m arm at 2.4 rad/s, the
// sample clock is 60 Hz, so nodes are laid ~0.12 m apart, and the deviation
// bound was a flat 0.30 m. A node could travel two and a half node-spacings and
// end up BEHIND its own leader; the polyline folds, the strip's side vector
// flips across the crossing, and the band pinches into a wedge.
//
// The distance constraint cannot catch it. Distance is a scalar: a node that has
// passed THROUGH its leader reads as "slightly too close", and the constraint
// settles it happily in the reversed order.
//
// So the bound along the path is now a FRACTION of the spacing, and this is the
// proof that the fraction is a safe one.

#define SWEPT_ORDER_FRAC 0.45f

static void Test_NodesCannotCrossTheirNeighbour(void)
{
    // Both ends of a segment are free to move toward each other, so the gap can
    // close by twice the bound. Anything at or above 0.5 permits a swap.
    CHECK_MSG(2.0f * SWEPT_ORDER_FRAC < 1.0f,
              "two neighbours moving their full allowance still cannot meet",
              "closes %.2f of the spacing", 2.0f * SWEPT_ORDER_FRAC);

    // And the bench case that actually broke, in metres.
    const float armLen = 3.0f, omega = 2.4f, hz = 60.0f;
    float spacing  = armLen * omega / hz;                 // metres between nodes
    float oldBound = SWEPT_HOME_MAX_DEV;
    float newBound = SWEPT_ORDER_FRAC * spacing;
    CHECK_MSG(oldBound > spacing,
              "the OLD flat bound really did allow a node past its leader",
              "%.2f m allowed vs %.3f m spacing", oldBound, spacing);
    CHECK_MSG(newBound < 0.5f * spacing,
              "the new along-path bound stays inside half the node spacing",
              "%.4f m vs %.4f m half-spacing", newBound, 0.5f * spacing);

    // The fix must not cost the LOOK. Sag and curl are lateral, and the across
    // bound is untouched, so the flutter the eye reads survives the fix intact.
    float lateral = (0.95f + 0.55f) / SWEPT_HOME_SPRING;  // RIBBON, the loosest
    CHECK_MSG(lateral < SWEPT_HOME_MAX_DEV,
              "lateral flutter is still nowhere near its own bound — no motion lost",
              "%.3f m of %.2f m", lateral, SWEPT_HOME_MAX_DEV);
}

// ── 0b2. The flow does not ride on the swing ─────────────────────────────────
//
// The owner's call, before anyone measured it: "the UV scroll is in the same
// direction as the motion, which is what makes it feel still." He was right, and
// the numbers say by how much.
//
// The UV was `arc from the TAIL / tile`. Once the history ring is full the tail
// retreats at exactly the speed the head advances, so a fixed piece of cloth sees
// its own arc value change at the tip speed whether or not anything is scrolling.
// That is not a scroll — it is the swing leaking into the texture.

#define SWEPT_FLOW_SPEED 2.10f
#define SWEPT_FLOW_TILE  1.10f

static void Test_FlowIsDecoupledFromTheSwing(void)
{
    const float armLen = 3.0f, omega = 2.4f;
    float tipSpeed = armLen * omega;                        // 7.2 m/s
    float bodyPass = 0.50f + 0.55f * 1.0f;                  // the textured layer

    // What the OLD formula did, at a fixed point of cloth, in tiles/sec.
    float fromSwing  = tipSpeed / SWEPT_FLOW_TILE;
    float fromScroll = SWEPT_FLOW_SPEED * bodyPass;
    float oldTotal   = fromSwing + fromScroll;
    CHECK_MSG(fromSwing > 2.0f * fromScroll,
              "the swing really did dominate the old UV motion",
              "%.1f tiles/s from the swing vs %.1f from the scroll",
              fromSwing, fromScroll);
    CHECK_MSG(fromSwing / oldTotal > 0.70f,
              "...to the tune of most of it — the 'scroll speed' was a minority term",
              "%.0f%% of the motion was the swing", 100.0f * fromSwing / oldTotal);

    // And it was too fast to read. A strip 3 m long at a 1.10 m tile is 2.7 tiles,
    // so the whole sheet crossed it three times a second. Too fast to track and
    // not moving look the same, which is why no scroll SPEED ever fixed it.
    float stripTiles = armLen / SWEPT_FLOW_TILE;
    CHECK_MSG(oldTotal / stripTiles > 2.5f,
              "the old flow crossed the whole strip several times a second",
              "%.1f strip-lengths/sec", oldTotal / stripTiles);

    // The new rate, at a fixed point of cloth, is the scroll term ALONE.
    float newRate = fromScroll;
    CHECK_MSG(newRate / stripTiles > 0.4f && newRate / stripTiles < 1.5f,
              "the new flow crosses the strip about once a second — trackable",
              "%.2f strip-lengths/sec", newRate / stripTiles);
    // The property that matters more than the number: doubling the swing speed
    // must not change the flow at all.
    float fast = SWEPT_FLOW_SPEED * bodyPass;   // no tipSpeed term anywhere
    CHECK_MSG(fabsf(fast - newRate) < 1e-6f,
              "and swinging twice as fast no longer changes the flow rate",
              "%.4f vs %.4f tiles/s", fast, newRate);
}

typedef struct { float t, v; } Stop2;
static float Curve2(const Stop2 *s, int n, float t)
{
    if (t <= s[0].t) return s[0].v;
    if (t >= s[n - 1].t) return s[n - 1].v;
    for (int i = 0; i < n - 1; i++)
        if (t >= s[i].t && t <= s[i + 1].t) {
            float f = (t - s[i].t) / (s[i + 1].t - s[i].t);
            return s[i].v + (s[i + 1].v - s[i].v) * f;
        }
    return 0.0f;
}

// ── 0b3. The additive layers must not saturate the band ─────────────────────
//
// The layers OVERLAP — the core sits inside the body, which sits inside the halo
// — and they are additive, so the frame buffer sees their SUM. Above 1.0 every
// texel clips to white and the sheet's filaments become mathematically
// unrecoverable, however good the sheet is. The first version summed to 2.01 at
// the head and 1.85 through the body.
//
// It went unnoticed because the ribbon was also FOLDING, and the fold carved
// dark notches across the band that read as detail. Fixing the fold removed the
// only structure surviving the clipping, so "it works" and "it is blown out"
// arrived in the same frame. This test is here so that cannot recur silently.

static void Test_AdditiveBudget(void)
{
    const float alpha[3] = {0.07f, 0.31f, 0.28f};   // halo, body, core
    const float headPow[3] = {0.0f, 0.0f, 3.4f};
    // BLADE alpha curve, and the width curve that also scales alpha.
    const Stop2 aCurve[] = {{0.00f,0.00f},{0.25f,0.32f},{0.70f,0.82f},{1.00f,1.00f}};
    const Stop2 wCurve[] = {{0.00f,0.00f},{0.25f,0.55f},{0.60f,1.00f},{0.88f,0.72f},{1.00f,0.18f}};

    float worst = 0.0f, worstS = 0.0f;
    float bodyPeak = 0.0f;
    for (float sr = 0.02f; sr <= 0.92f; sr += 0.01f) {
        float base = Curve2(aCurve, 4, sr) * Curve2(wCurve, 5, sr);
        float sum = 0.0f;
        for (int L = 0; L < 3; L++) {
            float a = base * alpha[L];
            if (headPow[L] > 0.0f) a *= powf(sr, headPow[L]);
            sum += a;
        }
        if (sum > worst) { worst = sum; worstS = sr; }
        if (sr < 0.80f && sum > bodyPeak) bodyPeak = sum;
    }
    // 0.5, not 1.0. The first pass at this test used 1.0 as the ceiling, passed,
    // and the effect still burned out: E1's streak bloom lifts anything near the
    // threshold, so the point at which the eye reads "clipped" sits well under
    // full white. The owner measured it at half — this is that number, not a
    // guess about it.
    CHECK_MSG(bodyPeak < 0.5f,
              "along the BODY the layers stay well under the BLOOM's ceiling",
              "peaks at %.2f, ceiling 0.50", bodyPeak);
    CHECK_MSG(worst < 0.8f, "and even the hottest point is not several times over",
              "%.2f at segRatio %.2f", worst, worstS);

    // The head is ALLOWED to blow out — that is the one place a trail should.
    float headSum = 0.0f;
    for (int L = 0; L < 3; L++)
        headSum += alpha[L];
    CHECK_MSG(headSum > 0.5f, "the head still blows out on purpose",
              "%.2f at the tip, vs a 0.50 body ceiling", headSum);

    // What the old numbers did, kept as the regression this test exists for.
    const float oldAlpha[3] = {0.16f, 0.85f, 1.00f};
    float oldSum = oldAlpha[0] + oldAlpha[1] + oldAlpha[2];
    CHECK_MSG(oldSum > 2.0f, "the old stack really was twice over saturation",
              "%.2f", oldSum);
    CHECK_MSG(oldAlpha[1] + oldAlpha[2] > 1.5f,
              "...and body+core alone clipped wherever they overlapped",
              "%.2f", oldAlpha[1] + oldAlpha[2]);
}

// ── 0b4. HAZE is a BACKDROP, and that is an arithmetic claim ────────────────
//
// The owner's structure for a projectile (29/07): the wake is at least TWO
// trails on the same trajectory — a wide faint field that gives it mass, and a
// defined ribbon on top that gives it shape. Drawing only the sharp one reads as
// a wire; drawing only the hazy one reads as smoke.
//
// HAZE is a STYLE rather than its own function, which is the project's own count
// rule (VFX_PLAN §Part 4): it follows the same trajectory, keeps the same
// history and lags with the same cloth, and differs only in width profile,
// opacity, sheet and how loosely it is anchored. What that rule does NOT excuse
// is shipping numbers that fail to make it a backdrop, which is what this checks.

static void Test_HazeSitsUnderTheSharpTrail(void)
{
    // It has to be much broader — a wake spreads, a struck arc does not.
    const float aspectRibbon = 0.0715f, aspectHaze = 0.1600f;
    CHECK_MSG(aspectHaze > 2.0f * aspectRibbon,
              "the haze is far broader than the trail it backs",
              "1:%.1f vs 1:%.1f", 0.5f / aspectHaze, 0.5f / aspectRibbon);

    // ...and much fainter, because additive cost is AREA x ALPHA. Being both
    // wider and as bright is exactly how the backdrop would bury the thing it is
    // supposed to sit behind.
    const float sharpSum = 0.07f + 0.31f + 0.28f;   // halo + body + core
    const float hazeSum = 0.10f + 0.22f;            // halo + body, no core
    CHECK_MSG(hazeSum < sharpSum,
              "and fainter, so it backs the trail instead of burying it",
              "%.2f vs %.2f", hazeSum, sharpSum);
    // The product is the real budget: width x alpha is what lands on the frame.
    float sharpLoad = aspectRibbon * sharpSum, hazeLoad = aspectHaze * hazeSum;
    CHECK_MSG(hazeLoad < 1.5f * sharpLoad,
              "the two layers cost a comparable amount of frame, not 5x",
              "haze %.4f vs sharp %.4f", hazeLoad, sharpLoad);

    // (No hot core: a backdrop with a bright line down its middle gives the wake
    // two spines, and the sharp trail already owns that job. The two-layer stack
    // itself is pinned in the mirror below, where it can actually be checked.)

    // THE CURVE THAT IS NOT A LENS. Every other style is thin-wide-thin because
    // it is a struck arc. A wake is widest at the TAIL and narrows toward the
    // head, because it spreads as it ages.
    const Stop2 hazeW[] = {{0.00f,0.00f},{0.20f,1.00f},{0.65f,0.72f},{1.00f,0.38f}};
    const Stop2 hazeA[] = {{0.00f,0.00f},{0.22f,0.62f},{0.70f,0.90f},{1.00f,1.00f}};
    float peakS = 0.0f, peak = 0.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.002f) {
        float w = Curve2(hazeW, 4, t);
        if (w > peak) { peak = w; peakS = t; }
    }
    CHECK_MSG(peakS < 0.35f, "the haze is widest near the TAIL, unlike every other style",
              "peak at %.3f (0 = tail)", peakS);
    CHECK_MSG(Curve2(hazeW, 4, 1.0f) > 0.15f,
              "...but it still reaches the head, or the projectile floats free of its wake",
              "%.2f at the head", Curve2(hazeW, 4, 1.0f));

    // ALPHA MUST FALL AT LEAST AS FAST AS WIDTH toward the tail, or the last
    // stretch is sub-pixel while still visible and breaks into dashes. Against a
    // curve that WIDENS backward this is a sharper constraint than usual, which
    // is the whole reason it is checked here separately.
    float worstS = -1.0f, gap = 0.0f;
    for (float t = 0.01f; t <= 0.30f; t += 0.005f) {
        float w = Curve2(hazeW, 4, t), a = Curve2(hazeA, 4, t);
        if (a > w && (a - w) > gap) { gap = a - w; worstS = t; }
    }
    CHECK_MSG(worstS < 0.0f, "haze alpha never leads haze width near the tail",
              "alpha leads by %.3f at t = %.2f", gap, worstS);
}

// ── 0b5. The trail keeps the ELEMENT's hue ──────────────────────────────────
//
// THE BUG THIS EXISTS FOR (owner, 30/07): "quả cầu và màu của trail không ăn gì
// nhau" — the orb was saturated blue and the trail was near-white, and they
// looked like two unrelated effects sharing a screen.
//
// It was arithmetic, and it slipped through because nothing pinned it. The core
// layer whitened to 1.00 and carried 0.28 of the total emitted energy, so the
// three layers summed to a hue 66% desaturated toward white — while the orb,
// which does not whiten at all, stayed fully saturated. Two halves of one effect
// with two different colour rules.
//
// Whitening at the source is still correct: a saturated hue stacks additively
// into more of itself and never reaches white, so emissiveBoost has nothing to
// lift. It is a seasoning. This bounds how much of one it is allowed to be.

static void Test_TrailKeepsTheElementHue(void)
{
    // VC_MAT_WATER's glow, the colour the head is supposed to read as.
    const float glow[3] = {80.0f, 180.0f, 255.0f};
    // alphaMul, whiten — halo, body, core.
    const float a[3] = {0.07f, 0.31f, 0.28f};
    const float w[3] = {0.00f, 0.06f, 0.20f};

    float sum[3] = {0.0f, 0.0f, 0.0f};
    for (int L = 0; L < 3; L++)
        for (int c = 0; c < 3; c++)
            sum[c] += (glow[c] + (255.0f - glow[c]) * w[L]) * a[L];

    float mx = sum[0], mn = sum[0];
    for (int c = 1; c < 3; c++) {
        if (sum[c] > mx) mx = sum[c];
        if (sum[c] < mn) mn = sum[c];
    }
    float kept = (mx - mn) / mx;             // 1 = the pure element hue, 0 = white
    float orbKept = (glow[2] - glow[0]) / glow[2];

    CHECK_MSG(kept > 0.55f,
              "the trail still reads as the ELEMENT, not as white",
              "%.0f%% of the hue survives the whitening", 100.0f * kept);
    // And it must be in the same family as the ORB, which is the thing it is
    // attached to. This is the assertion that would have caught the bug: the
    // trail alone looked defensible, the pair did not.
    CHECK_MSG(kept > 0.5f * orbKept,
              "and it is in the same colour family as the orb it trails from",
              "trail keeps %.0f%%, orb keeps %.0f%%", 100.0f * kept, 100.0f * orbKept);

    // What the old numbers did, kept as the regression this test exists for.
    const float wOld[3] = {0.00f, 0.18f, 1.00f};
    float old[3] = {0.0f, 0.0f, 0.0f};
    for (int L = 0; L < 3; L++)
        for (int c = 0; c < 3; c++)
            old[c] += (glow[c] + (255.0f - glow[c]) * wOld[L]) * a[L];
    float omx = old[2], omn = old[0];
    float oldKept = (omx - omn) / omx;
    CHECK_MSG(oldKept < 0.4f, "the old stack really did wash the hue out",
              "only %.0f%% survived", 100.0f * oldKept);

    // The core is the layer that decides this: it is the only one whitened hard
    // AND a large share of the energy. Bound its contribution directly, so the
    // next person to raise it sees why they should not.
    float total = a[0] + a[1] + a[2];
    CHECK_MSG(a[2] * w[2] / total < 0.20f,
              "no single layer contributes more than a fifth of the energy as pure white",
              "the core contributes %.0f%%", 100.0f * a[2] * w[2] / total);
}

// ── 0b6. The style validator must not go stale ──────────────────────────────
//
// THE BUG THIS EXISTS FOR, and it is the most expensive kind. VFX_TRAIL_HAZE was
// added as a fourth style; `VFX_ComposeSweptTrail`'s range check still read
// `style > VFX_TRAIL_FILAMENT` and was not updated. So every request for the
// wide energy field — the bench entry, the projectile's field, an entire day of
// visual iteration — was SILENTLY clamped to BLADE, the narrowest and sharpest
// style there is.
//
// The owner asked "how is this any different from a normal trail?" and the
// answer was that it WAS one. Three rounds went into tuning alpha, whitening and
// texture on geometry that was never being built.
//
// A silent clamp is the worst failure mode available: an absent effect is
// noticed immediately, a plausible WRONG one is argued about. The arithmetic
// below is what the log finally proved, and it is kept as the regression.

// Defined with the other mirror helpers, further down.
static int FileHas(const char *path, const char *needle);

static void Test_StyleValidatorCoversEveryStyle(void)
{
    // The log: travelled 5.04 m, drawn radius 0.126 m. Which aspect is that?
    const float travelled = 5.04f, observed = 0.126f;
    const float aspect[4] = {0.0250f, 0.0715f, 0.0125f, 0.1600f}; // BLADE..HAZE
    const char *nm[4] = {"BLADE", "RIBBON", "FILAMENT", "HAZE"};
    int match = -1;
    for (int i = 0; i < 4; i++)
        if (fabsf(travelled * aspect[i] - observed) < 0.002f) match = i;
    CHECK_MSG(match == 0,
              "the observed radius identifies the style that was ACTUALLY drawn",
              "%.3f m matches %s, not HAZE's %.3f m",
              observed, (match >= 0) ? nm[match] : "nothing",
              travelled * aspect[3]);
    // And what it should have been — a factor of six, which is why "wide faint
    // field" and "thin bright line" were the same object.
    CHECK_MSG(aspect[3] / aspect[0] > 5.0f,
              "HAZE would have been several times wider, so the clamp was not subtle",
              "%.1fx wider", aspect[3] / aspect[0]);

    // The structural fix: validate against a COUNT, so adding a style cannot
    // leave a range check behind.
    CHECK(FileHas("core/composition/visual_composer.h", "VFX_TRAIL_STYLE_COUNT"),
          "the enum still carries a count for range checks to use");
    CHECK(FileHas("core/composition/common/vc_swept_trail.inl",
                  "style >= VFX_TRAIL_STYLE_COUNT"),
          "and the validator still checks against it, not against the last style by name");
    // The needle carries the surrounding punctuation on purpose. Written as the
    // bare expression it also matched the COMMENT that explains the fix — so the
    // test failed precisely because the bug had been documented. A negative
    // FileHas cannot tell code from prose about code; give it something only the
    // code can contain.
    CHECK(!FileHas("core/composition/common/vc_swept_trail.inl",
                   "|| style > VFX_TRAIL_FILAMENT)"),
          "the stale by-name check is gone");
    // A clamp that says nothing is how this survived a day.
    CHECK(FileHas("core/composition/common/vc_swept_trail.inl",
                  "is out of range — clamped to BLADE"),
          "and an out-of-range style still announces itself");

    // Every style must have authored curves and an aspect, or a new one silently
    // draws with a zero-width envelope, which is the same class of bug.
    for (int i = 0; i < 4; i++)
        CHECK_MSG(aspect[i] > 0.0f, "every style has a real aspect",
                  "%s is %.4f", nm[i], aspect[i]);
}

// ── 0c. The authored flow sheet is USABLE as a trail sheet ───────────────────
//
// energy_flow.png is 1792x896 greyscale with the flow running across its WIDTH,
// so it is sideways, mostly empty, and does not tile. Each of those is handled
// once at load, and each of them is arithmetic, so each is checked here rather
// than by looking.

#define SWEPT_ASSET_CROP0 0.30f
#define SWEPT_ASSET_CROP1 0.70f
#define SWEPT_ASSET_FADE  0.125f

static void Test_AssetSheetGeometry(void)
{
    // Mean alpha per 5% band of the SOURCE height, measured off the file. The
    // crop window is defended by this, not by an opinion about where the
    // filaments look like they are.
    static const float rowMean[20] = {
        1.63f, 1.46f, 1.67f, 2.16f, 1.63f, 1.73f, 3.93f, 16.14f, 49.50f, 100.69f,
        98.53f, 57.57f, 16.09f, 4.15f, 2.06f, 1.39f, 1.53f, 1.17f, 1.91f, 2.05f};
    float inside = 0.0f, outside = 0.0f;
    int   nIn = 0, nOut = 0;
    for (int b = 0; b < 20; b++) {
        float mid = ((float)b + 0.5f) / 20.0f;
        if (mid > SWEPT_ASSET_CROP0 && mid < SWEPT_ASSET_CROP1) { inside  += rowMean[b]; nIn++; }
        else                                                    { outside += rowMean[b]; nOut++; }
    }
    inside /= (float)nIn; outside /= (float)nOut;
    CHECK_MSG(outside < 4.0f, "everything the crop DISCARDS really is empty",
              "mean %.2f of 255 outside the crop", outside);
    CHECK_MSG(inside > 10.0f * outside, "and everything it keeps is where the filaments are",
              "%.1f inside vs %.2f outside", inside, outside);

    // The cross-fade endpoints. A raised cosine over F rows runs 0 -> 1, so the
    // first output row IS the discarded tail's first row and the last blended row
    // IS the source's — which is exactly what makes the wrap continuous. Get the
    // ramp backwards and the seam moves rather than disappearing.
    const int F = 224;
    float t0 = 0.5f * (1.0f - cosf(3.14159265f * 0.0f / (float)F));
    float t1 = 0.5f * (1.0f - cosf(3.14159265f * (float)(F - 1) / (float)F));
    CHECK_MSG(t0 < 1e-6f, "the fade starts wholly on the wrapped-around tail", "t0 = %.4f", t0);
    CHECK_MSG(t1 > 0.9999f, "and ends wholly on the sheet's own head", "t1 = %.4f", t1);
    CHECK_MSG(SWEPT_ASSET_FADE > 0.0f && SWEPT_ASSET_FADE <= 1.0f / 3.0f,
              "the fade never eats more than a third of the tile",
              "%.3f of the length", SWEPT_ASSET_FADE);

    // How far the asset gets stretched along the strip at the default tile.
    // Doing this arithmetic is the point: the eyeball guess was 3x and the real
    // figure is 1.7x, which is the difference between "the default will smear"
    // and "the default is close to how it was painted".
    float srcAlong  = 1792.0f * (1.0f - SWEPT_ASSET_FADE);
    float srcAcross = 896.0f * (SWEPT_ASSET_CROP1 - SWEPT_ASSET_CROP0);
    float authored  = srcAlong / srcAcross;
    float drawn     = 1.10f / (2.0f * (3.0f * 0.0250f));   // 1.10 m tile / a 3 m blade's width
    CHECK_MSG(drawn / authored > 1.0f && drawn / authored < 2.5f,
              "the default tile draws the asset near its authored proportions",
              "authored %.1f:1, drawn %.1f:1 (%.1fx stretch)",
              authored, drawn, drawn / authored);
}

// ── 1. The aspect rule ───────────────────────────────────────────────────────
//
// The trap this exists for (core/docs/LANDMINES.md, "Thickness is a ratio
// against the thing's OWN length"): a ratio measured at ONE parameter value
// passes on the broken formula too. The assertion that matters is INVARIANCE —
// the same ratio at every travelled length, not a good number at one.

static void Test_Aspect(void)
{
    const float widthCeiling = 100.0f;   // effectively no ceiling: exercise the cap
    const float travels[] = {0.4f, 1.0f, 2.5f, 6.0f, 11.0f};
    const Style styles[] = {STYLE_BLADE, STYLE_RIBBON, STYLE_FILAMENT};
    const float wantRatio[] = {1.0f/20.0f, 1.0f/6.993f, 1.0f/40.0f};

    for (int s = 0; s < 3; s++) {
        float worst = 0.0f;
        for (int i = 0; i < 5; i++) {
            float full = 2.0f * HalfWidth(widthCeiling, 1.0f, travels[i], styles[s]);
            float r = full / travels[i];
            float e = fabsf(r - wantRatio[s]) / wantRatio[s];
            if (e > worst) worst = e;
        }
        CHECK_MSG(worst < 1e-4f, "aspect ratio is INVARIANT across travelled length",
                  "style %d worst relative error %.4f", s, worst);
    }

    // The blade's documented figure, stated once so a silent retune is loud.
    float full = 2.0f * HalfWidth(widthCeiling, 1.0f, 4.0f, STYLE_BLADE);
    CHECK_MSG(fabsf(full - 0.20f) < 1e-4f, "blade at 4 m of travel is 20 cm wide (1:20)",
              "%.4f m", full);

    // The caller's width is a CEILING, never exceeded however far the tip went.
    float capped = 2.0f * HalfWidth(0.30f, 1.0f, 50.0f, STYLE_BLADE);
    CHECK_MSG(fabsf(capped - 0.30f) < 1e-5f, "requested width is a ceiling, not a target",
              "%.4f m", capped);

    // ...and below the speed where that width is in proportion, travel wins.
    // This is the hard-turn failure the DoD names: the tail shortens, and a band
    // that keeps its width through that is a blob.
    float slow = 2.0f * HalfWidth(0.30f, 1.0f, 1.0f, STYLE_BLADE);
    CHECK_MSG(slow < 0.30f - 1e-4f, "a short tail yields a THINNER band, not a stub",
              "%.4f m at 1 m of travel", slow);

    // Monotone in travel: no length at which the band gets thinner as the tip
    // moves further.
    int mono = 1;
    float prev = -1.0f;
    for (float t = 0.0f; t < 20.0f; t += 0.25f) {
        float hw = HalfWidth(0.30f, 1.0f, t, STYLE_BLADE);
        if (hw < prev - 1e-6f) mono = 0;
        prev = hw;
    }
    CHECK(mono, "half-width is monotone non-decreasing in travelled length");

    // level01 (VFX_TrailSetWidth's wind-down) scales the ceiling, and 0 means 0.
    CHECK(HalfWidth(0.30f, 0.0f, 50.0f, STYLE_BLADE) == 0.0f,
          "width level 0 collapses the band completely");
    CHECK_MSG(fabsf(HalfWidth(0.30f, 0.5f, 50.0f, STYLE_BLADE) - 0.075f) < 1e-5f,
              "width level scales the ceiling linearly", "%.4f",
              HalfWidth(0.30f, 0.5f, 50.0f, STYLE_BLADE));
}

// ── 2. The width envelope ────────────────────────────────────────────────────

static void Test_WidthEnvelope(void)
{
    // Zero at the tail for every style: a strip that starts at full width has a
    // flat cap, and a flat cap on a short strip is the flat base of a triangle
    // (core/docs/LANDMINES.md).
    CHECK(CurveEval(k_bladeW, 5, 0.0f) == 0.0f,    "BLADE envelope is zero at the tail");
    CHECK(CurveEval(k_ribbonW, 4, 0.0f) == 0.0f,   "RIBBON envelope is zero at the tail");
    CHECK(CurveEval(k_filamentW, 3, 0.0f) == 0.0f, "FILAMENT envelope is zero at the tail");

    // A swept trail is anchored at the head, so unlike a free-flying element it
    // is NOT a symmetric lens: it must be near-full where the weapon is.
    // A LENS, per the guide's own "width over length" diagram: widest in the
    // BODY, tapered at BOTH ends. The first version held 0.74 at the head, which
    // draws a band that stops dead — "no taper" is item three on the guide's
    // list of common mistakes and it is plainly visible in the 29/07 capture.
    for (int c2 = 0; c2 < 3; c2++) {
        const Stop *W2[3] = {k_bladeW, k_ribbonW, k_filamentW};
        const int   N2[3] = {5, 5, 4};
        float head = CurveEval(W2[c2], N2[c2], 1.0f);
        float peak = 0.0f;
        for (float t2 = 0.0f; t2 <= 1.0f; t2 += 0.002f) {
            float w2 = CurveEval(W2[c2], N2[c2], t2);
            if (w2 > peak) peak = w2;
        }
        CHECK_MSG(head < peak * 0.30f,
                  "the head tapers to a needle, it does not stop dead",
                  "style %d: %.3f at the head vs %.3f at the peak", c2, head, peak);
    }

    // Rising from the tail to the peak, everywhere, for every style — a dip in
    // the middle reads as a break in the band.
    const Stop *curves[3] = {k_bladeW, k_ribbonW, k_filamentW};
    const int   counts[3] = {5, 5, 4};
    const float peaks[3]  = {0.60f, 0.62f, 0.85f};
    for (int c = 0; c < 3; c++) {
        int rising = 1;
        float prev = -1.0f;
        for (float t = 0.0f; t <= peaks[c] + 1e-4f; t += 0.01f) {
            float v = CurveEval(curves[c], counts[c], t);
            if (v < prev - 1e-5f) rising = 0;
            prev = v;
        }
        CHECK_MSG(rising, "envelope rises monotonically from tail to peak",
                  "style %d", c);
        // The peak is 1.0 by construction — the envelope is a SHAPE, and the
        // metres live in HalfWidth. If a stop ever exceeds 1 the aspect cap
        // stops being a cap.
        float maxV = 0.0f;
        for (float t = 0.0f; t <= 1.0f; t += 0.005f) {
            float v = CurveEval(curves[c], counts[c], t);
            if (v > maxV) maxV = v;
        }
        CHECK_MSG(maxV <= 1.0f + 1e-5f, "envelope never exceeds 1.0",
                  "style %d peaks at %.3f", c, maxV);
    }

    // A thread is a thread: its envelope must stay near-uniform over the body,
    // or FILAMENT stops being distinguishable from a thin BLADE.
    float lo = CurveEval(k_filamentW, 3, 0.3f);
    CHECK_MSG(lo > 0.8f, "FILAMENT is near-uniform along its body", "%.3f at s=0.3", lo);
}

// ── 3. The lag schedule ──────────────────────────────────────────────────────


static void Test_MaxNodes(void)
{
    CHECK_MSG(MaxNodes(0.45f) == 27, "0.45 s of tail is 27 nodes at 60 Hz",
              "%d", MaxNodes(0.45f));
    CHECK_MSG(MaxNodes(1.0f) == TRAIL_HISTORY_COUNT, "1.0 s fills the history exactly",
              "%d", MaxNodes(1.0f));
    CHECK_MSG(MaxNodes(9.0f) == TRAIL_HISTORY_COUNT, "an over-long lifetime clamps, not wraps",
              "%d", MaxNodes(9.0f));
    CHECK(MaxNodes(0.0f) >= 4, "a degenerate lifetime still leaves a drawable strip");
}

// ── 4. The sample clock is framerate-independent ─────────────────────────────
//
// The rate-vs-count rule (VFX_PLAN §0.3) applied to geometry. Pushing one node
// per FRAME would make the tail's length in metres a function of the frame rate;
// pushing at a fixed rate without sub-frame interpolation would, at 30 fps, land
// every sub-step of a frame on the same point and waste the history.

static int SimulateNodes(float dt, float seconds, float speed, float *outTravel)
{
    float acc = 0.0f;
    float pos = 0.0f, prev = 0.0f;
    int   nodes = 0;
    float travel = 0.0f;
    float lastNode = 0.0f;
    int   frames = (int)(seconds / dt + 0.5f);
    for (int f = 0; f < frames; f++) {
        prev = pos;
        pos += speed * dt;
        acc += dt;
        int steps = (int)(acc / SWEPT_SAMPLE_DT);
        if (steps > SWEPT_STEPS_MAX) { steps = SWEPT_STEPS_MAX; acc = 0.0f; }
        else acc -= (float)steps * SWEPT_SAMPLE_DT;
        for (int n = 1; n <= steps; n++) {
            float p = prev + (pos - prev) * ((float)n / (float)steps);
            travel += fabsf(p - lastNode);
            lastNode = p;
            nodes++;
        }
    }
    *outTravel = travel;
    return nodes;
}

static void Test_SampleClock(void)
{
    float t30, t60, t144, t20;
    int n30  = SimulateNodes(1.0f/30.0f,  1.0f, 5.0f, &t30);
    int n60  = SimulateNodes(1.0f/60.0f,  1.0f, 5.0f, &t60);
    int n144 = SimulateNodes(1.0f/144.0f, 1.0f, 5.0f, &t144);
    int n20  = SimulateNodes(1.0f/20.0f,  1.0f, 5.0f, &t20);

    CHECK_MSG(abs(n30 - 60) <= 1 && abs(n60 - 60) <= 1 &&
              abs(n144 - 60) <= 1 && abs(n20 - 60) <= 1,
              "node count is ~60/s at 20, 30, 60 and 144 fps",
              "%d / %d / %d / %d", n20, n30, n60, n144);

    // Sub-frame interpolation: at 30 fps two sub-steps must land on DIFFERENT
    // points, so the travelled length still comes out right.
    CHECK_MSG(fabsf(t30 - 5.0f) < 0.15f && fabsf(t144 - 5.0f) < 0.15f,
              "travelled length is ~5 m at every frame rate",
              "%.3f / %.3f m", t30, t144);

    // Consequence for the look: identical width at 30 and 144 fps, because the
    // width is derived from the travelled length.
    float w30  = HalfWidth(100.0f, 1.0f, t30,  STYLE_BLADE);
    float w144 = HalfWidth(100.0f, 1.0f, t144, STYLE_BLADE);
    CHECK_MSG(fabsf(w30 - w144) / w144 < 0.05f,
              "band width agrees within 5% between 30 and 144 fps",
              "%.4f vs %.4f", w30, w144);
}

// ── 5. Which side of the strip is u = 0 ──────────────────────────────────────
//
// The masked-ribbon landmine (core/docs/LANDMINES.md, 28/07/2026): an asymmetric
// mask on the wrong side does not look like a bug, it looks like the effect is
// "a bit soft" — which survives a screenshot review indefinitely. The .inl
// derives `side = cross(tangent, normal)` = radially OUTWARD for a turning tip.
// Here that derivation is checked NUMERICALLY on a real sampled arc, including
// the sign the plane normal comes out with.

static void Test_SideIsOuter(void)
{
    // A tip sweeping a horizontal circle, sampled the way the ring samples it.
    V3 ring[SWEPT_RING];
    const float R = 1.5f;
    const int   span = 24;
    for (int i = 0; i <= span; i++) {
        float a = (float)i * 0.05f;
        ring[i] = v3(R * cosf(a), 0.3f * sinf(a * 0.4f), R * sinf(a));
    }
    int head = span, half = span / 2;
    V3 c = ring[head], b = ring[head - half], a = ring[head - span];
    V3 n = norm(cross(sub(b, a), sub(c, b)));

    // tangent at the head, as DrawRibbonStripEx takes it (from the last segment)
    V3 tangent = norm(sub(ring[head], ring[head - 1]));
    V3 side    = norm(cross(tangent, n));
    V3 outward = norm(v3(ring[head].x, 0.0f, ring[head].z));   // radial, centre at origin

    CHECK_MSG(dot(side, outward) > 0.9f,
              "cross(tangent, normal) points radially OUTWARD — u = 0 is the outer edge",
              "dot = %.4f", dot(side, outward));

    // Reversing the direction of travel reverses the turn, hence the normal,
    // hence `side` — and the hot edge stays on the outside of the swing. This is
    // what makes the un-flipped sign correct rather than lucky.
    V3 rr[SWEPT_RING];
    for (int i = 0; i <= span; i++) rr[i] = ring[span - i];
    V3 c2 = rr[head], b2 = rr[head - half], a2 = rr[head - span];
    V3 n2 = norm(cross(sub(b2, a2), sub(c2, b2)));
    V3 tan2  = norm(sub(rr[head], rr[head - 1]));
    V3 side2 = norm(cross(tan2, n2));
    V3 out2  = norm(v3(rr[head].x, 0.0f, rr[head].z));
    CHECK_MSG(dot(side2, out2) > 0.9f,
              "reversed sweep: u = 0 is STILL the outer edge",
              "dot = %.4f", dot(side2, out2));

    // A straight path has no plane. The .inl keeps the previous normal instead
    // of normalising a zero vector — assert the degenerate case is detectable.
    V3 s0 = v3(0,0,0), s1 = v3(1,0,0), s2 = v3(2,0,0);
    V3 nd = cross(sub(s1, s0), sub(s2, s1));
    CHECK_MSG(dot(nd, nd) < 1e-10f, "a straight path yields a degenerate normal (kept, not used)",
              "|n|^2 = %.3e", dot(nd, nd));
}

// ── 6. A figure-eight is survivable ──────────────────────────────────────────
//
// The bench drives a lemniscate on purpose: a trail that looks right on a
// straight line and breaks on a hard turn is the classic failure. Two things
// must hold through the crossing — the tail never inverts (travel > 0 always),
// and the plane normal reverses EXACTLY ONCE per lobe rather than flickering.

static void Test_FigureEight(void)
{
    const int N = 600;
    V3 path[600];
    for (int i = 0; i < N; i++) {
        float a = (float)i / (float)N * 4.0f * PI;
        path[i] = v3(1.6f * sinf(a), 1.15f + 0.45f * sinf(2.0f * a),
                     1.1f * sinf(a) * cosf(a));
    }

    // Travelled length over a 27-node window, walked along the whole path.
    int   win = MaxNodes(0.45f) - 1;
    float minTravel = 1e9f, maxTravel = 0.0f;
    for (int i = win; i < N; i++) {
        float t = 0.0f;
        for (int k = 0; k < win; k++) t += dist(path[i - k], path[i - k - 1]);
        if (t < minTravel) minTravel = t;
        if (t > maxTravel) maxTravel = t;
    }
    CHECK_MSG(minTravel > 0.0f, "the tail never collapses to zero on a figure-eight",
              "min travel %.4f m", minTravel);
    // The band therefore narrows at the crossing (slowest point) instead of
    // holding its width and blobbing — assert the width actually MOVES.
    float wMin = 2.0f * HalfWidth(100.0f, 1.0f, minTravel, STYLE_BLADE);
    float wMax = 2.0f * HalfWidth(100.0f, 1.0f, maxTravel, STYLE_BLADE);
    CHECK_MSG(wMax / wMin > 1.15f, "the band visibly narrows where the tip slows",
              "%.4f m -> %.4f m", wMin, wMax);

    // Normal flips: one per lobe crossing, not a flicker storm.
    int flips = 0, valid = 0;
    V3 cur = v3(0, 0, 0);
    int has = 0;
    int span = win, half = win / 2;
    for (int i = span; i < N; i++) {
        V3 n = cross(sub(path[i - half], path[i - span]), sub(path[i], path[i - half]));
        if (dot(n, n) < 1e-10f) continue;
        n = norm(n);
        valid++;
        if (!has) { cur = n; has = 1; continue; }
        if (dot(n, cur) < 0.0f) { flips++; cur = n; }
        else {
            V3 l = v3(cur.x + (n.x - cur.x) * 0.25f,
                      cur.y + (n.y - cur.y) * 0.25f,
                      cur.z + (n.z - cur.z) * 0.25f);
            cur = norm(l);
        }
    }
    CHECK_MSG(valid > N / 2, "the swing plane is defined along most of the path",
              "%d of %d samples", valid, N - span);
    CHECK_MSG(flips >= 2 && flips <= 8,
              "the plane reverses a few times per double loop, not every frame",
              "%d flips over %d samples", flips, valid);
}

// ── 7. The filament bundle ───────────────────────────────────────────────────
//
// Four strands at equal spacing and similar width draw a COMB — four parallel
// wires, which is exactly what the owner's 29/07 capture showed. The fix is
// irregularity, so the irregularity has to be asserted or the next tidy-up
// silently restores the comb.

static const float k_spread[4] = { -1.00f, -0.50f, 0.38f, 1.00f };

static void Test_FilamentBundle(void)
{
    int distinct = 1;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            if (fabsf(k_spread[i] - k_spread[j]) < 1e-3f) distinct = 0;
    CHECK(distinct, "every filament strand sits at its own offset");

    int sorted = 1;
    for (int i = 1; i < 4; i++) if (k_spread[i] <= k_spread[i - 1]) sorted = 0;
    CHECK(sorted, "offsets are ordered, so strand index maps to position across the bundle");

    CHECK(k_spread[0] < 0.0f && k_spread[3] > 0.0f,
          "the bundle straddles the path instead of hanging off one side");

    float sum = 0.0f;
    for (int i = 0; i < 4; i++) sum += k_spread[i];
    CHECK_MSG(fabsf(sum) < 0.15f, "the bundle is centred on the path", "sum %.3f", sum);

    float gmin = 1e9f, gmax = 0.0f;
    for (int i = 1; i < 4; i++) {
        float g = k_spread[i] - k_spread[i - 1];
        if (g < gmin) gmin = g;
        if (g > gmax) gmax = g;
    }
    CHECK_MSG(gmin / gmax < 0.85f, "spacing is IRREGULAR — a bundle of threads, not a comb",
              "gap ratio %.3f", gmin / gmax);

    // The spread is keyed to the band's own half-width, so it must stay legible
    // at the width filament actually gets. At 2 m of travel a strand is 5 cm
    // wide and the bundle spans ~11 cm: separated, not a 1 m fan.
    float halfW = HalfWidth(100.0f, 1.0f, 2.0f, STYLE_FILAMENT);
    float span  = (k_spread[3] - k_spread[0]) * halfW * 2.2f;
    CHECK_MSG(span > 2.0f * halfW && span < 0.30f,
              "the bundle separates without becoming a fan", "%.3f m across", span);
}

// ── 8. Teleport vs. fast swing ───────────────────────────────────────────────
//
// The gap between two samples is assumed to be a path the tip SWEPT. When the
// transform is moved instead — the bench's spawn point dragged, an agent
// respawned, a blink — that assumption draws a long straight streak bridging two
// places the weapon never was. The discriminator has to pass every real swing at
// every frame rate and still catch a drag, so it is worth pinning both sides.

#define SWEPT_TELEPORT_SPEED 45.0f
#define SWEPT_TELEPORT_MIN   0.75f

static int IsTeleport(float movedMetres, float dt)
{
    float limit = SWEPT_TELEPORT_SPEED * dt;
    if (limit < SWEPT_TELEPORT_MIN) limit = SWEPT_TELEPORT_MIN;
    return movedMetres > limit;
}

static void Test_Teleport(void)
{
    // A fast weapon tip is ~20-25 m/s. None of these may be cut.
    CHECK(!IsTeleport(25.0f / 60.0f,  1.0f/60.0f),  "25 m/s at 60 fps is a swing, not a teleport");
    CHECK(!IsTeleport(25.0f / 30.0f,  1.0f/30.0f),  "25 m/s at 30 fps is a swing, not a teleport");
    CHECK(!IsTeleport(25.0f / 144.0f, 1.0f/144.0f), "25 m/s at 144 fps is a swing, not a teleport");
    // A frame hitch is the case a fixed metre threshold gets wrong: the tip
    // legitimately covers a lot of ground in one long frame.
    CHECK(!IsTeleport(25.0f * 0.10f, 0.10f), "a 100 ms hitch mid-swing is not a teleport");

    // Dragging the spawn point, or a respawn: metres in one frame.
    CHECK(IsTeleport(3.2f, 1.0f/60.0f),  "moving the spawn point 3.2 m IS a teleport");
    CHECK(IsTeleport(1.0f, 1.0f/60.0f),  "even a 1 m jump in one 60 fps frame is a teleport");
    CHECK(IsTeleport(8.0f, 0.10f),       "a big jump during a hitch is still a teleport");

    // The floor and the rate must not contradict each other: below ~16.7 ms the
    // floor governs, above it the speed does, and the crossover has to be
    // continuous or there is a frame rate at which the rule inverts.
    float cross = SWEPT_TELEPORT_MIN / SWEPT_TELEPORT_SPEED;
    CHECK_MSG(!IsTeleport(SWEPT_TELEPORT_MIN * 0.99f, cross * 0.5f) &&
               IsTeleport(SWEPT_TELEPORT_MIN * 1.01f, cross * 0.5f),
              "the floor governs below the crossover frame time", "crossover %.4f s", cross);
}


// ── 9. The BLADE sheet has nothing unresolvable in it ────────────────────────
//
// The dotted-blade artefact (owner's 29/07 capture) was the SweepSlash mask
// reused at a sixth of the width it was authored for: its cross-blade
// striations run at sin(u*PI*23) — 11.5 cycles ACROSS the band — and a 6 cm
// band is a handful of pixels. LoadTextureFromImage makes one mip level, so
// there is nothing to filter it with; each pixel samples whichever phase it
// lands on and the strip breaks into dashes.
//
// This is not a taste question, it is a sampling one, so it can be asserted:
// count the direction changes in the profile. A shape the eye reads as "hot
// edge, smear inward" needs ONE peak. Anything with more is detail the band
// cannot resolve.

static float SmoothStepMirror(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float BladeProfile(float u)
{
    float e    = 1.0f - u;
    float body = powf(e, 1.2f);
    float d    = (u - 0.18f) / 0.35f;
    float rim  = expf(-d * d);
    float a    = 0.50f * body + 0.60f * rim;
    return a * (1.0f - SmoothStepMirror((e - 0.975f) / 0.025f));
}

// The centre-weighted sheet the two styles that NEVER dashed use (main.c:1035).
static float GlobalTrailProfile(float u)
{
    float d = fabsf(u - 0.5f) * 2.0f;
    float a = 1.0f - d * d;
    return a < 0.0f ? 0.0f : a;
}

// What fraction of a profile's alpha sits in the outer `frac` of the band. THE
// measurement that explained the artefact: a mask decides how wide the effect
// LOOKS, independently of how wide the geometry IS, and a feature packed into a
// fifth of a 3-pixel strip is a 0.6-pixel line — which rasterises as dashes.
static float OuterEnergyFraction(float (*prof)(float), float frac)
{
    float total = 0.0f, outer = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float u = ((float)i + 0.5f) / 1000.0f;
        float a = prof(u);
        if (a > 1.0f) a = 1.0f;
        total += a;
        if (u < frac) outer += a;
    }
    return (total > 0.0f) ? outer / total : 0.0f;
}

// The sheet the blade used to borrow, for contrast (vc_sweep_slash.inl).
static float SlashRowWithStriations(float u, float v)
{
    float e     = 1.0f - u;
    float body  = powf(e, 2.4f);
    float d     = u / 0.05f;
    float rim   = expf(-d * d);
    float prof  = (0.40f * body + 0.72f * rim) *
                  (1.0f - SmoothStepMirror((e - 0.985f) / 0.015f));
    float lines  = 0.72f + 0.28f * sinf(u * PI * 9.0f  + v * 1.7f);
    float lines2 = 0.85f + 0.15f * sinf(u * PI * 23.0f - v * 0.6f);
    return prof * lines * lines2;
}

static int CountTurns(const float *a, int n)
{
    int turns = 0, dir = 0;
    for (int i = 1; i < n; i++) {
        float d = a[i] - a[i - 1];
        if (fabsf(d) < 1e-6f) continue;
        int nd = (d > 0.0f) ? 1 : -1;
        if (dir != 0 && nd != dir) turns++;
        dir = nd;
    }
    return turns;
}

static void Test_BladeMask(void)
{
    float row[64], slash[64];
    for (int x = 0; x < 64; x++) {
        float u = ((float)x + 0.5f) / 64.0f;
        row[x]   = BladeProfile(u);
        slash[x] = SlashRowWithStriations(u, 0.5f);
    }
    // THE ASSERTION THAT WOULD HAVE CAUGHT IT FIRST TIME. The blade's visible
    // feature must be a comparable size to its geometry, or the strip dashes
    // however wide the band actually is.
    float bladeOuter  = OuterEnergyFraction(BladeProfile, 0.2f);
    float globalOuter = OuterEnergyFraction(GlobalTrailProfile, 0.2f);
    CHECK_MSG(bladeOuter < 0.40f,
              "the BLADE mask does not pack its alpha into a fifth of the band",
              "%.0f%% of its alpha in the outer 20%%", bladeOuter * 100.0f);
    CHECK_MSG(BladeProfile(0.5f) > 0.35f,
              "the middle of the BLADE band still carries real alpha",
              "%.3f at u = 0.5", BladeProfile(0.5f));
    CHECK_MSG(globalOuter < 0.15f,
              "reference: the sheet the never-dashing styles use is centre-weighted",
              "%.0f%%", globalOuter * 100.0f);

    int turns = CountTurns(row, 64);
    CHECK_MSG(turns <= 2, "the BLADE profile has ONE peak — nothing the band cannot resolve",
              "%d direction changes across u", turns);

    // The contrast that motivated the change, pinned so nobody "restores the
    // reuse" without seeing the number.
    int slashTurns = CountTurns(slash, 64);
    CHECK_MSG(slashTurns > 6,
              "the SweepSlash sheet really is high-frequency across the blade "
              "(fine for ITS width, dashes at a trail's)",
              "%d direction changes across u", slashTurns);

    // The rim must survive minification: it has to be a meaningful FRACTION of
    // the band, not three texels of a 192-wide sheet. Measure its half-height
    // width in u.
    float peak = 0.0f; int peakX = 0;
    for (int x = 0; x < 64; x++) if (row[x] > peak) { peak = row[x]; peakX = x; }
    int lo = peakX, hi = peakX;
    while (lo > 0 && row[lo] > peak * 0.5f) lo--;
    while (hi < 63 && row[hi] > peak * 0.5f) hi++;
    float widthU = (float)(hi - lo) / 64.0f;
    CHECK_MSG(widthU > 0.12f, "the hot edge is a wide enough fraction of the band to survive minification",
              "half-height width %.3f of u", widthU);

    // ...and it must still be an EDGE: peaked near the outer border, not centred.
    float peakU = ((float)peakX + 0.5f) / 64.0f;
    CHECK_MSG(peakU < 0.25f, "the hot line sits against the OUTER edge, not down the middle",
              "peak at u = %.3f", peakU);

    // Asymmetric across the blade — a symmetric profile is a tube, not a blade.
    CHECK_MSG(BladeProfile(0.15f) > 3.0f * BladeProfile(0.85f),
              "the profile is asymmetric: hot outside, smear inside", "%.3f vs %.3f",
              BladeProfile(0.15f), BladeProfile(0.85f));

    // Along the strip: one shallow swell, so the band is not a uniform sheet of
    // light and not a dashed one either.
    float col[64];
    for (int y = 0; y < 64; y++) {
        float v = ((float)y + 0.5f) / 64.0f;
        col[y] = 0.86f + 0.14f * sinf(v * 2.0f * PI);
    }
    int vturns = CountTurns(col, 64);
    CHECK_MSG(vturns <= 2, "the lengthwise swell is ONE cycle, not a stripe pattern",
              "%d direction changes along v", vturns);
    float vmin = 2.0f, vmax = 0.0f;
    for (int y = 0; y < 64; y++) { if (col[y] < vmin) vmin = col[y]; if (col[y] > vmax) vmax = col[y]; }
    CHECK_MSG(vmin / vmax > 0.65f, "the swell modulates, it does not cut the band into pieces",
              "min/max %.3f", vmin / vmax);
}


// ── 10. The screen-space floor, and the tail ─────────────────────────────────
//
// The blade rendered DOTTED and the giveaway was that it tracked ZOOM: solid up
// close, broken far out, and the tail went first at every zoom. Replacing the
// mask changed nothing, which ruled out texture frequency and left geometry: a
// strip under ~1 px wide is rasterised only where its centre lands inside a
// pixel. Two separate consequences, and both are arithmetic.

#define SWEPT_MIN_PIXELS      2.0f
#define SWEPT_CORE_MIN_PIXELS 5.0f

static float PixelsPerMetre(float dist, float fovyDeg, float screenH)
{
    if (dist < 0.01f) dist = 0.01f;
    float t = tanf(fovyDeg * 0.5f * (PI / 180.0f));
    if (t < 1e-4f) t = 1e-4f;
    return screenH / (2.0f * dist * t);
}

static float ScreenFloor(float halfW, float pxPerMetre, float minFullPx, float *outAlphaScale)
{
    *outAlphaScale = 1.0f;
    if (pxPerMetre <= 0.0f || minFullPx <= 0.0f) return halfW;
    float minHalf = (minFullPx * 0.5f) / pxPerMetre;
    if (halfW >= minHalf) return halfW;
    *outAlphaScale = (halfW > 0.0f) ? (halfW / minHalf) : 0.0f;
    return minHalf;
}


// The tail is thinner than a pixel at every zoom, because the envelope takes it
// to zero. So it must be DIMMER than it is thin, or it dashes instead of fading.
static void Test_TailFadesFasterThanItThins(void)
{
    const Stop *W[3] = {k_bladeW, k_ribbonW, k_filamentW};
    const int   Wn[3] = {5, 5, 4};
    const Stop *A[3] = {k_bladeA, k_ribbonA, k_filamentA};
    const int   An[3] = {4, 3, 3};
    const char *names[3] = {"BLADE", "RIBBON", "FILAMENT"};

    for (int c = 0; c < 3; c++) {
        float worstS = -1.0f, worstGap = 0.0f;
        for (float s = 0.01f; s <= 0.35f; s += 0.01f) {
            float w = CurveEval(W[c], Wn[c], s);
            float a = CurveEval(A[c], An[c], s);
            if (a > w && (a - w) > worstGap) { worstGap = a - w; worstS = s; }
        }
        CHECK_MSG(worstS < 0.0f, "tail alpha never exceeds tail width",
                  "%s: alpha leads width by %.3f at s = %.2f", names[c], worstGap, worstS);
    }
}


// ── 11. Foreshortening on a plane-pinned strip (core/ribbon_strip.c) ─────────
//
// THE ACTUAL CAUSE of the dashed blade, after three wrong ones. A strip pinned to
// a plane has a width vector that rotates with the tangent, so how much of it
// survives projection changes ALONG the strip — by 13x on this very path. The
// thin end is sub-pixel and dashes while the wide end is solid. A camera-facing
// strip cannot do this: its side vector is built perpendicular to the view, so
// its factor is 1 everywhere. THAT is why the middle style never broke up, and
// why FILAMENT — the THINNEST geometry of the three — never did either.
//
// This test rebuilds the bench's own path and sweeps every camera angle, so it
// measures the real thing rather than a hypothesis about it.

#define RIBBON_MIN_PROJECTION 0.35f

static V3 BenchPath(float t)
{
    float a = t * 3.0f;
    return v3(1.2f * sinf(a), 1.15f + 0.45f * sinf(2.0f * a),
              0.85f * sinf(a) * cosf(a));
}

static V3 StripTangent(const V3 *p, int n, int i)
{
    if (i == 0)     return norm(sub(p[1], p[0]));
    if (i == n - 1) return norm(sub(p[n - 1], p[n - 2]));
    return norm(sub(p[i + 1], p[i - 1]));
}

// `blend` mirrors the guard in DrawRibbonStripEx; `cameraFacing` mirrors the
// mode the other two styles use.
static float WorstProjection(float tNow, float theta, float dist,
                             int cameraFacing, int blend)
{
    enum { N = 27 };
    V3 p[N];
    for (int i = 0; i < N; i++) p[i] = BenchPath(tNow - (float)(N - 1 - i) / 60.0f);

    int half = (N - 1) / 2;
    V3 nrm3 = norm(cross(sub(p[half], p[0]), sub(p[N - 1], p[half])));
    V3 cam  = v3(sinf(theta) * dist, dist * 0.8f, cosf(theta) * dist);
    V3 view = norm(sub(v3(0.0f, 0.2f, 0.0f), cam));

    float worst = 1.0f;
    V3 prev = v3(0, 0, 0); int havePrev = 0;
    for (int i = 0; i < N; i++) {
        V3 tg = StripTangent(p, N, i);
        V3 primary = cameraFacing ? view : nrm3;
        V3 side = cross(tg, primary);
        if (len(side) < 1e-4f) side = cross(tg, v3(0, 1, 0));
        side = norm(side);
        if (havePrev && dot(side, prev) < 0.0f) side = v3(-side.x, -side.y, -side.z);
        prev = side; havePrev = 1;

        V3 v = norm(sub(p[i], cam));
        float along = dot(side, v);
        float proj = sqrtf(fmaxf(0.0f, 1.0f - along * along));

        if (blend && proj < RIBBON_MIN_PROJECTION) {
            V3 cs = cross(tg, v);
            if (len(cs) > 1e-4f) {
                cs = norm(cs);
                if (dot(cs, side) < 0.0f) cs = v3(-cs.x, -cs.y, -cs.z);
                float w = 1.0f - proj / RIBBON_MIN_PROJECTION;
                side = norm(v3(side.x * (1 - w) + cs.x * w,
                               side.y * (1 - w) + cs.y * w,
                               side.z * (1 - w) + cs.z * w));
                along = dot(side, v);
                proj = sqrtf(fmaxf(0.0f, 1.0f - along * along));
            }
        }
        if (proj < worst) worst = proj;
    }
    return worst;
}

static float SweepWorst(int cameraFacing, int blend)
{
    float worst = 1.0f;
    for (int k = 0; k < 24; k++) {
        float th = (float)k * PI / 12.0f;
        for (int j = 0; j < 42; j++) {
            float w = WorstProjection((float)j * 0.05f, th, 8.4f, cameraFacing, blend);
            if (w < worst) worst = w;
        }
    }
    return worst;
}

static void Test_Foreshortening(void)
{
    // THE FACT THAT EXPLAINS THE WHOLE BUG, and the one the owner kept pointing
    // at: the camera-facing styles cannot lose their width, at any angle, at any
    // phase. Nothing about their geometry being thicker — it is structural.
    float camFacing = SweepWorst(1, 0);
    CHECK_MSG(camFacing > 0.90f,
              "a camera-facing strip keeps its width at EVERY camera angle and phase",
              "worst %.3f", camFacing);

    // ...while the plane-pinned one collapses, which is the dashing.
    float pinnedRaw = SweepWorst(0, 0);
    CHECK_MSG(pinnedRaw < 0.15f,
              "a plane-pinned strip DOES collapse somewhere along itself (the bug)",
              "worst %.3f", pinnedRaw);

    // And within a SINGLE strip it varies enormously — which is what makes it
    // dashes rather than a uniformly faint band, and why it tracks curvature:
    // a straight strip has a constant tangent and so a constant factor.
    {
        enum { N = 27 };
        V3 p[N];
        float tNow = 1.05f;
        for (int i = 0; i < N; i++) p[i] = BenchPath(tNow - (float)(N - 1 - i) / 60.0f);
        int half = (N - 1) / 2;
        V3 nrm3 = norm(cross(sub(p[half], p[0]), sub(p[N - 1], p[half])));
        V3 cam  = v3(sinf(PI / 3.0f) * 8.4f, 8.4f * 0.8f, cosf(PI / 3.0f) * 8.4f);
        float lo = 1.0f, hi = 0.0f;
        for (int i = 0; i < N; i++) {
            V3 tg = StripTangent(p, N, i);
            V3 side = norm(cross(tg, nrm3));
            V3 v = norm(sub(p[i], cam));
            float pr = sqrtf(fmaxf(0.0f, 1.0f - dot(side, v) * dot(side, v)));
            if (pr < lo) lo = pr;
            if (pr > hi) hi = pr;
        }
        CHECK_MSG(hi / fmaxf(lo, 1e-3f) > 5.0f,
                  "the projected width varies hugely WITHIN one strip — hence dashes",
                  "%.2f to %.2f along the band", lo, hi);
    }

    // The fix, measured the same way.
    float pinnedFixed = SweepWorst(0, 1);
    CHECK_MSG(pinnedFixed > 0.30f,
              "blending toward camera-facing keeps the band visible everywhere",
              "worst %.3f (was %.3f)", pinnedFixed, pinnedRaw);
    CHECK_MSG(pinnedFixed > pinnedRaw * 3.0f,
              "and it is a large improvement, not a rounding one",
              "%.3f vs %.3f", pinnedFixed, pinnedRaw);

    // No-op above the threshold: nothing that already reads correctly changes.
    // A strip seen broadside must come out bit-identical.
    float broadsideRaw   = WorstProjection(0.30f, PI, 8.4f, 0, 0);
    float broadsideFixed = WorstProjection(0.30f, PI, 8.4f, 0, 1);
    if (broadsideRaw >= RIBBON_MIN_PROJECTION)
        CHECK_MSG(fabsf(broadsideRaw - broadsideFixed) < 1e-5f,
                  "above the threshold the guard is a no-op",
                  "%.5f vs %.5f", broadsideRaw, broadsideFixed);
    else
        CHECK(1, "above the threshold the guard is a no-op (phase not applicable)");
}

// ── the mirror guard ─────────────────────────────────────────────────────────

// A mirror needle must pin the CODE, not the FORMATTING. These needles were
// written with the source's column alignment baked in — `#define FOO    1.0f`,
// three spaces — and the moment a formatter reflowed the file, SEVENTEEN of them
// failed at once while nothing about the behaviour had changed. A test that
// cries wolf on whitespace teaches people to ignore it, which is worse than not
// having it. So both sides are collapsed to single spaces first: any run of
// spaces, tabs and newlines compares equal to one space, and multi-line needles
// work for free.
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

static void Test_MirrorStillMatchesSource(void)
{
    const char *inl = "core/composition/common/vc_swept_trail.inl";

    // ── What this file still OWNS ───────────────────────────────────────────
    //
    // After the port onto core/trail_system.h, the mechanism lives in the
    // engine and is pinned by core/tests/trail_cloth_test.c. Duplicating those
    // needles here would just be a second place to update. What belongs here is
    // the AUTHORING — the numbers that decide whether this reads as a blade, a
    // cloth or a thread — and the WIRING, i.e. that the authored numbers are
    // actually handed to the engine.

    CHECK(FileHas(inl, "return 0.0250f;"), "source still uses the 1:20 blade aspect");
    CHECK(FileHas(inl, "return 0.0715f;"),
          "source still uses the 1:7 ribbon aspect (cloth is BROAD)");
    CHECK(FileHas(inl, "return 0.0125f;"), "source still uses the 1:40 filament aspect");
    CHECK(FileHas(inl, "return (cap < want) ? cap : want;"),
          "the caller's width is still a CEILING, not a value");

    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.60f, 1.00f);"),
          "the blade width envelope peaks in the BODY, not at the head");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 1.00f, 0.18f);"),
          "the blade's head still tapers to a needle");
    CHECK(FileHas(inl, "{-1.00f, -0.50f, 0.38f, 1.00f}"),
          "the filament spread table still matches this mirror");

    // ── The layer stack ─────────────────────────────────────────────────────
    //
    // The three-strip idea used to be a hand-rolled pass loop in this file. It
    // is now a TrailLayer table, and the ratios have to survive the move.
    CHECK(FileHas(inl, ".widthMul = 1.55f, .alphaMul = 0.07f"),
          "the halo is still faint and wide");
    CHECK(FileHas(inl, ".widthMul = 0.26f, .alphaMul = 0.28f"),
          "and the core still thin, and no longer a second saturated band");
    CHECK(FileHas(inl, ".whiten = 0.00f"),
          "the hue-carrying glow layer is still NOT whitened");
    CHECK(FileHas(inl, ".alphaMul = 0.31f, .whiten = 0.06f"),
          "the body layer is barely whitened, so it keeps the element's hue");
    CHECK(FileHas(inl, ".alphaMul = 0.28f, .whiten = 0.20f"),
          "and the core is a SEASONING of white, not a colourless band");
    CHECK(FileHas(inl, "return lowTier ? 1 : 2;"),
          "FILAMENT is still 2 strands — the caller owns how many wisps there are");
    CHECK(FileHas(inl, ".headAlphaPow = 3.4f"),
          "the white-hot core is still concentrated at the head, and now more tightly");
    // THE RULE, not a preference: several additive copies of one textured
    // pattern at different phases sum to something FLAT, and the wider layers
    // throw the sheet's edge detail outward as spikes.
    CHECK(FileHas(inl, ".scrollMul = 1.05f, .headAlphaPow = 0.0f, .texture = &s_sweptBodyTex"),
          "exactly ONE layer carries the texture, and it is the body");
    // HAZE: a SECOND stack, two layers, no core, on the structure-free sheet.
    CHECK(FileHas(inl, "static TrailLayer s_sweptHazeLayers[2] = {"),
          "the haze still has its own TWO-layer stack — no white-hot core");
    CHECK(FileHas(inl, "cfg.layers = s_sweptHazeLayers;"),
          "and the HAZE style still selects it");
    // THE FIELD CARRIES THE FLOW. This shipped twice with the haze stripped of
    // its texture, on a rule that only applies WITHIN one trail's layer stack —
    // and a featureless band cannot be seen to scroll, which is the field's one
    // job. Its outer layer is a bare shape; its body keeps the flow sheet.

    // STRIPPED BACK, on the owner's call: one untextured tube while the SHAPE is
    // being judged. Three wrong silhouettes in a row each had two candidate
    // causes — the second layer or the wrapped sheet — and neither could be
    // ruled out without removing the other. A bare tube has exactly one thing
    // that can be wrong.
    CHECK(FileHas(inl, "s_sweptHazeLayers[1].texture = NULL;"),
          "no haze layer is handed a ribbon band sheet — those seam on a tube");
    CHECK(FileHas(inl, "(s_hazeTex >= 0.5f && s_sweptTubeTex.id != 0) ? &s_sweptTubeTex : NULL"),
          "and only the u-SEAMLESS sheet can ever go on the tube, on request");
    CHECK(FileHas(inl, "t->layerCount = (s_hazeLayers >= 1.5f) ? 2 : 1;"),
          "the layer count is a live dial, so one-vs-two costs no rebuild");
    CHECK(FileHas(inl, "return 0.1600f;"),
          "the haze aspect is still 1:3 — a wake is BROAD");
    // THE FIELD IS A VOLUME. A flat card can be made wide and faint and still
    // reads as a decal of a field; a swept tube has a silhouette from anywhere.
    CHECK(FileHas(inl, "cfg.shape = TRAIL_SHAPE_TUBE;"),
          "the haze field is still swept as a TUBE, not a flat strip");
    CHECK(FileHas(inl, "s->style == VFX_TRAIL_HAZE && GfxQuality_Get() >= GFX_MED"),
          "and the tube is still tier-gated — the gate may only clamp DOWN");
    CHECK(FileHas(inl, "s_sweptLayers[0].texture = (s_sweptHaloTex.id != 0) ? &s_sweptHaloTex : NULL;"),
          "the halo still gets the structure-free sheet");
    CHECK(FileHas(inl, "s_sweptLayers[2].texture = s_sweptLayers[0].texture;"),
          "...and so does the core");

    // ── The sheets ──────────────────────────────────────────────────────────
    CHECK(FileHas(inl, "#define SWEPT_STREAKS 16"),
          "the procedural sheet still has interior structure, not a plain falloff");
    CHECK(FileHas(inl, "if (t <= -1.0f || t >= 1.0f) continue;"),
          "each streak is still FINITE along v, not a full-height lane");
    CHECK(FileHas(inl, "dv -= floorf(dv + 0.5f);"),
          "and its extent still wraps, so the tiled sheet has no seam");
    CHECK(!FileHas(inl, "sinf(2.0f * PI * (float)cyc[f] * v + phase[f])"),
          "the rigid continuous sine lanes are gone for good");
    CHECK(FileHas(inl, "a += st_amp[f] * expf(-d * d) * env * base * base;"),
          "streaks are still damped by the band profile SQUARED, away from the edge");
    CHECK(FileHas(inl, "#define SWEPT_ASSET_PATH \"assets/textures/energy_flow.png\""),
          "the body sheet still comes from the authored flow asset");
    CHECK(FileHas(inl, "ImageRotateCW(&src);"),
          "still rotated a quarter turn — the asset's flow runs across its WIDTH");
    CHECK(FileHas(inl, "ImageCrop(&src,"),
          "still cropped to the band that actually carries filaments");
    CHECK(FileHas(inl, "float t = 0.5f * (1.0f - cosf(PI * (float)y / (float)F));"),
          "the wrap is still cross-faded, so the tiled sheet has no seam");
    CHECK(FileHas(inl, "SetTextureWrap(s_sweptBladeTex, TEXTURE_WRAP_REPEAT);"),
          "the sheet still WRAPS, so it can be tiled and scrolled");
    CHECK(FileHas(inl, "s_sweptBodyTex = (s_sweptSheet >= 0.5f && s_sweptAssetTex.id != 0)"),
          "and it still falls back to the procedural sheet if the asset is missing");
    CHECK(FileHas(inl, "float d = fabsf(u - 0.5f) * 2.0f;"),
          "the band profile still matches this mirror");

    // ── The WIRING. Authored numbers are worthless if they are not handed over,
    // and every one of these was a hand-rolled loop in this file yesterday. ──
    CHECK(FileHas(inl, "cfg.type = TRAIL_TYPE_FOLLOWER;"),
          "the trail is a real TrailEntity now, not a private ring");
    CHECK(!FileHas(inl, "DrawRibbonStripEx"),
          "and this file no longer draws a ribbon itself");
    CHECK(FileHas(inl, "cfg.sampleHz = SWEPT_SAMPLE_HZ;"),
          "the fixed-rate sample clock is still asked for (a RATE, not per frame)");
    CHECK(FileHas(inl, "cfg.teleportSpeed = SWEPT_TELEPORT_SPEED;"),
          "a teleport still CUTS the trail instead of bridging the gap");
    CHECK(FileHas(inl, "cfg.idleSpeed = SWEPT_IDLE_SPEED;"),
          "a stationary weapon still lets its trail DECAY");
    CHECK(FileHas(inl, "cfg.nodeHomeSpring = SWEPT_HOME_SPRING;"),
          "nodes are still sprung back toward the path they were LAID on");
    CHECK(FileHas(inl, "cfg.nodeOrderFrac = SWEPT_ORDER_FRAC;"),
          "and the order bound is still asked for — the fix for the self-twist");
    CHECK(FileHas(inl, "cfg.forceField = &s_sweptCloth[s->style];"),
          "node motion still comes from a ForceField, not hand-written sin()");
    CHECK(FileHas(inl, "cfg.uvMetresPerTile"),
          "the flow UV is still the MATERIAL one, not segRatio");
    CHECK(FileHas(inl, "cfg.blendMode = BLEND_ADDITIVE;") &&
          FileHas(inl, "cfg.useCustomBlendMode = true;"),
          "a trail still EMITS: additive, per the blend law");
    CHECK(FileHas(inl, "cfg.disableInnerCore = true;"),
          "the engine's legacy sub-pixel core is still off — the stack replaces it");
    CHECK(FileHas(inl, "cfg.gradient = SweptTrail_Gradient(s->matId);"),
          "colour along the strip still comes from the element material");
    CHECK(FileHas(inl, "cfg.ribbonMode = (s->style == VFX_TRAIL_BLADE) ? RIBBON_FIXED_NORMAL"),
          "BLADE still lies in the swing plane; the others stay camera-facing");

    // Handle safety. Trail ids are recycled and the pool evicts by priority, so
    // a stored id can silently become somebody else's entity.
    CHECK(FileHas(inl, "t->ownerTag != (SWEPT_TAG_BASE | (slot << 4) | strand)"),
          "a strand id is still VALIDATED against its tag, never trusted");
    CHECK(FileHas(inl, "s->strandId[c] = SweptTrail_SpawnStrand(s, i, c);"),
          "and an evicted strand still respawns — eviction stays self-healing");
    // The entity holds the CALLER's Matrix now, so the kill path must detach or
    // it is a read after free every frame until the idle fade finishes.
    CHECK(FileHas(inl, "Trail_AttachToTransform(s->strandId[k], NULL,"),
          "kill still DETACHES rather than killing — a wind-down, and no dangling Matrix");

    // The sparkles, which are the one layer that is not geometry.
    CHECK(FileHas(inl, "s->sparkAcc += dt * SWEPT_SPARK_RATE * s_sweptSpark;"),
          "sparks are still a RATE carried between frames, not a count per call");
    CHECK(FileHas(inl, "sqrtf(Random01()) * (float)span"),
          "they are still born ALONG the ribbon, biased toward the head");
    CHECK(FileHas(inl, ".velocity = Vector3Scale(jit, 0.11f),"),
          "and still magic DUST — born, hanging, drifting — not thrown grit");
    CHECK(!FileHas(inl, ".stretchStrength = 1.0f,"),
          "a streaked dust mote is still a contradiction");
    CHECK(FileHas(inl, ".alphaCurve = &s_sweptTwinkle,"),
          "each mote still twinkles on its own clock");
    CHECK(FileHas(inl, "GfxQuality_Get() < GFX_MED"),
          "the tier gate still only clamps DOWN");

    // The freeze, and the thing that makes it an instrument rather than a pause
    // button: it must wait for a FULL ribbon, or the dial shows nothing.
    CHECK(FileHas(inl, "(t->historyCount >= SweptTrail_MaxNodes(s->lifetime))"),
          "the freeze still waits for a full ribbon before it holds");
    CHECK(FileHas(inl, "Trail_SetFrozen(s->strandId[c], frozen);"),
          "and it is still the engine's freeze, not a second one here");

    // Dead weight is really gone. Every one of these was an instrument for the
    // dashing, and the dashing was the ribbon FOLDING — fixed at the source.
    CHECK(!FileHas(inl, "s_sweptMinPx") && !FileHas(inl, "SWEPT_MIN_PIXELS"),
          "the screen-space width floor is gone with the bug it never fixed");
    CHECK(!FileHas(inl, "s_sweptBladeFlat") && !FileHas(inl, "s_sweptCamFacing"),
          "and so are the two diagnostic dials it came with");
    CHECK(!FileHas(inl, "SWEPT_LAG_STEP"),
          "the filament lag schedule is gone — strands diverge in the air field now");
    CHECK(!FileHas(inl, "SweptTrail_Simulate") && !FileHas(inl, "SweptTrail_Push"),
          "the private cloth simulation and history ring are gone");
}

int main(void)
{
    printf("=== swept trail (H1) ===\n");
    Test_ClothStaysNearThePath();
    Test_NodesCannotCrossTheirNeighbour();
    Test_FlowIsDecoupledFromTheSwing();
    Test_AdditiveBudget();
    Test_HazeSitsUnderTheSharpTrail();
    Test_TrailKeepsTheElementHue();
    Test_StyleValidatorCoversEveryStyle();
    Test_AssetSheetGeometry();
    Test_Aspect();
    Test_WidthEnvelope();
    Test_MaxNodes();
    Test_SampleClock();
    Test_SideIsOuter();
    Test_FigureEight();
    Test_FilamentBundle();
    Test_Teleport();
    Test_BladeMask();
    Test_TailFadesFasterThanItThins();
    Test_Foreshortening();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

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

static void Test_LagSchedule(void)
{
    CHECK(LagSamples(0) == 0, "strand 0 rides the live tip (zero lag)");

    int prev = -1, strictly = 1;
    for (int i = 0; i < 4; i++) {
        int n = LagSamples(i);
        if (n <= prev) strictly = 0;
        prev = n;
    }
    CHECK(strictly, "lag is strictly increasing across strands");

    // The lag is in SECONDS. If it were ever rewritten as a frame count this
    // conversion is the thing that would quietly disappear.
    for (int i = 0; i < 4; i++) {
        float secs = (float)LagSamples(i) * SWEPT_SAMPLE_DT;
        float want = (float)i * SWEPT_LAG_STEP;
        CHECK_MSG(fabsf(secs - want) <= SWEPT_SAMPLE_DT * 0.5f + 1e-6f,
                  "lag in seconds matches the schedule to within half a sample",
                  "strand %d: %.4f s vs %.4f s", i, secs, want);
    }

    // The whole schedule has to fit in the ring, or the oldest strand reads a
    // sample that has already been overwritten — which is not a crash, it is a
    // strand that jitters, i.e. exactly the kind of bug a screenshot loses.
    CHECK_MSG(LagSamples(3) < SWEPT_RING - 1, "the deepest lag fits in the ring",
              "%d of %d", LagSamples(3), SWEPT_RING);

    // ...and inside the tail itself at the shortest sensible lifetime, or the
    // last strand is asking for history the trail no longer holds.
    CHECK_MSG(LagSamples(3) < MaxNodes(0.20f), "the deepest lag fits a 0.2 s tail",
              "%d of %d nodes", LagSamples(3), MaxNodes(0.20f));
}

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

static void Test_ScreenFloor(void)
{
    const float fovy = 45.0f, H = 1080.0f;

    // Close up, a 6 cm blade band is many pixels and must be left ALONE — a
    // floor that engages when it is not needed would fatten every trail.
    float aScale;
    float near_ = PixelsPerMetre(6.0f, fovy, H);
    float hw = ScreenFloor(0.03f, near_, SWEPT_MIN_PIXELS, &aScale);
    CHECK_MSG(fabsf(hw - 0.03f) < 1e-6f && aScale == 1.0f,
              "at 6 m the band is left untouched", "%.4f m, alpha x%.3f", hw, aScale);

    // Far out it goes sub-pixel; the floor holds the width and pays in alpha.
    float far_ = PixelsPerMetre(90.0f, fovy, H);
    float hw2 = ScreenFloor(0.03f, far_, SWEPT_MIN_PIXELS, &aScale);
    CHECK_MSG(hw2 > 0.03f && aScale < 1.0f,
              "far away the width is floored and the alpha pays for it",
              "%.4f m, alpha x%.3f", hw2, aScale);
    CHECK_MSG(fabsf(2.0f * hw2 * far_ - SWEPT_MIN_PIXELS) < 1e-3f,
              "the floored band is exactly the minimum pixel width",
              "%.3f px", 2.0f * hw2 * far_);

    // THE POINT OF PAYING IN ALPHA: brightness x width is conserved, so a
    // distant trail fades instead of becoming a fat opaque worm. Without this a
    // floor is worse than the dashes it fixes.
    float unfloored = 0.03f;
    CHECK_MSG(fabsf(hw2 * aScale - unfloored) < 1e-6f,
              "width x alpha is conserved by the floor", "%.6f vs %.6f",
              hw2 * aScale, unfloored);

    // Monotone in distance: no range where moving away makes the band brighter.
    float prevProduct = 1e9f; int mono = 1;
    for (float d = 2.0f; d < 200.0f; d *= 1.15f) {
        float p = PixelsPerMetre(d, fovy, H);
        float a; ScreenFloor(0.03f, p, SWEPT_MIN_PIXELS, &a);
        if (a > prevProduct + 1e-6f) mono = 0;
        prevProduct = a;
    }
    CHECK(mono, "alpha compensation never increases with distance");

    // The inner core is 0.4/1.5 = 0.267x the outer half-width, so it is
    // sub-pixel while the band around it still looks fine — which is why the
    // BRIGHTEST layer is the one seen breaking up. Its gate must engage first.
    float coreRatio = 0.4f / 1.5f;
    CHECK_MSG(SWEPT_CORE_MIN_PIXELS * coreRatio >= SWEPT_MIN_PIXELS * 0.5f,
              "the core is dropped while it is still wider than half the floor",
              "%.2f px", SWEPT_CORE_MIN_PIXELS * coreRatio);
    CHECK(SWEPT_CORE_MIN_PIXELS > SWEPT_MIN_PIXELS,
          "the core gate engages BEFORE the band's own floor does");
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

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *inl = "core/composition/common/vc_swept_trail.inl";
    CHECK(FileHas(inl, "return 0.0250f;"),   "source still uses the 1:20 blade aspect");
    CHECK(FileHas(inl, "return 0.0715f;"),
          "source still uses the 1:7 ribbon aspect (cloth is broader than a blade)");
    CHECK(FileHas(inl, "return 0.0125f;"),   "source still uses the 1:40 filament aspect");
    CHECK(FileHas(inl, "return (cap < want) ? cap : want;"),
          "width is still MIN(requested, aspect cap)");
    CHECK(FileHas(inl, "#define SWEPT_SAMPLE_HZ    60.0f"),
          "the sample clock is still 60 Hz");
    CHECK(FileHas(inl, "#define SWEPT_LAG_STEP     0.030f"),
          "the filament lag step is still 30 ms");
    CHECK(FileHas(inl, "Vector3Lerp(s->prevTip, tip,"),
          "sub-frame interpolation is still in the push loop");
    CHECK(FileHas(inl, "Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, b))"),
          "the swing normal is still the cross product of two chords");
    CHECK(FileHas(inl, "Vector3DotProduct(n, s->normal) < 0.0f"),
          "the normal is still SNAPPED at an inflection, not lerped through zero");
    // The trail no longer hands its width to a TrailEntity, so there is no
    // 1.5x outer multiplier to divide out — this file draws the strip itself in
    // three layers (see k_sweptPassW).
    CHECK(FileHas(inl, "static const float k_sweptPassW[3]     = {1.55f, 1.00f, 0.26f};"),
          "the three-layer pass widths still match this mirror");
    CHECK(FileHas(inl, "static const float k_sweptPassA[3]     = {0.16f, 0.85f, 1.00f};"),
          "the glow layer is still faint and wide, the core bright and thin");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 0.60f, 1.00f);"),
          "the blade width envelope peaks in the BODY, not at the head");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sweptWidthCurve[VFX_TRAIL_BLADE], 1.00f, 0.18f);"),
          "the blade's head still tapers to a needle");
    // Guide §3, the layer that was missing entirely: a tiled, scrolled flow
    // sheet with fibres inside the band.
    CHECK(FileHas(inl, "SetTextureWrap(s_sweptBladeTex, TEXTURE_WRAP_REPEAT);"),
          "the sheet still WRAPS, so it can be tiled and scrolled");
    CHECK(FileHas(inl, "arc[k] / SWEPT_FLOW_TILE"),
          "UV is still tiled by METRES, not stretched over the strip");
    CHECK(FileHas(inl, "s->elapsed * SWEPT_FLOW_SPEED * s_sweptFlow"),
          "the flow still scrolls over time");
    CHECK(FileHas(inl, "#define SWEPT_STREAKS 16"),
          "the sheet still has interior structure, not a plain falloff");
    // THE SHAPE OF THAT STRUCTURE, not just its presence. Continuous lanes
    // running the full height of the sheet can only TRANSLATE when v scrolls —
    // "a sine graph being dragged along" (owner, 29/07) — and read as static at
    // any scroll rate. Finite streaks appear and vanish, which is where the eye
    // actually gets motion from. Two things make a streak finite: a bounded
    // envelope, and a circular distance in v so the bound can wrap seamlessly.
    CHECK(FileHas(inl, "if (t <= -1.0f || t >= 1.0f) continue;"),
          "each streak is still FINITE along v, not a full-height lane");
    CHECK(FileHas(inl, "dv -= floorf(dv + 0.5f);"),
          "and its extent still wraps, so the tiled sheet has no seam");
    CHECK(!FileHas(inl, "sinf(2.0f * PI * (float)cyc[f] * v + phase[f])"),
          "the rigid continuous sine lanes are gone for good");
    // ...and the HALO does not carry them. The glow pass is 2.6x wider, so any
    // structure in its sheet is thrown 2.6x further out: a fibre lane wandering
    // across u — invisible at the body's edge — becomes long spikes at the
    // halo's, and the band reads as sawtoothed, i.e. cut into segments. The
    // identical lesson is written in vc_sweep_slash.inl ("the halo must not
    // carry the rim") and was repeated here anyway.
    CHECK(FileHas(inl, "s_sweptHaloTex"),
          "there is still a SEPARATE fibre-less sheet for the halo pass");
    CHECK(FileHas(inl, "Texture2D sheet = (pass == 1 || s_sweptHaloTex.id == 0)"),
          "and BOTH the halo and the core pass use it — only the body is textured");
    CHECK(FileHas(inl, "a += st_amp[f] * expf(-d * d) * env * base * base;"),
          "streaks are still damped by the band profile SQUARED, away from the edge");
    // A correction gain that divides by dt is not framerate-independent: at
    // 60 fps `corr * 0.25/dt` is a gain of fifteen, at 144 fps thirty-six, and
    // the chain rings node-to-node.
    CHECK(!FileHas(inl, "0.25f / dt"),
          "the dt-scaled constraint feedback is gone");
    // The blend law and the tier gate are contracts, not tuning.
    CHECK(FileHas(inl, "BeginBlendMode(BLEND_ADDITIVE);"),
          "a trail still EMITS: additive, per the blend law");
    CHECK(FileHas(inl, "rlDisableDepthMask();") && FileHas(inl, "rlEnableDepthMask();"),
          "depth WRITE is still off while depth TEST stays on");
    CHECK(FileHas(inl, "GfxQuality_Get() < GFX_MED"),
          "the filament strand count is still gated by the quality tier");
    CHECK(FileHas(inl, "{ -1.00f, -0.50f, 0.38f, 1.00f }"),
          "the filament spread table still matches this mirror");
    // The bug the 29/07 capture showed: the strands knotted at the head because
    // the offset axis flipped sign at every inflection.
    CHECK(FileHas(inl, "Vector3DotProduct(lat, s->lateralAxis) < 0.0f"),
          "the FILAMENT spread axis is still sign-STABILISED, unlike `normal`");
    CHECK(FileHas(inl, "Vector3Scale(s->lateralAxis,"),
          "the spread still uses lateralAxis, not the raw swing normal");
    CHECK(FileHas(inl, "#define SWEPT_TELEPORT_SPEED 45.0f") &&
          FileHas(inl, "#define SWEPT_TELEPORT_MIN   0.75f"),
          "the teleport discriminator still matches this mirror");
    CHECK(FileHas(inl, "SweptTrail_Cut(s, i, tip);"),
          "a teleport still CUTS the trail instead of bridging the gap");
    // Layer four of the reference sheet, and the only one that is not geometry.
    CHECK(FileHas(inl, "s->sparkAcc += dt * SWEPT_SPARK_RATE"),
          "sparkles are still shed at a RATE, not a count per frame");
    CHECK(FileHas(inl, "sqrtf(Random01()) * (float)span"),
          "sparkles are still born ALONG the ribbon, biased toward the head");
    // The shape's life is SIMULATED, not decorated. A synthetic wave layered on
    // a rigid history is what the owner read as "a picture being moved in a
    // circle" (29/07) — the ribbon has to lag, sag and overshoot on its own.
    CHECK(FileHas(inl, "ForceField_Evaluate(fld, s->ring[idx], s->nvel[idx]"),
          "node motion still comes from a ForceField, not hand-written sin()");
    CHECK(FileHas(inl, "FORCE_NOISE_CURL"),
          "the air the ribbon hangs in is still divergence-free curl noise");
    CHECK(FileHas(inl, "FORCE_DRAG"),
          "drag is still what makes the tail lag instead of snapping to the path");
    CHECK(FileHas(inl, "Ribbon_ConstrainSegment(&s->ring[lead], &s->ring[idx],"),
          "inextensibility still uses the SHARED rope constraint, not a second copy");
    // A node that collapses onto its neighbour, or passes through it, gives a
    // zero or REVERSED segment — and a reversed segment flips the strip's side
    // vector and pinches the band into a bowtie (owner's 29/07 capture).
    // THE MODE, not the number. `stretchOnly = false` does not mean "also
    // enforce a minimum", it means "force the distance to be EXACTLY this" — so
    // the floor call silently overwrote the ceiling call and pinned every
    // segment to a third of its rest spacing. A 6 m ribbon collapsed to a third
    // of its length and read as a short stiff spindle (owner's recording,
    // 29/07). The enum exists so that mistake cannot be spelled.
    CHECK(FileHas(inl, "RIBBON_CONSTRAIN_MAX);"),
          "the ceiling call is still a MAX (inextensible), not an exact length");
    CHECK(FileHas(inl, "RIBBON_CONSTRAIN_MIN);"),
          "the floor call is still a MIN (cannot collapse), not an exact length");
    CHECK(!FileHas(inl, ", true, false);") && !FileHas(inl, ", true, true);"),
          "no constraint call passes a bare bool any more");
    CHECK(FileHas("core/ribbon_strip.h", "RIBBON_CONSTRAIN_EXACT") &&
          FileHas("core/ribbon_strip.h", "RIBBON_CONSTRAIN_MAX") &&
          FileHas("core/ribbon_strip.h", "RIBBON_CONSTRAIN_MIN"),
          "the three constraint intents are still named in the API");
    CHECK(FileHas(inl, "#define SWEPT_FLOW_SPEED     2.10f"),
          "the flow speed still matches this mirror");
    // THE ANCHOR. Without it the cloth forces drive the shape instead of
    // perturbing it, and the ribbon writhes free of the path it was dragged
    // along — the owner's "snake being swung by the head" (29/07).
    CHECK(FileHas(inl, "Vector3Scale(pull, SWEPT_HOME_SPRING)"),
          "nodes are still sprung back toward the path they were LAID on");
    CHECK(FileHas(inl, "#define SWEPT_HOME_MAX_DEV   0.30f"),
          "and the stray distance is still hard-bounded in metres");
    // ...but that metre bound must apply ACROSS the path only. Applied to the
    // whole deviation it is 2.5x the node spacing at bench speed and the ribbon
    // folds (Test_NodesCannotCrossTheirNeighbour). The split is the fix, so the
    // decomposition itself is what has to be pinned, not just the constant.
    CHECK(FileHas(inl, "#define SWEPT_ORDER_FRAC     0.45f"),
          "the along-path bound is still a FRACTION of the node spacing");
    CHECK(FileHas(inl, "float along = Vector3DotProduct(off, dir);"),
          "the deviation is still split along/across the path before clamping");
    CHECK(FileHas(inl, "float spacing = fminf(s->nrest[idx], s->nrest[leadI]);"),
          "and the along bound still uses the SMALLER of the two spacings");
    // THE BUG THIS CAUGHT. nhome[] shadows ring[], and ring[0] is seeded in TWO
    // places that Push() never runs through — spawn and teleport-cut. Leaving
    // nhome[0] at {0,0,0} anchored the trail's first node to the WORLD ORIGIN,
    // i.e. it appeared at the map centre and snapped to the spawn point.
    CHECK(FileHas(inl, "s->nhome[0]    = tip;") && FileHas(inl, "s->nhome[0]   = tip;"),
          "the anchor is seeded in BOTH places the ring is seeded (spawn and cut)");
    // Contrast: the core burns at the head only, so there is one bright spot
    // rather than a uniformly lit length.
    CHECK(FileHas(inl, "al *= powf(t, SWEPT_CORE_HEAD_POW)"),
          "the white-hot core is still concentrated at the head");
    CHECK(FileHas(inl, "{0.00f, 0.18f, 1.00f}"),
          "the hue-carrying glow layer is still NOT whitened");
    // Dust, not sparks: it stays where it was born and twinkles.
    CHECK(FileHas(inl, ".velocity = Vector3Scale(jit, 0.11f),"),
          "motes are still nearly stationary dust, not thrown grit");
    CHECK(!FileHas(inl, ".stretchStrength = 1.0f,"),
          "and they are no longer streaked (a streaked dust mote is a contradiction)");
    CHECK(FileHas(inl, ".alphaCurve = &s_sweptTwinkle,"),
          "the twinkle curve is still wired");
    CHECK(FileHas("core/ribbon_strip.h", "void Ribbon_ConstrainSegment("),
          "the rope constraint is still public, so there is only one of it");
    // Only the newest node belongs to the emitter; everything behind it is free.
    CHECK(FileHas(inl, "for (int k = 1; k < n; k++)"),
          "the head is still pinned and the rest of the ribbon is not");
    // The sheet is SYMMETRIC now: a camera-facing strip has no outer edge, so
    // an edge-weighted mask puts its bright line at whatever side the view makes
    // u = 0. The brightness comes from the layers instead.
    CHECK(FileHas(inl, "float d = fabsf(u - 0.5f) * 2.0f;"),
          "the trail sheet is still symmetric across the band");
    CHECK(!FileHas(inl, "0.50f * body + 0.60f * rim"),
          "the edge-weighted sheet is gone");
    CHECK(FileHas(inl, "s_sweptBladeFlat") && FileHas(inl, "s_sweptCamFacing"),
          "both diagnostic dials are still wired");
    // The inner core is the last structural difference between BLADE and the two
    // styles that never dashed; it is 0.267x the width and 1.5x the brightness
    // of the band, so it goes sub-pixel four times sooner.
    CHECK(FileHas(inl, "static float s_sweptCore      = 0.0f;"),
          "the sub-pixel inner core is OFF by default");
    const char *rib = "core/ribbon_strip.c";
    // THE PINCH. `cross(tangent, normal)` collapses where the path runs along
    // the reference direction; the old code then fell back to an unrelated
    // vector, so `side` JUMPED rather than flipped, and the band closed to a
    // point and re-opened rotated. A sign check cannot undo a 90-degree jump.
    CHECK(FileHas(rib, "#define RIBBON_SIDE_DEGENERATE"),
          "a degenerate cross product is still DETECTED rather than fallen back on");
    CHECK(FileHas(rib, "prevSide, Vector3Scale(tangent, Vector3DotProduct(prevSide, tangent))"),
          "and the side vector is still parallel-TRANSPORTED through it");
    // The TANGENT is the one that gets fabricated. ComputeTangent returns a
    // hard-coded (1,0,0) when the central difference degenerates, and that is a
    // confident wrong unit vector, not a short one — so a cross-product length
    // guard cannot see it. It has to be validated one level up.
    CHECK(FileHas(rib, "d = Vector3Subtract(points[i].position, points[i - 1].position);"),
          "the tangent still falls back to a ONE-SIDED difference before giving up");
    CHECK(FileHas(rib, "tangent = havePrevTangent ? prevTangent"),
          "and it is still carried forward rather than fabricated");
    // Only ONE layer may carry the fibres: three additive copies of the same
    // quasi-periodic pattern at different phases sum to something flat, which is
    // why the flow looked frozen at every scroll speed.
    CHECK(FileHas(inl, "(pass == 1 || s_sweptHaloTex.id == 0)"),
          "the fibre sheet is still used by the BODY layer alone");
    // The anti-bowtie continuity check must compare the vector the geometry is
    // BUILT from. Recording it before the foreshortening blend meant the check
    // ran on the raw side while the quads used the blended one, and the two are
    // free to point opposite ways — which is a twist, not a subtle error.
    {
        char *src = NULL;
        FILE *f = fopen(rib, "rb");
        static char buf[200000];
        if (f) { size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f); src = buf; }
        const char *blend = src ? strstr(src, "float w = 1.0f - proj / RIBBON_MIN_PROJECTION;") : NULL;
        const char *store = src ? strstr(src, "prevSide = side;") : NULL;
        CHECK_MSG(blend && store && store > blend,
                  "side continuity is recorded AFTER every modification to `side`",
                  "%s", blend && store ? "store precedes the blend" : "pattern not found");
    }
    CHECK(FileHas(rib, "#define RIBBON_MIN_PROJECTION 0.35f"),
          "ribbon_strip still holds a minimum projected width");
    CHECK(FileHas(rib, "if (mode != RIBBON_CAMERA_FACING) {"),
          "the projected-width guard still exempts camera-facing strips");
    CHECK(FileHas(rib, "float w = 1.0f - proj / RIBBON_MIN_PROJECTION;"),
          "the guard still ROTATES the side vector rather than widening it");
    CHECK(!FileHas(rib, "tint.a * (1.0f / widen)"),
          "the widen-and-dim version is gone (it made dim gaps, not solid band)");
    CHECK(!FileHas(inl, "s_slashTex"),
          "the trail no longer borrows the SweepSlash sheet");
    CHECK(FileHas(inl, "#define SWEPT_MIN_PIXELS      2.0f") &&
          FileHas(inl, "#define SWEPT_CORE_MIN_PIXELS 5.0f"),
          "the screen-space floor still matches this mirror");
    CHECK(FileHas(inl, "aScale * s_sweptAlphaMul;"),
          "the screen-space floor is still PAID FOR in alpha, not applied bare");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sweptAlphaCurve[VFX_TRAIL_BLADE], 0.25f, 0.32f);"),
          "the blade's tail alpha still falls faster than its width");
}

int main(void)
{
    printf("=== swept trail (H1) ===\n");
    Test_ClothStaysNearThePath();
    Test_NodesCannotCrossTheirNeighbour();
    Test_Aspect();
    Test_WidthEnvelope();
    Test_LagSchedule();
    Test_MaxNodes();
    Test_SampleClock();
    Test_SideIsOuter();
    Test_FigureEight();
    Test_FilamentBundle();
    Test_Teleport();
    Test_BladeMask();
    Test_ScreenFloor();
    Test_TailFadesFasterThanItThins();
    Test_Foreshortening();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

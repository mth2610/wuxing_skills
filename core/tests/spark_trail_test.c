// core headless test — VFX_ComposeSparkTrail's shape and its wiring into the
// WISP trail type.
//
// This primary exists because Charge Converge and the deleted Spirit Swarm read
// as DOTS. Everything that decides whether a spark reads as a spark rather than
// a dash or a leaf is arithmetic — its aspect against its own length, whether
// both ends come to a point, whether its alpha falls at least as fast as its
// width — and none of it needs a GPU (core/CLAUDE.md §1).
//
// It also pins the ONE thing about the WISP type that a signature cannot tell
// you and that would silently draw the tail out in FRONT: which end of the
// strand is the head.
//
// This mirrors core/composition/common/vc_spark_trail.inl.
//
// What the mirror CANNOT see: whether a spiral of these reads as energy being
// pulled in, and whether the element force field carries them convincingly.

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

#define SPARK_TRAIL_NODES  12
#define SPARK_TRAIL_ASPECT (1.0f / 28.0f)
#define TRAIL_HISTORY_COUNT 60

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

// segRatio 1 = HEAD (history[0], where the spark is), 0 = the tail's tip.
static const Stop k_w[] = {{0.00f,0.00f},{0.30f,0.62f},{0.80f,1.00f},{1.00f,0.20f}};
static const Stop k_a[] = {{0.00f,0.00f},{0.30f,0.45f},{0.85f,1.00f},{1.00f,1.00f}};

// The WISP type's BUILT-IN taper, which this primary deliberately overrides.
static float SmoothStepC(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
static float WispBuiltinTaper(float segRatio)
{
    return SmoothStepC(0.0f, 0.2f, segRatio) * SmoothStepC(1.0f, 0.5f, 1.0f - segRatio);
}

// ── 1. Aspect: a spark, not a leaf ───────────────────────────────────────────

static void Test_Aspect(void)
{
    // The WISP draw uses `thick` as a HALF-width, so the full width is twice it.
    float worst = 0.0f;
    for (float len = 0.1f; len < 4.0f; len += 0.05f) {
        float full = 2.0f * (len * SPARK_TRAIL_ASPECT);
        float ratio = len / full;
        float e = fabsf(ratio - 14.0f) / 14.0f;
        if (e > worst) worst = e;
    }
    // INVARIANCE is the assertion that matters: a ratio measured at one length
    // passes on the broken formula too (core/docs/LANDMINES.md).
    CHECK_MSG(worst < 1e-4f, "the 1:14 comet aspect holds at EVERY tail length",
              "worst relative error %.5f", worst);

    float full = 2.0f * (0.45f * SPARK_TRAIL_ASPECT);
    CHECK_MSG(fabsf(full - 0.0321f) < 1e-3f,
              "the bench's 45 cm spark is about 3 cm across", "%.4f m", full);
}

// ── 2. Both ends come to a point ─────────────────────────────────────────────

static void Test_Lens(void)
{
    CHECK(CurveEval(k_w, 4, 0.0f) == 0.0f, "the tail's far tip is a needle");
    CHECK_MSG(CurveEval(k_w, 4, 1.0f) < 0.30f,
              "and the HEAD comes to a point too, rather than a cut-off rectangle",
              "%.3f at the head", CurveEval(k_w, 4, 1.0f));

    // Widest just BEHIND the head — a comet, not a teardrop pointing backwards.
    float peakS = 0.0f, peak = 0.0f;
    for (float s = 0.0f; s <= 1.0f; s += 0.002f) {
        float w = CurveEval(k_w, 4, s);
        if (w > peak) { peak = w; peakS = s; }
    }
    CHECK_MSG(peakS > 0.55f && peakS < 0.95f, "it is widest just behind the head",
              "peak at %.3f", peakS);

    // THE REASON THIS CURVE EXISTS. The WISP type's own taper is pointed at the
    // tail and FLAT at the head — full width from segRatio 0.5 onward — so a
    // spark drawn with the default ends in a rectangle exactly where the eye is
    // looking. Asserting the built-in is flat there keeps that justification
    // honest if the trail system ever changes it.
    CHECK_MSG(WispBuiltinTaper(1.0f) > 0.95f && WispBuiltinTaper(0.7f) > 0.95f,
              "the WISP built-in taper really is FLAT at the head (hence the override)",
              "%.3f at head, %.3f at 0.7",
              WispBuiltinTaper(1.0f), WispBuiltinTaper(0.7f));
}

// ── 3. Alpha falls at least as fast as width ─────────────────────────────────

static void Test_TailFadesFasterThanItThins(void)
{
    // Anywhere a strip narrows to nothing it is sub-pixel before it is gone, so
    // brightness has to lead the narrowing or the last stretch breaks into
    // dashes (core/docs/LANDMINES.md, 29/07).
    float worstS = -1.0f, gap = 0.0f;
    for (float s = 0.01f; s <= 0.35f; s += 0.01f) {
        float w = CurveEval(k_w, 4, s), a = CurveEval(k_a, 4, s);
        if (a > w && (a - w) > gap) { gap = a - w; worstS = s; }
    }
    CHECK_MSG(worstS < 0.0f, "tail alpha never leads tail width",
              "alpha leads by %.3f at s = %.2f", gap, worstS);
}

// ── 4. Budget ────────────────────────────────────────────────────────────────

static void Test_Budget(void)
{
    CHECK_MSG(SPARK_TRAIL_NODES <= TRAIL_HISTORY_COUNT,
              "the node count fits the trail system's history", "%d of %d",
              SPARK_TRAIL_NODES, TRAIL_HISTORY_COUNT);
    // A tail, not a rope. These are spawned by the DOZEN at a rate, so the node
    // count is the whole cost story: 40/s x 0.7 s life x 12 nodes is ~340 nodes
    // live, inside a 500-entity pool.
    CHECK_MSG(SPARK_TRAIL_NODES <= 16, "a spark keeps a short history, not a long one",
              "%d nodes", SPARK_TRAIL_NODES);
    float liveAtBenchRate = 40.0f * 0.7f;
    CHECK_MSG(liveAtBenchRate < 500.0f,
              "the bench's emission rate stays inside the trail pool",
              "%.0f live vs 500", liveAtBenchRate);
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
    const char *inl = "core/composition/common/vc_spark_trail.inl";
    CHECK(FileHas(inl, "#define SPARK_TRAIL_ASPECT (1.0f / 28.0f)"),
          "the aspect still matches this mirror");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_sparkWidth, 1.00f, 0.20f);"),
          "the head still tapers to a point");

    // THE WIRING FACT A SIGNATURE CANNOT CARRY. SpawnTrailEntity lays a WISP as
    // pos + strandDir * u * len, and the WISP draw maps segRatio 1 to
    // history[0] — so `target` is the direction the TAIL trails in. Point it
    // along the velocity instead and every spark grows its tail out in FRONT of
    // itself, which is not a subtle failure but is a very easy edit to make.
    CHECK(FileHas(inl, "Vector3Scale(Vector3Normalize(vel), -1.0f)"),
          "the strand is still laid AGAINST the velocity (tail behind, not ahead)");

    // Contracts, not tuning.
    CHECK(FileHas(inl, "cfg.blendMode        = BLEND_ADDITIVE;"),
          "a spark still EMITS: additive, per the blend law");
    CHECK(FileHas(inl, "cfg.ribbonMode       = RIBBON_CAMERA_FACING;"),
          "still camera-facing — the mode that does not dash on a curve");
    CHECK(FileHas(inl, "cfg.disableInnerCore = true;"),
          "still no second sub-pixel core strip");
    CHECK(FileHas(inl, "cfg.forceField  = m->fld;"),
          "drift still comes from the element's material, not from this file");
    CHECK(FileHas(inl, "cfg.priority    = VFX_PRIORITY_LOW;"),
          "a spark still cannot evict an ultimate's trail");
}

int main(void)
{
    printf("=== spark trail (primary) ===\n");
    Test_Aspect();
    Test_Lens();
    Test_TailFadesFasterThanItThins();
    Test_Budget();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

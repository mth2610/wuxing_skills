// core headless test — CONTACT SPARK's radial strand geometry.
//
// INHERITED FROM spark_trail_test.c, which was deleted with SPARK TRAIL itself
// on 27/08/2026. The arithmetic it pinned did not go with that effect: the
// curves, the 1:28 aspect and the WISP head-end fact moved into
// vc_contact_spark.inl, which was always their other reader, so the guard moved
// with them. What is gone from here is only what was specific to the deleted
// primary — its element force field, and its velocity-derived strand direction.
//
// Everything that decides whether a strand reads as a spark rather than a dash
// or a leaf is arithmetic — its aspect against its own length, whether both
// ends come to a point, whether its alpha falls at least as fast as its width —
// and none of it needs a GPU (core/CLAUDE.md §1).
//
// This mirrors core/composition/common/vc_contact_spark.inl.
//
// What the mirror CANNOT see: whether a burst of these reads as one contact
// rather than as unrelated lines that happened to cross a position.

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

#define CONTACT_SPARK_NODES  12
#define CONTACT_SPARK_ASPECT (1.0f / 28.0f)
#define CONTACT_SPARK_MAX    12
#define TRAIL_HISTORY_COUNT  60

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

// segRatio 1 = HEAD (history[0], where the strand's tip is), 0 = the tail's end.
static const Stop k_w[] = {{0.00f,0.00f},{0.30f,0.62f},{0.80f,1.00f},{1.00f,0.20f}};
static const Stop k_a[] = {{0.00f,0.00f},{0.30f,0.45f},{0.85f,1.00f},{1.00f,1.00f}};

// The WISP type's BUILT-IN taper, which this composition deliberately overrides.
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
        float full = 2.0f * (len * CONTACT_SPARK_ASPECT);
        float ratio = len / full;
        float e = fabsf(ratio - 14.0f) / 14.0f;
        if (e > worst) worst = e;
    }
    // INVARIANCE is the assertion that matters: a ratio measured at one length
    // passes on the broken formula too (core/docs/LANDMINES.md).
    CHECK_MSG(worst < 1e-4f, "the 1:14 comet aspect holds at EVERY strand length",
              "worst relative error %.5f", worst);

    // The composition's own range is 0.90..2.20 m before its scale argument.
    float full = 2.0f * (0.90f * CONTACT_SPARK_ASPECT);
    CHECK_MSG(fabsf(full - 0.0643f) < 1e-3f,
              "the shortest strand is about 6 cm across", "%.4f m", full);
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
    // strand drawn with the default ends in a rectangle exactly where the eye is
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
    CHECK_MSG(CONTACT_SPARK_NODES <= TRAIL_HISTORY_COUNT,
              "the node count fits the trail system's history", "%d of %d",
              CONTACT_SPARK_NODES, TRAIL_HISTORY_COUNT);
    // A tail, not a rope. A burst is 8..12 strands at once, so the node count
    // is the whole cost story.
    CHECK_MSG(CONTACT_SPARK_NODES <= 16, "a strand keeps a short history, not a long one",
              "%d nodes", CONTACT_SPARK_NODES);
    float liveAtWorstBurst = (float)(CONTACT_SPARK_MAX * CONTACT_SPARK_NODES);
    CHECK_MSG(liveAtWorstBurst < 500.0f,
              "the worst single burst stays inside the trail pool",
              "%.0f nodes vs a 500-entity pool", liveAtWorstBurst);
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
    const char *inl = "core/composition/common/vc_contact_spark.inl";
    CHECK(FileHas(inl, "#define CONTACT_SPARK_ASPECT (1.0f / 28.0f)"),
          "the aspect still matches this mirror");
    CHECK(FileHas(inl, "FloatCurve_AddStop(&s_contactSparkWidth, 1.00f, 0.20f);"),
          "the head still tapers to a point");

    // THE WIRING FACT A SIGNATURE CANNOT CARRY. SpawnTrailEntity lays a WISP as
    // pos + strandDir * u * len, and the WISP draw maps segRatio 1 to
    // history[0] — so `target` is the direction the TAIL trails in. Point it
    // along the flight direction instead and every strand grows its tail out in
    // FRONT of itself, which is not a subtle failure but is a very easy edit to
    // make.
    CHECK(FileHas(inl, "cfg.target = Vector3Scale(dir, -1.0f); // history trails behind the head"),
          "the strand is still laid AGAINST the flight direction (tail behind, not ahead)");

    // Contracts, not tuning.
    CHECK(FileHas(inl, "cfg.blendMode = BLEND_ADDITIVE;"),
          "a spark still EMITS: additive, per the blend law");
    CHECK(FileHas(inl, "cfg.ribbonMode = RIBBON_CAMERA_FACING;"),
          "still camera-facing — the mode that does not dash on a curve");
    CHECK(FileHas(inl, "cfg.disableInnerCore = true;"),
          "still no second sub-pixel core strip");
    CHECK(FileHas(inl, "cfg.forceField = NULL;"),
          "still straight radial flight — a contact burst is not element drift");
    CHECK(FileHas(inl, "cfg.priority = VFX_PRIORITY_LOW;"),
          "a contact spark still cannot evict an ultimate's trail");

    // The purge is load-bearing: the deleted primary must not come back by name.
    CHECK(!FileHas(inl, "SPARK_TRAIL_ASPECT") && !FileHas(inl, "s_sparkWidth"),
          "and no SPARK_TRAIL_* name survived the effect it was named for");
}

int main(void)
{
    printf("=== contact spark (radial strands) ===\n");
    Test_Aspect();
    Test_Lens();
    Test_TailFadesFasterThanItThins();
    Test_Budget();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

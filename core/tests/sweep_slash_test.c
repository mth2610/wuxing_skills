// core headless test — VFX_ComposeSweepSlash's geometry and mask arithmetic.
//
// A slash is almost entirely arithmetic: whether the head outruns the tail,
// whether the band ever inverts, whether the tip comes to a point, and whether
// the generated mask is actually asymmetric across the blade. None of those
// questions needs a GPU, and every one of them would otherwise be answered by a
// build → screenshot → guess cycle (core/CLAUDE.md §1).
//
// This mirrors core/composition/common/vc_sweep_slash.inl. A mirror rots into
// fiction the moment the source moves, so Test_MirrorStillMatchesSource pins the
// load-bearing expressions — if the .inl changes shape, this file fails LOUDLY
// rather than continuing to assert things about code that no longer exists.
//
// What the mirror CANNOT see: how the band looks against the scene, whether the
// tilt reads as a diagonal from the game camera, and whether the striation
// frequency survives at gameplay distance. Those are eyeball questions.

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

#ifndef PI
#define PI 3.1415926535f
#endif

// ── the mirror ───────────────────────────────────────────────────────────────

#define SLASH_SWEEP_T  0.34f
#define SLASH_TAIL_LAG 0.16f
#define SLASH_TAIL_END 1.00f

static float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static float SmoothStep01(float x)
{
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

// aHead/aTail, exactly as the .inl computes them.
static void Schedule(float t01, float arcRad, float *aHead, float *aTail)
{
    const float half = arcRad * 0.5f;
    float headT = Clamp01(t01 / SLASH_SWEEP_T);
    float tailT = Clamp01((t01 - SLASH_TAIL_LAG) / (SLASH_TAIL_END - SLASH_TAIL_LAG));
    float headE = 1.0f - powf(1.0f - headT, 2.2f);
    float tailE = SmoothStep01(tailT);
    *aHead = -half + arcRad * headE;
    *aTail = -half + arcRad * tailE;
}

static float WidthEnv(float s)
{
    if (s <= 0.0f || s >= 1.0f) return 0.0f;
    return powf(sinf(PI * s), 0.85f);
}

// The cross-blade profile, as SweepSlash_Profile computes it. u = 0 is the OUTER
// edge of the strip — see the .inl for the cross-product derivation that fixes
// which side that is. `withRim` false = the halo sheet.
static float Profile(float u, int withRim)
{
    float e = 1.0f - u;
    if (!withRim)
        return powf(e, 1.7f) * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));
    float body = powf(e, 2.4f);
    float d    = u / 0.05f;
    float rim  = expf(-d * d);
    float prof = 0.40f * body + 0.72f * rim;
    return prof * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));
}

// One texel of the generated edge mask.
static float MaskAlpha(float u, float v)
{
    float lines  = 0.72f + 0.28f * sinf(u * PI * 9.0f  + v * 1.7f);
    float lines2 = 0.85f + 0.15f * sinf(u * PI * 23.0f - v * 0.6f);
    float pulse  = 0.80f + 0.20f * sinf(v * PI * 5.0f + u * 2.3f);
    float a = Profile(u, 1) * lines * lines2 * pulse;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

// ── the band ─────────────────────────────────────────────────────────────────

static void Test_HeadNeverFallsBehindTail(void)
{
    // If the tail ever passes the head the strip turns inside out: the ribbon
    // points run backwards, halfWidth is applied to a reversed tangent, and the
    // band flips through itself for a frame. Cheap to prove it cannot happen.
    const float arc = 2.2f;
    int inverted = 0;
    float worstT = -1.0f, worstSpan = 0.0f;
    for (int i = 0; i <= 1000; i++)
    {
        float t = (float)i / 1000.0f;
        float h, l;
        Schedule(t, arc, &h, &l);
        if (h - l < -1e-6f) { inverted++; if (h - l < worstSpan) { worstSpan = h - l; worstT = t; } }
    }
    CHECK_MSG(inverted == 0, "the head never falls behind the tail (band never inverts)",
              "%d samples inverted, worst %.4f rad at t01=%.3f", inverted, worstSpan, worstT);
}

static void Test_BandExistsWhileTheSwingReads(void)
{
    // The band must be open through the part of the swing the eye is on. A
    // schedule where the tail catches up early gives a slash that is gone before
    // it has crossed — the failure mode of every hand-timed arc in the old
    // SlashArc family.
    const float arc = 2.2f;
    float h, l;
    Schedule(0.34f, arc, &h, &l);          // head has just arrived
    float atPeak = h - l;
    CHECK_MSG(atPeak > 0.55f * arc,
              "at the moment the head arrives, the band still spans >55% of the arc",
              "%.0f%% of %.2f rad", 100.0f * atPeak / arc, arc);

    Schedule(1.0f, arc, &h, &l);
    CHECK_MSG(fabsf(h - l) < 1e-5f, "the band closes to zero exactly at t01 = 1",
              "span %.6f rad", h - l);

    Schedule(0.0f, arc, &h, &l);
    CHECK_MSG(fabsf(h - l) < 1e-5f, "the band is closed at t01 = 0 (nothing pops in)",
              "span %.6f rad", h - l);
}

static void Test_HeadIsFastestAtTheStart(void)
{
    // Ease-out, i.e. a follow-through: the first third of the head's travel must
    // cover more arc than the last third. A linear head reads as a machine part
    // swinging, not as a body committing to a swing.
    const float arc = 2.2f;
    float h0, h1, h2, h3, l;
    Schedule(0.0f, arc, &h0, &l);
    Schedule(SLASH_SWEEP_T / 3.0f, arc, &h1, &l);
    Schedule(SLASH_SWEEP_T * 2.0f / 3.0f, arc, &h2, &l);
    Schedule(SLASH_SWEEP_T, arc, &h3, &l);
    CHECK_MSG((h1 - h0) > 1.8f * (h3 - h2),
              "the head covers >1.8x more arc in its first third than its last",
              "first %.2f rad, last %.2f rad", h1 - h0, h3 - h2);
}

static void Test_BothEndsComeToAPoint(void)
{
    // A blade leaves a LENS — pointed at both ends, because both ends are the
    // same edge at different times. The first envelope (sqrt, belly at 0.8) was
    // zero at both ends on paper and drew a blunt club leading a thin streamer:
    // it reached HALF width by s = 0.25, so the tail was fat for most of its
    // length and only pinched at the very last percent. "Zero at the endpoint" is
    // not the same property as "comes to a point".
    CHECK_MSG(WidthEnv(1.0f) < 1e-4f, "half-width is zero at the head",
              "%.5f", WidthEnv(1.0f));
    CHECK_MSG(WidthEnv(0.0f) < 1e-4f, "half-width is zero at the tail",
              "%.5f", WidthEnv(0.0f));

    // The real assertion: the two ends must taper at the SAME rate, i.e. neither
    // end is the blunt one.
    float worst = 0.0f, worstAt = 0.0f;
    for (int i = 1; i < 500; i++)
    {
        float s = (float)i / 1000.0f;
        float d = fabsf(WidthEnv(s) - WidthEnv(1.0f - s));
        if (d > worst) { worst = d; worstAt = s; }
    }
    CHECK_MSG(worst < 1e-4f, "the envelope is symmetric (neither end is blunt)",
              "worst mismatch %.5f at s=%.3f", worst, worstAt);

    // ...and only ONE belly in between. Two maxima would read as a bone.
    int peaks = 0;
    float peakAt = 0.0f, peakVal = 0.0f;
    for (int i = 1; i < 999; i++)
    {
        float s = (float)i / 1000.0f;
        float a = WidthEnv(s - 0.001f), b = WidthEnv(s), c = WidthEnv(s + 0.001f);
        if (b > a && b >= c) { peaks++; peakAt = s; peakVal = b; }
    }
    CHECK_MSG(peaks == 1, "the width envelope has exactly one belly", "%d peaks", peaks);
    CHECK_MSG(peakAt > 0.45f && peakAt < 0.55f, "the belly sits in the middle",
              "peak at s=%.2f (%.2f)", peakAt, peakVal);

    // Quarter-width check: the old sqrt hit 0.50 at s = 0.25, which is what made
    // the tail look blunt. A lens is well under half by then.
    CHECK_MSG(WidthEnv(0.25f) < 0.80f && WidthEnv(0.10f) < 0.45f,
              "the taper is gradual near the ends, not a step up to full width",
              "s=0.10 -> %.2f, s=0.25 -> %.2f", WidthEnv(0.10f), WidthEnv(0.25f));
}

static void Test_HaloSheetHasNoRim(void)
{
    // All three passes share one outer edge, so a halo carrying the rim lays a
    // 1.8x thicker rim on the same line and the edge becomes a broad gradient —
    // "crescent moon, not a cut". The halo must therefore be MONOTONE: brightest
    // at the border-shoulder and falling inward, with no interior spike.
    float prev = -1.0f;
    int rises = 0;
    for (int x = 1; x < 192; x++)
    {
        float u = ((float)x + 0.5f) / 192.0f;
        if (u < 0.05f) continue;              // inside the AA shoulder
        float a = Profile(u, 0);
        if (prev >= 0.0f && a > prev + 1e-6f) rises++;
        prev = a;
    }
    CHECK_MSG(rises == 0, "the halo sheet falls monotonically inward (no rim of its own)",
              "%d rises", rises);
    // The discriminating comparison is SHAPE, not level: the edge sheet spikes
    // near the border, the halo does not. (Levels are set by the pass alphas —
    // the halo draws at 0.26 — so comparing raw profiles proves nothing.)
    float edgeSpike = Profile(0.03f, 1) / Profile(0.25f, 1);
    float haloSpike = Profile(0.03f, 0) / Profile(0.25f, 0);
    CHECK_MSG(edgeSpike > 2.5f && haloSpike < 1.6f,
              "the edge sheet spikes at the border and the halo does not",
              "edge %.2fx vs halo %.2fx over their own mid-blade value",
              edgeSpike, haloSpike);
}

// ── the mask ─────────────────────────────────────────────────────────────────

static void Test_MaskIsAsymmetricAcrossTheBlade(void)
{
    // A symmetric cross-section is a tube. The blade's own edge is at u = 0 and
    // everything behind it is a smear, so the outer half must dominate by a lot.
    float inner = 0.0f, outer = 0.0f;
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 64; x++)
        {
            float u = ((float)x + 0.5f) / 64.0f;
            float v = ((float)y + 0.5f) / 128.0f;
            float a = MaskAlpha(u, v);
            if (u < 0.5f) outer += a; else inner += a;
        }
    CHECK_MSG(outer > 3.0f * inner,
              "the outer half of the mask carries >3x the inner half's energy",
              "outer %.0f vs inner %.0f (%.1fx)", outer, inner, outer / (inner + 1e-6f));
}

static void Test_MaskEdgeIsHotButAntiAliased(void)
{
    // The rim has to be a bright LINE just inside the border, and the border
    // itself has to fall to nothing — a mask that is still bright at u = 1
    // staircases the moment the arc is seen edge-on.
    float best = 0.0f, bestU = 0.0f;
    for (int x = 0; x < 64; x++)
    {
        float u = ((float)x + 0.5f) / 64.0f;
        float a = MaskAlpha(u, 0.5f);
        if (a > best) { best = a; bestU = u; }
    }
    CHECK_MSG(bestU > 0.02f && bestU < 0.18f,
              "the hot line sits just INSIDE the outer edge (u = 0 side)",
              "brightest at u=%.3f (%.2f)", bestU, best);
    CHECK_MSG(MaskAlpha(0.001f, 0.5f) < 0.10f * best,
              "the outer border itself falls to <10% of the rim (AA shoulder)",
              "border %.3f vs rim %.3f", MaskAlpha(0.001f, 0.5f), best);
    CHECK_MSG(best > 0.55f, "the rim is actually bright, not a hint",
              "peak alpha %.2f", best);
}

static void Test_MaskHasStriations(void)
{
    // The whole reason the mask is a texture rather than a vertex-colour ramp:
    // detail across the blade at a frequency 2 vertices cannot hold. Measure it
    // where the mask has energy — the smear at u < 0.4 is near zero by design,
    // and a ratio taken there says nothing.
    float mean = 0.0f, mn = 1e9f, mx = -1e9f;
    int n = 0;
    for (int x = 0; x < 64; x++)
    {
        float u = ((float)x + 0.5f) / 64.0f;
        if (u > 0.45f) continue;
        float a = MaskAlpha(u, 0.5f);
        mean += a; n++;
        if (a < mn) mn = a;
        if (a > mx) mx = a;
    }
    mean /= (float)n;
    CHECK_MSG((mx - mn) > 0.25f * mean,
              "the striations vary the mask by >25% of its mean across the blade",
              "spread %.3f on mean %.3f", mx - mn, mean);
}

static void Test_PassesShareOneOuterEdge(void)
{
    // The second screenshot: the slash came out as two or three PARALLEL WIRES.
    // The mask's rim sits at a fixed fraction of the strip's width, so passes
    // centred on the same path but of different widths put their rims at
    // different radii. Aligning the passes by their OUTER edge is what collapses
    // them back into one edge — and it is pure arithmetic, so assert it.
    const float passW[3] = {1.5f, 1.0f, 0.34f};
    const float halfW = 0.087f;  // representative: 4 m of arc x 0.022
    for (int i = 1; i <= 20; i++)
    {
        float s   = (float)i / 21.0f;
        float env = WidthEnv(s);
        float ref = 0.0f;
        for (int p = 0; p < 3; p++)
        {
            float hw   = halfW * passW[p] * env;
            float rr   = 1.0f + halfW * env * (1.0f - passW[p]);
            float edge = rr + hw;
            if (p == 0) ref = edge;
            else if (fabsf(edge - ref) > 1e-5f)
            {
                CHECK_MSG(0, "every pass's outer edge lands on the same radius",
                          "pass %d edge %.6f vs %.6f at s=%.2f", p, edge, ref, s);
                return;
            }
        }
    }
    CHECK(1, "every pass's outer edge lands on the same radius (one edge, not three wires)");

    // ...and the widest pass must still reach further INWARD than the others,
    // or the "halo behind the blade" is not behind anything.
    float env = WidthEnv(0.8f);
    float innerHalo = (1.0f + halfW * env * (1.0f - passW[0])) - halfW * passW[0] * env;
    float innerCore = (1.0f + halfW * env * (1.0f - passW[2])) - halfW * passW[2] * env;
    CHECK_MSG(innerHalo < innerCore - 1e-4f,
              "the halo pass reaches further inward than the core pass",
              "halo %.4f vs core %.4f", innerHalo, innerCore);
}

static void Test_BandIsThinAgainstItsOwnArc(void)
{
    // The fourth capture read as a LEAF: pointed at both ends, correct, and far
    // too fat. Fatness is an aspect ratio against the distance the head travels,
    // and the width had been keyed to the arc's RADIUS instead — so the ratio
    // moved with arcRad and only happened to be right at one angle.
    const float length = 1.8f, arcRad = 2.2f;
    float arcLen = length * arcRad;                  // 3.96 m of travel
    float fullWidth = 2.0f * (arcLen * 0.022f);      // at the belly, env = 1
    float ratio = arcLen / fullWidth;
    CHECK_MSG(ratio > 18.0f && ratio < 30.0f,
              "the band is between 1:18 and 1:30 against the arc it travelled",
              "1:%.1f (%.2f m wide over %.2f m)", ratio, fullWidth, arcLen);

    // And the ratio must hold at ANY sweep angle — that is the whole point of
    // keying it to arc length. A narrow flick and a full swing get the same look.
    float flick = length * 0.7f;
    float flickRatio = flick / (2.0f * (flick * 0.022f));
    CHECK_MSG(fabsf(flickRatio - ratio) < 0.5f,
              "a 0.7 rad flick has the same aspect ratio as a 2.2 rad swing",
              "1:%.1f vs 1:%.1f", flickRatio, ratio);
}

// ── the sparks ───────────────────────────────────────────────────────────────

#define SLASH_SPARK_RATE 24.0f
#define SPARK_LIFE_MIN   0.09f
#define SPARK_LIFE_MAX   0.19f
#define SPARK_STRETCH    1.10f

static void Test_SparksStayAGarnish(void)
{
    // The first screenshot: a chain of round beads strung along the arc, with
    // the blade a faint smear behind them. Every one of these three numbers was
    // wrong at once, and all three are computable.
    float avgLife = 0.5f * (SPARK_LIFE_MIN + SPARK_LIFE_MAX);
    float live    = SLASH_SPARK_RATE * avgLife;
    CHECK_MSG(live < 6.0f, "fewer than 6 sparks alive at once (countable = too many)",
              "%.1f live (%.0f/s x %.2f s)", live, SLASH_SPARK_RATE, avgLife);

    // stretchFactor = 1 + speed*strength (core/particle_system.c:1003). A spark
    // thrown at the slowest speed the .inl uses must still draw as a streak.
    float factor = 1.0f + 2.5f * SPARK_STRETCH;
    CHECK_MSG(factor > 3.0f, "even the slowest spark stretches >3x (streak, not dot)",
              "%.2fx at 2.5 m/s", factor);

    // A spark must be gone well before the head has finished its travel, or it
    // stops being something the edge shed and becomes something laid on the arc.
    // Sweep on the bench's 1.6 s swing = 0.34 * 1.6 s.
    float sweepSeconds = 0.34f * 1.6f;
    CHECK_MSG(avgLife / sweepSeconds < 0.35f,
              "a spark lives under 35% of the sweep (it trails, it does not decorate)",
              "%.0f%% of %.2f s", 100.0f * avgLife / sweepSeconds, sweepSeconds);
}

// ── the guard ────────────────────────────────────────────────────────────────

static char *SlurpFile(const char *path)
{
    static char buf[262144];
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *src = SlurpFile("core/composition/common/vc_sweep_slash.inl");
    CHECK(src != NULL, "core/composition/common/vc_sweep_slash.inl readable");
    if (!src) return;

    // Every expression this file mirrors. If one of them moves, the assertions
    // above are describing code that no longer runs.
    CHECK(strstr(src, "#define SLASH_SWEEP_T     0.34f") != NULL,
          "mirror: SLASH_SWEEP_T unchanged");
    CHECK(strstr(src, "#define SLASH_TAIL_LAG    0.16f") != NULL,
          "mirror: SLASH_TAIL_LAG unchanged");
    CHECK(strstr(src, "float headE = 1.0f - powf(1.0f - headT, 2.2f);") != NULL,
          "mirror: the head's ease-out");
    CHECK(strstr(src, "float tailE = SmoothStep01(tailT);") != NULL,
          "mirror: the tail's smoothstep");
    CHECK(strstr(src, "return powf(sinf(PI * s), 0.85f);") != NULL,
          "mirror: the width envelope is still the lens");
    CHECK(strstr(src, "float body = powf(e, 2.4f);") != NULL,
          "mirror: the mask's inward smear");
    CHECK(strstr(src, "float rim  = expf(-d * d);") != NULL,
          "mirror: the mask's hot rim");
    CHECK(strstr(src, "float prof = 0.40f * body + 0.72f * rim;") != NULL,
          "mirror: rim-to-smear weighting unchanged");
    CHECK(strstr(src, "return powf(e, 1.7f) * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));") != NULL,
          "mirror: the halo profile (no rim term)");
    CHECK(strstr(src, "(pass == 0 && s_slashHaloTex.id != 0)") != NULL,
          "the halo pass still gets the rim-less sheet");
    CHECK(strstr(src, "return prof * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));") != NULL,
          "mirror: the mask's AA shoulder at the outer border");
    // The one that would fail silently on screen: which side of the strip the
    // rim is on is decided by ribbon_strip.c's side vector, not by this file.
    CHECK(strstr(src, "float e = 1.0f - u;") != NULL,
          "mirror: u = 0 is still the OUTER edge (mask not flipped)");

    // Contracts this file cannot compute but that the .inl must keep, each one a
    // landmine already paid for elsewhere in the project.
    CHECK(strstr(src, "RIBBON_FIXED_NORMAL") != NULL,
          "the strip stays in ONE plane (spec: not camera-facing)");
    CHECK(strstr(src, "Ribbon_ComputeArcLengthUV") != NULL,
          "arc-length UV, so the mask does not stretch as the band shortens");
    CHECK(strstr(src, "s_slashDistortAcc += dt * SLASH_DISTORT_RATE;") != NULL,
          "refraction is emitted as a RATE, not once per frame");
    CHECK(strstr(src, "s_slashSparkAcc += dt * SLASH_SPARK_RATE") != NULL,
          "sparks are emitted as a RATE, not a count per call");
    CHECK(strstr(src, "float halfW  = arcLen * 0.022f * s_slashWidth * shrink;") != NULL,
          "mirror: width is keyed to ARC LENGTH, not radius");
    CHECK(strstr(src, "rr += halfW * env * (1.0f - passW[pass]);") != NULL,
          "passes are still aligned by their OUTER edge (not by centre)");
    CHECK(strstr(src, "#define SLASH_SPARK_RATE  24.0f") != NULL,
          "mirror: spark rate unchanged");
    CHECK(strstr(src, ".lifetime = Math_Mix(0.09f, 0.19f, Random01()),") != NULL,
          "mirror: spark lifetime unchanged");
    CHECK(strstr(src, ".stretchStrength = 1.10f,") != NULL,
          "mirror: spark stretch unchanged (0.10 drew round beads)");
    CHECK(strstr(src, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false") != NULL &&
          strstr(src, "VFXRender_EndDraw(&renderScope)") != NULL,
          "depth-state change is batch-flushed (ENGINE_LANDMINES §1)");
    CHECK(strstr(src, "CameraShake") == NULL && strstr(src, "Camera_Shake") == NULL,
          "no camera shake on the composition's own initiative");
}

int main(void)
{
    printf("=== core headless test: sweep slash ===\n");
    Test_HeadNeverFallsBehindTail();
    Test_BandExistsWhileTheSwingReads();
    Test_HeadIsFastestAtTheStart();
    Test_BothEndsComeToAPoint();
    Test_MaskIsAsymmetricAcrossTheBlade();
    Test_MaskEdgeIsHotButAntiAliased();
    Test_MaskHasStriations();
    Test_HaloSheetHasNoRim();
    Test_PassesShareOneOuterEdge();
    Test_BandIsThinAgainstItsOwnArc();
    Test_SparksStayAGarnish();
    Test_MirrorStillMatchesSource();
    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}

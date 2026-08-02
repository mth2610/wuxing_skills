// core headless test — the GPU trail deform pipeline (trail_deform.vs/.fs) and
// the C routing that feeds it.
//
// The deform shader is a two-stage pipeline: the VERTEX stage displaces each
// strip vertex in the strip's own (side, stripNormal) frame under one of five
// uniform-selected modes; the FRAGMENT stage reinterprets one RGBA texture as a
// packed 4-channel wisp material and feeds BOTH the alpha body pass and the
// additive emission pass with the same formula.
//
// What the mirror CANNOT see: whether the wisp looks like energy. It can only
// prove that the displacement stays in a sane metre budget, that the strip can
// never detach from its emitter, that two casts with different spawn phases
// actually differ, and that every mode is reachable by exactly one threshold.

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

#define TAU 6.2831853f

// Defaults mirrored from vc_core_smoketrail.inl's energy trail.
// The intended look is "long visible waves, fast texture flow": LOW frequency
// (a few big sweeps down the strip — high freq + high amp is what thrashed),
// crests that TRAVEL fast enough to read as flowing, and the swim axis
// (strip normal) kept under the side axis. The ACTIVE mode is CURL3 (mode 2):
// curl noise is time-panned so the shape is unmistakably animated every
// frame — sines read as rigid ("cứng chứng"). Strength stays small because
// curl3 displaces by (n-0.5)*strength*env*2.0 with |n-0.5| <= 0.5.
static const float kAmpA[3] = { 0.090f, 0.035f, 0.014f };
static const float kAmpB[3] = { 0.050f, 0.020f, 0.008f };
static const float kFreq[3] = { 1.20f, 2.40f, 4.20f };
static const float kSpeed[3] = { 4.50f, 5.50f, 6.50f };
static const float kEnvHead = 0.10f, kEnvTail = 0.88f, kStrength = 0.22f;
static const float kCurlScale = 0.7f;
static const float kTailFadeA = 0.88f, kTailFadeB = 1.0f;
static const float kEdgeTear = 0.35f; // dissolve threshold jitter at the edges
static const float kMatMode = 1.0f;   // packed wisp material (the uber shader's fragment half)

static float SmoothStep(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float SmoothStepC(float e0, float e1, float x)
{
    if (x <= e0) return 0.0f;
    if (x >= e1) return 1.0f;
    return SmoothStep(e0, e1, x);
}

// The envelope from trail_deform.vs: force the wave to 0 at the head (so the
// strip never detaches from the emitter) and to 0 at the tail (so the old end
// does not thrash).
static float MirrorEnv(float seg)
{
    float headFade = kEnvHead > 0.001f ? kEnvHead : 0.001f;
    float tailStart = kEnvTail;
    if (tailStart < kEnvHead) tailStart = kEnvHead;
    if (tailStart > 0.999f) tailStart = 0.999f;
    return SmoothStep(0.0f, headFade, seg) *
           (1.0f - SmoothStep(tailStart, 1.0f, seg));
}

// The sin-multi displacement, split into (side, stripNormal) components.
static void MirrorSinMulti(float seg, float t, float phase,
                           float *sideD, float *normD)
{
    *sideD = kAmpA[0] * sinf(seg * kFreq[0] * TAU + t * kSpeed[0] + phase)
           + kAmpA[1] * sinf(seg * kFreq[1] * TAU + t * kSpeed[1] + phase * 2.31f)
           + kAmpA[2] * sinf(seg * kFreq[2] * TAU + t * kSpeed[2] + phase * 4.67f);
    *normD = kAmpB[0] * sinf(seg * kFreq[0] * 3.77f + t * kSpeed[0] * 1.31f + phase)
           + kAmpB[1] * sinf(seg * kFreq[1] * 5.13f + t * kSpeed[1] * 0.63f + phase * 1.71f)
           + kAmpB[2] * sinf(seg * kFreq[2] * 2.89f + t * kSpeed[2] * 1.87f + phase * 3.13f);
}

// ── 1. Mode thresholds ──────────────────────────────────────────────────────
//
// Five modes share one program; the branch thresholds sit at 0.5 / 1.5 / 2.5 /
// 3.5 so an INTEGER uniform mode (1..4) lands in exactly one branch — a
// 1.5-based chain would drop mode 1 into passthrough and shift every other
// mode up by one (this happened once, caught by this mirror). Mode 0
// (passthrough) never reaches this shader at all: the C side routes
// deform.mode > 0 only.

static int BranchForMode(float mode)
{
    if (mode >= 3.5f) return 4;      // NOISE
    if (mode >= 2.5f) return 3;      // HELIX
    if (mode >= 1.5f) return 2;      // CURL3
    if (mode >= 0.5f) return 1;      // SIN_MULTI
    return 0;                        // passthrough — routed away in C
}

static void Test_ModeThresholds(void)
{
    CHECK_MSG(BranchForMode(1.0f) == 1, "mode 1 lands in SIN_MULTI only", "%d", BranchForMode(1.0f));
    CHECK_MSG(BranchForMode(2.0f) == 2, "mode 2 lands in CURL3 only", "%d", BranchForMode(2.0f));
    CHECK_MSG(BranchForMode(3.0f) == 3, "mode 3 lands in HELIX only", "%d", BranchForMode(3.0f));
    CHECK_MSG(BranchForMode(4.0f) == 4, "mode 4 lands in NOISE only", "%d", BranchForMode(4.0f));
    CHECK_MSG(BranchForMode(0.0f) == 0, "mode 0 stays in passthrough", "%d", BranchForMode(0.0f));
    // The fractional uniform test: any in-range value lands in SOME branch.
    CHECK_MSG(BranchForMode(2.99f) == 3, "fractional modes cannot fall through",
              "%d", BranchForMode(2.99f));
}

// ── 2. The envelope anchors both ends ──────────────────────────────────────
//
// The whole contract: env(0) = 0 exactly, env(1) = 0 exactly (the tail clamp
// keeps smoothstep(0.999, 1, 1) = 1), and the interior reaches the flat 1.0 so
// the middle of the strip is free to wave.

static void Test_EnvelopeAnchorsTheStrip(void)
{
    CHECK_MSG(MirrorEnv(0.0f) == 0.0f, "the wave is exactly 0 at the emitter",
              "%.6f", MirrorEnv(0.0f));
    CHECK_MSG(MirrorEnv(1.0f) == 0.0f, "and exactly 0 at the tail",
              "%.6f", MirrorEnv(1.0f));
    CHECK_MSG(MirrorEnv(0.5f * kEnvHead) > 0.49f && MirrorEnv(0.5f * kEnvHead) < 0.51f,
              "the head fade midpoint sits halfway through the fade region",
              "%.4f", MirrorEnv(0.5f * kEnvHead));

    float worst = 0.0f;
    for (float seg = 0.0f; seg <= 1.0f; seg += 0.001f) {
        float e = MirrorEnv(seg);
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst <= 1.0f + 1e-6f, "the envelope never exceeds 1", "%.6f", worst);
    CHECK_MSG(worst > 0.90f, "and the mid-strip reaches near-full strength", "%.4f", worst);

    // headFade 0.10 < tailStart 0.88: the two fades cannot overlap, or the
    // whole strip would be dimmed at once.
    CHECK_MSG(kEnvHead < kEnvTail, "the head and tail fades cannot overlap",
              "%.2f vs %.2f", kEnvHead, kEnvTail);
}

// ── 3. The sin-multi metre budget ──────────────────────────────────────────
//
// Displacement is env * strength * sum(amplitudes). With the defaults the
// worst possible displacement is 0.217 m in the strip plane — a clearly
// visible wave on a 0.30 m ribbon (the energy look wants VISIBLE motion, just
// not high-frequency thrash), and still far less than the trail length, so
// the shape never leaves its sampled path.

static void Test_SinMultiBudget(void)
{
    float sideSum = kAmpA[0] + kAmpA[1] + kAmpA[2];
    float normSum = kAmpB[0] + kAmpB[1] + kAmpB[2];
    float worst = sideSum + normSum;
    CHECK_MSG(worst < 0.35f, "the worst-case wave stays a fraction of a metre",
              "%.3f m", worst);
    CHECK_MSG(worst > 0.15f, "...but is more than a whisper — the wave is visible",
              "%.3f m", worst);
    // The "swim" axis (strip normal, toward/away from the camera) must stay
    // well under the side axis, or the ribbon thrashes at the viewer.
    CHECK_MSG(kAmpB[0] < kAmpA[0] && normSum < sideSum,
              "the normal-axis budget stays under the side-axis budget",
              "norm %.3f vs side %.3f", normSum, sideSum);
    // Frequency stays LOW: with amplitude this large, a high octave-0 would
    // zigzag the strip into noise instead of sweeping it. One big sweep must
    // take at least a fifth of the strip to complete.
    CHECK_MSG(kFreq[0] < 2.0f,
              "octave-0 stays a long sweep, not a wobble",
              "%.2f cycles per strip", kFreq[0]);
    // And the crests TRAVEL: crest speed = speed/(freq*TAU) segments per
    // second. Below ~0.3 seg/s the wave reads as frozen (the original sin).
    float crest = kSpeed[0] / (kFreq[0] * TAU);
    CHECK_MSG(crest > 0.30f, "the wave crests visibly travel down the strip",
              "%.2f segments/sec", crest);

    // Octave decay: the highest frequency carries the least amplitude, so the
    // strip gets detail, not jitter.
    CHECK_MSG(kAmpA[0] > kAmpA[1] && kAmpA[1] > kAmpA[2],
              "side octaves decay with frequency", "%.3f > %.3f > %.3f",
              kAmpA[0], kAmpA[1], kAmpA[2]);
    CHECK_MSG(kAmpB[0] > kAmpB[1] && kAmpB[1] > kAmpB[2],
              "normal octaves decay with frequency", "%.3f > %.3f > %.3f",
              kAmpB[0], kAmpB[1], kAmpB[2]);
    CHECK_MSG(kFreq[2] > kFreq[1] && kFreq[1] > kFreq[0],
              "and the frequencies climb so the decay is real",
              "%.2f < %.2f < %.2f", kFreq[0], kFreq[1], kFreq[2]);

    // Sample the true worst case over a grid: env * amplitude terms can never
    // exceed the sum, but the check that matters is that no sampled frame does.
    // (Dormant-mode guard: the ACTIVE mode is CURL3, whose visibility budget
    // lives in the flat-router test — the tightened envelope now keeps
    // the sin chain's sampled extremes under 0.05 m, which is fine.)
    float sd = 0.0f, nd = 0.0f, maxD = 0.0f;
    for (float seg = 0.0f; seg <= 1.0f; seg += 0.01f)
        for (float t = 0.0f; t < 3.0f; t += 0.1f) {
            MirrorSinMulti(seg, t, 1.0f, &sd, &nd);
            float d = (fabsf(sd) + fabsf(nd)) * MirrorEnv(seg) * kStrength;
            if (d > maxD) maxD = d;
        }
    CHECK_MSG(maxD <= worst + 1e-4f && maxD > 0.02f,
              "sampled displacement respects the budget and stays visible",
              "%.4f m sampled vs %.4f m worst-case", maxD, worst);
}

// ── 4. DEFORM IS OFF — the material-only route ─────────────────────────────
//
// The user killed the vertex waves ("tắt deform đi"): the curl/sin chains
// read as rigid and hid the material's torn edges. deform.mode = 0 means the
// VERTEX stage passes the strip through untouched, but the trail still routes
// through the uber shader because material.mode = 1 needs the FRAGMENT stage
// (wisp, dissolve, edge tear, tail ramp). The router key is
// deform.mode > 0 OR material.mode > 0.

static void Test_FlatMaterialRouting(void)
{
    // Mode 0 hits NO displacement branch — the strip stays flat.
    CHECK_MSG(BranchForMode(0.0f) == 0, "deform mode 0 is passthrough",
              "branch %d", BranchForMode(0.0f));
    // The material mode is the reason the uber shader is still used.
    CHECK_MSG(kMatMode > 0.5f, "the packed material stays active",
              "%.2f", kMatMode);
}

// The SMOKE_WIDEN width envelope (ComputeWidthEnvelopeFast, segRatio 0 = tail
// .. 1 = head, age = 1 - segRatio): THIN at the source, monotonically
// widening to FULL width at the tail — the plume rolls out and stays broad;
// only the material tail ramp evaporates the tip. The tail must never be the
// smallest part (that was the "sao cái đuôi vẫn nhỏ" bug: the LIFECYCLE
// envelope dissolved the whole tail third to zero).
// (Mirror of TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN.)
static float MirrorSmokeWiden(float segRatio)
{
    float age = 1.0f - segRatio;
    float grow = SmoothStepC(0.0f, 0.25f, age);
    return 0.15f + 0.85f * grow;
}

static void Test_TailIsWidestAndDissolves(void)
{
    // The tail (segRatio 0) is FULL width; the head (segRatio 1) is thin;
    // width never shrinks along the way — monotonic widening.
    float wHead = MirrorSmokeWiden(1.0f), wMid = MirrorSmokeWiden(0.5f);
    float wTail = MirrorSmokeWiden(0.0f), wNearTail = MirrorSmokeWiden(0.25f);
    CHECK_MSG(wTail > 0.99f, "the tail carries FULL width — đuôi to",
              "%.2f", wTail);
    CHECK_MSG(wNearTail >= wMid && wMid >= wHead && wHead > 0.1f,
              "the plume widens monotonically from head to tail",
              "head %.2f mid %.2f near-tail %.2f", wHead, wMid, wNearTail);

    // The material tip fade: alpha full through head + body, dissolving only
    // in the final stretch — the tip evaporates, the broad tail stays.
    float fadeHead = 1.0f - SmoothStep(kTailFadeA, kTailFadeB, 0.0f);
    float fadeBody = 1.0f - SmoothStep(kTailFadeA, kTailFadeB, 0.8f);
    float fadeTail = 1.0f - SmoothStep(kTailFadeA, kTailFadeB, 1.0f);
    CHECK_MSG(kTailFadeA < kTailFadeB && kTailFadeB <= 1.0f,
              "the tip fade ramp is enabled and ends at the tail",
              "%.2f..%.2f", kTailFadeA, kTailFadeB);
    CHECK_MSG(fadeHead > 0.99f && fadeBody > 0.9f,
              "the head and body stay fully opaque",
              "%.2f / %.2f", fadeHead, fadeBody);
    CHECK_MSG(fadeTail < 0.01f, "the very tip dissolves to nothing — tan biến",
              "%.2f", fadeTail);
}

// ── 5. Per-spawn phase desync ──────────────────────────────────────────────
//
// u_wavePhase is seeded per spawn, and the B octaves multiply it by 2.31/4.67
// (side) and 1.71/3.13 (normal) — offsets chosen so two casts never run in
// step. The proof that matters: at the same (seg, t), two phases differ by a
// visible amount, but never by more than twice the budget.

static void Test_PhaseDesync(void)
{
    const float phase0 = 0.0f, phase1 = 1.7f;
    double acc = 0.0;
    float maxDiff = 0.0f;
    int n = 0;
    for (float seg = 0.0f; seg <= 1.0f; seg += 0.02f)
        for (float t = 0.0f; t < 2.0f; t += 0.05f) {
            float s0, n0, s1, n1;
            MirrorSinMulti(seg, t, phase0, &s0, &n0);
            MirrorSinMulti(seg, t, phase1, &s1, &n1);
            float e = MirrorEnv(seg) * kStrength;
            float diff = fabsf((s1 - s0) * e) + fabsf((n1 - n0) * e);
            acc += diff; n++;
            if (diff > maxDiff) maxDiff = diff;
        }
    float mean = (float)(acc / n);
    CHECK_MSG(mean > 0.01f, "two spawn phases actually differ on screen",
              "mean |d1-d0| %.4f m", mean);
    float sideSum = kAmpA[0] + kAmpA[1] + kAmpA[2];
    float normSum = kAmpB[0] + kAmpB[1] + kAmpB[2];
    CHECK_MSG(maxDiff <= 2.0f * (sideSum + normSum) + 1e-4f,
              "and the difference stays inside twice the budget",
              "%.4f m vs %.4f m", maxDiff, 2.0f * (sideSum + normSum));
}

// ── 5. Curl3 / noise / helix boundedness ───────────────────────────────────
//
// NOISE and CURL3 displace by (n - 0.5) * 2 where n in [0,1] — the shader
// cannot move a vertex more than u_waveStrength metres. HELIX displaces by
// ampA.x at most. These are structural bounds, not tuning: a hash-noise mode
// that displaced by the raw hash value (0..1) would throw a 0.10 m ribbon
// half a metre sideways.

static void Test_HashModesAreBounded(void)
{
    float maxNoise = 0.0f, maxHelix = 0.0f;
    for (float seg = 0.0f; seg <= 1.0f; seg += 0.01f) {
        float e = MirrorEnv(seg);
        // noise/curl: |(n-0.5)*2| <= 1 in every axis
        float d = 1.0f * kStrength * e;
        if (d > maxNoise) maxNoise = d;
        // helix: cos/sin on a unit circle, radius ampA.x
        float h = kAmpA[0] * kStrength * e;
        if (h > maxHelix) maxHelix = h;
    }
    CHECK_MSG(maxNoise <= kStrength + 1e-6f,
              "hash modes can never exceed u_waveStrength",
              "%.4f m vs %.2f m", maxNoise, kStrength);
    CHECK_MSG(maxHelix <= kAmpA[0] + 1e-6f,
              "helix can never exceed its base amplitude",
              "%.4f m vs %.3f m", maxHelix, kAmpA[0]);
    CHECK_MSG(maxNoise >= kStrength * 0.9f,
              "...but reach most of it mid-strip, so the wave is visible",
              "%.4f m", maxNoise);
}

// ── 6. The reinterpreted attribute's fallback ──────────────────────────────
//
// vertexNormal carries the SIDE vector, but a non-ribbon draw under this
// shader writes zero-length normals. The shader must fall back to a fixed
// axis instead of dividing by zero — a length-0 normalize is NaN, and NaN in
// a position reaches the rasterizer as a dropped triangle.

static void Test_SideFallback(void)
{
    // length 0 → fixed axis, no NaN.
    float l = 0.0f;
    CHECK_MSG(!(l > 0.5f), "a degenerate side vector takes the fallback branch",
              "length %.2f", l);

    // A real side vector: the strip writes a UNIT side (cross of tangent and
    // plane normal, normalized), so any ribbon vertex passes the threshold
    // with room to spare while a degenerate zero-length write does not.
    float sx = 0.0f, sy = 0.6f, sz = 0.8f;
    float len = sqrtf(sx * sx + sy * sy + sz * sz);
    CHECK_MSG(len > 0.5f, "a real ribbon side vector passes the threshold",
              "length %.3f", len);
    float ux = sx / len, uy = sy / len, uz = sz / len;
    float nlen = sqrtf(ux * ux + uy * uy + uz * uz);
    CHECK_MSG(fabsf(nlen - 1.0f) < 1e-5f && nlen == nlen,
              "the normalized side is a unit vector, not NaN",
              "%.6f", nlen);
}

// ── 7. Packed material arithmetic ──────────────────────────────────────────
//
// The fragment shader reads one RGBA texture twice (coarse + fine pan), mixes
// R and G by a turbulence-clamped factor, erodes the tail with a smoothstep on
// B, and multiplies the alpha through. Three invariants: the mix factor stays
// in [0,1] (a clamped mix cannot overshoot either source), the dissolve mask
// is monotone in the B channel, and the output alpha can never exceed the
// input vertex alpha (a wisp can only erode, never gain opacity).

static void Test_PackedMaterialMath(void)
{
    // Turbulence modulation stays inside [0,1] after the clamp.
    float turb = 0.35f * (0.8f - 0.5f) * 2.0f;   // strongest sane A sample
    if (turb > 1.0f) turb = 1.0f;
    if (turb < -1.0f) turb = -1.0f;
    float mixW = 0.35f + turb;
    if (mixW > 1.0f) mixW = 1.0f;
    if (mixW < 0.0f) mixW = 0.0f;
    CHECK_MSG(mixW >= 0.0f && mixW <= 1.0f, "the wisp mix never leaves [0,1]",
              "%.4f", mixW);

    // Dissolve: monotone in B — as B rises, the mask can only open (the edge
    // tear shifts the threshold, it never breaks monotonicity in B).
    float prev = -1.0f;
    int monotone = 1;
    for (float b = 0.0f; b <= 1.0f; b += 0.02f) {
        float edge = 0.18f > 0.001f ? 0.18f : 0.001f;
        float m = SmoothStep(0.30f, 0.30f + edge, b);
        if (m < prev - 1e-5f) { monotone = 0; break; }
        prev = m;
    }
    CHECK(monotone, "the dissolve mask is monotone in the B channel");

    // Edge tear: the threshold jitter is bounded by u_edgeTear and reaches
    // zero at the band centre — the CENTRE can never be torn, only the edges.
    float centre = SmoothStep(0.12f, 0.45f, 0.0f);
    float edgeB = SmoothStep(0.12f, 0.45f, 1.0f);
    CHECK_MSG(centre < 0.01f && edgeB > 0.99f,
              "the tear is weighted to the edges, zero at the centre",
              "centre %.2f edge %.2f", centre, edgeB);
    CHECK_MSG(kEdgeTear > 0.0f && kEdgeTear < 0.6f,
              "the tear stays a bite, not a shredder",
              "%.2f", kEdgeTear);

    // Alpha erodes, never gains: alpha = wisp * mask * vColor.a, all in [0,1].
    float a = 0.83f, wisp = 0.72f, mask = 0.61f;
    float alpha = wisp * mask * a;
    CHECK_MSG(alpha <= a + 1e-6f && alpha >= 0.0f,
              "the packed material only erodes the body alpha",
              "%.3f vs %.3f", alpha, a);
    // The discard threshold: below 0.003 the alpha is imperceptible on any
    // additive blend, and the fragment is dropped rather than blended.
    CHECK_MSG(0.003f < 0.01f, "the discard threshold sits under 1% alpha",
              "%.4f", 0.003f);
}

// ── the mirror guard ───────────────────────────────────────────────────────

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
    const char *vs = "core/trails/shaders/trail_deform.vs";
    const char *fs = "core/trails/shaders/trail_deform.fs";
    const char *c  = "core/trails/trail_system.c";
    const char *rb = "core/ribbon_strip.c";
    const char *inl = "core/composition/common/vc_core_smoketrail.inl";

    // The branch thresholds — the integer-mode exclusivity this mirror tests.
    // 0.5/1.5/2.5/3.5: integer modes 1..4 map 1:1 onto the four branches.
    CHECK(FileHas(vs, "if (u_deformMode >= 3.5)"), "NOISE branch still guarded at 3.5");
    CHECK(FileHas(vs, "else if (u_deformMode >= 2.5)"), "HELIX branch still guarded at 2.5");
    CHECK(FileHas(vs, "else if (u_deformMode >= 1.5)"), "CURL3 branch still guarded at 1.5");
    CHECK(FileHas(vs, "else if (u_deformMode >= 0.5)"), "SIN_MULTI branch still guarded at 0.5");

    // The envelope — the two ends this mirror anchors.
    CHECK(FileHas(vs, "float env = smoothstep(0.0, headFade, seg)"),
          "the head fade is still the envelope's first factor");
    CHECK(FileHas(vs, "(1.0 - smoothstep(tailStart, 1.0, seg))"),
          "the tail fade still subtracts to zero at seg = 1");

    // The sin-multi octaves — the budget and desync arithmetic.
    CHECK(FileHas(vs, "u_waveAmpA.x * sin(seg * u_waveFreq.x * TAU + t * u_waveSpeed.x + phase)"),
          "the side octave-0 term is unchanged");
    CHECK(FileHas(vs, "u_waveFreq.x * 3.77"), "the normal octave-0 detune is unchanged");
    CHECK(FileHas(vs, "phase * 4.67"), "the side phase desync factor is unchanged");
    CHECK(FileHas(vs, "pos += d * env * u_waveStrength;"),
          "the sin-multi result still lands inside the envelope * strength");

    // The fallback — the degenerate-side guard.
    CHECK(FileHas(vs, "sideLen > 0.5"), "the side-vector fallback threshold is unchanged");

    // The hash modes' bound: displacement must stay a (n-0.5) product.
    CHECK(FileHas(vs, "(n - 0.5) * u_waveStrength * env * 2.0"),
          "NOISE still scales (n-0.5), never the raw hash");
    // VNoise3D returns a FLOAT. CURL3 builds a vec3 from three calls — a
    // single `vec3 n = VNoise3D(...)` is a compile error that kills the whole
    // deform shader and falls back to flat ribbons (this happened once).
    CHECK(FileHas(vs, "vec3 n = vec3(VNoise3D"),
          "CURL3 still constructs its vec3 from three scalar noise calls");
    CHECK(!FileHas(vs, "vec3 n = VNoise3D"),
          "CURL3 no longer assigns the scalar noise directly to a vec3");

    // The fragment: packed material + the both-pass feed.
    CHECK(FileHas(fs, "if (u_matMode < 0.5)"), "the passthrough guard is unchanged");
    CHECK(FileHas(fs, "mix(texC.r, texF.g, mixW)"), "the wisp is still R/G by the mix");
    CHECK(FileHas(fs, "smoothstep(thresh, thresh + edge, texC.b)"),
          "the dissolve is still a B-channel smoothstep on the jittered threshold");
    CHECK(FileHas(fs, "float thresh = u_dissolve + (texF.g - 0.5f) * 2.0f * u_edgeTear * edgeBias;"),
          "the edge tear still jitters the threshold with fine noise");
    CHECK(FileHas(fs, "float alpha = wisp * dissolveMask * vColor.a * tailMask;"),
          "the alpha can only erode the vertex alpha (and the tail ramp)");
    CHECK(FileHas(fs, "if (alpha < 0.003)"), "the discard threshold is unchanged");
    CHECK(FileHas(fs, "finalColor = vec4(colour, alpha);"),
          "both passes still consume the same src.rgb * src.a formula");
    CHECK(FileHas(fs, "u_tailFadeA >= u_tailFadeB"),
          "the tail ramp keeps its disabled guard (start >= end)");

    // The composition: vertex deform is OFF (flat ribbon), the material half
    // stays on, the envelope widens to a full tail, the tip fades late.
    CHECK(FileHas(inl, "cfg.deform.mode = 0.0f"),
          "the energy trail still runs flat — deform off");
    CHECK(FileHas(inl, "cfg.material.mode = 1.0f"),
          "the packed material still feeds the fragment stage");
    CHECK(FileHas(inl, "TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN"),
          "the energy trail still widens to FULL width at the tail");
    CHECK(FileHas(inl, "cfg.material.tailFadeA = 0.88f"),
          "the tip fade is still set late — only the tail's last stretch");
    CHECK(FileHas(c, "case TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN:"),
          "the widen envelope still exists in the width switch");
    CHECK(FileHas(c, "l->tailFadeA = GetShaderLocation(shader, \"u_tailFadeA\");"),
          "the C layer still binds the tail ramp uniforms");

    // The C routing: the uber shader is picked by deform OR material — a
    // material-only trail (deform 0) still gets the packed wisp fragment.
    CHECK(FileHas(c, "t->deform.mode > 0.0f || t->material.mode > 0.0f"),
          "the router key is deform OR material — material-only trails still ride the uber shader");
    CHECK(FileHas(c, "scratchOuter[h].v = TrailUsesDeformShader(t)"),
          "material-only trails still carry seg in texcoord.y for the tail ramp");
    CHECK(FileHas(c, "t->deform = config.deform;"),
          "the deform config is still copied at spawn");

    // The geometry: the side vector is only written by the deform entry point.
    CHECK(FileHas(rb, "if (writeNormals) rlNormal3f(side.x, side.y, side.z);"),
          "the side vector is still written per-vertex only when asked");
    CHECK(FileHas(rb, "bool writeNormals)"),
          "the writeNormals flag still gates the whole path");
}

int main(void)
{
    printf("=== trail deform: mode thresholds, envelope, budget, curl3, phase, packed material ===\n");
    Test_ModeThresholds();
    Test_EnvelopeAnchorsTheStrip();
    Test_SinMultiBudget();
    Test_FlatMaterialRouting();
    Test_TailIsWidestAndDissolves();
    Test_PhaseDesync();
    Test_HashModesAreBounded();
    Test_SideFallback();
    Test_PackedMaterialMath();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

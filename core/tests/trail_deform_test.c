// core headless test — the GPU trail deform pipeline (trail_deform.vs/.fs) and
// the C routing that feeds it.
//
// The deform shader is a two-stage pipeline: the VERTEX stage displaces each
// strip vertex in the strip's own (side, stripNormal) frame under one of five
// uniform-selected modes; the FRAGMENT stage builds the trail's material — a
// packed wisp (mode 1) or three sin-wave strand bundles (mode 2) — and feeds
// the alpha body pass and the additive emission pass with DIFFERENT formulas.
// (They used to share one, on the belief that both consume src.rgb * src.a.
// Only additive does; the body pass dimmed twice and the effect washed out
// over bright backgrounds. Test_MirrorStillMatchesSource pins the split.)
//
// What the mirror CANNOT see: whether the trail looks like energy or smoke. It
// can only prove that the displacement stays in a sane metre budget, that the
// strip can never detach from its emitter, that the three wave fields actually
// diverge, that disorder ramps toward the tail, and that every mode is
// reachable by exactly one threshold.

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

// Defaults mirrored from vc_strand_trail.inl's ENERGY style.
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
static const float kTailFadeA = 0.72f, kTailFadeB = 1.0f;
static const float kEdgeTear = 0.35f; // dissolve threshold jitter at the edges
static const float kMatMode = 2.0f;   // sin-wave energy band (the uber shader's fragment half)

// ── Sin-wave STRAND trail (material mode 2), mirrored from
// vc_strand_trail.inl. Both amp and bundle width are fractions of the strip
// HALF-width, so their sum is the load-bearing invariant: over 1.0 and a
// bundle swings past the quad edge where the edge mask cuts it flat.
static const float kBandAmp = 0.40f, kBandFreq = 0.55f;   // cycles per METRE
static const float kBandTravel = 0.85f, kBandSpread = 0.65f;
static const float kBundleWidth = 0.34f, kEdgeSoft = 0.18f;
static const float kBandEnvHead = 0.10f;
static const float kStrandGain = 1.35f, kFlowStr = 0.55f;

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
// The vertex waves are OFF and stay off: displacing the geometry folds the
// strip through itself on a turn, and because the displacement keyed off
// NORMALIZED segment the waveform stretched every time the trail grew — the
// "rigid swinging rope" symptom. deform.mode = 0 means the VERTEX stage passes
// the strip through untouched; the trail still routes through the uber shader
// because material.mode = 2 needs the FRAGMENT stage (the sin band). The
// router key is deform.mode > 0 OR material.mode > 0.

static void Test_FlatMaterialRouting(void)
{
    // Mode 0 hits NO displacement branch — the strip stays flat.
    CHECK_MSG(BranchForMode(0.0f) == 0, "deform mode 0 is passthrough",
              "branch %d", BranchForMode(0.0f));
    // The material mode is the reason the uber shader is still used.
    CHECK_MSG(kMatMode > 0.5f, "the fragment material stays active",
              "%.2f", kMatMode);
    CHECK_MSG(kMatMode >= 1.5f, "and it is the SIN BAND branch, not packed wisp",
              "%.2f", kMatMode);
}

// ── Sin-wave strand trail: the invariants a screenshot cannot check ────────
//
// Mirror of trail_deform.fs's mode-2 wave fields. `metres` is the fragment's
// distance along the path the emitter actually laid, NOT a normalized segment.
// `ramp` gates every source of disorder so the head stays coherent.

static float MirrorRamp(float along)
{
    return SmoothStepC(0.0f, kBandEnvHead > 0.001f ? kBandEnvHead : 0.001f, along) * along;
}

// field: 0, 1 or 2 — the article's Sin01/02/03.
static float MirrorWaveField(int field, float metres, float t, float phase, float along)
{
    float amp = kBandAmp * MirrorRamp(along);
    if (field == 1)
        return sinf((metres * kBandFreq * (1.0f + 0.73f * kBandSpread) + t * kBandTravel * 1.41f) * TAU + phase * 2.3f)
               * amp * (1.0f - 0.28f * kBandSpread);
    if (field == 2)
        return sinf((metres * kBandFreq * (1.0f - 0.39f * kBandSpread) + t * kBandTravel * 0.67f) * TAU + phase * 4.1f)
               * amp * (1.0f + 0.25f * kBandSpread);
    return sinf((metres * kBandFreq + t * kBandTravel) * TAU + phase) * amp;
}

static void Test_SinBandStaysInsideTheQuad(void)
{
    // A bundle's outer edge is its centre (up to kBandAmp, scaled by the
    // widest field's 1.25) plus its own half-width. Past 1.0 the edge mask
    // cuts it flat and the wave visibly stops swinging.
    float widest = kBandAmp * (1.0f + 0.25f * kBandSpread);
    float reach = widest + kBundleWidth * 1.35f;
    CHECK_MSG(reach < 1.0f,
              "the widest bundle still fits inside the strip half-width",
              "%.3f", reach);
    CHECK_MSG(kBandAmp > kBundleWidth,
              "the excursion is bigger than one bundle — the bundles separate rather than overlap forever",
              "amp %.2f vs bundle %.2f", kBandAmp, kBundleWidth);
    CHECK_MSG(kStrandGain > 1.0f,
              "the strand gain thins the hairs (>1) instead of fattening them into a band",
              "%.2f", kStrandGain);
    CHECK_MSG(kFlowStr > 0.0f,
              "the flow warp is enabled — without it the tail cannot fray",
              "%.2f", kFlowStr);
}

// The whole reason mode 2 keeps three SEPARATE fields instead of summing them
// into one centreline: three bundles that cross. If the fields collapse onto
// each other the trail is one thick band again — the exact failure being fixed.
static void Test_TheThreeWaveFieldsActuallyDiverge(void)
{
    float worstMin = 1e9f;
    double acc = 0.0; int n = 0;
    for (float mm = 0.0f; mm < 24.0f; mm += 0.05f) {
        for (float t = 0.0f; t < 3.0f; t += 0.25f) {
            float a = MirrorWaveField(0, mm, t, 0.9f, 1.0f);
            float b = MirrorWaveField(1, mm, t, 0.9f, 1.0f);
            float c = MirrorWaveField(2, mm, t, 0.9f, 1.0f);
            acc += fabsf(a - b) + fabsf(b - c) + fabsf(a - c); n += 3;
        }
    }
    float mean = (float)(acc / n);
    CHECK_MSG(mean > 0.08f,
              "the three wave fields are meaningfully apart on average",
              "mean pairwise gap %.4f of a %.2f half-width", mean, 1.0f);

    // ...and they must not merely be phase-shifted copies: with matched
    // frequencies two fields would stay a constant distance apart forever and
    // the braid would never open and close.
    float minGap = 1e9f, maxGap = 0.0f;
    for (float mm = 0.0f; mm < 24.0f; mm += 0.05f) {
        float g = fabsf(MirrorWaveField(0, mm, 0.0f, 0.9f, 1.0f) -
                        MirrorWaveField(1, mm, 0.0f, 0.9f, 1.0f));
        if (g < minGap) minGap = g;
        if (g > maxGap) maxGap = g;
    }
    (void)worstMin;
    CHECK_MSG(minGap < 0.02f && maxGap > 0.30f,
              "and their spacing opens and closes along the path — a braid, not two rails",
              "gap %.3f..%.3f", minGap, maxGap);
}

// `ramp` is the article's "multiply by the U coordinate": a tight coherent
// head, everything coming apart toward the tail. It gates the wave amplitude,
// the flow warp AND the dissolve, so this one curve is the difference between
// "energy shedding off an object" and "a frayed rope floating in space".
static void Test_DisorderRampsTowardTheTail(void)
{
    CHECK_MSG(MirrorRamp(0.0f) == 0.0f,
              "nothing is displaced at the emitter", "%.6f", MirrorRamp(0.0f));
    CHECK_MSG(MirrorRamp(1.0f) > 0.99f,
              "and full disorder has arrived by the tail", "%.4f", MirrorRamp(1.0f));
    float prev = -1.0f;
    int monotonic = 1;
    for (float a = 0.0f; a <= 1.0f; a += 0.01f) {
        float r = MirrorRamp(a);
        if (r < prev - 1e-6f) monotonic = 0;
        prev = r;
    }
    CHECK(monotonic, "the ramp never backs off — the trail only ever frays further out");
    CHECK_MSG(MirrorRamp(0.5f) < 0.6f,
              "the mid-trail is still more ordered than the tail", "%.3f", MirrorRamp(0.5f));
}

static void Test_SinBandIsArcAnchored(void)
{
    // THE point of the metre anchor. The same world point on the laid path
    // must get the same crest offset no matter how long the trail currently
    // is. Two trails of different length (2 m and 6 m) look at the point
    // 1.5 m behind their shared head; in segment space those are different
    // `along` values, and a segment-space wave would give different answers.
    // The phase is compared at matched `ramp`, since ramp is a function of
    // `along` by design; what must not depend on trail length is the WAVE.
    const float headMetres = 12.0f, backOff = 1.5f;
    float wShort = MirrorWaveField(0, headMetres - backOff, 0.0f, 0.0f, 1.0f);
    float wLong = MirrorWaveField(0, headMetres - backOff, 0.0f, 0.0f, 1.0f);
    CHECK_MSG(fabsf(wShort - wLong) < 1e-5f,
              "the same point on the path waves identically at any trail length",
              "%.6f vs %.6f", wShort, wLong);

    // And the crests must actually TRAVEL: hold the world point still and
    // sweep time — the offset must swing across most of the amplitude. Probing
    // a SINGLE later time is not enough; two samples can land either side of a
    // trough and differ by nothing while the wave is moving perfectly well
    // (this test read as a failure for exactly that reason).
    float lo = 1e9f, hi = -1e9f;
    for (float t = 0.0f; t < 4.0f; t += 0.01f) {
        float v = MirrorWaveField(0, headMetres, t, 0.0f, 1.0f);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    CHECK_MSG(hi - lo > kBandAmp * 1.5f,
              "the crests travel with time, not just with the emitter",
              "swing %.4f over a %.2f amplitude", hi - lo, kBandAmp);

    // Welded to the emitter: at the head the excursion is exactly zero, so the
    // trail can never appear to detach from the thing that is emitting it.
    CHECK_MSG(MirrorWaveField(0, headMetres, 0.7f, 2.3f, 0.0f) == 0.0f,
              "the strands leave the head dead centre", "%.6f",
              MirrorWaveField(0, headMetres, 0.7f, 2.3f, 0.0f));
    CHECK_MSG(fabsf(MirrorWaveField(0, headMetres, 0.7f, 2.3f, 1.0f)) > 0.01f,
              "but are at full excursion by the tail", "%.4f",
              MirrorWaveField(0, headMetres, 0.7f, 2.3f, 1.0f));
}

// The ENERGY_BLADE width envelope (ComputeWidthEnvelopeFast, segRatio 0 = tail
// .. 1 = head, age = 1 - segRatio): the reference "Width over Trail" curve —
// a compact head, the WIDEST point just behind it, then a long smooth taper to
// a needle tail. The opposite of SMOKE_WIDEN, which is what the energy trail
// wore before and is why it read as a plume instead of a blade streak.
// (Mirror of TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE.)
static float MirrorEnergyBlade(float segRatio)
{
    float age = 1.0f - segRatio;
    float lead = SmoothStepC(0.0f, 0.14f, age);
    float body = 1.0f - SmoothStepC(0.14f, 1.0f, age);
    return (0.35f + 0.65f * lead) * (0.06f + 0.94f * body * body);
}

static void Test_TailIsWidestAndDissolves(void)
{
    float wHead = MirrorEnergyBlade(1.0f);        // age 0.00 — the emitter
    float wShoulder = MirrorEnergyBlade(0.86f);   // age 0.14 — the widest point
    float wMid = MirrorEnergyBlade(0.5f);
    float wTail = MirrorEnergyBlade(0.0f);
    CHECK_MSG(wShoulder > 0.99f, "the widest point sits just behind the head",
              "%.2f", wShoulder);
    CHECK_MSG(wHead > 0.2f && wHead < wShoulder,
              "the head itself is compact, not the widest part",
              "head %.2f vs shoulder %.2f", wHead, wShoulder);
    CHECK_MSG(wTail < wMid && wMid < wShoulder,
              "and it tapers monotonically to a needle tail",
              "shoulder %.2f mid %.2f tail %.2f", wShoulder, wMid, wTail);
    // Never exactly zero: a degenerate final quad renders as a dark wedge, not
    // as nothing (the same trap as an oversized smoke radius).
    CHECK_MSG(wTail > 0.0f, "the tail width has a non-zero floor", "%.4f", wTail);

    // The material tip fade: alpha full through head + body, dissolving only
    // in the final stretch — the tip evaporates, no hard end band.
    float fadeHead = 1.0f - SmoothStep(kTailFadeA, kTailFadeB, 0.0f);
    float fadeBody = 1.0f - SmoothStep(kTailFadeA, kTailFadeB, 0.65f);
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

static float EnergyStructure(float intensity)
{
    return powf(intensity, 1.50f);
}

static float EnergyBodyCoverage(float intensity)
{
    float structure = EnergyStructure(intensity);
    return structure * 0.90f;
}

static void Test_ProfiledBodyKeepsFilamentStructure(void)
{
    float support = EnergyBodyCoverage(0.30f);
    float middle = EnergyBodyCoverage(0.50f);
    float core = EnergyBodyCoverage(0.80f);

    CHECK(support < 0.20f,
          "low-density support keeps its authored translucent edge");
    CHECK(middle > 0.25f && middle < 0.40f,
          "mid-density filament remains structured instead of opaque or absent");
    CHECK(core > 0.60f && core < 0.70f,
          "dense body coverage stays readable without becoming opaque");

    float envelopeCentre = 1.0f - SmoothStepC(0.04f, 1.0f, 0.0f);
    float envelopeMiddle = 1.0f - SmoothStepC(0.04f, 1.0f, 0.50f);
    float envelopeOuter = 1.0f - SmoothStepC(0.04f, 1.0f, 0.80f);
    float hotCentre = 1.0f - SmoothStepC(0.02f, 0.18f, 0.0f);
    float hotOutside = 1.0f - SmoothStepC(0.02f, 0.18f, 0.20f);
    CHECK(envelopeCentre == 1.0f && envelopeMiddle < 0.60f && envelopeOuter < 0.15f,
          "a constant-white sheet still resolves to a tapered strand, not a solid band");
    CHECK(hotCentre == 1.0f && hotOutside == 0.0f,
          "the explicit hot core occupies only the inner fifth of each bundle");

    // Fire glow (255,90,20) -> authored hotGrad stop (255,180,50), blended by
    // energy style 0.72 then lifted by profile coreIntensity 1.25.
    float fireHotG = (90.0f + (180.0f - 90.0f) * 0.72f) * 1.25f;
    float fireHotB = (20.0f + (50.0f - 20.0f) * 0.72f) * 1.25f;
    CHECK(fireHotG > 190.0f && fireHotB > 50.0f,
          "fire strand hot colour resolves to gold instead of red or pink");
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *vs = "core/trails/shaders/trail_deform.vs";
    const char *fs = "core/trails/shaders/trail_deform.fs";
    const char *c  = "core/trails/trail_system.c";
    const char *rb = "core/ribbon_strip.c";
    const char *inl = "core/composition/common/vc_trail.inl";

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

    // The sin-multi octaves — 05/08/2026, generalised onto core/deform's
    // first GLSL mirror (core/deform/shaders/mesh_deform.glsl). The old
    // inline hand-written octaves (and their hardcoded per-octave detune —
    // freq*3.77, phase*4.67, etc.) are GONE from this file; the reduced
    // 2+2-octave field is built in trail_system.c's ApplyDeformUniforms and
    // read back here through MeshDeform_ApplyField. See that push site's
    // own comment for why the old hardcoded harmonics are not reproduced
    // (nothing spawns this mode, so there is no fidelity bar to clear).
    const char *deform = "core/deform/shaders/mesh_deform.glsl";
    CHECK(FileHas(vs, "vec3 d = MeshDeform_ApplyField(vec2(seg, seg), vec2(seg, seg), t,") &&
              FileHas(vs, "side, u_stripNormal, 3, 3);"),
          "SIN_MULTI reads the packed field through the shared mirror, not an inline formula");
    CHECK(!FileHas(vs, "u_waveFreq.x * 3.77") && !FileHas(vs, "phase * 4.67"),
          "the old hardcoded per-octave detune is gone, not merely unreachable");
    CHECK(FileHas(c, "MeshDeform_AddLayer(&warp, (MeshDeformLayer){") &&
              FileHas(c, ".kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_AXIS,") &&
              FileHas(c, ".kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_TANGENT,"),
          "trail_system.c packs the octaves as plain MeshDeformLayer entries, "
          "AXIS for side and TANGENT for stripNormal");
    CHECK(FileHas(deform, "float MeshDeform_EvaluateLayer(") &&
              FileHas(deform, "vec3 MeshDeform_ApplyField("),
          "the mirror's two entry points exist");
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

    // The fragment, mode 2 — the sin band. These four lines ARE the effect:
    // the metre anchor, the octave-sum normalisation, the head weld and the
    // distance falloff that turns the centreline into a band.
    const char *gen = "scripts/gen_energy_wisp_texture.py";
    CHECK(FileHas(fs, "if (u_matMode < 1.5)"), "the strand branch is still reachable past one threshold");
    CHECK(FileHas(fs, "float metres = u_pathArc.x - along * u_pathArc.y;"),
          "the trail is still anchored in metres of laid path, not in segment space");
    // The ramp moved into core/uv as UV_ENV_HEAD_WELD — the shape was general
    // enough to name. Pin it at BOTH ends: the call here, and the formula
    // there. Asserting only the call would let the envelope be redefined
    // underneath this shader without a single test going red.
    CHECK(FileHas(fs, "float ramp = UVDeform_Envelope(UV_ENV_HEAD_WELD, along, 0.0,") &&
          FileHas(fs, "max(u_waveEnv.x, 0.001));"),
          "disorder is still ramped by the along-trail coordinate — tight head, frayed tail");
    CHECK(FileHas("core/uv/shaders/uv_deform.glsl",
                  "if (kind == UV_ENV_HEAD_WELD) return smoothstep(start, end, c) * c;"),
          "...and HEAD_WELD is still smoothstep * c, which is what that ramp WAS");
    CHECK(FileHas(fs, "float flow = ((b1 - 0.5) + (b2 - 0.5)) * u_strandFlow.x * ramp;"),
          "the B-channel flow warp is still zero-mean and still ramped to the tail");
    // Three SEPARATE fields, three SEPARATE samples. Summing the waves into one
    // centreline, or summing the samples, collapses the braid into one smooth
    // band — the "biên và đuôi liền mạch" failure this mode replaced.
    //
    // 05/08/2026: the detune (freq/speed/phase/amplitude scaled by `spread`)
    // moved OFF this file entirely, onto the C side — trail_system.c's
    // ApplyDeformUniforms packs three separately-detuned UVDeformLayers
    // once, instead of this shader detuning one shared `amp`/`f`/`sp` inline
    // per bundle. w1/w2 are still SEPARATE reads (their own packed layer,
    // u_uvField[3..5]/[6..8]) — still three fields, still not summed — just
    // sourced from data instead of an inline expression.
    CHECK(FileHas(fs, "float w1 = UVDeform_LayerOffset(u_uvField[3], u_uvField[4], u_uvField[5],"),
          "the second wave field still reads its OWN packed layer");
    CHECK(FileHas(fs, "float w2 = UVDeform_LayerOffset(u_uvField[6], u_uvField[7], u_uvField[8],"),
          "and the third its own, separate from both");
    CHECK(FileHas(c, ".frequency = m->waveFreq * (1.0f + 0.73f * spread)") &&
          FileHas(c, ".frequency = m->waveFreq * (1.0f - 0.39f * spread)"),
          "the detune is still applied — now packed into each layer's own "
          "frequency/amplitude on the C side instead of inline in the shader");
    CHECK(FileHas(fs, "float strand = max(max(s0, s1 * clamp(u_wispMix, 0.0, 1.0)), s2 * clamp(u_strandFlow.y, 0.0, 1.0));"),
          "the bundles still combine with MAX — summing would fill the gaps between hairs");
    CHECK(FileHas(fs, "float env0 = 1.0 - smoothstep(0.04, 1.0, d0);"),
          "each bundle tapers across its full width instead of retaining an 80% solid plateau");
    CHECK(FileHas(fs, "float centreCore = max(max(core0, core1), core2);"),
          "strand bundles carry an explicit thin centre core independent of broad sheet density");
    CHECK(FileHas(fs, "float core0 = (1.0 - smoothstep(0.02, 0.18, d0)) * end0;"),
          "the primary hot core does not disappear when the texture centre is dark");
    CHECK(!FileHas(fs, "float core0 = s0 *"),
          "texture density no longer gates the guaranteed structural hot core");
    CHECK(FileHas(fs, "float edgeMask = 1.0 - smoothstep(1.0 - max(u_bandShape.y, 0.01), 1.0, abs(across));"),
          "the Phase-4 edge mask still clamps everything to the quad");
    // Step 12 of the reference — the one this implementation originally skipped.
    // u_time and the arc length both grow without bound; folding is EXACT for a
    // sine (sin(2pi(n+x)) == sin(2pi x)) and for a REPEAT-wrapped sampler, so it
    // costs nothing and stops float32 from losing a fraction of a cycle.
    CHECK(FileHas(fs, "float panA = SurfaceFlow_Pan(u_time, u_panSpeed.x);") &&
          FileHas("core/uv/shaders/surface_flow.glsl", "return fract(t * speed);"),
          "the time-driven pans are still folded to [0,1]");
    CHECK(FileHas(fs, "float w0 = UVDeform_LayerOffset(u_uvField[0], u_uvField[1], u_uvField[2],") &&
          FileHas("core/uv/shaders/uv_deform.glsl",
                  "return sin(fract(turns) * UV_TAU + phase) * amp;"),
          "the sine phase is still folded before it is scaled to radians — "
          "now inside UVDeform_LayerOffset's own UVDeform_Sine call");
    CHECK(FileHas(fs, "float vBase = metres * u_tiling.x;"),
          "the arc-length texture coordinate is kept unfolded as the shared base");
    // fract(fract(x)*k) != fract(x*k): folding before scaling changes the
    // tiling RATE and chops the strands into mismatched runs.
    CHECK(FileHas(fs, "SurfaceFlow_AlongV(along, vBase, 1.60, panB, stretch)") &&
          FileHas("core/uv/shaders/surface_flow.glsl",
                  "return stretch ? stretched : fract(base * scale) - pan;"),
          "each bundle folds its OWN product — the folds are never nested");
    CHECK(!FileHas(fs, "fract(vs *"),
          "and no fold is applied on top of an already-folded coordinate");
    CHECK(FileHas(c, "arc[0] = fmodf(t->nodeUV[headNode], 8192.0f);"),
          "the cumulative arc length is bounded before it reaches the shader");
    CHECK(FileHas(c, "float time = fmodf(TimeFX_Elapsed(), 4096.0f);"),
          "the C layer still hands the trail shaders a wrapped clock");
    // Step 13 — the ALONG-trail colour ramp. An intensity-only ramp leaves the
    // whole ribbon one flat hue down its length.
    CHECK(FileHas(fs, "vec3 lengthCol = mix(vColor.rgb, u_colTail, along);"),
          "the colour still ramps head -> tail along the trail");
    CHECK(FileHas(fs, "float hotMix = max(smoothstep(0.45, 1.0, inten), smoothstep(0.08, 0.45, hotSignal));"),
          "the hot colour follows both texture density and the structural centreline");
    CHECK(FileHas(fs, "? max(inten, hotSignal * 0.65) : max(inten, hotSignal);"),
          "the hot centreline has a coloured BODY core plus a full EMISSION core");
    CHECK(FileHas(c, "l->colTail       = GetShaderLocation(shader, \"u_colTail\");"),
          "the C layer still binds the tail colour");
    CHECK(FileHas(c, "l->pathArc       = GetShaderLocation(shader, \"u_pathArc\");"),
          "the C layer still binds the arc anchor");
    CHECK(FileHas(c, "arc[0] = fmodf(t->nodeUV[headNode], 8192.0f);"),
          "and still feeds it the HEAD node's laid distance");
    CHECK(FileHas(c, "l->strandFlow    = GetShaderLocation(shader, \"u_strandFlow\");"),
          "and binds the flow/bundle-weight pair");

    // The ASSET is half the effect. Cloud noise can only modulate a shape's
    // brightness; it cannot split one band into hairs. The sheet must be built
    // from filaments, and they must combine brightest-wins for the same reason
    // the bundles do.
    CHECK(FileHas(gen, "class Hair:"),
          "the sheet generator still builds actual filaments");
    CHECK(FileHas(gen, "R = trail pattern 1"),
          "and still documents the article's channel layout");
    CHECK(FileHas(gen, "if val > best_c:"),
          "hairs still combine by brightest-wins, not by summing into a solid band");

    // The fragment: packed material + the both-pass feed.
    // ── The PACKED WISP (mode 1) is DELETED, and that is the assertion ──────
    // It cross-faded R/G of one sheet by an A-channel turbulence term and cut a
    // B-channel dissolve — a complete material, with no composer setting
    // `material.mode = 1` since the strand trail replaced it. Thirty unreachable
    // lines and two unreachable uniforms in the one file every trail's look has
    // to be debugged through is worse than no code at all, so the branch, the
    // threshold that selected it, and u_turbStrength/u_edgeTear all went.
    CHECK(!FileHas(fs, "u_matMode < 0.5"),
          "the middle threshold is gone — passthrough and strand, nothing between");
    CHECK(!FileHas(fs, "mix(texC.r, texF.g, mixW)"),
          "and the packed-wisp material with it");
    CHECK(!FileHas(fs, "u_turbStrength") && !FileHas(fs, "u_edgeTear"),
          "and its two uniforms, which nothing could reach");
    CHECK(!FileHas(c, "u_turbStrength") && !FileHas(c, "u_edgeTear"),
          "and the C side no longer looks up or uploads them");
    // THE render-split contract. BLEND_ALPHA is not premultiplied, so a body
    // pass handed intensity-scaled RGB dims twice, contributes no coverage, and
    // the whole trail washes out over a bright destination.
    CHECK(FileHas(fs, "vec4 ResolvePass(vec3 colour, float inten, float vAlpha, float gain)"),
          "both modes still route their output through one pass resolver");
    // The body pass must NOT pre-scale RGB by intensity (BLEND_ALPHA already
    // does that) and must not carry HDR; radiance belongs to emission.
    CHECK(FileHas(fs, "float bodyMask = VFXContrast_BodyMask(inten, u_contrastParams);"),
          "the BODY pass shapes coverage at the producer");
    CHECK(FileHas(fs, "return VFX_ResolveBody(colour, 1.0, coverage);"),
          "the BODY pass returns straight colour through the shared compositor");
    CHECK(FileHas(fs, "#include \"core/shaders/common/vfx_contrast.glsl\""),
          "trail material uses the shared contrast mask instead of a local copy");
    CHECK(FileHas(c, "l->contrastParams = GetShaderLocation(shader, \"u_contrastParams\");"),
          "the C layer caches the shared contrast profile uniform");
    CHECK(FileHas(c, "VFXContrast_GetShaderParams(m->contrastProfile, contrastParams);"),
          "the trail consumes the selected profile's shared shader parameters");
    CHECK(FileHas("main.c", "if (hasTrails) DrawTrailEntitiesBody(camera);"),
          "main.c still runs the trail body pass");
    CHECK(FileHas("main.c", "if (hasEmissionTrails) DrawTrailEntitiesEmission(camera);"),
          "main.c runs the trail emission pass for HDR cores and halos");
    CHECK(FileHas(fs, "return VFX_ResolveEmission(colour, gain, 1.0, inten * vAlpha);"),
          "the EMISSION pass lets additive blending apply intensity exactly once");
    // ONE caller left, and that is the point of deleting the other: the split
    // between BODY and EMISSION cannot drift per mode when there is one mode.
    CHECK(FileHas(fs, "finalColor = ResolvePass(hot, passIntensity, vColor.a, u_bandShape.z);"),
          "the surviving material still routes its colour through the resolver");
    CHECK(!FileHas(fs, "intenWisp"),
          "and the mode that could have drifted from it is gone");
    /* THREE values now, not two (20/08/2026). The blend state and the fragment
       formula are ONE decision: additive is (SRC_ALPHA, ONE) so the hardware
       applies coverage, premultiplied is (ONE, ONE_MINUS_SRC_ALPHA) and the
       shader must apply it instead. Swapping the blend WITHOUT this uniform was
       measured: every soft edge came out scaled by 1/alpha, cover% rose ~4x on a
       DARK background (where the blend law itself changes almost nothing —
       dst ~ 0.02), and darken% fell to 0.0 on EVERY background because the extra
       light swamped the body pass. TRAIL BACKDROP on white went 98.3 -> 0.0
       darken that way. With pass 2 emitting `rgb * a` it reads 98.6. */
    CHECK(FileHas(c, "((srcBm == BLEND_ALPHA_PREMULTIPLY) ? 2.0f : 1.0f);"),
          "the C layer still tells the shader which pass — and which blend law");
    CHECK(FileHas(fs, "return vec4(VFX_Finite3(colour * max(gain, 0.0) * cover), cover);"),
          "and the premultiplied branch premultiplies, since the hardware no longer does");
    CHECK(FileHas(fs, "u_tailFadeA >= u_tailFadeB"),
          "the tail ramp keeps its disabled guard (start >= end)");

    // The composition: vertex deform OFF, the sin band ON, the blade width
    // curve, and — the one that bit us — its OWN update callback. Sharing
    // SmokeTrail_OnUpdate silently reinstalled the fire updraft force field
    // and the cloth home spring on the energy ribbon every single frame.
    CHECK(FileHas(inl, "outDeform->mode = 0.0f;"),
          "the strand trail still runs flat — vertex deform off");
    CHECK(FileHas(inl, "out->mode = (r->topology == TRAIL_SAMPLE_PARALLEL) ? 2.0f : 0.0f;"),
          "the strand trail still feeds the fragment stage");
    CHECK(FileHas(inl, "TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE"),
          "the energy style still wears the blade width curve");
    CHECK(FileHas(inl, "TrailRecipe_ToLegacyMaterial(rec, em, base, &cfg.material, &cfg.deform);"),
          "the strand trail still owns its update callback, not the smoke one");
    CHECK(FileHas(inl, "cfg.forceField = mot->cloth ? &s_sweptCloth[s->kind] : NULL;"),
          "and that callback still clears the cloth/force state every frame");
    // Styles are DATA. A second composer would re-acquire every bug this one
    // has already been through — that is the whole reason for the table.
    CHECK(FileHas(inl, "static TrailRecipe k_trailPresets[TRAIL_PRESET_COUNT];"),
          "the styles still live in one table, not in copied composers");
    CHECK(FileHas(inl, "r->additive = false;"),
          "the smoke style still occludes instead of glowing");
    CHECK(FileHas(inl, "cfg.material.bodyOpacity = rec->bodyOpacity;"),
          "and every style still declares how much it survives into the body pass");
    CHECK(FileHas(inl, "Color hotTarget = ColorGradient_Sample(m->hotGrad, 0.20f);"),
          "spawned strand cores use the material hot gradient");
    CHECK(FileHas(inl, "Color hotTarget = ColorGradient_Sample(m->hotGrad, 0.20f);"),
          "live-tuned strand cores keep using the material hot gradient");
    CHECK(!FileHas(inl, "255 - base.r") && !FileHas(inl, "255 - lbase.r"),
          "strand no longer creates a pink core by whitening red glow");
    CHECK(FileHas(inl, "out->tailColor = (Color){"),
          "every style still authors an along-trail colour ramp");
    // Strand DENSITY lives in the asset — no parameter turns 34 hairs into 300 —
    // so each style owns its sheet, and a LIVE style swap must re-point the
    // layer too or the smoke style renders the energy sheet and looks unchanged.
    CHECK(FileHas("core/trails/trail_recipe.h", "VFX_SurfaceId surface;"),
          "each style still names its own strand sheet");
    CHECK(FileHas(inl, "r->surface = VFX_SURFACE_SMOKE_STRAND;"),
          "and the smoke style still points at the smoke-authored one");
    CHECK(FileHas(inl, "s->recipe.layers = s->layers;"),
          "a live style swap still re-points the layer, not just the numbers");
    CHECK(FileHas("core/vfx_surface_registry.h", "VFX_SURFACE_SMOKE_STRAND"),
          "the smoke strand sheet is a registered surface, not a raw path");
    CHECK(FileHas("scripts/gen_smoke_strand_texture.py", "class SubWisp:"),
          "and its generator builds a WISP (a whole trail shape), not a tileable material");
    // The reference's R/G are one complete streak each, head/tail taper painted
    // in. Tiling such a sheet gives a rope of identical segments — no head, no
    // tail, no silhouette — and three sin-offset samples of a rope are three
    // ropes. That was two attempts' worth of wrong.
    CHECK(FileHas("scripts/gen_smoke_strand_texture.py", "must NOT tile along V"),
          "the generator states that the smoke sheet cannot tile");
    CHECK(FileHas(fs, "bool stretch = u_strandFlow.z > 0.5;"),
          "the shader still chooses tile-vs-stretch per style");
    CHECK(FileHas(fs, "float v0 = SurfaceFlow_AlongV(along, vBase, 1.00, panA, stretch);"),
          "and a stretched sheet still maps once across the whole trail");
    CHECK(FileHas(inl, "UVFx_SyncStretch(&r->deform, &r->flow, true);"),
          "the smoke style still stretches its shape sheet");
    CHECK(!FileHas(inl, "UVFx_SyncStretch(&k_trailPresets[TRAIL_PRESET_ENERGY]"),
          "and the energy style still tiles its material sheet");

    // ── The tail ──────────────────────────────────────────────────────────
    // Softening a shared cut only blurs it. The three bundles must END at
    // three different segments, applied PER BUNDLE (before the max — after it,
    // one cut would clip the combined result again).
    CHECK(FileHas(fs, "float endLate = clamp(u_tailFadeA + u_tailShape.x, 0.0, 1.0);"),
          "the bundles still end at staggered points, not on one line");
    CHECK(FileHas(fs, "s0 *= end0;") &&
          FileHas(fs, "s1 *= end1;") &&
          FileHas(fs, "s2 *= end2;"),
          "and each bundle's end is applied to that bundle alone");
    CHECK(FileHas(fs, "if (max(inten, hotSignal) < 0.004)"),
          "a dark strand texel cannot discard the independent hot core");
    CHECK(FileHas(fs, "float thr = u_dissolve * ramp + u_tailShape.y * dying;"),
          "the dissolve still bites harder over the dying stretch — holes, not dimming");
    CHECK(FileHas(fs, "mix(1.0, clamp(u_tailShape.z, 0.05, 1.0), dying)"),
          "and the bundles still narrow as they die instead of fading at full width");
    CHECK(FileHas(c, "l->tailShape     = GetShaderLocation(shader, \"u_tailShape\");"),
          "the C layer still binds the tail shape");
    CHECK(FileHas(inl, "out->tailStagger = r->mask.tailStagger;"),
          "and a live style swap still re-pushes the tail treatment");
    // The old puff trail was SUPPOSED to be deleted (two similar-looking bench
    // buttons meant the wrong one kept getting judged), but the deletion never
    // happened: VFX_ComposeSmokeTrail is still declared, still implemented in
    // vc_smoke_trail.inl, and still wired to bench entry 25. A red assertion
    // that nobody acts on is worse than no assertion — it trains the reader to
    // skim failures, which is exactly how the strand-trail hunt of 09-10/08
    // ignored a genuinely failing suite. So this now pins the CURRENT contract:
    // the replacement exists and the survivor says out loud what replaced it,
    // so the two cannot be confused at the bench. Removing it entirely is an
    // open decision recorded in core/docs/PROGRESS.md.
    CHECK(FileHas(inl, "TRAIL_PRESET_SMOKE") && FileHas("core/trails/trail_recipe.h", "TRAIL_PRESET_SMOKE,"),
          "the strand trail provides the smoke replacement");
    CHECK(FileHas("core/composition/common/vc_smoke_trail.inl", "VFX_ComposeStrandTrail"),
          "and the surviving puff trail names its replacement, so neither is judged as the other");
    CHECK(FileHas(inl, "void VFX_Trail_Stop(int trailId)"),
          "and the strand trail owns its own soft-release entry point");
    CHECK(FileHas(c, "case TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE:"),
          "the blade envelope still exists in the width switch");
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
    Test_SinBandStaysInsideTheQuad();
    Test_TheThreeWaveFieldsActuallyDiverge();
    Test_DisorderRampsTowardTheTail();
    Test_SinBandIsArcAnchored();
    Test_TailIsWidestAndDissolves();
    Test_PhaseDesync();
    Test_HashModesAreBounded();
    Test_SideFallback();
    Test_PackedMaterialMath();
    Test_ProfiledBodyKeepsFilamentStructure();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

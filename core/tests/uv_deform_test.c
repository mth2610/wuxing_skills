// core/uv — UV-space deformation and surface flow.
//
// WHAT THIS CAN SEE: the arithmetic. Every function in core/uv/shaders/*.glsl
// and its C twin in core/uv/*.c is transliterated below, so the invariants
// that matter — that the trail's migrated expressions are BIT-IDENTICAL to the
// ones they replaced, that folding is exact where it is claimed to be, that
// the envelopes have the shape their names promise — can be settled without a
// GPU, a window, or a screenshot.
//
// WHAT IT CANNOT SEE: whether the shader compiles, whether the uniforms
// arrive, whether the sheet is bound, or whether any of it looks right. A
// green run here says the maths is capable of the effect, nothing more. The
// mirror also rots silently — a C twin of a GLSL expression stays green
// forever after the GLSL changes underneath it — so the last test asserts the
// load-bearing lines still exist in the real sources, including the build
// wiring, because a missing shader file does not report as a shader problem.
//
// Standalone: links nothing, no raylib, no GL. Paths are repo-root relative;
// scripts/run_core_tests.sh cds there.

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...)                                        \
  do {                                                                         \
    g_checks++;                                                                \
    if (cond) printf("PASS: %s\n", name);                                      \
    else {                                                                     \
      printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__);                       \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

#define CHECK(cond, name)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (cond) printf("PASS: %s\n", name);                                      \
    else {                                                                     \
      printf("FAIL: %s\n", name);                                              \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

#define UV_TAU 6.2831853f

// ── The mirror ──────────────────────────────────────────────────────────────
// Transliterations of core/uv/shaders/uv_deform.glsl and surface_flow.glsl.
// Every literal carries an f suffix: in C an unsuffixed 0.73 is a double and
// would promote the whole expression, which would make the bit-identity test
// below measure the wrong thing.

#define UV_ENV_NONE 0
#define UV_ENV_RAMP 1
#define UV_ENV_BELL 2
#define UV_ENV_SMOOTHSTEP 3
#define UV_ENV_HEAD_WELD 4

static float Fract(float x) { return x - floorf(x); }

static float SmoothStep(float e0, float e1, float x) {
  float span = e1 - e0;
  if (fabsf(span) < 1e-9f) return (x < e0) ? 0.0f : 1.0f;
  float t = (x - e0) / span;
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

static float Envelope(int kind, float c, float start, float end) {
  if (kind == UV_ENV_NONE) return 1.0f;
  if (kind == UV_ENV_SMOOTHSTEP) return SmoothStep(start, end, c);
  if (kind == UV_ENV_HEAD_WELD) return SmoothStep(start, end, c) * c;
  float span = end - start;
  float k = (c - start) / (fabsf(span) < 0.000001f ? 0.000001f : span);
  if (k < 0.0f) k = 0.0f;
  else if (k > 1.0f) k = 1.0f;
  if (kind == UV_ENV_BELL) return 0.5f - 0.5f * cosf(UV_TAU * k);
  return k;
}

static float SinePhase(float turns, float phase, float amp) {
  return sinf(Fract(turns) * UV_TAU + phase) * amp;
}

static float FoldAngle(float radians) { return Fract(radians / UV_TAU) * UV_TAU; }

static float NoiseFlow(float sA, float sB, float amp) {
  return ((sA - 0.5f) + (sB - 0.5f)) * amp;
}

static float Pan(float t, float speed) { return Fract(t * speed); }

static float AlongV(float stretched, float base, float scale, float pan,
                    int stretch) {
  return stretch ? stretched : Fract(base * scale) - pan;
}

static float AcrossU(float across, float centre, float halfWidth) {
  return 0.5f + (across - centre) / (2.0f * halfWidth);
}

// ── 1. The trail migration is BIT-IDENTICAL, not merely equivalent ──────────
//
// This is the regression proof for the whole exercise. The strand trail landed
// after eight rounds of tuning and the migration was allowed on the condition
// that the rendered result does not change. "Looks the same" is not a claim
// this machine can make — it cannot create a Vulkan instance — so the claim
// made instead is the stronger and checkable one: every expression the
// migration touched produces the SAME FLOAT.
//
// That is why UVDeform_SinePhase takes whole TURNS rather than radians, and
// why the detune multipliers stayed outside the call in trail_deform.fs.
// sin(x)*amp*k and sin(x)*(amp*k) are not the same float, so folding the
// detune into the amp argument would have been "equivalent" and wrong.
static void Test_TrailWavesAreBitIdentical(void) {
  const float f = 0.55f, sp = 0.85f, spread = 0.65f, envHead = 0.10f;
  const float baseAmp = 0.40f, ph = 1.7f, tiling = 0.65f;
  int mismatches = 0, samples = 0;
  float worstDelta = 0.0f;

  for (int ti = 0; ti < 24; ti++) {
    float t = (float)ti * 3.7f;
    for (int mi = 0; mi < 20; mi++) {
      float metres = (float)mi * 0.83f - 4.0f;
      for (int ai = 0; ai <= 16; ai++) {
        float along = (float)ai / 16.0f;

        // OLD — trail_deform.fs mode 2 as it stood before the migration.
        float oldRamp = SmoothStep(0.0f, fmaxf(envHead, 0.001f), along) * along;
        float amp = baseAmp * oldRamp;
        float oldW0 = sinf(Fract(metres * f + t * sp) * UV_TAU + ph) * amp;
        float oldW1 =
            sinf(Fract(metres * f * (1.0f + 0.73f * spread) + t * sp * 1.41f) *
                     UV_TAU + ph * 2.3f) * amp * (1.0f - 0.28f * spread);
        float oldW2 =
            sinf(Fract(metres * f * (1.0f - 0.39f * spread) + t * sp * 0.67f) *
                     UV_TAU + ph * 4.1f) * amp * (1.0f + 0.25f * spread);
        float oldPanA = Fract(t * 0.35f), oldPanB = Fract(t * 0.70f);
        float vBase = metres * tiling;

        // NEW — the same lines routed through core/uv.
        float newRamp = Envelope(UV_ENV_HEAD_WELD, along, 0.0f,
                                 fmaxf(envHead, 0.001f));
        float namp = baseAmp * newRamp;
        float newW0 = SinePhase(metres * f + t * sp, ph, namp);
        float newW1 =
            SinePhase(metres * f * (1.0f + 0.73f * spread) + t * sp * 1.41f,
                      ph * 2.3f, namp) * (1.0f - 0.28f * spread);
        float newW2 =
            SinePhase(metres * f * (1.0f - 0.39f * spread) + t * sp * 0.67f,
                      ph * 4.1f, namp) * (1.0f + 0.25f * spread);
        float newPanA = Pan(t, 0.35f), newPanB = Pan(t, 0.70f);

        const float o[7] = {oldRamp, oldW0, oldW1, oldW2, oldPanA, oldPanB,
                            0.5f + (0.3f - oldW0) / (2.0f * 0.34f)};
        const float n[7] = {newRamp, newW0, newW1, newW2, newPanA, newPanB,
                            AcrossU(0.3f, newW0, 0.34f)};
        for (int k = 0; k < 7; k++) {
          samples++;
          if (o[k] != n[k]) {
            mismatches++;
            float d = fabsf(o[k] - n[k]);
            if (d > worstDelta) worstDelta = d;
          }
        }

        // Both TILE and STRETCH modes of the along-surface coordinate.
        for (int s = 0; s <= 1; s++) {
          float ov0 = s ? along : Fract(vBase) - oldPanA;
          float ov1 = s ? along : Fract(vBase * 1.60f) - oldPanB;
          float ov2 = s ? along : Fract(vBase * 0.70f) - oldPanA * 0.55f;
          float ovD = Fract(vBase * 0.45f) - oldPanB * 0.35f; // dissolve always tiles
          float nv0 = AlongV(along, vBase, 1.00f, newPanA, s);
          float nv1 = AlongV(along, vBase, 1.60f, newPanB, s);
          float nv2 = AlongV(along, vBase, 0.70f, newPanA * 0.55f, s);
          float nvD = AlongV(along, vBase, 0.45f, newPanB * 0.35f, 0);
          const float ov[4] = {ov0, ov1, ov2, ovD};
          const float nv[4] = {nv0, nv1, nv2, nvD};
          for (int k = 0; k < 4; k++) {
            samples++;
            if (ov[k] != nv[k]) {
              mismatches++;
              float d = fabsf(ov[k] - nv[k]);
              if (d > worstDelta) worstDelta = d;
            }
          }
        }
      }
    }
  }

  CHECK_MSG(mismatches == 0,
            "trail migration is bit-identical across the whole sweep",
            "%d of %d samples differ, worst delta %g", mismatches, samples,
            (double)worstDelta);
  CHECK_MSG(samples > 50000, "the sweep is actually wide", "only %d samples",
            samples);
}

// ── 2. Why SinePhase takes TURNS: the regrouping it prevents ────────────────
//
// A negative control for the test above. If UVDeform_SinePhase had taken the
// drive and frequency separately, or folded the detune into `amp`, the result
// would have been "equivalent" — and a different float. Prove that the wrong
// grouping really does differ, so test 1 is measuring something.
static void Test_TheWrongGroupingWouldHaveDiffered(void) {
  int differing = 0;
  for (int i = 0; i < 4000; i++) {
    float metres = (float)i * 0.017f - 30.0f;
    float f = 0.55f, spread = 0.65f, amp = 0.31f;
    float k = 1.0f - 0.28f * spread;
    float turns = metres * f * (1.0f + 0.73f * spread);
    float right = SinePhase(turns, 0.9f, amp) * k; // what the shader does
    float wrong = SinePhase(turns, 0.9f, amp * k); // the tempting shortcut
    if (right != wrong) differing++;
  }
  CHECK_MSG(differing > 0,
            "sin(x)*amp*k is not sin(x)*(amp*k) — test 1 is not vacuous",
            "%d differing of 4000", differing);
}

// ── 3. Folding: exact for a sine, and for almost nothing else ───────────────
static void Test_FoldingASineIsExact(void) {
  // At small arguments the fold is a no-op to within an ULP: it is the same
  // sine, just written differently.
  // Bounded against the argument's OWN precision rather than a magic constant:
  // the difference may not exceed a few ULP of x*TAU, which is the most any
  // rewrite of the same expression is allowed to move.
  float worstRatio = 0.0f;
  for (int i = 1; i < 3000; i++) {
    float x = (float)i * 0.0017f; // up to ~5 turns
    float d = fabsf(sinf(Fract(x) * UV_TAU) - sinf(x * UV_TAU));
    float ulp = x * UV_TAU * 1.1920929e-7f; // 2^-23
    float ratio = d / ulp;
    if (ratio > worstRatio) worstRatio = ratio;
  }
  CHECK_MSG(worstRatio < 8.0f,
            "at a small argument, folding moves the sine by only a few ULP of "
            "its own argument",
            "worst is %g ULP", (double)worstRatio);

  // At LARGE arguments the two visibly disagree — and the folded one is the
  // one that is right. Measured against a double-precision reference, because
  // the whole question here is float32 precision and a float32 instrument
  // cannot answer it. This is the reason the fold exists: it is not a
  // micro-optimisation, it is what stops the argument outgrowing float32's
  // ability to resolve a fraction of a cycle.
  double worstFolded = 0.0, worstUnfolded = 0.0;
  for (int i = 0; i < 3000; i++) {
    float x = 40000.0f + (float)i * 0.31f;
    double truth = sin(fmod((double)x, 1.0) * 6.283185307179586);
    double df = fabs((double)sinf(Fract(x) * UV_TAU) - truth);
    double du = fabs((double)sinf(x * UV_TAU) - truth);
    if (df > worstFolded) worstFolded = df;
    if (du > worstUnfolded) worstUnfolded = du;
  }
  CHECK_MSG(worstFolded < worstUnfolded * 0.1,
            "at a large argument the FOLDED sine is the accurate one — this is "
            "what the fold buys",
            "folded off by %g, unfolded off by %g", worstFolded, worstUnfolded);

  // WHAT IN-SHADER FOLDING DOES NOT FIX, and the module's answer to it.
  //
  // Folding a product that has ALREADY been computed at full magnitude cannot
  // recover information the product lost. Frame-to-frame resolution is the
  // case that proves it: the phase step per frame is speed*dt and the
  // quantum at the argument is (t*speed)*2^-23, so their ratio is
  // dt/(t*2^-23) — the speed cancels. No amount of folding downstream changes
  // it, and a stuttering animation stays stuttering.
  //
  // The fix has to happen at the ORIGIN, on the C side, where the modulus can
  // be chosen deliberately: SurfaceFlow_PackGPU folds each pan before it is
  // uploaded, and trail_system.c folds cumulative arc length with fmodf. Show
  // the difference, so nobody "fixes" a stutter by adding a fold in the .fs.
  const float bigT = 3000000.0f, speed = 4.0f, dt = 1.0f / 60.0f;
  int movedShaderFold = 0, movedCpuFold = 0;
  float prevS = 2.0f, prevC = 2.0f;
  for (int i = 0; i < 240; i++) {
    float t = bigT + (float)i * dt;
    // folded in the shader, from a full-magnitude product
    float vs = sinf(FoldAngle(t * speed));
    // folded at the origin: the clock itself is wrapped before it is scaled
    float vc = sinf(FoldAngle(Fract(t * speed / UV_TAU) * UV_TAU));
    if (vs != prevS) movedShaderFold++;
    if (vc != prevC) movedCpuFold++;
    prevS = vs;
    prevC = vc;
  }
  CHECK_MSG(movedShaderFold < 240,
            "at a large clock an in-shader fold does NOT restore frame-to-frame "
            "motion — the bits were gone before it ran",
            "moved on %d of 240 frames", movedShaderFold);
  CHECK(movedCpuFold >= movedShaderFold,
        "which is why the module folds pans and arc length on the C SIDE, at "
        "the origin, not in the shader");

  // And the price at small angles stays bounded.
  float worstAngle = 0.0f;
  for (int i = 0; i < 3000; i++) {
    float r = (float)i * 0.021f;
    float d = fabsf(sinf(FoldAngle(r)) - sinf(r));
    if (d > worstAngle) worstAngle = d;
  }
  CHECK_MSG(worstAngle < 1e-5f,
            "UVDeform_FoldAngle costs about an ULP at small angles",
            "worst delta %g", (double)worstAngle);
}

static void Test_NestedFoldsChangeTheTilingRate(void) {
  int differing = 0;
  float worst = 0.0f;
  for (int i = 0; i < 2000; i++) {
    float x = (float)i * 0.037f;
    float once = Fract(x * 1.60f);
    float nested = Fract(Fract(x) * 1.60f);
    if (once != nested) {
      differing++;
      float d = fabsf(once - nested);
      if (d > worst) worst = d;
    }
  }
  CHECK_MSG(differing > 1000 && worst > 0.1f,
            "fract(fract(x)*k) != fract(x*k) — never nest a fold",
            "%d differing, worst %g", differing, (double)worst);
}

// ── 4. The envelopes have the shape their names promise ─────────────────────
static void Test_EnvelopeInvariants(void) {
  // HEAD_WELD: excursion is EXACTLY zero at the emitter, and never decreases.
  CHECK(Envelope(UV_ENV_HEAD_WELD, 0.0f, 0.0f, 0.10f) == 0.0f,
        "HEAD_WELD is exactly 0 at c=0 — the weld holds");
  int monotone = 1;
  float prev = -1.0f;
  for (int i = 0; i <= 200; i++) {
    float c = (float)i / 200.0f;
    float v = Envelope(UV_ENV_HEAD_WELD, c, 0.0f, 0.10f);
    if (v < prev - 1e-6f) monotone = 0;
    prev = v;
  }
  CHECK(monotone, "HEAD_WELD grows monotonically toward the tail");

  // BELL: a bounded feature needs zero VALUE and zero SLOPE at both ends, or
  // it still reads as a continuous lane with a visible cut at each end.
  float b0 = Envelope(UV_ENV_BELL, 0.0f, 0.0f, 1.0f);
  float b1 = Envelope(UV_ENV_BELL, 1.0f, 0.0f, 1.0f);
  float bMid = Envelope(UV_ENV_BELL, 0.5f, 0.0f, 1.0f);
  const float h = 0.0005f;
  float slope0 = (Envelope(UV_ENV_BELL, h, 0.0f, 1.0f) - b0) / h;
  float slope1 = (b1 - Envelope(UV_ENV_BELL, 1.0f - h, 0.0f, 1.0f)) / h;
  CHECK_MSG(fabsf(b0) < 1e-6f && fabsf(b1) < 1e-6f,
            "BELL is zero at both ends", "b0=%g b1=%g", (double)b0, (double)b1);
  CHECK_MSG(fabsf(slope0) < 1e-2f && fabsf(slope1) < 1e-2f,
            "BELL has zero SLOPE at both ends — a bounded feature, not a lane",
            "slope0=%g slope1=%g", (double)slope0, (double)slope1);
  CHECK_MSG(fabsf(bMid - 1.0f) < 1e-6f, "BELL peaks at 1 in the middle",
            "mid=%g", (double)bMid);

  // RAMP clamps outside its span rather than running away.
  CHECK(Envelope(UV_ENV_RAMP, -5.0f, 0.0f, 1.0f) == 0.0f &&
            Envelope(UV_ENV_RAMP, 5.0f, 0.0f, 1.0f) == 1.0f,
        "RAMP clamps outside [start, end]");
  // A degenerate span must not divide by zero.
  float degenerate = Envelope(UV_ENV_RAMP, 0.5f, 0.3f, 0.3f);
  CHECK_MSG(degenerate == degenerate && fabsf(degenerate) <= 1.0f,
            "a zero-width RAMP span is finite, not NaN", "got %g",
            (double)degenerate);

  CHECK(Envelope(UV_ENV_NONE, 0.3f, 0.2f, 0.9f) == 1.0f,
        "NONE is 1 everywhere");
}

// ── 5. NOISE_FLOW is zero-mean; a biased pair would translate the surface ───
static void Test_NoiseFlowIsZeroMean(void) {
  float sum = 0.0f;
  int n = 0;
  for (int i = 0; i <= 100; i++) {
    for (int j = 0; j <= 100; j++) {
      sum += NoiseFlow((float)i / 100.0f, (float)j / 100.0f, 1.0f);
      n++;
    }
  }
  float mean = sum / (float)n;
  CHECK_MSG(fabsf(mean) < 1e-5f,
            "NoiseFlow over a uniform field has zero mean — it warps, it does "
            "not slide",
            "mean %g", (double)mean);
  CHECK(NoiseFlow(0.5f, 0.5f, 3.0f) == 0.0f,
        "the neutral sample pair (0.5, 0.5) contributes exactly nothing");
}

// ── 6. The GPU pack: layout, compaction, and the count the shader loops to ──
#define MAX_LAYERS 4
#define FLOATS_PER_LAYER 12

static void Test_PackLayoutAndCompaction(void) {
  // Mirror of UVDeform_PackGPU: three vec4 per layer, enums as floats in .x,
  // zero-amplitude layers skipped so GPU indices are not CPU indices.
  float amps[MAX_LAYERS] = {0.4f, 0.0f, 0.7f, 0.0f}; // two live, two dead
  float out[MAX_LAYERS * FLOATS_PER_LAYER];
  memset(out, 0, sizeof(out));
  int packed = 0;
  for (int i = 0; i < MAX_LAYERS; i++) {
    if (fabsf(amps[i]) < 1e-6f) continue;
    float *p = out + packed * FLOATS_PER_LAYER;
    p[0] = 0.0f;     // kind
    p[3] = amps[i];  // amplitude
    p[4] = 1.5f;     // frequency
    p[8] = 4.0f;     // env = HEAD_WELD
    packed++;
  }

  CHECK_MSG(packed == 2, "two live layers of four survive the pack",
            "packed %d", packed);
  CHECK_MSG(out[0 * FLOATS_PER_LAYER + 3] == 0.4f &&
                out[1 * FLOATS_PER_LAYER + 3] == 0.7f,
            "the pack COMPACTS — layer 2 lands at GPU index 1, so a GPU index "
            "is not a CPU index",
            "slot0 amp %g slot1 amp %g", (double)out[3],
            (double)out[FLOATS_PER_LAYER + 3]);
  CHECK(out[2 * FLOATS_PER_LAYER + 3] == 0.0f,
        "everything past the packed count stays zeroed");
  CHECK(FLOATS_PER_LAYER % 4 == 0,
        "the stride is a whole number of vec4 — std140 pads array elements to "
        "16 bytes, so a float[] or vec3[] upload would misalign");
}

// ── 7. The mirror still matches the source ──────────────────────────────────
// Everything above is a C twin of GLSL. A twin stays green forever after its
// original changes, so pin the load-bearing lines in the real files. Includes
// the BUILD wiring: a missing shader file does not report as a shader problem,
// so "is it copied" is a maths-level invariant here, not a packaging detail.

static void CollapseWS(const char *src, char *out, size_t cap) {
  size_t o = 0;
  int inWS = 0;
  for (size_t i = 0; src[i] && o + 2 < cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!inWS && o > 0) out[o++] = ' ';
      inWS = 1;
    } else {
      out[o++] = (char)c;
      inWS = 0;
    }
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = '\0';
}

// Drops // line comments. A NEGATIVE assertion ("this form no longer appears")
// is worthless without it: these files explain in prose exactly which mistakes
// they are avoiding, so a plain search finds every banned construct quoted in
// the comment that bans it. Positive assertions do not need it, but stripping
// is harmless for them.
static void StripLineComments(const char *src, char *out, size_t cap) {
  size_t o = 0;
  int inComment = 0;
  for (size_t i = 0; src[i] && o + 2 < cap; i++) {
    if (!inComment && src[i] == '/' && src[i + 1] == '/') inComment = 1;
    if (src[i] == '\n') inComment = 0;
    if (!inComment) out[o++] = src[i];
  }
  out[o] = '\0';
}

static int FileHasImpl(const char *path, const char *needle, int stripComments) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  static char buf[400000], stripped[400000], flat[400000];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0';
  fclose(f);
  if (stripComments) {
    StripLineComments(buf, stripped, sizeof(stripped));
    CollapseWS(stripped, flat, sizeof(flat));
  } else {
    CollapseWS(buf, flat, sizeof(flat));
  }
  char want[2048];
  CollapseWS(needle, want, sizeof(want));
  return strstr(flat, want) != NULL;
}

static int FileHas(const char *path, const char *needle) {
  return FileHasImpl(path, needle, 0);
}

// Code only — use for every negative assertion.
static int FileHasCode(const char *path, const char *needle) {
  return FileHasImpl(path, needle, 1);
}

static void Test_MirrorStillMatchesSource(void) {
  const char *deform = "core/uv/shaders/uv_deform.glsl";
  const char *flow = "core/uv/shaders/surface_flow.glsl";
  const char *field = "core/uv/shaders/uv_field.glsl";
  const char *trail = "core/trails/shaders/trail_deform.fs";

  // The primitives this file mirrors.
  CHECK(FileHas(deform, "return sin(fract(turns) * UV_TAU + phase) * amp;"),
        "SinePhase folds turns, then scales — the mirrored form");
  CHECK(FileHas(deform, "return fract(radians / UV_TAU) * UV_TAU;"),
        "FoldAngle is the radians-domain round trip");
  CHECK(FileHas(deform, "if (kind == UV_ENV_HEAD_WELD) return smoothstep(start, end, c) * c;"),
        "HEAD_WELD is smoothstep * c, the trail's ramp exactly");
  CHECK(FileHas(deform, "if (kind == UV_ENV_BELL) return 0.5 - 0.5 * cos(UV_TAU * k);"),
        "BELL is the raised cosine, not a triangle");
  CHECK(FileHas(deform, "return ((sA - 0.5) + (sB - 0.5)) * amp;"),
        "NoiseFlow biases each sample to zero mean BEFORE adding");
  CHECK(FileHas(flow, "return stretch ? stretched : fract(base * scale) - pan;"),
        "AlongV folds base and scale together, exactly once");
  CHECK(FileHas(flow, "return 0.5 + (across - centre) / (2.0 * halfWidth);"),
        "AcrossU remaps onto the swung centreline");
  CHECK(FileHas(flow, "return fract(t * speed);"), "Pan folds its clock");

  // Negative assertions: the forms that were wrong, pinned out of existence.
  // All of these read CODE — the files discuss these very mistakes in prose.
  CHECK(!FileHasCode(flow, "fract(fract("),
        "no nested fold survives in surface_flow.glsl");
  CHECK(!FileHasCode(deform, "fract(fract("),
        "no nested fold survives in uv_deform.glsl");
  CHECK(!FileHasCode(flow, "fract(mat.x * tilePan.x)"),
        "the packed field path does NOT fold — that would put a derivative "
        "seam through a triplanar consumer");
  CHECK(!FileHasCode(deform, "uniform "),
        "uv_deform.glsl declares no uniforms — it is the pure tier, so a "
        "shader with its own naming can call it");
  CHECK(!FileHasCode(flow, "uniform "), "surface_flow.glsl declares no uniforms");
  CHECK(!FileHasCode(deform, "#include") &&
            !FileHasCode(flow, "noise.glsl"),
        "neither pure file includes noise.glsl — it has no include guard, so "
        "depending on it would double-define hash2/vnoise/fbm2");

  // vec4-only uniform arrays.
  CHECK(FileHas(field, "uniform vec4 u_uvField[UV_DEFORM_MAX_LAYERS * 3];") &&
            FileHas(field, "uniform vec4 u_flowLayer[SURFACE_FLOW_MAX_LAYERS * 2];"),
        "both uniform arrays are vec4[] — never float[] or vec3[]");
  CHECK(!FileHasCode(field, "uniform float u_uvField") &&
            !FileHasCode(field, "uniform vec3 u_uvField"),
        "and no scalar or vec3 array crept back in");

  // The C twins.
  CHECK(FileHas("core/uv/uv_deform.c", "return sinf(Fract(turns) * UV_TAU + phase) * amp;"),
        "the C mirror of SinePhase matches the GLSL");
  CHECK(FileHas("core/uv/uv_deform.c", "if (kind == UV_ENV_BELL) return 0.5f - 0.5f * cosf(UV_TAU * k);"),
        "the C mirror of BELL matches the GLSL");
  CHECK(FileHas("core/uv/surface_flow.c", "return stretch ? stretched : Fract(base * scale) - pan;"),
        "the C mirror of AlongV matches the GLSL");
  CHECK(FileHas("core/uv/uv_deform.c", "if (fabsf(L->amplitude) < 1e-6f) continue;"),
        "PackGPU compacts zero-amplitude layers");
  CHECK(FileHas("core/uv/uv_deform.c", "(int)layer.kind >= UV_DEFORM_KIND_COUNT") &&
            FileHas("core/uv/uv_deform.c", "TraceLog(LOG_WARNING"),
        "range checks are against the COUNT sentinel, and every clamp "
        "announces itself");
  CHECK(!FileHasCode("core/uv/uv_deform.c", "malloc") &&
            !FileHasCode("core/uv/surface_flow.c", "malloc"),
        "no allocation anywhere in the module");

  // ── The trail migration, in the real shader ──
  CHECK(FileHas(trail, "#include \"core/uv/shaders/uv_deform.glsl\"") &&
            FileHas(trail, "#include \"core/uv/shaders/surface_flow.glsl\""),
        "trail_deform.fs includes the module");
  // NOT "#include uv_field.glsl" — confirmed on a real build 05/08/2026 that
  // combination breaks compilation (rlvk/shaderc: "Missing entry point"),
  // because this file ALREADY includes uv_deform.glsl/surface_flow.glsl
  // directly above and uv_field.glsl re-includes both — ShaderPreprocessor_Load
  // (core/shading/shader_preprocessor.c) is a naive recursive text substitution with
  // NO include-path dedup, so their bodies land in the flattened source
  // twice. u_uvField/u_uvMeta are declared directly instead (see there).
  CHECK(!FileHasCode(trail, "#include \"core/uv/shaders/uv_field.glsl\"") &&
            FileHas(trail, "uniform vec4 u_uvField[UV_DEFORM_MAX_LAYERS * 3];") &&
            FileHas(trail, "uniform vec4 u_uvMeta;"),
        "u_uvField/u_uvMeta are declared directly, not pulled in via "
        "uv_field.glsl's include (which broke a real build)");
  // 05/08/2026: w0/w1/w2 generalised a step further, off UVDeform_SinePhase
  // called inline onto UVDeform_LayerOffset reading the PACKED field
  // (u_uvField, declared directly above) — trail_system.c's ApplyDeformUniforms
  // now builds all three bundles' detune into the packed layers rather than
  // this file detuning them inline. No longer bit-identical to the pre-
  // packed version (the generic path fixes amplitude*envelope's
  // multiplication order) — algebraically identical, and nothing here can
  // assert bit-identity across that boundary the way the earlier migration
  // (SurfaceFlow_AlongV, below) could.
  CHECK(FileHas(trail, "float w0 = UVDeform_LayerOffset(u_uvField[0], u_uvField[1], u_uvField[2],") &&
            FileHas(trail, "float w1 = UVDeform_LayerOffset(u_uvField[3], u_uvField[4], u_uvField[5],") &&
            FileHas(trail, "float w2 = UVDeform_LayerOffset(u_uvField[6], u_uvField[7], u_uvField[8],"),
        "w0/w1/w2 each read their own packed UVDeformField layer");
  CHECK(!FileHasCode(trail, "uniform vec4  u_sinWave;") &&
            !FileHasCode("core/trails/trail_system.c", "float sinWave[4] ="),
        "u_sinWave is gone on both sides, not merely unused");
  CHECK(FileHas(trail, "float v0 = SurfaceFlow_AlongV(along, vBase, 1.00, panA, stretch);"),
        "the along-surface coordinate goes through the module");
  CHECK(FileHas(trail, "SurfaceFlow_AlongV(along, vBase, 0.45, panB * 0.35, false)"),
        "the dissolve channel still always TILES, stretch or not — preserved "
        "exactly, and now visible rather than implicit");
  CHECK(FileHas(trail, "float ramp = UVDeform_Envelope(UV_ENV_HEAD_WELD, along, 0.0,"),
        "the disorder ramp is the module's HEAD_WELD envelope");
  CHECK(!FileHasCode(trail, "sin(fract(metres * f + u_time * sp) * TAU + ph) * amp"),
        "and the old inline sine is gone, not merely shadowed");
  CHECK(!FileHasCode(trail, "float v0 = stretch ? along : fract(vBase) - panA;"),
        "and so is the old inline tile-or-stretch");

  // ── The other two consumers ──
  // The shield migrated onto the shared glass shell — that half still holds. It
  // does NOT use the shared calcFresnel(), and that is a decision, not a gap:
  // glass_shell.fs's quartic also drives the wall alpha and the scene-through
  // window (physics), which must not move when the rim is restyled — the reason
  // is written at glass_shell.fs:11. Asserted NEGATIVELY so a later "use the
  // shared helper everywhere" cleanup goes red here instead of silently
  // recoupling styling to physics.
  CHECK(FileHas("core/shaders/glass_shell.fs", "u_emissionOnly") &&
            FileHas("core/shaders/glass_shell.fs", "float fresnel = fresnelX2 * fresnelX2;") &&
            !FileHas("core/shaders/glass_shell.fs", "calcFresnel("),
        "the shield uses the shared glass shell, with its OWN quartic fresnel");
  CHECK(!FileHasCode("core/composition/common/vc_shield_shell.inl", "SurfaceFlow_Apply") &&
            !FileHasCode("core/composition/common/vc_shield_shell.inl", "shield_shell.fs"),
        "the shield shell no longer owns a flow-map shader path");
  CHECK(FileHas("core/shaders/aura_shell.fs", "sin(UVDeform_FoldAngle("),
        "aura_shell's periodic scan folds its clock through the module");
  CHECK(FileHas("core/shaders/aura_shell.fs", "float yScroll = fragPosition.y * u_heightScale - u_time * u_scrollSpeed;"),
        "...while the fbm3 domain scroll right above it stays UNFOLDED — the "
        "fold is exact for a sine and would make an aperiodic field jump");

  // ── Build wiring. A missing shader file does not report as a shader problem ──
  CHECK(FileHas("CMakeLists.txt", "file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders)"),
        "CMakeLists.txt creates core/uv/shaders in the build tree");
  CHECK(FileHas("CMakeLists.txt", "configure_file(core/uv/shaders/uv_deform.glsl") &&
            FileHas("CMakeLists.txt", "configure_file(core/uv/shaders/surface_flow.glsl") &&
            FileHas("CMakeLists.txt", "configure_file(core/uv/shaders/uv_field.glsl") &&
            FileHas("CMakeLists.txt", "configure_file(core/uv/shaders/flow_map.glsl"),
        "every one of the four .glsl has a configure_file line");
  CHECK(FileHas("CMakeLists.txt", "core/uv/uv_deform.c") &&
            FileHas("CMakeLists.txt", "core/uv/surface_flow.c") &&
            FileHas("CMakeLists.txt", "core/uv/flow_map.c"),
        "and all three .c are in the target");
  CHECK(FileHas("Makefile.Android", "cp -f core/uv/shaders/*.glsl") &&
            FileHas("Makefile.Android", "core/uv/uv_deform.c"),
        "Android copies the includes and builds the sources — it globs "
        "core/shaders/common only, so the move needed its own line");
}

int main(void) {
  printf("=== core/uv: trail bit-identity, folding, envelopes, packing, mirror ===\n");
  Test_TrailWavesAreBitIdentical();
  Test_TheWrongGroupingWouldHaveDiffered();
  Test_FoldingASineIsExact();
  Test_NestedFoldsChangeTheTilingRate();
  Test_EnvelopeInvariants();
  Test_NoiseFlowIsZeroMean();
  Test_PackLayoutAndCompaction();
  Test_MirrorStillMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

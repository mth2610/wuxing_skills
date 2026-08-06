// core headless test — PMTube_DrawFaded's alpha mask anchors at the emitter,
// same as the radius profile / deform envelope / centreline weld.
//
// WHY THIS FILE EXISTS. Fourth consumer of one fact, found the same way as the
// other three (see core/docs/LANDMINES.md and pm_tube_envelope_anchor_test.c):
// `PMTubeConfig.anchorAtTail` says "my emitter is at t=0 of this swept path".
// PMTube_DrawFaded's vertical alpha mask
//     m(t) = smoothstep(0, fadeInEnd, t) * (1 - smoothstep(fadeOutStart, 1, t))
// is not symmetric — its two ends mean different physical things, spelled out
// at the call site in core/trails/trail_system.c: "Chân tắt nhanh (khói phải
// dính vào nguồn), ngọn tan chậm hơn" — the foot snaps on at the SOURCE, the
// far end dissolves slowly. It ran on the raw `t` regardless of the anchor,
// so on the smoke trail (whose source is at t=1) the two ends were swapped:
// freshly emitted smoke faded to nothing while the oldest, widest end was
// fully opaque.
//
// Swapping the two PARAMETERS at the call site turns out to be numerically
// identical to flipping the COORDINATE (checked below — the first draft of
// this file asserted the opposite and was wrong). The coordinate is still the
// right place: the parameters carry physical names, the callee already knows
// the anchor, and the smoke column shares this function without flipping.
//
// WHAT THIS CANNOT SEE: whether the result reads as smoke. That needs eyes.
//
// Standalone: links nothing. Paths are repo-root relative.

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...)                                       \
  do {                                                                        \
    g_checks++;                                                               \
    if (cond) printf("PASS: %s\n", name);                                     \
    else {                                                                    \
      printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__);                     \
      g_failures++;                                                           \
    }                                                                         \
  } while (0)

#define CHECK(cond, name)                                                     \
  do {                                                                        \
    g_checks++;                                                               \
    if (cond) printf("PASS: %s\n", name);                                     \
    else {                                                                    \
      printf("FAIL: %s\n", name);                                             \
      g_failures++;                                                           \
    }                                                                         \
  } while (0)

// Mirror of PMTubeSmoothStep (pm_tube.inl), transliterated.
static float SS(float e0, float e1, float x) {
  float span = e1 - e0;
  if (fabsf(span) < 1e-6f) return (x < e0) ? 0.0f : 1.0f;
  float k = (x - e0) / span;
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  return k * k * (3.0f - 2.0f * k);
}

// The shipped constants at the one call site (trail_system.c).
#define FADE_IN_END 0.10f
#define FADE_OUT_START 0.72f

static float Mask(float c) {
  return SS(0.0f, FADE_IN_END, c) * (1.0f - SS(FADE_OUT_START, 1.0f, c));
}

// The trail's funnel, anchored at the tail: t=0 is the OLD wide end, t=1 the
// emitter. (radiusTailFrac 0.12, radiusPow 1.7.)
static float CapsuleTrail(float t) {
  return 0.12f + 0.88f * powf(1.0f - t, 1.7f);
}

// ── 1. The bug: the emitter end is the transparent one ──────────────────────
static void Test_UnanchoredMaskFadesOutTheFreshSmoke(void) {
  // Fraction of the body, measured from the emitter (t=1), that the mask
  // pushes below half opacity.
  float bugAtEmitter = Mask(1.0f);
  float fixAtEmitter = Mask(1.0f - 1.0f); // flipped coordinate at t=1 -> c=0

  // At the emitter itself both are 0 (the weld) — the difference is which
  // SIDE of it is opaque. Sample just inside the body instead.
  float bugNearEmitter = Mask(0.85f);            // t=0.85, raw coordinate
  float fixNearEmitter = Mask(1.0f - 0.85f);     // same ring, flipped
  float bugAtOldEnd = Mask(0.15f);
  float fixAtOldEnd = Mask(1.0f - 0.15f);

  printf("  at emitter (t=1.00): pre-fix %.3f, post-fix %.3f\n",
         bugAtEmitter, fixAtEmitter);
  printf("  near emitter (t=0.85): pre-fix %.3f, post-fix %.3f\n",
         bugNearEmitter, fixNearEmitter);
  printf("  near old end (t=0.15): pre-fix %.3f, post-fix %.3f\n",
         bugAtOldEnd, fixAtOldEnd);

  CHECK_MSG(bugNearEmitter < 0.60f && bugAtOldEnd > 0.95f,
      "pre-fix, the mask is faint where smoke is BORN and fully opaque at the "
      "oldest end — the two physical meanings of the mask are swapped",
      "near emitter %.3f vs near old end %.3f", bugNearEmitter, bugAtOldEnd);

  CHECK_MSG(fixNearEmitter > 0.95f && fixAtOldEnd < 0.60f,
      "post-fix, fresh smoke is opaque and the old end dissolves — which is "
      "what the call site's own comment always said it wanted",
      "near emitter %.3f vs near old end %.3f", fixNearEmitter, fixAtOldEnd);
}

// ── 2. Parameter swap and coordinate flip are the SAME NUMBER ───────────────
// Written expecting the opposite, and the measurement said otherwise:
// smoothstep(0, a, 1-t) == 1 - smoothstep(1-a, 1, t) exactly, so passing
// (1-fadeOutStart, 1-fadeInEnd) reproduces the flipped mask bit for bit. The
// choice between them is therefore NOT a correctness question — it is a
// semantic one, and that is the reason to record it here rather than delete
// the test:
//
//   fadeInEnd  means "the foot, welded to the SOURCE, snaps on fast"
//   fadeOutStart means "the far end dissolves slowly"
//
// Pushing 1-x through them at the call site keeps the arithmetic and makes
// both parameter NAMES lie, in a function shared with the smoke column, which
// does not flip. The coordinate lives in the callee where the anchor already
// is; the physical constants stay physical. Anyone who "simplifies" this back
// to a swap at the call site should read this and choose knowingly.
static void Test_ParameterSwapEqualsCoordinateFlipNumerically(void) {
  float worst = 0.0f, worstT = 0.0f;
  for (int i = 0; i <= 100; i++) {
    float t = (float)i / 100.0f;
    float flipped = Mask(1.0f - t);
    float swapped = SS(0.0f, 1.0f - FADE_OUT_START, t) *
                    (1.0f - SS(1.0f - FADE_IN_END, 1.0f, t));
    float d = fabsf(flipped - swapped);
    if (d > worst) { worst = d; worstT = t; }
  }
  CHECK_MSG(worst < 1e-5f,
      "flipping the coordinate and swapping the two fade parameters are the "
      "same number — so this fix was chosen for naming/ownership reasons, not "
      "arithmetic ones, and the comment saying so must stay true",
      "max difference %.6f at t=%.2f", worst, worstT);
}

// ── 3. The combined result: opacity x width along the body ──────────────────
// What the eye integrates is roughly mask x local diameter. Pre-fix the trail
// put its opacity on the thin fresh end and its width on the transparent old
// end, so the two never coincided.
static void Test_OpacityAndWidthNoLongerFightEachOther(void) {
  float bugPeak = 0.0f, fixPeak = 0.0f;
  float bugSum = 0.0f, fixSum = 0.0f;
  for (int i = 0; i <= 200; i++) {
    float t = (float)i / 200.0f;
    float w = CapsuleTrail(t);
    float b = Mask(t) * w;
    float f = Mask(1.0f - t) * w;
    if (b > bugPeak) bugPeak = b;
    if (f > fixPeak) fixPeak = f;
    bugSum += b; fixSum += f;
  }
  printf("  mask x width: pre-fix peak %.3f mean %.3f | post-fix peak %.3f mean %.3f\n",
         bugPeak, bugSum / 201.0f, fixPeak, fixSum / 201.0f);

  // Post-fix the profile is smooth and centred; pre-fix it spikes hard at the
  // wide old end and collapses over the whole emitter half.
  CHECK_MSG(fixPeak < bugPeak,
      "post-fix the visible mass is spread along the body instead of piling "
      "onto the widest, oldest ring — a wake that thins with age, not a blob "
      "at the back",
      "peak %.3f -> %.3f", bugPeak, fixPeak);
}

// ── 4. Source-drift guard ───────────────────────────────────────────────────
static void CollapseWS(const char *in, char *out, size_t outCap) {
  size_t o = 0; int inWS = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < outCap; p++) {
    int c = *p;
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

static int FileHas(const char *path, const char *needle) {
  static char buf[600000], flat[600000];
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0';
  fclose(f);
  CollapseWS(buf, flat, sizeof(flat));
  char want[2048];
  CollapseWS(needle, want, sizeof(want));
  return strstr(flat, want) != NULL;
}

static void Test_MirrorMatchesSource(void) {
  const char *pm = "core/geometry/pm_tube.inl";
  const char *hh = "core/geometry/procedural_mesh_utils.h";
  const char *ts = "core/trails/trail_system.c";

  CHECK(FileHas(pm, "float f1 = data->anchorAtTail ? (1.0f - t1) : t1;") &&
            FileHas(pm, "float f2 = data->anchorAtTail ? (1.0f - t2) : t2;"),
        "PMTube_DrawFaded flips the mask coordinate on the anchor");
  CHECK(FileHas(pm, "float m1 = PMTubeSmoothStep(0.0f, fadeInEnd, f1) *") &&
            FileHas(pm, "(1.0f - PMTubeSmoothStep(fadeOutStart, 1.0f, f1));"),
        "...and the mask really is built from the flipped coordinate, not "
        "from t with the parameters shuffled");

  // The anchor travels WITH the mesh — the whole point, so no caller can
  // forget to pass it on for a fifth time.
  CHECK(FileHas(hh, "bool anchorAtTail;") &&
            FileHas(pm, "out->anchorAtTail = cfg->anchorAtTail;"),
        "the mesh carries the anchor, so the draw call cannot lose it");

  // The shipped fade constants this file's arithmetic assumes.
  CHECK(FileHas(ts, "PMTube_DrawFaded(&tubeMesh, tiles, uvOff, base, 0.10f, 0.72f, mpt);"),
        "the call site's fade constants (0.10 / 0.72) are unchanged");

  // The other three consumers of the same anchor must still be wired — if a
  // refactor drops one, this file's premise ("four consumers, one fact") is
  // no longer true and someone should find out here.
  CHECK(FileHas(pm, "float tEnv = cfg->anchorAtTail ? (1.0f - t) : t;") &&
            FileHas(pm, "float grow = (p == 1.0f) ? tEnv : powf(tEnv, p);") &&
            FileHas(pm, "deform += PMTubeShapeDeformNoise(cfg, radialSegs, j, tEnv, tNoise, time, normal,") &&
            FileHas(pm, "float w = cfg->centerlineAmp * tEnv * tEnv;"),
        "the other three emitter-anchored quantities are still on the shared "
        "coordinate — four consumers, one fact");
}

int main(void) {
  printf("=== core/geometry: PMTube_DrawFaded alpha mask anchors at the emitter ===\n");
  Test_UnanchoredMaskFadesOutTheFreshSmoke();
  Test_ParameterSwapEqualsCoordinateFlipNumerically();
  Test_OpacityAndWidthNoLongerFightEachOther();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

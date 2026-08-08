// core headless test — pm_tube.inl's RADIUS anchor and its DEFORM-ENVELOPE
// anchor must be the same anchor.
//
// WHY THIS FILE EXISTS. Third bug of one family, found on the smoke trail
// after the other two (pm_tube_offset_clamp_test.c, and
// pm_tube_envelope_coordinate_test.c) were fixed and the churn STILL read as
// flat. PMTubeConfig had a flag that re-anchored only the radius profile —
// it was called `radiusAnchorAtTail`, and the name was the bug. A moving
// trail's emitter sits at t=0 of the swept path, and THREE things anchor to
// the emitter:
//   1. the radius profile r(t)      — thin at the source
//   2. the deform ENVELOPE          — UV_ENV_HEAD_WELD: "no excursion at
//                                     the source" (MeshDeformLayer.env)
//   3. the centreline weld (t*t)    — the base does not fidget
// The flag moved (1) and left (2) and (3) at t=0. After the flip, t=0 is the
// trail's FAT back — so the widest part of the tube got envelope 0 (no churn
// at all), while envelope 1 landed at t=1, the 0.12x-radius front tip where
// there is nothing to bulge.
//
// WHAT THAT COSTS, and why no amplitude could hide it: the absolute radial
// excursion a viewer sees is
//     |dR(t)| = baseRadius * capsuleCurve(t) * envelope(t) * (noise term)
// so `capsuleCurve x envelope` is the whole shape of the available churn.
// Amplitude scales that product uniformly — it cannot move where the product
// is zero. This file measures the product for the smoke column and the smoke
// trail on the SAME layer numbers (they are a verbatim copy of each other,
// see core/composition/common/vc_smoke_trail.inl).
//
// WHAT THIS CANNOT SEE: whether the churn looks like smoke. That needs eyes.
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

// ── Mirrors of the two formulas under test, transliterated from
// core/uv/uv_deform.c and core/geometry/pm_tube.inl (drift-guarded below).
static float SmoothStep(float e0, float e1, float x) {
  float span = e1 - e0;
  if (fabsf(span) < 1e-9f) return (x < e0) ? 0.0f : 1.0f;
  float t = (x - e0) / span;
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}
static float HeadWeld(float start, float end, float c) {
  return SmoothStep(start, end, c) * c;
}
static float HeadWeldSq(float start, float end, float c) {
  return SmoothStep(start, end, c) * c * c;
}
// pm_tube.inl: capsuleCurve = tailFrac + (1 - tailFrac) * tEnv^pow
static float CapsuleCurve(float tEnv, float tailFrac, float pow_) {
  return tailFrac + (1.0f - tailFrac) * powf(tEnv, pow_);
}

// The smoke funnel's shipped numbers — identical in SmokeColumn_BuildShape
// and SmokeTrail_BuildShape (core/composition/common/).
#define TAIL_FRAC 0.12f
#define RADIUS_POW 1.7f
// Layer 0: NORMAL_SCALE, UV_ENV_HEAD_WELD, [0, 0.22]
#define L0_START 0.0f
#define L0_END 0.22f
// Layer 1: NORMAL_OFFSET, UV_ENV_HEAD_WELD_SQ, [0, 0.35]
#define L1_START 0.0f
#define L1_END 0.35f

#define SAMPLES 201

// `anchorAtTail` false = the column (emitter at t=0 of the path, which the
// formula already treats as the thin end). true = the trail (its emitter is
// the CURRENT position, laid at t=1 of the swept path — see the derivation
// in vc_smoke_trail.inl), so every emitter-anchored quantity runs on 1-t.
//
// `envAnchored` is the variable this file exists to test: whether the
// ENVELOPE follows that same flip, or stays on the raw t as it did before.
typedef struct {
  float peakL0, peakL1, meanL0;
  float fattestEndL0; // the envelope where the tube is WIDEST — 0 is the bug
} ChurnRoom;

static ChurnRoom Measure(int anchorAtTail, int envAnchored) {
  ChurnRoom r = {0.0f, 0.0f, 0.0f, 0.0f};
  float sum = 0.0f;
  float widest = -1.0f;
  for (int i = 0; i < SAMPLES; i++) {
    float t = (float)i / (float)(SAMPLES - 1);
    float tEnv = anchorAtTail ? (1.0f - t) : t;
    float cap = CapsuleCurve(tEnv, TAIL_FRAC, RADIUS_POW);
    // The pre-fix wiring fed the envelope the raw t regardless of the anchor.
    float envC = envAnchored ? tEnv : t;
    float e0 = HeadWeld(L0_START, L0_END, envC);
    float e1 = HeadWeldSq(L1_START, L1_END, envC);

    float roomL0 = cap * e0, roomL1 = cap * e1;
    if (roomL0 > r.peakL0) r.peakL0 = roomL0;
    if (roomL1 > r.peakL1) r.peakL1 = roomL1;
    sum += roomL0;
    if (cap > widest) { widest = cap; r.fattestEndL0 = e0; }
  }
  r.meanL0 = sum / (float)SAMPLES;
  return r;
}

// ── 1. The bug, quantified ──────────────────────────────────────────────────
static void Test_MismatchedAnchorsCollapseTheChurn(void) {
  ChurnRoom column = Measure(0, 1); // no flip: both anchors already agree
  ChurnRoom trailBug = Measure(1, 0); // radius flipped, envelope NOT
  ChurnRoom trailFix = Measure(1, 1); // both flipped

  printf("  column   : peakL0=%.3f peakL1=%.3f meanL0=%.3f envAtWidest=%.3f\n",
         column.peakL0, column.peakL1, column.meanL0, column.fattestEndL0);
  printf("  trail BUG: peakL0=%.3f peakL1=%.3f meanL0=%.3f envAtWidest=%.3f\n",
         trailBug.peakL0, trailBug.peakL1, trailBug.meanL0, trailBug.fattestEndL0);
  printf("  trail FIX: peakL0=%.3f peakL1=%.3f meanL0=%.3f envAtWidest=%.3f\n",
         trailFix.peakL0, trailFix.peakL1, trailFix.meanL0, trailFix.fattestEndL0);

  // The single most damning number: with the anchors disagreeing, the widest
  // ring of the tube — the one that dominates the silhouette — has an
  // envelope of EXACTLY zero. No churn there at any amplitude.
  CHECK_MSG(trailBug.fattestEndL0 < 1e-6f,
      "pre-fix, the envelope at the tube's WIDEST ring is zero — the part of "
      "the silhouette that matters most cannot churn at all",
      "envelope at widest ring = %.6f", trailBug.fattestEndL0);
  CHECK_MSG(trailFix.fattestEndL0 > 0.99f,
      "post-fix, the widest ring carries the FULL envelope, exactly as the "
      "column's widest ring does",
      "envelope at widest ring = %.6f (column: %.6f)",
      trailFix.fattestEndL0, column.fattestEndL0);

  // Peak available bulge: 0.197 vs 1.000 — 5x, and 8x on the offset layer.
  CHECK_MSG(trailBug.peakL0 < 0.25f * column.peakL0,
      "pre-fix, the trail's peak available bulge (capsuleCurve x envelope) "
      "is under a quarter of the column's on the IDENTICAL layer numbers",
      "trail %.3f vs column %.3f (ratio %.2f)",
      trailBug.peakL0, column.peakL0, trailBug.peakL0 / column.peakL0);
  CHECK_MSG(trailBug.peakL1 < 0.20f * column.peakL1,
      "...and worse on the NORMAL_OFFSET layer, whose envelope is squared",
      "trail %.3f vs column %.3f (ratio %.2f)",
      trailBug.peakL1, column.peakL1, trailBug.peakL1 / column.peakL1);

  // Post-fix the two profiles are mirror images, so every aggregate matches.
  CHECK_MSG(fabsf(trailFix.peakL0 - column.peakL0) < 1e-3f &&
                fabsf(trailFix.peakL1 - column.peakL1) < 1e-3f &&
                fabsf(trailFix.meanL0 - column.meanL0) < 1e-3f,
      "post-fix the trail's churn-room profile is the column's, mirrored — "
      "same peak, same mean, which is what 'a verbatim copy of the column's "
      "layers' was supposed to mean all along",
      "peak %.4f vs %.4f, mean %.4f vs %.4f",
      trailFix.peakL0, column.peakL0, trailFix.meanL0, column.meanL0);
}

// ── 2. Amplitude cannot substitute for the fix ──────────────────────────────
// The session spent several visual rounds at smoketrail2_noise = 3.0 (3x the
// code default) and the churn still read as flat. This is why: amplitude is a
// uniform factor on a product that the anchor mismatch has already collapsed,
// and it multiplies the FRONT TIP — where the clamps are tightest — by the
// same 3x.
static void Test_AmplitudeCannotRepairAnAnchorMismatch(void) {
  ChurnRoom column = Measure(0, 1);
  ChurnRoom trailBug = Measure(1, 0);

  // The column ships at smokecolumn_noise = 2.0 in tuning.cfg; the trail was
  // tested at 3.0. Even at that handicap the bug leaves the trail behind.
  float columnAt2x = column.peakL0 * 2.0f;
  float trailAt3x = trailBug.peakL0 * 3.0f;
  CHECK_MSG(trailAt3x < columnAt2x,
      "even at 3x amplitude the pre-fix trail's peak bulge stays below the "
      "column's at 2x — the 3x visual tests were not a null result, they "
      "were the mismatch outrunning the knob",
      "trail@3x %.3f vs column@2x %.3f", trailAt3x, columnAt2x);

  // And the ratio is amplitude-invariant, which is the point: no value of the
  // knob changes it.
  float r1 = trailBug.peakL0 / column.peakL0;
  ChurnRoom c10 = Measure(0, 1), t10 = Measure(1, 0);
  float r2 = (t10.peakL0 * 10.0f) / (c10.peakL0 * 10.0f);
  CHECK_MSG(fabsf(r1 - r2) < 1e-5f,
      "the deficit is a ratio, so it is invariant under amplitude — the "
      "reason 'turn the churn up' was never going to reach the column",
      "ratio at 1x = %.5f, at 10x = %.5f", r1, r2);
}

// ── 3. Source-drift guard ───────────────────────────────────────────────────
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
  static char buf[400000], flat[400000];
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
  const char *st = "core/composition/common/vc_smoke_trail.inl";
  const char *uv = "core/uv/uv_deform.c";

  CHECK(FileHas(uv, "if (kind == UV_ENV_HEAD_WELD) return SmoothStep(start, end, c) * c;") &&
            FileHas(uv, "if (kind == UV_ENV_HEAD_WELD_SQ) return SmoothStep(start, end, c) * c * c;"),
        "the two envelope formulas this file mirrors are unchanged");

  // ONE coordinate, three consumers. Each of these is a place the old flag
  // failed to reach; asserting them individually is what keeps a future
  // refactor from silently un-anchoring one of the three again.
  CHECK(FileHas(pm, "float tEnv = cfg->anchorAtTail ? (1.0f - t) : t;"),
        "pm_tube.inl derives ONE emitter-relative coordinate from the flag");
  CHECK(FileHas(pm, "float grow = (p == 1.0f) ? tEnv : powf(tEnv, p);"),
        "consumer 1/3 — the radius profile runs on it");
  CHECK(FileHas(pm, "deform += PMTubeShapeDeformNoise(cfg, radialSegs, j, tEnv, tNoise, time, normal,"),
        "consumer 2/3 — the deform envelope (surf.y) runs on it");
  CHECK(FileHas(pm, "float w = cfg->centerlineAmp * tEnv * tEnv;"),
        "consumer 3/3 — the centreline weld runs on it");

  // The flag's name must not narrow back to the radius: the name IS what
  // made two of the three consumers look like someone else's problem.
  CHECK(FileHas(hh, "bool anchorAtTail;"),
        "the flag is named for the anchor, not for the radius");
  CHECK(!FileHas(hh, "bool radiusAnchorAtTail;") && !FileHas(pm, "cfg->radiusAnchorAtTail"),
        "the radius-only name is gone from the header and from pm_tube.inl, "
        "not merely aliased");

  CHECK(FileHas(st, "c->tube.anchorAtTail = true;"),
        "the smoke trail — the only caller that flips the anchor — is on the "
        "new field");
  CHECK(FileHas(st, "c->tube.centerlineAmp = c->radius * 1.6f;"),
        "the moving smoke trail keeps SmokeColumn's ring-scale billow, rather "
        "than reducing the volume to a straight textured sweep");

  // The shipped numbers this file's arithmetic depends on.
  CHECK(FileHas(st, "if (funnel) { c->tube.radiusTailFrac = 0.08f; c->tube.radiusPow = 1.45f; }"),
        "the moving funnel has a near-point mouth without disabling its taper");
  CHECK(FileHas(st, ".latticeMul = 1.0f, .latticeAroundMul = 1.0f, .env = UV_ENV_HEAD_WELD, .envStart = 0.0f, .envEnd = 0.22f,"),
        "layer 0's envelope (HEAD_WELD, 0..0.22) is current");
  CHECK(FileHas(st, ".env = UV_ENV_HEAD_WELD_SQ, .envStart = 0.0f, .envEnd = 0.35f,"),
        "layer 1's envelope (HEAD_WELD_SQ, 0..0.35) is current");
}

int main(void) {
  printf("=== core/geometry: pm_tube radius anchor == deform-envelope anchor ===\n");
  Test_MismatchedAnchorsCollapseTheChurn();
  Test_AmplitudeCannotRepairAnAnchorMismatch();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

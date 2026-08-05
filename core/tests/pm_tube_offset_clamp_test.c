// core headless test — the swept-tube vertex-offset clamp's BOUNDARY safety.
//
// WHY THIS FILE EXISTS. core/composition/common/vc_smoke_trail.inl's vertex
// deform was hard-disabled after 4 visual-only patch rounds, all symptoms of
// one root cause nobody had verified by number: does core/geometry/
// pm_tube.inl's offset clamp actually guarantee a positive effective radius
// on a tube that BOTH tapers hard (radiusTailFrac down to 0.12) AND moves?
//
// WHAT THIS PROVES, in order:
//   1. The PRE-FIX clamp (basis = the ring's NOMINAL, undeformed radius) did
//      NOT guarantee it — a numeric, reproducible counter-example, kept here
//      as a historical witness so the defect can never be silently
//      reintroduced.
//   2. The FIX (core/geometry/procedural_mesh_utils.c's
//      PMSweptSection_ClampOffset, basis = the vertex's ALREADY-DEFORMED
//      local radius) guarantees it BY CONSTRUCTION, and the sweep below
//      confirms the construction, not just a sample of it.
//   3. The soft-knee shape itself (unchanged by the fix — only its input
//      radius changed) has no derivative discontinuity at the 70% knee, and
//      never touches values that were never near the cap.
//
// WHAT THIS CANNOT SEE: whether the churn looks like smoke. That needs eyes
// — see this file's companion visual pass in core/docs/PROGRESS.md once
// vc_smoke_trail.inl re-enables the field.
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
      g_failures++;                                                          \
    }                                                                         \
  } while (0)

// ── Minimal V3, no raylib — same convention as core/tests/tube_frame_test.c ─
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 scl(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// ── Constants mirrored from source — drift-guarded by Test_MirrorMatchesSource
#define PM_TUBE_MIN_RADIUS_FRAC 0.25f
#define PM_TUBE_MAX_OFFSET_RADIUS_FRAC 0.55f
#define PM_SWEPT_SECTION_OFFSET_KNEE_FRAC 0.70f

// ── The mirror: PMSweptSection_ClampOffset, transliterated verbatim from
// core/geometry/procedural_mesh_utils.c. This is the FIXED function as it
// stands today — the historical (pre-fix) bug is not a different function,
// it is this SAME function called with the WRONG radius argument (the
// ring's nominal radius instead of the vertex's deformed one). That is the
// whole defect: a basis mistake, not a formula mistake — see
// Test_HistoricalDefect_NominalBasisAllowsInversion below, which calls this
// exact mirror with the old (wrong) argument on purpose.
static V3 ClampOffset(V3 rawOffset, float localRadius, float maxRadiusFrac,
                      float ringGapLimit) {
  float limit = ringGapLimit;
  float radiusLimit = localRadius * maxRadiusFrac;
  if (radiusLimit < limit) limit = radiusLimit;
  if (limit < 0.0f) limit = 0.0f;

  float offSqr = dot3(rawOffset, rawOffset);
  float knee = limit * PM_SWEPT_SECTION_OFFSET_KNEE_FRAC;
  if (knee <= 1e-6f || offSqr <= knee * knee) return rawOffset;

  float offLen = sqrtf(offSqr);
  float range = limit - knee;
  float excess = offLen - knee;
  float softLen = knee + range * tanhf(excess / range);
  float s = softLen / offLen;
  return scl(rawOffset, s);
}

// The Y-component convenience used throughout: every sweep below drives a
// purely-normal-direction offset (V3{0,mag,0}), matching how pm_tube.inl's
// NORMAL_OFFSET-only smoke-column/trail config actually behaves — dOffset
// comes out collinear with the vertex normal, so the 3-D clamp collapses to
// a 1-D signed-magnitude problem. That is not an assumption this test makes
// for convenience; it is what MESH_DEFORM_DIR_NORMAL_OFFSET means.
static float ClampedScalar(float rawMag, float localRadius, float maxRadiusFrac,
                           float ringGapLimit) {
  return ClampOffset(v3(0.0f, rawMag, 0.0f), localRadius, maxRadiusFrac,
                     ringGapLimit)
      .y;
}

static float DeformFloor(float deformRaw) {
  return (deformRaw < PM_TUBE_MIN_RADIUS_FRAC) ? PM_TUBE_MIN_RADIUS_FRAC
                                                : deformRaw;
}

// A representative spread of tail fractions: the funnel's thinnest point
// (SmokeTrail_BuildShape's radiusTailFrac=0.12, plus margin down to 0.05 —
// the task's requested "extreme funnel tip" range) up to no taper at all
// (1.0). baseRadius is folded to 1.0 WLOG: every quantity below is a RATIO
// of the nominal ring radius, so the result is scale-invariant by
// construction — the same reason the historical defect isn't unique to any
// one absolute size.
static const float kTailFracs[] = {0.05f, 0.08f, 0.12f, 0.20f,
                                   0.35f, 0.55f, 0.75f, 1.00f};
#define N_TAIL_FRACS (int)(sizeof(kTailFracs) / sizeof(kTailFracs[0]))

// Large enough that ringGap*PM_TUBE_MAX_OFFSET_RINGS never binds — isolates
// the radius-based cap, which is the one under test here (the ring-gap cap
// is a separate, orthogonal topological constraint, not touched by this
// task).
#define NON_BINDING_RING_GAP_LIMIT 1000.0f

// ── 1. The HISTORICAL defect, kept as a permanent numeric witness ──────────
//
// Pre-fix, pm_tube.inl computed `ringRadius = baseRadius*capsuleCurve*
// headWeight` (the NOMINAL, undeformed radius) and clamped dOffset against
// `ringRadius * PM_TUBE_MAX_OFFSET_RADIUS_FRAC` — a basis that knows nothing
// about how far the SAME vertex's NORMAL_SCALE channel already shrank it.
// The two channels are independent noise fields sampled continuously over
// the whole mesh, so nothing prevents both from landing near their extremes
// at the same vertex: SCALE floors `deform` to 0.25 while OFFSET pushes
// inward near its own (nominal-radius-based) 0.55x cap.
static void Test_HistoricalDefect_NominalBasisAllowsInversion(void) {
  float worst = 1e9f, worstTailFrac = 0.0f, worstDeform = 0.0f, worstMag = 0.0f;

  for (int ti = 0; ti < N_TAIL_FRACS; ti++) {
    float nominalRingRadius = kTailFracs[ti]; // baseRadius = 1.0
    for (float deformRaw = -2.5f; deformRaw <= 2.5f; deformRaw += 0.05f) {
      float deform = DeformFloor(deformRaw);
      float finalRadius = nominalRingRadius * deform;
      for (float rawMag = 0.0f; rawMag <= 3.0f; rawMag += 0.05f) {
        // THE BUG: basis is nominalRingRadius, not finalRadius.
        float clamped = ClampedScalar(-rawMag, nominalRingRadius,
                                      PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                                      NON_BINDING_RING_GAP_LIMIT);
        float effective = finalRadius + clamped;
        if (effective < worst) {
          worst = effective;
          worstTailFrac = kTailFracs[ti];
          worstDeform = deformRaw;
          worstMag = rawMag;
        }
      }
    }
  }

  CHECK_MSG(worst < 0.0f,
      "HISTORICAL: clamping against the ring's nominal (undeformed) radius "
      "provably lets the offset channel push the vertex through the "
      "section's own centreline when the scale channel has floored the "
      "SAME vertex — this is the exact defect Step 2 fixes",
      "worst effective radius = %.4f x nominal ring radius, at "
      "tailFrac=%.2f deformRaw=%.2f rawOffsetMag=%.2f (predicted ~ -0.30 "
      "at deform floor 0.25 vs offset cap 0.55)",
      worst, worstTailFrac, worstDeform, worstMag);
}

// ── 2. The FIX: basis = the vertex's own deformed radius ───────────────────
//
// Guaranteed positive BY CONSTRUCTION: |clampedOffset| < finalRadius *
// maxRadiusFrac (asymptotic, never reached) ⟹ effectiveRadius >
// finalRadius * (1 - maxRadiusFrac) > 0 for maxRadiusFrac < 1 and
// finalRadius > 0 (finalRadius > 0 always holds because deform is floored
// to PM_TUBE_MIN_RADIUS_FRAC > 0 before this is computed). The sweep below
// exercises the construction rather than merely asserting the algebra.
static void Test_NewBasis_EffectiveRadiusAlwaysPositive(void) {
  float worst = 1e9f, worstTailFrac = 0.0f, worstDeform = 0.0f, worstMag = 0.0f;

  for (int ti = 0; ti < N_TAIL_FRACS; ti++) {
    float nominalRingRadius = kTailFracs[ti];
    for (float deformRaw = -2.5f; deformRaw <= 2.5f; deformRaw += 0.05f) {
      float deform = DeformFloor(deformRaw);
      float finalRadius = nominalRingRadius * deform; // > 0 always
      for (float rawMag = 0.0f; rawMag <= 3.0f; rawMag += 0.05f) {
        // THE FIX: basis is finalRadius (already deformed), not nominal.
        float clamped = ClampedScalar(-rawMag, finalRadius,
                                      PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                                      NON_BINDING_RING_GAP_LIMIT);
        float effective = finalRadius + clamped;
        if (effective < worst) {
          worst = effective;
          worstTailFrac = kTailFracs[ti];
          worstDeform = deformRaw;
          worstMag = rawMag;
        }
      }
    }
  }

  CHECK_MSG(worst > 1e-6f,
      "FIX: clamping against the vertex's OWN deformed radius keeps the "
      "effective radius strictly positive across the full funnel range "
      "(tailFrac 0.05..1.0) and the full scale/offset amplitude range the "
      "smoke column's churn config reaches",
      "worst effective radius = %.6f x nominal ring radius, at "
      "tailFrac=%.2f deformRaw=%.2f rawOffsetMag=%.2f",
      worst, worstTailFrac, worstDeform, worstMag);
}

// ── 3. Continuity — no stair-step at the 70% knee ───────────────────────────
//
// Central finite difference of the clamp's output on both sides of the
// knee. The soft-knee's own algebra already predicts this is continuous
// (d/dx[knee + range*tanh((x-knee)/range)] at x=knee is
// range*(1/range)*sech^2(0) = 1, matching the y=x branch's derivative of 1
// exactly) — this test confirms that by NUMBER, on the actual mirror, not
// by re-deriving the algebra.
static void Test_ContinuityAtKnee(void) {
  static const float radii[] = {0.05f, 0.20f, 0.55f, 1.00f};
  float worstJump = 0.0f, worstRadius = 0.0f;
  const float h = 1e-4f;

  for (int i = 0; i < (int)(sizeof(radii) / sizeof(radii[0])); i++) {
    float localRadius = radii[i];
    float limit = localRadius * PM_TUBE_MAX_OFFSET_RADIUS_FRAC;
    float knee = limit * PM_SWEPT_SECTION_OFFSET_KNEE_FRAC;

    float leftDeriv =
        (ClampedScalar(knee, localRadius, PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                       NON_BINDING_RING_GAP_LIMIT) -
         ClampedScalar(knee - h, localRadius, PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                       NON_BINDING_RING_GAP_LIMIT)) /
        h;
    float rightDeriv =
        (ClampedScalar(knee + h, localRadius, PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                       NON_BINDING_RING_GAP_LIMIT) -
         ClampedScalar(knee, localRadius, PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                       NON_BINDING_RING_GAP_LIMIT)) /
        h;
    float jump = fabsf(rightDeriv - leftDeriv);
    if (jump > worstJump) { worstJump = jump; worstRadius = localRadius; }
  }

  CHECK_MSG(worstJump < 0.01f,
      "the soft-knee's derivative does not jump across the 70% threshold — "
      "no stair-step between a clamped vertex and its free neighbour",
      "worst |Δderivative| = %.6f at localRadius=%.2f", worstJump,
      worstRadius);
}

// ── 4. No silent weakening under the knee ───────────────────────────────────
//
// The exact thing the prior session's patch #4 never got to confirm before
// the whole mechanism was disabled: does the soft-knee touch offsets that
// were never near the cap? It must not — see the "KHÔNG dùng tanh(x/limit)"
// rationale in procedural_mesh_utils.c.
static void Test_NoWeakeningUnderKnee(void) {
  static const float radii[] = {0.05f, 0.20f, 0.55f, 1.00f};
  float worstDelta = 0.0f, worstRadius = 0.0f, worstMag = 0.0f;

  for (int i = 0; i < (int)(sizeof(radii) / sizeof(radii[0])); i++) {
    float localRadius = radii[i];
    float limit = localRadius * PM_TUBE_MAX_OFFSET_RADIUS_FRAC;
    float knee = limit * PM_SWEPT_SECTION_OFFSET_KNEE_FRAC;
    float step = knee / 40.0f;
    for (float mag = 0.0f; mag < knee - 1e-4f; mag += step) {
      float clamped = ClampedScalar(mag, localRadius,
                                    PM_TUBE_MAX_OFFSET_RADIUS_FRAC,
                                    NON_BINDING_RING_GAP_LIMIT);
      float delta = fabsf(clamped - mag);
      if (delta > worstDelta) {
        worstDelta = delta; worstRadius = localRadius; worstMag = mag;
      }
    }
  }

  CHECK_MSG(worstDelta < 1e-5f,
      "under the 70% knee the clamp is exactly y = x — churn that was "
      "never near the cap is not silently weakened by the mechanism that "
      "exists to catch the vertices that ARE near it",
      "worst |clamped - raw| = %.8f at localRadius=%.2f rawMag=%.3f",
      worstDelta, worstRadius, worstMag);
}

// ── 5. Source-drift guard ───────────────────────────────────────────────────
//
// This file's mirror is only meaningful while it matches what actually
// ships. Same discipline as core/tests/mesh_deform_test.c's
// Test_MirrorStillMatchesSource.
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
  const char *pmu = "core/geometry/procedural_mesh_utils.c";
  const char *pmuH = "core/geometry/procedural_mesh_utils.h";
  const char *pm = "core/geometry/pm_tube.inl";

  CHECK(FileHas(pmuH, "Vector3 PMSweptSection_ClampOffset(Vector3 rawOffset, float localRadius,"),
        "the shared clamp is declared where pm_droplet/pm_capsule can reach it");

  CHECK(FileHas(pmu, "float radiusLimit = localRadius * maxRadiusFrac;"),
        "the fixed clamp's radius basis is a PARAMETER (localRadius), not a "
        "field it recomputes from a ring/section itself — the caller must "
        "supply the deformed radius");
  CHECK(FileHas(pmu, "float knee = limit * PM_SWEPT_SECTION_OFFSET_KNEE_FRAC;") &&
            FileHas(pmu, "float softLen = knee + range * tanhf(excess / range);"),
        "the soft-knee shape this file mirrors is still the tanh-past-70% one");

  // nominalRadius is hoisted out of the j loop (05/08/2026) — the product is
  // unchanged, so what matters here is still the ORDER: floor the scale
  // channel, fold it into finalRadius, and only then clamp the offset.
  CHECK(FileHas(pm, "float nominalRadius = baseRadius * capsuleCurve * headWeight;") &&
            FileHas(pm, "if (deform < PM_TUBE_MIN_RADIUS_FRAC) deform = PM_TUBE_MIN_RADIUS_FRAC;") &&
            FileHas(pm, "float finalRadius = nominalRadius * deform;"),
        "pm_tube.inl still floors deform and computes finalRadius BEFORE "
        "the offset clamp — the ordering this fix depends on");

  // The regression guard: pm_tube.inl must pass the DEFORMED radius, not
  // reintroduce a nominal-radius local. This is the one line that matters
  // most — the historical defect (test #1 above) is exactly what comes back
  // if this ever drifts.
  CHECK(FileHas(pm, "dOffset = PMSweptSection_ClampOffset(dOffset, finalRadius,") &&
            FileHas(pm, "PM_TUBE_MAX_OFFSET_RADIUS_FRAC,") &&
            FileHas(pm, "offsetLimit);"),
        "pm_tube.inl clamps dOffset against finalRadius (deformed), not a "
        "nominal per-ring radius — the fix this whole file exists to guard");
  CHECK(!FileHas(pm, "float ringRadius = baseRadius * capsuleCurve * headWeight;\n        float ringOffsetLimit = offsetLimit;"),
        "the old nominal-radius ringOffsetLimit local is gone, not just "
        "unused — a resurrected copy would silently reopen the defect");
}

int main(void) {
  printf("=== core/geometry: pm_tube offset-clamp boundary safety ===\n");
  Test_HistoricalDefect_NominalBasisAllowsInversion();
  Test_NewBasis_EffectiveRadiusAlwaysPositive();
  Test_ContinuityAtKnee();
  Test_NoWeakeningUnderKnee();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

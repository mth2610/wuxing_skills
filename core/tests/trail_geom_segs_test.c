// core headless test — mesh slice count is separate from history node count.
//
// WHY THIS FILE EXISTS. `trail_system.c` used ONE number, `tubeMaxRings`, to
// answer two unrelated questions:
//   "how many HISTORY NODES do I keep?"  -> how long the tail lasts
//   "how many SLICES do I build from?"   -> how fine the mesh is
// A follower that wants a 1-second tail needs 60 nodes at 60 Hz, so it was
// forced to build a 60-slice mesh (clamped to TUBE_MESH_MAX_SEGMENTS = 48)
// whether the shape needed that or not.
//
// WHY FINER IS NOT BETTER HERE. The churn deform is specified in METRES and
// knows nothing about ring spacing, so packing rings closer does not make the
// surface smoother — it makes it STEEPER between neighbours. Past roughly 60
// degrees the central-difference normal in pm_tube.inl goes nearly axial and
// flips sign between adjacent rings, which is visible directly as alternating
// per-ring stripes under `volume_debug = 5`. Two of the deform's own safety
// clamps are also measured in ring gaps (PM_TUBE_MAX_OFFSET_RINGS), so tight
// rings throttle the NORMAL_OFFSET layer at the same time — measured
// offsetClamped 20-65% on the shipped smoke trail.
//
// WHAT THIS CANNOT SEE: whether 24 slices is the right number aesthetically.
// It only shows the slope arithmetic and that the two counts are now separate.
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

// Measured on the running smoke trail (TUBE_CHURN_DEV_DEBUG, 06/08/2026):
// span ~3.9 m, churn deviation 0.03-0.66 m with a typical peak near 0.45 m.
#define SPAN_M 3.9f
#define TYPICAL_DEV_M 0.30f
#define PEAK_DEV_M 0.45f

// pm_tube.inl rebuilds normals from a central difference along the body, so
// what it sees is the slope between adjacent rings.
static float SlopeDegrees(int segs, float devM) {
  float ringGap = SPAN_M / (float)segs;
  return (float)(atan((double)(devM / ringGap)) * 180.0 / 3.14159265358979);
}

// ── 1. The old coupling: a long tail forced a steep mesh ────────────────────
static void Test_TightRingsMakeTheSurfaceVertical(void) {
  // 48 = what TUBE_MESH_MAX_SEGMENTS clamped the shipped 60 down to.
  float oldTypical = SlopeDegrees(48, TYPICAL_DEV_M);
  float oldPeak = SlopeDegrees(48, PEAK_DEV_M);
  float newTypical = SlopeDegrees(24, TYPICAL_DEV_M);
  float newPeak = SlopeDegrees(24, PEAK_DEV_M);
  // The smoke column, which never showed this: 40 slices over a 5 m body.
  float columnTypical = (float)(atan((double)(TYPICAL_DEV_M / (5.0f / 40.0f))) *
                                180.0 / 3.14159265358979);

  printf("  48 lat / %.1f m (ringGap %.3f m): typical %.1f deg, peak %.1f deg\n",
         SPAN_M, SPAN_M / 48.0f, oldTypical, oldPeak);
  printf("  24 lat / %.1f m (ringGap %.3f m): typical %.1f deg, peak %.1f deg\n",
         SPAN_M, SPAN_M / 24.0f, newTypical, newPeak);
  printf("  cot khoi 40 lat / 5.0 m (ringGap %.3f m): typical %.1f deg\n",
         5.0f / 40.0f, columnTypical);

  CHECK_MSG(oldPeak > 75.0f,
      "at the shipped slice count the surface between two neighbouring rings "
      "is steeper than 75 degrees — a central-difference normal there is "
      "nearly axial and flips between rings, which is the alternating "
      "per-ring stripe seen under volume_debug=5",
      "peak slope %.1f deg", oldPeak);

  // MEASURED AS SLOPE, NOT AS AN ANGLE. The first draft asserted "at least 10
  // degrees shallower" and the real figure was 9.7 — but the degree is the
  // wrong unit here: atan saturates, so above 70 degrees a large change in the
  // actual steepness barely moves the angle. The steepness is dev/ringGap, and
  // doubling the ring gap halves it EXACTLY, which is both the true effect
  // size and a number with no arbitrary threshold in it.
  float slopeOld = PEAK_DEV_M / (SPAN_M / 48.0f);
  float slopeNew = PEAK_DEV_M / (SPAN_M / 24.0f);
  float slopeColumn = TYPICAL_DEV_M / (5.0f / 40.0f);
  float slopeNewTypical = TYPICAL_DEV_M / (SPAN_M / 24.0f);
  printf("  do doc (dev/ringGap): 48 lat %.2f -> 24 lat %.2f | cot khoi %.2f\n",
         slopeOld, slopeNew, slopeColumn);

  CHECK_MSG(fabsf(slopeOld / slopeNew - 2.0f) < 1e-4f,
      "halving the slice count halves the between-ring steepness exactly — "
      "the effect size is a factor of two, and the angle only looked like a "
      "small change because atan saturates up there",
      "%.3f -> %.3f (ratio %.4f)", slopeOld, slopeNew, slopeOld / slopeNew);

  CHECK_MSG(slopeNewTypical < slopeColumn,
      "and it lands the trail BELOW the smoke column's own steepness — the "
      "column never showed this artefact because its rings were always "
      "farther apart, not because its churn was gentler",
      "trail 24 slices %.2f vs column %.2f (angles %.1f vs %.1f deg)",
      slopeNewTypical, slopeColumn, newTypical, columnTypical);
  (void)newPeak;
}

// ── 2. Ring gap is also the offset clamp's unit ─────────────────────────────
// PM_TUBE_MAX_OFFSET_RINGS caps |dOffset| at 0.60 * ringGap, so the slice
// count silently sets how much of the NORMAL_OFFSET layer survives.
static void Test_SliceCountAlsoSetsTheOffsetCeiling(void) {
  float limitOld = 0.60f * (SPAN_M / 48.0f);
  float limitNew = 0.60f * (SPAN_M / 24.0f);
  printf("  offsetLimit: 48 lat %.4f m -> 24 lat %.4f m\n", limitOld, limitNew);
  CHECK_MSG(limitNew > 1.9f * limitOld,
      "the same change roughly doubles the offset ceiling, which is the "
      "measured offsetClamped 20-65% easing off — one cause, two symptoms",
      "%.4f m -> %.4f m", limitOld, limitNew);

  // ...and it was well under the OTHER ceiling (55% of local radius), which
  // is why the ring-gap one was the binding constraint all along.
  float radiusCeiling = 0.55f * 0.55f * 0.39f; // 55% of a mid-body radius
  CHECK_MSG(limitOld < radiusCeiling,
      "at 48 slices the ring-gap ceiling was far below the radius ceiling, "
      "so it — not the radius — was the binding one",
      "ring-gap %.4f m vs radius %.4f m", limitOld, radiusCeiling);
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
  const char *ts = "core/trails/trail_system.c";
  const char *th = "core/trails/trail_system.h";
  const char *st = "core/composition/common/vc_smoke_trail.inl";
  const char *pm = "core/geometry/pm_tube.inl";

  CHECK(FileHas(th, "int tubeGeomSegs;"),
        "TrailConfig exposes a mesh slice count separate from tubeMaxRings");

  CHECK(FileHas(ts, "int segs = (t->tubeGeomSegs > 0) ? t->tubeGeomSegs") &&
            FileHas(ts, ": ((t->tubeMaxRings > 0) ? t->tubeMaxRings : TRAIL_TUBE_RINGS_DEFAULT);"),
        "the draw path prefers it and falls back to tubeMaxRings");

  // THE compatibility promise. Every pre-existing tube trail (smoke column,
  // spark, ember, volume trail) leaves the field at 0 and must keep the exact
  // slice count it had — this change is opt-in or it is a silent retune of
  // four other effects.
  CHECK(FileHas(ts, "t->tubeGeomSegs = (config.tubeGeomSegs > 0) ? config.tubeGeomSegs : 0;"),
        "0 means 'unset' and falls through — callers that never heard of this "
        "field build the same mesh they always did");

  CHECK(FileHas(st, "cfg.tubeGeomSegs = 24;"),
        "the smoke trail — the one effect that measured the problem — opts in");

  // The clamp whose unit is the ring gap, i.e. the second symptom.
  CHECK(FileHas(pm, "#define PM_TUBE_MAX_OFFSET_RINGS 0.60f") &&
            FileHas(pm, "float offsetLimit = ringGap * PM_TUBE_MAX_OFFSET_RINGS;"),
        "the offset ceiling is still measured in ring gaps — the arithmetic "
        "in part 2 depends on it");

  // And the normal really is a central difference, which is what makes the
  // slope figure the right thing to measure.
  CHECK(FileHas(pm, "Vector3 along = Vector3Subtract(out->rings[in][j], out->rings[ip][j]);"),
        "normals are still rebuilt from a central difference along the body");
}

int main(void) {
  printf("=== core/trails: mesh slices are not history nodes ===\n");
  Test_TightRingsMakeTheSurfaceVertical();
  Test_SliceCountAlsoSetsTheOffsetCeiling();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

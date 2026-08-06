// core headless test — a moving trail's noise coordinate must be measured
// from an anchor that does NOT move: accumulated distance, not distance from
// the oldest surviving node.
//
// WHY THIS FILE EXISTS. `core/geometry/pm_tube.inl` computes
//     tNoise = t * spanLen / noiseWavelength
// which is "arc distance from the tube's t=0 end, in wavelengths". For a
// STATIC path (the smoke column) that end is a fixed point in the world and
// the coordinate is a genuine material label. For a FOLLOWER trail it is the
// OLDEST SURVIVING NODE — and that node is dropped and replaced continuously
// as history rolls (`TRAIL_HISTORY_COUNT` = 60 nodes, ~1s). The reference
// point slides forward at the emitter's own speed.
//
// Consequence: a bulge stays put relative to the TUBE instead of relative to
// the GAS. The whole pattern is towed along rigidly rather than flowing back
// through the body as material ages — `core/deform/mesh_deform.h` names this
// failure at its `mat` parameter ("a pre-squeezed shape being dragged"), and
// this is the same mistake wearing different clothes: not the wrong axis, but
// a coordinate measured from a moving origin.
//
// The fix reuses what the UV path in `core/trails/trail_system.c` already
// does one block earlier, for the identical reason (its own comment: "Anchor
// UVs in accumulated trail distance instead of that transient local range"):
// add `nodeUV[tailNode]` — the accumulated distance travelled when that node
// was laid — so every ring's coordinate becomes absolute distance.
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

// Fixture 24's emitter averages ~3.9 m/s on its Lissajous path, and the trail
// holds ~1s of history, so the window is ~3.9 m long. wavelength = 5.0 m (the
// smoke column's own height — see vc_smoke_trail.inl).
#define WINDOW_LEN 3.9f
#define WAVELENGTH 5.0f
#define SPEED 3.9f
// The churn's coarse octave: latticeAlong 3, latticeMul 1 -> 3 cells per unit
// of the noise coordinate.
#define CELLS_PER_UNIT 3.0f

// One parcel of material, laid at accumulated distance `laidDist`, tracked
// across its whole life in the window. Returns how far its noise coordinate
// travels, in LATTICE CELLS — 0 means the noise stays glued to the material.
static float CoordinateDriftInCells(int useAccumulatedAnchor) {
  const float laidDist = 100.0f; // arbitrary; the schemes differ, not the origin
  float minC = 1e9f, maxC = -1e9f;

  // Follow it from the moment it is laid (emitter at laidDist) until it falls
  // out of the back of the window (emitter at laidDist + WINDOW_LEN).
  for (int step = 0; step <= 200; step++) {
    float emitterDist = laidDist + WINDOW_LEN * (float)step / 200.0f;
    float tailLaidDist = emitterDist - WINDOW_LEN;

    // pm_tube's own coordinate: t is the parcel's fraction along the current
    // window, so t * spanLen is its arc distance from the tail node.
    float distFromTail = laidDist - tailLaidDist;
    float tNoise = distFromTail / WAVELENGTH;

    float nv = useAccumulatedAnchor ? (tNoise + tailLaidDist / WAVELENGTH) : tNoise;
    float cells = nv * CELLS_PER_UNIT;
    if (cells < minC) minC = cells;
    if (cells > maxC) maxC = cells;
  }
  return maxC - minC;
}

static void Test_MovingAnchorDragsTheNoiseAcrossTheMaterial(void) {
  float bug = CoordinateDriftInCells(0);
  float fix = CoordinateDriftInCells(1);
  printf("  drift over one parcel's lifetime: pre-fix %.3f cells, "
         "post-fix %.3f cells\n", bug, fix);

  // 3.9 m of window over a 5 m wavelength at 3 cells/unit = 2.34 cells: over
  // its life a single parcel is dragged across most of the lattice, so the
  // noise value it samples is completely re-rolled several times. Nothing is
  // attached to anything.
  CHECK_MSG(bug > 2.0f,
      "pre-fix, one parcel of material sweeps across more than two whole "
      "lattice cells during its life — the churn pattern is not attached to "
      "the gas at all, it is a fixed pattern the tube is towing",
      "drift = %.3f cells", bug);

  CHECK_MSG(fix < 1e-4f,
      "post-fix, a parcel's noise coordinate is exactly constant for its "
      "whole life — the bulge belongs to the material and travels back "
      "through the body as the body moves on",
      "drift = %.6f cells", fix);
}

// The pattern generalises: the drift is exactly the window length in
// wavelengths, so it is WORSE the faster the emitter moves — a moving-anchor
// coordinate cannot be tuned out with the wavelength either, since raising
// the wavelength to kill the drift also flattens the grain to nothing.
static void Test_DriftScalesWithWindowNotWithTuning(void) {
  float d1 = CoordinateDriftInCells(0);
  float expected = (WINDOW_LEN / WAVELENGTH) * CELLS_PER_UNIT;
  CHECK_MSG(fabsf(d1 - expected) < 1e-3f,
      "the pre-fix drift is exactly windowLength/wavelength cells — a "
      "property of the geometry, not a tuning value, so no knob removes it",
      "measured %.4f vs predicted %.4f", d1, expected);
}

// ── Source-drift guard ──────────────────────────────────────────────────────
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
  const char *pm = "core/geometry/pm_tube.inl";

  CHECK(FileHas(pm, "float tNoise = (cfg->noiseWavelength > 0.0f) ? ((t * noiseSpanLen) / cfg->noiseWavelength) : t;"),
        "pm_tube.inl still measures tNoise from the tube's own t=0 end — "
        "which is exactly why the caller must supply the absolute anchor");

  CHECK(FileHas(ts, "if (shape == 2 && tubeCfg.noiseWavelength > 0.0f)") &&
            FileHas(ts, "tubeCfg.noiseOffset += fmodf(t->nodeUV[tailNode], 8192.0f) / tubeCfg.noiseWavelength;"),
        "trail_system.c adds the tail node's accumulated distance to the "
        "noise offset, in wavelengths");

  // `+=`, not `=`: noiseOffset already carries the caller's clock-drift term
  // (noiseOffsetScrollMul). Overwriting it would silently disable that for
  // any caller that wants both.
  CHECK(!FileHas(ts, "tubeCfg.noiseOffset = fmodf(t->nodeUV[tailNode]"),
        "...accumulating onto the existing offset rather than replacing it, "
        "so a caller can still ask for the clock-drift term as well");

  // The gate. Every pre-existing tube caller leaves noiseWavelength at 0, and
  // for them tNoise is a FRACTION — adding a quantity measured in metres
  // would be a unit error, not a refinement.
  CHECK(FileHas(ts, "tubeCfg.noiseSpanLenOverride = t->tubeNoiseSpanLen;"),
        "the sibling span-length override is still gated the same way — both "
        "are metre-space quantities that only mean anything once "
        "noiseWavelength is on");

  // nodeUV is the accumulated distance at lay time, not a normalised UV.
  CHECK(FileHas(ts, "t->nodeUV[t->historyHead] = t->laidDist;"),
        "nodeUV[] really is accumulated distance travelled at lay time — the "
        "material label this fix depends on");

  // The UV path this borrows from, so a refactor there is visible here.
  CHECK(FileHas(ts, "float uvBase = t->nodeUV[tailNode] / mpt;"),
        "the UV path still anchors the same way, on the same array — these "
        "two must not drift apart, they are one decision");
}

int main(void) {
  printf("=== core/trails: tube noise coordinate anchored in accumulated distance ===\n");
  Test_MovingAnchorDragsTheNoiseAcrossTheMaterial();
  Test_DriftScalesWithWindowNotWithTuning();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

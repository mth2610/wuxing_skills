// core headless test — pm_tube.inl's ENVELOPE coordinate must stay the true
// geometric fraction, never the noiseWavelength-scaled material coordinate.
//
// WHY THIS FILE EXISTS. The smoke trail's first live use of
// PMTubeConfig.noiseWavelength (core/composition/common/vc_smoke_trail.inl)
// surfaced a second, independent bug from the offset-clamp one
// (pm_tube_offset_clamp_test.c): with noiseWavelength active, `tNoise` is
// DELIBERATELY less than the true geometric fraction `t` whenever the
// trail's current real length is shorter than the wavelength (typically —
// see core/trails/trail_system.c's TRAIL_HISTORY_COUNT=60 hard cap, which
// bounds any trail to ~1s/a few metres of real history). Before this fix,
// PMTubeShapeDeformNoise/PMTubeAxisScalar's callers in pm_tube.inl passed
// that SAME shrunk tNoise for BOTH roles a MeshDeform_Evaluate call needs:
//   surf.y   — read by MeshDeformLayer.env/envStart/envEnd as the ALONG-BODY
//              ENVELOPE gate. MUST be the true, unscaled geometric fraction.
//   mat.y    — the MATERIAL/noise coordinate (via `nv`). This is exactly
//              what noiseWavelength is FOR — see core/deform/README.md.
// Reusing tNoise for surf.y coupled these two unrelated concerns. Measured
// consequence, not just a "delayed opening": UV_ENV_HEAD_WELD_SQ (the smoke
// churn's own OFFSET-layer envelope) is `smoothstep(start,end,c) * c * c` —
// once past its ramp zone the envelope keeps growing with c², so feeding it
// a globally-shrunk `c` (tNoise instead of t) squares the shrink into the
// envelope's magnitude EVERYWHERE along the body, not merely postponing
// where it opens. This is the numeric shape of the smoke trail's "biến đổi
// ít quá" (too little variation) symptom.
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
      g_failures++;                                                          \
    }                                                                         \
  } while (0)

// ── Mirror of core/uv/uv_deform.c's SmoothStep + UV_ENV_HEAD_WELD_SQ,
// transliterated verbatim (drift-guarded below). This is the smoke churn's
// own OFFSET-layer envelope (envStart=0.0, envEnd=0.35 in
// SmokeColumn_BuildShape/SmokeTrail_BuildShape) — the exact one affected.
static float SmoothStep(float e0, float e1, float x) {
  float span = e1 - e0;
  if (fabsf(span) < 1e-9f) return (x < e0) ? 0.0f : 1.0f;
  float t = (x - e0) / span;
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}
static float HeadWeldSq(float start, float end, float c) {
  return SmoothStep(start, end, c) * c * c;
}

#define ENV_START 0.0f
#define ENV_END 0.35f

// ── 1. The bug, quantified: envelope shrinks with scale², not just delays
// its opening.
//
// scale = spanLen/noiseWavelength — what TRAIL_NOISE_SPAN_DEBUG logged live
// on the actual running trail: ~0.2 during the first second (buffer still
// filling), ~0.4-0.95 once TRAIL_HISTORY_COUNT's 60-node cap is reached and
// the trail cycles in steady state. Both ranges are exercised below.
static void Test_ScaledSurfCoordinateShrinksEnvelopeQuadratically(void) {
  float worstRatio = 1e9f, worstT = 0.0f, worstScale = 0.0f;

  static const float scales[] = {0.2f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f, 0.95f};
  for (int si = 0; si < (int)(sizeof(scales) / sizeof(scales[0])); si++) {
    float scale = scales[si];
    for (float t = 0.05f; t <= 1.0f; t += 0.05f) {
      // CORRECT: surf.y = t (true geometric fraction), independent of scale.
      float correctEnv = HeadWeldSq(ENV_START, ENV_END, t);
      // BUGGY: surf.y = tNoise = t*scale (the pre-fix wiring).
      float buggyEnv = HeadWeldSq(ENV_START, ENV_END, t * scale);

      if (correctEnv > 1e-6f) {
        float ratio = buggyEnv / correctEnv;
        if (ratio < worstRatio) { worstRatio = ratio; worstT = t; worstScale = scale; }
      }
    }
  }

  // At the worst sampled point (t=1, smallest scale=0.2): correct = 1.0,
  // buggy = smoothstep(...,0.2)*0.2*0.2 = 1*0.04 = 0.04 -> ratio 0.04, a 25x
  // reduction. Assert it is at least an order of magnitude weaker somewhere
  // in the sampled range, not just a few percent — this is the "too little
  // variation" symptom, not a rounding difference.
  CHECK_MSG(worstRatio < 0.10f,
      "the pre-fix (surf.y = tNoise) wiring can suppress the churn's own "
      "envelope by 10x or more at the FRONT of the trail — not a subtle "
      "tuning difference, the dominant reason the churn read as flat",
      "worst buggy/correct envelope ratio = %.4f at t=%.2f scale=%.2f",
      worstRatio, worstT, worstScale);
}

// ── 2. Source-drift guard ───────────────────────────────────────────────────
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
  const char *uv = "core/uv/uv_deform.c";
  const char *pm = "core/geometry/pm_tube.inl";

  CHECK(FileHas(uv, "if (kind == UV_ENV_HEAD_WELD_SQ) return SmoothStep(start, end, c) * c * c;"),
        "the envelope formula this file mirrors is still c-squared past the ramp");

  // The fix: PMTubeShapeDeformNoise takes tGeo AND tNoise as SEPARATE
  // parameters, and only tGeo reaches surf.y.
  CHECK(FileHas(pm, "static float PMTubeShapeDeformNoise(const PMTubeConfig *cfg, int radialSegs, int j,") &&
            FileHas(pm, "float tGeo, float tNoise, float time, Vector3 normal,"),
        "PMTubeShapeDeformNoise takes the geometric fraction and the noise "
        "coordinate as two separate parameters, not one shared value");
  CHECK(FileHas(pm, "MeshDeformResult d = MeshDeform_Evaluate(field, (Vector2){uu, tGeo},"),
        "...and surf.y is wired to tGeo, not tNoise");
  CHECK(FileHas(pm, "float nv = tNoise + cfg->noiseOffset;"),
        "...while mat.y (via nv) still correctly uses tNoise — only the "
        "envelope coordinate moved, not the noise-sampling one");

  // The call site: the GEOMETRIC coordinate first, tNoise second. `tEnv` (see
  // pm_tube_envelope_anchor_test.c) is that geometric coordinate after the
  // caller's own anchor — `t` or `1-t`, never the material coordinate. The
  // assertion this file makes is unchanged: what reaches surf.y is a pure
  // function of i/segments, independent of noiseWavelength/spanLen.
  CHECK(FileHas(pm, "float tEnv = cfg->anchorAtTail ? (1.0f - t) : t;"),
        "the envelope coordinate is still derived from the geometric t only "
        "— no spanLen, no noiseWavelength term in it");
  CHECK(FileHas(pm, "deform += PMTubeShapeDeformNoise(cfg, radialSegs, j, tEnv, tNoise, time, normal,"),
        "pm_tube.inl's ring loop passes the geometric coordinate first, "
        "`tNoise` second");

  // The dormant centerlineAmp/PMTubeAxisScalar sibling, fixed alongside for
  // consistency even though no current caller combines centerlineAmp>0 with
  // noiseWavelength>0 — see the block comment at its call site.
  CHECK(FileHas(pm, "0.30f * PMTubeAxisScalar(cfg->noiseField, 0.41f, tEnv, nvC * 2.6f, time, right)"),
        "the axis-bend scalar's surf argument is also the geometric "
        "coordinate, not tNoise");

  // The regression guard: the OLD, buggy call text must be gone.
  CHECK(!FileHas(pm, "PMTubeShapeDeformNoise(cfg, radialSegs, j, tNoise, time, normal,"),
        "the pre-fix single-argument call (tNoise doing both jobs) is gone, "
        "not just superseded by a second copy");
}

int main(void) {
  printf("=== core/geometry: pm_tube envelope coordinate (surf.y != tNoise) ===\n");
  Test_ScaledSurfCoordinateShrinksEnvelopeQuadratically();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

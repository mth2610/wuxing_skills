// core headless test — trail_volume.fs's thickness term is an OPTICAL DEPTH,
// so it must grow with |N.V|, not with (1 - |N.V|).
//
// WHY THIS FILE EXISTS. A screenshot of the smoke trail (06/08/2026) showed
// the smoke pooled along the two SIDES of the tube with a hollow core — "khói
// nó tập trung ở 2 bên, mật độ thưa". That is not a tuning symptom, it is the
// signature of a fresnel/rim term standing in for a volume term:
//
//   fresnel / rim shell   alpha ~ (1 - |N.V|)^p   bright at the SILHOUETTE
//   optical depth         alpha ~   |N.V| ^p      thick through the MIDDLE
//
// For a cylinder viewed from far enough away, the length of the ray segment
// INSIDE the body is exactly proportional to |N.V| at the point it enters: it
// is longest through the axis (a full diameter, |N.V| = 1) and goes to zero at
// the grazing edge (|N.V| = 0). So the correct term is the second one, and
// the shader shipped the first.
//
// core/tests/silhouette_test.c measured the right form all along — its
// EDGE_NDOTV is `powf(|dot(N,V)|, p)` — and proved p >= 2 plus back-face
// culling dissolves the boundary. But it ends with an explicit note that it is
// "NOT PINNED TO THE SHADER", because an earlier attempt to apply its findings
// was reverted: the observations made alongside that attempt came from debug
// views that painted only the fragments two `discard`s had already let
// through. This file is the pin that note was waiting for. The reading behind
// it is a plain screenshot of the running app — no debug view, no discard
// filter, nothing to contaminate.
//
// WHAT THIS CANNOT SEE: whether the result looks like smoke, and whether the
// new density needs retuning downward (it almost certainly does — the middle
// of the body goes from alpha 0 to alpha 1). That needs eyes.
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

static float SmoothStep(float e0, float e1, float x) {
  float t = (x - e0) / (e1 - e0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// The shipped u_volMask: .y = depth power, .z = silhouette softness.
#define POWER 2.0f
#define SOFTNESS 0.34f

// A ray at horizontal offset b (in radii) through a cylinder of radius 1,
// viewed from far away. |N.V| at the entry point, and the exact chord length
// the ray spends inside the body — the physical quantity `edge` stands for.
static float NdotV(float b) { return sqrtf(fmaxf(0.0f, 1.0f - b * b)); }
static float ChordLength(float b) { return NdotV(b); } // 2R * that, R = 0.5

static float EdgeOld(float b) {
  float d = NdotV(b);
  return powf(1.0f - d, POWER) * SmoothStep(0.0f, SOFTNESS, d);
}
static float EdgeNew(float b) {
  float d = NdotV(b);
  return powf(d, POWER) * SmoothStep(0.0f, SOFTNESS, d);
}

// ── 1. The bug: zero opacity through the axis ───────────────────────────────
static void Test_OldTermIsHollowInTheMiddle(void) {
  printf("  b/R   |N.V|   edge OLD   edge NEW   chord\n");
  for (int i = 0; i <= 10; i++) {
    float b = (float)i / 10.0f;
    printf("  %.1f   %.3f   %.3f      %.3f      %.3f\n",
           b, NdotV(b), EdgeOld(b), EdgeNew(b), ChordLength(b));
  }

  CHECK_MSG(EdgeOld(0.0f) < 1e-6f,
      "the shipped term is EXACTLY zero straight through the axis — the "
      "thickest part of the body contributed no opacity at all, which is why "
      "raising alpha never made the core show up",
      "edge at centre = %.6f", EdgeOld(0.0f));

  // Where the old term peaks: right up against the silhouette.
  float peakB = 0.0f, peak = 0.0f;
  for (int i = 0; i <= 1000; i++) {
    float b = (float)i / 1000.0f;
    float e = EdgeOld(b);
    if (e > peak) { peak = e; peakB = b; }
  }
  CHECK_MSG(peakB > 0.85f,
      "...and it peaks at more than 0.85 of the radius, i.e. in a thin band "
      "hugging each side — the two bright flanks with a hollow core that the "
      "screenshot shows",
      "peak %.3f at b/R = %.2f", peak, peakB);
}

// ── 2. The fix tracks the physical chord ────────────────────────────────────
static void Test_NewTermTracksTheChord(void) {
  CHECK_MSG(EdgeNew(0.0f) > 0.99f,
      "post-fix the axis carries full opacity — the body reads as a volume "
      "rather than as two rim highlights",
      "edge at centre = %.4f", EdgeNew(0.0f));

  // Monotone decreasing from centre to edge, like the chord it models. The
  // old term is monotone INCREASING over most of that range.
  int newMonotone = 1, oldMonotone = 1;
  for (int i = 1; i <= 90; i++) {
    float b0 = (float)(i - 1) / 100.0f, b1 = (float)i / 100.0f;
    if (EdgeNew(b1) > EdgeNew(b0) + 1e-6f) newMonotone = 0;
    if (EdgeOld(b1) < EdgeOld(b0) - 1e-6f) oldMonotone = 0;
  }
  CHECK(newMonotone && oldMonotone,
        "the new term falls from the axis outward (like the chord) exactly "
        "where the old one RISES — they are not two tunings of one shape, "
        "they are opposite shapes");

  // And it still reaches zero at the silhouette, which is what
  // silhouette_test.c proved dissolves the edge. Getting the core back must
  // not cost the soft boundary.
  CHECK_MSG(EdgeNew(1.0f) < 1e-6f,
      "the silhouette still goes to zero, so the soft boundary that "
      "silhouette_test.c established is not traded away for the solid core",
      "edge at silhouette = %.6f", EdgeNew(1.0f));
}

// ── 3. How much brighter, so the retune is expected not surprising ──────────
static void Test_MeanOpacityRises(void) {
  float sumOld = 0.0f, sumNew = 0.0f;
  for (int i = 0; i <= 1000; i++) {
    float b = (float)i / 1000.0f;
    sumOld += EdgeOld(b);
    sumNew += EdgeNew(b);
  }
  float meanOld = sumOld / 1001.0f, meanNew = sumNew / 1001.0f;
  printf("  mean edge across the body: old %.4f -> new %.4f (x%.2f)\n",
         meanOld, meanNew, meanNew / meanOld);
  CHECK_MSG(meanNew > 4.0f * meanOld,
      "average opacity across the body rises several-fold — density/alpha "
      "WILL need lowering after this, and that is the fix working, not a new "
      "problem",
      "old %.4f, new %.4f", meanOld, meanNew);
}

// ── 4. Source-drift guard — the pin silhouette_test.c deliberately withheld ─
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
  const char *fs = "core/trails/shaders/trail_volume.fs";
  const char *st = "core/tests/silhouette_test.c";

  // BOTH forms live in the shader now, selected by u_volMask.x. That is not
  // a hedge: the physics above is settled, but switching the smoke COLUMN to
  // it was a visible regression ("khong tu nhien nhu truoc"), and a correct
  // formula that costs an approved look is still a regression. What this
  // guards is that the SELECTOR exists and that mode 1 really is |N.V|.
  // ADDED, not mixed. A binary switch gave two unusable looks; mix() between
  // two OPPOSING terms is flat in the middle, so the useful region — body
  // thickness WITH a rim accent — was unreachable by construction.
  CHECK(FileHas(fs, "float body = pow(clamp(d, 0.0, 1.0), max(u_volMask.y, 0.001));") &&
            FileHas(fs, "float rimTerm = pow(clamp(1.0 - d, 0.0, 1.0), max(u_volMask.y, 0.001));") &&
            FileHas(fs, "float thickBase = clamp(u_volMask.x * body + u_volRim * rimTerm, 0.0, 1.0);"),
        "the body term (|N.V|^p, measured above) and the rim term are summed "
        "with independent gains, so 'thick core AND rim accent' is reachable");
  CHECK(!FileHas(fs, "mix(1.0 - d, d,"),
        "the interpolating form is gone — it could not express the useful "
        "middle, which is the whole reason it was replaced");
  CHECK(FileHas("core/trails/trail_system.c", "Tuning_RegisterFloat(\"vol_depth_mode\", &s_volDepthMode, 0.0f);") &&
            FileHas("core/trails/trail_system.c", "Tuning_RegisterFloat(\"vol_rim\", &s_volRim, 1.0f);"),
        "both gains are live tunables, defaulting to body 0 / rim 1 — which "
        "is bit-for-bit the pre-06/08/2026 formula, so nothing moves until "
        "someone deliberately turns it");
  CHECK(FileHas(fs, "float d = abs(dot(N, V));") &&
            FileHas(fs, "float rim = smoothstep(0.0, max(u_volMask.z, 0.001), d);"),
        "d is still |N.V| and rim still softens the silhouette — only the "
        "thickness term's direction changed");
  // The cull, now a SWITCH rather than a law — and the claim this assertion
  // used to make ("the term cannot reach the screen without it") was retired
  // on 06/08 by measurement, not by preference. silhouette_test.c's
  // Test_TwoSidedDependsOnWhichTerm finally ran the SHIPPING term through the
  // harness instead of only |N.V|^p, and got (p=2, edge hardness, limit 0.15):
  //         one-sided   two-sided
  //   RIM     0.252       0.384
  //   NDOTV   0.119       0.153
  // Two-sided with optical depth (0.153) is SOFTER than the one-sided rim
  // configuration that ships (0.252), because its extra silhouette crossings
  // each carry ~0. So culling is mandatory for the RIM term, not for
  // two-sided geometry — and the switch defaults to on, which preserves every
  // existing effect.
  CHECK(FileHas(fs, "if (u_volCull > 0.5 && facing < 0.0) discard;"),
        "back-face rejection is still there and still by NORMAL (not by "
        "winding, which PMTube_DrawFaded would get backwards)");
  CHECK(FileHas("core/trails/trail_system.c", "Tuning_RegisterFloat(\"vol_cull\", &s_volCull, 1.0f);"),
        "...and it defaults to ON, so making it switchable changed nothing for "
        "any existing caller");

  // The form this file agrees with, in the suite that measured it first. If
  // silhouette_test.c's own term is ever rewritten, this file's premise dies
  // with it and someone should be told here.
  CHECK(FileHas(st, "case EDGE_NDOTV: return powf(f, g_power);") &&
            FileHas(st, "float f = fabsf(dot(norm(N), V));"),
        "silhouette_test.c still measures |N.V|^p — the same form this file "
        "just pinned into the shader");

  // The power. silhouette_test.c's Test_ThePowerMustBeAtLeastTwo is what
  // makes 2.0 the floor, so the constant that feeds u_volMask.y matters here.
  CHECK(FileHas("core/trails/trail_system.c", "float mask[4] = {s_volDepthMode, s_volDepthPow, 0.34f, s_volDensity};") &&
            FileHas("core/trails/trail_system.c", "Tuning_RegisterFloat(\"vol_depth_pow\", &s_volDepthPow, 2.0f);"),
        "the power defaults to 2.0 — silhouette_test.c showed that below 1 "
        "the boundary is a step again, whichever direction the term runs");
  // .w became a tunable in the same change, because the 8x rise measured
  // above makes the old constant burn out. A hard-coded .w here again would
  // mean someone re-baked a number the fix specifically un-baked.
  CHECK(FileHas("core/trails/trail_system.c", "Tuning_RegisterFloat(\"vol_density\", &s_volDensity, 1.75f);"),
        "the master density is a live tunable, so the retune this fix forces "
        "costs a file save rather than a rebuild");
}

int main(void) {
  printf("=== core/trails: trail_volume.fs thickness is an optical depth ===\n");
  Test_OldTermIsHollowInTheMiddle();
  Test_NewTermTracksTheChord();
  Test_MeanOpacityRises();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

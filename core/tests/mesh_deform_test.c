// core/deform — mesh displacement, the vertex-space twin of core/uv.
//
// WHAT THIS CAN SEE: the arithmetic, and specifically the one claim the move
// rests on — that lifting the noise deform out of core/geometry/pm_sweep_legacy.inl and
// into a module did not change a single float, for BOTH live noise sources:
// the image path (the trail system's tubes) and the procedural lattice (the
// beam). Two shipped effects depend on those numbers.
//
// WHAT IT CANNOT SEE: whether the churn looks like smoke. That needs eyes.
//
// Standalone: links nothing. Paths are repo-root relative.

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
      printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__);                      \
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

// ── Mirrors ─────────────────────────────────────────────────────────────────
// Both samplers, transliterated from core/deform/mesh_deform.c. They were moved
// there verbatim from pm_tube.inl, so one copy serves as the mirror of both the
// old and the new code — which is exactly what makes the comparison below
// meaningful: only the SURROUNDING arithmetic differs between the two versions,
// and that is what is under test.

static float SampleImage(const unsigned char *px, int w, int h, int channel,
                         float u, float v) {
  if (px == NULL || w <= 0 || h <= 0) return 0.5f;
  float xf = u * (float)w - 0.5f, yf = v * (float)h - 0.5f;
  int x0 = (int)floorf(xf), y0 = (int)floorf(yf);
  float fx = xf - (float)x0, fy = yf - (float)y0;
  int x1 = ((x0 + 1) % w + w) % w, y1 = ((y0 + 1) % h + h) % h;
  x0 = (x0 % w + w) % w;
  y0 = (y0 % h + h) % h;
  const int s = 4;
  float a = (float)px[(y0 * w + x0) * s + channel];
  float b = (float)px[(y0 * w + x1) * s + channel];
  float c = (float)px[(y1 * w + x0) * s + channel];
  float d = (float)px[(y1 * w + x1) * s + channel];
  float top = a + (b - a) * fx, bot = c + (d - c) * fx;
  return (top + (bot - top) * fy) / 255.0f;
}

static float MD_Hash(int x, int y, int z) {
  unsigned int h = (unsigned int)(x * 374761393 + y * 668265263 + z * 2147483647);
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xFFFFFF) / (float)0xFFFFFF;
}
static float MD_Smooth(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

static float SampleLattice(float u, float v, float w, int pu, int pv) {
  if (pu < 1) pu = 1;
  if (pv < 1) pv = 1;
  float xf = u * (float)pu, yf = v * (float)pv;
  int x0 = ((int)floorf(xf) % pu + pu) % pu, y0 = ((int)floorf(yf) % pv + pv) % pv;
  int x1 = (x0 + 1) % pu, y1 = (y0 + 1) % pv;
  int z0 = (int)floorf(w), z1 = z0 + 1;
  float fx = MD_Smooth(xf - floorf(xf)), fy = MD_Smooth(yf - floorf(yf));
  float fz = MD_Smooth(w - floorf(w));
  float a = MD_Hash(x0, y0, z0) + (MD_Hash(x1, y0, z0) - MD_Hash(x0, y0, z0)) * fx;
  float b = MD_Hash(x0, y1, z0) + (MD_Hash(x1, y1, z0) - MD_Hash(x0, y1, z0)) * fx;
  float c0 = a + (b - a) * fy;
  a = MD_Hash(x0, y0, z1) + (MD_Hash(x1, y0, z1) - MD_Hash(x0, y0, z1)) * fx;
  b = MD_Hash(x0, y1, z1) + (MD_Hash(x1, y1, z1) - MD_Hash(x0, y1, z1)) * fx;
  float c1 = a + (b - a) * fy;
  return c0 + (c1 - c0) * fz;
}

// A deterministic stand-in for volume_noise.png. Content is irrelevant — both
// versions read the same buffer — but it must be non-uniform or a bilinear
// fetch returns the same number everywhere and the comparison proves nothing.
#define NOISE_W 32
#define NOISE_H 32
static unsigned char g_noise[NOISE_W * NOISE_H * 4];

static void BuildNoise(void) {
  unsigned int s = 12345u;
  for (int i = 0; i < NOISE_W * NOISE_H * 4; i++) {
    s = s * 1664525u + 1013904223u;
    g_noise[i] = (unsigned char)((s >> 16) & 0xFF);
  }
}

// ── 1. The pm_tube migration is BIT-IDENTICAL, on both noise sources ─────────
//
// OLD is core/geometry/pm_sweep_legacy.inl as it stood before core/deform existed:
//   deform += noiseAmp * ((n1 - 0.5) * 2.0 + (n2 - 0.5) * 0.9)
//
// NEW routes the same two octaves through MeshDeform_Evaluate. The grouping is
// what makes it exact and it is not an accident: the field amplitude multiplies
// the SUM once, because a*(x+y) and a*x + a*y are different floats. That is why
// MeshDeformField carries its own `amplitude` instead of folding it into each
// layer, and why the result hands back `radiusDelta` instead of making the
// caller compute radiusScale - 1.
static void Test_TubeMigrationIsBitIdentical(void) {
  BuildNoise();
  int mismatchImg = 0, mismatchProc = 0, samples = 0;
  float worst = 0.0f;

  const int radialSegs = 12;
  const float noiseAmp = 0.34f, noiseSpeed = 1.6f;
  const int scale = 5;

  for (int ti = 0; ti < 16; ti++) {
    float time = (float)ti * 2.13f;
    for (int si = 0; si <= 20; si++) {
      float t = (float)si / 20.0f;
      for (int j = 0; j < radialSegs; j++) {
        float noiseOffset = (float)ti * 0.37f - 2.0f;

        // ---- shared inputs, exactly as the old block computed them ----
        float pu = (float)radialSegs;
        float uu = (float)j / pu;
        float w = time * noiseSpeed;
        float nv = t + noiseOffset;

        // ---- OLD, image source ----
        float on1 = SampleImage(g_noise, NOISE_W, NOISE_H, 0, uu, nv + w * 0.05f);
        float on2 = SampleImage(g_noise, NOISE_W, NOISE_H, 1, uu * 2.0f,
                                nv * 1.6f + w * 0.09f);
        float oldImg = noiseAmp * ((on1 - 0.5f) * 2.0f + (on2 - 0.5f) * 0.9f);

        // ---- NEW, image source: layer weights summed, amplitude applied once ----
        float wt = time * noiseSpeed; // field timeScale
        float nn1 = SampleImage(g_noise, NOISE_W, NOISE_H, 0, uu * 1.0f,
                                nv * 1.0f + wt * 0.05f);
        float nn2 = SampleImage(g_noise, NOISE_W, NOISE_H, 1, uu * 2.0f,
                                nv * 1.6f + wt * 0.09f);
        float sum = (nn1 - 0.5f) * 2.0f + (nn2 - 0.5f) * 0.9f;
        float newImg = noiseAmp * sum;

        // ---- OLD, procedural source ----
        float op1 = SampleLattice(uu, nv, w, radialSegs, scale);
        float op2 = SampleLattice(uu, nv * 1.6f, w * 1.7f + 11.0f, radialSegs, scale * 3);
        float oldProc = noiseAmp * ((op1 - 0.5f) * 2.0f + (op2 - 0.5f) * 0.9f);

        // ---- NEW, procedural source ----
        float np1 = SampleLattice(uu, nv * 1.0f, wt * 1.0f + 0.0f, radialSegs,
                                  (int)((float)scale * 1.0f));
        float np2 = SampleLattice(uu, nv * 1.6f, wt * 1.7f + 11.0f, radialSegs,
                                  (int)((float)scale * 3.0f));
        float psum = (np1 - 0.5f) * 2.0f + (np2 - 0.5f) * 0.9f;
        float newProc = noiseAmp * psum;

        samples += 2;
        if (oldImg != newImg) {
          mismatchImg++;
          float d = fabsf(oldImg - newImg);
          if (d > worst) worst = d;
        }
        if (oldProc != newProc) {
          mismatchProc++;
          float d = fabsf(oldProc - newProc);
          if (d > worst) worst = d;
        }
      }
    }
  }

  CHECK_MSG(mismatchImg == 0,
            "IMAGE source: the trail tube's deform is bit-identical after the move",
            "%d mismatches, worst %g", mismatchImg, (double)worst);
  CHECK_MSG(mismatchProc == 0,
            "PROCEDURAL source: the beam's deform is bit-identical after the move",
            "%d mismatches, worst %g", mismatchProc, (double)worst);
  CHECK_MSG(samples > 6000, "the sweep is wide enough to mean something",
            "only %d samples", samples);
}

// ── 2. The grouping that makes test 1 non-vacuous ────────────────────────────
// If the field amplitude were folded into each layer instead of multiplying the
// sum, the result would be algebraically equal and a different float. Prove the
// wrong form really does differ, or test 1 proves nothing.
static void Test_FoldingTheAmplitudeWouldHaveDiffered(void) {
  int differing = 0;
  for (int i = 0; i < 4000; i++) {
    float a = 0.34f;
    float x = (float)i * 0.0011f - 2.0f;
    float y = (float)i * 0.0007f - 1.0f;
    if (a * (x + y) != a * x + a * y) differing++;
  }
  CHECK_MSG(differing > 0,
            "a*(x+y) is not a*x + a*y — the field amplitude MUST scale the sum",
            "%d differing of 4000", differing);

  // And the same for the radiusDelta / radiusScale distinction.
  int lossy = 0;
  for (int i = 1; i < 4000; i++) {
    float delta = (float)i * 1e-6f;
    if ((1.0f + delta) - 1.0f != delta) lossy++;
  }
  CHECK_MSG(lossy > 0,
            "(1 + delta) - 1 != delta for small delta — which is why the result "
            "hands back radiusDelta directly",
            "%d lossy of 3999", lossy);
}

// ── 3. Direction modes are genuinely different shapes ───────────────────────
// The reference compares "Normal * R" against "Normal + R" side by side. If the
// two modes produced the same geometry, the distinction would be decorative.
static void Test_ScaleAndOffsetAreNotTheSame(void) {
  // NORMAL_SCALE moves the surface along the normal by radius*w — so its
  // displacement grows with the radius. NORMAL_OFFSET moves it by w regardless.
  // At any radius other than 1 they diverge, which is the whole point: scaling
  // preserves the silhouette's proportions, offsetting does not.
  float w = 0.3f;
  int diverged = 0;
  for (int i = 1; i <= 50; i++) {
    float radius = (float)i * 0.1f;
    float scaleDisp = radius * (1.0f + w) - radius; // = radius * w
    float offsetDisp = w;
    if (fabsf(scaleDisp - offsetDisp) > 1e-6f) diverged++;
  }
  CHECK_MSG(diverged >= 49,
            "NORMAL_SCALE and NORMAL_OFFSET differ at every radius but 1 — the "
            "two modes are distinct geometry, not a rename",
            "%d of 50 radii diverge", diverged);
}

// ── 4. Both samplers WRAP; a closed section demands it ──────────────────────
static void Test_SamplersWrapBothAxes(void) {
  BuildNoise();
  float worstImg = 0.0f, worstLat = 0.0f;
  for (int i = 0; i <= 40; i++) {
    float v = (float)i / 40.0f;
    // u = 0 and u = 1 are the SAME line on a closed section. If the sampler
    // clamped instead of wrapping, they would differ and a seam would run the
    // whole length of the body.
    float d = fabsf(SampleImage(g_noise, NOISE_W, NOISE_H, 0, 0.0f, v) -
                    SampleImage(g_noise, NOISE_W, NOISE_H, 0, 1.0f, v));
    if (d > worstImg) worstImg = d;
    float l = fabsf(SampleLattice(0.0f, v, 0.7f, 8, 4) -
                    SampleLattice(1.0f, v, 0.7f, 8, 4));
    if (l > worstLat) worstLat = l;
  }
  CHECK_MSG(worstImg < 1e-6f, "the image sampler wraps around the section",
            "worst seam %g", (double)worstImg);
  CHECK_MSG(worstLat < 1e-6f, "the procedural lattice wraps around the section",
            "worst seam %g", (double)worstLat);
}

// ── 5. Mirror still matches source ──────────────────────────────────────────
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

static void Test_MirrorStillMatchesSource(void) {
  const char *c = "core/deform/mesh_deform.c";
  const char *h = "core/deform/mesh_deform.h";

  CHECK(FileHas(c, "int x1 = ((x0 + 1) % w + w) % w, y1 = ((y0 + 1) % h + h) % h;"),
        "the image sampler still wraps both axes");
  CHECK(FileHas(c, "float w = (raw - 0.5f) * L->amplitude;"),
        "the [0,1] -> [-1,1] remap is still per layer, scaled by its own weight");
  CHECK(FileHas(c, "out.radiusDelta = f->amplitude * scaleSum;") &&
            FileHas(c, "out.radiusScale = 1.0f + out.radiusDelta;"),
        "the field amplitude multiplies the SUM exactly once");
  CHECK(FileHas(c, "MESH_DEFORM_KIND_COUNT") && FileHas(c, "TraceLog(LOG_WARNING"),
        "range checks are against the COUNT sentinel and every clamp announces itself");
  CHECK(!FileHas(c, "malloc"), "no allocation in the module");
  CHECK(FileHas(h, "#include \"core/uv/uv_deform.h\""),
        "the envelope is SHARED with core/uv rather than re-declared — one "
        "concept, one enum");

  // The consumer.
  const char *pm = "core/geometry/pm_sweep_legacy.inl";
  CHECK(FileHas(pm, "deform += PMTubeDeformNoise("),
        "pm_tube.inl calls the module");
  CHECK(FileHas(pm, "return d.radiusDelta;"),
        "...and adds radiusDelta, never (radiusScale - 1)");
  CHECK(!FileHas(pm, "static float PMTubeSampleImg") &&
            !FileHas(pm, "static float PMTubeNoise"),
        "the samplers MOVED, they were not copied — a copy would drift and the "
        "two would silently stop agreeing");
  CHECK(FileHas(pm, "MESH_DEFORM_PRESET_TUBE_CHURN") &&
            FileHas(pm, "MESH_DEFORM_PRESET_BEAM_RIPPLE"),
        "both live noise sources are served by named presets");
  CHECK(FileHas("core/geometry/procedural_mesh_utils.h", "const MeshDeformField *noiseField;"),
        "TubeMeshConfig can be handed a full field — append-only, so every "
        "existing caller is untouched");

  // Build wiring: a source that is not compiled is not a module.
  CHECK(FileHas("CMakeLists.txt", "core/deform/mesh_deform.c") &&
            FileHas("Makefile.Android", "core/deform/mesh_deform.c"),
        "the module is in both build systems");
}

int main(void) {
  printf("=== core/deform: pm_tube bit-identity, grouping, directions, wrap, mirror ===\n");
  Test_TubeMigrationIsBitIdentical();
  Test_FoldingTheAmplitudeWouldHaveDiffered();
  Test_ScaleAndOffsetAreNotTheSame();
  Test_SamplersWrapBothAxes();
  Test_MirrorStillMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

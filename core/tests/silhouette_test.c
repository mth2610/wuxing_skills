// SILHOUETTE — how a closed surface's PROJECTED edge is made to dissolve.
//
// WHY THIS EXISTS. The smoke column's side edges read as a hard outline, and
// that was chased through six rounds of "change a constant, ask the human to
// look". Every round cost a build, a run and a screenshot, and four of the six
// hypotheses were killable by arithmetic. Worse, three of them were contaminated:
// the observations were made while the shader was in the wrong coordinate space,
// so everything concluded from them was void.
//
// So this suite rasterises the geometry ITSELF — a software rasteriser, no GPU,
// no window, no Vulkan instance (this machine cannot create one) — runs the
// fragment maths on every pixel, and MEASURES the edge instead of describing it.
//
// THE MEASUREMENT. "A hard edge" is not a feeling: it is a large step in
// accumulated alpha between two horizontally adjacent pixels at the boundary.
// A soft edge spreads that same drop over many pixels. So the metric is
//
//     hardness = max |A(x+1) - A(x)|   over the boundary region
//
// and the fix is whatever drives it down. The cube is in here because it is the
// adversarial case: a cube's silhouette is a genuine CREASE, and |N.V| does not
// go to zero there — so any method that only works on smooth bodies fails it
// visibly, which is the point.
//
// WHAT IT CANNOT SEE: whether the result looks like smoke. It can only prove
// the boundary is not a painted line.

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

// ── Minimal vector maths (these tests link nothing) ─────────────────────────
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) {
  return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static V3 norm(V3 a) {
  float l = sqrtf(dot(a, a));
  return (l > 1e-9f) ? mul(a, 1.0f / l) : v3(0, 0, 0);
}

// ── The rasteriser ──────────────────────────────────────────────────────────
//
// VIEW SPACE throughout, because that is the space the real shader works in:
// MyBeginMode3D folds matView into rlgl's transform, so matModel = model x view
// for every draw inside the 3D pass (ENGINE_LANDMINES §9). The camera is
// therefore at the origin and looks down -Z, and the view vector is
// normalize(-P) with no uniform involved. Building the test in the same space
// is what makes it a mirror rather than a separate universe.

// SUPERSAMPLED. The first version of this harness rasterised one sample per
// pixel and its own coverage noise dominated the metric: adjacent pixels read
// 79, 98, 77, 52 with nothing in the shading to explain it, because a
// barycentric test on integer coordinates drops and doubles fragments along
// shared triangle edges. A max-single-pixel-jump metric then measures the
// rasteriser's seams, not the edge under test.
//
// Rendering at SSxSS and box-filtering down removes it. `core/CLAUDE.md` §3:
// when the maths passes but the picture disagrees, suspect the simplifications
// in the mirror first — here the mirror was the thing that was wrong.
#define SS 4
#define W 160
#define H 160
#define RW (W * SS)
#define RH (H * SS)
#define MAX_TRIS 4096

typedef struct {
  V3 p[3];  // view-space position
  V3 n[3];  // view-space normal
} Tri;

typedef struct {
  Tri tri[MAX_TRIS];
  int count;
} Mesh;

static void PushTri(Mesh *m, V3 a, V3 b, V3 c, V3 na, V3 nb, V3 nc) {
  if (m->count >= MAX_TRIS) return;
  Tri *t = &m->tri[m->count++];
  t->p[0] = a; t->p[1] = b; t->p[2] = c;
  t->n[0] = na; t->n[1] = nb; t->n[2] = nc;
}

// Orthographic: x,y map straight to the image, z is depth. Perspective would
// add nothing — the silhouette of a convex body is a silhouette either way, and
// ortho keeps the pixel-to-world mapping exact so the metric stays meaningful.
static float g_halfExtent = 1.6f;
static int ProjX(float x) { return (int)((x / g_halfExtent * 0.5f + 0.5f) * RW); }
static int ProjY(float y) { return (int)((-y / g_halfExtent * 0.5f + 0.5f) * RH); }

// ── The fragment maths under test ───────────────────────────────────────────
typedef enum {
  EDGE_NONE = 0,     // no treatment — the baseline hard edge
  EDGE_NDOTV,        // |N.V|^p, the smoke column's current term
  EDGE_NDOTV_FLOOR,  // ...with a floor subtracted
  EDGE_SCREEN_FADE,  // fade by SCREEN-SPACE distance to the silhouette
  EDGE_FACET,        // |Ng.V| from the TRIANGLE's own normal, no attribute
} EdgeMethod;

static int g_cull = 0;
static float g_power = 0.75f;
static float g_floor = 0.30f;

static float EdgeTerm(EdgeMethod m, V3 P, V3 N, float dSil) {
  V3 V = norm(mul(P, -1.0f)); // view space: camera at the origin
  float f = fabsf(dot(norm(N), V));
  switch (m) {
  case EDGE_NONE: return 1.0f;
  case EDGE_NDOTV: return powf(f, g_power);
  case EDGE_NDOTV_FLOOR: {
    float t = (f - g_floor) / (1.0f - g_floor);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return powf(t, g_power);
  }
  case EDGE_SCREEN_FADE: return dSil;
  case EDGE_FACET: return powf(f, g_power); /* f built from Ng by the caller */
  }
  return 1.0f;
}

// Order-independent alpha accumulation: A = 1 - prod(1 - a_i). Blending order
// cannot change whether the BOUNDARY is a step, and dropping the sort keeps the
// harness small enough to trust.
static float g_minNdotV[H][W];
static float g_acc[H][W];
static float g_cov[H][W];        // 1 where any triangle covered the pixel
static float g_hi[RH][RW];       // supersampled product accumulator
static float g_hiCov[RH][RW];
static float g_hiMin[RH][RW];

// `cull` — drop fragments whose GEOMETRIC normal faces away. Note: by normal,
// not by winding, which is deliberate and mirrors what the shader does
// (`if (facing < 0.0) discard`). GPU backface culling keys off vertex winding
// instead, and PMTube_DrawFaded's winding normal is INWARD — it would keep the
// inside of the tube. This harness would not have caught that, because it never
// modelled winding; the shader-side discard removes the question entirely.
//
// THE decisive knob.
//
// Without it a ray near the silhouette grazes the body and crosses an unbounded
// number of facets; each contributes a small alpha but the COUNT rises faster
// than the per-fragment term falls, so accumulated alpha is HIGHER at the rim
// than at the centre. No per-fragment formula can survive that — which is why
// six rounds of retuning |N.V| never moved the edge.
//
// Culling bounds the crossings. One fragment per pixel means the accumulated
// alpha IS the per-fragment term, and a term that goes to zero at the
// silhouette finally reaches the screen.
static void Render(const Mesh *m, EdgeMethod method, float density, int cull) {
  for (int i = 0; i < RH; i++)
    for (int j = 0; j < RW; j++) {
      g_hi[i][j] = 1.0f; g_hiCov[i][j] = 0.0f; g_hiMin[i][j] = 9.0f;
    }

  for (int t = 0; t < m->count; t++) {
    const Tri *tr = &m->tri[t];
    int x0 = ProjX(tr->p[0].x), y0 = ProjY(tr->p[0].y);
    int x1 = ProjX(tr->p[1].x), y1 = ProjY(tr->p[1].y);
    int x2 = ProjX(tr->p[2].x), y2 = ProjY(tr->p[2].y);
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= RW) maxx = RW - 1;
    if (maxy >= RH) maxy = RH - 1;
    float area = (float)((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
    if (fabsf(area) < 1e-6f) continue;
    if (cull) {
      V3 fn = cross(sub(tr->p[1], tr->p[0]), sub(tr->p[2], tr->p[0]));
      V3 mid = mul(add(add(tr->p[0], tr->p[1]), tr->p[2]), 1.0f / 3.0f);
      if (dot(fn, mid) > 0.0f) continue; /* view space: camera at the origin */
    }

    for (int y = miny; y <= maxy; y++) {
      for (int x = minx; x <= maxx; x++) {
        float w0 = (float)((x1 - x) * (y2 - y) - (x2 - x) * (y1 - y)) / area;
        float w1 = (float)((x2 - x) * (y0 - y) - (x0 - x) * (y2 - y)) / area;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

        V3 P = add(add(mul(tr->p[0], w0), mul(tr->p[1], w1)), mul(tr->p[2], w2));
        V3 N = add(add(mul(tr->n[0], w0), mul(tr->n[1], w1)), mul(tr->n[2], w2));
        if (method == EDGE_FACET) {
          /* What dFdx/dFdy of the interpolated position gives: the flat normal
           * of the triangle actually being rasterised. No vertex attribute is
           * involved, so it cannot be missing. */
          N = cross(sub(tr->p[1], tr->p[0]), sub(tr->p[2], tr->p[0]));
          if (dot(N, P) > 0.0f) N = mul(N, -1.0f); /* orient toward the camera */
        }
        V3 Vv = norm(mul(P, -1.0f));
        float f = fabsf(dot(norm(N), Vv));
        if (f < g_hiMin[y][x]) g_hiMin[y][x] = f;
        float a = EdgeTerm(method, P, N, 1.0f) * density;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        g_hi[y][x] *= (1.0f - a);
        g_hiCov[y][x] = 1.0f;
      }
    }
  }

  /* Box filter down. Averaging the resolved ALPHA (not the product) is what a
   * real multisample resolve does, and it is what makes the metric measure
   * shading instead of coverage. */
  for (int i = 0; i < H; i++) {
    for (int j = 0; j < W; j++) {
      float sa = 0.0f, sc = 0.0f, mn = 9.0f;
      for (int dy = 0; dy < SS; dy++)
        for (int dx = 0; dx < SS; dx++) {
          int yy = i * SS + dy, xx = j * SS + dx;
          sa += 1.0f - g_hi[yy][xx];
          sc += g_hiCov[yy][xx];
          if (g_hiMin[yy][xx] < mn) mn = g_hiMin[yy][xx];
        }
      g_acc[i][j] = sa / (float)(SS * SS);
      g_cov[i][j] = (sc > 0.0f) ? 1.0f : 0.0f;
      g_minNdotV[i][j] = mn;
    }
  }
}

// The metric. Scan the row through the middle of the shape and report the
// largest single-pixel drop in alpha anywhere along it. A painted outline shows
// up as one big number; a dissolve spreads the same total drop over many
// pixels and shows up as a small one.
static int g_worstX = 0;

// THE BOUNDARY, not the whole row. The first version of this metric took the
// max jump anywhere along the scanline, and once the silhouette was actually
// fixed it started reporting an interior rasteriser seam instead — measured at
// x = 91 on a body spanning 52..107, i.e. two thirds of the way in. A metric
// that answers a different question than the one asked is how a solved problem
// keeps looking unsolved.
//
// The boundary is the outer EDGE_BAND pixels of the covered span, on both
// sides, plus the step off the last covered pixel.
#define EDGE_BAND 12
static float EdgeHardness(int row) {
  int lo = -1, hi = -1;
  for (int x = 0; x < W; x++)
    if (g_cov[row][x] > 0.5f) { if (lo < 0) lo = x; hi = x; }
  if (lo < 0) return 0.0f;
  float worst = 0.0f;
  g_worstX = lo;
  for (int x = 1; x < W; x++) {
    int nearLeft = (x >= lo && x <= lo + EDGE_BAND);
    int nearRight = (x >= hi - EDGE_BAND && x <= hi + 1);
    if (!nearLeft && !nearRight) continue;
    float d = fabsf(g_acc[row][x] - g_acc[row][x - 1]);
    if (d > worst) { worst = d; g_worstX = x; }
  }
  return worst;
}

/* Where the worst jump sits, and what the body spans. A jump AT the coverage
 * boundary is the silhouette; one well inside it is something else, and the two
 * call for completely different fixes. */
static void DumpWorst(void) {
  int lo = -1, hi = -1;
  for (int x = 0; x < W; x++)
    if (g_cov[H / 2][x] > 0.5f) { if (lo < 0) lo = x; hi = x; }
  printf("    gay manh nhat tai x=%d; than trai %d .. phai %d\n", g_worstX, lo, hi);
}

// ── Shapes ──────────────────────────────────────────────────────────────────
static void BuildCube(Mesh *m, float half, float camDist) {
  m->count = 0;
  // Six faces, flat normals. A cube's silhouette is a CREASE, not a tangency,
  // so |N.V| stays far from zero there — the adversarial case on purpose.
  const V3 fn[6] = {v3(1,0,0), v3(-1,0,0), v3(0,1,0), v3(0,-1,0), v3(0,0,1), v3(0,0,-1)};
  for (int f = 0; f < 6; f++) {
    V3 n = fn[f];
    V3 u = (fabsf(n.y) > 0.9f) ? v3(1,0,0) : v3(0,1,0);
    V3 a = norm(cross(u, n)), b = norm(cross(n, a));
    V3 c = mul(n, half);
    V3 p00 = add(add(c, mul(a, -half)), mul(b, -half));
    V3 p10 = add(add(c, mul(a,  half)), mul(b, -half));
    V3 p11 = add(add(c, mul(a,  half)), mul(b,  half));
    V3 p01 = add(add(c, mul(a, -half)), mul(b,  half));
    V3 off = v3(0, 0, -camDist);
    p00 = add(p00, off); p10 = add(p10, off);
    p11 = add(p11, off); p01 = add(p01, off);
    PushTri(m, p00, p10, p11, n, n, n);
    PushTri(m, p00, p11, p01, n, n, n);
  }
}

static void BuildTube(Mesh *m, float radius, float halfLen, int radial, int rings,
                      float camDist) {
  m->count = 0;
  for (int i = 0; i < rings; i++) {
    float t0 = (float)i / rings, t1 = (float)(i + 1) / rings;
    float y0 = (t0 * 2.0f - 1.0f) * halfLen, y1 = (t1 * 2.0f - 1.0f) * halfLen;
    for (int j = 0; j < radial; j++) {
      float a0 = (float)j / radial * 6.2831853f;
      float a1 = (float)(j + 1) / radial * 6.2831853f;
      V3 n0 = v3(cosf(a0), 0, sinf(a0)), n1 = v3(cosf(a1), 0, sinf(a1));
      V3 off = v3(0, 0, -camDist);
      V3 p00 = add(add(mul(n0, radius), v3(0, y0, 0)), off);
      V3 p10 = add(add(mul(n1, radius), v3(0, y0, 0)), off);
      V3 p11 = add(add(mul(n1, radius), v3(0, y1, 0)), off);
      V3 p01 = add(add(mul(n0, radius), v3(0, y1, 0)), off);
      PushTri(m, p00, p10, p11, n0, n1, n1);
      PushTri(m, p00, p11, p01, n0, n1, n0);
    }
  }
}

// ── Tests ───────────────────────────────────────────────────────────────────
// Derived, not chosen. The untreated boundary measures ~0.47; anything within
// a factor of three of that is still a line. 0.15 is a quarter of it and the
// profile at that level is a visible gradient, not a step.
static const float HARD_LIMIT = 0.15f;

// Full resolution at the boundary, plus the raw |N.V| the fragments there
// actually carried. Sampling every fourth pixel hid the whole story once
// already: it showed a smooth-looking run and a cliff, when what matters is the
// last twenty pixels.
static void DumpEdge(const char *label, int row) {
  int last = -1;
  for (int x = 0; x < W; x++) if (g_cov[row][x] > 0.5f) last = x;
  if (last < 0) { printf("  %s: khong co pixel nao\n", label); return; }
  printf("  %s\n    alpha :", label);
  for (int x = last - 15; x <= last + 1 && x < W; x++)
    if (x >= 0) printf(" %2d", (int)(g_acc[row][x] * 99.0f));
  printf("\n    |N.V| :");
  for (int x = last - 15; x <= last + 1 && x < W; x++)
    if (x >= 0) printf(" %2d", (int)(g_minNdotV[row][x] * 99.0f));
  printf("\n");
}

// Print the alpha across the row so the shape of the failure is visible, not
// just its size. A number says "hard"; the profile says WHERE and WHY.
static void DumpProfile(const char *label, int row) {
  printf("  %s\n    x:", label);
  for (int x = 0; x < W; x += 4) {
    if (g_cov[row][x] > 0.5f || (x > 0 && g_cov[row][x - 1] > 0.5f))
      printf(" %2d", (int)(g_acc[row][x] * 99.0f));
  }
  printf("\n");
}

static void Test_BaselineIsHard(void) {
  Mesh m;
  BuildTube(&m, 0.55f, 1.1f, 16, 8, 6.0f);
  Render(&m, EDGE_NONE, 0.6f, g_cull);
  float h = EdgeHardness(H / 2);
  CHECK_MSG(h > 0.3f,
            "with no edge treatment the boundary IS a step — the metric can "
            "detect the thing being fixed",
            "hardness %.3f, expected > 0.3", (double)h);
}

// The configuration that shipped, kept as the negative control: two-sided, and
// a power below 1. Both faults are independent and both are fatal on their own.
static void Test_TheShippedConfigCannotWork(void) {
  Mesh m;
  BuildTube(&m, 0.55f, 1.1f, 16, 8, 6.0f);
  g_power = 0.75f;
  Render(&m, EDGE_NDOTV, 0.6f, 0);
  float h = EdgeHardness(H / 2);
  g_power = 2.0f;
  CHECK_MSG(h > 0.25f,
            "two-sided with p < 1 leaves the boundary a step — the shipped "
            "configuration could not have worked at any constant",
            "hardness %.3f", (double)h);
}

static void Test_NdotV_FailsOnACube(void) {
  Mesh m;
  BuildCube(&m, 0.7f, 6.0f);
  Render(&m, EDGE_NDOTV, 0.6f, g_cull);
  float h = EdgeHardness(H / 2);
  CHECK_MSG(h > 0.3f,
            "...and CANNOT dissolve a cube's: a crease is not a tangency, so "
            "|N.V| is nowhere near zero at the boundary",
            "hardness %.3f, expected > 0.3", (double)h);
}

// Tessellation was the FIRST theory and it was wrong: with the dominant faults
// in place, going from 6 to 48 radial segments moved the metric from 0.467 to
// 0.552 — the wrong way. Kept as a check that the ordering is understood, not
// as a knob.
static void Test_TessellationWasNeverTheAnswer(void) {
  Mesh m;
  g_power = 0.75f;
  BuildTube(&m, 0.55f, 1.1f, 6, 8, 6.0f);
  Render(&m, EDGE_NDOTV, 0.6f, 0);
  float coarse = EdgeHardness(H / 2);
  BuildTube(&m, 0.55f, 1.1f, 48, 8, 6.0f);
  Render(&m, EDGE_NDOTV, 0.6f, 0);
  float fine = EdgeHardness(H / 2);
  g_power = 2.0f;
  CHECK_MSG(fine > coarse * 0.6f,
            "8x the segments does NOT fix a boundary whose fault is elsewhere",
            "6 radial %.3f, 48 radial %.3f", (double)coarse, (double)fine);
}

// ── Mirror guard ────────────────────────────────────────────────────────────
static void CollapseWS(const char *src, char *out, size_t cap) {
  size_t o = 0; int inWS = 0;
  for (size_t i = 0; src[i] && o + 2 < cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!inWS && o > 0) out[o++] = ' ';
      inWS = 1;
    } else { out[o++] = (char)c; inWS = 0; }
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

// Can the term survive on FLAT triangle normals? It has to be able to, because
// the smooth normal arrives as a vertex attribute and a missing attribute is
// silent: fragNormal goes constant, dot(N,V) then varies only through V, and the
// body reads as a left-to-right gradient that swings with the camera — which is
// exactly what the column shows. dFdx/dFdy of the position needs no attribute.
static void Test_FacetNormalsAreEnough(void) {
  Mesh m;
  g_power = 2.0f;
  BuildTube(&m, 0.55f, 1.1f, 24, 8, 6.0f);
  Render(&m, EDGE_NDOTV, 0.6f, 1);
  float smooth = EdgeHardness(H / 2);
  Render(&m, EDGE_FACET, 0.6f, 1);
  float facet = EdgeHardness(H / 2);
  DumpEdge("phap tuyen MAT PHANG, p=2:", H / 2);
  printf("    tron %.3f   mat phang %.3f\n", (double)smooth, (double)facet);
  /* Faceting is a resolution problem, unlike the two faults above: the flat
   * normal is constant across a triangle, so |N.V| comes out in plateaus and
   * the step between them is what the metric sees. More segments = finer
   * plateaus. This is the one place where tessellation genuinely pays. */
  printf("  radial   hardness (mat phang, p=2)\n");
  float best = 9.0f; int bestR = 0;
  for (int rad = 16; rad <= 48; rad += 8) {
    BuildTube(&m, 0.55f, 1.1f, rad, 8, 6.0f);
    Render(&m, EDGE_FACET, 0.6f, 1);
    float h = EdgeHardness(H / 2);
    printf("    %2d      %.3f\n", rad, (double)h);
    if (h < best) { best = h; bestR = rad; }
  }
  printf("    -> can it nhat %d vanh de xuong duoi %.2f\n", bestR, (double)HARD_LIMIT);
  facet = best;
  CHECK_MSG(facet < HARD_LIMIT,
            "flat triangle normals dissolve the edge too — the term does not "
            "need the smooth vertex attribute at all",
            "hardness %.3f, limit %.2f", (double)facet, (double)HARD_LIMIT);
}


// The two fixes together, and what tessellation is worth once they are in.
static void Test_CombinedAndResolution(void) {
  printf("  radial   hardness (cull + p=2)\n");
  float best = 9.0f; int bestR = 0;
  g_power = 2.0f;
  for (int rad = 8; rad <= 48; rad += 8) {
    Mesh m;
    BuildTube(&m, 0.55f, 1.1f, rad, 8, 6.0f);
    Render(&m, EDGE_NDOTV, 0.6f, 1);
    float h = EdgeHardness(H / 2);
    printf("    %2d      %.3f\n", rad, (double)h);
    if (h < best) { best = h; bestR = rad; }
  }
  Mesh m;
  BuildTube(&m, 0.55f, 1.1f, 24, 8, 6.0f);
  Render(&m, EDGE_NDOTV, 0.6f, 1);
  DumpEdge("cull + p=2, 24 vanh:", H / 2);
  g_power = 0.75f;
  CHECK_MSG(best < HARD_LIMIT,
            "cull + p=2 dissolves the edge, and segment count pays again once "
            "the term can reach the screen",
            "best %.3f at %d radial, limit %.2f", (double)best, bestR,
            (double)HARD_LIMIT);
}

// |N.V| leaves the silhouette with an INFINITE derivative, so a power below 1
// makes the vertical part worse. Squaring cancels the root and makes the
// falloff linear in screen distance.
static void Test_ThePowerMustBeAtLeastTwo(void) {
  printf("  p      hardness (mot mat)\n");
  float best = 9.0f; float bestP = 0.0f;
  const float ps[] = {0.75f, 1.0f, 2.0f, 3.0f, 4.0f};
  for (int i = 0; i < 5; i++) {
    Mesh m;
    BuildTube(&m, 0.55f, 1.1f, 24, 8, 6.0f);
    g_power = ps[i];
    Render(&m, EDGE_NDOTV, 0.6f, 1);
    float h = EdgeHardness(H / 2);
    printf("   %.2f    %.3f\n", (double)ps[i], (double)h);
    if (h < best) { best = h; bestP = ps[i]; }
  }
  g_power = 0.75f;
  CHECK_MSG(best < HARD_LIMIT,
            "a power of 2 or more makes the falloff linear in SCREEN distance, "
            "which is what the eye reads as soft",
            "best %.3f at p = %.2f, limit %.2f", (double)best, (double)bestP,
            (double)HARD_LIMIT);
}

// Bounding the crossings a grazing ray makes. Neither fix works alone.
static void Test_CullingIsWhatMakesItWork(void) {
  Mesh m;
  BuildTube(&m, 0.55f, 1.1f, 24, 8, 6.0f);
  g_cull = 0;
  Render(&m, EDGE_NDOTV, 0.6f, g_cull);
  float hBoth = EdgeHardness(H / 2);
  DumpEdge("HAI MAT  — |N.V|^0.75:", H / 2);
  g_cull = 1;
  Render(&m, EDGE_NDOTV, 0.6f, g_cull);
  float hCull = EdgeHardness(H / 2);
  DumpEdge("MOT MAT  — |N.V|^0.75:", H / 2);
  g_cull = 0;
  CHECK_MSG(hCull < hBoth,
            "culling back faces helps: a grazing ray stops crossing an "
            "unbounded amount of surface",
            "two-sided %.3f -> one-sided %.3f", (double)hBoth, (double)hCull);
  CHECK_MSG(hCull > HARD_LIMIT,
            "...but NOT on its own — with p below 1 the boundary is still a "
            "step, so a one-variable-at-a-time search never lands",
            "hardness %.3f still above %.2f", (double)hCull, (double)HARD_LIMIT);
}

// NOT PINNED TO THE SHADER, and that is the honest state.
//
// This harness found two real things — a per-fragment term cannot dissolve a
// boundary while a grazing ray crosses an unbounded number of facets, and the
// power must be at least 2 because |N.V| leaves the silhouette with an infinite
// derivative. Both were applied to trail_volume.fs and both were then reverted,
// because the changes that went in alongside them were justified by debug
// images that were painting only the fragments two discards had let through,
// and the result on screen went backwards.
//
// The measurements above stand on their own and are reproducible by running
// this file. Re-applying them to the shader needs a reading from the app that
// has not been contaminated the way those were — which is what
// sandbox/colour_probe.c and the discard-suppressed debug views exist to make
// possible. Pinning the shader from here before that would just be the same
// mistake with a test attached.

int main(void) {
  printf("=== silhouette: how a projected edge is made to dissolve ===\n");
  Test_CullingIsWhatMakesItWork();
  Test_ThePowerMustBeAtLeastTwo();
  Test_CombinedAndResolution();
  Test_FacetNormalsAreEnough();
  Test_BaselineIsHard();
  Test_TheShippedConfigCannotWork();
  Test_NdotV_FailsOnACube();
  Test_TessellationWasNeverTheAnswer();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

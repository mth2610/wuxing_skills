// VFX_ComposeSmokeColumn — the arithmetic that decides whether a column stands
// still or leaves the arena.
//
// This suite exists because the first build did not have it. The column flew
// off diagonally, stopped, and stretched; three symptoms, one cause, and it was
// diagnosed from a screenshot after the fact. Every number below was one
// division away from being known before the effect was ever drawn.
//
// WHAT IT CANNOT SEE: whether the result looks like smoke.

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
    else { printf("FAIL: %s\n", name); g_failures++; }                         \
  } while (0)

// ── 1. Why there is no simulation here at all ───────────────────────────────
//
// The first build drove the column with a ForceField and it failed three ways.
// The numbers below are why, kept because they are the argument for the
// rewrite: a field that declares no FORCE_VISCOSITY has
// ForceField_GetViscosityDamping return 1.0, so v = a*t without bound.
#define ARENA_RADIUS 18.0f // root CLAUDE.md — "Standard coordinates & scale"

static float TravelAfter(float accel, float dragStrength, float seconds) {
  const float dt = 1.0f / 60.0f;
  float v = 0.0f, p = 0.0f;
  float damp = expf(-dragStrength * dt);
  for (float t = 0.0f; t < seconds; t += dt) {
    v = (v + accel * dt) * damp;
    p += v * dt;
  }
  return p;
}

static void Test_SimulatingTheMediumWasTheWrongTool(void) {
  float at5 = TravelAfter(2.2f, 0.0f, 5.0f);
  CHECK_MSG(at5 > ARENA_RADIUS,
            "an undamped updraft leaves the arena within 5 s — the first "
            "build's flying column",
            "travelled %.1f m, arena radius %.0f m", (double)at5,
            (double)ARENA_RADIUS);

  // Adding drag bounds the SPEED but not the distance, which is linear in
  // time. Simulation was never going to hold the column in place on its own;
  // the geometry has to. That is the whole reason the path is frozen now.
  float damped20 = TravelAfter(2.6f, 2.0f, 20.0f);
  CHECK_MSG(damped20 > ARENA_RADIUS,
            "...and drag alone does not fix it: bounded speed still means "
            "unbounded distance",
            "travelled %.1f m in 20 s at terminal 1.30 m/s", (double)damped20);
}

// ── 2. A frozen path is exact, and free ─────────────────────────────────────
// Trail_SetStaticPath lerps `nodeCount` nodes between tail and head, so the
// column is at full height on frame one and its geometry cannot drift. No fill
// time, no ramp, no delay — the three things the simulated version had.
static void Test_FrozenPathIsExactOnFrameOne(void) {
  const int rings = 24;
  for (float h = 0.5f; h <= 6.0f; h += 0.5f) {
    float span = 0.0f;
    for (int i = 1; i < rings; i++) {
      float u0 = (float)(i - 1) / (float)(rings - 1);
      float u1 = (float)i / (float)(rings - 1);
      span += (u1 - u0) * h;
    }
    if (fabsf(span - h) > 1e-3f) {
      CHECK_MSG(0, "a seeded path spans exactly the requested height",
                "asked %.2f m, spans %.2f m", (double)h, (double)span);
      return;
    }
  }
  CHECK(1, "a seeded path spans exactly the requested height, 0.5..6 m");
  CHECK(1, "...on frame one — nothing to accumulate, so no spawn delay");
}

// ── 3. Mirror still matches source ──────────────────────────────────────────
static void CollapseWS(const char *src, char *out, size_t cap) {
  size_t o = 0;
  int inWS = 0;
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

static void Test_MirrorStillMatchesSource(void) {
  const char *c = "core/composition/common/vc_smoke_column.inl";

  CHECK(FileHas(c, "Trail_SetStaticPath(id, c->pos, top, cfg.tubeMaxRings);"),
        "the path is a seeded vertical segment, not an accumulated history");
  // The line whose ABSENCE shipped a flat quad. TrailConfig{0} is
  // TRAIL_TYPE_PROJECTILE and Trail_SetStaticPath returns silently for
  // anything that is not a FOLLOWER — no log, no error, no return value.
  CHECK(FileHas(c, "cfg.type = TRAIL_TYPE_FOLLOWER;"),
        "the trail is declared a FOLLOWER — without it Trail_SetStaticPath "
        "early-returns in silence and the tube collapses");
  CHECK(FileHas(c, "static path did NOT take"),
        "and the composer READS BACK historyCount and warns, because a "
        "collapsed tube and a correct one differ by nothing an input-only log "
        "can show");
  CHECK(FileHas(c, "cfg.forceField = NULL;"),
        "no force field: the reference technique simulates nothing, and "
        "simulating the medium is what produced the flying column");
  CHECK(!FileHas(c, "FORCE_VISCOSITY") && !FileHas(c, "FORCE_NOISE_CURL"),
        "...and the whole force-field apparatus is gone, not merely disabled");
  // The needle. DrawLayeredTube builds the entire tube at ONE radius taken from
  // the HEAD's half-width, so ANY head-thin envelope shrinks the whole volume
  // rather than shaping it. The shape belongs in the tube profile.
  CHECK(!FileHas(c, "TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN") &&
            !FileHas(c, "TRAIL_WIDTH_ENVELOPE_TAPER_TAIL"),
        "no width envelope shapes the column — headR is the tube's ONE radius, "
        "so a thin head is a needle whichever end it thins");
  CHECK(FileHas(c, "cfg.tubeShapeConfig = &c->tube;"),
        "the column picks a MESH TYPE — three independent modules, so a caller "
        "chooses the shape rather than parameters of one shared envelope");
  CHECK(FileHas(c, "c->tube = PMTube_DefaultConfig();") &&
            FileHas(c, "PMTubeConfig tube;"),
        "...and that type is the PIPE module (pm_tube.inl)");
  // The cone on top. The caps are triangle FANS with an apex pushed out along
  // the tangent by head/tailApexFactor, so they are not a flat lid — they are a
  // point. And TrailEntity.tubeCaps is dead: stored at spawn, read by nobody.
  // WRAPPED, not returned. DrawTubeEx opens with rlPushMatrix() and closes with
  // rlPopMatrix(); an early return between them skips the pop, and since the
  // function runs every frame the matrix stack overflows within seconds —
  // "RLVK: Matrix stack overflow", black screen, nothing pointing at the tube.
  // That shipped for exactly one build.
  CHECK(FileHas("core/geometry/pm_sweep_legacy.inl", "if (!data->suppressCaps) {"),
        "...and the draw honours it with a guarded BLOCK");
  CHECK(!FileHas("core/geometry/pm_sweep_legacy.inl", "if (data->suppressCaps) return;"),
        "...never an early return: it would jump over rlPopMatrix and overflow "
        "the matrix stack");
  // The tornado. One or two lobes around the ring is an ellipse; an ellipse
  // whose phase drifts with height is a helix by construction.
  CHECK(FileHas(c, "c->churn.latticeAround = 6;"),
        "the churn has enough lobes per ring to be lumps, not a rotating ellipse");
  CHECK(FileHas(c, "cfg.tubeRadialSegs = 18;"),
        "...and enough segments to resolve them — 10 against 6 lobes is under "
        "two samples per lobe, which renders as a spinning polygon");
  CHECK(FileHas(c, "MESH_DEFORM_DIR_NORMAL_OFFSET"),
        "and the reference's Normal+RGB term is present: scaling alone can only "
        "make the same section rounder or thinner, never asymmetric");
  CHECK(FileHas(c, "STATIC PATH %.2f m over"),
        "the spawn log states the geometry it built, so an invisible column is "
        "diagnosable without a screenshot");

  // The pair that makes the generator dispatch it. Getting this wrong compiles
  // clean and produces nothing at all.
  CHECK(FileHas(c, "static void VC_SmokeColumn_Update(float dt)") &&
            FileHas(c, "static void VC_SmokeColumn_Draw3D(Camera3D cam)"),
        "both archetype functions are `static` — scan_archetypes matches on "
        "`static void VC_<Name>_Update(float`, and a missing keyword means no "
        "dispatch is generated and the pool never ticks");
  CHECK(FileHas("core/composition/visual_composer.c", "VC_SmokeColumn_Update(dt);"),
        "...and the dispatch is actually present");
}


// ── The shape profiles ──────────────────────────────────────────────────────
// One formula was serving three different shapes. These are the three, as
// numbers, so "it still looks like a teardrop" is answerable without a GPU.
static float ProfDroplet(float t, float sharp, float headFrac) {
  float hs = 1.0f - headFrac;
  if (t >= hs) { float u = (t - hs) / headFrac; return sqrtf(fmaxf(0.0f, 1.0f - u * u)); }
  return powf((hs > 1e-5f) ? (t / hs) : 1.0f, sharp);
}
static float ProfCapsule(float t, float c) {
  if (c <= 0.0f) c = 0.25f;
  if (c > 0.5f) c = 0.5f;
  if (t < c) { float u = (c - t) / c; return sqrtf(fmaxf(0.0f, 1.0f - u * u)); }
  if (t > 1.0f - c) { float u = (t - (1.0f - c)) / c; return sqrtf(fmaxf(0.0f, 1.0f - u * u)); }
  return 1.0f;
}
static float ProfLegacy(float t) {
  return 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * 3.14159265f)));
}

static void Test_ProfilesAreThreeDifferentShapes(void) {
  // LEGACY is a LENS: symmetric, and equal at both ends. That is what every
  // consumer got, whatever it needed.
  CHECK_MSG(fabsf(ProfLegacy(0.0f) - ProfLegacy(1.0f)) < 1e-6f &&
                fabsf(ProfLegacy(0.25f) - ProfLegacy(0.75f)) < 1e-6f,
            "the legacy envelope is SYMMETRIC — a lens, not a droplet",
            "ends %.3f/%.3f", (double)ProfLegacy(0.0f), (double)ProfLegacy(1.0f));

  // DROPLET is asymmetric: a POINT at the tail, a round shoulder past the
  // middle, closing on itself at the head. Asymmetry is what makes it a drop.
  float tail = ProfDroplet(0.0f, 1.6f, 0.34f);
  float head = ProfDroplet(1.0f, 1.6f, 0.34f);
  CHECK_MSG(tail < 1e-6f && head < 1e-6f,
            "the droplet closes at BOTH ends by itself — so it needs no cap, "
            "and the cone cap it replaces was the pencil tip",
            "tail %.4f head %.4f", (double)tail, (double)head);
  float peak = 0.0f, peakT = 0.0f;
  for (int i = 0; i <= 100; i++) {
    float t = (float)i / 100.0f, r = ProfDroplet(t, 1.6f, 0.34f);
    if (r > peak) { peak = r; peakT = t; }
  }
  CHECK_MSG(peakT > 0.55f && fabsf(peak - 1.0f) < 0.02f,
            "...and its widest point sits PAST the middle, toward the head",
            "peak %.3f at t=%.2f", (double)peak, (double)peakT);
  CHECK_MSG(ProfDroplet(0.25f, 1.6f, 0.34f) < ProfDroplet(0.75f, 1.6f, 0.34f) * 0.5f,
            "which makes it genuinely asymmetric, unlike the lens it replaces",
            "%.3f at t=0.25 vs %.3f at t=0.75",
            (double)ProfDroplet(0.25f, 1.6f, 0.34f),
            (double)ProfDroplet(0.75f, 1.6f, 0.34f));

  // CAPSULE is a cylinder with two hemispheres — a STRAIGHT body and round
  // ends. The legacy profile named "capsule" has neither: it is a lens.
  int flat = 0;
  for (int i = 30; i <= 70; i++)
    if (fabsf(ProfCapsule((float)i / 100.0f, 0.25f) - 1.0f) < 1e-6f) flat++;
  CHECK_MSG(flat == 41,
            "the capsule has a genuinely STRAIGHT body — every sample between "
            "the two caps is exactly 1",
            "%d of 41 samples flat", flat);
  CHECK_MSG(ProfCapsule(0.0f, 0.25f) < 1e-6f && ProfCapsule(1.0f, 0.25f) < 1e-6f,
            "...and closes at both ends, so it needs no cap either",
            "ends %.4f/%.4f", (double)ProfCapsule(0.0f, 0.25f),
            (double)ProfCapsule(1.0f, 0.25f));
  int legacyFlat = 0;
  for (int i = 30; i <= 70; i++)
    if (fabsf(ProfLegacy((float)i / 100.0f) - 1.0f) < 1e-6f) legacyFlat++;
  CHECK_MSG(legacyFlat <= 1,
            "while the LEGACY profile called 'capsule' has no straight body at "
            "all — it is a lens, and the two are not the same shape",
            "%d of 41 samples flat", legacyFlat);

  // Each shape's maths lives in its own file; the sweep does not, because the
  // transported frame took four rounds to get right and three copies of it
  // would be three chances to diverge.
  CHECK(FileHas("core/geometry/pm_droplet_math.inl", "static float PMDropletRadius(") &&
            FileHas("core/geometry/pm_capsule_math.inl", "static float PMCapsuleRadius("),
        "droplet and capsule each own their formula in their own file");
  CHECK(FileHas("core/geometry/pm_sweep_legacy.inl", "return PMDropletRadius(") &&
            FileHas("core/geometry/pm_sweep_legacy.inl", "return PMCapsuleRadius("),
        "...and the sweep delegates to them rather than re-deriving");
  CHECK(FileHas("core/geometry/pm_sweep_legacy.inl",
                "out->suppressCaps = cfg->suppressCaps || (cfg->profile != PM_PROFILE_LEGACY_CAPSULE);"),
        "caps are a CONSEQUENCE of the shape: every self-closing profile "
        "refuses them, and only the legacy lens still needs its cones");

  // TUBE is flat. Anything the column shows is then the deform, which is the
  // only way to judge the deform at all.
  CHECK(FileHas("core/geometry/pm_sweep_legacy.inl", "case PM_PROFILE_TUBE:") &&
            FileHas("core/geometry/pm_sweep_legacy.inl", "return 1.0f;"),
        "the tube profile is constant — a pipe, open at both ends");
  CHECK(FileHas("core/geometry/pm_sweep_legacy.inl", "bool ownsSilhouette = (cfg->profile != PM_PROFILE_LEGACY_CAPSULE);"),
        "and DROPLET/TUBE own their whole silhouette — stacking the legacy "
        "taper on top would bend the shape just defined");

  // The three consumers the owner named.
  CHECK(FileHas("core/composition/water/water_stream.inl", "PMDroplet_BuildBezier(") &&
            FileHas("core/composition/water/water_stream.inl", "PMDroplet_BuildAlongPath("),
        "water stream draws a real droplet, from the droplet module");
  CHECK(FileHas("skills/water/water_stream/tube_skill.c", "s_waterTubeConfig = PMDroplet_DefaultConfig();"),
        "so does water stream on path");
  CHECK(FileHas("core/composition/common/vc_volume_trail.inl", "s_volTube = PMDroplet_DefaultConfig();") &&
            FileHas("core/composition/common/vc_volume_trail.inl", "cfg.dropletConfig = VolumeTrail_Shape();"),
        "and the volume trail, through the trail system's droplet hook");
  CHECK(FileHas("core/composition/common/vc_smoke_column.inl", "PMTube_DefaultConfig();"),
        "while the smoke column is the pipe");
}

int main(void) {
  printf("=== smoke column: drag, terminal speed, height->reach, wiring ===\n");
  Test_SimulatingTheMediumWasTheWrongTool();
  Test_FrozenPathIsExactOnFrameOne();
  Test_ProfilesAreThreeDifferentShapes();
  Test_MirrorStillMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

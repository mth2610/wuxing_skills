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
  // The whole cap apparatus is gone with the legacy sweep: no flag, no early
  // return, no rlPushMatrix to jump over. The bug it once caused (an early
  // return past rlPopMatrix, matrix-stack overflow, black screen) cannot recur
  // because there is no branch there at all.
  // The draw's ONLY early return sits before rlPushMatrix. Pinned as an
  // ordering, because the bug it replaces was exactly a return placed after it:
  // the pop was skipped, the matrix stack overflowed within seconds, and the
  // screen went black with nothing naming the tube.
  // There is no rlPushMatrix left to jump over — and removing it was not
  // tidying. The pair wrapped no transform at all, so its only effect was to
  // put rlgl into transformRequired, which folds the VIEW matrix into matModel
  // (ENGINE_LANDMINES §9). fragPosition/fragNormal then land in view space
  // while viewPos is a world-space camera position, and the shader's dot
  // product mixes the two: a valid number, a plausible gradient, and an object
  // that appears to shadow itself, shifts with the camera, and changes entirely
  // on a different map because the world coordinates changed.
  // The tornado, take two. Lobe COUNT was not the whole story: both octaves
  // shared f->latticeAround, and value noise seats its extrema on lattice
  // nodes, so the two piled onto the same meridians and reinforced into a fixed
  // set of ribs. Ribs that slide along the body read as a screw thread. The fix
  // is a per-layer around-period, so pin that the two layers actually differ.
  CHECK(FileHas(c, "c->churn.latticeAround = 3;") && FileHas(c, "c->churn.latticeAlong = 3;"),
        "the churn is LOW frequency — the reference deforms in a few sweeping "
        "bends over the whole height, not a ripple field");
  CHECK(FileHas(c, ".latticeAroundMul = 2.0f,"),
        "and the fine octave has its OWN around-period — two octaves sharing one "
        "are one octave with a wobble, which is the spinning read");
  // Reference proportions: the shape lives ALONG the column, so rings are what
  // it needs sampled. 24 x 28 had it backwards.
  CHECK(FileHas(c, "cfg.tubeMaxRings = 40;"),
        "...many rings — the shape lives ALONG the column, so that is what "
        "needs sampling (24 x 28 had it backwards)");
  // The snake. No amount of surface displacement bends a straight tube; it only
  // roughens it. The reference wireframe's sections stay round while the whole
  // body sweeps sideways, which means the CENTRELINE moves.
  CHECK(FileHas(c, "c->tube.centerlineAmp = c->radius * 1.6f;") &&
            FileHas("core/geometry/pm_tube.inl",
                    "pos = Vector3Add(pos, Vector3Add(Vector3Scale(right, bendA * w),"),
        "the centreline itself bends, not just the surface");
  // `tEnv` (05/08/2026) is the emitter-relative geometric coordinate: `t` for
  // the column (anchorAtTail false — bit-identical to what this line said
  // before), `1-t` for a caller whose emitter is at the far end of the swept
  // path. "Pinned to zero at the base" is the invariant; which end the base
  // is on is the caller's to state. See pm_tube_envelope_anchor_test.c.
  CHECK(FileHas("core/geometry/pm_tube.inl", "float w = cfg->centerlineAmp * tEnv * tEnv;"),
        "...and it is pinned to zero at the base — a source that sways reads as "
        "a flying object, not as something being emitted");
  // A single low-frequency arc sliding rigidly along the body is a snake made
  // of wire. Two scales, and most of the motion coming from the field evolving
  // IN PLACE rather than translating, is what makes it read as gas.
  // `t` -> `tNoise` 05/08/2026: tNoise falls back to plain t whenever
  // noiseWavelength is 0 (every caller that doesn't opt in, including the
  // column) — same behaviour, renamed so a moving trail can ask for the
  // noise coordinate in real metres instead of a fraction of its own
  // currently-fluctuating length. See noiseWavelength's doc comment in
  // procedural_mesh_utils.h.
  //
  // `t` back for the SURF argument, 05/08/2026 (same day, second patch) —
  // the rename above went one variable too far: PMTubeAxisScalar's third
  // argument lands in surf.y, which MeshDeformLayer.env/envStart/envEnd
  // read as the along-body ENVELOPE gate and must stay the true geometric
  // fraction. Passing tNoise there too (this test asserted exactly that)
  // let the trail's first live noiseWavelength user prove the bug: with
  // tNoise < 1, UV_ENV_HEAD_WELD_SQ = smoothstep(...)*c*c reads a shrunk
  // `c`, squaring the shrink into the envelope's magnitude everywhere
  // along the body, not just delaying where it opens — see
  // core/tests/pm_tube_envelope_coordinate_test.c for the numbers. `nvC`
  // (mat.y, the material/noise coordinate) is correctly still built from
  // tNoise — only the surf.y slot moves back to `t`. Column is unaffected
  // either way (tNoise == t here, noiseWavelength stays 0).
  CHECK(FileHas("core/geometry/pm_tube.inl", "float nvC = tNoise + cfg->noiseOffset * 0.45f;") &&
            FileHas("core/geometry/pm_tube.inl", "0.30f * PMTubeAxisScalar(cfg->noiseField, 0.41f, tEnv, nvC * 2.6f, time, right)"),
        "the bend has two scales and drifts slowly, so the sway is not stiff");
  // The texture. sin(TAU*(ku*u + kv*v)) is a plane wave in direction (ku, kv),
  // so drawing both from one range makes most modes DIAGONAL — 45-degree
  // corduroy, which wrapped on a column is a barber pole. Rising smoke is
  // streaks drawn out ALONG the flow: fast across it, slow along it.
  const char *gen = "scripts/gen_volume_surface.py";
  CHECK(FileHas(gen, "ku = rng.randint(2, max_freq)") &&
            FileHas(gen, "lo = int(round(va * ku / aspect_hi))"),
        "elongation is specified in WORLD space (aspect = 4*ku/kv), so it stays "
        "true if the sheet's proportions change");
  // The lean, which survived a correct magnitude. Every mode drawn with kv > 0
  // tilts the same way and their sum is a sheared field — diagonal corduroy,
  // the exact artefact the anisotropy was meant to remove. Alternating rather
  // than coin-flipping, because a fair coin over ~20 modes still lands lopsided
  // often enough to see: measured +0.245 gradient correlation on a random draw
  // against +0.053 when the split is exact.
  CHECK(FileHas(gen, "if idx % 2 == 1:") && FileHas(gen, "kv = -kv"),
        "...and the sign of kv is split EXACTLY in half, so the leans cancel");
  // A 1:4 sheet covers four times the metres along as around at the same texel
  // density. The consumer has to know that or texels come out 4x wide, which is
  // the shear this whole thread has been about.
  // ONE WRAP. The sheet is the column's SKIN, not a tiling material: its tongue
  // count is authored for the whole circumference, and only half a cylinder
  // faces the camera — 4 tongues around render as 2. Metre-matching the
  // around-axis gave 2 wraps, so 8 around and 4 visible, double the reference.
  CHECK(FileHas(gen, "SIZE_V = 1024") &&
            FileHas("core/geometry/pm_tube.inl", "#define PM_TUBE_UV_AROUND_WRAPS 1") &&
            !FileHas("core/geometry/pm_tube.inl", "PM_TUBE_UV_ASPECT"),
        "the sheet wraps the column exactly once");
  // Generalised 05/08/2026 onto core/uv's SurfaceFlow (core/uv/surface_flow.h)
  // — same fact, now expressed as layer 1's tiling instead of a hand-rolled
  // `uv2 = (u, v * u_volMask.x)`: {1.0, 1.63} tiles AROUND (u) exactly once
  // (unscaled) and ALONG (v) by 1.63x, so sheet 2 wraps the circumference
  // once too — scaling u would double the count again, the same bug one
  // layer down.
  CHECK(FileHas("core/trails/trail_system.c", ".tiling = {1.0f, 1.63f}"),
        "...and sheet 2 wraps once as well — scaling ITS u doubled the count "
        "again, which is the same bug one layer down");
  // billow() has its crest on a CONTOUR LINE, not over an area, so everything
  // it makes is a topographic map — a fishnet of hairs read one way, a solid
  // felt with hairline gaps read the other. Measured on the sheet it shipped:
  // 0.0% of texels below A=30, i.e. NOTHING was transparent, so no monotone
  // remap of it could open a hole. Five opacity curves were compared on the
  // same field; two came out entirely black and the rest were the same hatching.
  // The curve was never the lever.
  CHECK(!FileHas(gen, "cov = billow(detail") && FileHas(gen, "cov = sample(detail, u, v)"),
        "the coverage is a plain field, not billow — billow cannot make areas");
  CHECK(FileHas(gen, "cov_f = stretch(cov_f)") &&
            FileHas(gen, "a = smoothstep(cfg[\"cov_lo\"], cfg[\"cov_hi\"], cov_f[i_px])"),
        "...normalised, then thresholded, so what is below the cut is a TRUE "
        "zero and the tongues are separated by real black");
  // THE POLARITY. billow's crest is a CURVE, not an area, so the raw field is
  // thin lines. Read as opacity directly it renders as wire wool — a fishnet of
  // bright hairs. What looked like smoke in that render were the DARK bands
  // BETWEEN the ridges: broad, elongated, moving right. Those are 1 - cov.
  // Same field, same motion, opacity read the other way round.
  // And the gamma has to follow it across 1.0. billow of a field clustered at
  // its mean lands near 1, so 1-cov lands near 0; a gamma above 1 crushes that
  // to nothing. 2.6 produced a 1% sheet.
  // Aim ONE sheet near 58%, never the final density: the shader multiplies two
  // samples. Thresholding each sheet down to the target gave 34% x 34% = 10%.
  CHECK(FileHas(gen, "cov_lo=0.16, cov_hi=0.72,"),
        "...aimed so the PRODUCT lands near 30%, not each sheet");
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
static float ProfDroplet(float t, float a) {
  if (t <= 0.0f || t >= 1.0f) return 0.0f;
  float pk = sqrtf(a / (a + 1.0f));
  float norm = powf(pk, a) * sqrtf(fmaxf(0.0f, 1.0f - pk * pk));
  return powf(t, a) * sqrtf(fmaxf(0.0f, 1.0f - t * t)) / norm;
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
  float tail = ProfDroplet(0.0f, 1.6f);
  float head = ProfDroplet(1.0f, 1.6f);
  CHECK_MSG(tail < 1e-6f && head < 1e-6f,
            "the droplet closes at BOTH ends by itself — so it needs no cap, "
            "and the cone cap it replaces was the pencil tip",
            "tail %.4f head %.4f", (double)tail, (double)head);
  float peak = 0.0f, peakT = 0.0f;
  for (int i = 0; i <= 100; i++) {
    float t = (float)i / 100.0f, r = ProfDroplet(t, 1.6f);
    if (r > peak) { peak = r; peakT = t; }
  }
  CHECK_MSG(peakT > 0.55f && fabsf(peak - 1.0f) < 0.02f,
            "...and its widest point sits PAST the middle, toward the head",
            "peak %.3f at t=%.2f", (double)peak, (double)peakT);
  CHECK_MSG(ProfDroplet(0.25f, 1.6f) < ProfDroplet(0.75f, 1.6f) * 0.5f,
            "which makes it genuinely asymmetric, unlike the lens it replaces",
            "%.3f at t=0.25 vs %.3f at t=0.75",
            (double)ProfDroplet(0.25f, 1.6f),
            (double)ProfDroplet(0.75f, 1.6f));

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
  CHECK(FileHas("core/geometry/pm_droplet.inl", "PMDropletRadius(t, cfg->tailSharp)") &&
            FileHas("core/geometry/pm_capsule.inl", "PMCapsuleRadius(t, cfg->capFrac)"),
        "...and each module uses only its OWN formula");
  CHECK(!FileHas("core/geometry/pm_tube.inl", "suppressCaps") &&
            !FileHas("core/geometry/pm_droplet.inl", "suppressCaps") &&
            !FileHas("core/geometry/pm_capsule.inl", "suppressCaps"),
        "no cap flag exists either — the cone is gone by construction");

  // The tube's envelope is r(t) = tailFrac + (1-tailFrac)*t^p, and it collapses
  // to the constant 1 on the defaults — still a pipe unless a caller asks for
  // the flare. Smoke does widen with height (the reference's S(V) = V^p), and
  // `funnel` was the parameter that was supposed to say so: taken by the public
  // entry point, threaded through two calls, read by nothing, and still printed
  // in the spawn log. Pin that it now reaches the geometry.
  CHECK(FileHas("core/geometry/pm_tube.inl",
                "capsuleCurve = cfg->radiusTailFrac + (1.0f - cfg->radiusTailFrac) * grow;"),
        "the tube has a radius envelope that can open with height");
  CHECK(FileHas("core/composition/common/vc_smoke_column.inl", "if (funnel) { c->tube.radiusTailFrac = 0.12f;"),
        "...and `funnel` actually drives it, instead of only reaching the log");
  // S(V) = V^p with p > 1: the displacement stays tight at the source and opens
  // out near the top. HEAD_WELD alone is p = 1, which churns as hard at knee
  // height as at the crown.
  CHECK(FileHas("core/composition/common/vc_smoke_column.inl", "UV_ENV_HEAD_WELD_SQ") &&
            FileHas("core/uv/uv_deform.c",
                    "if (kind == UV_ENV_HEAD_WELD_SQ) return SmoothStep(start, end, c) * c * c;"),
        "the displacement envelope is p = 2, not the linear weld");

  // ── BƯỚC 4: the three opacity terms geometry cannot supply ────────────────
  // Without these the column draws under raylib's default shader: an opaque
  // lumpy solid. It still renders, so nothing reports a problem.
  const char *vfs = "core/trails/shaders/trail_volume.fs";
  CHECK(FileHas("core/composition/common/vc_smoke_column.inl", "cfg.tubeVolumeShading = true;"),
        "the column asks for volume shading");
  // Generalised 05/08/2026: the multiply itself now happens inside
  // core/uv's SurfaceFlow_Blend (SURFACE_FLOW_MUL), driven by a SurfaceFlow
  // whose layer 1 asks for that blend — trail_volume.fs just reads the
  // already-blended alpha back out. Pin both halves: the shader takes
  // `.a` off the generic sample (not a hand-rolled `s1.a * s2.a` anymore),
  // and the C side actually asks for MUL, not ADD/MAX — either alone could
  // silently regress to alpha-over coverage-adding, which is the "polished
  // stone" bug this exists to avoid.
  CHECK(FileHas(vfs, "float pattern = flowSample.a;"),
        "two sheets MULTIPLIED — alpha-over layering can only add coverage, "
        "which is why two passes read as polished stone");
  CHECK(FileHas("core/trails/trail_system.c", ".blend = SURFACE_FLOW_MUL"),
        "...and the C side actually asks core/uv's SurfaceFlow for MUL, not "
        "ADD or MAX");
  // |N.V|, NOT 1-|N.V|. The shell reading puts opacity 0.000 on the column's
  // axis and its PEAK at 85% of the radius — a bright band hugging the
  // boundary, which IS the hard edge. A rolloff constant cannot move a peak
  // that sits at the edge by construction, which is why retuning it from 0.34
  // to 0.75 changed nothing on screen. The mesh is the hull of a VOLUME and a
  // ray travels furthest through the middle.
  // The space rule, and it cuts the other way here. ENGINE_LANDMINES §9 scopes
  // matModel = model x view to DrawMesh; this tube goes out through immediate
  // mode, where matModel is identity and fragPosition is WORLD. Pin the form
  // every shipping fresnel in this engine uses, so nobody "fixes" it back to
  // the view-space reading the first draft of this shader had.
  CHECK(FileHas("core/geometry/pm_tube.inl", "void PMTube_DrawFaded(") &&
            FileHas(vfs, "float fade = vColor.a * colDiffuse.a;"),
        "the vertical fade rides in a VERTEX ATTRIBUTE, not a per-draw uniform "
        "(ENGINE_LANDMINES §8)");
  // THE SPIRAL, source three. Panning u moves the sheet AROUND the tube's
  // circumference — rotation, by definition — and any along-pan on top makes
  // the sum a diagonal, i.e. a screw. The first draft of trail_volume.fs panned
  // both axes and manufactured the exact artefact it was written to remove.
  // Generalised 05/08/2026: the pan constants now live in s_volFlow's two
  // SurfaceFlowLayer.pan fields instead of a raw `pan[4]`. AROUND stays the
  // FIRST component of Vector2 pan (.x) on both layers — pin it at 0 on
  // each literal so a future edit cannot reintroduce the screw thread by
  // giving one layer a nonzero .x.
  CHECK(FileHas("core/trails/trail_system.c", ".pan = {0.0f, 0.085f}") &&
            FileHas("core/trails/trail_system.c", ".pan = {0.0f, -0.043f}"),
        "the sheet pans ALONG the body only — the around components are zero");
  // And source four: u wraps at 1.0, so a non-integer around-scale leaves a
  // hard seam running the full length of the column. Same rule already stated
  // for the deform lattice in core/deform/mesh_deform.h.

  // The bisect tool. The column's two motions share one clock (noiseOffset is
  // derived from uvScrollOffset), so turning the scroll off stops both and
  // proves nothing. Freezing the MESH alone is what separates them.
  CHECK(FileHas("core/trails/trail_system.c",
                "float runNoiseOffset = t->tubeDeformFrozen ? 0.0f : (-t->uvScrollOffset * 0.5f);") &&
            FileHas("core/trails/trail_system.c",
                    "float buildTime = t->tubeDeformFrozen ? 0.0f : (float)GetTime();"),
        "freezing the deform stops BOTH its clocks — the along-offset and the "
        "noise's own time axis; stopping one still leaves motion");
  CHECK(FileHas("core/composition/common/vc_smoke_column.inl", "\"smokecolumn_freeze\"") &&
            FileHas("core/composition/common/vc_smoke_column.inl", "deform %s — sheet vẫn trượt"),
        "...it is a live tunable and it logs ON CHANGE, because a frozen column "
        "and a broken one look identical");

  // THE SPIRAL, source five — the one that actually survived to the screen.
  // u spans 0..1 around the section whatever its size, so on a funnel whose
  // circumference changes 3.3x the texel aspect changes with it: square at the
  // base, three times as wide at the top. Any diagonal feature is progressively
  // sheared, which the eye reads as twist. Raising smokecolumn_tile to 3.0 made
  // it go away by leaving barely one tile along the whole body — it hid the
  // shear, it did not remove it.
  CHECK(FileHas("core/geometry/pm_tube.inl",
                "float u1 = (float)j * (float)aroundTiles / (float)radialSegs;"),
        "...and it is an INTEGER count, so u still wraps exactly at the seam");

  // NOTHING IS PINNED ABOUT THE EDGE TERM ANY MORE, and that is deliberate.
  //
  // Six rounds of constants were tried here and every one of them was justified
  // by a debug image that turned out to be painting only the fragments two
  // discards had let through. The shader is back to what it was when the column
  // last read as "close, only the edges are hard", and it will not be changed
  // again on the strength of a reading this session has not honestly obtained.
  //
  // What survives is in core/tests/silhouette_test.c, which rasterises the
  // geometry on the CPU and owes nothing to those images.
  // A shader that is not deployed does not report as a shader problem.
  CHECK(FileHas("CMakeLists.txt", "core/trails/shaders/trail_volume.fs") &&
            FileHas("Makefile.Android", "core/trails/shaders/*.fs"),
        "and both build systems actually copy it");

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

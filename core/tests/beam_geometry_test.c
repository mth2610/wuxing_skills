// core headless test — P4 beam, STEP 1: the two DoD cases from
// core/docs/VFX_PLAN.md §4.3, both of which have a real failure mode that no
// amount of tuning can reach.
//
// WHY THIS FILE EXISTS BEFORE ANYONE LOOKS AT THE BEAM. Both DoD cases are
// arithmetic, and core/CLAUDE.md's debugging table says arithmetic questions
// must not cost a build-and-look cycle. This file answers both without a GPU.
//
//   CASE 1  nearly coincident endpoints  -> must not draw a degenerate tube
//   CASE 2  viewed end-on                -> must not disappear
//
// Case 2 is the interesting one and it is why the hot core exists at all.
// trail_volume.fs weights every fragment by |N.V|. Point the beam at the
// camera and the tube's surface normals are perpendicular to the view
// direction EVERYWHERE, so d ~ 0 over the whole body — and the term is then
// zero, not merely small, so no density, alpha or power setting recovers it.
// The answer has to be a layer that does not consult |N.V| at all.
//
// WHAT THIS CANNOT SEE: whether the beam looks like a beam. That is step 1's
// single look, and it is the owner's.
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

// trail_volume.fs's shipped thickness term, at the shipped u_volMask
// (body gain 0, rim gain 1, power 2, softness 0.34 — the 06/08 lock-in).
#define POWER 2.0f
#define SOFTNESS 0.34f
static float VolumeEdge(float d) {
  return powf(1.0f - d, POWER) * SmoothStep(0.0f, SOFTNESS, d);
}

// ── CASE 2: end-on ──────────────────────────────────────────────────────────
static void Test_EndOnKillsTheVolumeShadedBody(void) {
  // Looking down the axis of a cylinder: the surface normal is radial, the
  // view direction is axial, so their dot product is ~0 everywhere on the
  // body. A tiny residue remains only from perspective across the radius.
  printf("  |N.V|   volume edge\n");
  for (int i = 0; i <= 4; i++) {
    float d = (float)i * 0.05f;
    printf("   %.2f    %.4f\n", d, VolumeEdge(d));
  }

  CHECK_MSG(VolumeEdge(0.0f) < 1e-6f,
      "dead end-on the volume term is EXACTLY zero, not small — so the body "
      "layer cannot be recovered by density, alpha or power, and a beam fired "
      "at the camera would vanish if the body were the only layer",
      "edge at |N.V| = 0 is %.8f", VolumeEdge(0.0f));

  // Even a few degrees off-axis it is still far below the side-on value, so
  // "nearly end-on" is a wide band, not a single unlucky angle.
  float sideOn = VolumeEdge(0.34f); // where the term peaks
  CHECK_MSG(VolumeEdge(0.10f) < 0.45f * sideOn,
      "and the collapse is not a knife-edge: well off-axis the body is still "
      "under half its side-on opacity, so this is a band of camera angles",
      "edge(0.10) = %.4f vs peak %.4f", VolumeEdge(0.10f), sideOn);
}

// ── CASE 1: coincident endpoints ────────────────────────────────────────────
// pm_tube.inl: PMTubeSamplePath returns early at zero total length, and
// ringGap falls back to baseRadius. Nothing crashes — it just is not a beam.
// The composition must therefore refuse ABOVE that floor, and it must refuse
// by going transparent rather than by an early return, so the trail (and its
// UV scroll / churn phase) survives to be reused.
#define BEAM_MIN_LENGTH 0.05f
static void Test_CoincidentEndpointsHideRatherThanDrawGarbage(void) {
  CHECK_MSG(BEAM_MIN_LENGTH > 0.0f,
      "there is a nonzero minimum length at all — a beam of length 0 reaches "
      "pm_tube's degenerate-path early return, where the result is a shape "
      "but not a beam",
      "min length %.3f m", BEAM_MIN_LENGTH);

  // The floor must sit above the geometry's own degenerate threshold
  // (PMTubeSamplePath treats totalLength <= 0 as degenerate) with real margin,
  // not just above zero.
  CHECK_MSG(BEAM_MIN_LENGTH > 1e-3f,
      "...and with margin over float noise, not merely > 0",
      "min length %.4f m", BEAM_MIN_LENGTH);
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
  const char *bm = "core/composition/common/vc_beam.inl";
  const char *fs = "core/trails/shaders/trail_volume.fs";

  // THE end-on answer. If this ever becomes `true`, the beam disappears when
  // fired at the camera and the body layer cannot save it.
  CHECK(FileHas(bm, "cfg.tubeVolumeShading = !isCore;"),
        "the core is drawn WITHOUT the volume shader — the one line that makes "
        "an end-on beam survive");
  CHECK(FileHas(bm, "cfg.blendMode = isCore ? BLEND_ADDITIVE : BLEND_ALPHA;") &&
            FileHas(bm, "cfg.useCustomBlendMode = true;"),
        "blend law (VFX_PLAN §0.3): the body occludes so it is alpha, the core "
        "emits so it is additive — and the flag is set, because BLEND_ALPHA is "
        "0 and cannot be detected by > 0");

  CHECK(FileHas(bm, "#define BEAM_MIN_LENGTH 0.05f"),
        "the minimum length this file's arithmetic assumes is the shipped one");
  // Hiding by ALPHA, not by an early return — that is what lets the beam come
  // back with its time state intact instead of popping in fresh.
  // EPSILON, not zero. trail_system.c reads alphaMul == 0 as "unset" and
  // substitutes 1.0 (`(ly->alphaMul > 0.0f) ? ly->alphaMul : 1.0f`, twice), so
  // a hide written as `* 0.0f` renders at FULL alpha — the precise opposite.
  // This shipped that way until beam_probe failed to remove the core on
  // screen. Pinned as a value check because the bug is invisible in review:
  // `* 0.0f` reads as obviously correct.
  CHECK(FileHas(bm, "float live = (Beam_Length(b) >= BEAM_MIN_LENGTH) ? 1.0f : 1e-4f;") &&
            FileHas(bm, ".alphaMul = 0.80f * s_beamAlphaMul * live,"),
        "the self-hide uses a nonzero epsilon — zero is the one value "
        "trail_system.c reinterprets as full alpha");
  CHECK(FileHas("core/trails/trail_system.c", "float aMul = (ly->alphaMul > 0.0f) ? ly->alphaMul : 1.0f;"),
        "...and that sentinel is still there, so the epsilon is still required "
        "(if this ever changes, the epsilons above can go back to 0)");

  // The static-path trap that has already shipped once in this archetype.
  CHECK(FileHas(bm, "cfg.type = TRAIL_TYPE_FOLLOWER;"),
        "cfg.type is set — Trail_SetStaticPath returns SILENTLY for any other "
        "type, and the tube then collapses to a flat quad with no warning");
  CHECK(FileHas(bm, "if (laid != BEAM_PATH_NODES)"),
        "...and the result is READ BACK, because the failure above is invisible "
        "in a log that prints only the inputs");

  // Slices decoupled from nodes (06/08) — a straight beam wants few nodes and
  // enough slices for step 2's deform.
  CHECK(FileHas(bm, "cfg.tubeGeomSegs = isCore ? 8 : 24;"),
        "mesh slices are set explicitly, not inherited from the node count");
  // The core was simplified on the owner's call (06/08): no sheet, no scroll,
  // no volume shader. Pinned because each of those is a thing someone will
  // want to "improve" back onto it, and every one of them adds another
  // unsorted alpha pass over the body for no silhouette gained.
  CHECK(FileHas(bm, ".texture = NULL, // flat — see above") &&
            FileHas(bm, "cfg.uvScrollSpeed = (isCore || probe) ? 0.0f : 0.85f;"),
        "the core stays plain: flat texture, no scroll");
  CHECK(FileHas(bm, "cfg.layerCount = 1;"),
        "one alpha layer on the body, not two — a second textured layer on the "
        "same mesh is another unsorted pass, which is the trail blend defect "
        "this file must not feed");

  // STEP 2, and the layer that carries the verdict. NORMAL_SCALE, not
  // TANGENT: mesh_deform.c's TANGENT branch adds `tangent * w`, and pm_tube
  // passes the PATH tangent, so on a straight tube it slides rings along their
  // own axis — vertices move, the OUTLINE does not, and the outline is the
  // entire quantity under test. Pinned so nobody "restores" the spec's word.
  CHECK(FileHas(bm, ".kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_NORMAL_SCALE,"),
        "the travelling pulse scales the section (moves the outline) rather "
        "than sliding rings along the axis (moves nothing visible)");
  // Matched as a CODE form (`.direction = ...`), not as a bare token: the file
  // names MESH_DEFORM_DIR_TANGENT in the comment that explains why it is
  // wrong here, and a bare-token check would fire on that explanation. Same
  // family as the stray-`#include`-in-a-comment trap in docs/LANDMINES.md.
  CHECK(!FileHas(bm, ".direction = MESH_DEFORM_DIR_TANGENT"),
        "...and no layer actually uses TANGENT on a straight beam");
  CHECK(FileHas(bm, "b->bodyTube.noiseField = probe ? NULL : &b->deform;") &&
            FileHas(bm, "b->bodyTube.noiseWavelength = 2.0f;"),
        "the field is wired and its grain is in METRES, so a 3 m and a 30 m "
        "beam carry the same swell size");
  // The core stays smooth — a second wobbling outline inside the first buys
  // nothing and costs another unsorted pass.
  CHECK(FileHas(bm, "b->coreTube.noiseField = NULL;") &&
            FileHas(bm, "cfg.tubeNoiseAmp = (isCore || probe) ? 0.0f : (0.45f * s_beamDeform);"),
        "the core is excluded from the deform, and its extAmp is 0 rather than "
        "relying on the NULL field alone");
  // The verdict knob: beam_deform = 0 reproduces step 1 exactly, so "does the
  // outline earn the volume" is a file save rather than a rebuild.
  CHECK(FileHas(bm, "Tuning_RegisterFloat(\"beam_deform\", &s_beamDeform, 1.0f);"),
        "deform strength is live, so step 1 and step 2 can be A/B'd without a "
        "rebuild — 0 is step 1");

  // The shipped volume constants this file mirrors.
  CHECK(FileHas(fs, "float rim = smoothstep(0.0, max(u_volMask.z, 0.001), d);") &&
            FileHas(fs, "float d = clamp(abs(dot(N, V)), 0.0, 1.0);"),
        "the |N.V| weighting this file models is still what the shader does");
}

// ── The sheet contract, which step 1 got wrong on its first look ────────────
//
// trail_volume.fs says, in its own source: "A = coverage in the OPAQUE layout;
// RGB is grey luminance kept grey so the caller's tint survives", and then does
// `colour = s1.rgb * vColor.rgb`. That is a CONTRACT on the sheet, and nothing
// enforced it — so picking VFX_SURFACE_ENERGY_TUBE (a real colour sheet: bright
// filaments over a dark ground, coverage staying high BETWEEN them) produced
// exactly two symptoms at once: tinted-by-sheet colour, and opaque BLACK where
// the ground is dark but A is high. "Trong suốt -> thành màu đen" is not a
// blend bug; the fragment is genuinely opaque and genuinely black.
//
// This check exists so the next volume effect cannot repeat it silently. It
// reads the declared channels straight out of the registry JSON — the same
// file scripts/validate_vfx_surface_registry.py enforces at configure time.
static void Test_VolumeSheetMustDeclareGreyLuminance(void) {
  const char *reg = "assets/vfx_surface_profiles.json";

  // The consumer side of the contract: if this line ever stops multiplying the
  // sheet's RGB into the colour, the requirement below is moot and this test
  // should be revisited rather than kept passing out of habit.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "vec3 colour = s1.rgb * vColor.rgb * colDiffuse.rgb;"),
        "trail_volume.fs still multiplies the sheet's RGB into the output, so "
        "a non-grey sheet still corrupts the caller's tint");

  // What the beam actually picked, and what that sheet promises.
  CHECK(FileHas("core/composition/common/vc_beam.inl",
                "VFX_SurfaceRegistry_Get(VFX_SURFACE_VOLUME_FIRE)"),
        "the beam draws with a VOLUME_* sheet");
  CHECK(FileHas(reg, "volume_surface_fire.png") &&
            FileHas(reg, "GREY luminance in RGB so the caller's VFX_Material tint survives"),
        "...and that sheet's registry entry declares GREY luminance in RGB — "
        "the property trail_volume.fs depends on and cannot check itself");

  // The negative: the sheet that broke it makes no such promise, and the beam
  // must not be back on it.
  CHECK(!FileHas("core/composition/common/vc_beam.inl",
                 "VFX_SurfaceRegistry_Get(VFX_SURFACE_ENERGY_TUBE)"),
        "the beam is NOT on ENERGY_TUBE, whose entry says 'tintable energy "
        "filaments' and promises nothing about grey");
}

// ── PROBE MODE is an instrument, so its contract gets pinned too ────────────
//
// beam_probe exists to answer ONE question the shipped beam cannot: is the
// bright band where the formula says the edge is? That only works if probe
// really removes every OTHER thing that moves the apparent edge. A probe that
// quietly leaves one of them in is worse than no probe — it produces a
// confident reading of the wrong image, which is exactly the trap
// silhouette_test.c's own "NOT PINNED TO THE SHADER" note was written about.
static void Test_ProbeStripsEverythingThatMovesTheEdge(void) {
  const char *bm = "core/composition/common/vc_beam.inl";

  CHECK(FileHas(bm, "b->bodyTube.radiusTailFrac = probe ? 1.0f : 0.80f;"),
        "probe removes the TAPER — a tapered silhouette runs diagonally across "
        "the screen, and a diagonal edge is the hardest kind to judge");
  CHECK(FileHas(bm, "b->bodyTube.noiseField = probe ? NULL : &b->deform;"),
        "probe removes the DEFORM — a bulging outline has no fixed edge to "
        "locate in the first place");
  CHECK(FileHas(bm, ".alphaMul = probe ? 1e-4f : (0.35f * s_beamCoreMul * live),"),
        "probe removes the additive CORE — a bright filament down the middle "
        "is precisely what makes the body's own gradient unreadable");
  CHECK(FileHas(bm, ".texture = probe ? NULL : &s_beamSheet,"),
        "probe removes the SHEET — its light and dark patches are "
        "indistinguishable from the shading gradient under measurement");
  CHECK(FileHas(bm, "cfg.uvScrollSpeed = (isCore || probe) ? 0.0f : 0.85f;"),
        "probe stops all MOTION — anything moving turns a position question "
        "into a memory question");
  CHECK(FileHas(bm, "cfg.tubeRadialSegs = isCore ? 8 : (probe ? 32 : 16);"),
        "probe raises the radial count — a polygon corner reads as an edge and "
        "would confound the edge being located");
  // AND it must be FAT. At the shipped 0.10 m over 4.6 m the tube is ~10 px
  // wide, i.e. under a pixel per face at 16 radial. dFdx/dFdy read a 2x2 pixel
  // quad, so below that size the derivative straddles different triangles and
  // the reconstructed normal is rasteriser noise — the speckling seen in the
  // band views. An instrument thinner than its own sampling footprint measures
  // the sampling, not the thing.
  CHECK(FileHas(bm, "cfg.thick = isCore ? (b->width * 0.34f) : (probe ? (b->width * 6.0f) : b->width);"),
        "probe fattens the tube so each face covers several pixels — otherwise "
        "it measures the rasteriser instead of the shading");
  // DEFAULTS ON as of the end of 06/08: the beam is parked as one plain
  // cylinder because the volume shading it depends on reads a fragNormal that
  // rlNormal3f never delivers (core/docs/VOLUME_SHADING_HANDOFF.md). Tuning
  // the layered version against a meaningless |N.V| would bake noise into the
  // numbers, so the stack stays off until the normal path is fixed.
  CHECK(FileHas(bm, "Tuning_RegisterFloat(\"beam_probe\", &s_beamProbe, 1.0f);") &&
            FileHas(bm, "static float s_beamProbe = 1.0f;"),
        "...and it defaults ON, parking the beam as a plain cylinder until the "
        "volume normal path is fixed");

  // The companion debug view. Bands, not a ramp: the eye cannot read a value
  // off a continuous grey, which is why volume_debug = 2 never settled this.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 9.5 && u_volDebug < 10.5) {"),
        "volume_debug = 10 paints |N.V| as discrete bands, so the question "
        "becomes 'does the bright band land on the red band' — which an image "
        "can actually answer");

  // EVERY debug branch needs BOTH bounds. `if (u_volDebug > 8.5)` had no upper
  // one — correct while it was the last branch, and a trap the moment modes 10
  // and 11 were added below it: volume_debug = 10 fell into the constant-red
  // branch and returned, so four rounds of diagnosis were spent reading a
  // CONSTANT. The symptom was the worst kind available — an unchanging screen
  // that survived every toggle, which reads as "a deep systemic bug" and
  // actually meant "the measurement is not running".
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 8.5 && u_volDebug < 9.5) {"),
        "the constant-red branch is bounded ABOVE as well — an open-ended last "
        "branch silently swallows every mode added after it");
  // Mode 12 is the one that can actually answer "where is the edge": mode 10
  // sits above the discard like every other debug view, so it composites BOTH
  // tube walls with opaque alpha and no depth sort, and which one wins a pixel
  // depends on raster order — i.e. on camera angle. An instrument whose answer
  // moves with the camera cannot locate anything.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 11.5 && u_volDebug < 12.5) {"),
        "there is a band view BELOW the discard, so the edge can be located "
        "on the near wall alone");
  // The cull vector and the shading vector must not be the same variable. The
  // dFdx branch used to force N toward the camera, which made `facing`
  // unconditionally >= 0 and disabled `discard` entirely — so every
  // below-discard view still composited both walls, defeating the reason
  // mode 12 was added at all.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs", "float facing = dot(Nattr, V);") &&
            FileHas("core/trails/shaders/trail_volume.fs", "float d = clamp(abs(dot(N, V)), 0.0, 1.0);"),
        "cull reads the ATTRIBUTE normal (whose outward sign pm_tube.inl "
        "enforces) while shading reads the selected one — one vector cannot "
        "serve both, because forcing its sign silently disables the cull");
  // Matched with its trailing comment, so the line that EXPLAINS the removal
  // does not itself trip the check — same trap as the MESH_DEFORM_DIR_TANGENT
  // assertion earlier in this file, and as the stray-#include-in-a-comment
  // landmine. A bare-token check on removed code fires on its own obituary.
  CHECK(!FileHas("core/trails/shaders/trail_volume.fs",
                 "N = -N; // huong ve camera"),
        "...and the sign-forcing line that caused it is gone, not just moved");
  // The line that answers the owner's original question by superimposition
  // rather than by inference.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 12.5 && u_volDebug < 13.5) {") &&
            FileHas("core/trails/shaders/trail_volume.fs", "float bR = sqrt(max(0.0, 1.0 - ds * ds));") &&
            FileHas("core/trails/shaders/trail_volume.fs", "abs(bR - 0.960)"),
        "mode 13 draws a b/R contour scale ON the tube with the white line at "
        "0.960 — where silhouette_test.c measures the shipping term's peak — "
        "so the comparison is two images rather than a chain of reasoning");
  // It must ignore vol_normal_src: flat normals give 16 discrete values on a
  // 16-sided tube, so a thin contour appears and disappears as the camera
  // moves. That is an artefact of the instrument and it wasted a round.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs", "float ds = abs(dot(Nattr, V));"),
        "...and it always reads the INTERPOLATED normal, because a position "
        "question needs a continuous surface");
  // The self-contained check: compare the two normal sources against EACH
  // OTHER. No camera, no V, no uniform can influence the answer, so it cannot
  // be spoiled the way every camera-dependent view in this session was.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 13.5 && u_volDebug < 14.5) {") &&
            FileHas("core/trails/shaders/trail_volume.fs", "float agree = abs(dot(Nattr, Ng));"),
        "mode 14 compares the attribute normal against the geometric one from "
        "dFdx — black means the attribute is a TANGENT, and that single image "
        "settles it without any camera dependence");
  // ...and the BLOCK that holds the above-discard views is bounded too. It
  // ends in an unbounded fallback (`q = ... : fade; return;`), so without an
  // upper bound it swallows every mode placed below the discard — which is
  // exactly what happened to modes 11 and 12 (volume_debug = 12 rendered the
  // grey `fade` view). Second instance of this shape in one session: the
  // first was the `> 8.5` branch eating mode 10. Numbering modes without
  // bracketing each number is a cumulative trap.
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 0.5 && u_volDebug < 10.5) {"),
        "the above-discard debug BLOCK is bounded above, so modes below the "
        "discard are reachable at all");
  CHECK(FileHas("core/trails/shaders/trail_volume.fs",
                "if (u_volDebug > 7.5 && u_volDebug < 8.5) {") &&
            FileHas("core/trails/shaders/trail_volume.fs",
                    "if (u_volDebug > 10.5 && u_volDebug < 11.5) {") &&
            FileHas("core/trails/shaders/trail_volume.fs",
                    "if (u_volDebug > 6.5 && u_volDebug < 7.5) {"),
        "...and so is every other numbered mode, so the next one added cannot "
        "be swallowed either");
}

int main(void) {
  printf("=== core/composition: beam (P4) step 1 — the two DoD cases ===\n");
  Test_EndOnKillsTheVolumeShadedBody();
  Test_CoincidentEndpointsHideRatherThanDrawGarbage();
  Test_VolumeSheetMustDeclareGreyLuminance();
  Test_ProbeStripsEverythingThatMovesTheEdge();
  Test_MirrorMatchesSource();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

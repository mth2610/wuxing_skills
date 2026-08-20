// core headless test — P5, VFX_ComposeShockRing.
//
// P5's claim is that a mid-air ring is a different PRIMARY from
// `VFX_ComposeGroundWave` and not the same one with a NULL height callback. That
// claim is falsifiable, so most of this file tests it: if the only difference
// were the raycast, the two would agree on everything else, and they do not —
// this one has real thickness out of its own plane and takes an orientation,
// because it is the only one of the two that is ever seen edge-on.
//
// The rest is the arithmetic the ground wave paid for once already and which is
// re-derived here rather than shared, because the two profiles are allowed to
// diverge: the crest sitting exactly ON a vertex, the shade's face factor not
// dimming the crest it is meant to sit behind, and the thickness being a ratio
// against the band's own width.
//
// What it cannot see: whether it reads as a blast. It can prove that the crest
// leads, that the section is a lens rather than a plane, and that nothing about
// the ring grows without bound as it expands.

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#ifndef PI
#define PI 3.1415926535f
#endif

#define SHOCK_MAX_SLICES   64
#define SHOCK_RADIALS      7
#define SHOCK_CREST_U      0.5f
#define SHOCK_CREST_K      1.0f
#define SHOCK_CORE_RATIO   0.22f
#define SHOCK_CANVAS_MUL   5.2f
#define SHOCK_THICK_RATIO  0.30f
#define SHOCK_SHADE_FLOOR  0.30f
#define SHOCK_FACE_DIM     0.60f

static float Mix(float a, float b, float t) { return a + (b - a) * t; }
static float SmoothStep01(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.6f);
}
static float CoreWidth(float rNow) { return rNow * SHOCK_CORE_RATIO; }
static float CanvasWidth(float rNow) { return CoreWidth(rNow) * SHOCK_CANVAS_MUL; }
static float HalfThickness(float core, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return core * SHOCK_THICK_RATIO * SmoothStep01(t01 / 0.10f) * powf(1.0f - t01, 1.1f);
}
static float Detail(int tier)
{
    switch (tier) { case 3: return 96.0f; case 2: return 72.0f; case 1: return 48.0f; default: return 32.0f; }
}
static float Profile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return sinf(PI * powf(u, SHOCK_CREST_K));
}
static float Shade(float u)
{
    float h = Profile(u);
    float face = (u <= SHOCK_CREST_U)
                     ? 1.0f
                     : Mix(1.0f, SHOCK_FACE_DIM, (u - SHOCK_CREST_U) / (1.0f - SHOCK_CREST_U));
    return Mix(SHOCK_SHADE_FLOOR, 1.0f, h) * face;
}
static float Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return SmoothStep01(t01 / 0.08f) * powf(1.0f - t01, 1.6f);
}
static int Slices(int tier)
{
    switch (tier) { case 3: return SHOCK_MAX_SLICES; case 2: return 40; case 1: return 24; default: return 16; }
}

// ── 1. It is NOT a ground wave with the raycast switched off ────────────────

static void Test_ItHasThicknessOutOfItsOwnPlane(void)
{
    // THE WHOLE ARGUMENT FOR P5 BEING ITS OWN PRIMARY. A flat annulus seen along
    // its own plane is a line; a mid-air ring is seen from every angle, so it
    // must have a section perpendicular to the plane it expands in.
    float band = CoreWidth(3.0f);
    float half = HalfThickness(band, 0.35f);
    CHECK_MSG(half > 0.0f, "the ring has real thickness out of its own plane",
              "%.4f m half-thickness at t=0.35 on a 3 m ring", half);

    // And that thickness is visible against the band — not a token displacement.
    // Below about a tenth of the band it would not survive the first frame of
    // foreshortening.
    CHECK_MSG(half / band > 0.08f,
              "the section is a LENS, not a plane with a nudge",
              "%.2f of the band's width", half / band);

    // A RATIO AGAINST THE BAND'S OWN WIDTH, which is itself a ratio of the
    // radius — so nothing grows without bound. Keyed to the radius directly the
    // ring would grow a thicker wall the further it travelled; keyed to a
    // constant it would flatten into a decal at large radii.
    float worst = 0.0f;
    for (float r = 0.5f; r <= 20.0f; r += 0.1f) {
        float b = CoreWidth(r);
        float ratio = HalfThickness(b, 0.35f) / b;
        float ref = HalfThickness(CoreWidth(1.0f), 0.35f) / CoreWidth(1.0f);
        float e = fabsf(ratio - ref) / ref;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f,
              "the section keeps its shape at every radius — thickness is a ratio "
              "against the band, not against the world",
              "worst relative drift %.7f", worst);

    // The ring THINS as it spreads: a shell of material being stretched, not a
    // solid hoop growing.
    CHECK_MSG(HalfThickness(CoreWidth(3.0f), 0.85f) <
              HalfThickness(CoreWidth(3.0f), 0.25f),
              "it is fattest just after release and thins as it goes",
              "%.4f late vs %.4f early", HalfThickness(CoreWidth(3.0f), 0.85f),
              HalfThickness(CoreWidth(3.0f), 0.25f));
}

// ── 2. The crest LEADS, and it lands on a vertex ────────────────────────────

static void Test_CrestLeadsAndLandsOnAVertex(void)
{
    // THE BUG THIS EXISTS FOR, inherited from the ground wave and worth
    // re-asserting because the constant is re-declared here: the band is only
    // SHOCK_RADIALS samples across, so its vertices land at 0, 1/6, 2/6 ... 1. A
    // crest that falls BETWEEN two of them is never built — the profile is
    // evaluated either side of the peak and the peak is skipped. The curve would
    // be right and the mesh would be wrong.
    float step = 1.0f / (float)(SHOCK_RADIALS - 1);
    float onVertex = SHOCK_CREST_U / step;
    CHECK_MSG(fabsf(onVertex - floorf(onVertex + 0.5f)) < 1e-4f,
              "the crest sits exactly ON a vertex, so the mesh actually builds it",
              "crest at index %.4f of %d", onVertex, SHOCK_RADIALS - 1);

    // The exponent is what puts sin(PI * u^k)'s peak there. Derived, not guessed:
    // u^k = 0.5 at the crest, so k = ln(0.5)/ln(crestU).
    float k = logf(0.5f) / logf(SHOCK_CREST_U);
    CHECK_MSG(fabsf(k - SHOCK_CREST_K) < 1e-3f,
              "and the exponent is ln(0.5)/ln(crestU), not a fitted number",
              "%.4f vs %.4f", k, SHOCK_CREST_K);

    // The peak really is where it is claimed, by measurement.
    float peak = -1.0f, peakAt = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.0005f) {
        float v = Profile(u);
        if (v > peak) { peak = v; peakAt = u; }
    }
    CHECK_MSG(fabsf(peakAt - SHOCK_CREST_U) < 0.005f,
              "the profile peaks at the crest by measurement, not by assertion",
              "peaks at %.4f", peakAt);
    CHECK_MSG(peak > 0.999f, "and reaches full height there", "%.5f", peak);

    // ZERO AT BOTH BASES: the band has to close, or it has a hard edge where it
    // stops.
    CHECK(Profile(0.0f) == 0.0f && Profile(1.0f) == 0.0f,
          "the band closes at both bases");

    // THE CANVAS. The mesh is deliberately much wider than the front it draws,
    // because the strands are carved out of it per-pixel and cannot reach past
    // geometry that was never built. A mesh sized to the visible band — which is
    // what this primary used to build — can only ever produce a clean hoop, and
    // no shader fixes that from the inside.
    CHECK_MSG(SHOCK_CANVAS_MUL >= 2.0f,
              "the mesh is a CANVAS, several times wider than the front it draws",
              "%.2fx the front's width", SHOCK_CANVAS_MUL);
    CHECK_MSG(CanvasWidth(3.0f) > CoreWidth(3.0f) * 1.9f,
              "...at every radius, since both are ratios of the same radius",
              "%.3f m canvas vs %.3f m front", CanvasWidth(3.0f), CoreWidth(3.0f));

    // AND THE ROOM IS ON BOTH SIDES. A canvas hanging mostly outward — which is
    // what a leading crest gives — leaves a clean hole in the middle far larger
    // than any reference shows, because there is no geometry for the smoke to
    // reach inward into. The crest at the middle is what closes that hole.
    float outward = 1.0f - SHOCK_CREST_U;
    CHECK_MSG(fabsf(outward - SHOCK_CREST_U) < 0.02f,
              "the canvas hangs off the front in BOTH directions, so the middle fills",
              "%.3f outward vs %.3f inward", outward, SHOCK_CREST_U);

    // The size of the hole follows directly, and it must be well inside the
    // front. At canvas 3.0 with the crest at 1/3 the inner base sat at 0.78 of
    // the front radius and the ring read as a thin annulus around a big void.
    float holeRatio = 1.0f - SHOCK_CORE_RATIO * SHOCK_CANVAS_MUL * SHOCK_CREST_U;
    CHECK_MSG(holeRatio < 0.50f,
              "the inner base reaches well inside the front, so the middle is not a void",
              "hole is %.2f of the front radius", holeRatio);
    // AND IT MUST NOT REACH THE CENTRE. Polar UVs pinch to a point there: the
    // sheet is squeezed to zero circumference and renders as radial streaks
    // converging on the middle, so the ring stops reading as a ring at all.
    CHECK_MSG(holeRatio > 0.30f,
              "...but stays clear of the polar singularity at the centre",
              "hole is %.2f of the front radius", holeRatio);
}

// ── 3. The self-shading replaces the light that is not there ───────────────

static void Test_ShadingIsAuthoredAndAsymmetric(void)
{
    // The floor is what keeps the slopes visible at all. Below about a quarter an
    // additive draw of them does not register against the arena and only the
    // crest survives — the "faint thin line" failure the ground wave recorded.
    CHECK_MSG(SHOCK_SHADE_FLOOR >= 0.25f, "the slopes stay visible, not just the crest",
              "%.2f floor", SHOCK_SHADE_FLOOR);

    // THE CREST IS THE BRIGHTEST POINT, and this is the assertion that catches
    // the face-factor-as-a-step bug: written as a step at the crest, the rule
    // meant to darken the slope BEHIND the crest dimmed the crest itself, and
    // the band lost most of its contrast.
    float best = -1.0f, bestAt = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.0005f) {
        float v = Shade(u);
        if (v > best) { best = v; bestAt = u; }
    }
    CHECK_MSG(fabsf(bestAt - SHOCK_CREST_U) < 0.01f,
              "the crest is the brightest point of the band, undimmed",
              "brightest at %.4f", bestAt);
    CHECK_MSG(fabsf(Shade(SHOCK_CREST_U) - 1.0f) < 1e-3f,
              "and it reaches full brightness", "%.4f", Shade(SHOCK_CREST_U));

    // THE ASYMMETRY, which is the "lit from the blast centre" read and the one
    // thing a flat additive decal cannot fake. Compare two points at the SAME
    // profile height, one either side of the crest.
    // Well inside the crest: with the crest at 1/3 of the canvas the profile is
    // still near its peak just below it, so a sample taken there and its
    // equal-height partner both land where the face factor is ~1 and the test
    // would pass on nothing.
    float uIn = 0.10f;
    float target = Profile(uIn);
    float uOut = SHOCK_CREST_U;
    for (float u = SHOCK_CREST_U; u <= 1.0f; u += 0.0005f)
        if (Profile(u) <= target) { uOut = u; break; }
    CHECK_MSG(Shade(uIn) > Shade(uOut) * 1.15f,
              "at equal height the INNER face is clearly brighter than the outer",
              "%.3f at u=%.2f vs %.3f at u=%.2f", Shade(uIn), uIn, Shade(uOut), uOut);
}

// ── 4. The life: it arrives, it decelerates, it never overshoots ───────────

static void Test_LifeEnvelope(void)
{
    // Ease-OUT: a front is fastest at the instant it is released. A linear
    // expansion reads as a growing circle rather than as something thrown.
    float firstTenth = Radius01(0.10f);
    float lastTenth = Radius01(1.0f) - Radius01(0.90f);
    CHECK_MSG(firstTenth > lastTenth * 3.0f,
              "it covers far more ground in its first tenth than its last",
              "%.3f vs %.3f", firstTenth, lastTenth);

    // The reference front releases its radius almost immediately, then holds
    // near its final size while the smoke keeps changing.  A merely gentle
    // ease-out reads as a circle still being scaled in the last frames.
    CHECK_MSG(Radius01(0.5f) > 0.80f && Radius01(0.5f) < 0.88f,
              "it reaches most of its radius by halfway",
              "%.3f", Radius01(0.5f));
    CHECK_MSG(Radius01(1.0f) - Radius01(0.6f) < 0.10f,
              "...then visually settles while erosion keeps evolving",
              "%.3f of the radius remains", Radius01(1.0f) - Radius01(0.6f));

    // Monotone and bounded: never overshoots the radius it was given, never
    // retreats.
    int ok = 1;
    float prev = -1.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.001f) {
        float r = Radius01(t);
        if (r < prev - 1e-6f || r > 1.0f + 1e-6f) ok = 0;
        prev = r;
    }
    CHECK(ok, "the front only ever moves outward, and never past its target radius");

    // Alpha: in FAST (a shock arrives, it does not fade in), out slow, zero at
    // both ends.
    CHECK(Alpha01(0.0f) == 0.0f && Alpha01(1.0f) == 0.0f, "alpha closes at both ends");
    CHECK_MSG(Alpha01(0.08f) > 0.85f, "it arrives rather than fading in",
              "%.3f at t=0.08", Alpha01(0.08f));
    CHECK_MSG(Alpha01(0.5f) > 0.28f,
              "and is still clearly visible halfway through — not a flash that "
              "leaves a ghost expanding",
              "%.3f", Alpha01(0.5f));
    // BUT IT MUST ACTUALLY DISPERSE. At the old 1.1 the ring ended its life
    // nearly as bright as it began, so it read as stopping rather than as smoke
    // thinning out. The last quarter has to give up most of what is left.
    CHECK_MSG(Alpha01(0.75f) < Alpha01(0.5f) * 0.55f,
              "and the tail really disperses rather than merely stopping",
              "%.3f at 0.75 vs %.3f at 0.5", Alpha01(0.75f), Alpha01(0.5f));
}

// ── 5. The tier ladder ──────────────────────────────────────────────────────

static void Test_TierLadder(void)
{
    int monotone = 1;
    for (int t = 0; t < 3; t++)
        if (Slices(t) > Slices(t + 1)) monotone = 0;
    CHECK(monotone, "slice count clamps DOWN and only down");

    // Still a closed ring at the bottom. 16 slices is a 22.5-degree step; the
    // faceting is inside the additive falloff at the sizes this is drawn at.
    CHECK_MSG(Slices(0) >= 12, "and it is still a closed ring at the lowest tier",
              "%d slices", Slices(0));
    CHECK_MSG((float)Slices(3) / (float)Slices(0) >= 3.0f,
              "while the lowest tier is several times cheaper",
              "%d vs %d slices", Slices(3), Slices(0));
}

// ── the mirror guard ────────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pendingSpace = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pendingSpace = 1; continue; }
        if (pendingSpace && o > 0 && o + 1 < cap) out[o++] = ' ';
        pendingSpace = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[400000], flat[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_MirrorMatchesTheSource(void)
{
    const char *inl = "core/composition/common/vc_shock_ring.inl";

    CHECK(FileHas(inl, "#define SHOCK_CREST_U 0.5f"), "the crest is what this test mirrors");
    CHECK(FileHas(inl, "#define SHOCK_THICK_RATIO 0.30f"), "so is the out-of-plane thickness");
    CHECK(FileHas(inl, "#define SHOCK_CORE_RATIO 0.22f"), "and the visible front");
    CHECK(FileHas(inl, "#define SHOCK_CANVAS_MUL 5.2f"), "and the canvas it is carved out of");

    // THE LENS. Two sweeps at +offset and -offset is the mechanical statement of
    // "it has a section", and it is the only thing separating this from a flat
    // annulus.
    CHECK(FileHas(inl, "for (int side = 0; side < 2; side++)"),
          "both faces of the lens are swept");
    CHECK(FileHas(inl, "float sgn = (side == 0) ? 1.0f : -1.0f;"),
          "...at +offset and -offset out of the plane");
    CHECK(FileHas(inl, "curRing[i] = Vector3Add(p, Vector3Scale(n, sgn * ringOff[i]));"),
          "and the displacement is along the ring's OWN normal");

    // The reference's ring stays geometrically circular.  Its apparent motion
    // and irregular boundary come from the smoke shader's coverage, erosion and
    // radial stretch, never from moving the mesh underneath the smoke.
    CHECK(!FileHas(inl, "s_shockDeform"),
          "no mesh-deformation tuning path remains");
    CHECK(!FileHas(inl, "ShockRing_AngleNoise") &&
          !FileHas(inl, "ShockRing_Hash1"),
          "the CPU sweep has no angular noise that can wobble the ring");
    CHECK(!FileHas(inl, "float dR =") && !FileHas(inl, "float dY ="),
          "the CPU sweep keeps both radial and vertical positions analytic");

    // NO TERRAIN. The whole point of it being a separate primary at the API level.
    CHECK(!FileHas(inl, "GroundHeightSampleFn"), "it takes no height callback");
    CHECK(!FileHas(inl, "MapManager_GetGroundHeightAt("), "and raycasts nothing");

    // The orientation, and its guard.
    CHECK(FileHas(inl, "if (Vector3LengthSqr(normal) < 1e-8f)"),
          "a degenerate normal is caught on the SQUARED length, before any normalise");
    CHECK(FileHas(inl, "normal = (Vector3){0.0f, 1.0f, 0.0f};"),
          "...and defaulted to horizontal rather than refused");
    CHECK(FileHas(inl, "VC_PlaneFrame(n, &axA, &axB);"),
          "the plane frame comes from the shared guarded helper");

    // The blend law and the flushes — depth mask, culling AND blend, both sides.
    // BODY carries the colour and EMISSION is the bloom on top: a purely additive
    // ring washes towards white and loses the element it was cast with.
    //
    // EMISSION became PREMULTIPLIED on 20/08/2026. Additive can only add, and on
    // a background already at 1.0 there is nothing to add to — the ring measured
    // darken 0.9% of its own footprint on white at warmup 40. Premultiplied,
    // white reads 81.5 (warmup 20: 2.4 -> 52.0), and chroma rises on EVERY
    // background (white 0.257 -> 0.346, warm 0.442 -> 0.483, cool 0.173 -> 0.200,
    // dark 0.340 -> 0.357). The trade is |d| on the warm plate, 0.157 -> 0.119.
    CHECK(FileHas(inl, "pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION") &&
          FileHas(inl, "pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_PREMULTIPLIED"),
          "it draws its colour in BODY and its bloom in EMISSION");
    // Blend state and fragment formula are ONE decision: (ONE, ONE_MINUS_SRC_ALPHA)
    // does not apply coverage, so the shader must. Selecting the blend alone was
    // measured on the trail presets as a 1/alpha brightening of every soft edge.
    CHECK(FileHas("core/shaders/shock_ring.fs", "if (u_premultiply > 0.5)") &&
          FileHas("core/shaders/shock_ring.fs", "VFX_ResolvePremultiplied(col, u_emission, a,"),
          "...and the emission pass premultiplies in the shader, since the blend no longer does");
    CHECK(FileHas(inl, "VFXRender_BeginDraw(") &&
          FileHas(inl, "false);") && FileHas(inl, "rlDisableBackfaceCulling();"),
          "no depth write, both walls — flushed on both sides");
    CHECK(FileHas(inl, "ShockRing_SetUniforms(m, (pass == 0) ? alpha : alpha * 0.70f,") &&
          FileHas(inl, "(pass == 0) ? 1.0f : 2.50f, t01, seed, hasSmoke,") &&
          FileHas(inl, "(pass == 0) ? 0.0f : 1.0f);"),
          "the emission pass carries enough energy and coverage to cross the bloom "
          "threshold — and tells the shader which blend law it is under");
    CHECK(FileHas(inl, "rlEnableBackfaceCulling();") &&
          FileHas(inl, "VFXRender_EndDraw(&renderScope);"),
          "and the restore is flushed too");

    // Authored shading, not a lit material.
    CHECK(!FileHas(inl, "Material_Begin("),
          "no lit material — it would be black-on-black in the night arena");
    CHECK(FileHas(inl, "ringCol[i] = VC_WithAlpha(c, (unsigned char)(alpha * ShockRing_Shade(u) * 255.0f));"),
          "the shading is authored into the vertex colour");
    CHECK(FileHas(inl, "Color c = VC_MixColor(m->body, m->glow, h);"),
          "and the colours come from VFX_Material, body at the bases, glow at the crest");

    // The crest sits AT the front, with the canvas hanging off it.
    CHECK(FileHas(inl, "ringRad[i] = rNow + canvas * (u - SHOCK_CREST_U);"),
          "the crest is placed at the front's radius, not at the mesh's middle");

    // UVs, without which the fragment stage has no coordinates to make strands
    // in — this is the attribute the old clean-hoop version never emitted.
    CHECK(FileHas(inl, "rlTexCoord2f(u0, v0);") && FileHas(inl, "rlTexCoord2f(u1, v1);"),
          "the sweep emits UVs: u around the ring, v across the canvas");

    // Static storage, no allocation, no shake.
    CHECK(FileHas(inl, "static Color ringCol[SHOCK_RADIALS];"), "static storage");
    CHECK(!FileHas(inl, "malloc("), "no allocation");
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");
}

// ── the GLSL mirror ─────────────────────────────────────────────────────────
//
// The silhouette is a fragment-stage construction, so the load-bearing
// expressions are in GLSL and this file cannot execute them. What it CAN do is
// assert they still exist, so the shader cannot quietly drift back into drawing
// a clean hoop while every numeric test above stays green. It cannot tell you
// the ring looks right — only that the mechanism is still wired up.

static void Test_ItIsAnErodedRopeNotGeneratedStrands(void)
{
    const char *fs = "core/shaders/shock_ring.fs";
    const char *inl = "core/composition/common/vc_shock_ring.inl";

    CHECK(FileHas(inl, "ResourceManager_LoadShader(\"core/shaders/shock_ring.vs\", "
                       "\"core/shaders/shock_ring.fs\")"),
          "the composer loads the smoke shader through ResourceManager");

    // THE SHADER MUST BE COPIED INTO THE BUILD TREE (ENGINE_LANDMINES, "every
    // runtime-loaded shader must be configure_file'd"). Worth a test rather than
    // a comment because of HOW it fails: the load returns id 0,
    // ShockRing_HasShader() goes false, and the composer quietly draws its
    // fallback — the clean analytic hoop this whole rewrite exists to replace.
    // Nothing errors. The ring renders. It looks exactly like the version before
    // the change, which is indistinguishable from "the new code did not work"
    // and sends you debugging a shader that never ran.
    CHECK(FileHas("CMakeLists.txt",
                  "configure_file(core/shaders/shock_ring.fs "
                  "${CMAKE_CURRENT_BINARY_DIR}/core/shaders/shock_ring.fs COPYONLY)"),
          "the fragment shader is copied into the build tree");
    CHECK(FileHas("CMakeLists.txt",
                  "configure_file(core/shaders/shock_ring.vs "
                  "${CMAKE_CURRENT_BINARY_DIR}/core/shaders/shock_ring.vs COPYONLY)"),
          "...and so is the vertex shader");

    // THE WISPS ARE LIT DIRECTLY, NOT AS A CONTOUR. An earlier version lit the
    // level set {dens == thr}. That is right for a procedural field, but across
    // the rope the density is a single HUMP — up from the inner edge, peak at the
    // centre-line, down to the outer edge — so any threshold below the peak is
    // crossed TWICE and the contour draws an inner rim AND an outer rim. The ring
    // then reads as double-edged no matter how many ropes exist, which is exactly
    // how it was misdiagnosed as a leftover second rope. A level set of a hump
    // has to look like that; the fix is not to take a level set.
    CHECK(!FileHas(fs, "abs(dens - thrA)"),
          "no iso-contour: a level set across a hump is crossed twice and double-edges");
    CHECK(FileHas(fs, "float hot = alive * smoothstep(0.26, 0.78, dens) * "
                      "mix(1.0, 0.30, u_t01);"),
          "the wisps are lit by their own density instead");
    // AND THE CORES COOL. Young smoke has bright dense cores, old smoke has
    // none. Held constant, the ring is at its brightest when it should be at its
    // faintest — it reads as stopping rather than as dispersing.
    CHECK(!FileHas(fs, "smoothstep(0.26, 0.78, dens);"),
          "...and they cool over the ring's life rather than staying hot");

    // SHAPED, NOT POWERED. The sheet's alpha lives mostly between 0.3 and 0.6 —
    // it is thin smoke — so pow(dens, 2.6) maps nearly all of it below 0.2 and
    // the ring goes dim and sparse while every other term still looks correct.
    CHECK(!FileHas(fs, "pow(clamp(dens, 0.0, 1.0), 2.6)"),
          "...over the range the data actually occupies, not the top of it");
    CHECK(FileHas(fs, "float cover = clamp(smoke * 0.60 + hot * 0.95, 0.0, 1.0) * edge;"),
          "and the dim body sits under the hot cores");

    // SUBTRACTIVE, NOT GENERATIVE. Nothing here indexes a per-feature cell. The
    // previous version did — one strand per angular cell — and that is a comb by
    // definition: evenly spaced features, which the eye reads long before it
    // reads any per-strand variation. Warp and jitter do not touch the period.
    CHECK(!FileHas(fs, "u_fibers"), "no per-strand cell decomposition survives");
    CHECK(!FileHas(fs, "float halfW"), "...no per-strand width");
    CHECK(!FileHas(fs, "float tipV"), "...no per-strand length");

    // THE ROPE IS CLOSED BEFORE IT IS TORN, and it is torn by a threshold that
    // CLIMBS. Erosion at a fixed threshold is a static stencil, not a tear.
    CHECK(FileHas(fs, "float thrA = mix(0.02, 0.82, t2) + clumpT;") &&
          FileHas(fs, "float t2 = u_t01 * u_t01;"),
          "erosion begins during expansion, not only after the ring has stopped");
    CHECK(FileHas(fs, "float clumpT = (clump - 0.5) * 0.28;"),
          "the newborn ring's erosion variance is narrow enough to remain closed");
    CHECK(FileHas(inl, "static float s_shockHole = 0.16f;") &&
          FileHas(inl, "Tuning_RegisterFloat(\"shock_hole\", &s_shockHole, 0.16f);"),
          "the low smoke tail is not cut back into an oversized empty middle");

    // A fast ease-out and quadratic erosion overlap: the circular front settles
    // while its smoke keeps reshaping, as in the reference sequence.
    CHECK(FileHas(fs, "float t2 = u_t01 * u_t01;") && FileHas(inl, "return 1.0f - powf(1.0f - t01, 2.6f);"),
          "the front settles early while erosion continues to reshape it");

    // ONE ROPE. The second, thinner one riding outside it was judged wrong
    // against the render: sharing this one's noise field made the pair move as a
    // single object rather than as two, and it only muddied the inner rope's
    // behaviour. A second ring should be a second CALL at its own radius and
    // phase, not a second lobe on this field.
    CHECK(FileHas(fs, "float ropeA = Rope(v, coreV, widA, u_t01);"), "there is one rope");
    CHECK(!FileHas(fs, "ropeB") && !FileHas(fs, "thrB") && !FileHas(fs, "float widB"),
          "...and no second lobe sharing its field (sA/sB are texture layers, not ropes)");

    // EXPANSION STRETCHES THE SAMPLE SPACE. This is where the radial tendrils
    // come from; nothing animates a wisp length.
    CHECK(FileHas(fs, "float vs = coreV + (v - coreV) * mix(1.0, 0.40, u_t01);"),
          "the sample space compresses radially, so the smoke is pulled outward");
    CHECK(FileHas(fs, "float radialRuffle = (FbmRing(vec2(u * 6.0, 47.0 + u_seed), 6.0, 2) - 0.5) *") &&
          FileHas(fs, "vs += radialRuffle;"),
          "noise continuously ruffles smoke coverage radially without moving the mesh");
    CHECK(FileHas(fs, "float reach = (d < 0.0) ? w * mix(1.42, 1.15, u_t01) : w;"),
          "young smoke has a long inward tail, then pulls outward instead of filling the disc");
    // NOT FURTHER. At 0.16 the compression is over six times by the end and the
    // sheet's texels smear into straight rays — a starburst, not stretched smoke.
    CHECK(!FileHas(fs, "mix(1.0, 0.16, u_t01)"),
          "...but not so far that the wisps become radial rays");

    // WORLD-SQUARE CELLS. u spans 2*PI*R, v spans 0.66*R, so a cell square in UV
    // is nine times wider than tall in metres and the field smears sideways.
    CHECK(FileHas(fs, "const float U_PER_V = 9.5;") &&
          FileHas(fs, "float nV = nU / U_PER_V;"),
          "the noise cells are square in the WORLD, not in UV");
    CHECK(FileHas(fs, "float us = u + drift * 0.85 * (0.35 + u_t01) / U_PER_V;"),
          "...and the motion-noise amplitude is converted through the same ratio");

    // THE SHAPE IS AUTHORED. An fbm is statistically homogeneous — every region
    // looks like every other — so eroding one gives evenly distributed features
    // of one size all the way round, which is the "too regular" failure. The
    // sheet supplies the non-uniformity; noise only distorts its UVs.
    CHECK(FileHas(fs, "float sheet = clamp(sA * 0.85 + sB * 0.55, 0.0, 1.0);"),
          "the density field comes from the simulated smoke sheet, summed not maxed");
    CHECK(FileHas(fs, "float dens = mix(proc, sheet, float(u_hasSmoke));"),
          "...with the procedural field kept only as the sheet-missing fallback");
    CHECK(FileHas(inl, "VFX_SurfaceRegistry_Get(VFX_SURFACE_SHOCK_RING_SMOKE)"),
          "and the composer binds the ring's OWN sheet, not the impact disc's");

    // REMAP THE SHEET'S OCCUPIED BAND. Thin smoke leaves three quarters of the
    // sheet empty and most of the rest under 0.3. Every threshold downstream is
    // written against a full-range field, so without this line they all measure
    // against a range the data never reaches and the ring renders as a few stray
    // flecks — with no single term looking wrong. Retune HERE when the sheet is
    // regenerated, not in the erosion or the hot-core curve.
    CHECK(FileHas(fs, "sheet = smoothstep(0.03 + 0.06 * (u_layerDetail - 1.0),"),
          "the sheet is remapped into the range the thresholds below assume");

    // THE RESOLUTION LADDER IS ON THE TIME AXIS. Reference shows a small sharp
    // ring and softer, fainter smoke further out — the SAME ring at three
    // moments, not three rings at one moment. So one draw, whose sharpness runs
    // from HIGH to LOW over its own life; young smoke really is sharper than old.
    // Drawn as three near-coincident instances instead, it only thickens one
    // edge and closes the middle; drawn as time-staggered echoes it puts three
    // fronts on screen at once and stops being one object.
    CHECK(FileHas(inl, "#define SHOCK_DETAIL_EARLY 1.60f") &&
          FileHas(inl, "#define SHOCK_DETAIL_LATE  0.50f"),
          "sharpness runs from HIGH to LOW across the ring's own life");
    CHECK(FileHas(inl, "float layerDetail = Math_Mix(SHOCK_DETAIL_EARLY, SHOCK_DETAIL_LATE, t01);"),
          "...driven straight off t01, on ONE ring");
    CHECK(!FileHas(inl, "s_shockLayerTable"),
          "and there is no layer table: not three instances, not three echoes");

    // DETAIL MUST NOT SCALE THE ANGULAR SAMPLE RATE. At 0.5 late in life that
    // leaves about one repeat of the sheet around the whole circumference, so
    // every texel column is magnified into a wide radial band and the ring reads
    // as a starburst exactly when it should be at its softest.
    CHECK(FileHas(fs, "float tuA = fract(us * 4.0 + u_layerPhase * 0.11);"),
          "the angular sample rate is fixed and coprime, independent of detail");
    CHECK(!FileHas(fs, "us * 2.0 * u_layerDetail"),
          "...so softening never stretches the sheet into rays");

    // FLOOR THE SAMPLING DIVISOR. Early in the ring's life the rope is narrow,
    // and dividing by that width magnifies the sheet several times across the
    // band — the texels smear into radial streaks converging on the centre and
    // the ring reads as a starburst. The floor caps the stretch; it is not a
    // divide-by-zero guard, and 0.04 (which is one) does not do the job.
    CHECK(FileHas(fs, "float tvA = 0.5 + (vs - coreV) / max(widA * 2.2, 0.26) + pan;"),
          "the sheet cannot be stretched without limit when the rope is narrow");

    // THE STRIP'S EMPTY MARGINS MUST NOT BE SAMPLED. The sheet is authored
    // non-tiling and its ends are deliberately blank; wrapped with a bare
    // fract() those blanks land at fixed angles and punch the same large hole in
    // every ring, which reads as a bug rather than as a tear.
    // THE SHEET TILES, so it wraps directly. The previous strip did not, and its
    // blank ends had to be squeezed out with a 0.06..0.94 remap — a workaround
    // for a texture that should simply have been periodic. Making the generator
    // wrap its noise lattice and its particles removed the need for it.
    CHECK(!FileHas(fs, "0.06 + 0.88"),
          "the sampler wraps the strip directly, with no margin workaround");
    CHECK(FileHas(inl, "VFX_SURFACE_SHOCK_RING_SMOKE") &&
          !FileHas(fs, "0.06 + 0.88"),
          "...because the sheet it binds is periodic in x");

    // THE SHEET IS SIMULATED, AND REPRODUCIBLY SO. An fbm is statistically
    // homogeneous, so a ring eroded from one is a necklace of identical beads
    // however it is tuned; advection gives long strokes next to fine detail next
    // to nothing, because neighbouring regions have different histories.
    CHECK(FileHas("scripts/gen_shock_ring_smoke.py", "def _velocity(") &&
          FileHas("scripts/gen_shock_ring_smoke.py", "Curl of a scalar potential"),
          "the sheet's generator advects particles through a curl-noise field");
    CHECK(FileHas("scripts/gen_shock_ring_smoke.py", "px -= math.floor(px)"),
          "...wrapping them in x, which is what makes the sheet tileable");

    // PERCENTILE NORMALISATION. A handful of pixels where many streaks crossed
    // accumulate several times more than anything else; dividing by that maximum
    // pushed 88% of the sheet under alpha 0.1 and the ring rendered EMPTY while
    // every term in the generator still looked correct.
    CHECK(FileHas("scripts/gen_shock_ring_smoke.py", "target = int(nonzero * 0.992)"),
          "and the accumulation is normalised on a percentile, never on the maximum");

    // THE SEAM. u wraps at 1.0, so every hash taken at u * N must wrap at N, and
    // every octave must double the period along with the frequency.
    CHECK(FileHas(fs, "float x0 = mod(i.x, period);") &&
          FileHas(fs, "float x1 = mod(i.x + 1.0, period);"),
          "the noise lattice wraps in x, so the ring's u seam cannot show a line");
    CHECK(FileHas(fs, "p *= 2.0; period *= 2.0;"),
          "...and octaves double the period too, or the seam reopens at octave two");

    // The canvas fade on RAW v: coverage that survives to the mesh boundary is
    // clipped into a straight chord across the smoke.
    CHECK(FileHas(fs, "float edge = smoothstep(0.0, 0.05, v) * "
                      "(1.0 - smoothstep(0.93, 1.0, v));"),
          "the canvas fade is taken on RAW v, so no polygon edge cuts the smoke");

    // Driven by the ring's own life, never by wall clock: two rings at different
    // phases must not share a pattern.
    CHECK(!FileHas(fs, "u_time"), "nothing in the silhouette is driven by u_time");
}

static void Test_DetailIsPixelsNotVertices(void)
{
    // The noise cell count is a fragment-stage pattern, so it is allowed to be —
    // and must be — larger than the slice count. Tying detail to geometry is how
    // the ring ends up either cheap and clean or expensive and lumpy.
    CHECK_MSG(Detail(3) > (float)Slices(3) * 1.2f,
              "there is more angular detail than there are slices: pixels, not vertices",
              "%.0f cells vs %d slices", Detail(3), Slices(3));

    int monotone = 1;
    for (int t = 0; t < 3; t++)
        if (Detail(t) > Detail(t + 1)) monotone = 0;
    CHECK(monotone, "the detail ladder clamps DOWN and only down");

    // EVEN WHOLE NUMBERS ONLY. Whole, because the shader wraps every hash at this
    // value to close the u seam and a fractional period puts a bright line down
    // one radius of the ring; EVEN, because the coarse tear mask samples at half
    // this frequency and its period has to close too.
    int ok = 1;
    for (int t = 0; t <= 3; t++) {
        float n = Detail(t);
        if (fabsf(n - floorf(n + 0.5f)) > 1e-4f) ok = 0;
        if (fabsf(n * 0.5f - floorf(n * 0.5f + 0.5f)) > 1e-4f) ok = 0;
    }
    CHECK(ok, "and every tier's cell count is EVEN, so the half-frequency mask closes too");
}

int main(void)
{
    printf("=== P5 shock ring (the ring off the ground) ===\n");
    Test_ItHasThicknessOutOfItsOwnPlane();
    Test_CrestLeadsAndLandsOnAVertex();
    Test_ShadingIsAuthoredAndAsymmetric();
    Test_LifeEnvelope();
    Test_TierLadder();
    Test_DetailIsPixelsNotVertices();
    Test_MirrorMatchesTheSource();
    Test_ItIsAnErodedRopeNotGeneratedStrands();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

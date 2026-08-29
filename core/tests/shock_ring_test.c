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

#define SHOCK_MAX_SLICES   96
#define SHOCK_RADIALS      13
#define SHOCK_CREST_U      0.5f
#define SHOCK_CREST_K      1.0f
#define SHOCK_CORE_RATIO   0.22f
#define SHOCK_CANVAS_MUL   5.2f
#define SHOCK_THICK_RATIO  0.30f
#define SHOCK_FLARE_RATIO  0.34f
#define SHOCK_FLARE_K      1.35f
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
static float Flare(float u)
{
    if (u >= SHOCK_CREST_U) return 0.0f;
    return powf((SHOCK_CREST_U - u) / SHOCK_CREST_U, SHOCK_FLARE_K);
}
static float FlareHeight(float canvas, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return canvas * SHOCK_FLARE_RATIO * SmoothStep01(t01 / 0.45f);
}
// Mirrors ShockRing_Detail's tier table (the even-rounding there is identity at
// s_shockDetail = 1). It read 96/72/48/32 long after the shader stopped wanting
// a high cell count, which made the "detail exceeds slices" assertion below
// pass on a stale number rather than on the code.
static float Detail(int tier)
{
    switch (tier) { case 3: return 32.0f; case 2: return 26.0f; case 1: return 20.0f; default: return 16.0f; }
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
    switch (tier) { case 3: return SHOCK_MAX_SLICES; case 2: return 64; case 1: return 40; default: return 16; }
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
              "the section has real depth, not a plane with a nudge",
              "%.2f of the band's width", half / band);

    // BUT THE DEPTH IS ALL BEHIND THE FRONT. `half` and the bell are one
    // displacement now, scaled by Flare(u), which is zero at and beyond the
    // crest: the front is where the two swept sheets COINCIDE, so it draws as
    // one line instead of two parallel ones. Everything the ring has out of its
    // plane is inward of the crest, where the trailing material is.
    float canvas3 = CanvasWidth(3.0f);
    float depthAtFront = (half + FlareHeight(canvas3, 0.35f)) * Flare(0.78f);
    float depthBehind = (half + FlareHeight(canvas3, 0.35f)) * Flare(0.10f);
    CHECK_MSG(depthAtFront == 0.0f && depthBehind > half,
              "...and all of it is behind the crest, so the front is a single line",
              "front %.4f behind %.4f half %.4f", depthAtFront, depthBehind, half);

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
          FileHas("core/shaders/shock_ring.fs",
                  "finalColor = VFX_ResolvePremultiplied(hotCol, u_emission * 0.30, ga,"),
          "...and the emission pass premultiplies in the shader, since the blend no longer does");
    // COVERAGE AND BRIGHTNESS MUST STAY SEPARATE ARGUMENTS. Delivered as
    // bodyColor * intensity * a, the only way to make a core bright enough to
    // bloom is to make it nearly opaque — and the premultiplied dst*(1-a) term
    // then erases the BODY pass drawn underneath, so the ring comes out as bare
    // white filaments with no smoke around them. The six-argument form adds
    // light without adding coverage.
    CHECK(FileHas("core/shaders/shock_ring.fs", "hotCol, glowMask, u_emission);"),
          "the glow is delivered as the emission triple, so it does not erase the body pass");
    CHECK(FileHas(inl, "VFXRender_BeginDraw(") &&
          FileHas(inl, "false);") && FileHas(inl, "rlDisableBackfaceCulling();"),
          "no depth write, both walls — flushed on both sides");
    // THE GAIN IS 7.0 AND IT IS NOT A TASTE SETTING — IT IS A THRESHOLD.
    // main.c sets bloomThreshold = 1.25 in HDR luma, so an emission pass that
    // never reaches it does not glow, however "bright" its numbers look. At the
    // previous 2.50 the arithmetic was: coverage(~0.7) * 0.70 * life alpha(~0.6)
    // = 0.29, times a whitened fire glow of luma 0.64, times 2.50 = 0.47 — less
    // than half of what blooms. Every term looked reasonable on its own and the
    // ring rendered as matte orange paper. Lower this and it stops glowing.
    CHECK(FileHas(inl, "ShockRing_SetUniforms(m, (pass == 0) ? alpha * 1.75f : alpha * 0.90f,") &&
          FileHas(inl, "(pass == 0) ? 1.0f : 7.00f, t01, seed,") &&
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

static void Test_TheSheetIsGone(void)
{
    const char *fs = "core/shaders/shock_ring.fs";
    const char *inl = "core/composition/common/vc_shock_ring.inl";

    // 26/08/2026, owner decision. Every visible part of this ring used to come
    // out of an authored thin-smoke strip; once the shader grew a real leading
    // edge, the sheet's smoke read as lint around it. The asset and its
    // generator stay in the tree — nothing here binds them.
    CHECK(!FileHas(fs, "sampler2D") && !FileHas(fs, "texture(texture0"),
          "the shader samples no texture at all");
    CHECK(!FileHas(inl, "VFX_SurfaceRegistry_Get(") && !FileHas(inl, "rlSetTexture("),
          "and the composer looks up and binds no surface");

    // THE UNIFORM SET MUST SHRINK WITH IT, and this is the trap in removing a
    // texture path rather than a cosmetic tidy-up: a uniform the shader no
    // longer reads is optimised out, GetShaderLocation returns -1, and
    // ShockRing_HasShader — which requires every location to be >= 0 — then
    // reports the shader as unusable. The primitive silently falls back to the
    // old clean analytic hoop, with no error anywhere.
    CHECK(!FileHas(inl, "u_hasSmoke") && !FileHas(inl, "u_layerDetail") &&
          !FileHas(inl, "u_layerPhase") && !FileHas(inl, "u_detail"),
          "and the dead uniforms went too, or HasShader would fail the whole shader");
    CHECK(!FileHas(inl, "ShockRing_Detail(") && !FileHas(inl, "SHOCK_DETAIL_EARLY"),
          "...along with the tier ladder that fed them");
}

static void Test_TheWakeIsProceduralAndGraded(void)
{
    const char *fs = "core/shaders/shock_ring.fs";

    // WHAT THE WAKE IS FOR. The mesh's bell opens INWARD of the front, so with
    // nothing drawn behind the line the whole three-dimensional section is
    // invisible and the ring is a flat glowing loop. The wake is the front's own
    // trailing tail and it is what the bell is drawn on.
    CHECK(FileHas(fs, "float wx = clamp((frontV - v) / max(wakeLen, 0.03), 0.0, 1.0);") &&
          FileHas(fs, "float wake = (dR > 0.0) ? 0.0 : fall * fall;"),
          "the tail hangs INWARD off the front, never ahead of it");

    // LENGTH IS IN v UNITS AND v ONLY SPANS 1.0. Written with the wrong factors
    // the tail reached 1.5 — one and a half canvases — so it ran past the hole
    // and the ring rendered as a filled blob with a bright outline.
    CHECK(FileHas(fs, "float wakeLen = widA * outGain * mix(1.30, 1.70, u_t01) * mix(0.35, 1.55, wakeN);"),
          "...and its length stays a fraction of the section, not a multiple of the canvas");

    // ── THE CONTRAST CLIFF, WHICH COST FOUR RENDERS TO FIND ─────────────────
    //
    // Each octave of a value noise is an independent sample near 0.5 and FbmRing
    // divides by the total amplitude, so a three-octave field already sits in a
    // narrow band around the mean; SUMMING two such fields narrows it again. The
    // first version of this tail did both and measured essentially constant — a
    // debug pass that painted the field straight out came back flat grey — so
    // every threshold written against 0..1 stopped carving and started acting as
    // an on/off switch for the whole ring: smooth airbrushed band at one
    // setting, bare wire 0.18 higher, nothing in between.
    //
    // Over-correcting has a cliff of its own. Stretched 3.2x the field clamps
    // BIMODAL, mostly exact 0 and exact 1, and the threshold again has nothing
    // to bite on — sweeping the tear from 0.30 to 0.92 across the whole life
    // changed the late ring almost not at all.
    CHECK(FileHas(fs, "float streak = FbmRing(vec2(u * 30.0, wx * 0.85 + 31.0 + u_seed), 30.0, 2);"),
          "two octaves, not three, so the field keeps a usable range");
    CHECK(FileHas(fs, "float grain = clamp((streak - 0.5) * 1.90 + 0.5, 0.0, 1.0);"),
          "...stretched enough for a threshold to carve, not so far that it clamps bimodal");
    CHECK(FileHas(fs, "grain = clamp(grain * (0.55 + 0.90 * fine), 0.0, 1.0);") &&
          !FileHas(fs, "streak * 0.66 + fine * 0.42"),
          "...and the detail is MULTIPLIED in, since adding it is the averaging that flattened it");

    // FEATURES LONG IN THE RADIAL DIRECTION. This is why a procedural field is
    // acceptable here where the one it replaced was a "necklace": the shape of
    // the sample domain does the work — high angular frequency against a low
    // radial one — rather than a warp trying to hide a period.
    CHECK(FileHas(fs, "u * 30.0, wx * 0.85") && FileHas(fs, "u * 64.0, wx * 1.70"),
          "the sample domain is stretched radially, so the tail is streaks not blobs");

    // FOUR MULTIPLICATIVE GATES EACH AVERAGING A HALF LEAVE A SIXTEENTH. The
    // falloff, the shading, the tear and the two angular gates are all
    // fractions; their product put the tail at ~0.06 coverage — present in the
    // arithmetic, invisible on screen. Gained back in ONE place rather than by
    // inflating each gate until none of them gates.
    CHECK(FileHas(fs, "wake = clamp(wake * 2.10, 0.0, 1.0) * mix(1.0, 0.55, u_t01);"),
          "the gate product is compensated once, and the tail still thins with age");

    // THE TEARING climbs quadratically, so the tail comes apart from a
    // continuous skirt into separate strands as the ring ages.
    CHECK(FileHas(fs, "float tear = mix(0.30, 0.92, t2) + (clump - 0.5) * 0.30;") &&
          FileHas(fs, "float t2 = u_t01 * u_t01;"),
          "erosion begins during the expansion and finishes it off");

    // THE TAIL IS LIT BY ITS FILAMENTS, NOT AS A MASS. At 0.30 with a gain of 7
    // every fragment of the skirt arrives at 2.1 in HDR — clipped, so the whole
    // thing tone-maps to one flat saturated orange and every bit of structure
    // behind it is thrown away.
    CHECK(FileHas(fs, "float glowMask = clamp(rim * 2.80 + ember * 1.15 + wake * 0.08, 0.0, 1.0) *"),
          "the tail's mass is barely emissive; its filaments carry the light");
    CHECK(FileHas(fs, "float ember = pow(smoothstep(0.74, 0.98, grain), 2.0) * wake *"),
          "...and those filaments are the top of the grain range, thinned");

    // VALUE LIVES ON THE RADIUS.
    CHECK(FileHas(fs, "float heat = 1.0 - wx;") &&
          FileHas(fs, "vec3 coal = u_bodyColor.rgb * mix(0.16, 1.00, heat * heat);"),
          "and the tail cools behind the front rather than being one flat colour");

    // Driven by the ring's own life, never by wall clock.
    CHECK(!FileHas(fs, "u_time"), "nothing in the silhouette is driven by u_time");

    // The canvas fade is taken on RAW v, so no polygon edge cuts the tail.
    CHECK(FileHas(fs, "float edge = smoothstep(0.0, 0.05, v) * "
                      "(1.0 - smoothstep(0.93, 1.0, v));"),
          "the canvas fade is taken on RAW v, so no polygon edge cuts the tail");

    // The noise lattice still wraps, or the u seam shows a bright radius.
    CHECK(FileHas(fs, "float x0 = mod(i.x, period);") &&
          FileHas(fs, "p *= 2.0;") && FileHas(fs, "period *= 2.0;"),
          "every hash wraps at its period, and octaves double the period too");
}

static void Test_DetailIsPixelsNotVertices(void)
{
    // The noise cell count is a fragment-stage pattern and is INDEPENDENT of the
    // slice count — the two ladders are allowed to disagree, which is the point.
    //
    // THE OLD ASSERTION HERE WAS "detail > slices", and it is obsolete twice
    // over. The cell count was deliberately dropped to ~32 because a high one
    // produces a necklace (see ShockRing_Detail's own comment), and the slice
    // count was raised to 96 because the leading edge is a LINE and a line shows
    // faceting a diffuse band hid. Both moved for good reasons and in opposite
    // directions; what still has to hold is that neither is derived from the
    // other, and that the cell count stays in the range that reads as clumps
    // rather than as a chain of beads.
    CHECK_MSG(Detail(3) >= 16.0f && Detail(3) <= 48.0f,
              "the angular cell count stays in the clump range, not the necklace range",
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

// ── The 2026-08-25 redesign: a bell with a leading edge ────────────────────
//
// What the ring was before this, and why none of it is a taste argument: a flat
// annulus (half-thickness 6% of the radius) of evenly-lit torn smoke, with no
// front. It read as a decal — correct in every part and inert as a whole.

static void Test_TheSectionIsABellOpeningInward(void)
{
    const char *inl = "core/composition/common/vc_shock_ring.inl";

    // THE BELL EXISTS AND IT IS NOT A TOKEN. Against a canvas of 1.14 * radius,
    // a flare ratio of 0.34 puts the inner lip about a third of a radius out of
    // the plane — the same order as the ring's own visible width, which is what
    // it takes to read as a volume rather than as a thicker line.
    CHECK(FileHas(inl, "#define SHOCK_FLARE_RATIO 0.34f"),
          "the section flares out of the ring's plane by a real fraction of itself");

    // IT OPENS INWARD. Outward it was drawn twice, half a radius apart, because
    // the two sheets at ±offset are widest exactly where the front sits — the
    // bright leading edge came out as two concentric wire circles. Inward, the
    // front lands where the sheets COINCIDE.
    CHECK_MSG(Flare(SHOCK_CREST_U) == 0.0f && Flare(1.0f) == 0.0f,
              "and it is flat at the crest and everywhere outside it",
              "crest %.4f outer %.4f", Flare(SHOCK_CREST_U), Flare(1.0f));
    CHECK_MSG(Flare(0.0f) > 0.99f,
              "...opening toward the INNER base, where the trailing smoke is",
              "%.4f", Flare(0.0f));
    int rises = 1;
    for (float u = 0.0f; u < SHOCK_CREST_U - 0.01f; u += 0.01f)
        if (Flare(u) < Flare(u + 0.01f) - 1e-5f) rises = 0;
    CHECK(rises, "...monotonically, so the skirt cannot fold back on itself");

    // A BELL, NOT A CONE. The exponent keeps the section flat near the crest and
    // opens it late; a straight ramp reads as a folded paper funnel.
    CHECK_MSG(Flare(SHOCK_CREST_U * 0.5f) < 0.5f,
              "and the exponent holds it flat near the crest, so it is a bell",
              "half-way %.4f vs linear 0.5", Flare(SHOCK_CREST_U * 0.5f));

    // IT OPENS OVER THE LIFE. At release nothing has had time to be thrown out
    // of the plane.
    float canvas = CanvasWidth(3.0f);
    CHECK_MSG(FlareHeight(canvas, 0.02f) < FlareHeight(canvas, 0.60f) * 0.20f,
              "the lip opens as the ring travels rather than being born open",
              "%.4f vs %.4f", FlareHeight(canvas, 0.02f), FlareHeight(canvas, 0.60f));

    // A BELL'S SILHOUETTE IS WHAT THE EYE READS, so it needs samples across it.
    // Seven was plenty for a flat lens and is a visible polyline for this.
    CHECK_MSG(SHOCK_RADIALS >= 11 && (SHOCK_RADIALS % 2) == 1,
              "enough radials to draw the bell, and odd so the crest still lands on one",
              "%d", SHOCK_RADIALS);

    // AND THE SLICE COUNT WENT UP FOR THE SAME KIND OF REASON: the leading edge
    // is a LINE, and a line makes faceting visible that a diffuse band hid.
    CHECK_MSG(SHOCK_MAX_SLICES >= 96, "and enough slices that the front is not a polygon",
              "%d", SHOCK_MAX_SLICES);
}

static void Test_TheFrontIsTheIdentity(void)
{
    const char *fs = "core/shaders/shock_ring.fs";
    const char *inl = "core/composition/common/vc_shock_ring.inl";

    // NOT AN ISO-CONTOUR. The band is taken on `v`, the canvas COORDINATE, which
    // is monotonic across the section — one crossing, one line. Taken on a
    // density field (a hump) any level below the peak is crossed twice and the
    // ring double-edges; the shader header records that failure at length.
    CHECK(FileHas(fs, "float dR = v - frontV;"),
          "the leading edge is a band on the canvas coordinate, not a level set");

    // ASYMMETRIC. Symmetric it is a tube: equally soft both sides, so nothing in
    // it says which way the ring is travelling and it reads as a glowing wire
    // laid on the smoke.
    CHECK(FileHas(fs, "float rimOut = max(rimW * 0.34, 0.005);") &&
          FileHas(fs, "float rimIn = max(rimW * 1.30, 0.011);"),
          "...hard on the leading side and trailing behind, like a discontinuity");

    // IT IS NOT A CIRCLE, at three scales: it rides the rope's own wandering
    // centre-line, it carries a slow and a fast radial wander of its own, and
    // its width varies along its length.
    CHECK(FileHas(fs, "float frontF = FbmRing(vec2(u * 23.0, 151.0 + u_seed), 23.0, 2) - 0.5;") &&
          FileHas(fs, "frontN * 1.30 + frontF * 0.70"),
          "...and it wanders at two frequencies, so it is never locally straight");
    CHECK(FileHas(fs, "float rimT = FbmRing(vec2(u * 6.0, 101.0 + u_seed), 6.0, 2);"),
          "...with its thickness varying along its length");

    // IT BURNS IN ARCS. Measured on its own with the tail switched off, an
    // evenly bright front is a neon hoop. The gate reaches 0.10, i.e. some arcs
    // have no leading edge at all.
    CHECK(FileHas(fs, "mix(0.10, 1.35, rimArc)"),
          "...and it is violent in places and absent in others");

    // AND IT DIES FIRST. On t01^2 it was still at 47% three quarters through the
    // life, so the late ring read as a thick unbroken rope instead of shreds.
    CHECK(FileHas(fs, "float rimLife = pow(max(1.0 - u_t01, 0.0), 2.4);"),
          "...and it is gone well before the tail has finished dispersing");

    // THE SILHOUETTE VARIES AT LOW FREQUENCY. A rope of constant section is the
    // "necklace" one level up: however irregular the erosion inside it, the
    // outline is still a circle and the eye reads that first.
    CHECK(FileHas(fs, "float lobeN = FbmRing(vec2(u * 4.0, 61.0 + u_seed), 4.0, 2);") &&
          FileHas(fs, "float outGain = 1.0 + lobe * 2.60 * (0.30 + u_t01);"),
          "a few arcs throw the front much further out than the rest");
    CHECK(FileHas(fs, "float coreV = u_coreV + wob * 0.42;"),
          "...and the centre-line's own wander is large enough to read in RADIUS");

    // COLOUR RANGE COMES FROM NOT PRE-WHITENING. Whitened 0.32 the glow is
    // already (1.00, 0.56, 0.37), so at the emission gain every lit part
    // saturates to the same cream and the effect has one colour. Near the
    // element's own hue the tone map clips the channels in order and does the
    // black-body ramp — white core, yellow shoulder, orange falloff, red edge —
    // for free. Measured: chroma 0.357 -> 0.521 on a dark plate, 0.284 -> 0.420
    // on mid, with nothing else changed.
    CHECK(FileHas(inl, "ColorNormalize(VC_Whiten(m->glow, 0.08f))"),
          "the glow keeps the element hue, so the HDR gain can ramp it to white");
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
    Test_TheSectionIsABellOpeningInward();
    Test_TheFrontIsTheIdentity();
    Test_MirrorMatchesTheSource();
    Test_TheSheetIsGone();
    Test_TheWakeIsProceduralAndGraded();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

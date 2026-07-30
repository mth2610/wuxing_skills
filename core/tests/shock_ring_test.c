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
#define SHOCK_CREST_U      0.6666667f
#define SHOCK_CREST_K      1.7095f
#define SHOCK_BAND_RATIO   0.22f
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
static float BandWidth(float rNow) { return rNow * SHOCK_BAND_RATIO; }
static float HalfThickness(float band, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return band * SHOCK_THICK_RATIO * SmoothStep01(t01 / 0.10f) * powf(1.0f - t01, 1.1f);
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
    return SmoothStep01(t01 / 0.08f) * powf(1.0f - t01, 1.1f);
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
    float band = BandWidth(3.0f);
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
        float b = BandWidth(r);
        float ratio = HalfThickness(b, 0.35f) / b;
        float ref = HalfThickness(BandWidth(1.0f), 0.35f) / BandWidth(1.0f);
        float e = fabsf(ratio - ref) / ref;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f,
              "the section keeps its shape at every radius — thickness is a ratio "
              "against the band, not against the world",
              "worst relative drift %.7f", worst);

    // The ring THINS as it spreads: a shell of material being stretched, not a
    // solid hoop growing.
    CHECK_MSG(HalfThickness(BandWidth(3.0f), 0.85f) <
              HalfThickness(BandWidth(3.0f), 0.25f),
              "it is fattest just after release and thins as it goes",
              "%.4f late vs %.4f early", HalfThickness(BandWidth(3.0f), 0.85f),
              HalfThickness(BandWidth(3.0f), 0.25f));
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

    // A LEADING crest means more of the band is inside it than outside — a
    // symmetric doughnut is a ripple, not a blast.
    CHECK_MSG(SHOCK_CREST_U > 0.55f, "the crest leads: the long slope trails inside",
              "crest at %.3f of the band", SHOCK_CREST_U);
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
    float uIn = 0.30f;
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

    // Sharper than the ground wave's 2.2 — a mid-air shock has no ground drag to
    // explain a long tail.
    CHECK_MSG(Radius01(0.5f) > 0.8f, "it is most of the way out by halfway",
              "%.3f", Radius01(0.5f));

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
    CHECK_MSG(Alpha01(0.5f) > 0.4f,
              "and is still clearly visible halfway through — not a flash that "
              "leaves a ghost expanding",
              "%.3f", Alpha01(0.5f));
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

    CHECK(FileHas(inl, "#define SHOCK_CREST_U 0.6666667f"), "the crest is what this test mirrors");
    CHECK(FileHas(inl, "#define SHOCK_THICK_RATIO 0.30f"), "so is the out-of-plane thickness");
    CHECK(FileHas(inl, "#define SHOCK_BAND_RATIO 0.22f"), "and the band");

    // THE LENS. Two sweeps at +offset and -offset is the mechanical statement of
    // "it has a section", and it is the only thing separating this from a flat
    // annulus.
    CHECK(FileHas(inl, "for (int side = 0; side < 2; side++)"),
          "both faces of the lens are swept");
    CHECK(FileHas(inl, "float sgn = (side == 0) ? 1.0f : -1.0f;"),
          "...at +offset and -offset out of the plane");
    CHECK(FileHas(inl, "curRing[i] = Vector3Add(p, Vector3Scale(n, sgn * ringOff[i]));"),
          "and the displacement is along the ring's OWN normal");

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
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); BeginBlendMode(BLEND_ADDITIVE); "
                       "rlDisableDepthMask(); rlDisableBackfaceCulling(); "
                       "rlDrawRenderBatchActive();"),
          "it EMITS: additive, no depth write, both walls — flushed on both sides");
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); "
                       "rlEnableDepthMask(); EndBlendMode(); rlDrawRenderBatchActive();"),
          "and the restore is flushed too");

    // Authored shading, not a lit material.
    CHECK(!FileHas(inl, "Material_Begin("),
          "no lit material — it would be black-on-black in the night arena");
    CHECK(FileHas(inl, "ringCol[i] = VC_WithAlpha(c, (unsigned char)(alpha * ShockRing_Shade(u) * 255.0f));"),
          "the shading is authored into the vertex colour");
    CHECK(FileHas(inl, "Color c = VC_MixColor(m->body, m->glow, h);"),
          "and the colours come from VFX_Material, body at the bases, glow at the crest");

    // Static storage, no allocation, no shake.
    CHECK(FileHas(inl, "static Color ringCol[SHOCK_RADIALS];"), "static storage");
    CHECK(!FileHas(inl, "malloc("), "no allocation");
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");
}

int main(void)
{
    printf("=== P5 shock ring (the ring off the ground) ===\n");
    Test_ItHasThicknessOutOfItsOwnPlane();
    Test_CrestLeadsAndLandsOnAVertex();
    Test_ShadingIsAuthoredAndAsymmetric();
    Test_LifeEnvelope();
    Test_TierLadder();
    Test_MirrorMatchesTheSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

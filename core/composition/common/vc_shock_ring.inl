// ── P5. VFX_ComposeShockRing — the expanding ring, OFF the ground ────────────
//
// `VFX_ComposeGroundWave` conforms to terrain: it raycasts the map, it lifts
// itself off the surface, and its whole cross-section is a lip standing UP out of
// the ground. A ring in mid-air — an impact in the air, a parry, a barrier
// breaking — shares none of that, and the plan is right that it is a different
// primary rather than a NULL height function.
//
// BUT THE HEIGHT FUNCTION IS THE SMALLER HALF OF THE DIFFERENCE, and the larger
// half is what actually makes this worth writing:
//
//   A GROUND RING IS NEVER SEEN EDGE-ON. You look down at the floor, so a flat
//   annulus lying on it is always presented. A ring in mid-air is seen from every
//   angle including exactly along its own plane, and a flat annulus at that angle
//   is a LINE — it disappears at the one moment a parry ring most needs to read.
//
// So this ring has THICKNESS PERPENDICULAR TO ITS OWN PLANE: the cross-section is
// a LENS, swept around the circle, drawn as a front face at +normal and a back
// face at -normal. Edge-on you see the lens's silhouette; face-on you see the
// annulus; and because it is additive with culling off, grazing angles cross both
// faces and the rim brightens on its own — the same free fresnel a tube gets, for
// the same reason.
//
// It also takes an ORIENTATION, which a ground wave cannot: `normal` is the plane
// the ring expands in. A parry ring faces the parry; a barrier break faces the
// barrier. Passing (0,1,0) gives the horizontal ring, i.e. the ground wave's
// pose without the ground wave's cost.
//
// ── THE MESH IS A CANVAS, NOT THE RING ──────────────────────────────────────
//
// This primary spent its first life drawing a clean analytic hoop: a band sized
// to the visible front, shaded across seven radials, no texture and no shader.
// Nothing in that construction can produce a ragged edge, and no amount of
// vertex jitter fixes it — jittered vertices give a LUMPY hoop, which reads as
// crude geometry rather than as material coming apart. The raggedness is a
// per-PIXEL silhouette or it is nothing.
//
// So the sweep now builds a band roughly SHOCK_CANVAS_MUL times wider than the
// front it is drawing, carries UVs, and hands the question of which parts of
// that band exist to `core/shaders/shock_ring.fs`. That shader draws a CLOSED
// ROPE of smoke sitting at SHOCK_CREST_U and then TEARS it — the rest of the
// canvas width is room for the torn smoke to be stretched outward into as the
// ring expands. Sizing the mesh to the visible band caps that stretch at the
// geometry edge and the ring can only ever be a clean hoop.
//
// (The shader's own header records the attempt in between, which generated one
// strand per angular cell instead of tearing a continuous band. It is worth
// reading before proposing anything generative here again: one feature per cell
// is a comb by definition, and no warp or jitter removes the period.)
//
// The fallback path, when the shader fails to load, is the old clean hoop drawn
// across the canvas: readable, obviously not the intended look, never silent.
//
// LIT? NO — for the same reason as the ground wave, and it is a landmine rather
// than a preference (ENGINE_LANDMINES §3): a lit material in the night arena has
// almost no light to multiply by. The shading is AUTHORED vertex colour, and the
// blend law then requires additive + unlit, which is what it is.
//
// IMMEDIATE MODE: call every frame with `t01` running 0 -> 1. Called once it
// draws a single frame and looks like nothing happened.

#define SHOCK_MAX_SLICES 64
#define SHOCK_RADIALS 7 // across the canvas, inner base -> outer base
// Where the dense front sits across the CANVAS, and it is 1/3 EXACTLY for the
// reason the ground wave records: the canvas is only SHOCK_RADIALS samples
// across, so its vertices land at 0, 1/6, 2/6 ... 1. A crest that falls BETWEEN
// two of them is never built — the curve would be right and the mesh would be
// wrong, which is the shape of bug that survives a screenshot. 1/3 is vertex 2.
//
// It has moved twice. 2/3 when this drew a clean hoop and the crest had to LEAD;
// 1/3 when the band became a canvas and the room was needed outward. Now 1/2,
// because a canvas hanging mostly outward leaves a hole in the middle far larger
// than any reference shows — the smoke has to reach inward too. 1/2 is vertex 3
// of 6, so the mesh still builds the crest.
#define SHOCK_CREST_U 0.5f
#define SHOCK_CREST_K 1.0f // ln(0.5)/ln(1/2) — puts sin(PI*u^k)'s peak on the crest
// The VISIBLE front's width, as a fraction of the current radius. A shock front
// spreads as it travels; a front of fixed width reads as a hoop of constant
// section sliding outward.
#define SHOCK_CORE_RATIO 0.22f
// How much wider the mesh is than that front. This is the hard cap on how far
// the torn smoke can be stretched, in BOTH directions — and with the crest at
// the middle it is also what sets the size of the hole: the inner base sits at
// rNow - 0.5 * SHOCK_CORE_RATIO * SHOCK_CANVAS_MUL * rNow, so raising this
// closes the middle. Not a safety margin.
//
// THERE IS A CEILING. At 6.0 the inner base sits at 0.34 of the front radius and
// the smoke reaches nearly to the centre — where polar UVs pinch to a point, so
// the sheet is squeezed to zero circumference and renders as radial streaks
// converging on the middle. 5.2 leaves enough room for an inward smoke tail
// while keeping the visible annulus lean rather than doughnut-thick.
#define SHOCK_CANVAS_MUL 5.2f
// Half-thickness out of the plane, as a ratio against the FRONT's own width —
// the thickness rule (core/docs/LANDMINES.md, "Thickness is a ratio against the
// thing's OWN length"). Against the canvas it would fatten by 3x for free the
// day the canvas widened; against the radius the ring would grow a thicker and
// thicker wall as it expanded; against a constant it would flatten into a decal
// at large radii.
#define SHOCK_THICK_RATIO 0.30f
#define SHOCK_SHADE_FLOOR 0.30f
#define SHOCK_FACE_DIM 0.60f // the trailing (outer) slope, relative to the crest

// ── ONE RING, WHOSE CHARACTER CHANGES OVER ITS LIFE ─────────────────────────
//
// Reference shows a small sharp ring and, further out, progressively softer and
// fainter smoke — but those are the SAME ring at three moments, not three rings
// at one moment. So this draws once, and what varies is not the count but the
// reading:
//
//   · EARLY  — small, tight, sharp-edged, bright.
//   · LATE   — large, broad, smoothed, faint, coming apart.
//
// This was briefly built as time-staggered ECHOES, several copies of the front
// frozen at different ages and drawn together. That reproduces the picture but
// not the thing the picture is of: it puts three fronts on screen at once, and
// the ring stops being one object. It was also tried as three near-coincident
// High/Mid/Low instances (Thomas Pluys, 80.lv), which only thickens one edge and
// closes the middle. The resolution ladder that workflow describes belongs on
// the TIME axis here — the smoke really is sharper when it is young.
//
// `u_layerDetail` carries that: above 1 sharpens the sheet read, below 1 smooths
// it, and it is driven straight off t01.
#define SHOCK_DETAIL_EARLY 1.60f
#define SHOCK_DETAIL_LATE  0.50f

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int emission;
    int t01;
    int detail;
    int coreV;
    int seed;
    int hasSmoke;
    int layerPhase;
    int layerDetail;
    int hole;
} ShockRingShader;

static ShockRingShader s_shockShader = {0};
static bool s_shockInit = false;
static float s_shockBand = 1.0f;
static float s_shockThick = 1.0f;
static float s_shockAlpha = 1.0f;
static float s_shockDetail = 1.0f;
// How much of the canvas's inner edge is cut away, in v units. This is the size
// of the empty middle and it is a JUDGEMENT, not a derivation: three overlapping
// layers otherwise close the centre completely and the ring reads as a disc,
// while cutting too much leaves the thin annulus around a void that started this.
// Live-tunable because it is the one number here best settled by looking.
static float s_shockHole = 0.16f;

static void ShockRing_InitShared(void)
{
    if (s_shockInit)
        return;
    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("shock_band", &s_shockBand, 1.0f);
    Tuning_RegisterFloat("shock_thick", &s_shockThick, 1.0f);
    Tuning_RegisterFloat("shock_alpha", &s_shockAlpha, 1.0f);
    Tuning_RegisterFloat("shock_detail", &s_shockDetail, 1.0f);
    Tuning_RegisterFloat("shock_hole", &s_shockHole, 0.16f);
    s_shockInit = true;
}

static void ShockRing_InitShader(void)
{
    if (s_shockShader.shader.id != 0) return;
    s_shockShader.shader = ResourceManager_LoadShader("core/shaders/shock_ring.vs",
                                                      "core/shaders/shock_ring.fs");
    if (s_shockShader.shader.id == 0) return;
    s_shockShader.bodyColor = GetShaderLocation(s_shockShader.shader, "u_bodyColor");
    s_shockShader.glowColor = GetShaderLocation(s_shockShader.shader, "u_glowColor");
    s_shockShader.opacity = GetShaderLocation(s_shockShader.shader, "u_opacity");
    s_shockShader.emission = GetShaderLocation(s_shockShader.shader, "u_emission");
    s_shockShader.t01 = GetShaderLocation(s_shockShader.shader, "u_t01");
    s_shockShader.detail = GetShaderLocation(s_shockShader.shader, "u_detail");
    s_shockShader.coreV = GetShaderLocation(s_shockShader.shader, "u_coreV");
    s_shockShader.seed = GetShaderLocation(s_shockShader.shader, "u_seed");
    s_shockShader.hasSmoke = GetShaderLocation(s_shockShader.shader, "u_hasSmoke");
    s_shockShader.layerPhase = GetShaderLocation(s_shockShader.shader, "u_layerPhase");
    s_shockShader.layerDetail = GetShaderLocation(s_shockShader.shader, "u_layerDetail");
    s_shockShader.hole = GetShaderLocation(s_shockShader.shader, "u_hole");
}

static bool ShockRing_HasShader(void)
{
    return s_shockShader.shader.id != 0 && s_shockShader.bodyColor >= 0 &&
           s_shockShader.glowColor >= 0 && s_shockShader.opacity >= 0 &&
           s_shockShader.emission >= 0 && s_shockShader.t01 >= 0 &&
           s_shockShader.detail >= 0 && s_shockShader.coreV >= 0 &&
           s_shockShader.seed >= 0 && s_shockShader.hasSmoke >= 0 &&
           s_shockShader.layerPhase >= 0 && s_shockShader.layerDetail >= 0 &&
           s_shockShader.hole >= 0;
}

// ── The arithmetic, factored out so core/tests/shock_ring_test.c mirrors it ──

// Radius over the ring's life. Ease-OUT: a front is fastest at the instant it is
// released and decelerates, so a linear expansion reads as a growing circle
// rather than as something that was thrown.
//
// The reference releases most of its radius almost immediately.  It then holds
// near that radius while the smoke boundary keeps eroding and stretching; a
// gentle ease-out instead makes the late frames read as a circle still scaling.
static float ShockRing_Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.6f);
}

// The width of the front the eye is meant to read.
static float ShockRing_CoreWidth(float radiusNow)
{
    return radiusNow * SHOCK_CORE_RATIO * s_shockBand;
}

// The width of the geometry, which is larger — see the canvas note at the top.
static float ShockRing_CanvasWidth(float radiusNow)
{
    return ShockRing_CoreWidth(radiusNow) * SHOCK_CANVAS_MUL;
}

// Half-thickness out of the plane. It has its own envelope: the ring is at its
// fattest just after release and thins as it spreads, which is what makes it read
// as a shell of material being stretched rather than a solid hoop growing.
static float ShockRing_HalfThickness(float coreWidth, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float rise = SmoothStep01(t01 / 0.10f);
    float thin = powf(1.0f - t01, 1.1f);
    return coreWidth * SHOCK_THICK_RATIO * rise * thin * s_shockThick;
}

// Cross-canvas profile: 0 at both bases, 1 at the crest, crest at SHOCK_CREST_U
// rather than the middle — dead centre reads as a symmetric doughnut, which is a
// ripple, not a blast. One expression, so there is no piecewise seam to go
// discontinuous at the join.
static float ShockRing_Profile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return sinf(PI * powf(u, SHOCK_CREST_K));
}

// The self-shading that replaces real lighting. The crest carries most of it, and
// the INNER half is brighter than the outer at the same height — that asymmetry
// is the whole "lit from the blast centre" read, and it is the thing a flat
// additive decal cannot fake. `u <= crest` keeps the crest vertex on the BRIGHT
// side of the comparison, which matters because the crest sits exactly ON a
// vertex: as a step at the crest it would dim the band's own brightest point.
static float ShockRing_Shade(float u)
{
    float h = ShockRing_Profile(u);
    float face = (u <= SHOCK_CREST_U)
                     ? 1.0f
                     : Math_Mix(1.0f, SHOCK_FACE_DIM,
                                (u - SHOCK_CREST_U) / (1.0f - SHOCK_CREST_U));
    return Math_Mix(SHOCK_SHADE_FLOOR, 1.0f, h) * face;
}

static float ShockRing_Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float in = SmoothStep01(t01 / 0.08f);   // a shock ARRIVES; it does not fade in
    // 1.1 barely fades at all across the second half, so the ring was ending its
    // life as bright as it began — it has to DISPERSE, not just stop.
    float out = powf(1.0f - t01, 1.6f);
    return in * out;
}

// Tier budget: the only thing here that scales with fill, and the gate may only
// ever clamp DOWN. It is still a closed, thick ring at every tier.
static int ShockRing_Slices(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return SHOCK_MAX_SLICES;
    case GFX_MED:  return 40;
    case GFX_LOW:  return 24;
    default:       return 16;
    }
}

// Angular cell count of the smoke noise. INDEPENDENT of the slice count — the
// tearing is a fragment-stage pattern, so this buys detail without buying
// vertices, and the two ladders are allowed to disagree.
//
// SMALL. Raising this makes the ring WORSE, which is the opposite of what the
// name suggests. Cells are square in the world, so this number is also roughly
// how many features fit around the whole circumference: at 96 that is ninety-six
// similar shapes in a row, and the eye reads the chain, not the shapes. Around
// twenty-four large clumps, each with its own internal structure from the fbm
// octaves, reads as torn smoke; ninety-six small ones read as a necklace. Detail
// belongs in the octaves, not in the base frequency.
//
// It must be an EVEN WHOLE NUMBER. Whole, because the shader wraps every hash at
// this value to close the u seam and a fractional period puts a bright line down
// one radius of the ring; even, because the coarse tear mask samples at half
// this frequency and its period must close too.
static float ShockRing_Detail(void)
{
    float base;
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: base = 32.0f; break;
    case GFX_MED:  base = 26.0f; break;
    case GFX_LOW:  base = 20.0f; break;
    default:       base = 16.0f; break;
    }
    float n = 2.0f * floorf(base * s_shockDetail * 0.5f + 0.5f);
    return n < 8.0f ? 8.0f : n;
}

// ── The composition ─────────────────────────────────────────────────────────

static void ShockRing_SetUniforms(const VFX_ElementMaterial *m, float opacity,
                                  float emission, float t01, float seed,
                                  int hasSmoke)
{
    Vector4 body = ColorNormalize(m->body);
    Vector4 glow = ColorNormalize(VC_Whiten(m->glow, 0.32f));
    float detail = ShockRing_Detail();
    float coreV = SHOCK_CREST_U;
    SetShaderValue(s_shockShader.shader, s_shockShader.bodyColor, &body, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.glowColor, &glow, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.opacity, &opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.emission, &emission, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.t01, &t01, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.detail, &detail, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.coreV, &coreV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.seed, &seed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.hasSmoke, &hasSmoke, SHADER_UNIFORM_INT);
    // THE RESOLUTION LADDER LIVES ON THE TIME AXIS. Young smoke is sharp; old
    // smoke is diffuse. Driving it off t01 is what makes one ring pass through
    // the whole High/Mid/Low range instead of three rings each sitting at one
    // point of it.
    float layerDetail = Math_Mix(SHOCK_DETAIL_EARLY, SHOCK_DETAIL_LATE, t01);
    float layerPhase = 0.0f;
    SetShaderValue(s_shockShader.shader, s_shockShader.layerPhase, &layerPhase,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.layerDetail, &layerDetail,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.hole, &s_shockHole,
                   SHADER_UNIFORM_FLOAT);
}

// One sweep of the lens at `sgn` * offset, emitting UVs for the fragment stage:
// u runs around the ring and WRAPS at 1.0, v runs across the canvas from the
// inner base to the outer.
static void ShockRing_SweepFace(Vector3 center, Vector3 n, Vector3 axA, Vector3 axB,
                                const float *ringRad, const float *ringOff,
                                const Color *ringCol, int radials, int slices,
                                float sgn)
{
    static Vector3 prevRing[SHOCK_RADIALS];
    static Vector3 curRing[SHOCK_RADIALS];

    for (int s = 0; s <= slices; s++)
    {
        float ang = (float)s / (float)slices * 2.0f * PI;
        float ca = cosf(ang), sa = sinf(ang);
        float u1 = (float)s / (float)slices;
        float u0 = (float)(s - 1) / (float)slices;

        for (int i = 0; i < radials; i++)
        {
            Vector3 p = Vector3Add(center,
                                   Vector3Add(Vector3Scale(axA, ca * ringRad[i]),
                                              Vector3Scale(axB, sa * ringRad[i])));
            curRing[i] = Vector3Add(p, Vector3Scale(n, sgn * ringOff[i]));
        }

        if (s > 0)
        {
            for (int i = 0; i < radials - 1; i++)
            {
                float v0 = (float)i / (float)(radials - 1);
                float v1 = (float)(i + 1) / (float)(radials - 1);
                Color c0 = ringCol[i], c1 = ringCol[i + 1];
                rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                rlTexCoord2f(u0, v0);
                rlVertex3f(prevRing[i].x, prevRing[i].y, prevRing[i].z);
                rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                rlTexCoord2f(u0, v1);
                rlVertex3f(prevRing[i + 1].x, prevRing[i + 1].y, prevRing[i + 1].z);
                rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                rlTexCoord2f(u1, v1);
                rlVertex3f(curRing[i + 1].x, curRing[i + 1].y, curRing[i + 1].z);
                rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                rlTexCoord2f(u1, v0);
                rlVertex3f(curRing[i].x, curRing[i].y, curRing[i].z);
            }
        }
        for (int i = 0; i < radials; i++) prevRing[i] = curRing[i];
    }
}

// `normal` = the plane the ring expands in ((0,1,0) is horizontal). `radius` =
// where the front arrives at t01 = 1, in metres.
void VFX_ComposeShockRing(Vector3 center, Vector3 normal, VC_MaterialId mat,
                          float radius, float t01)
{
    ShockRing_InitShared();
    ShockRing_InitShader();
    if (radius <= 0.0f) radius = 3.0f;
    if (t01 <= 0.0f || t01 >= 1.0f) return;

    // A zero-length normal cannot be normalised to anything meaningful, and the
    // frame built from garbage spans nothing — every ring then collapses onto a
    // line, silently (core/docs/LANDMINES.md, 30/07). Checked SQUARED, before the
    // normalise, and defaulted rather than refused: a caller who forgot the
    // normal wants the horizontal ring, which is the overwhelmingly common case.
    if (Vector3LengthSqr(normal) < 1e-8f)
        normal = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 n = Vector3Normalize(normal);
    Vector3 axA, axB;
    VC_PlaneFrame(n, &axA, &axB);

    float alpha = ShockRing_Alpha01(t01) * s_shockAlpha;
    if (alpha <= 0.004f) return;

    float rNow = radius * ShockRing_Radius01(t01);
    float core = ShockRing_CoreWidth(rNow);
    float canvas = ShockRing_CanvasWidth(rNow);
    float half = ShockRing_HalfThickness(core, t01);
    if (core <= 0.001f) return;

    // Per-ring, stable across the ring's whole life so the strands do not
    // reshuffle every frame, but different for two rings in different places.
    float seed = fmodf(fabsf(center.x * 13.7f + center.y * 3.1f + center.z * 7.3f),
                       17.0f);

    const VFX_ElementMaterial *m = VFX_Material(mat);
    // The SHAPE comes from this simulated thin-smoke strip; noise only distorts
    // its UVs. This surface TILES: it is periodic in x, so the ring wraps it
    // directly instead of
    // remapping away blank ends. See scripts/gen_shock_ring_smoke.py for why it
    // is advected rather than generated from noise.
    const VFX_SurfaceProfile *smokeSurface =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_SHOCK_RING_SMOKE);
    const int hasSmoke = (smokeSurface != NULL && smokeSurface->body.id != 0) ? 1 : 0;
    const int slices = ShockRing_Slices();
    const int radials = SHOCK_RADIALS;

    // Colour and shading are the same for every slice, so they are computed once
    // across the canvas rather than per vertex. With the shader live these are
    // the fallback's colours; the shader reads its own uniforms and ignores them.
    static Color ringCol[SHOCK_RADIALS];
    static float ringOff[SHOCK_RADIALS]; // out-of-plane displacement, metres
    static float ringRad[SHOCK_RADIALS]; // distance from centre, metres
    for (int i = 0; i < radials; i++)
    {
        float u = (float)i / (float)(radials - 1);
        float h = ShockRing_Profile(u);
        // Body at the bases, glow at the crest: the hot line is where the
        // material is thinnest and moving fastest.
        Color c = VC_MixColor(m->body, m->glow, h);
        ringCol[i] = VC_WithAlpha(c, (unsigned char)(alpha * ShockRing_Shade(u) * 255.0f));
        ringOff[i] = half * h;
        // The CREST sits at rNow — that is where the front is. The canvas hangs
        // off it in both directions, mostly outward.
        ringRad[i] = rNow + canvas * (u - SHOCK_CREST_U);
        if (ringRad[i] < 0.0f) ringRad[i] = 0.0f;
    }

    // Two passes. The BODY pass is what carries the ring's colour: a purely
    // additive ring washes towards white and the element is lost, which is the
    // same reason the ground wave draws its front in body first. The EMISSION
    // pass is the bloom on top of it, deliberately weaker.
    //
    // Depth test on, depth WRITE off, and culling off so the far face shows
    // through the near one — that is what makes the rim brighten on its own.
    // Every one of those is flushed on BOTH sides, because rlgl draws the queued
    // geometry LATER and the state at DRAW time is what applies (ENGINE_LANDMINES
    // §1 and its 30/07 postscript on culling, which is exactly how a swept tube
    // came out as half a shell).
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 0) ScreenDistort_BeginVFXBody();
        else           ScreenDistort_BeginVFXEmission();
        rlDrawRenderBatchActive();
        if (pass == 0) BeginBlendMode(BLEND_ALPHA);
        else           BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        bool shaded = ShockRing_HasShader();
        if (shaded) SkillManager_BeginShader(s_shockShader.shader);

        if (shaded)
            ShockRing_SetUniforms(m, (pass == 0) ? alpha : alpha * 0.70f,
                                  (pass == 0) ? 1.0f : 2.50f, t01, seed, hasSmoke);
        rlSetTexture(shaded && hasSmoke ? smokeSurface->body.id : 0);
        rlBegin(RL_QUADS);
        // TWO SWEEPS, at +offset and -offset: the cross-section is a LENS, not a
        // flat annulus, and that is what survives being viewed along the ring's
        // own plane.
        for (int side = 0; side < 2; side++)
        {
            float sgn = (side == 0) ? 1.0f : -1.0f;
            ShockRing_SweepFace(center, n, axA, axB, ringRad, ringOff, ringCol,
                                radials, slices, sgn);
        }
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        if (shaded) SkillManager_EndShader();

        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
        rlDrawRenderBatchActive();
        ScreenDistort_EndVFXLayer();
    }
}

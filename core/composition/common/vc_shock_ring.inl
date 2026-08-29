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
// a BELL, swept around the circle, drawn as a front face at +normal and a back
// face at -normal. Edge-on you see the lens's silhouette; face-on you see the
// annulus; and because it is additive with culling off, grazing angles cross both
// faces and the rim brightens on its own — the same free fresnel a tube gets, for
// the same reason.
//
// It was a LENS until 25/08/2026 — a bump of half-thickness 0.066 * radius — and
// that is 6%, which is to say the ring was a DECAL. See SHOCK_FLARE_RATIO.
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
// ── AND THE SHADER DRAWS A FRONT, WHICH IS THE OTHER HALF OF THE IDENTITY ───
//
// A torn annulus of burning cloud can be beautiful and still not read as a
// SHOCK, because nothing in it says which way it is going. `shock_ring.fs` puts
// a leading edge on it: a thin band riding the outer side of the smoke, hard on
// its leading face and trailing behind, irregular in radius at two frequencies,
// burning in arcs, and gone well before the smoke has finished dispersing. The
// bell opens INWARD of that front, so the volume sits behind the edge where the
// material the front has already passed actually is — and so that the front
// lands where the two sheets coincide instead of being drawn twice.
//
// ── THE PATH THIS TOOK, SO IT IS NOT WALKED AGAIN ───────────────────────────
//
// 25/08: the sheet-smoke ring gained the bell and the front (measured: structure
// 0.649 -> 0.722 on a dark plate; white darken% 27 -> 18, because a ring made of
// thin bright lines adds light instead of occluding).
// 26/08: the authored sheet was removed entirely at the owner's request and the
// tail rebuilt procedurally; then reverted to the pre-redesign version; then
// returned here, to the no-texture build, at the owner's request. The sheet
// version is not the current one — do not reintroduce texture0 without asking.
//
// LIT? NO — for the same reason as the ground wave, and it is a landmine rather
// than a preference (ENGINE_LANDMINES §3): a lit material in the night arena has
// almost no light to multiply by. The shading is AUTHORED vertex colour, and the
// blend law then requires additive + unlit, which is what it is.
//
// IMMEDIATE MODE: call every frame with `t01` running 0 -> 1. Called once it
// draws a single frame and looks like nothing happened.

// The leading edge is a LINE, and a line makes faceting visible that a diffuse
// band hid completely: at 64 slices the front came out as a visible polygon.
#define SHOCK_MAX_SLICES 96
// Across the canvas, inner base -> outer base. It was 7 while the cross-section
// was a flat lens and seven samples were plenty to draw a bump. The section is
// now a FLARED BELL (see SHOCK_FLARE_RATIO), and a bell's silhouette is the
// thing the eye reads, so it needs enough samples not to be a polyline. Must be
// ODD so that SHOCK_CREST_U = 0.5 still lands exactly on a vertex.
#define SHOCK_RADIALS 13
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
// ── THE FLARE, AND WHY A FLAT ANNULUS WAS THE WHOLE PROBLEM ─────────────────
//
// Everything above describes a LENS: a bump of half-thickness 0.066 * radius,
// swept round. Measured against the radius that is 6% — which is to say the ring
// was, to the eye, a DECAL. It read as a texture lying in a plane no matter how
// good the texture was, because there was no silhouette for the light to travel
// across and nothing for the camera to see edge-on.
//
// A real shock front is not a hoop, it is a BELL: the material is thrown outward
// and, at the front, out of the plane as well. That shape is why an explosion
// ring reads as three-dimensional from every angle, and it is free here — the
// canvas already extends well outside the visible front, so the flare only has
// to displace part of it.
//
// A RATIO AGAINST THE CANVAS, not the radius (core/docs/LANDMINES.md, "Thickness
// is a ratio against the thing's OWN length"): the canvas is what is being bent,
// and against the radius the lip would grow taller than the ring is wide.
// Drawn twice at ±normal, so the section is a symmetric double bell — the
// classic vapour-cone silhouette — and grazing angles cross both walls, which is
// where the rim brightening comes from. It opens INWARD; see ShockRing_Flare.
#define SHOCK_FLARE_RATIO 0.34f
#define SHOCK_FLARE_K 1.35f // >1 keeps the bell flat near the crest and opens it late
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
// the TIME axis here, and the shader now reads it straight off u_t01: the front
// fades on (1-t)^2.4, the wake lengthens, and its tear threshold climbs
// quadratically. There is no layer uniform left to carry it.

typedef struct
{
    Shader shader;
    int bodyColor;
    int glowColor;
    int opacity;
    int emission;
    int t01;
    int coreV;
    int seed;
    int hole;
    int premultiply;
} ShockRingShader;

static ShockRingShader s_shockShader = {0};
static bool s_shockInit = false;
static float s_shockBand = 1.0f;
static float s_shockThick = 1.0f;
static float s_shockAlpha = 1.0f;
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
    s_shockShader.coreV = GetShaderLocation(s_shockShader.shader, "u_coreV");
    s_shockShader.seed = GetShaderLocation(s_shockShader.shader, "u_seed");
    s_shockShader.hole = GetShaderLocation(s_shockShader.shader, "u_hole");
    s_shockShader.premultiply = GetShaderLocation(s_shockShader.shader, "u_premultiply");
}

static bool ShockRing_HasShader(void)
{
    return s_shockShader.shader.id != 0 && s_shockShader.bodyColor >= 0 &&
           s_shockShader.glowColor >= 0 && s_shockShader.opacity >= 0 &&
           s_shockShader.emission >= 0 && s_shockShader.t01 >= 0 &&
           s_shockShader.coreV >= 0 && s_shockShader.seed >= 0 &&
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
    return 1.0f - powf(1.0f - t01, 3.8f);
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

// OUT-OF-PLANE DISPLACEMENT OF THE BELL: 0 at the crest, 1 at the INNER base.
//
// THE BELL OPENS INWARD, AND THAT IS NOT A FREE CHOICE. It was built opening
// outward first, on the reading that a shock front flares as it travels. Two
// things are wrong with that. Physically, the front is by definition the
// outermost thing there is — nothing can be thrown ahead of it, so a skirt on
// the far side of the front is smoke that arrived somewhere before the shock
// did. Visually it is worse: the ring is drawn as two sheets at ±offset, so a
// feature sitting where the flare is widest is drawn TWICE, half a radius
// apart, and the bright leading edge came out as two concentric wire circles
// instead of one line. Opening inward puts the front where the two sheets
// COINCIDE — one line — and puts the volume behind it, which is where the
// material that has already been passed by the front actually is.
//
// The exponent keeps the section nearly flat for the first part of that travel
// and opens it late, which is what makes a bell rather than a cone — a straight
// ramp reads as a folded paper funnel.
static float ShockRing_Flare(float u)
{
    if (u >= SHOCK_CREST_U) return 0.0f;
    float x = (SHOCK_CREST_U - u) / SHOCK_CREST_U;
    return powf(x, SHOCK_FLARE_K);
}

// How tall that bell is, in metres. It OPENS over the ring's life: at release
// the front is still a tight lens and there has been no time to throw anything
// out of the plane; by the time the radius has settled the lip is fully open.
// Tying it to the canvas (not the radius) is the thickness rule.
static float ShockRing_FlareHeight(float canvasWidth, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float open = SmoothStep01(t01 / 0.45f);
    return canvasWidth * SHOCK_FLARE_RATIO * open * s_shockThick;
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
    case GFX_MED:  return 64;
    case GFX_LOW:  return 40;
    default:       return 16;
    }
}

// ── The composition ─────────────────────────────────────────────────────────

static void ShockRing_SetUniforms(const VFX_ElementMaterial *m, float opacity,
                                  float emission, float t01, float seed,
                                  float premultiply)
{
    Vector4 body = ColorNormalize(m->body);
    // BARELY WHITENED, AND THAT IS THE COLOUR RANGE. Whitened 0.32 the glow is
    // (1.00, 0.56, 0.37) — already most of the way to white before the HDR gain
    // touches it, so at gain 7 every lit part of the ring saturates to the same
    // cream and the effect has one colour. Left near the element's own hue the
    // tone map does the black-body ramp for free: the channels clip in order, so
    // a core goes white, its shoulder yellow, its falloff orange and its edge
    // red. That ladder is most of what makes fire look like fire.
    Vector4 glow = ColorNormalize(VC_Whiten(m->glow, 0.08f));
    float coreV = SHOCK_CREST_U;
    SetShaderValue(s_shockShader.shader, s_shockShader.bodyColor, &body, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.glowColor, &glow, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shockShader.shader, s_shockShader.opacity, &opacity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.emission, &emission, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.t01, &t01, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.coreV, &coreV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.seed, &seed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shockShader.shader, s_shockShader.hole, &s_shockHole,
                   SHADER_UNIFORM_FLOAT);
    // The blend state and the fragment formula are ONE decision. The emission
    // pass is drawn premultiplied (ONE, ONE_MINUS_SRC_ALPHA), where the hardware
    // no longer applies coverage — so the shader has to. Setting one without the
    // other divides the two apart and brightens every soft edge by 1/alpha.
    SetShaderValue(s_shockShader.shader, s_shockShader.premultiply, &premultiply,
                   SHADER_UNIFORM_FLOAT);
}

// Metres lifted clear of the ground receiver to eliminate coplanar Z-fighting.
#define SHOCK_GROUND_LIFT 0.05f

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
            // When sweeping the underside (-n), clamp displacement so it stays above ground plane.
            float off = (sgn < 0.0f) ? fminf(ringOff[i], SHOCK_GROUND_LIFT * 0.8f) : ringOff[i];
            curRing[i] = Vector3Add(p, Vector3Scale(n, sgn * off));
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

    // Lift center along normal so ring bases clear ground/terrain to prevent Z-fighting
    center = Vector3Add(center, Vector3Scale(n, SHOCK_GROUND_LIFT));

    float alpha = ShockRing_Alpha01(t01) * s_shockAlpha;
    if (alpha <= 0.004f) return;

    float rNow = radius * ShockRing_Radius01(t01);
    float core = ShockRing_CoreWidth(rNow);
    float canvas = ShockRing_CanvasWidth(rNow);
    float half = ShockRing_HalfThickness(core, t01);
    float flareH = ShockRing_FlareHeight(canvas, t01);
    if (core <= 0.001f) return;

    // Per-ring, stable across the ring's whole life so the strands do not
    // reshuffle every frame, but different for two rings in different places.
    float seed = fmodf(fabsf(center.x * 13.7f + center.y * 3.1f + center.z * 7.3f),
                       17.0f);

    const VFX_ElementMaterial *m = VFX_Material(mat);
    // NO TEXTURE. This bound an authored thin-smoke strip until 26/08/2026 and
    // every visible part of the ring came out of it; the sheet went by owner
    // decision once the shader grew a real leading edge, because with a front to
    // look at the sheet's smoke read as lint around it. `shock_ring.fs` records
    // what the sheet cost and what replaced it. The asset and its generator are
    // still in the tree (VFX_SURFACE_SHOCK_RING_SMOKE,
    // scripts/gen_shock_ring_smoke.py) — nothing binds them here any more.
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
        // ONE displacement function, and it is ZERO AT AND BEYOND THE CREST.
        //
        // This was `half * h + flareH * Flare(u)` — a lens bump centred on the
        // crest, plus the bell. The bump is the problem: the front sits OUTSIDE
        // the crest (frontV is about 0.58 of the rope's half-width past it), and
        // sin(PI*u) is still 0.71 there, so the two sheets swept at ±half were a
        // visible distance apart exactly where the effect's sharpest feature is.
        // The leading edge rendered as TWO parallel bright lines, which is the
        // same failure the flare's direction was chosen to avoid, arriving by a
        // different term (ENGINE_LANDMINES.md, "a feature on a two-sided swept
        // sheet is drawn TWICE").
        //
        // Folding the lens into the bell means the section has no thickness
        // where the front is — so the front is one line — and all of its volume
        // behind, which is where the material is anyway. The edge-on case that
        // the lens existed for is now carried by the bell, which is far deeper
        // than the lens ever was.
        ringOff[i] = (half + flareH) * ShockRing_Flare(u);
        // The CREST sits at rNow — that is where the front is. The canvas hangs
        // off it in both directions, mostly outward.
        ringRad[i] = rNow + canvas * (u - SHOCK_CREST_U);
        if (ringRad[i] < 0.0f) ringRad[i] = 0.0f;
    }

    // Two passes, carrying DIFFERENT SIGNALS rather than the same one at two
    // strengths. The BODY pass is the ring's pigment: a purely additive ring
    // washes towards white and the element is lost, which is the same reason the
    // ground wave draws its front in body first, and it is also what lets the
    // ring attenuate bright scenery instead of only adding to it. The EMISSION
    // pass is the LIGHT, and it is driven by the shader's narrow core term, not
    // by the body's soft coverage.
    //
    // 25/08/2026: the emission gain was 2.50 and the ring did not glow at all —
    // no part of it ever crossed main.c's bloomThreshold of 1.25. Worth writing
    // the arithmetic down, because no single term looked wrong: coverage ~0.7 x
    // the 0.70 opacity factor x a life alpha of ~0.6 gave 0.29, and a whitened
    // fire glow of luma 0.64 x 2.50 x 0.29 arrives at 0.47. Half of what blooms,
    // from three innocuous-looking multiplications.
    //
    // Depth test on, depth WRITE off, and culling off so the far face shows
    // through the near one — that is what makes the rim brighten on its own.
    // Every one of those is flushed on BOTH sides, because rlgl draws the queued
    // geometry LATER and the state at DRAW time is what applies (ENGINE_LANDMINES
    // §1 and its 30/07 postscript on culling, which is exactly how a swept tube
    // came out as half a shell).
    for (int pass = 0; pass < 2; pass++)
    {
        VFXRenderScope renderScope = VFXRender_BeginDraw(
            pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
            // PREMULTIPLIED emission, not additive (20/08/2026): the ring is a
            // glowing thing, and additive over a bright destination can only
            // add to something already at 1.0. Paired with u_premultiply below.
            pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        bool shaded = ShockRing_HasShader();
        if (shaded) SkillManager_BeginShader(s_shockShader.shader);

        if (shaded)
            ShockRing_SetUniforms(m, (pass == 0) ? alpha * 1.75f : alpha * 0.90f,
                                  (pass == 0) ? 1.0f : 7.00f, t01, seed,
                                  (pass == 0) ? 0.0f : 1.0f);
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
        rlDrawRenderBatchActive();
        if (shaded) SkillManager_EndShader();

        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&renderScope);
    }
}

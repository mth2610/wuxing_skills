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
// LIT? NO — for the same reason as the ground wave, and it is a landmine rather
// than a preference (ENGINE_LANDMINES §3): a lit material in the night arena has
// almost no light to multiply by. The shading is AUTHORED vertex colour, and the
// blend law then requires additive + unlit, which is what it is.
//
// IMMEDIATE MODE: call every frame with `t01` running 0 -> 1. Called once it
// draws a single frame and looks like nothing happened.

#define SHOCK_MAX_SLICES 64
#define SHOCK_RADIALS 7 // across the band, inner base -> outer base
// Where the crest sits across the band, and it is 2/3 EXACTLY for the reason the
// ground wave records: the band is only SHOCK_RADIALS samples across, so its
// vertices land at 0, 1/6, 2/6 ... 1. A crest at 0.62 falls BETWEEN two of them
// and the geometry never builds the crest at all — the curve would be right and
// the mesh would be wrong, which is the shape of bug that survives a screenshot.
#define SHOCK_CREST_U 0.6666667f
#define SHOCK_CREST_K 1.7095f // ln(0.5)/ln(2/3) — puts sin(PI*u^k)'s peak on the crest
// Band width as a fraction of the CURRENT radius. A shock front spreads as it
// travels; a band of fixed width reads as a hoop of constant section sliding
// outward.
#define SHOCK_BAND_RATIO 0.22f
// Half-thickness out of the plane, as a ratio against the band's OWN width — the
// thickness rule (core/docs/LANDMINES.md, "Thickness is a ratio against the
// thing's OWN length"). Keyed to the radius instead, the ring would grow a
// thicker and thicker wall as it expanded; keyed to a constant it would flatten
// into a decal at large radii.
#define SHOCK_THICK_RATIO 0.30f
#define SHOCK_SHADE_FLOOR 0.30f
#define SHOCK_FACE_DIM 0.60f // the trailing (outer) slope, relative to the crest

static bool s_shockInit = false;
static float s_shockBand = 1.0f;
static float s_shockThick = 1.0f;
static float s_shockAlpha = 1.0f;

static void ShockRing_InitShared(void)
{
    if (s_shockInit)
        return;
    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("shock_band", &s_shockBand, 1.0f);
    Tuning_RegisterFloat("shock_thick", &s_shockThick, 1.0f);
    Tuning_RegisterFloat("shock_alpha", &s_shockAlpha, 1.0f);
    s_shockInit = true;
}

// ── The arithmetic, factored out so core/tests/shock_ring_test.c mirrors it ──

// Radius over the ring's life. Ease-OUT: a front is fastest at the instant it is
// released and decelerates, so a linear expansion reads as a growing circle
// rather than as something that was thrown. Sharper than the ground wave's 2.2 —
// a mid-air shock has no ground drag to explain a long tail.
static float ShockRing_Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.6f);
}

static float ShockRing_BandWidth(float radiusNow)
{
    return radiusNow * SHOCK_BAND_RATIO * s_shockBand;
}

// Half-thickness out of the plane. It has its own envelope: the ring is at its
// fattest just after release and thins as it spreads, which is what makes it read
// as a shell of material being stretched rather than a solid hoop growing.
static float ShockRing_HalfThickness(float bandWidth, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float rise = SmoothStep01(t01 / 0.10f);
    float thin = powf(1.0f - t01, 1.1f);
    return bandWidth * SHOCK_THICK_RATIO * rise * thin * s_shockThick;
}

// Cross-band profile: 0 at both bases, 1 at the crest, crest at SHOCK_CREST_U
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
    float out = powf(1.0f - t01, 1.1f);
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

// ── The composition ─────────────────────────────────────────────────────────

// `normal` = the plane the ring expands in ((0,1,0) is horizontal). `radius` =
// where the front arrives at t01 = 1, in metres.
void VFX_ComposeShockRing(Vector3 center, Vector3 normal, VC_MaterialId mat,
                          float radius, float t01)
{
    ShockRing_InitShared();
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
    float band = ShockRing_BandWidth(rNow);
    float half = ShockRing_HalfThickness(band, t01);
    if (band <= 0.001f) return;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    const int slices = ShockRing_Slices();
    const int radials = SHOCK_RADIALS;

    // Colour and shading are the same for every slice, so they are computed once
    // across the band rather than per vertex.
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
        ringRad[i] = rNow - band * 0.5f + band * u;
    }

    // Additive + unlit, per the blend law: this EMITS. Depth test on, depth WRITE
    // off, and culling off so the far face shows through the near one — that is
    // what makes the rim brighten on its own. Every one of those is flushed on
    // BOTH sides, because rlgl draws the queued geometry LATER and the state at
    // DRAW time is what applies (ENGINE_LANDMINES §1 and its 30/07 postscript on
    // culling, which is exactly how a swept tube came out as half a shell).
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlBegin(RL_QUADS);

    // TWO SWEEPS, at +offset and -offset: the cross-section is a LENS, not a flat
    // annulus, and that is what survives being viewed along the ring's own plane.
    for (int side = 0; side < 2; side++)
    {
        float sgn = (side == 0) ? 1.0f : -1.0f;
        static Vector3 prevRing[SHOCK_RADIALS];
        static Vector3 curRing[SHOCK_RADIALS];

        for (int s = 0; s <= slices; s++)
        {
            float ang = (float)s / (float)slices * 2.0f * PI;
            float ca = cosf(ang), sa = sinf(ang);
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
                    Color c0 = ringCol[i], c1 = ringCol[i + 1];
                    rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                    rlVertex3f(prevRing[i].x, prevRing[i].y, prevRing[i].z);
                    rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                    rlVertex3f(prevRing[i + 1].x, prevRing[i + 1].y, prevRing[i + 1].z);
                    rlColor4ub(c1.r, c1.g, c1.b, c1.a);
                    rlVertex3f(curRing[i + 1].x, curRing[i + 1].y, curRing[i + 1].z);
                    rlColor4ub(c0.r, c0.g, c0.b, c0.a);
                    rlVertex3f(curRing[i].x, curRing[i].y, curRing[i].z);
                }
            }
            for (int i = 0; i < radials; i++) prevRing[i] = curRing[i];
        }
    }

    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();
}

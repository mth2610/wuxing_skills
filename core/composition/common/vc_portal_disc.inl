// ── P6. VFX_ComposePortalDisc — the flat disc with a rim ─────────────────────
//
// WHY NOT `TRAIL_TYPE_PORTAL`, which the plan names as the unused thing to put to
// work. Look at what it actually draws (`core/trail_system.c`, the
// TRAIL_TYPE_PORTAL branch): `DrawCameraFacingQuad` — one billboard, spun about
// its own axis, scaled by a spawn ramp. That is a SPRITE. It is the same shape
// from every angle, it has no rim, and a portal seen edge-on would stay a fully
// presented square.
//
// A portal disc is defined by two things a billboard cannot express:
//   1. IT LIES IN A PLANE THE WORLD CHOSE, not the plane the camera is in. You
//      walk around a portal and it foreshortens. That is most of what makes it
//      read as a hole in space rather than a decal floating in front of you.
//   2. IT HAS A RIM, and the rim is where all the energy is. The interior is
//      DARK — additive adds nothing there, so what you see through the middle is
//      the scene behind, which is precisely the "somewhere else" read. Exactly
//      the argument `VFX_ComposeEnergyOrb` makes for a fresnel limb over stacked
//      shells: brightest through the CENTRE is backwards for anything that is
//      supposed to be a surface with a hole in it.
//
// So TRAIL_TYPE_PORTAL stays without a consumer, and it should be deleted rather
// than adopted — noted here so the next planning round does not re-propose it.
//
// THE INTERIOR SWIRL is polar UVs on the shared volume sheet: u wraps around the
// disc, v runs out from the centre, and u SCROLLS, which is a rotation of the
// pattern without rotating any geometry. Same technique as the black-hole swirl.
// The sheet is seamless on both axes by construction, so the wrap at u = 0/1 —
// which on a disc is a radial line straight through the middle — does not show.
//
// LIT? NO (ENGINE_LANDMINES §3). Authored vertex colour, additive, unlit.
//
// IMMEDIATE MODE: call every frame with `t01` running 0 -> 1. It OPENS by growing
// from nothing, holds, and CLOSES by collapsing — unlike a beam, which stops
// being fed and goes out, a portal shuts.

#define PORTAL_MAX_SLICES 72
#define PORTAL_RINGS 8 // across the radius, centre -> rim
// The rim: where it starts as a fraction of the radius, and how far past the
// disc's edge it reaches. It overhangs slightly so the bright line is not
// exactly the geometry's boundary — a rim that ends where the disc ends reads as
// a cut edge rather than as energy running around a hole.
#define PORTAL_RIM_INNER 0.86f
#define PORTAL_RIM_OUTER 1.07f
// Half-thickness of the rim out of the plane, as a ratio against the RIM's own
// width — the thickness rule again (core/docs/LANDMINES.md). Without it the rim
// is a flat annulus and vanishes when the portal is seen edge-on, which is the
// one angle a portal is most often approached from.
#define PORTAL_RIM_THICK 0.55f
// How fast the interior falls to black going inward. 2.4 keeps the middle
// genuinely empty: at 1.0 the whole disc glows evenly and it is a coin, not a
// hole.
#define PORTAL_CORE_FALLOFF 2.4f
#define PORTAL_INTERIOR_ALPHA 0.34f
#define PORTAL_RIM_ALPHA 0.85f
#define PORTAL_RIM_WHITEN 0.45f
// Open and close as fractions of t01, and the swirl's rate in turns/sec.
#define PORTAL_OPEN 0.18f
#define PORTAL_CLOSE 0.85f
#define PORTAL_SWIRL 0.42f
// Radial repeats of the sheet from centre to rim. More than a couple and the
// pattern crowds into an unreadable ring near the middle, where the
// circumference is smallest.
#define PORTAL_V_TILES 1.6f

static bool s_portalInit = false;
static Texture2D s_portalSheet = {0};
static float s_portalAlpha = 1.0f;
static float s_portalSwirl = 1.0f;
static float s_portalRim = 1.0f;

static void PortalDisc_InitShared(void)
{
    if (s_portalInit)
        return;
    // The same seamless-on-both-axes sheet P1 and P4 use. ResourceManager caches
    // by path, so this is that texture and not a second copy of it.
    s_portalSheet = ResourceManager_LoadTexture("assets/textures/energy_volume.png");
    if (s_portalSheet.id != 0)
    {
        SetTextureFilter(s_portalSheet, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_portalSheet, TEXTURE_WRAP_REPEAT);
    }
    else
    {
        // Announced: a missing sheet draws a smooth gradient disc, which looks
        // like a deliberate design choice rather than a failed load.
        TraceLog(LOG_WARNING,
                 "VFX_PORTAL: energy_volume.png missing — the interior draws smooth");
    }
    // Lazily, never from a subsystem Init (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("portal_alpha", &s_portalAlpha, 1.0f);
    Tuning_RegisterFloat("portal_swirl", &s_portalSwirl, 1.0f);
    Tuning_RegisterFloat("portal_rim", &s_portalRim, 1.0f);
    s_portalInit = true;
}

// ── The arithmetic, factored out so core/tests/portal_disc_test.c mirrors it ──

// Radius over the portal's life. It OPENS with an overshoot-free ease-out, holds
// at full, and COLLAPSES on the way out — a portal that fades at constant size
// reads as a decal being turned down.
static float PortalDisc_Scale(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    if (t01 < PORTAL_OPEN)
    {
        float k = t01 / PORTAL_OPEN;
        return 1.0f - powf(1.0f - k, 2.4f);
    }
    if (t01 > PORTAL_CLOSE)
    {
        float k = (1.0f - t01) / (1.0f - PORTAL_CLOSE);
        // CUBED, not squared, and the difference is measurable rather than a
        // matter of feel. Squared, the collapse crossed half size 0.044 into its
        // phase against the opening's 0.045 — the two were the SAME speed, so
        // "it snaps shut" was a comment describing something the curve did not
        // do. Cubed it crosses at 0.031, a clear 1.45x. Pinned in
        // core/tests/portal_disc_test.c as a phase-relative time, because the
        // two phases have different lengths and comparing raw t01 measures the
        // wrong thing (which is how the first version of that check passed on a
        // curve that was not snapping at all).
        return k * k * k;
    }
    return 1.0f;
}

static float PortalDisc_Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    if (t01 < PORTAL_OPEN) return SmoothStep01(t01 / PORTAL_OPEN);
    if (t01 > PORTAL_CLOSE) return (1.0f - t01) / (1.0f - PORTAL_CLOSE);
    return 1.0f;
}

// The interior, as a function of distance from the centre (0..1 at the rim's
// inner edge). Near zero in the middle — additive adds nothing there, so the
// scene behind shows through and the disc reads as a HOLE. Anything flatter than
// this is a coin.
static float PortalDisc_Interior(float rr01)
{
    if (rr01 <= 0.0f) return 0.0f;
    if (rr01 >= 1.0f) return 1.0f;
    return powf(rr01, PORTAL_CORE_FALLOFF);
}

// The rim's cross-section: 0 at both edges of the rim band, 1 in the middle of
// it. A lens, so the rim has a section rather than being a line.
static float PortalDisc_RimProfile(float w)
{
    if (w <= 0.0f || w >= 1.0f) return 0.0f;
    return sinf(PI * w);
}

// Tier budget. Clamps DOWN only, and it is still a disc with a rim at every
// tier — dropping the rim would give the low tier a different effect rather than
// a cheaper one, and the rim IS the portal.
static int PortalDisc_Slices(void)
{
    switch (GfxQuality_Get())
    {
    case GFX_HIGH: return PORTAL_MAX_SLICES;
    case GFX_MED:  return 48;
    case GFX_LOW:  return 32;
    default:       return 20;
    }
}

// ── The composition ─────────────────────────────────────────────────────────

// `normal` = the plane the portal lies in ((0,1,0) is flat on the ground, which
// is the summoning-seal pose; a wall portal faces along the wall's normal).
// `radius` = the disc's radius in metres at full open.
void VFX_ComposePortalDisc(Vector3 center, Vector3 normal, VC_MaterialId mat,
                           float radius, float t01)
{
    PortalDisc_InitShared();
    if (radius <= 0.0f) radius = 1.5f;
    if (t01 <= 0.0f || t01 >= 1.0f) return;

    // Checked SQUARED, before the normalise, and DEFAULTED rather than refused:
    // normalising ~zero returns garbage silently and the frame built on it spans
    // nothing (core/docs/LANDMINES.md, 30/07), while a caller who omitted the
    // normal almost certainly wants the flat-on-the-ground pose.
    if (Vector3LengthSqr(normal) < 1e-8f)
        normal = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 n = Vector3Normalize(normal);
    Vector3 axA, axB;
    VC_PlaneFrame(n, &axA, &axB);

    float alpha = PortalDisc_Alpha01(t01) * s_portalAlpha;
    float scale = PortalDisc_Scale(t01);
    if (alpha <= 0.004f || scale <= 0.004f) return;

    float rNow = radius * scale;
    float rimIn = rNow * PORTAL_RIM_INNER;
    float rimOut = rNow * PORTAL_RIM_OUTER;
    float rimHalf = (rimOut - rimIn) * PORTAL_RIM_THICK * s_portalRim;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    const int slices = PortalDisc_Slices();
    // The swirl is a UV SCROLL, not a geometry rotation: the disc never turns, so
    // its silhouette is rock steady while the material inside it moves. A
    // rotating disc reads as a spinning plate.
    float uScroll = TimeFX_Elapsed() * PORTAL_SWIRL * s_portalSwirl;

    // Additive + unlit per the blend law. Depth WRITE off — it emits, so it must
    // not occlude — and culling off, so the rim's far face shows through its near
    // one and the rim brightens on its own at grazing angles. Flushed on BOTH
    // sides of every one of those, because rlgl draws the queued geometry LATER
    // and the state at DRAW time is what applies (ENGINE_LANDMINES §1 + its
    // 30/07 postscript on culling).
    VFXRenderScope renderScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();

    // ── The interior, textured with POLAR UVs ───────────────────────────────
    rlSetTexture(s_portalSheet.id);
    rlBegin(RL_QUADS);
    for (int s = 0; s < slices; s++)
    {
        float a0 = (float)s / (float)slices * 2.0f * PI;
        float a1 = (float)(s + 1) / (float)slices * 2.0f * PI;
        float u0 = (float)s / (float)slices + uScroll;
        float u1 = (float)(s + 1) / (float)slices + uScroll;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);

        for (int r = 0; r < PORTAL_RINGS; r++)
        {
            float k0 = (float)r / (float)PORTAL_RINGS;
            float k1 = (float)(r + 1) / (float)PORTAL_RINGS;
            float rr0 = rimIn * k0, rr1 = rimIn * k1;
            float i0 = PortalDisc_Interior(k0), i1 = PortalDisc_Interior(k1);
            // Body deep inside, glow toward the rim: the material heats up as it
            // approaches the edge it is being held open by.
            Color cc0 = VC_WithAlpha(VC_MixColor(m->body, m->glow, i0),
                                     (unsigned char)(alpha * i0 * PORTAL_INTERIOR_ALPHA * 255.0f));
            Color cc1 = VC_WithAlpha(VC_MixColor(m->body, m->glow, i1),
                                     (unsigned char)(alpha * i1 * PORTAL_INTERIOR_ALPHA * 255.0f));
            float v0 = k0 * PORTAL_V_TILES, v1 = k1 * PORTAL_V_TILES;

            rlColor4ub(cc0.r, cc0.g, cc0.b, cc0.a);
            rlTexCoord2f(u0, v0);
            rlVertex3f(center.x + (axA.x * c0 + axB.x * s0) * rr0,
                       center.y + (axA.y * c0 + axB.y * s0) * rr0,
                       center.z + (axA.z * c0 + axB.z * s0) * rr0);
            rlColor4ub(cc0.r, cc0.g, cc0.b, cc0.a);
            rlTexCoord2f(u1, v0);
            rlVertex3f(center.x + (axA.x * c1 + axB.x * s1) * rr0,
                       center.y + (axA.y * c1 + axB.y * s1) * rr0,
                       center.z + (axA.z * c1 + axB.z * s1) * rr0);
            rlColor4ub(cc1.r, cc1.g, cc1.b, cc1.a);
            rlTexCoord2f(u1, v1);
            rlVertex3f(center.x + (axA.x * c1 + axB.x * s1) * rr1,
                       center.y + (axA.y * c1 + axB.y * s1) * rr1,
                       center.z + (axA.z * c1 + axB.z * s1) * rr1);
            rlColor4ub(cc1.r, cc1.g, cc1.b, cc1.a);
            rlTexCoord2f(u0, v1);
            rlVertex3f(center.x + (axA.x * c0 + axB.x * s0) * rr1,
                       center.y + (axA.y * c0 + axB.y * s0) * rr1,
                       center.z + (axA.z * c0 + axB.z * s0) * rr1);
        }
    }
    rlEnd();

    // ── The rim: UNTEXTURED, and that is the rule rather than an economy ────
    // Structure belongs to exactly ONE layer. The rim is 5x narrower than the
    // interior it borders, so the same sheet drawn across it would be
    // unresolvably high-frequency, and unresolvable detail comes back as dashes
    // (core/docs/LANDMINES.md, 29/07). The rim is a lit SHAPE.
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    {
        const int RW = 5; // samples across the rim band
        static Vector3 prevRing[5 * 2];
        static Vector3 curRing[5 * 2];
        static Color rimCol[5];
        for (int i = 0; i < RW; i++)
        {
            float w = (float)i / (float)(RW - 1);
            float p = PortalDisc_RimProfile(w);
            // Whitened at the SOURCE: a saturated hue stacks additively into more
            // of itself and never reaches white, so emissiveBoost has nothing to
            // lift (VC_Whiten). The rim is the hottest thing in the effect.
            Color c = VC_Whiten(m->glow, PORTAL_RIM_WHITEN * p);
            rimCol[i] = VC_WithAlpha(c, (unsigned char)(alpha * PORTAL_RIM_ALPHA *
                                                        Math_Mix(0.25f, 1.0f, p) * 255.0f));
        }
        for (int s = 0; s <= slices; s++)
        {
            float ang = (float)s / (float)slices * 2.0f * PI;
            float ca = cosf(ang), sa = sinf(ang);
            for (int i = 0; i < RW; i++)
            {
                float w = (float)i / (float)(RW - 1);
                float rr = rimIn + (rimOut - rimIn) * w;
                float off = rimHalf * PortalDisc_RimProfile(w);
                Vector3 base = {center.x + (axA.x * ca + axB.x * sa) * rr,
                                center.y + (axA.y * ca + axB.y * sa) * rr,
                                center.z + (axA.z * ca + axB.z * sa) * rr};
                curRing[i] = Vector3Add(base, Vector3Scale(n, off));
                curRing[RW + i] = Vector3Subtract(base, Vector3Scale(n, off));
            }
            if (s > 0)
            {
                // Both faces of the lens, so the rim survives being seen exactly
                // edge-on — which for a portal is the common approach angle.
                for (int side = 0; side < 2; side++)
                {
                    int o = side * RW;
                    for (int i = 0; i < RW - 1; i++)
                    {
                        Color k0 = rimCol[i], k1 = rimCol[i + 1];
                        rlColor4ub(k0.r, k0.g, k0.b, k0.a);
                        rlVertex3f(prevRing[o + i].x, prevRing[o + i].y, prevRing[o + i].z);
                        rlColor4ub(k1.r, k1.g, k1.b, k1.a);
                        rlVertex3f(prevRing[o + i + 1].x, prevRing[o + i + 1].y, prevRing[o + i + 1].z);
                        rlColor4ub(k1.r, k1.g, k1.b, k1.a);
                        rlVertex3f(curRing[o + i + 1].x, curRing[o + i + 1].y, curRing[o + i + 1].z);
                        rlColor4ub(k0.r, k0.g, k0.b, k0.a);
                        rlVertex3f(curRing[o + i].x, curRing[o + i].y, curRing[o + i].z);
                    }
                }
            }
            for (int i = 0; i < RW * 2; i++) prevRing[i] = curRing[i];
        }
    }
    rlEnd();

    rlSetTexture(0); // must not leak the binding into whatever draws next
    rlColor4ub(255, 255, 255, 255);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    VFXRender_EndDraw(&renderScope);
}

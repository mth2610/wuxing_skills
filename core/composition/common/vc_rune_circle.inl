// ── E5.2 — VFX_ComposeRuneCircle ─────────────────────────────────────────────
//
// AAA ORGANIC RUNE CIRCLE:
// 1. BODY (alpha): a material-colour underlay beneath a saturated inscription
//    keeps the element hue legible on pale ground without a black fringe.
// 2. EMISSION (additive): a soft outer halo and compact luminous core give the
//    circle a readable bloom footprint without whitening its elemental hue.
// 3. Particle-free focal geometry: a pair of nested squares and a centre ring
//    restore the ritual hierarchy without motes, spokes, or radial clutter.
// 4. Living material: small, phase-shifted radial breathing and travelling arc
//    emphasis keep the perfect construction from reading as a static UI decal.

#include "core/vfx_contrast.h"
#include "core/geometry/procedural_mesh_utils.h"

#define RUNE_MAX_RINGS   4
#define RUNE_RING_POINTS 97      // 96 segments + the duplicated closing point

static SkillCurve s_runeOpen = {0};   // radius vs t01: snaps open, eases shut
static SkillCurve s_runeFade = {0};   // alpha vs t01
static bool       s_runeInit = false;

static float s_runeWhite = 0.25f;     // how white the band is (element = tint)
static float s_runeWidth = 1.0f;      // x on band thickness
static float s_runeSpin  = 1.0f;      // x on rotation speed
static float s_runeDash  = 1.0f;      // x on dash density

static Texture2D s_runeTex = {0};
#define RUNE_GLYPH_SHEETS 4
static Texture2D s_runeGlyph[RUNE_GLYPH_SHEETS] = {0};

static void Rune_InitShared(void)
{
    if (s_runeInit) return;

    FloatCurve_AddStop(&s_runeOpen, 0.00f, 0.05f);
    FloatCurve_AddStop(&s_runeOpen, 0.12f, 1.06f);
    FloatCurve_AddStop(&s_runeOpen, 0.22f, 1.00f);
    FloatCurve_AddStop(&s_runeOpen, 0.82f, 0.98f);
    FloatCurve_AddStop(&s_runeOpen, 1.00f, 0.55f);

    FloatCurve_AddStop(&s_runeFade, 0.00f, 0.0f);
    FloatCurve_AddStop(&s_runeFade, 0.08f, 1.0f);
    FloatCurve_AddStop(&s_runeFade, 0.80f, 1.0f);
    FloatCurve_AddStop(&s_runeFade, 1.00f, 0.0f);

    Tuning_RegisterFloat("rune_white", &s_runeWhite, 0.25f);
    Tuning_RegisterFloat("rune_width", &s_runeWidth, 1.0f);
    Tuning_RegisterFloat("rune_spin",  &s_runeSpin,  1.0f);
    Tuning_RegisterFloat("rune_dash",  &s_runeDash,  1.0f);

    s_runeTex = ResourceManager_LoadTexture("assets/textures/rune_line.png");
    if (s_runeTex.id == 0)
    {
        TraceLog(LOG_WARNING, "RuneCircle: rune_line.png missing, using flat white band");
        Image wimg = GenImageColor(4, 4, WHITE);
        s_runeTex = LoadTextureFromImage(wimg);
        UnloadImage(wimg);
    }
    else SetTextureFilter(s_runeTex, TEXTURE_FILTER_BILINEAR);

    for (int g = 0; g < RUNE_GLYPH_SHEETS; g++)
    {
        char path[96];
        snprintf(path, sizeof(path), "assets/textures/rune_glyphs_%d.png", g);
        s_runeGlyph[g] = ResourceManager_LoadTexture(path);
        if (s_runeGlyph[g].id != 0)
        {
            SetTextureFilter(s_runeGlyph[g], TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_runeGlyph[g], TEXTURE_WRAP_REPEAT);
        }
    }

    s_runeInit = true;
}

static void Rune_PlaneBasis(Vector3 normal, Vector3 *outU, Vector3 *outV)
{
    Vector3 n = Vector3Normalize(normal);
    Vector3 ref = (fabsf(n.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                       : (Vector3){1.0f, 0.0f, 0.0f};
    *outU = Vector3Normalize(Vector3CrossProduct(n, ref));
    *outV = Vector3CrossProduct(n, *outU);
}

// Dynamic sweeping energy modulation along the arc.
static float Rune_ArcCharacter(int ring, float a01, float time)
{
    float dir = (ring % 2 == 0) ? 1.0f : -1.0f;
    float longSweep = sinf(a01 * 2.0f * PI * (float)(2 + ring)
                         + time * (1.15f + 0.18f * (float)ring) * dir);
    float fineSweep = sinf(a01 * 2.0f * PI * (float)(7 + ring * 2)
                         - time * (0.55f + 0.09f * (float)ring) * dir
                         + (float)ring * 1.71f);
    return 0.75f + 0.25f * longSweep;
}

static float Rune_DashMask(int ring, float a01)
{
    float teeth = (float)(7 + ring * 5) * s_runeDash;
    float phase = (float)ring * 0.37f;
    float x = (a01 * teeth + phase);
    float f = x - floorf(x);
    float duty = 0.62f - 0.10f * (float)(ring % 3);
    if (f > duty) return 0.0f;
    float edge = 0.12f;
    float head = (f < edge) ? (f / edge) : 1.0f;
    float tail = (f > duty - edge) ? ((duty - f) / edge) : 1.0f;
    return fminf(head, tail);
}

static float Rune_OrganicRadiusOffset(int ring, float angle, float time, float radius)
{
    float phase = (float)ring * 1.37f;
    float slow = sinf(angle * (3.0f + (float)ring) + time * 0.63f + phase);
    float fine = sinf(angle * (8.0f + 2.0f * (float)ring) - time * 0.31f + phase * 2.1f);
    float breath = 0.72f + 0.28f * sinf(time * 0.91f + phase);
    return radius * breath * (slow * 0.0034f + fine * 0.0015f);
}

static void Rune_DrawPolygon(Vector3 center, Vector3 u, Vector3 v, Vector3 normal,
                             float radius, float angleOffset, int sides,
                             float halfWidth, Color tint, Texture2D tex, Camera3D cam)
{
    if (sides < 3 || sides > 32 || radius <= 0.001f || halfWidth <= 0.0001f) return;

    RibbonPoint polyPts[33];
    for (int i = 0; i <= sides; i++)
    {
        float angle = angleOffset + (float)i * (2.0f * PI / (float)sides);
        polyPts[i].position = Vector3Add(center,
            Vector3Add(Vector3Scale(u, cosf(angle) * radius),
                       Vector3Scale(v, sinf(angle) * radius)));
        polyPts[i].halfWidth = halfWidth;
        polyPts[i].v = (float)i / (float)sides;
        polyPts[i].tint = tint;
    }
    DrawRibbonStripEx(polyPts, sides + 1, tex, cam, RIBBON_FIXED_NORMAL, normal);
}

static void Rune_DrawFocusGlyph(Vector3 center, Vector3 normal, Vector3 u, Vector3 v,
                                float radius, float time, float halfWidth, Color tint)
{
    float pulse = 1.0f + 0.022f * sinf(time * 1.35f);
    float squareRadius = radius * 0.44f * pulse;
    float squareSpin = time * 0.18f * s_runeSpin;
    Rune_DrawPolygon(center, u, v, normal, squareRadius, squareSpin,
                     4, halfWidth, tint, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, squareRadius, squareSpin + PI * 0.25f,
                     4, halfWidth, tint, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, radius * 0.21f * pulse, -squareSpin,
                     32, halfWidth * 0.78f, tint, s_runeTex, camera);
}

// Continuous: call once per frame with the caster's progress.
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat,
                           float radius, float t01, int ringCount)
{
    Rune_InitShared();
    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;
    if (ringCount < 1) ringCount = 1;
    if (ringCount > RUNE_MAX_RINGS) ringCount = RUNE_MAX_RINGS;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float open  = SkillCurve_Eval(&s_runeOpen, t01);
    float fade  = SkillCurve_Eval(&s_runeFade, t01);
    if (fade <= 0.001f) return;

    Vector3 u, v;
    Rune_PlaneBasis(normal, &u, &v);
    float time = TimeFX_Elapsed();

    Color inscriptionCol = m->glow;
    Color bodyInk = VFXContrast_ApplyColor(m->body, VFX_CONTRAST_MAGIC, VFX_CONTRAST_BODY);
    Color saturatedInscription = VFXContrast_ApplyColor(inscriptionCol, VFX_CONTRAST_MAGIC, VFX_CONTRAST_BODY);

    static RibbonPoint pts[RUNE_RING_POINTS];

    // ── PASS 1: BODY (Alpha Core & Contrast Keyline) ─────────────────────────
    VFXRenderScope bodyScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);

    static const float bodyW[2][2] = {
        { 1.06f, 1.02f },  // sub-layer 0: material-colour underlay width
        { 0.94f, 0.88f }   // sub-layer 1: saturated inscription width
    };
    static const float bodyA[2] = { 0.72f, 0.96f };

    for (int r = 0; r < ringCount; r++)
    {
        float dir   = (r % 2 == 0) ? 1.0f : -1.0f;
        float speed = (0.55f + 0.22f * (float)r) * dir * s_runeSpin;
        float spin  = time * speed;

        bool isGlyph = (r % 2 == 0) && (s_runeGlyph[r % RUNE_GLYPH_SHEETS].id != 0);
        float rr = radius * open * (1.0f - 0.19f * (float)r);
        float halfW = radius * (isGlyph ? 0.052f : (0.016f - 0.002f * (float)r))
                             * s_runeWidth;
        halfW *= VC_Breathe(time + (float)r, 2.1f + 0.4f * (float)r, 0.12f);

        for (int sub = 0; sub < 2; sub++)
        {
            Texture2D tex = isGlyph ? s_runeGlyph[r % RUNE_GLYPH_SHEETS] : s_runeTex;
            Color passCol = (sub == 0) ? bodyInk : saturatedInscription;

            for (int i = 0; i < RUNE_RING_POINTS; i++)
            {
                float a01 = (float)i / (float)(RUNE_RING_POINTS - 1);
                float ang = a01 * 2.0f * PI + spin;
                float rPerturbed = rr + Rune_OrganicRadiusOffset(r, ang, time, radius);

                Vector3 p = Vector3Add(center,
                              Vector3Add(Vector3Scale(u, cosf(ang) * rPerturbed),
                                         Vector3Scale(v, sinf(ang) * rPerturbed)));
                pts[i].position  = p;
                pts[i].halfWidth = halfW * (isGlyph ? bodyW[sub][1] : bodyW[sub][0]);
                pts[i].v         = a01;

                bool solid = isGlyph || (r == ringCount - 1);
                float mask = solid ? 1.0f : Rune_DashMask(r, a01);
                float arcMod = Rune_ArcCharacter(r, a01, time);
                float a = fade * mask * arcMod * bodyA[sub] * (0.95f - 0.10f * (float)r);
                pts[i].tint = ColorAlpha(passCol, a);
            }

            DrawRibbonStripEx(pts, RUNE_RING_POINTS, tex, camera,
                              RIBBON_FIXED_NORMAL, normal);
        }
    }

    float focusHalfW = radius * 0.0085f * s_runeWidth;
    Rune_DrawFocusGlyph(center, normal, u, v, radius * open, time, focusHalfW * 1.42f,
                        ColorAlpha(bodyInk, fade * 0.95f));
    Rune_DrawFocusGlyph(center, normal, u, v, radius * open, time, focusHalfW * 0.82f,
                        ColorAlpha(saturatedInscription, fade * 0.96f));
    VFXRender_EndDraw(&bodyScope);

    // ── PASS 2: EMISSION (soft halo + compact luminous core) ─────────────────
    VFXRenderScope emissionScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);

    static const float haloW[2] = { 1.55f, 0.44f };
    static const float haloA[2] = { 0.10f, 0.48f };
    Color emissionCol = ColorLerp(m->glow, m->soft, 0.30f);

    for (int r = 0; r < ringCount; r++)
    {
        float dir   = (r % 2 == 0) ? 1.0f : -1.0f;
        float speed = (0.55f + 0.22f * (float)r) * dir * s_runeSpin;
        float spin  = time * speed;

        bool isGlyph = (r % 2 == 0) && (s_runeGlyph[r % RUNE_GLYPH_SHEETS].id != 0);
        float rr = radius * open * (1.0f - 0.19f * (float)r);
        float halfW = radius * (isGlyph ? 0.052f : (0.016f - 0.002f * (float)r))
                             * s_runeWidth;
        halfW *= VC_Breathe(time + (float)r, 2.1f + 0.4f * (float)r, 0.12f);

        for (int pass = 0; pass < 2; pass++)
        {
            Texture2D tex = isGlyph ? s_runeGlyph[r % RUNE_GLYPH_SHEETS] : s_runeTex;
            Color passTint = emissionCol;

            for (int i = 0; i < RUNE_RING_POINTS; i++)
            {
                float a01 = (float)i / (float)(RUNE_RING_POINTS - 1);
                float ang = a01 * 2.0f * PI + spin;
                float rPerturbed = rr + Rune_OrganicRadiusOffset(r, ang, time, radius);

                Vector3 p = Vector3Add(center,
                              Vector3Add(Vector3Scale(u, cosf(ang) * rPerturbed),
                                         Vector3Scale(v, sinf(ang) * rPerturbed)));
                pts[i].position  = p;
                pts[i].halfWidth = halfW * haloW[pass];
                pts[i].v         = a01;

                bool solid = isGlyph || (r == ringCount - 1);
                float mask = solid ? 1.0f : Rune_DashMask(r, a01);
                float arcMod = Rune_ArcCharacter(r, a01, time);
                float a = fade * mask * arcMod * haloA[pass] * (0.90f - 0.08f * (float)r);
                pts[i].tint = ColorAlpha(passTint, a);
            }

            DrawRibbonStripEx(pts, RUNE_RING_POINTS, tex, camera,
                              RIBBON_FIXED_NORMAL, normal);
        }
    }

    Rune_DrawFocusGlyph(center, normal, u, v, radius * open, time, focusHalfW * haloW[0],
                        ColorAlpha(emissionCol, fade * haloA[0]));
    Rune_DrawFocusGlyph(center, normal, u, v, radius * open, time, focusHalfW * haloW[1],
                        ColorAlpha(emissionCol, fade * haloA[1]));
    VFXRender_EndDraw(&emissionScope);
}

// ── E5.2 — VFX_ComposeRuneCircle ─────────────────────────────────────────────
//
// AAA SACRED GEOMETRY RUNE CIRCLE (Organic Incandescent Mana Architecture):
// 1. Dual-Pass Multi-Layer Isolation (Zero washout on bright ground):
//    - Pass 1 (BODY, BLEND_ALPHA): Dark ink occlusion keyline + Saturated Hue Core.
//    - Pass 2 (EMISSION, BLEND_ADDITIVE): 3-Pass Additive Radiance (Wide Halo + Luminous Body + White-Hot Core)
//      exceeding 1.5 in HDR buffer, triggering radiant Bloom.
// 2. 3D Ethereal Rising Motes (Rune_DrawMotes):
//    - 20 sparkling motes of mana ascending and swirling above the circle plane.
// 3. Organic Mana Flow & Wave Turbulence:
//    - Harmonic sinusoidal micro-waves giving the rings a living, breathing liquid energy feel.
// 4. Sacred Geometry Core:
//    - Interlocking 8-pointed star (Octagram) + Center focus ring + Radial spokes.
// 5. Precision Cardinal Anchors (Rune_DrawAnchors):
//    - Delicate diamond sigils & radial tick pointers at 4 cardinal nodes.
// 6. Dynamic Energy Sweep (Rune_ArcCharacter):
//    - Travelling energy modulation along arcs.

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
    if (s_runeGlyph[0].id == 0)
        TraceLog(LOG_WARNING, "RuneCircle: rune_glyphs_*.png missing — "
                              "falling back to dashed rings (run scripts/gen_rune_textures.py)");

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
    float sweep = sinf(a01 * 6.2831853f * (float)(2 + ring) + time * (1.8f + 0.4f * (float)ring) * dir);
    return 0.75f + 0.25f * sweep;
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

// Draws a sacred closed polygon (e.g. triangle, square, hexagon) as a smooth ribbon.
static void Rune_DrawPolygon(Vector3 center, Vector3 u, Vector3 v, Vector3 normal,
                             float radius, float angleOffset, int sides,
                             float halfWidth, Color tint, Texture2D tex, Camera3D cam)
{
    if (sides < 3 || sides > 16 || radius <= 0.001f || halfWidth <= 0.0001f) return;
    RibbonPoint polyPts[17];
    for (int i = 0; i <= sides; i++)
    {
        float ang = angleOffset + (float)i * (2.0f * PI / (float)sides);
        Vector3 p = Vector3Add(center,
                      Vector3Add(Vector3Scale(u, cosf(ang) * radius),
                                 Vector3Scale(v, sinf(ang) * radius)));
        polyPts[i].position  = p;
        polyPts[i].halfWidth = halfWidth;
        polyPts[i].v         = (float)i / (float)sides;
        polyPts[i].tint      = tint;
    }
    DrawRibbonStripEx(polyPts, sides + 1, tex, cam, RIBBON_FIXED_NORMAL, normal);
}

// Structural cardinal diamond anchors with delicate tick pointers.
static void Rune_DrawAnchors(Vector3 center, Vector3 normal, Vector3 u, Vector3 v,
                             float radius, float fade, float time,
                             Color color, bool isEmission)
{
    const int count = 4;
    float diaRadius = radius * (isEmission ? 0.038f : 0.028f) * s_runeWidth;
    float halfW = radius * (isEmission ? 0.007f : 0.0045f) * s_runeWidth;
    if (diaRadius <= 0.001f || fade <= 0.001f) return;

    for (int i = 0; i < count; i++)
    {
        float ang = (float)i * (2.0f * PI / (float)count) + time * 0.20f * s_runeSpin;
        Vector3 pos = Vector3Add(center,
                        Vector3Add(Vector3Scale(u, cosf(ang) * radius),
                                   Vector3Scale(v, sinf(ang) * radius)));

        float pulse = 1.0f + 0.18f * sinf(time * 3.5f + (float)i * 1.57f);
        float r = diaRadius * pulse;
        Color c = ColorAlpha(color, ((float)color.a * fade * (isEmission ? 0.85f : 0.95f)) / 255.0f);

        // Precision diamond sigil
        Rune_DrawPolygon(pos, u, v, normal, r, ang + PI * 0.25f, 4, halfW, c, s_runeTex, camera);

        // White-hot center filament for diamond in emission
        if (isEmission)
        {
            Color coreC = ColorAlpha(ColorLerp(color, WHITE, 0.85f), fade * 0.95f);
            Rune_DrawPolygon(pos, u, v, normal, r, ang + PI * 0.25f, 4, halfW * 0.4f, coreC, s_runeTex, camera);
        }

        // Delicate radial tick pointers
        RibbonPoint tickPts[2];
        Vector3 dir = Vector3Normalize(Vector3Subtract(pos, center));
        tickPts[0].position = Vector3Add(pos, Vector3Scale(dir, r * 1.15f));
        tickPts[0].halfWidth = halfW * 0.85f;
        tickPts[0].v = 0.0f;
        tickPts[0].tint = c;

        tickPts[1].position = Vector3Add(pos, Vector3Scale(dir, r * 2.3f));
        tickPts[1].halfWidth = halfW * 0.2f;
        tickPts[1].v = 1.0f;
        tickPts[1].tint = ColorAlpha(c, 0.0f);

        DrawRibbonStripEx(tickPts, 2, s_runeTex, camera, RIBBON_FIXED_NORMAL, normal);
    }
}

// Radial sacred spokes connecting inner star to outer ring.
static void Rune_DrawSpokes(Vector3 center, Vector3 normal, Vector3 u, Vector3 v,
                            float rInner, float rOuter, float fade, float time,
                            Color color, bool isEmission)
{
    const int count = 8;
    float halfW = rOuter * (isEmission ? 0.006f : 0.0035f) * s_runeWidth;
    if (halfW <= 0.0003f || fade <= 0.001f || rOuter <= rInner) return;

    RibbonPoint pts[2];
    for (int i = 0; i < count; i++)
    {
        float ang = (float)i * (2.0f * PI / (float)count) - time * 0.30f * s_runeSpin;
        float pulse = 0.70f + 0.30f * sinf(time * 2.5f + (float)i * 0.785f);
        Color c = ColorAlpha(color, ((float)color.a * fade * pulse * (isEmission ? 0.75f : 0.85f)) / 255.0f);

        Vector3 dir = Vector3Add(Vector3Scale(u, cosf(ang)), Vector3Scale(v, sinf(ang)));
        pts[0].position = Vector3Add(center, Vector3Scale(dir, rInner));
        pts[0].halfWidth = halfW;
        pts[0].v = 0.0f;
        pts[0].tint = c;

        pts[1].position = Vector3Add(center, Vector3Scale(dir, rOuter));
        pts[1].halfWidth = halfW;
        pts[1].v = 1.0f;
        pts[1].tint = c;

        DrawRibbonStripEx(pts, 2, s_runeTex, camera, RIBBON_FIXED_NORMAL, normal);
    }
}

// 3D Ethereal Rising Motes swirling above the magic circle.
static void Rune_DrawMotes(Vector3 center, Vector3 u, Vector3 v, Vector3 normal,
                           float radius, float fade, float time,
                           Color color, bool isEmission)
{
    const int count = 20;
    float baseSize = radius * (isEmission ? 0.024f : 0.016f) * s_runeWidth;
    if (baseSize <= 0.0005f || fade <= 0.001f) return;

    for (int i = 0; i < count; i++)
    {
        float seed = (float)i * 13.37f;
        float rFrac = 0.22f + 0.72f * sqrtf((float)i / (float)count);
        float r = radius * rFrac;

        float ang = seed + time * (0.75f + 0.35f * (1.0f - rFrac)) * s_runeSpin;
        float risePeriod = 1.8f + 0.35f * (float)(i % 5);
        float life = fmodf(time + seed, risePeriod) / risePeriod;
        float yLift = (0.04f + life * 0.65f) * radius;
        float lifeFade = sinf(life * PI);

        Vector3 pGround = Vector3Add(center,
                            Vector3Add(Vector3Scale(u, cosf(ang) * r),
                                       Vector3Scale(v, sinf(ang) * r)));
        Vector3 pos = Vector3Add(pGround, Vector3Scale(normal, yLift));
        pos = VC_MotionJitter(pos, radius * 0.012f, 4.0f, time, seed);

        float twinkle = 0.6f + 0.4f * sinf(time * 6.5f + seed);
        float sz = baseSize * (0.65f + 0.35f * (float)(i % 3)) * twinkle * lifeFade;

        Color moteCol = isEmission
            ? ColorLerp(color, WHITE, 0.45f + 0.45f * twinkle)
            : color;
        Color c = ColorAlpha(moteCol, ((float)color.a * fade * lifeFade * (isEmission ? 0.92f : 0.60f)) / 255.0f);

        DrawCoreBillboardQuad(pos, sz, camera, c);
    }
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
    Color keylineCol = ColorLerp((Color){10, 10, 15, 255}, m->body, 0.20f);

    Color bodyInk = VFXContrast_ApplyColor(keylineCol, VFX_CONTRAST_MAGIC, VFX_CONTRAST_BODY);
    Color saturatedInscription = VFXContrast_ApplyColor(inscriptionCol, VFX_CONTRAST_MAGIC, VFX_CONTRAST_BODY);

    static RibbonPoint pts[RUNE_RING_POINTS];

    // ── PASS 1: BODY (Alpha Core & Contrast Keyline) ─────────────────────────
    VFXRenderScope bodyScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);

    static const float bodyW[2][2] = {
        { 1.25f, 1.06f },  // sub-layer 0: dark keyline outline width
        { 1.00f, 0.88f }   // sub-layer 1: saturated inscription width
    };
    static const float bodyA[2] = { 0.90f, 0.96f };

    for (int r = 0; r < ringCount; r++)
    {
        bool isGlyph = (r % 2 == 0) && (s_runeGlyph[r % RUNE_GLYPH_SHEETS].id != 0);
        float dir   = (r % 2 == 0) ? 1.0f : -1.0f;
        float speed = (0.55f + 0.22f * (float)r) * dir * s_runeSpin;
        float spin  = time * speed;

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
                // Organic micro-wave perturbation
                float wave = sinf(ang * 5.0f + time * 3.0f + (float)r * 1.2f) * (0.004f * radius);
                float rPerturbed = rr + wave;

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

    // Sacred Octagram (Inner Star Core) & Radial Spokes in Body Pass
    float rStar = radius * open * 0.45f * (1.0f + 0.025f * sinf(time * 2.8f));
    float starSpin = time * 0.35f * s_runeSpin;
    float starHalfW = radius * 0.008f * s_runeWidth;
    Color starCol = ColorAlpha(saturatedInscription, fade * 0.92f);
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin, 4, starHalfW, starCol, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin + PI * 0.25f, 4, starHalfW, starCol, s_runeTex, camera);

    float rCenter = radius * open * 0.22f;
    Rune_DrawPolygon(center, u, v, normal, rCenter, -starSpin, 16, starHalfW * 0.85f, starCol, s_runeTex, camera);

    Rune_DrawSpokes(center, normal, u, v, rStar, radius * open * 0.81f, fade, time, saturatedInscription, false);
    Rune_DrawAnchors(center, normal, u, v, radius * open, fade, time, bodyInk, false);
    Rune_DrawMotes(center, u, v, normal, radius * open, fade, time, bodyInk, false);
    VFXRender_EndDraw(&bodyScope);

    // ── PASS 2: EMISSION (3-Pass Additive Radiance & White-Hot Bloom Core) ───
    VFXRenderScope emissionScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);

    // 3 passes: Wide soft halo -> Vivid elemental body -> White-hot incandescent laser core
    static const float haloW[3]     = { 2.4f,  1.0f,  0.35f };
    static const float haloA[3]     = { 0.32f, 0.92f, 1.00f };
    static const float haloWhite[3] = { 0.0f,  0.30f, 0.88f };

    for (int r = 0; r < ringCount; r++)
    {
        bool isGlyph = (r % 2 == 0) && (s_runeGlyph[r % RUNE_GLYPH_SHEETS].id != 0);
        float dir   = (r % 2 == 0) ? 1.0f : -1.0f;
        float speed = (0.55f + 0.22f * (float)r) * dir * s_runeSpin;
        float spin  = time * speed;

        float rr = radius * open * (1.0f - 0.19f * (float)r);
        float halfW = radius * (isGlyph ? 0.052f : (0.016f - 0.002f * (float)r))
                             * s_runeWidth;
        halfW *= VC_Breathe(time + (float)r, 2.1f + 0.4f * (float)r, 0.12f);

        int passes = isGlyph ? 2 : 3;
        for (int pass = 0; pass < passes; pass++)
        {
            bool glyphPass = isGlyph;
            Texture2D tex = glyphPass ? s_runeGlyph[r % RUNE_GLYPH_SHEETS] : s_runeTex;
            Color passTint = ColorLerp(inscriptionCol, WHITE, haloWhite[pass]);

            for (int i = 0; i < RUNE_RING_POINTS; i++)
            {
                float a01 = (float)i / (float)(RUNE_RING_POINTS - 1);
                float ang = a01 * 2.0f * PI + spin;
                float wave = sinf(ang * 5.0f + time * 3.0f + (float)r * 1.2f) * (0.004f * radius);
                float rPerturbed = rr + wave;

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

    // Sacred Octagram in Emission: Layer 1 (Glowing Radiant Halo) + Layer 2 (White-Hot Bloom Core)
    Color starEmitCol = ColorAlpha(inscriptionCol, fade * 0.75f);
    Color starCoreCol = ColorAlpha(ColorLerp(inscriptionCol, WHITE, 0.88f), fade * 0.95f);
    float starHaloW = starHalfW * 2.2f;

    // Outer Radiant Halo
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin, 4, starHaloW, starEmitCol, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin + PI * 0.25f, 4, starHaloW, starEmitCol, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, rCenter, -starSpin, 16, starHaloW * 0.90f, starEmitCol, s_runeTex, camera);

    // White-Hot Incandescent Core (>1.5 into Bloom)
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin, 4, starHalfW * 0.45f, starCoreCol, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, rStar, starSpin + PI * 0.25f, 4, starHalfW * 0.45f, starCoreCol, s_runeTex, camera);
    Rune_DrawPolygon(center, u, v, normal, rCenter, -starSpin, 16, starHalfW * 0.40f, starCoreCol, s_runeTex, camera);

    Rune_DrawSpokes(center, normal, u, v, rStar, radius * open * 0.81f, fade, time, inscriptionCol, true);
    Rune_DrawAnchors(center, normal, u, v, radius * open, fade, time, inscriptionCol, true);
    Rune_DrawMotes(center, u, v, normal, radius * open, fade, time, inscriptionCol, true);
    VFXRender_EndDraw(&emissionScope);
}




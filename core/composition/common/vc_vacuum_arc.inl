// ── vc_vacuum_arc.inl ────────────────────────────────────────────────────────
//
// VFX 3: 3D Curved Parabolic Vacuum Wind Arcs.
// High-altitude sweeping aerodynamic vacuum streamlines cutting through 3D space
// with needle-sharp sinusoidal tapering, razor-sharp leading rim, and translucent body.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/ribbon_strip.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Texture2D s_vacRibbonTex = {0};

static Texture2D VacuumArc_GetTexture(void)
{
    if (s_vacRibbonTex.id == 0)
    {
        const int W = 128;
        const int H = 64;
        Image img = GenImageColor(W, H, BLANK);
        for (int y = 0; y < H; y++)
        {
            float v = (float)y / (float)(H - 1);
            float striation = 0.94f + 0.06f * sinf(v * 28.0f * PI);

            for (int x = 0; x < W; x++)
            {
                float u = (float)x / (float)(W - 1); // 0 = outer razor edge, 1 = inner fade

                // 1. Hot razor leading rim
                float edgeDist = u / 0.065f;
                float razorRim = expf(-edgeDist * edgeDist) * 0.88f;

                // 2. Ethereal translucent vacuum smear
                float smear = powf(fmaxf(0.0f, 1.0f - u), 2.2f) * 0.45f;

                // 3. Sub-pixel antialiasing shoulder
                float aaShoulder = (u < 0.015f) ? (u / 0.015f) : 1.0f;

                float alpha = (razorRim + smear) * aaShoulder * striation;
                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) });
            }
        }
        s_vacRibbonTex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(s_vacRibbonTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_vacRibbonTex, TEXTURE_WRAP_CLAMP);
    }
    return s_vacRibbonTex;
}

static Vector3 VacuumArc_EvalBezierCubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    Vector3 p = Vector3Scale(p0, uuu);
    p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
    p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
    p = Vector3Add(p, Vector3Scale(p3, ttt));
    return p;
}

void VFX_DrawCurvedVacuumArc3D(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                              Color rimColor, Color bodyColor, float maxWidth,
                              float progress, float duration, Camera3D camera)
{
    (void)duration;
    if (progress <= 0.0f || progress >= 1.0f) return;

    // Progression: head cuts forward, tail follows with slight lag
    float headU, tailU, alphaFade;
    if (progress < 0.55f)
    {
        float cutPhase = progress / 0.55f;
        headU = 1.0f - (1.0f - cutPhase) * (1.0f - cutPhase); // ease-out
        tailU = fmaxf(0.0f, headU - 0.70f);
        alphaFade = 1.0f;
    }
    else
    {
        float dissolvePhase = (progress - 0.55f) / 0.45f;
        headU = 1.0f;
        tailU = 0.30f + dissolvePhase * 0.70f;
        alphaFade = 1.0f - Clamp(dissolvePhase, 0.0f, 1.0f);
    }

    if (headU <= tailU || alphaFade <= 0.002f) return;

    const int SEGMENTS = 48;
    static RibbonPoint corePts[48];
    static RibbonPoint haloPts[48];
    Texture2D vacuumTex = VacuumArc_GetTexture();

    for (int i = 0; i < SEGMENTS; i++)
    {
        float frac = (float)i / (float)(SEGMENTS - 1);
        float splineT = tailU + (headU - tailU) * frac;
        Vector3 pos = VacuumArc_EvalBezierCubic(p0, p1, p2, p3, splineT);

        // Sinusoidal needle-sharp tapering at tips (no blunt edges)
        float taper = sinf(frac * PI);
        taper = powf(taper, 1.15f);

        float width = maxWidth * taper;
        float headBoost = 0.75f + 0.25f * frac;
        float finalAlpha = Clamp(alphaFade * taper * headBoost, 0.0f, 1.0f);

        // Core razor ribbon
        corePts[i].position = pos;
        corePts[i].halfWidth = width * 0.45f;
        corePts[i].tint = ColorAlpha(rimColor, Clamp(finalAlpha * 0.90f, 0.0f, 1.0f));
        corePts[i].v = frac;

        // Outer ethereal halo ribbon
        haloPts[i].position = pos;
        haloPts[i].halfWidth = width * 0.75f;
        haloPts[i].tint = ColorAlpha(bodyColor, Clamp(finalAlpha * 0.40f, 0.0f, 1.0f));
        haloPts[i].v = frac;
    }

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    // 1. Soft atmospheric halo pass (provides ethereal depth without geometric twisting)
    DrawRibbonStrip(haloPts, SEGMENTS, vacuumTex, camera);

    // 2. High-contrast razor edge pass
    DrawRibbonStrip(corePts, SEGMENTS, vacuumTex, camera);

    rlEnableDepthMask();
    EndBlendMode();
}

void VFX_ComposeVacuumArc(Vector3 pos, float yaw, float progress, float duration, Camera3D camera)
{
    Vector3 fwd = (Vector3){ sinf(yaw), 0.0f, cosf(yaw) };
    Vector3 rgt = (Vector3){ cosf(yaw), 0.0f, -sinf(yaw) };

    Color rimColor  = (Color){ 255, 255, 255, 255 };
    Color bodyColor = (Color){ 180, 215, 250, 255 };

    // High-altitude sweeping aerodynamic arch (matching the reference image)
    Vector3 p0 = Vector3Add(pos, Vector3Add(Vector3Scale(rgt, -1.2f), Vector3Add(Vector3Scale(fwd, -0.9f), (Vector3){ 0.0f, 0.10f, 0.0f })));
    Vector3 p1 = Vector3Add(pos, Vector3Add(Vector3Scale(rgt, -2.4f), Vector3Add(Vector3Scale(fwd, -0.2f), (Vector3){ 0.0f, 3.20f, 0.0f })));
    Vector3 p2 = Vector3Add(pos, Vector3Add(Vector3Scale(rgt, -0.6f), Vector3Add(Vector3Scale(fwd,  1.8f), (Vector3){ 0.0f, 3.80f, 0.0f })));
    Vector3 p3 = Vector3Add(pos, Vector3Add(Vector3Scale(rgt,  1.6f), Vector3Add(Vector3Scale(fwd,  2.6f), (Vector3){ 0.0f, 0.60f, 0.0f })));

    VFX_DrawCurvedVacuumArc3D(p0, p1, p2, p3, rimColor, bodyColor, 0.42f, progress, duration, camera);
}

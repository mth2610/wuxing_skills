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

// ─────────────────────────────────────────────────────────────────────────────
// Streamline Seed Structure for Upper-Hemisphere Inward Vortex
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    float startTheta;  // Azimuth angle around Y [0..2pi]
    float startPhi;    // Elevation angle above ground [0.08..1.42 rad] (strictly Y >= 0)
    float radius;      // Distance on hemisphere [R_min..R_max]
    float swirlTotal;  // Total rotational swirl angle (radians) along suction path
    float timeOffset;  // Staggered launch timing [0..0.45]
    float streamLen;   // Arc length fraction in [0..1] space [0.30..0.55]
    float widthScale;  // Thickness variation multiplier
} VacuumStreamSeed;

// 10 Distinct Staggered Inflow Streamlines covering the Upper Hemisphere (Y >= 0)
static const VacuumStreamSeed s_streamSeeds[10] = {
    { 0.15f * PI, 0.22f, 2.70f,  2.15f * PI, 0.00f, 0.42f, 1.05f },
    { 0.58f * PI, 0.65f, 2.45f, -2.40f * PI, 0.08f, 0.38f, 0.90f },
    { 0.95f * PI, 0.18f, 2.95f,  2.05f * PI, 0.16f, 0.45f, 1.15f },
    { 1.32f * PI, 0.82f, 2.30f, -2.55f * PI, 0.04f, 0.36f, 0.85f },
    { 1.70f * PI, 0.35f, 2.80f,  2.25f * PI, 0.12f, 0.40f, 1.00f },
    { 0.35f * PI, 1.15f, 2.15f, -2.35f * PI, 0.20f, 0.35f, 0.80f },
    { 0.78f * PI, 0.40f, 2.65f,  2.50f * PI, 0.06f, 0.44f, 1.10f },
    { 1.15f * PI, 1.25f, 2.20f, -2.20f * PI, 0.18f, 0.36f, 0.85f },
    { 1.52f * PI, 0.28f, 2.85f,  2.10f * PI, 0.14f, 0.42f, 1.00f },
    { 1.90f * PI, 0.75f, 2.50f, -2.45f * PI, 0.10f, 0.39f, 0.95f }
};

// Evaluate the 3D suction vortex trajectory at progress parameter u in [0, 1]
// u = 0: origin on upper hemisphere (above ground Y >= 0)
// u = 1: arrives exactly at focalPoint
static inline Vector3 Vacuum_EvalSuctionPath(Vector3 focalPoint, const VacuumStreamSeed *seed, float u)
{
    float uClamped = Clamp(u, 0.0f, 1.0f);

    // Initial origin coordinates on the upper hemisphere (Y >= 0)
    // Horizontal radius from vertical axis
    float rH0 = seed->radius * cosf(seed->startPhi);
    // Initial height offset relative to focal point
    float y0 = seed->radius * sinf(seed->startPhi); // strictly > 0 (upper hemisphere)

    // Inward spiral contraction: non-linear acceleration as air is pulled into singularity
    float rH = rH0 * powf(1.0f - uClamped, 1.35f);

    // Altitude contraction towards focal point Y
    float curY = focalPoint.y + (y0 - focalPoint.y * 0.35f) * (1.0f - powf(uClamped, 0.85f));

    // Vortex swirling angle: angular momentum speeds up as radius shrinks
    float curAngle = seed->startTheta + seed->swirlTotal * powf(uClamped, 1.30f);

    Vector3 pos;
    pos.x = focalPoint.x + rH * cosf(curAngle);
    pos.y = curY;
    pos.z = focalPoint.z + rH * sinf(curAngle);
    return pos;
}

// Draw a single suction streamline converging at focal point
static void Vacuum_DrawSingleStreamline(Vector3 focalPoint, const VacuumStreamSeed *seed,
                                       Color rimColor, Color bodyColor, float baseWidth,
                                       float globalProgress, Camera3D camera)
{
    // Stream progression with time offset
    float localT = (globalProgress - seed->timeOffset) / (1.0f - seed->timeOffset);
    if (localT <= 0.0f || localT >= 1.05f) return;

    // Head cuts inwards, tail follows behind
    float headU = Clamp(localT * 1.35f, 0.0f, 1.0f);
    float tailU = Clamp(headU - seed->streamLen, 0.0f, 1.0f);
    if (headU <= tailU) return;

    // Disappearance phase: when head reaches focalPoint (headU >= 1.0), fade rapidly
    float tipFade = 1.0f;
    if (headU >= 0.98f)
    {
        tipFade = (1.0f - tailU) / seed->streamLen;
        tipFade = Clamp(tipFade, 0.0f, 1.0f);
    }
    if (tipFade <= 0.002f) return;

    const int SEGMENTS = 32;
    static RibbonPoint corePts[32];
    static RibbonPoint haloPts[32];
    Texture2D vacuumTex = VacuumArc_GetTexture();

    float streamSpan = headU - tailU;
    if (streamSpan < 0.001f) return;

    float streamMaxWidth = baseWidth * seed->widthScale;

    for (int i = 0; i < SEGMENTS; i++)
    {
        float frac = (float)i / (float)(SEGMENTS - 1);
        float u = tailU + streamSpan * frac;

        Vector3 pos = Vacuum_EvalSuctionPath(focalPoint, seed, u);

        // Sinusoidal needle-sharp tapering at tips (no blunt ends)
        float taper = sinf(frac * PI);
        taper = powf(taper, 1.15f);

        // Inward shrinkage: streamlines thin out as they concentrate into focal point
        float convergeThin = 1.0f - 0.45f * u;
        float width = streamMaxWidth * taper * convergeThin;

        // Head is brighter, tail is translucent
        float headBoost = 0.70f + 0.30f * frac;
        float finalAlpha = Clamp(tipFade * taper * headBoost, 0.0f, 1.0f);

        corePts[i].position = pos;
        corePts[i].halfWidth = width * 0.45f;
        corePts[i].tint = ColorAlpha(rimColor, Clamp(finalAlpha * 0.95f, 0.0f, 1.0f));
        corePts[i].v = frac;

        haloPts[i].position = pos;
        haloPts[i].halfWidth = width * 0.85f;
        haloPts[i].tint = ColorAlpha(bodyColor, Clamp(finalAlpha * 0.42f, 0.0f, 1.0f));
        haloPts[i].v = frac;
    }

    // 1. Soft atmospheric halo pass (ethereal vacuum body)
    DrawRibbonStrip(haloPts, SEGMENTS, vacuumTex, camera);

    // 2. High-contrast razor edge pass (piercing white core)
    DrawRibbonStrip(corePts, SEGMENTS, vacuumTex, camera);
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic Core API: Compose Vacuum Suction Vortex
// Converges streamlines from upper-hemisphere space into focalPoint
// ─────────────────────────────────────────────────────────────────────────────
void VFX_ComposeVacuumConverge(Vector3 focalPoint, float sphereRadius,
                              float progress, Camera3D camera)
{
    if (progress <= 0.0f || progress >= 1.0f) return;

    Color rimColor  = (Color){ 255, 255, 255, 255 };
    Color bodyColor = (Color){ 180, 220, 255, 255 };

    float scale = (sphereRadius > 0.1f) ? (sphereRadius / 2.6f) : 1.0f;
    float baseWidth = 0.28f * scale;

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    for (int i = 0; i < 10; i++)
    {
        VacuumStreamSeed seed = s_streamSeeds[i];
        seed.radius *= scale;
        Vacuum_DrawSingleStreamline(focalPoint, &seed, rimColor, bodyColor, baseWidth, progress, camera);
    }

    rlEnableDepthMask();
    EndBlendMode();
}

// Backward-compatible wrapper for VFX 3
void VFX_ComposeVacuumArc(Vector3 pos, float yaw, float progress, float duration, Camera3D camera)
{
    (void)yaw;
    (void)duration;
    // Focal point situated at character chest/hilt height (~1.05m above ground)
    Vector3 focalPoint = Vector3Add(pos, (Vector3){ 0.0f, 1.05f, 0.0f });
    VFX_ComposeVacuumConverge(focalPoint, 2.7f, progress, camera);
}


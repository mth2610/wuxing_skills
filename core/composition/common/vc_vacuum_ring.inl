// ── vc_vacuum_ring.inl ───────────────────────────────────────────────────────
//
// VFX 4: Thin Expanding Vacuum Ground Annulus.
// Clean, razor-edged planar vacuum compression ring expanding smoothly on the ground.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif
// Forward declaration from visual_composer.h / vc_ground_wave.inl
float VFX_GroundHeightFromMap(float worldX, float worldZ, void *unused);

static Texture2D s_vacRingTex = {0};

static Texture2D VacuumRing_GetTexture(void)
{
    if (s_vacRingTex.id == 0)
    {
        const int W = 256;
        const int H = 32;
        Image img = GenImageColor(W, H, BLANK);
        for (int y = 0; y < H; y++)
        {
            float v = (float)y / (float)(H - 1);
            float striation = 0.96f + 0.04f * sinf(v * 32.0f * PI);

            for (int x = 0; x < W; x++)
            {
                float u = (float)x / (float)(W - 1); // 0 = inner trailing edge, 1 = outer shock front

                float profile = 0.0f;
                if (u < 0.70f)
                {
                    float t = u / 0.70f;
                    profile = t * t * 0.35f;
                }
                else if (u < 0.96f)
                {
                    float t = (u - 0.70f) / 0.26f;
                    profile = 0.35f + (t * t * (3.0f - 2.0f * t)) * 0.65f;
                }
                else
                {
                    // Razor sharp 4% outer falloff
                    float t = (u - 0.96f) / 0.04f;
                    profile = 1.0f - t;
                }

                float alpha = profile * striation;
                unsigned char a = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f);
                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, a });
            }
        }
        s_vacRingTex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(s_vacRingTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_vacRingTex, TEXTURE_WRAP_REPEAT);
    }
    return s_vacRingTex;
}

static void VacuumRing_DrawGroundDisc(Vector3 center, float radius, Color color, float alpha01)
{
    if (radius <= 0.01f || alpha01 <= 0.002f) return;
    const int SLICES = 48;
    float da = (2.0f * PI) / (float)SLICES;

    float y_center = fmaxf(center.y, VFX_GroundHeightFromMap(center.x, center.z, NULL)) + 0.030f;
    Color cCenter = ColorAlpha(color, Clamp(alpha01 * 0.22f, 0.0f, 1.0f));
    Color cEdge = ColorAlpha(color, 0.0f);

    rlSetTexture(0);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < SLICES; i++)
    {
        float a0 = (float)i * da;
        float a1 = (float)(i + 1) * da;

        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        float x0 = center.x + c0 * radius, z0 = center.z + s0 * radius;
        float x1 = center.x + c1 * radius, z1 = center.z + s1 * radius;
        float y0 = fmaxf(center.y, VFX_GroundHeightFromMap(x0, z0, NULL)) + 0.030f;
        float y1 = fmaxf(center.y, VFX_GroundHeightFromMap(x1, z1, NULL)) + 0.030f;

        // Counter-clockwise looking down from +Y
        rlColor4ub(cCenter.r, cCenter.g, cCenter.b, cCenter.a);
        rlVertex3f(center.x, y_center, center.z);

        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
        rlVertex3f(x1, y1, z1);

        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
        rlVertex3f(x0, y0, z0);
    }
    rlEnd();
}

static void VacuumRing_DrawAnnulusStrip(Vector3 center, float innerR, float outerR,
                                       Color color, float alpha01, float vTiling)
{
    if (outerR <= 0.01f || alpha01 <= 0.002f) return;
    if (innerR < 0.005f) innerR = 0.005f;

    const int SLICES = 64;
    float da = (2.0f * PI) / (float)SLICES;

    Color c = ColorAlpha(color, Clamp(alpha01, 0.0f, 1.0f));
    rlColor4ub(c.r, c.g, c.b, c.a);

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < SLICES; i++)
    {
        float a0 = (float)i * da;
        float a1 = (float)(i + 1) * da;

        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        float x_in0 = center.x + c0 * innerR, z_in0 = center.z + s0 * innerR;
        float x_in1 = center.x + c1 * innerR, z_in1 = center.z + s1 * innerR;
        float x_out0 = center.x + c0 * outerR, z_out0 = center.z + s0 * outerR;
        float x_out1 = center.x + c1 * outerR, z_out1 = center.z + s1 * outerR;

        float y_in0  = fmaxf(center.y, VFX_GroundHeightFromMap(x_in0, z_in0, NULL)) + 0.035f;
        float y_in1  = fmaxf(center.y, VFX_GroundHeightFromMap(x_in1, z_in1, NULL)) + 0.035f;
        float y_out0 = fmaxf(center.y, VFX_GroundHeightFromMap(x_out0, z_out0, NULL)) + 0.035f;
        float y_out1 = fmaxf(center.y, VFX_GroundHeightFromMap(x_out1, z_out1, NULL)) + 0.035f;

        float v0 = ((float)i / (float)SLICES) * vTiling;
        float v1 = ((float)(i + 1) / (float)SLICES) * vTiling;

        // CCW triangles looking down from +Y:
        // Triangle 1: in0 -> in1 -> out1
        rlTexCoord2f(0.0f, v0);
        rlVertex3f(x_in0, y_in0, z_in0);

        rlTexCoord2f(0.0f, v1);
        rlVertex3f(x_in1, y_in1, z_in1);

        rlTexCoord2f(1.0f, v1);
        rlVertex3f(x_out1, y_out1, z_out1);

        // Triangle 2: in0 -> out1 -> out0
        rlTexCoord2f(0.0f, v0);
        rlVertex3f(x_in0, y_in0, z_in0);

        rlTexCoord2f(1.0f, v1);
        rlVertex3f(x_out1, y_out1, z_out1);

        rlTexCoord2f(1.0f, v0);
        rlVertex3f(x_out0, y_out0, z_out0);
    }
    rlEnd();
}

void VFX_DrawExpandingVacuumRing(Vector3 center, float radius, float bandWidth,
                                 Color ringColor, float alpha01)
{
    if (radius <= 0.015f || bandWidth <= 0.001f || alpha01 <= 0.002f) return;

    Texture2D ringTex = VacuumRing_GetTexture();

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    // 1. Subtle inner vacuum air pocket / floor pressure wash
    VacuumRing_DrawGroundDisc(center, radius * 0.95f, ringColor, alpha01);

    // 2. Primary expanding vacuum shockwave front (sharp bright razor rim)
    rlSetTexture(ringTex.id);
    float innerR1 = fmaxf(0.01f, radius - bandWidth);
    VacuumRing_DrawAnnulusStrip(center, innerR1, radius, ringColor, alpha01, 8.0f);

    // 3. Secondary trailing echo compression ring (trailing ripple)
    if (radius > 0.40f)
    {
        float echoR = radius * 0.72f;
        float echoW = bandWidth * 0.55f;
        float innerR2 = fmaxf(0.01f, echoR - echoW);
        Color echoCol = ColorLerp(ringColor, (Color){ 175, 220, 255, 255 }, 0.35f);
        VacuumRing_DrawAnnulusStrip(center, innerR2, echoR, echoCol, alpha01 * 0.52f, 6.0f);
    }

    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndBlendMode();
}

void VFX_ComposeVacuumRing(Vector3 center, float radius, float progress)
{
    float ringProgress = Clamp(progress / 0.85f, 0.0f, 1.0f);
    float ringEase = 1.0f - powf(1.0f - ringProgress, 3.0f);
    float curR = 0.35f + ringEase * (radius - 0.35f);
    float alpha = (1.0f - ringProgress) * 0.92f;
    Color ringCol = (Color){ 230, 245, 255, 255 };
    VFX_DrawExpandingVacuumRing(center, curR, 0.45f, ringCol, alpha);
}

// ── vc_optical_flare.inl ─────────────────────────────────────────────────────
//
// VFX 1: Optical Starburst & Horizontal Anamorphic Cine Streak.
// Provides a cinema-grade optical lens flare with an 8-ray diffraction starburst
// and an extreme-aspect horizontal anamorphic streak aligned to CameraRight.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/vfx_light.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static Texture2D s_optStreakTex = {0};
static Texture2D s_optStarburstTex = {0};

static Texture2D OptFlare_GetStreakTexture(void)
{
    if (s_optStreakTex.id == 0)
    {
        const int W = 256;
        const int H = 64;
        Image img = GenImageColor(W, H, BLANK);
        for (int y = 0; y < H; y++)
        {
            float v = (float)y / (float)(H - 1);
            float dy = fabsf(v - 0.5f) * 2.0f; // 0 at center line, 1 at top/bottom

            // Extreme thin optical core + subtle atmospheric halo
            float vCore = expf(-(dy * dy) * 85.0f) * 0.92f;
            float vHalo = expf(-(dy * dy) * 12.0f) * 0.08f;
            float vFalloff = vCore + vHalo;

            for (int x = 0; x < W; x++)
            {
                float u = (float)x / (float)(W - 1);
                float dx = fabsf(u - 0.5f) * 2.0f; // 0 at center, 1 at ends

                // Long cinematic horizontal wings with smooth power falloff
                float hFalloff = powf(fmaxf(0.0f, 1.0f - dx), 1.85f);
                float alpha = vFalloff * hFalloff;

                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) });
            }
        }
        s_optStreakTex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(s_optStreakTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_optStreakTex, TEXTURE_WRAP_CLAMP);
    }
    return s_optStreakTex;
}

static Texture2D OptFlare_GetStarburstTexture(void)
{
    if (s_optStarburstTex.id == 0)
    {
        const int S = 128;
        Image img = GenImageColor(S, S, BLANK);
        for (int y = 0; y < S; y++)
        {
            for (int x = 0; x < S; x++)
            {
                float u = ((float)x + 0.5f) / (float)S * 2.0f - 1.0f;
                float v = ((float)y + 0.5f) / (float)S * 2.0f - 1.0f;
                float r2 = u * u + v * v;
                if (r2 >= 1.0f) continue;
                float r = sqrtf(r2);
                float angle = atan2f(v, u);

                // 4 primary cardinal spikes (horizontal & vertical)
                float c4 = fabsf(cosf(2.0f * angle));
                float spikeCardinal = powf(c4, 38.0f) * expf(-r * 2.5f);

                // 4 diagonal secondary spikes (45 deg)
                float s4 = fabsf(sinf(2.0f * angle));
                float spikeDiagonal = powf(s4, 30.0f) * expf(-r * 3.8f) * 0.70f;

                // Pinpoint central hot core
                float core = expf(-r2 * 80.0f) * 1.35f;
                float corona = expf(-r2 * 9.0f) * 0.38f;

                float alpha = (core + corona + spikeCardinal + spikeDiagonal) * (1.0f - r * r);
                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) });
            }
        }
        s_optStarburstTex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(s_optStarburstTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_optStarburstTex, TEXTURE_WRAP_CLAMP);
    }
    return s_optStarburstTex;
}

void VFX_DrawOpticalStarburstStreak(Vector3 pos, Color coreCol, Color streakCol,
                                    float starRadius, float streakLength,
                                    float streakThickness, float intensity, Camera3D camera)
{
    if (intensity <= 0.001f) return;

    Vector3 camFwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(camFwd, camera.up));
    Vector3 camUp = Vector3CrossProduct(camRight, camFwd);

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    // 1. Multi-ray diffraction starburst billboard
    if (starRadius > 0.001f)
    {
        Texture2D starTex = OptFlare_GetStarburstTexture();
        float curStarRadius = starRadius * (0.88f + 0.12f * sinf(GetTime() * 16.0f));
        Vector3 r = Vector3Scale(camRight, curStarRadius);
        Vector3 u = Vector3Scale(camUp, curStarRadius);

        Color sCol = ColorAlpha(coreCol, Clamp(intensity * 0.95f, 0.0f, 1.0f));

        rlSetTexture(starTex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(sCol.r, sCol.g, sCol.b, sCol.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - r.x - u.x, pos.y - r.y - u.y, pos.z - r.z - u.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + r.x - u.x, pos.y + r.y - u.y, pos.z + r.z - u.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + r.x + u.x, pos.y + r.y + u.y, pos.z + r.z + u.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - r.x + u.x, pos.y - r.y + u.y, pos.z - r.z + u.z);
        rlEnd();
        rlSetTexture(0);
    }

    // 2. Horizontal Anamorphic Cine Streak
    if (streakLength > 0.001f && streakThickness > 0.001f)
    {
        Texture2D streakTex = OptFlare_GetStreakTexture();
        Vector3 rH = Vector3Scale(camRight, streakLength * 0.5f);
        Vector3 uH = Vector3Scale(camUp, streakThickness * 0.5f);

        Color streakTint = ColorAlpha(streakCol, Clamp(intensity * 0.92f, 0.0f, 1.0f));

        rlSetTexture(streakTex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(streakTint.r, streakTint.g, streakTint.b, streakTint.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - rH.x - uH.x, pos.y - rH.y - uH.y, pos.z - rH.z - uH.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + rH.x - uH.x, pos.y + rH.y - uH.y, pos.z + rH.z - uH.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + rH.x + uH.x, pos.y + rH.y + uH.y, pos.z + rH.z + uH.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - rH.x + uH.x, pos.y - rH.y + uH.y, pos.z - rH.z + uH.z);
        rlEnd();
        rlSetTexture(0);
    }

    rlEnableDepthMask();
    EndBlendMode();

    VFXLight_Spawn(pos, coreCol, 2.8f * intensity, 0.04f, VFX_PRIORITY_HIGH_ULTIMATE);
}

void VFX_ComposeOpticalFlare(Vector3 pos, float starRadius, float streakLength, float intensity, Camera3D camera)
{
    Color coreCol = (Color){ 255, 255, 255, 255 };
    Color streakCol = (Color){ 230, 245, 255, 255 };
    VFX_DrawOpticalStarburstStreak(pos, coreCol, streakCol, starRadius, streakLength, 0.040f, intensity, camera);
}

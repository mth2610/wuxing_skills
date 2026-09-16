// ── vc_optical_flare.inl ─────────────────────────────────────────────────────
//
// VFX 1: Cinema-Grade Optical Lens Flare & Anamorphic Cine Streak.
// Multi-layered optical composition:
//   1. Blinding incandescent diamond core with soft spherical corona.
//   2. 8 razor-sharp diffraction blades + 8 micro-diffraction tertiary rays.
//   3. Secondary breathing starburst for organic optical scintillations.
//   4. Dual-layer horizontal anamorphic streak (electric cyan halo + needle core).
//   5. Subtle circular lens aperture reflection ring.
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
        const int W = 512;
        const int H = 128;
        Image img = GenImageColor(W, H, BLANK);
        for (int y = 0; y < H; y++)
        {
            float v = (float)y / (float)(H - 1);
            float dy = fabsf(v - 0.5f) * 2.0f; // 0 at center line, 1 at top/bottom

            // Ultra-sharp needle core + mid glow + wide soft atmospheric wing
            float vCore = expf(-(dy * dy) * 450.0f) * 1.0f;
            float vMid  = expf(-(dy * dy) * 55.0f) * 0.45f;
            float vHalo = expf(-(dy * dy) * 10.0f) * 0.15f;
            float vFalloff = vCore + vMid + vHalo;

            for (int x = 0; x < W; x++)
            {
                float u = (float)x / (float)(W - 1);
                float dx = fabsf(u - 0.5f) * 2.0f; // 0 at center, 1 at ends

                // Smooth cinematic horizontal wing taper
                float hTaper = powf(fmaxf(0.0f, 1.0f - dx), 1.75f);
                float alpha = vFalloff * hTaper;
                if (alpha < 0.001f) continue;

                // Subtle chromatic tint: hot core is pure white, horizontal wings shift to soft electric cyan
                float cyanShift = dx * 0.45f;
                unsigned char rCol = (unsigned char)(Clamp((1.0f - cyanShift * 0.65f) * 255.0f, 0.0f, 255.0f));
                unsigned char gCol = (unsigned char)(Clamp((1.0f - cyanShift * 0.20f) * 255.0f, 0.0f, 255.0f));
                unsigned char bCol = 255;
                unsigned char aCol = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f);

                ImageDrawPixel(&img, x, y, (Color){ rCol, gCol, bCol, aCol });
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
        const int S = 256;
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

                // 4 Razor-sharp cardinal diffraction spikes (horizontal & vertical)
                float c4 = fabsf(cosf(2.0f * angle));
                float spikeCardinal = powf(c4, 72.0f) * expf(-r * 2.2f);

                // 4 Diagonal secondary spikes (45 deg)
                float s4 = fabsf(sinf(2.0f * angle));
                float spikeDiagonal = powf(s4, 60.0f) * expf(-r * 3.4f) * 0.68f;

                // 8 Micro-diffraction tertiary shimmer rays
                float c8 = fabsf(cosf(4.0f * angle));
                float microRays = powf(c8, 45.0f) * expf(-r * 4.8f) * 0.32f;

                // Pinpoint central hot core + soft spherical iris corona
                float core = expf(-r2 * 180.0f) * 2.2f;
                float corona = expf(-r2 * 14.0f) * 0.42f;

                float envelope = 1.0f - r * r;
                float alpha = (core + corona + spikeCardinal + spikeDiagonal + microRays) * envelope;
                if (alpha < 0.001f) continue;

                // Chromatic rim: core white, outer edges soft azure
                float tint = r * 0.35f;
                unsigned char rCol = (unsigned char)(Clamp((1.0f - tint * 0.7f) * 255.0f, 0.0f, 255.0f));
                unsigned char gCol = (unsigned char)(Clamp((1.0f - tint * 0.25f) * 255.0f, 0.0f, 255.0f));
                unsigned char bCol = 255;
                unsigned char aCol = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f);

                ImageDrawPixel(&img, x, y, (Color){ rCol, gCol, bCol, aCol });
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
    rlDisableDepthTest();

    float time = (float)GetTime();

    // 2. Multi-Ray Razor Diffraction Starburst Billboard (Primary + Secondary breathing shimmer)
    if (starRadius > 0.001f)
    {
        Texture2D starTex = OptFlare_GetStarburstTexture();
        float curStarRadius = starRadius * (0.92f + 0.08f * sinf(time * 14.0f));
        Vector3 r = Vector3Scale(camRight, curStarRadius);
        Vector3 u = Vector3Scale(camUp, curStarRadius);

        Color sCol = ColorAlpha(coreCol, Clamp(intensity * 0.98f, 0.0f, 1.0f));

        // Primary starburst
        rlSetTexture(starTex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(sCol.r, sCol.g, sCol.b, sCol.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - r.x - u.x, pos.y - r.y - u.y, pos.z - r.z - u.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + r.x - u.x, pos.y + r.y - u.y, pos.z + r.z - u.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + r.x + u.x, pos.y + r.y + u.y, pos.z + r.z + u.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - r.x + u.x, pos.y - r.y + u.y, pos.z - r.z + u.z);
        rlEnd();

        // Secondary counter-shimmer starburst offset by 22.5 degrees
        float rotAngle = 0.392699f; // 22.5 deg
        float cosA = cosf(rotAngle);
        float sinA = sinf(rotAngle);
        Vector3 rRot = Vector3Add(Vector3Scale(r, cosA * 0.72f), Vector3Scale(u, -sinA * 0.72f));
        Vector3 uRot = Vector3Add(Vector3Scale(r, sinA * 0.72f), Vector3Scale(u, cosA * 0.72f));
        Color sCol2 = ColorAlpha(streakCol, Clamp(intensity * 0.55f * (0.85f + 0.15f * cosf(time * 18.0f)), 0.0f, 1.0f));

        rlBegin(RL_QUADS);
        rlColor4ub(sCol2.r, sCol2.g, sCol2.b, sCol2.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - rRot.x - uRot.x, pos.y - rRot.y - uRot.y, pos.z - rRot.z - uRot.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + rRot.x - uRot.x, pos.y + rRot.y - uRot.y, pos.z + rRot.z - uRot.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + rRot.x + uRot.x, pos.y + rRot.y + uRot.y, pos.z + rRot.z + uRot.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - rRot.x + uRot.x, pos.y - rRot.y + uRot.y, pos.z - rRot.z + uRot.z);
        rlEnd();

        rlSetTexture(0);
    }

    // 3. Horizontal Anamorphic Cine Streak (Double-layer: broad electric cyan aura + needle white core)
    if (streakLength > 0.001f && streakThickness > 0.001f)
    {
        Texture2D streakTex = OptFlare_GetStreakTexture();
        Vector3 rH = Vector3Scale(camRight, streakLength * 0.5f);
        Vector3 uH = Vector3Scale(camUp, streakThickness * 0.5f);

        // Outer cyan bloom streak
        Color streakTint = ColorAlpha(streakCol, Clamp(intensity * 0.90f, 0.0f, 1.0f));
        rlSetTexture(streakTex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(streakTint.r, streakTint.g, streakTint.b, streakTint.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - rH.x - uH.x, pos.y - rH.y - uH.y, pos.z - rH.z - uH.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + rH.x - uH.x, pos.y + rH.y - uH.y, pos.z + rH.z - uH.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + rH.x + uH.x, pos.y + rH.y + uH.y, pos.z + rH.z + uH.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - rH.x + uH.x, pos.y - rH.y + uH.y, pos.z - rH.z + uH.z);
        rlEnd();

        // Inner ultra-hot white needle core (half thickness, longer wings)
        Vector3 rHCore = Vector3Scale(camRight, streakLength * 0.65f);
        Vector3 uHCore = Vector3Scale(camUp, streakThickness * 0.28f);
        Color coreTint = ColorAlpha(coreCol, Clamp(intensity * 0.98f, 0.0f, 1.0f));

        rlBegin(RL_QUADS);
        rlColor4ub(coreTint.r, coreTint.g, coreTint.b, coreTint.a);
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(pos.x - rHCore.x - uHCore.x, pos.y - rHCore.y - uHCore.y, pos.z - rHCore.z - uHCore.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(pos.x + rHCore.x - uHCore.x, pos.y + rHCore.y - uHCore.y, pos.z + rHCore.z - uHCore.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(pos.x + rHCore.x + uHCore.x, pos.y + rHCore.y + uHCore.y, pos.z + rHCore.z + uHCore.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(pos.x - rHCore.x + uHCore.x, pos.y - rHCore.y + uHCore.y, pos.z - rHCore.z + uHCore.z);
        rlEnd();

        rlSetTexture(0);
    }

    rlEnableDepthTest();
    rlEnableDepthMask();
    EndBlendMode();

    VFXLight_Spawn(pos, coreCol, 3.2f * intensity, 0.04f, VFX_PRIORITY_HIGH_ULTIMATE);
}

void VFX_ComposeOpticalFlare(Vector3 pos, float starRadius, float streakLength, float intensity, Camera3D camera)
{
    Color coreCol = (Color){ 255, 255, 255, 255 };
    Color streakCol = (Color){ 215, 240, 255, 255 };
    VFX_DrawOpticalStarburstStreak(pos, coreCol, streakCol, starRadius, streakLength, 0.042f, intensity, camera);
}

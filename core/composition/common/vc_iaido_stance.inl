// ── vc_iaido_stance.inl ──────────────────────────────────────────────────────
//
// VFX 5: Complete Iaido Quick-Draw / Counter Stance Composite.
// Orchestrates all 4 basic primitives into the high-contrast anime/sci-fi composition:
//   1. Optical Starburst & Horizontal Anamorphic Cine Streak (Sword Hilt Focal Point).
//   2. High-contrast Character Silhouette Glow & Surface Envelope Field.
//   3. 3D Parabolic Curved Vacuum Wind Streamlines (High-altitude Sweeping Arches).
//   4. Thin Planar Vacuum Ground Annulus Expansion.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>

void VFX_ComposeIaidoStance(Vector3 playerPos, float yaw, float progress,
                            float duration, Camera3D camera,
                            const void *animStatePtr)
{
    if (progress < 0.0f || progress >= 1.0f) return;

    Vector3 fwd = (Vector3){ sinf(yaw), 0.0f, cosf(yaw) };
    Vector3 rgt = (Vector3){ cosf(yaw), 0.0f, -sinf(yaw) };

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 4: Expanding Vacuum Ground Ring
    // ─────────────────────────────────────────────────────────────────────────
    float groundProgress = Clamp(progress / 0.75f, 0.0f, 1.0f);
    float ringEase = 1.0f - powf(1.0f - groundProgress, 3.0f);
    float ringRadius = 0.40f + ringEase * 2.45f;
    float ringAlpha = (1.0f - groundProgress) * 0.92f;
    VFX_DrawExpandingVacuumRing(playerPos, ringRadius, 0.42f, (Color){ 230, 245, 255, 255 }, ringAlpha);

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 2: Character Silhouette Glow & Surface Aura
    // ─────────────────────────────────────────────────────────────────────────
    float silhIntensity = 1.0f;
    if (progress > 0.80f)
    {
        silhIntensity = 1.0f - (progress - 0.80f) / 0.20f;
    }
    Color silhColor = (Color){ 245, 250, 255, 255 };
    VFX_DrawCharacterSilhouetteGlow(playerPos, yaw, silhColor, silhIntensity, animStatePtr);

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 3: 3D High-Altitude Parabolic Vacuum Streamlines
    // ─────────────────────────────────────────────────────────────────────────
    Color vacuumRim  = (Color){ 255, 255, 255, 255 };
    Color vacuumBody = (Color){ 185, 220, 255, 255 };

    // Arc 1: Left Swoop Arch (grand high-altitude arch matching reference)
    Vector3 a1_p0 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -1.0f), Vector3Add(Vector3Scale(fwd, -0.7f), (Vector3){ 0.0f, 0.05f, 0.0f })));
    Vector3 a1_p1 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -2.4f), Vector3Add(Vector3Scale(fwd, -0.1f), (Vector3){ 0.0f, 3.10f, 0.0f })));
    Vector3 a1_p2 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -0.5f), Vector3Add(Vector3Scale(fwd,  1.6f), (Vector3){ 0.0f, 3.60f, 0.0f })));
    Vector3 a1_p3 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  1.5f), Vector3Add(Vector3Scale(fwd,  2.2f), (Vector3){ 0.0f, 0.50f, 0.0f })));

    float arc1Prog = Clamp((progress - 0.06f) / 0.84f, 0.0f, 1.0f);
    VFX_DrawCurvedVacuumArc3D(a1_p0, a1_p1, a1_p2, a1_p3, vacuumRim, vacuumBody, 0.38f, arc1Prog, duration, camera);

    // Arc 2: Overhead Crown Arch (spiraling across the player's silhouette)
    Vector3 a2_p0 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  1.1f), Vector3Add(Vector3Scale(fwd, -0.5f), (Vector3){ 0.0f, 0.15f, 0.0f })));
    Vector3 a2_p1 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  1.9f), Vector3Add(Vector3Scale(fwd,  0.3f), (Vector3){ 0.0f, 3.20f, 0.0f })));
    Vector3 a2_p2 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -0.4f), Vector3Add(Vector3Scale(fwd,  1.3f), (Vector3){ 0.0f, 3.70f, 0.0f })));
    Vector3 a2_p3 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -1.8f), Vector3Add(Vector3Scale(fwd,  0.4f), (Vector3){ 0.0f, 1.20f, 0.0f })));

    float arc2Prog = Clamp((progress - 0.14f) / 0.80f, 0.0f, 1.0f);
    VFX_DrawCurvedVacuumArc3D(a2_p0, a2_p1, a2_p2, a2_p3, vacuumRim, vacuumBody, 0.32f, arc2Prog, duration, camera);

    // Arc 3: Forward Sweeping Streamline (reaching toward opponent/boss)
    Vector3 a3_p0 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -0.2f), Vector3Add(Vector3Scale(fwd,  0.4f), (Vector3){ 0.0f, 0.40f, 0.0f })));
    Vector3 a3_p1 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  1.2f), Vector3Add(Vector3Scale(fwd,  1.4f), (Vector3){ 0.0f, 1.40f, 0.0f })));
    Vector3 a3_p2 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  2.4f), Vector3Add(Vector3Scale(fwd,  2.2f), (Vector3){ 0.0f, 2.10f, 0.0f })));
    Vector3 a3_p3 = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt,  3.6f), Vector3Add(Vector3Scale(fwd,  3.0f), (Vector3){ 0.0f, 0.90f, 0.0f })));

    float arc3Prog = Clamp((progress - 0.22f) / 0.74f, 0.0f, 1.0f);
    VFX_DrawCurvedVacuumArc3D(a3_p0, a3_p1, a3_p2, a3_p3, vacuumRim, vacuumBody, 0.26f, arc3Prog, duration, camera);

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 1: Optical Starburst & Anamorphic Cine Streak (Sword Hilt Focal Point)
    // ─────────────────────────────────────────────────────────────────────────
    // Anchored right at the left hip of the crouch stance (where the sword hilt is)
    Vector3 hiltPos = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -0.14f), Vector3Add(Vector3Scale(fwd, 0.10f), (Vector3){ 0.0f, 0.20f, 0.0f })));

    float flareIntensity = 0.0f;
    float streakLen = 0.0f;
    float starRad = 0.0f;

    if (progress < 0.60f)
    {
        // Charging phase: starburst breathing and building up
        float chargeP = progress / 0.60f;
        flareIntensity = chargeP * (0.80f + 0.20f * sinf(GetTime() * 18.0f));
        starRad = 0.25f + chargeP * 0.25f;
        streakLen = 0.5f + chargeP * 0.8f;
    }
    else
    {
        // Apex burst release phase: blinding anamorphic streak flash
        float burstP = (progress - 0.60f) / 0.40f;
        float flashEnv = (burstP < 0.12f) ? (burstP / 0.12f) : (1.0f - (burstP - 0.12f) / 0.88f);
        flareIntensity = 1.0f + flashEnv * 0.8f;
        starRad = 0.65f + flashEnv * 0.35f;
        streakLen = 2.6f + flashEnv * 1.4f;
    }

    Color starColor   = (Color){ 255, 255, 255, 255 };
    Color streakColor = (Color){ 235, 245, 255, 255 };
    VFX_DrawOpticalStarburstStreak(hiltPos, starColor, streakColor, starRad, streakLen, 0.038f, flareIntensity, camera);
}

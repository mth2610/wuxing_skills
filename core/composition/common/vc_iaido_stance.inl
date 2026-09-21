// ── vc_iaido_stance.inl ──────────────────────────────────────────────────────
//
// VFX 5: Complete Iaido Quick-Draw / Counter Stance Composite.
// Orchestrates all 4 basic primitives into the high-contrast anime/sci-fi composition:
//   1. Optical Starburst & Horizontal Anamorphic Cine Streak (Sword Hilt Focal Point).
//   2. Restrained Mesh Surface Aura overlay.
//   3. 3D Parabolic Curved Vacuum Wind Streamlines (High-altitude Sweeping Arches).
//   4. Thin Planar Vacuum Ground Annulus Expansion.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/time_fx.h"
#include "character/character_model.h"
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
    // LAYER 2: Mesh Surface Aura
    // ─────────────────────────────────────────────────────────────────────────
    float silhIntensity = 1.0f;
    if (progress > 0.80f)
    {
        silhIntensity = 1.0f - (progress - 0.80f) / 0.20f;
    }
    (void)animStatePtr;
    if (CharacterModel_IsLoaded()) {
        VFX_MeshSurfaceAuraParams aura = {
            .materialColor = (Color){245, 250, 255, 255},
            .rimWidth = 2.8f,
            .rimIntensity = 0.75f,
            .opacity = 0.34f * silhIntensity,
        };
        Matrix transform = MatrixMultiply(MatrixRotateY(yaw),
                                          MatrixTranslate(playerPos.x, playerPos.y, playerPos.z));
        VFX_DrawModelSurfaceAura(CharacterModel_GetModel(), transform, &aura);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 1: Optical Starburst & Anamorphic Cine Streak (Sword Hilt Focal Point)
    // ─────────────────────────────────────────────────────────────────────────
    // Anchored right at the left hip of the crouch stance (where the sword hilt is)
    Vector3 hiltPos = Vector3Add(playerPos, Vector3Add(Vector3Scale(rgt, -0.14f), Vector3Add(Vector3Scale(fwd, 0.10f), (Vector3){ 0.0f, 0.20f, 0.0f })));

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 3: Vacuum Suction Vortex Streamlines Converging at Sword Hilt
    // ─────────────────────────────────────────────────────────────────────────
    float suctionProg = Clamp(progress / 0.88f, 0.0f, 1.0f);
    VFX_ComposeVacuumConverge(hiltPos, 2.5f, suctionProg, camera);


    float flareIntensity = 0.0f;
    float streakLen = 0.0f;
    float starRad = 0.0f;

    if (progress < 0.60f)
    {
        // Charging phase: starburst breathing and building up
        float chargeP = progress / 0.60f;
        flareIntensity = chargeP * (0.80f + 0.20f * sinf(TimeFX_Elapsed() * 18.0f));
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

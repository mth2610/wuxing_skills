#include "character/weapon_trail.h"
#include "core/vfx_render.h"
#include "core/path_spline.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

#define TRAIL_TEX_WIDTH  128
#define TRAIL_TEX_HEIGHT 128
#define TRAIL_SUBDIVISIONS 5 // Subdivisions per control point span for C1 spline smoothness

static Texture2D s_trailTex = {0};

static inline float Trail_SmoothStep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// -----------------------------------------------------------------------------
// Procedural Sweep Slash Mask (Hot outer razor rim + inward smear + striations)
// -----------------------------------------------------------------------------
static void WeaponTrail_EnsureTexture(void) {
    if (s_trailTex.id != 0) return;

    Image img = GenImageColor(TRAIL_TEX_WIDTH, TRAIL_TEX_HEIGHT, BLANK);

    for (int y = 0; y < TRAIL_TEX_HEIGHT; y++) {
        float v = ((float)y + 0.5f) / (float)TRAIL_TEX_HEIGHT; // Along sweep (0 = head, 1 = tail)
        for (int x = 0; x < TRAIL_TEX_WIDTH; x++) {
            float u = ((float)x + 0.5f) / (float)TRAIL_TEX_WIDTH; // Across blade (0 = TIP, 1 = BASE)
            float e = 1.0f - u;                                   // Outerness (1 at tip, 0 at base)

            // 1. Sharp razor cutting rim at tip (u ~ 0)
            float d = u / 0.065f;
            float rim = expf(-d * d);

            // 2. Body smear fading fast inward towards hilt
            float body = powf(e, 2.2f);

            // 3. Energetic striations along the sweep
            float lines1 = 0.76f + 0.24f * sinf(u * PI * 14.0f + v * 2.2f);
            float lines2 = 0.86f + 0.14f * sinf(u * PI * 28.0f - v * 0.9f);

            // 4. Longitudinal energy pulse
            float pulse = 0.82f + 0.18f * sinf(v * PI * 4.0f + u * 2.6f);

            // 5. Antialiasing shoulder at the very tip to prevent pixel staircasing
            float shoulder = 1.0f - Trail_SmoothStep(0.982f, 1.0f, e);

            float alpha = (0.38f * body + 0.72f * rim) * lines1 * lines2 * pulse * shoulder;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;

            ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(alpha * 255.0f) });
        }
    }

    s_trailTex = LoadTextureFromImage(img);
    UnloadImage(img);

    if (s_trailTex.id != 0) {
        SetTextureFilter(s_trailTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_trailTex, TEXTURE_WRAP_CLAMP);
    }
}

// -----------------------------------------------------------------------------
// Lifecycle & Configuration
// -----------------------------------------------------------------------------
void WeaponTrail_Init(WeaponTrail *trail) {
    if (!trail) return;
    memset(trail, 0, sizeof(WeaponTrail));
    trail->minSpeed  = 1.50f; // m/s (triggers on attacks and quick turns)
    trail->maxAge    = 0.28f; // ~280ms duration for wide crescent arc coverage
    trail->glowColor = (Color){ 160, 220, 255, 255 }; // Metal sword cyan/silver aura
    trail->coreColor = (Color){ 255, 255, 255, 255 }; // Pure white-hot cutting core
    trail->enabled   = true;
}

void WeaponTrail_Reset(WeaponTrail *trail) {
    if (!trail) return;
    trail->count   = 0;
    trail->hasPrev = false;
}

void WeaponTrail_SetColors(WeaponTrail *trail, Color glowColor, Color coreColor) {
    if (!trail) return;
    trail->glowColor = glowColor;
    trail->coreColor = coreColor;
}

// -----------------------------------------------------------------------------
// Sampling & Motion Tracking
// -----------------------------------------------------------------------------
static void WeaponTrail_PushNode(WeaponTrail *trail, Vector3 base, Vector3 tip) {
    if (trail->count < WEAPON_TRAIL_MAX_POINTS) {
        // Shift right to make space at index 0 (newest head)
        for (int i = trail->count; i > 0; i--) {
            trail->points[i] = trail->points[i - 1];
        }
        trail->count++;
    } else {
        // Full buffer: discard oldest tail
        for (int i = WEAPON_TRAIL_MAX_POINTS - 1; i > 0; i--) {
            trail->points[i] = trail->points[i - 1];
        }
    }

    trail->points[0].base  = base;
    trail->points[0].tip   = tip;
    trail->points[0].age   = 0.0f;
    trail->points[0].alpha = 1.0f;
}

void WeaponTrail_Sample(WeaponTrail *trail, Matrix weaponWorldMat, Vector3 localBase, Vector3 localTip, float dt, bool forceEmit) {
    if (!trail || !trail->enabled) return;

    Vector3 worldBase = Vector3Transform(localBase, weaponWorldMat);
    Vector3 worldTip  = Vector3Transform(localTip, weaponWorldMat);

    if (!trail->hasPrev) {
        trail->prevBase = worldBase;
        trail->prevTip  = worldTip;
        trail->hasPrev  = true;
        return;
    }

    // Teleport cut: if position jumps abruptly (e.g. respawn, camera cut), clear history
    float tipDist = Vector3Distance(worldTip, trail->prevTip);
    if (tipDist > 2.2f) {
        trail->count = 0;
        trail->prevBase = worldBase;
        trail->prevTip  = worldTip;
        return;
    }

    float speed = (dt > 1e-5f) ? (tipDist / dt) : 0.0f;
    bool shouldEmit = forceEmit || (speed >= trail->minSpeed);

    // Push new control point if moving fast and has travelled sufficient arc distance
    if (shouldEmit && (tipDist >= 0.020f || forceEmit)) {
        WeaponTrail_PushNode(trail, worldBase, worldTip);
        trail->prevBase = worldBase;
        trail->prevTip  = worldTip;
    }

    // Age existing trail points
    for (int i = 0; i < trail->count; ) {
        trail->points[i].age += dt;
        if (trail->points[i].age >= trail->maxAge) {
            // Prune expired tail node
            for (int j = i; j < trail->count - 1; j++) {
                trail->points[j] = trail->points[j + 1];
            }
            trail->count--;
        } else {
            float normAge = trail->points[i].age / trail->maxAge;
            trail->points[i].alpha = fmaxf(0.0f, 1.0f - normAge * normAge);
            i++;
        }
    }
}

void WeaponTrail_Fade(WeaponTrail *trail, float dt) {
    if (!trail || trail->count == 0) return;

    for (int i = 0; i < trail->count; ) {
        trail->points[i].age += dt;
        if (trail->points[i].age >= trail->maxAge) {
            for (int j = i; j < trail->count - 1; j++) {
                trail->points[j] = trail->points[j + 1];
            }
            trail->count--;
        } else {
            float normAge = trail->points[i].age / trail->maxAge;
            trail->points[i].alpha = fmaxf(0.0f, 1.0f - normAge * normAge);
            i++;
        }
    }

    trail->hasPrev = false;
}

// -----------------------------------------------------------------------------
// Rendering (Centripetal Catmull-Rom Spline Ribbon, Emission Additive Pass)
// -----------------------------------------------------------------------------
void WeaponTrail_Draw(const WeaponTrail *trail) {
    if (!trail || !trail->enabled || trail->count < 2) return;

    WeaponTrail_EnsureTexture();
    if (s_trailTex.id == 0) return;

    // Use Engine VFXRenderScope for atomic blend, depth-mask, and batch flushes
    VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);

    rlSetTexture(s_trailTex.id);
    rlBegin(RL_QUADS);

    int count = trail->count;

    // ── PASS 1: Broad Luminous Aura (Soft Atmospheric Halo) ──────────────────
    for (int i = 0; i < count - 1; i++) {
        int idx0 = (i > 0) ? (i - 1) : 0;
        int idx1 = i;
        int idx2 = i + 1;
        int idx3 = (i + 2 < count) ? (i + 2) : (count - 1);

        Vector3 tip0  = trail->points[idx0].tip;
        Vector3 tip1  = trail->points[idx1].tip;
        Vector3 tip2  = trail->points[idx2].tip;
        Vector3 tip3  = trail->points[idx3].tip;

        Vector3 base0 = trail->points[idx0].base;
        Vector3 base1 = trail->points[idx1].base;
        Vector3 base2 = trail->points[idx2].base;
        Vector3 base3 = trail->points[idx3].base;

        float alpha1 = trail->points[idx1].alpha;
        float alpha2 = trail->points[idx2].alpha;

        for (int s = 0; s < TRAIL_SUBDIVISIONS; s++) {
            float tA = (float)s / (float)TRAIL_SUBDIVISIONS;
            float tB = (float)(s + 1) / (float)TRAIL_SUBDIVISIONS;

            float globA = ((float)i + tA) / (float)(count - 1);
            float globB = ((float)i + tB) / (float)(count - 1);

            // Centripetal spline interpolation across blade endpoints
            Vector3 sTipA  = CatmullRom_Centripetal(tip0, tip1, tip2, tip3, tA);
            Vector3 sTipB  = CatmullRom_Centripetal(tip0, tip1, tip2, tip3, tB);
            Vector3 sBaseA = CatmullRom_Centripetal(base0, base1, base2, base3, tA);
            Vector3 sBaseB = CatmullRom_Centripetal(base0, base1, base2, base3, tB);

            // Envelope: needle sharp at head, smooth dissipation at tail
            float headTaperA = (globA < 0.08f) ? powf(globA / 0.08f, 0.65f) : 1.0f;
            float headTaperB = (globB < 0.08f) ? powf(globB / 0.08f, 0.65f) : 1.0f;
            float tailTaperA = powf(1.0f - globA, 0.70f);
            float tailTaperB = powf(1.0f - globB, 0.70f);

            float widthEnvA = headTaperA * tailTaperA;
            float widthEnvB = headTaperB * tailTaperB;

            // Halo expands 12% beyond blade tip for luminous rim
            sTipA = Vector3Add(sTipA, Vector3Scale(Vector3Subtract(sTipA, sBaseA), 0.12f));
            sTipB = Vector3Add(sTipB, Vector3Scale(Vector3Subtract(sTipB, sBaseB), 0.12f));

            // Base tapers inward towards tip based on width envelope
            sBaseA = Vector3Lerp(sTipA, sBaseA, widthEnvA);
            sBaseB = Vector3Lerp(sTipB, sBaseB, widthEnvB);

            float aA = (alpha1 + tA * (alpha2 - alpha1)) * 0.38f * widthEnvA;
            float aB = (alpha1 + tB * (alpha2 - alpha1)) * 0.38f * widthEnvB;

            Color cA = (Color){
                trail->glowColor.r, trail->glowColor.g, trail->glowColor.b,
                (unsigned char)(aA * 255.0f)
            };
            Color cB = (Color){
                trail->glowColor.r, trail->glowColor.g, trail->glowColor.b,
                (unsigned char)(aB * 255.0f)
            };

            // Front Face (u=0 at Tip, u=1 at Base)
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(0.0f, globA); rlVertex3f(sTipA.x, sTipA.y, sTipA.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(0.0f, globB); rlVertex3f(sTipB.x, sTipB.y, sTipB.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(1.0f, globB); rlVertex3f(sBaseB.x, sBaseB.y, sBaseB.z);
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(1.0f, globA); rlVertex3f(sBaseA.x, sBaseA.y, sBaseA.z);

            // Back Face
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(1.0f, globA); rlVertex3f(sBaseA.x, sBaseA.y, sBaseA.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(1.0f, globB); rlVertex3f(sBaseB.x, sBaseB.y, sBaseB.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(0.0f, globB); rlVertex3f(sTipB.x, sTipB.y, sTipB.z);
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(0.0f, globA); rlVertex3f(sTipA.x, sTipA.y, sTipA.z);
        }
    }

    // ── PASS 2: White-Hot Razor Cutting Core ─────────────────────────────────
    for (int i = 0; i < count - 1; i++) {
        int idx0 = (i > 0) ? (i - 1) : 0;
        int idx1 = i;
        int idx2 = i + 1;
        int idx3 = (i + 2 < count) ? (i + 2) : (count - 1);

        Vector3 tip0  = trail->points[idx0].tip;
        Vector3 tip1  = trail->points[idx1].tip;
        Vector3 tip2  = trail->points[idx2].tip;
        Vector3 tip3  = trail->points[idx3].tip;

        Vector3 base0 = trail->points[idx0].base;
        Vector3 base1 = trail->points[idx1].base;
        Vector3 base2 = trail->points[idx2].base;
        Vector3 base3 = trail->points[idx3].base;

        float alpha1 = trail->points[idx1].alpha;
        float alpha2 = trail->points[idx2].alpha;

        for (int s = 0; s < TRAIL_SUBDIVISIONS; s++) {
            float tA = (float)s / (float)TRAIL_SUBDIVISIONS;
            float tB = (float)(s + 1) / (float)TRAIL_SUBDIVISIONS;

            float globA = ((float)i + tA) / (float)(count - 1);
            float globB = ((float)i + tB) / (float)(count - 1);

            Vector3 sTipA  = CatmullRom_Centripetal(tip0, tip1, tip2, tip3, tA);
            Vector3 sTipB  = CatmullRom_Centripetal(tip0, tip1, tip2, tip3, tB);
            Vector3 sBaseA = CatmullRom_Centripetal(base0, base1, base2, base3, tA);
            Vector3 sBaseB = CatmullRom_Centripetal(base0, base1, base2, base3, tB);

            float headTaperA = (globA < 0.08f) ? powf(globA / 0.08f, 0.65f) : 1.0f;
            float headTaperB = (globB < 0.08f) ? powf(globB / 0.08f, 0.65f) : 1.0f;
            float tailTaperA = powf(1.0f - globA, 0.70f);
            float tailTaperB = powf(1.0f - globB, 0.70f);

            float widthEnvA = headTaperA * tailTaperA;
            float widthEnvB = headTaperB * tailTaperB;

            sBaseA = Vector3Lerp(sTipA, sBaseA, widthEnvA);
            sBaseB = Vector3Lerp(sTipB, sBaseB, widthEnvB);

            float aA = (alpha1 + tA * (alpha2 - alpha1)) * 0.90f * widthEnvA;
            float aB = (alpha1 + tB * (alpha2 - alpha1)) * 0.90f * widthEnvB;

            Color cA = (Color){
                trail->coreColor.r, trail->coreColor.g, trail->coreColor.b,
                (unsigned char)(aA * 255.0f)
            };
            Color cB = (Color){
                trail->coreColor.r, trail->coreColor.g, trail->coreColor.b,
                (unsigned char)(aB * 255.0f)
            };

            // Front Face
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(0.0f, globA); rlVertex3f(sTipA.x, sTipA.y, sTipA.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(0.0f, globB); rlVertex3f(sTipB.x, sTipB.y, sTipB.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(1.0f, globB); rlVertex3f(sBaseB.x, sBaseB.y, sBaseB.z);
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(1.0f, globA); rlVertex3f(sBaseA.x, sBaseA.y, sBaseA.z);

            // Back Face
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(1.0f, globA); rlVertex3f(sBaseA.x, sBaseA.y, sBaseA.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(1.0f, globB); rlVertex3f(sBaseB.x, sBaseB.y, sBaseB.z);
            rlColor4ub(cB.r, cB.g, cB.b, cB.a); rlTexCoord2f(0.0f, globB); rlVertex3f(sTipB.x, sTipB.y, sTipB.z);
            rlColor4ub(cA.r, cA.g, cA.b, cA.a); rlTexCoord2f(0.0f, globA); rlVertex3f(sTipA.x, sTipA.y, sTipA.z);
        }
    }

    rlEnd();
    rlSetTexture(0);

    VFXRender_EndDraw(&scope);
}

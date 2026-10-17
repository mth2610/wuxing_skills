#ifndef WUXING_MEADOW_PALETTE_H
#define WUXING_MEADOW_PALETTE_H

#include <math.h>
#include "raylib.h"

/* The ecology bake and immutable blade colors use the same broad habitat.
 * Linear RGB counterpart: shaders/meadow_palette.glsl. */
static inline float MeadowHabitat(float x, float z)
{
    return (sinf(x * 0.15f + z * 0.10f) * 0.5f + 0.5f) * 0.6f
         + (sinf(x * -0.11f + z * 0.20f + 1.8f) * 0.5f + 0.5f) * 0.4f;
}

static inline Vector3 MeadowCanopyColor(float habitat)
{
    float h = fminf(1.0f, fmaxf(0.0f, habitat));
    // 3 botanical bands: deep mossy olive (0.0 .. 0.35), lush emerald clover (0.35 .. 0.75), sunlit golden meadow (0.75 .. 1.0)
    float t0 = fminf(1.0f, fmaxf(0.0f, (h - 0.20f) / 0.40f));
    float s0 = t0 * t0 * (3.0f - 2.0f * t0);
    float t1 = fminf(1.0f, fmaxf(0.0f, (h - 0.55f) / 0.35f));
    float s1 = t1 * t1 * (3.0f - 2.0f * t1);

    Vector3 deepMoss   = {0.24f, 0.40f, 0.13f};
    Vector3 lushClover = {0.32f, 0.50f, 0.16f};
    Vector3 sunlitGold = {0.44f, 0.56f, 0.22f};

    Vector3 c = {
        deepMoss.x + (lushClover.x - deepMoss.x) * s0,
        deepMoss.y + (lushClover.y - deepMoss.y) * s0,
        deepMoss.z + (lushClover.z - deepMoss.z) * s0,
    };
    return (Vector3){
        c.x + (sunlitGold.x - c.x) * s1,
        c.y + (sunlitGold.y - c.y) * s1,
        c.z + (sunlitGold.z - c.z) * s1,
    };
}
#endif

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
    float green = fminf(1.0f, fmaxf(0.0f, (habitat - 0.25f) / 0.50f));
    green = green * green * (3.0f - 2.0f * green);
    float sunlit = fminf(1.0f, fmaxf(0.0f, (0.45f - habitat) / 0.30f));
    sunlit = sunlit * sunlit * (3.0f - 2.0f * sunlit);
    Vector3 color = {0.36f + (0.28f - 0.36f) * green,
                     0.54f + (0.48f - 0.54f) * green,
                     0.15f + (0.13f - 0.15f) * green};
    return (Vector3){color.x + (0.44f - color.x) * sunlit,
                     color.y + (0.57f - color.y) * sunlit,
                     color.z + (0.18f - color.z) * sunlit};
}
#endif

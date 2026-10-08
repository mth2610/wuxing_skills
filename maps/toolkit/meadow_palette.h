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
    float straw = fminf(1.0f, fmaxf(0.0f, (0.45f - habitat) / 0.30f));
    straw = straw * straw * (3.0f - 2.0f * straw);
    Vector3 color = {0.46f + (0.32f - 0.46f) * green,
                     0.51f + (0.48f - 0.51f) * green,
                     0.19f + (0.14f - 0.19f) * green};
    return (Vector3){color.x + (0.66f - color.x) * straw,
                     color.y + (0.56f - color.y) * straw,
                     color.z + (0.30f - color.z) * straw};
}
#endif

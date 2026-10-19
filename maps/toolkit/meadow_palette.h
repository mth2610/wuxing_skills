#ifndef WUXING_MEADOW_PALETTE_H
#define WUXING_MEADOW_PALETTE_H

#include <math.h>
#include "raylib.h"

/* Habitat -> canopy colour model. The ecology bake and the immutable blade colours use the
 * same broad habitat. GLSL counterpart: shaders/meadow_palette.glsl.
 *
 * ===== PALETTE DEFINITION: must stay IDENTICAL in both files =====
 * Near blades take their tip colour from this header (baked at init) and distant ground takes
 * it from the shader, so any drift shows up as a visible seam where blades fade into ground.
 * maps/tests/test_meadow_palette_sync.py parses the MEADOW_PALETTE_* block below and the
 * matching block in meadow_palette.glsl, and fails on any mismatch. Edit both in one change.
 *
 * Three bands are blended by two overlapping smoothsteps over habitat:
 *   deep moss    -> lush clover : habitat 0.18 .. 0.54
 *   lush clover  -> sunlit gold : habitat 0.48 .. 0.82
 * (the second blend starts before the first ends, so 0.48 .. 0.54 mixes all three.)
 *
 * Colour-space note: this file passes the values through unchanged. Whether they are read as
 * linear or display-referred is decided by the consuming shader, not here.
 */
#define MEADOW_PALETTE_DEEP_MOSS 0.17f, 0.34f, 0.14f
#define MEADOW_PALETTE_LUSH_CLOVER 0.30f, 0.47f, 0.15f
#define MEADOW_PALETTE_SUNLIT_GOLD 0.54f, 0.56f, 0.20f
#define MEADOW_PALETTE_CLOVER_EDGE0 0.18f
#define MEADOW_PALETTE_CLOVER_EDGE1 0.54f
#define MEADOW_PALETTE_GOLD_EDGE0 0.48f
#define MEADOW_PALETTE_GOLD_EDGE1 0.82f

static inline float MeadowHabitat(float x, float z)
{
    return (sinf(x * 0.15f + z * 0.10f) * 0.5f + 0.5f) * 0.6f + (sinf(x * -0.11f + z * 0.20f + 1.8f) * 0.5f + 0.5f) * 0.4f;
}

/* Same definition as GLSL smoothstep(): clamps to [0,1] first, so out-of-range (and NaN)
 * habitat resolves to the first / last band exactly like the shader does. */
static inline float MeadowSmoothstep(float edge0, float edge1, float x)
{
    float t = fminf(1.0f, fmaxf(0.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

static inline Vector3 MeadowCanopyColor(float habitat)
{
    const Vector3 deepMoss = {MEADOW_PALETTE_DEEP_MOSS};
    const Vector3 lushClover = {MEADOW_PALETTE_LUSH_CLOVER};
    const Vector3 sunlitGold = {MEADOW_PALETTE_SUNLIT_GOLD};

    float s0 = MeadowSmoothstep(MEADOW_PALETTE_CLOVER_EDGE0, MEADOW_PALETTE_CLOVER_EDGE1, habitat);
    float s1 = MeadowSmoothstep(MEADOW_PALETTE_GOLD_EDGE0, MEADOW_PALETTE_GOLD_EDGE1, habitat);

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
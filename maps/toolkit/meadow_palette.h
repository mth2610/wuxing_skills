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
 * Four bands are blended by overlapping smoothsteps over habitat:
 *   deep moss    -> lush clover : habitat 0.15 .. 0.42
 *   lush clover  -> sunlit gold : habitat 0.38 .. 0.72
 *   sunlit gold  -> dry crest   : habitat 0.68 .. 0.92
 *
 * Colour-space note: this file passes the values through unchanged. Whether they are read as
 * linear or display-referred is decided by the consuming shader, not here.
 */
#define MEADOW_PALETTE_DEEP_MOSS 0.12f, 0.24f, 0.08f
#define MEADOW_PALETTE_LUSH_CLOVER 0.18f, 0.34f, 0.11f
#define MEADOW_PALETTE_SUNLIT_GOLD 0.30f, 0.40f, 0.14f
#define MEADOW_PALETTE_DRY_CREST 0.44f, 0.42f, 0.18f
#define MEADOW_PALETTE_CLOVER_EDGE0 0.15f
#define MEADOW_PALETTE_CLOVER_EDGE1 0.42f
#define MEADOW_PALETTE_GOLD_EDGE0 0.38f
#define MEADOW_PALETTE_GOLD_EDGE1 0.72f
#define MEADOW_PALETTE_CREST_EDGE0 0.68f
#define MEADOW_PALETTE_CREST_EDGE1 0.92f

static inline float MeadowHabitat(float x, float z)
{
    float w1 = (sinf(x * 0.085f + z * 0.055f) * 0.5f + 0.5f);
    float w2 = (sinf(x * -0.065f + z * 0.12f + 1.8f) * 0.5f + 0.5f);
    float w3 = (sinf(x * 0.18f - z * 0.14f + 0.6f) * 0.5f + 0.5f);
    return w1 * 0.50f + w2 * 0.35f + w3 * 0.15f;
}

/* Same definition as GLSL smoothstep(): clamps to [0,1] first, so out-of-range (and NaN)
 * habitat resolves to the first / last band exactly like the shader does. */
static inline float MeadowSmoothstep(float edge0, float edge1, float x)
{
    float denom = edge1 - edge0;
    if (!(denom > 1e-6f))
        return (x >= edge1) ? 1.0f : 0.0f;
    float t = (x - edge0) / denom;
    /* !(t > 0.0f) catches both t <= 0.0f and IEEE-754 NaN without libm fminf/fmaxf overhead */
    t = (t > 0.0f) ? ((t < 1.0f) ? t : 1.0f) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline Vector3 MeadowCanopyColor(float habitat)
{
    const Vector3 deepMoss = {MEADOW_PALETTE_DEEP_MOSS};
    const Vector3 lushClover = {MEADOW_PALETTE_LUSH_CLOVER};
    const Vector3 sunlitGold = {MEADOW_PALETTE_SUNLIT_GOLD};
    const Vector3 dryCrest = {MEADOW_PALETTE_DRY_CREST};

    float s0 = MeadowSmoothstep(MEADOW_PALETTE_CLOVER_EDGE0, MEADOW_PALETTE_CLOVER_EDGE1, habitat);
    float s1 = MeadowSmoothstep(MEADOW_PALETTE_GOLD_EDGE0, MEADOW_PALETTE_GOLD_EDGE1, habitat);
    float s2 = MeadowSmoothstep(MEADOW_PALETTE_CREST_EDGE0, MEADOW_PALETTE_CREST_EDGE1, habitat);

    Vector3 c0 = {
        deepMoss.x + (lushClover.x - deepMoss.x) * s0,
        deepMoss.y + (lushClover.y - deepMoss.y) * s0,
        deepMoss.z + (lushClover.z - deepMoss.z) * s0,
    };
    Vector3 c1 = {
        c0.x + (sunlitGold.x - c0.x) * s1,
        c0.y + (sunlitGold.y - c0.y) * s1,
        c0.z + (sunlitGold.z - c0.z) * s1,
    };
    return (Vector3){
        c1.x + (dryCrest.x - c1.x) * s2,
        c1.y + (dryCrest.y - c1.y) * s2,
        c1.z + (dryCrest.z - c1.z) * s2,
    };
}
#endif
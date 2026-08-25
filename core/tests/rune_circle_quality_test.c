/* Contract for VFX_ComposeRuneCircle.
 *
 * WHAT THIS FILE USED TO BE, AND WHY THAT WAS WRONG. Until 24/08/2026 it held
 * seventeen assertions that matched literal substrings of the composition —
 * including `"static const float haloA[2] = { 0.18f, 0.70f }"` and
 * `"#define RUNE_MAX_RINGS   4"`, whitespace and all. It measured no pixels. It
 * ran on every suite sweep (run_core_tests.sh globs core/tests/*_test.c), and
 * the only thing it could actually do was fail the moment anybody improved the
 * effect it was named after — which is what happened: every one of those lines
 * described a composition that scripts/render_vfx_matrix.sh reported as failing
 * (cover% halving from dark to white, over half its "structure" being the
 * background showing through the gaps, darken% 0.0 on both backgrounds the game
 * actually has).
 *
 * A test that pins tuning constants does not protect a look; it protects a
 * revision. So this file now asserts only two kinds of thing:
 *   - arithmetic it can genuinely evaluate here, and
 *   - the PRESENCE of mechanisms whose absence caused a measured defect, and
 *     the ABSENCE of the primitive whose measured defect is why they exist.
 * No constant in the shader or the composition is pinned. Tune freely; the
 * image harness is what judges the look.
 *
 * ONE MORE RULE, LEARNED THE SAME DAY, THREE TIMES. A NEGATIVE substring check
 * ("this file must not mention X") cannot tell a call from the comment that
 * explains why the call is gone — and a rewrite worth doing always leaves that
 * comment behind. Three of the assertions below started life negative and every
 * one of them failed on prose. Only one negative survives, `DrawRibbonStripEx(`
 * with its parenthesis, and it is a call signature rather than a word. Prefer
 * asserting that the RIGHT mechanism is present over asserting that the wrong
 * one is absent.
 *
 * The image half of the contract is not testable headlessly and lives in
 * scripts/render_vfx_matrix.sh "RUNE CIRCLE". At the 24/08 rewrite, warmup 90:
 *     cover%   7.82 dark / 6.64 white   (was 1.24 / 0.71 — a 46% collapse)
 *     absvar   61.9 dark / 18.3 white   (was 26.4 / 13.4)
 *     detail   0.373 dark               (was 0.636 on 1.5% coverage, i.e. a
 *                                        wireframe's worth of pixels)
 *     darken%  56.6 white               (was 93.4 on 0.71% coverage — the old
 *                                        effect scored there by being a pencil
 *                                        drawing with no glow at all)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } Rgb;

static int g_failures = 0;

static void Check(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); g_failures++; }
}

static Rgb Additive(Rgb dst, Rgb src, float alpha)
{
    return (Rgb){dst.r + src.r * alpha,
                 dst.g + src.g * alpha,
                 dst.b + src.b * alpha};
}

static Rgb AlphaOver(Rgb dst, Rgb src, float alpha)
{
    return (Rgb){dst.r * (1.0f - alpha) + src.r * alpha,
                 dst.g * (1.0f - alpha) + src.g * alpha,
                 dst.b * (1.0f - alpha) + src.b * alpha};
}

static float Chroma(Rgb c)
{
    float hi = fmaxf(c.r, fmaxf(c.g, c.b));
    float lo = fminf(c.r, fminf(c.g, c.b));
    return hi > 0.0f ? (hi - lo) / hi : 0.0f;
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *text = (char *)malloc((size_t)size + 1u);
    if (text == NULL) { fclose(f); return NULL; }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    return text;
}

static int Has(const char *text, const char *needle)
{
    return text != NULL && strstr(text, needle) != NULL;
}

/* ── 1. Why the pigment has to live in an alpha pass ─────────────────────── */
static void Test_AlphaBodyPreservesChroma(void)
{
    const Rgb brightStone = {0.62f, 0.67f, 0.72f};
    const Rgb warmRune = {1.00f, 0.28f, 0.06f};

    Rgb additiveOnly = brightStone;
    additiveOnly = Additive(additiveOnly, warmRune, 0.32f);
    additiveOnly = Additive(additiveOnly, warmRune, 0.92f);
    additiveOnly = Additive(additiveOnly, warmRune, 1.00f);

    Rgb split = AlphaOver(brightStone, warmRune, 0.90f);
    split = Additive(split, warmRune, 0.18f);
    split = Additive(split, warmRune, 0.72f);

    Check(Chroma(split) >= Chroma(additiveOnly) + 0.12f,
          "an alpha body materially preserves rune chroma over a tall additive "
          "stack — this is why the shader keeps a separate BODY pass");
}

/* ── 2. The Band() contract, evaluated rather than matched ────────────────
 *
 * This is the property the whole move away from ribbon geometry was for. A
 * rasterised triangle narrower than a pixel is hit or missed; an analytic band
 * must instead widen to the pixel and dim by exactly the ratio it widened by,
 * so the light it puts on screen does not depend on how thin it got. Mirrors
 * Band() in core/shaders/rune_circle.fs. */
static float BandPeak(float halfWidth, float pixel)
{
    float fw = fmaxf(pixel * 0.5f, 1e-6f);
    float w = fmaxf(halfWidth, fw);
    return halfWidth / w;
}

static void Test_ThinBandsConserveLight(void)
{
    const float px = 0.010f;              /* one pixel, in the band's units */

    /* Well above a pixel: full amplitude, real width. */
    Check(fabsf(BandPeak(0.050f, px) - 1.0f) < 1e-5f,
          "a band wider than a pixel is drawn at full amplitude");

    /* Far below a pixel: amplitude must fall in proportion, or a hair-thin ring
     * renders as bright as a thick one and aliases into dashes as it moves. */
    float thin = BandPeak(0.0005f, px);
    Check(thin > 0.0f && thin < 0.15f,
          "a band far below a pixel dims instead of staying full-bright");

    /* Total emitted light is flat across the clamp: peak * effective width. */
    float wideLight = BandPeak(0.050f, px) * 0.050f;
    float thinLight = BandPeak(0.0005f, px) * fmaxf(0.0005f, px * 0.5f);
    Check(fabsf(thinLight - 0.0005f) < 1e-6f && wideLight > thinLight,
          "the clamp conserves the band's total light rather than inventing it");
}

/* ── 3. The one-sided-skirt trap, evaluated rather than grepped ───────────
 *
 * The first shader draft built its halo out of a gaussian on a clamped
 * distance. That reads as "a skirt that starts at k and fades outward", and it
 * is not: on the clamped side the exponent is exactly zero, so the term is 1.0
 * everywhere over there — a flat wash with no shape at all. It painted the
 * whole quad, corners included, and looked like a solid orange square in the
 * capture. A two-sided bell is the correct shape and the difference is total,
 * not marginal, which is why it is worth pinning as arithmetic instead of as a
 * string somebody can defeat by renaming a variable. */
static float OneSidedSkirt(float r, float k, float w)
{
    float x = fmaxf(r - k, 0.0f) / w;
    return expf(-x * x);
}

static float TwoSidedBell(float r, float k, float w)
{
    float x = (r - k) / w;
    return expf(-x * x);
}

static void Test_OneSidedSkirtHasNoFalloff(void)
{
    Check(OneSidedSkirt(0.20f, 1.0f, 0.20f) > 0.99f &&
          OneSidedSkirt(0.90f, 1.0f, 0.20f) > 0.99f,
          "a gaussian on a clamped distance is 1.0 across the ENTIRE clamped "
          "side — it is a flood, not a skirt");
    Check(TwoSidedBell(0.20f, 1.0f, 0.20f) < 0.001f,
          "...whereas the two-sided bell the shader uses actually falls off "
          "there, which is what keeps the quad's corners empty");
}

/* ── 4. Mechanisms whose absence was a measured defect ─────────────────── */
static void Test_MechanismsArePresent(void)
{
    char *inl = ReadFile("core/composition/common/vc_rune_circle.inl");
    char *fs = ReadFile("core/shaders/rune_circle.fs");
    char *vs = ReadFile("core/shaders/rune_circle.vs");

    Check(inl != NULL, "the composition exists");
    Check(fs != NULL && vs != NULL, "the rune circle shader pair exists");

    /* The primitive that made every stroke sub-pixel and cost 28 batch flushes
     * per frame. Its absence is the point of the rewrite. */
    /* The call, not the word: the composition's own header explains what it
     * replaced, and a substring test that cannot tell a comment from a call is
     * exactly the failure mode this file was rewritten to stop repeating. */
    Check(!Has(inl, "DrawRibbonStripEx("),
          "the rune circle no longer traces analytic circles with ribbon "
          "geometry (28 draws, 1-3 px strokes, dashes when it turns)");

    /* Both passes. The alpha body is the only reason this effect stayed visible
     * on a white plate when the other two disc-shaped composers vanished. */
    Check(Has(inl, "VFX_RENDER_PASS_BODY") && Has(inl, "VFX_SURFACE_ALPHA"),
          "a colour-preserving BODY pass survives the move to a shader");
    Check(Has(inl, "VFX_RENDER_PASS_EMISSION"),
          "emission is still a separate pass");

    /* Analytic antialiasing, and its energy clamp. */
    Check(Has(fs, "vec3 Band3(") && Has(fs, "fwidth"),
          "coverage is filtered against its own screen-space footprint");

    /* Bright core, coloured rim: three terms off one distance. A stroke drawn
     * as coverage alone is a coloured line however bright it is made. */
    Check(Has(fs, "aura") && Has(fs, "coreness"),
          "strokes carry a centreline core and a wide aura, not just coverage");

    /* Erosion, and the two things that make it read rather than just exist. */
    Check(Has(fs, "biteLine") && Has(fs, "biteMark"),
          "the ink is eroded, and lines and marks are eroded differently");
    Check(Has(fs, "PolarFbm"),
          "polar noise walks a circle in the noise domain — fbm over a raw "
          "atan() angle seams at the branch cut");
    Check(Has(fs, "sin(u_flow"),
          "noise domains are driven by sin() of a wrapped phase, not the "
          "wrapped phase itself, which steps a whole period when it wraps");

    /* The coverage-only pigment plate: free on black, decisive on white. */
    Check(Has(fs, "float plate ="),
          "a pigment plate gives the inscription something to sit on when the "
          "scenery is bright (darken% 2.4 -> 56.6 on the white plate)");

    /* The interior. cover% of 1.5 with no fill is why the old ring could be
     * bright and still not look like a light. */
    Check(Has(fs, "halo") && Has(fs, "veil") && Has(fs, "float core"),
          "the effect has area — skirt, interior and core — for bloom to read");

    /* Travelling charge, rather than an even brightness wobble. */
    Check(Has(fs, "Sweep(") && Has(fs, "u_pulse"),
          "energy travels through the construction instead of modulating it "
          "evenly all the way round");

    /* Phase folding. u_time reaches four digits in a match and fract() cannot
     * recover precision lost before it. */
    Check(Has(inl, "Rune_WrapTau") && Has(inl, "Rune_Wrap01"),
          "rotating phases are folded into their period on the CPU");

    /* The two bugs this rewrite actually shipped and then fixed. Both were
     * invisible in code review and obvious in a capture. */
    Check(Has(fs, "float rim ="),
          "nothing may survive to the quad's edge — without this guard the "
          "quad's own corners paint");

    /* Ground conforming, and the two things that make it safe. */
    Check(Has(inl, "MapManager_SampleGroundSurfaceAt"),
          "conforming is gated on the sampler whose BOOL distinguishes \"no "
          "height data\" from \"height is 0.0\" — GetGroundHeightAt cannot");
    Check(Has(inl, "RUNE_Y_LIFT"),
          "the surface is lifted clear of its receiver, or it z-fights it");
    Check(Has(inl, "RUNE_MESH_CELLS") && Has(inl, "RUNE_H_SAMPLES"),
          "the surface is tessellated and draped over the receiver rather than "
          "being one flat quad the terrain cuts through");

    free(inl);
    free(fs);
    free(vs);
}

int main(void)
{
    printf("=== rune circle: chroma split, analytic coverage, energy ===\n");
    Test_AlphaBodyPreservesChroma();
    Test_ThinBandsConserveLight();
    Test_OneSidedSkirtHasNoFalloff();
    Test_MechanismsArePresent();
    if (g_failures == 0)
        puts("PASS: rune circle contract holds");
    printf("---- %d failures\n", g_failures);
    return g_failures != 0;
}

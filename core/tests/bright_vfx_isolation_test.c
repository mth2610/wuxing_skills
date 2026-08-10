/*
 * Isolated bright-background VFX probe.  No raylib, window, assets, map, or
 * Vulkan: this is the exact fixed-function blend arithmetic used by both GL
 * and rlvk.  It answers which layer FIRST removes colour from one emitter.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { float r, g, b; } Rgb;

static Rgb add(Rgb a, Rgb b) { return (Rgb){a.r + b.r, a.g + b.g, a.b + b.b}; }
static Rgb scale(Rgb c, float a) { return (Rgb){c.r*a, c.g*a, c.b*a}; }
static float max3(Rgb c) { return fmaxf(c.r, fmaxf(c.g, c.b)); }
static float min3(Rgb c) { return fminf(c.r, fminf(c.g, c.b)); }
static float chroma(Rgb c) { float hi = max3(c); return hi > 0.0f ? (hi - min3(c))/hi : 0.0f; }
static float aces1(float x)
{
    float y = (x*(2.51f*x + 0.03f))/(x*(2.43f*x + 0.59f) + 0.14f);
    return fmaxf(0.0f, fminf(1.0f, y));
}
static Rgb aces(Rgb c) { return (Rgb){aces1(c.r), aces1(c.g), aces1(c.b)}; }

/* Raylib BLEND_ADDITIVE: out = source*sourceAlpha + destination. */
static Rgb additive(Rgb dst, Rgb src, float srcAlpha)
{
    return add(dst, scale(src, srcAlpha));
}
static Rgb layered(Rgb scene, Rgb body, float bodyAlpha, Rgb emission)
{
    float coverage = bodyAlpha;
    return add(add(scale(scene, 1.0f - coverage), scale(body, coverage)), emission);
}

static void report(const char *stage, Rgb c)
{
    printf("%-30s rgb=(%.3f %.3f %.3f) chroma=%.3f\n",
           stage, c.r, c.g, c.b, chroma(c));
}

int main(void)
{
    /* One warm emissive particle; shader output before blend. */
    const Rgb vfx = {1.0f, 0.35f, 0.05f};
    const float alpha = 0.55f;
    const Rgb darkClear = {0.01f, 0.01f, 0.02f};
    const Rgb brightClear = {0.38f, 0.39f, 0.55f};
    const Rgb brightMap = {0.18f, 0.49f, 0.08f};

    Rgb dark = additive(darkClear, vfx, alpha);
    Rgb bright = additive(brightClear, vfx, alpha);
    Rgb mapped = additive(brightMap, vfx, alpha);
    /* New render graph: a coloured alpha body is stored separately, while the
       small halo remains additive in its own buffer. */
    Rgb layerDark = layered(darkClear, vfx, alpha, scale(vfx, 0.12f));
    Rgb layerBright = layered(brightClear, vfx, alpha, scale(vfx, 0.12f));

    report("0 dark clear + VFX (no post)", dark);
    report("1 bright clear + VFX (no post)", bright);
    report("2 bright map + VFX (no post)", mapped);
    report("3 dark clear + VFX + ACES", aces(dark));
    report("4 bright clear + VFX + ACES", aces(bright));
    report("5 bright map + VFX + ACES", aces(mapped));
    report("6 layered dark + ACES", aces(layerDark));
    report("7 layered bright + ACES", aces(layerBright));

    /* The loss must already exist before post-processing. */
    if (chroma(bright) >= chroma(dark)) {
        fprintf(stderr, "FAIL: bright clear did not reproduce pre-post washout\n");
        return 1;
    }
    /* Map changes the inherited destination colour; it is not required to fail. */
    if (fabsf(chroma(bright) - chroma(mapped)) < 0.02f) {
        fprintf(stderr, "FAIL: map background did not affect additive result\n");
        return 1;
    }
    if (chroma(aces(layerBright)) < 0.40f ||
        chroma(aces(layerBright)) < chroma(aces(bright)) + 0.20f) {
        fprintf(stderr, "FAIL: semantic body/emission split did not improve bright-background chroma\n");
        return 1;
    }
    Rgb softBody = layered(brightClear, vfx, 0.10f, (Rgb){0.0f, 0.0f, 0.0f});
    Rgb authoredSoft = add(scale(brightClear, 0.90f), scale(vfx, 0.10f));
    if (fabsf(softBody.r - authoredSoft.r) > 0.0001f ||
        fabsf(softBody.g - authoredSoft.g) > 0.0001f ||
        fabsf(softBody.b - authoredSoft.b) > 0.0001f) {
        fprintf(stderr, "FAIL: compositor changed authored soft-edge coverage\n");
        return 1;
    }
    puts("PASS: washout first appears at additive composition, before post-processing");

    /* ── An EMISSION WEIGHT reused as body COVERAGE (10/08/2026) ─────────────
     *
     * VFX_RIBBON_MAIN's layers are 0.10 / 0.36 / 0.30 — they SUM as light, so
     * they are emission weights, not coverage. The classic layered ribbon path
     * fed them straight into the BLEND_ALPHA body pass, capping body coverage at
     * 0.36: `scene*(1-0.36)` keeps 64% of the destination, so over anything
     * bright the trail cannot hold its own hue no matter what colour it writes.
     * `material.bodyOpacity` is the separately-authored coverage that fixes it.
     * Measured in the app on a bright clear (peak chroma): 0.31 at bodyOpacity
     * 0, 0.40 at 0.55, 0.72 at 1.0 — against 0.61 over the night sky. */
    {
        const float emissionWeight = 0.36f;   /* k_sweptLayers MAIN, layer 1 */
        const float authoredBody   = 1.00f;   /* s_sweptBodyOpacity */
        Rgb asWeight = layered(brightClear, vfx, emissionWeight, scale(vfx, 0.12f));
        Rgb asCoverage = layered(brightClear, vfx, authoredBody, scale(vfx, 0.12f));
        if (chroma(asWeight) >= chroma(asCoverage)) {
            fprintf(stderr, "FAIL: authored body coverage did not beat the emission weight\n");
            return 1;
        }
        /* And the gap must be worth the change, not a rounding win. */
        if (chroma(asCoverage) - chroma(asWeight) < 0.20f) {
            fprintf(stderr, "FAIL: body coverage barely moved chroma (%.3f -> %.3f)\n",
                    chroma(asWeight), chroma(asCoverage));
            return 1;
        }
        /* The renderer must actually route both layered paths through the one
         * helper — a C mirror of a policy that the renderer no longer applies is
         * fiction (core/CLAUDE.md §3). */
        FILE *f = fopen("core/trails/trail_system.c", "rb");
        static char src[400000];
        if (f == NULL) {
            fprintf(stderr, "FAIL: cannot read core/trails/trail_system.c\n");
            return 1;
        }
        size_t n = fread(src, 1, sizeof(src) - 1, f);
        src[n] = 0;
        fclose(f);
        if (strstr(src, "static float TrailLayerPassAlphaMul(") == NULL) {
            fprintf(stderr, "FAIL: the pass-aware layer alpha helper is gone\n");
            return 1;
        }
        if (strstr(src, "float aMul = TrailLayerPassAlphaMul(t, ly);") == NULL) {
            fprintf(stderr, "FAIL: a layered path stopped asking the helper for its alpha\n");
            return 1;
        }
        if (strstr(src, "ly->whiten > 0.0f && TrailLayerWhitensThisPass(t)") == NULL) {
            fprintf(stderr, "FAIL: whitening is no longer gated out of the body pass\n");
            return 1;
        }
        puts("PASS: body coverage is authored, not an emission weight reused as alpha");
        puts("PASS: and whitening stays out of the pass that has to carry hue");
    }
    return 0;
}

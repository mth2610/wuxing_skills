/*
 * Isolated bright-background VFX probe.  No raylib, window, assets, map, or
 * Vulkan: this is the exact fixed-function blend arithmetic used by both GL
 * and rlvk.  It answers which layer FIRST removes colour from one emitter.
 */
#include <math.h>
#include <stdio.h>

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
    float coverage = 1.0f - powf(1.0f - bodyAlpha, 6.0f);
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
    if (chroma(aces(layerBright)) < 0.55f ||
        fabsf(chroma(aces(layerBright)) - chroma(aces(layerDark))) > 0.22f) {
        fprintf(stderr, "FAIL: separated VFX layers did not retain bright-background chroma\n");
        return 1;
    }
    puts("PASS: washout first appears at additive composition, before post-processing");
    return 0;
}

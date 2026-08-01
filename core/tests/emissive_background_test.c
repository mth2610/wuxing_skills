/*
 * Reproduces the bright-background washout with FlameVolume's real default
 * body law: additive sprites, alpha 0.18, and a warm black-body sample.
 * This is deliberately renderer-free: it tests the blend/tone-map arithmetic
 * that every graphics backend must implement identically.
 */
#include <math.h>
#include <stdio.h>

typedef struct { float r, g, b; } Rgb;

static Rgb Add(Rgb a, Rgb b) { return (Rgb){a.r + b.r, a.g + b.g, a.b + b.b}; }
static Rgb Scale(Rgb a, float s) { return (Rgb){a.r*s, a.g*s, a.b*s}; }
static float Max3(Rgb c) { return fmaxf(c.r, fmaxf(c.g, c.b)); }
static float Min3(Rgb c) { return fminf(c.r, fminf(c.g, c.b)); }
static float Chroma(Rgb c) { float m = Max3(c); return m > 0.0f ? (m - Min3(c))/m : 0.0f; }

static float Aces1(float x)
{
    float y = (x*(2.51f*x + 0.03f))/(x*(2.43f*x + 0.59f) + 0.14f);
    return fmaxf(0.0f, fminf(1.0f, y));
}
static Rgb Aces(Rgb c) { return (Rgb){Aces1(c.r), Aces1(c.g), Aces1(c.b)}; }
static Rgb Over(Rgb src, float alpha, Rgb dst)
{
    return Add(Scale(src, alpha), Scale(dst, 1.0f - alpha));
}

int main(void)
{
    /* Flame body: s_fvolBodyBlend=1, s_fvolBodyAlpha=0.18, ramp near t=0.28. */
    const Rgb body = {248.0f/255.0f, 140.0f/255.0f, 40.0f/255.0f};
    const float alpha = 0.18f;
    const int overlappingSprites = 8;
    Rgb dark = {0.01f, 0.01f, 0.02f};
    Rgb bright = {0.38f, 0.39f, 0.55f};
    Rgb alphaDark = dark, alphaBright = bright;

    for (int i = 0; i < overlappingSprites; ++i) {
        dark = Add(dark, Scale(body, alpha));
        bright = Add(bright, Scale(body, alpha));
        alphaDark = Over(body, alpha, alphaDark);
        alphaBright = Over(body, alpha, alphaBright);
    }
    dark = Aces(dark);
    bright = Aces(bright);
    alphaDark = Aces(alphaDark);
    alphaBright = Aces(alphaBright);
    printf("fire additive after ACES: dark=(%.3f %.3f %.3f) chroma=%.3f; "
           "bright=(%.3f %.3f %.3f) chroma=%.3f\n",
           dark.r, dark.g, dark.b, Chroma(dark),
           bright.r, bright.g, bright.b, Chroma(bright));

    /* The bright backdrop is retained by additive blending before ACES. */
    if (bright.b <= dark.b || Chroma(bright) >= Chroma(dark)) {
        fprintf(stderr, "FAIL: did not reproduce bright-background washout\n");
        return 1;
    }
    printf("fire alpha body after ACES: dark chroma=%.3f; bright chroma=%.3f\n",
           Chroma(alphaDark), Chroma(alphaBright));
    if (Chroma(alphaBright) < 0.50f) {
        fprintf(stderr, "FAIL: alpha body did not retain warm contrast on bright background\n");
        return 1;
    }
    puts("PASS: additive body reproduces the bright-background washout");
    return 0;
}

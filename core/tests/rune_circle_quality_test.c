/* Bright-background and hierarchy contract for VFX_ComposeRuneCircle.
 *
 * The numeric half mirrors fixed-function blending only; it cannot validate
 * texture filtering, bloom radius, camera foreshortening, or motion.  Source
 * assertions below pin the production split so the mirror cannot drift away
 * from the renderer that it is intended to protect. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } Rgb;

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

int main(void)
{
    int failed = 0;
    const Rgb brightStone = {0.62f, 0.67f, 0.72f};
    const Rgb warmRune = {1.00f, 0.28f, 0.06f};

    /* Old rune: three additive submissions.  New rune: compact colour-bearing
     * coverage, then one restrained halo. */
    Rgb additiveOnly = brightStone;
    additiveOnly = Additive(additiveOnly, warmRune, 0.22f);
    additiveOnly = Additive(additiveOnly, warmRune, 0.85f);
    additiveOnly = Additive(additiveOnly, warmRune, 1.00f);
    Rgb split = AlphaOver(brightStone, warmRune, 0.86f);
    split = Additive(split, warmRune, 0.20f);

    if (Chroma(split) < Chroma(additiveOnly) + 0.12f) {
        fprintf(stderr, "FAIL: alpha core does not materially preserve rune chroma\n");
        failed++;
    }

    char *source = ReadFile("core/composition/common/vc_rune_circle.inl");
    if (!Has(source, "VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false")) {
        fprintf(stderr, "FAIL: rune circle has no colour-preserving BODY core\n");
        failed++;
    }
    if (!Has(source, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false")) {
        fprintf(stderr, "FAIL: rune circle has no separate additive halo\n");
        failed++;
    }
    if (!Has(source, "Rune_ArcCharacter(")) {
        fprintf(stderr, "FAIL: rune marks have no per-arc intensity variation\n");
        failed++;
    }
    if (!Has(source, "VFX_CONTRAST_MAGIC")) {
        fprintf(stderr, "FAIL: rune circle does not opt into shared magic contrast\n");
        failed++;
    }
    if (!Has(source, "Color keylineCol = ColorLerp(")) {
        fprintf(stderr, "FAIL: bright-ground rune has no dark precision keyline\n");
        failed++;
    }
    if (!Has(source, "static const float bodyW[2][2]")) {
        fprintf(stderr, "FAIL: BODY does not separate keyline from saturated inscription\n");
        failed++;
    }
    if (!Has(source, "bool glyphPass = isGlyph;")) {
        fprintf(stderr, "FAIL: glyph halo still expands into a blurry solid ring\n");
        failed++;
    }
    if (!Has(source, "Rune_DrawAnchors(")) {
        fprintf(stderr, "FAIL: rune composition lacks structural anchor marks\n");
        failed++;
    }
    if (!Has(source, "Color inscriptionCol = m->glow;")) {
        fprintf(stderr, "FAIL: rune inscription does not preserve the material hue ramp\n");
        failed++;
    }
    if (Has(source, "VC_Whiten(m->glow")) {
        fprintf(stderr, "FAIL: rune palette is still being pushed toward pale white\n");
        failed++;
    }

    free(source);
    if (failed == 0)
        puts("PASS: rune circle preserves bright-background colour and authored hierarchy");
    return failed != 0;
}

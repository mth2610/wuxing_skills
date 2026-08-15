/* Bright-background, particle-free focal-geometry contract for VFX_ComposeRuneCircle.
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

    /* A bright destination cannot hold hue through a tall additive stack.  The
     * rune must put its pigment in the alpha body, then spend only a small
     * additive budget on one halo. */
    Rgb additiveOnly = brightStone;
    additiveOnly = Additive(additiveOnly, warmRune, 0.32f);
    additiveOnly = Additive(additiveOnly, warmRune, 0.92f);
    additiveOnly = Additive(additiveOnly, warmRune, 1.00f);
    Rgb split = AlphaOver(brightStone, warmRune, 0.90f);
    split = Additive(split, warmRune, 0.18f);
    split = Additive(split, warmRune, 0.72f);

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
    if (!Has(source, "#define RUNE_MAX_RINGS   4")) {
        fprintf(stderr, "FAIL: rune no longer has the reference four-ring hierarchy\n");
        failed++;
    }
    if (!Has(source, "Rune_ArcCharacter(")) {
        fprintf(stderr, "FAIL: rune marks have no per-arc intensity variation\n");
        failed++;
    }
    if (!Has(source, "RUNE_GLYPH_SHEETS") || !Has(source, "s_runeGlyph")) {
        fprintf(stderr, "FAIL: reference glyph-sheet ring system is missing\n");
        failed++;
    }
    if (!Has(source, "VFX_CONTRAST_MAGIC")) {
        fprintf(stderr, "FAIL: rune circle does not opt into shared magic contrast\n");
        failed++;
    }
    if (!Has(source, "Color bodyInk = VFXContrast_ApplyColor(m->body,")) {
        fprintf(stderr, "FAIL: rune body is not sourced from the material colour\n");
        failed++;
    }
    if (Has(source, "(Color){10, 10, 15, 255}")) {
        fprintf(stderr, "FAIL: rune still paints a black keyline on bright ground\n");
        failed++;
    }
    if (!Has(source, "static const float bodyW[2][2]")) {
        fprintf(stderr, "FAIL: BODY does not separate keyline from saturated inscription\n");
        failed++;
    }
    if (!Has(source, "static const float haloW[2]")) {
        fprintf(stderr, "FAIL: rune has no separate halo and luminous core\n");
        failed++;
    }
    if (!Has(source, "static const float haloA[2] = { 0.18f, 0.70f }")) {
        fprintf(stderr, "FAIL: rune glow does not have a vivid luminous core\n");
        failed++;
    }
    if (!Has(source, "Color emissionCol = VFXContrast_ApplyColor(m->glow,")) {
        fprintf(stderr, "FAIL: rune glow is not using the material emission contrast\n");
        failed++;
    }
    if (Has(source, "Rune_DrawMotes(")) {
        fprintf(stderr, "FAIL: rune circle still draws particle-like motes\n");
        failed++;
    }
    if (Has(source, "Rune_DrawAnchors(") || Has(source, "Rune_DrawSpokes(")) {
        fprintf(stderr, "FAIL: rune composition contains stray radial geometry\n");
        failed++;
    }
    if (!Has(source, "Rune_DrawFocusGlyph(") || !Has(source, ", 4,")) {
        fprintf(stderr, "FAIL: rune lost its concentric square focal glyph\n");
        failed++;
    }
    if (!Has(source, "Color inscriptionCol = m->glow;")) {
        fprintf(stderr, "FAIL: rune inscription does not preserve the material hue ramp\n");
        failed++;
    }
    if (Has(source, "ColorLerp(inscriptionCol, WHITE")) {
        fprintf(stderr, "FAIL: rune palette is still being pushed toward pale white\n");
        failed++;
    }

    free(source);
    if (failed == 0)
        puts("PASS: rune circle preserves chroma, focal glyphs, and particle-free silhouette");
    return failed != 0;
}

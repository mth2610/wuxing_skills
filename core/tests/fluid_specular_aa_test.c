// core headless test — specular antialiasing by normal filtering
// (core/fluid/shaders/fluid_surface.fs).
//
// The complaint: hard bright dashes on the water ring's tube. Isolating the
// composite's additive terms in the sandbox settled which one it was — rimLight
// and the Fresnel-weighted reflection were both ~black, foam was a faint fringe,
// and the SUN SPECULAR carried the whole thing: a razor needle from the GGX lobe
// plus a soft cigar from the `sharpGlint` Blinn lobe. Fresnel was not involved,
// and neither was the back surface, which carries no shading at all.
//
// The fix is Kaplanyan et al. 2016 (Filament's `normalFiltering`): widen the lobe
// by the screen-space variance of the normal, because that variance IS the detail
// the surface cannot resolve. This mirrors that arithmetic and, above all, the
// energy correction the first attempt got wrong.
//
// It cannot see the dashes. That took the sandbox (NEW FX tab, WATER RING) and a
// debug view splitting the additive terms — and note that consecutive runs of the
// same fixture are NOT identical, so silhouette-scale detail must be compared
// across several runs before it is attributed to anything.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#define FLUID_SPECULAR_AA_VARIANCE 0.15f
#define FLUID_SPECULAR_AA_THRESHOLD 0.25f

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* Mirror of NormalFilteredRoughness(). `normalGradient2` stands in for
 * dot(dNdx,dNdx) + dot(dNdy,dNdy) — how fast the normal turns per pixel. */
static float FilteredRoughness(float perceptualRoughness, float normalGradient2)
{
    float variance = FLUID_SPECULAR_AA_VARIANCE * normalGradient2;
    float kernelRoughness = 2.0f * variance;
    if (kernelRoughness > FLUID_SPECULAR_AA_THRESHOLD) kernelRoughness = FLUID_SPECULAR_AA_THRESHOLD;
    float alpha = perceptualRoughness * perceptualRoughness;
    float squareAlpha = Clamp01(alpha * alpha + kernelRoughness);
    return sqrtf(sqrtf(squareAlpha));
}

/* Mirror of BlinnExponentScale(). */
static float BlinnExponentScale(float perceptualRoughness, float filteredRoughness)
{
    float alpha = perceptualRoughness * perceptualRoughness;
    float filteredAlpha = filteredRoughness * filteredRoughness;
    float s = (alpha * alpha) / fmaxf(filteredAlpha * filteredAlpha, 1e-8f);
    if (s < 0.02f) s = 0.02f;
    if (s > 1.0f) s = 1.0f;
    return s;
}

/* A pow(cos, n) lobe's solid angle goes as 1/(n+2), so its integrated energy is
 * amplitude/(n+2). The shader multiplies the amplitude by (n+2)/(n0+2) exactly to
 * hold that constant. */
static float LobeEnergy(float amplitude, float exponent) { return amplitude / (exponent + 2.0f); }
static float WidenedAmplitude(float amplitude, float exponent, float baseExponent)
{ return amplitude * ((exponent + 2.0f) / (baseExponent + 2.0f)); }

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return NULL; }
    size_t got = fread(text, 1, (size_t)n, f);
    text[got] = '\0'; fclose(f); return text;
}

int main(void)
{
    int bad = 0;
    const float authored = 0.090f;   /* the smooth end of mix(0.090, 0.150, noise) */

    /* ---- A perfectly smooth surface must be left alone. Antialiasing that
     * blurred a highlight where there is nothing to alias would just be a
     * roughness change with extra steps. */
    {
        float r = FilteredRoughness(authored, 0.0f);
        CHECK(fabsf(r - authored) < 1e-5f);
        CHECK(fabsf(BlinnExponentScale(authored, r) - 1.0f) < 1e-5f);
    }

    /* ---- Filtering only ever WIDENS, and grows with the normal's variance. */
    {
        float previous = 0.0f;
        for (float g2 = 0.0f; g2 <= 4.0f; g2 += 0.05f)
        {
            float r = FilteredRoughness(authored, g2);
            CHECK(r >= authored - 1e-6f);
            CHECK(r >= previous - 1e-6f);
            previous = r;
        }
        printf("      roughness %.3f -> %.3f at the variance ceiling\n",
               authored, FilteredRoughness(authored, 1.0e6f));
    }

    /* ---- The threshold caps it: an untrustworthy normal must not drive
     * roughness to 1 and delete the highlight entirely. */
    {
        float capped = FilteredRoughness(authored, 1.0e6f);
        float alpha = authored * authored;
        float expected = sqrtf(sqrtf(alpha * alpha + FLUID_SPECULAR_AA_THRESHOLD));
        CHECK(fabsf(capped - expected) < 1e-5f);
        CHECK(capped < 1.0f);
    }

    /* ---- THE ENERGY CORRECTION. This is the one the first attempt missed: the
     * GGX term is normalized and dims itself as it widens, but the two Blinn
     * lobes are not, so widening them at a fixed amplitude spread the same peak
     * over a larger solid angle and ADDED light. On screen the needle vanished
     * and a bigger, brighter comma took its place. */
    {
        const float baseExponent = 96.0f, baseAmplitude = 0.22f;   /* sharpGlint */
        float reference = LobeEnergy(baseAmplitude, baseExponent);
        for (float g2 = 0.0f; g2 <= 4.0f; g2 += 0.1f)
        {
            float r = FilteredRoughness(authored, g2);
            float scale = BlinnExponentScale(authored, r);
            float exponent = baseExponent * scale;
            float uncorrected = LobeEnergy(baseAmplitude, exponent);
            float corrected = LobeEnergy(WidenedAmplitude(baseAmplitude, exponent, baseExponent),
                                         exponent);
            CHECK(fabsf(corrected - reference) < 1e-6f);
            CHECK(uncorrected >= corrected - 1e-9f);   /* the defect always brightens */
        }
        float worstExponent = baseExponent * BlinnExponentScale(authored, FilteredRoughness(authored, 1.0e6f));
        printf("      sharpGlint exponent 96 -> %.1f; uncorrected that is %.1fx the energy\n",
               worstExponent, LobeEnergy(baseAmplitude, worstExponent) / reference);
    }

    /* ---- The exponent scale stays in (0, 1]: it may only widen a lobe, and the
     * floor stops a degenerate normal from flattening it to a constant. */
    for (float g2 = 0.0f; g2 <= 8.0f; g2 += 0.05f)
    {
        float s = BlinnExponentScale(authored, FilteredRoughness(authored, g2));
        CHECK(s > 0.0f && s <= 1.0f + 1e-6f);
    }

    /* ---- Anti-drift: the shader must still carry all three pieces. Losing the
     * amplitude correction is silent — it looks like "the highlight got softer",
     * which is what the fix is supposed to do. */
    {
        char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
        if (!shader) { printf("FAIL: cannot read fluid_surface.fs\n"); bad++; }
        else
        {
            CHECK(strstr(shader, "#define FLUID_SPECULAR_AA_VARIANCE 0.15") != NULL);
            CHECK(strstr(shader, "#define FLUID_SPECULAR_AA_THRESHOLD 0.25") != NULL);
            CHECK(strstr(shader, "dFdx(N)") != NULL && strstr(shader, "dFdy(N)") != NULL);
            CHECK(strstr(shader, "NormalFilteredRoughness(N, authoredRoughness)") != NULL);
            /* Every un-normalized lobe must carry its (n+2) amplitude term. */
            CHECK(strstr(shader, "((broadExponent + 2.0) / 50.0)") != NULL);
            CHECK(strstr(shader, "((glintExponent + 2.0) / 98.0)") != NULL);
            CHECK(strstr(shader, "((pointExponent + 2.0) / 46.0)") != NULL);
            free(shader);
        }
    }

    printf(bad ? "fluid_specular_aa: FAIL (%d)\n" : "fluid_specular_aa: PASS\n", bad);
    return bad ? 1 : 0;
}

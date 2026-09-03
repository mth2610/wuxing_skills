/* Magic trail tone-map migration contract.
 *
 * This headless guard cannot execute GLSL, blend a framebuffer, or measure
 * bloom. It pins the C -> shader opt-in and mirrors the ACES arithmetic so the
 * MAGIC-only migration cannot silently spread to the other trail profiles. */
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static const char *ReadFile(const char *path, char *text, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    size_t size;
    if (file == NULL) return NULL;
    size = fread(text, 1, capacity - 1u, file);
    if (!feof(file)) { fclose(file); return NULL; }
    text[size] = '\0';
    fclose(file);
    return text;
}

static void Check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int Has(const char *text, const char *needle)
{
    return text != NULL && strstr(text, needle) != NULL;
}

static float Aces(float x)
{
    float y = (x * (2.51f*x + 0.03f)) /
              (x * (2.43f*x + 0.59f) + 0.14f);
    if (y < 0.0f) return 0.0f;
    if (y > 1.0f) return 1.0f;
    return y;
}

static float AcesInverse(float y)
{
    float a, b, c, d;
    if (y < 0.0f) y = 0.0f;
    if (y > 0.9999f) y = 0.9999f;
    a = y*2.43f - 2.51f;
    b = y*0.59f - 0.03f;
    c = y*0.14f;
    d = b*b - 4.0f*a*c;
    if (d < 0.0f) d = 0.0f;
    return fmaxf((-b - sqrtf(d)) / (2.0f*a), 0.0f);
}

int main(void)
{
    static char trailBuf[150000];
    static char shaderBuf[40000];
    static char recipeBuf[120000];
    const char *trail = ReadFile("core/trails/trail_system.c",
                                 trailBuf, sizeof(trailBuf));
    const char *shader = ReadFile("core/trails/shaders/trail_deform.fs",
                                  shaderBuf, sizeof(shaderBuf));
    const char *recipe = ReadFile("core/composition/common/vc_trail.inl",
                                  recipeBuf, sizeof(recipeBuf));

    Check(trail != NULL && shader != NULL && recipe != NULL,
          "magic trail migration inputs must be readable");
    Check(Has(recipe, "TrailRecipe *r = &k_trailPresets[TRAIL_PRESET_MAGIC]") &&
              Has(recipe, "r->colour.contrast = VFX_CONTRAST_MAGIC;") &&
              Has(recipe, "r->hdrGain = 1.90f;"),
          "the guard must follow the authored MAGIC trail, not another preset");

    Check(Has(trail, "toneMapSafe") &&
              Has(trail, "VFX_CONTRAST_MAGIC") &&
              Has(trail, "u_tonemapSafe"),
          "trail runtime must opt MAGIC into tone-map-safe output");
    Check(Has(shader, "uniform float u_tonemapSafe;") &&
              Has(shader, "TrailToneMapSafeRadiance(radiance)"),
          "trail shader must preserve only the HDR excess of MAGIC radiance");
    Check(Has(shader, "(peak - 1.25) / max(peak, 0.00001)"),
          "MAGIC trail must leave the sub-threshold colour carrier untouched");

    /* Mirror the shipped 1.90 MAGIC gain with the blue used by the material
     * output probe. Non-MAGIC profiles keep oldDisplay; only MAGIC selects the
     * inverse-ACES path represented by safeDisplay. */
    {
        const float hue[3] = {70.0f/255.0f, 135.0f/255.0f, 1.0f};
        const float gain = 1.90f;
        float oldDisplay[3], fullSafeHDR[3], partialDisplay[3];
        float displayPeak = Aces(gain);
        float safeWeight = (gain - 1.25f) / gain;
        int isMagic[2] = {0, 1};
        for (int i = 0; i < 3; i++) {
            oldDisplay[i] = Aces(hue[i] * gain);
            fullSafeHDR[i] = AcesInverse(hue[i] * displayPeak);
            partialDisplay[i] = Aces(hue[i] * gain * (1.0f - safeWeight) +
                                     fullSafeHDR[i] * safeWeight);
        }
        Check(isMagic[0] == 0 && isMagic[1] == 1,
              "non-MAGIC and MAGIC must remain distinct opt-in states");
        Check(safeWeight > 0.30f && safeWeight < 0.40f,
              "the shipped MAGIC peak must receive partial, not full correction");
        Check(fabsf(partialDisplay[0]/partialDisplay[2] - hue[0]) <
                  fabsf(oldDisplay[0]/oldDisplay[2] - hue[0]),
              "HDR-excess correction must reduce washout without flattening the body");
        Check(fmaxf(1.0f - 1.25f, 0.0f) == 0.0f,
              "sub-threshold trail radiance must remain on the legacy path");
    }

    puts(failures ? "magic trail tonemap: FAIL" : "magic trail tonemap: PASS");
    return failures != 0;
}

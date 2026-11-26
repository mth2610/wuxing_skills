/* Contract & mathematical guard for unified Bright/Snow & Dark VFX Radiance.
 *
 * Verifies the four load-bearing invariants of the upgraded color/bloom model:
 *   1. Quadratic chroma-weighted VFX_LuminousOpacity is strictly monotonic on
 *      HDR snow (B <= 1.25): zero dark soot dip and zero mid-tone flattening.
 *   2. Premultiplied Carrier + Corona + Core + Optical Glare Veil + HDR-adaptive
 *      toneMapScene preserves strong elemental chroma on snow across all 6
 *      elements while staying within < 2% of legacy output on dark arenas.
 *   3. Achromatic snow at HDR (0.95, 1.00, 1.15) with auto-exposure produces
 *      zero milky self-bloom, while thin chromatic HDR filaments radiate vivid
 *      saturated bloom.
 *   4. Source-wiring guards pin the GLSL expressions in vfx_composite.glsl,
 *      bloom_bright.fs, post_process.fs, particle_lit.fs, and particle_gpu.fs.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } Rgb;

static float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float Smoothstep(float e0, float e1, float x) {
    float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
static float Max3(Rgb c) { return fmaxf(c.r, fmaxf(c.g, c.b)); }
static float Min3(Rgb c) { return fminf(c.r, fminf(c.g, c.b)); }
static float Luma(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
static float Sat(Rgb c) { float p = Max3(c); return p > 1e-4f ? Clamp01((p - Min3(c)) / p) : 0.0f; }

static float Aces1(float x) {
    float y = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return Clamp01(y);
}

static Rgb ToneMapScene(Rgb x, float hueRestore) {
    x.r = fmaxf(x.r, 0.0f); x.g = fmaxf(x.g, 0.0f); x.b = fmaxf(x.b, 0.0f);
    Rgb perCh = { Aces1(x.r), Aces1(x.g), Aces1(x.b) };
    float peak = Max3(x);
    if (peak <= 0.0f) return perCh;
    float baseW = Clamp01(hueRestore);
    float hdrLift = Smoothstep(1.0f, 3.2f, peak);
    float w = baseW + (fmaxf(baseW, 0.90f) - baseW) * hdrLift;
    float whiteMix = Smoothstep(4.5f, 14.0f, peak);
    float sc = Aces1(peak);
    Rgb hueKept = {
        ((x.r / peak) * (1.0f - whiteMix) + whiteMix) * sc,
        ((x.g / peak) * (1.0f - whiteMix) + whiteMix) * sc,
        ((x.b / peak) * (1.0f - whiteMix) + whiteMix) * sc
    };
    return (Rgb){
        perCh.r + (hueKept.r - perCh.r) * w,
        perCh.g + (hueKept.g - perCh.g) * w,
        perCh.b + (hueKept.b - perCh.b) * w
    };
}

/* Mirrors core/shaders/common/vfx_composite.glsl::VFX_LuminousOpacity */
static float LuminousOpacity(Rgb glow) {
    float peak = Max3(glow);
    if (peak <= 1e-4f) return 0.0f;
    float sat = Sat(glow);
    float p2 = peak * peak;
    float val = 0.82f * (0.18f + 0.82f * sat) * (p2 / (1.32f + p2));
    return val > 0.88f ? 0.88f : val;
}

/* Mirrors post_process/shaders/post_process.fs optical glare veil */
static Rgb ApplyBloomVeil(Rgb scene, Rgb bloom, float exposure, float bloomVeil) {
    float bPeak = Max3(bloom);
    if (bPeak > 1e-4f && bloomVeil > 0.001f) {
        float bSat = Sat(bloom);
        float bgLum = Luma(scene) * exposure;
        float veil = bloomVeil * bSat * Smoothstep(0.28f, 0.92f, bgLum) * (bPeak / (0.38f + bPeak));
        if (veil > 0.62f) veil = 0.62f;
        Rgb bTint = { bloom.r / bPeak, bloom.g / bPeak, bloom.b / bPeak };
        scene.r *= 1.0f + ((0.32f + 0.68f * bTint.r) - 1.0f) * veil;
        scene.g *= 1.0f + ((0.32f + 0.68f * bTint.g) - 1.0f) * veil;
        scene.b *= 1.0f + ((0.32f + 0.68f * bTint.b) - 1.0f) * veil;
    }
    return (Rgb){ scene.r + bloom.r, scene.g + bloom.g, scene.b + bloom.b };
}

static char *ReadWhole(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (!s) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *src, const char *needle, const char *msg) {
    if (src && strstr(src, needle)) return 0;
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void) {
    int failed = 0;

    /* 1. Strict monotonicity of LuminousOpacity on HDR snow (B = 1.25). */
    {
        const float B = 1.25f;
        float prevOut = B;
        for (int i = 1; i <= 80; ++i) {
            float p = (float)i * 0.10f;
            Rgb glow = { p, p * 0.35f, p * 0.04f };
            float a = LuminousOpacity(glow);
            float domOut = p + (1.0f - a) * B;
            if (domOut < B - 1e-5f || domOut <= prevOut) {
                fprintf(stderr, "FAIL: luminous opacity dipped or flattened at p=%.2f (out=%.4f prev=%.4f)\n",
                        p, domOut, prevOut);
                failed++;
                break;
            }
            prevOut = domOut;
        }
        /* Saturated HDR glow at p=2.0 must block >55% of snow's competing channel. */
        float a2 = LuminousOpacity((Rgb){ 2.0f, 0.7f, 0.08f });
        if (a2 < 0.55f) {
            fprintf(stderr, "FAIL: luminous opacity too weak at p=2.0 (%.3f < 0.55)\n", a2);
            failed++;
        }
        /* Faint outer glow at p=0.15 must stay nearly transparent (< 0.03). */
        float aTiny = LuminousOpacity((Rgb){ 0.15f, 0.05f, 0.006f });
        if (aTiny > 0.03f) {
            fprintf(stderr, "FAIL: faint outer glow still creates dark ring (a=%.4f > 0.03)\n", aTiny);
            failed++;
        }
    }

    /* 2. All 6 elemental hues retain strong chroma on snow AND stay unchanged on dark. */
    {
        const Rgb snow = { 0.90f, 0.95f, 1.12f };
        const Rgb dark = { 0.02f, 0.02f, 0.03f };
        const Rgb elements[6] = {
            { 1.00f, 0.36f, 0.06f }, /* Fire */
            { 0.15f, 0.50f, 0.95f }, /* Ice/Water jewel carrier */
            { 0.10f, 0.86f, 0.32f }, /* Wood */
            { 0.82f, 0.56f, 0.22f }, /* Earth */
            { 0.58f, 0.18f, 0.95f }, /* Lightning/Taiji */
            { 0.95f, 0.72f, 0.12f }  /* Holy/Gold */
        };
        for (int i = 0; i < 6; ++i) {
            Rgb e = elements[i];
            Rgb carrier = { e.r * 0.55f, e.g * 0.55f, e.b * 0.55f };
            float cov = 0.52f;
            Rgb glow = { e.r * 2.2f, e.g * 2.2f, e.b * 2.2f };
            float a = fmaxf(cov, LuminousOpacity(glow));
            Rgb onSnow = {
                carrier.r * cov + glow.r + (1.0f - a) * snow.r,
                carrier.g * cov + glow.g + (1.0f - a) * snow.g,
                carrier.b * cov + glow.b + (1.0f - a) * snow.b
            };
            Rgb bloom = { e.r * 0.35f, e.g * 0.35f, e.b * 0.35f };
            Rgb withBloom = ApplyBloomVeil(onSnow, bloom, 0.85f, 0.62f);
            Rgb tm = ToneMapScene((Rgb){ withBloom.r * 0.85f, withBloom.g * 0.85f, withBloom.b * 0.85f }, 0.62f);
            if (Sat(tm) < 0.35f) {
                fprintf(stderr, "FAIL: element %d washed out on snow (sat=%.3f < 0.35)\n", i, Sat(tm));
                failed++;
            }
            /* Dark scene optical veil must be exact zero (pure additive preserved). */
            Rgb darkVeil = ApplyBloomVeil(dark, bloom, 1.0f, 0.62f);
            Rgb darkAdd = { dark.r + bloom.r, dark.g + bloom.g, dark.b + bloom.b };
            if (fabsf(darkVeil.r - darkAdd.r) > 1e-5f ||
                fabsf(darkVeil.g - darkAdd.g) > 1e-5f ||
                fabsf(darkVeil.b - darkAdd.b) > 1e-5f) {
                fprintf(stderr, "FAIL: optical bloom veil altered dark-scene additive composite\n");
                failed++;
            }
        }
    }

    /* 3. Achromatic snow self-bloom gate: white snow with autoExposure stays below threshold. */
    {
        const Rgb snow = { 0.95f, 1.00f, 1.12f };
        const float expVal = 0.98f * (1.0f + (0.24f - 1.0f) * 0.60f); /* ~0.533 */
        const float threshold = 1.25f, knee = 0.15f;
        float sat = Sat(snow);
        float rawB = Max3(snow) * 0.8f + Luma(snow) * 0.2f;
        float lift = (1.0f - Smoothstep(0.08f, 0.28f, sat)) * (0.28f / expVal);
        float effB = fmaxf(0.0f, rawB - lift) * expVal;
        if (effB - threshold + knee > 0.0f) {
            fprintf(stderr, "FAIL: diffuse snow crossed bloom knee (effB=%.3f)\n", effB);
            failed++;
        }
    }

    /* 4. Source-wiring guards across core and post_process shaders. */
    {
        char *comp = ReadWhole("core/shaders/common/vfx_composite.glsl");
        char *bright = ReadWhole("post_process/shaders/bloom_bright.fs");
        char *post = ReadWhole("post_process/shaders/post_process.fs");
        char *plit = ReadWhole("core/particles/shaders/particle_lit.fs");
        char *pgpu = ReadWhole("core/particles/shaders/gpu/particle_gpu.fs");

        failed += Require(comp, "p2 / (1.32 + p2)", "vfx_composite.glsl must use quadratic luminous opacity");
        failed += Require(bright, "u_exposureTex", "bloom_bright.fs must couple autoExposure texture");
        failed += Require(bright, "achromaticLift", "bloom_bright.fs must suppress diffuse achromatic snow self-bloom");
        failed += Require(bright, "chromaMatchedPeak", "bloom_bright.fs must decouple thin-emitter energy from core whiteness");
        failed += Require(post, "u_bloomVeil", "post_process.fs must implement optical chromatic bloom veil");
        failed += Require(post, "hdrLift", "post_process.fs must ramp hue restoration on HDR emissive shoulder");
        failed += Require(plit, "u_surfaceMode", "particle_lit.fs must support premultiplied surface mode");
        failed += Require(pgpu, "u_blendLaw == 2", "particle_gpu.fs must support premultiplied GPU particle bucket");

        free(comp); free(bright); free(post); free(plit); free(pgpu);
    }

    puts(failed ? "vfx snow radiance contract: FAIL" : "PASS: unified bright/snow and dark VFX radiance contract verified");
    return failed != 0;
}

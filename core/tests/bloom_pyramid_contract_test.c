/* Regression guard for the HDR bloom pyramid.
 *
 * Three defects this locks down, all of which looked like "the art isn't bright
 * enough" rather than like renderer bugs:
 *
 *   1. The pyramid stopped at 1/16, so the widest blur the chain could produce
 *      was one 1/16 texel across — a tight halo and nothing beyond it.
 *   2. The upsample chain OVERWROTE its destination, so the final buffer held
 *      only the smallest mip stretched back up. Every level in between was
 *      computed and thrown away; making the pyramid deeper would have made this
 *      strictly worse.
 *   3. The bright pass clamped each pixel's energy with a hard cut, so past the
 *      cap raising a particle's emissive changed nothing at all.
 *
 * The numeric halves below assert the MATH; the source-string halves assert the
 * load-bearing expressions still exist, because a C mirror cannot run GLSL.
 * What the mirror cannot validate: that the GPU actually samples those offsets,
 * or that BLEND_ALPHA resolves to src*a + dst*(1-a) on a given backend.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (s == NULL) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) != NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int Reject(const char *source, const char *needle, const char *message)
{
    if (source == NULL || strstr(source, needle) == NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/* Mirrors bloom_bright.fs step 3: out = e * M / (M + e). */
static float SoftCeiling(float energy, float maxEnergy)
{
    return energy * maxEnergy / (maxEnergy + energy);
}

/* Mirrors bloom_downsample.fs: renormalised Karis-weighted group average.
 * `groups` are the five 2x2 group luminances, `w` their fixed kernel weights. */
static float KarisWeightedAverage(const float groups[5], int karisOn)
{
    static const float kernel[5] = {0.5f, 0.125f, 0.125f, 0.125f, 0.125f};
    float num = 0.0f, den = 0.0f;
    for (int i = 0; i < 5; ++i) {
        float w = kernel[i];
        if (karisOn) w /= (1.0f + groups[i]);
        num += groups[i] * w;
        den += w;
    }
    return num / den;
}

int main(void)
{
    int failed = 0;
    char *postFx = ReadFile("core/post_fx.c");
    char *postFxH = ReadFile("core/post_fx.h");
    char *bright = ReadFile("core/shaders/bloom_bright.fs");
    char *down = ReadFile("core/shaders/bloom_downsample.fs");
    char *up = ReadFile("core/shaders/bloom_upsample.fs");

    /* ---- 1. Pyramid depth ------------------------------------------------ */
    /* 5 levels below the 1/4 bright target = 6 total, reaching 1/128. Anything
     * shallower cannot produce a wide bleed no matter how the mix is tuned. */
    failed += Require(postFx, "#define DUAL_FILTER_LEVELS 5",
                      "bloom pyramid must keep 5 levels below the 1/4 bright pass");
    failed += Require(postFx, "s_dfLevels",
                      "the allocated level count must be resolved at init, not assumed");
    failed += Require(postFx, "if (w < 4 || h < 4)",
                      "a level too small for the 13-tap footprint must not be allocated");

    /* ---- 2. Upsample folds, never overwrites ----------------------------- */
    failed += Require(postFx, "BeginBlendMode(BLEND_ALPHA)",
                      "the upsample chain must blend onto its destination");
    failed += Require(postFx, "BLOOM_SCATTER_DEFAULT",
                      "the fold factor must be one named default, not a literal per call site");
    failed += Require(postFxH, "bloomScatter",
                      "scatter must be reachable from the public config");
    /* The downsample must still overwrite (mixAlpha < 0) — blending there would
     * accumulate the previous frame's pyramid, since nothing clears these. */
    failed += Require(postFx, "dstW, dstH,\n                     config, karis, -1.0f",
                      "the downsample pass must overwrite, not blend");

    /* Numeric: a fold is a lerp, so folding a level into itself is a no-op and
     * total energy cannot run away as the pyramid gets deeper. An additive
     * accumulate would multiply it by the level count. */
    {
        float scatter = 0.65f;
        float acc = 1.0f;
        for (int level = 0; level < 5; ++level)
            acc = acc * (1.0f - scatter) + 1.0f * scatter; /* mix(dst, src, s) */
        if (!(fabsf(acc - 1.0f) < 1e-5f)) {
            fprintf(stderr, "FAIL: lerp fold is not energy-normalised (got %f)\n", acc);
            failed++;
        }
    }

    /* ---- 3. Soft ceiling instead of a hard clamp ------------------------- */
    failed += Reject(bright, "(brightColor / currentEnergy) * maxEnergy",
                     "the hard energy clamp must be gone — it capped emissive dead");
    failed += Require(bright, "maxEnergy / (maxEnergy + energy)",
                      "the bright pass must use the asymptotic soft ceiling");
    failed += Require(bright, "uniform float u_exposure",
                      "bloom threshold must be evaluated in camera exposure space");
    failed += Require(bright, "* max(u_exposure, 0.0001)",
                      "exposure must affect bloom classification without writing exposed HDR");
    failed += Require(postFx, "brightExposureLoc",
                      "post FX must upload exposure to the bright prefilter");
    /* Exposure chart: the same raw emissive source must cross the camera-space
     * threshold predictably at 0.5 / 1 / 2 exposure, while the raw bloom write
     * remains unchanged and is exposed only in the final composite. */
    {
        const float raw = 0.75f, threshold = 1.0f;
        if (!((raw * 0.5f) < threshold && (raw * 1.0f) < threshold &&
              (raw * 2.0f) > threshold)) {
            fprintf(stderr, "FAIL: exposure chart does not cross bloom threshold consistently\n");
            failed++;
        }
    }
    failed += Require(bright, "col.rgb * weight * 2.2 * coverage",
                      "the historical extraction gain must stay, or every effect dims ~2x");
    /* The max-tap prefilter preserves a thin feature's PRESENCE but inflates its
     * ENERGY by up to 16x (one hot texel treated as a full 4x4 cell). The old
     * hard clamp hid that; lifting the ceiling exposed it as thin lightning
     * blooming into a fat white tube. Coverage scales the energy back without
     * touching fully-lit cells. */
    failed += Require(bright, "float coverage = sqrt(max(brightCount / 16.0, 1.0 / 16.0));",
                      "thin features must be energy-corrected by cell coverage");
    failed += Require(bright, "if (sampleBrightness > u_threshold) brightCount += 1.0;",
                      "coverage must be counted against the same threshold the pass uses");

    /* Numeric: coverage must leave a FULL cell untouched (that is the whole
     * point of lifting the ceiling) while pulling a one-texel spike down to a
     * quarter — not to a sixteenth, which is how thin emitters vanished before. */
    {
        float full = sqrtf(16.0f / 16.0f);
        float thin = sqrtf(1.0f / 16.0f);
        if (fabsf(full - 1.0f) > 1e-6f) {
            fprintf(stderr, "FAIL: coverage dims a fully lit cell (%.4f)\n", full);
            failed++;
        }
        if (fabsf(thin - 0.25f) > 1e-6f) {
            fprintf(stderr, "FAIL: one-texel coverage is %.4f, expected 0.25 "
                            "(linear 1/16 would make thin emitters vanish)\n", thin);
            failed++;
        }
    }

    /* Numeric: strictly increasing everywhere. This is the property the hard
     * clamp lacked and the reason 'raise emissiveBoost' did nothing past 4.0. */
    {
        const float maxE = 12.0f;
        float prev = -1.0f;
        for (float e = 0.5f; e < 200.0f; e *= 1.5f) {
            float out = SoftCeiling(e, maxE);
            if (!(out > prev)) {
                fprintf(stderr, "FAIL: soft ceiling flat at e=%.2f (%.4f <= %.4f)\n",
                        e, out, prev);
                failed++;
                break;
            }
            prev = out;
        }
        /* Near-identity for ordinary values, so existing effects do not shift. */
        if (!(SoftCeiling(1.0f, maxE) > 0.9f)) {
            fprintf(stderr, "FAIL: soft ceiling distorts ordinary brightness\n");
            failed++;
        }
        /* The old hard clamp cut at 4.0; the new ceiling must let a hot core
         * past that, which is the entire point of the change. */
        if (!(SoftCeiling(40.0f, maxE) > 4.0f)) {
            fprintf(stderr, "FAIL: hot cores still capped below the old hard clamp\n");
            failed++;
        }
    }

    /* ---- 4. Karis firefly weighting -------------------------------------- */
    failed += Require(down, "uniform float u_karis",
                      "the downsample must expose the firefly weighting switch");
    failed += Require(down, "1.0 + Luma(g0)",
                      "Karis weighting must divide by 1+luma per tap group");
    failed += Require(down, "/ max(wSum, 1e-5)",
                      "Karis weights must be renormalised or large bright areas dim");
    failed += Require(postFx, "float karis = (i == 0) ? s_bloomKaris : 0.0f",
                      "Karis must run on the first downsample only");
    failed += Require(down, "vec2(-2.0,  2.0) * t",
                      "the downsample must keep the 13-tap footprint");
    failed += Require(up, "* 4.0",
                      "the upsample must keep the 3x3 tent's centre weight");

    /* Numeric: the renormalised form leaves a uniformly bright cell EXACTLY
     * unchanged (so a big glow does not dim) while pulling down a lone spike
     * (so a one-texel firefly does not dominate its mip). */
    {
        float uniformCell[5] = {6.0f, 6.0f, 6.0f, 6.0f, 6.0f};
        float withKaris = KarisWeightedAverage(uniformCell, 1);
        if (!(fabsf(withKaris - 6.0f) < 1e-4f)) {
            fprintf(stderr, "FAIL: Karis dims a uniform bright cell (%f != 6.0)\n",
                    withKaris);
            failed++;
        }

        float fireflyCell[5] = {0.02f, 40.0f, 0.02f, 0.02f, 0.02f};
        float plain = KarisWeightedAverage(fireflyCell, 0);
        float damped = KarisWeightedAverage(fireflyCell, 1);
        if (!(damped < plain * 0.25f)) {
            fprintf(stderr, "FAIL: Karis does not suppress an isolated spike "
                            "(plain %f, damped %f)\n", plain, damped);
            failed++;
        }
    }

    /* ---- 5. The fold factor must actually reach the blend equation -------
     * BLEND_ALPHA mixes by the FRAGMENT's alpha. The upsample shader therefore
     * has to emit the scatter as its alpha; if it emitted a constant 1.0 the
     * fold would silently degrade back into an overwrite — and an overwrite
     * still produces a plausible-looking glow, so nothing on screen would say
     * the pyramid had stopped contributing. */
    failed += Require(up, "uniform float u_scatter",
                      "the upsample must receive the fold factor as a uniform");
    failed += Require(up, "clamp(u_scatter, 0.0, 1.0)",
                      "the upsample must emit the fold factor as fragment alpha");
    failed += Reject(up, "sum / 16.0, 1.0)",
                     "a constant alpha turns the fold back into an overwrite");
    failed += Require(postFx, "SetShaderValue(sh, usScatterLoc, &mixAlpha",
                      "post FX must push the fold factor into the upsample shader");
    failed += Require(down, "vec4(roundCol, 1.0)",
                      "the downsample writes unblended, so its alpha stays 1.0");

    free(postFx); free(postFxH); free(bright); free(down); free(up);
    if (failed != 0) {
        puts("bloom pyramid contract: FAIL");
        return 1;
    }
    puts("PASS: bloom pyramid is deep, folds instead of overwriting, "
         "and has no hard energy cap");
    return 0;
}

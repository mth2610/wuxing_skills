// core headless test — the surface filter must reach across a splat, and clamp.
//
// Two defects showed up in the same screenshot (a water body reconstructing as a
// heap of separate beads), and both are arithmetic:
//
//   1. REACH. The filter's spatial sigma was a constant sqrt(6) ~= 2.4 texels
//      and its radius a constant 10. A kernel that projects to forty texels
//      cannot be flattened by either, so every splat kept its own dome. Both
//      must scale with the kernel's PROJECTED size.
//   2. RANGE. Samples outside the depth range were down-weighted (a Gaussian
//      bilateral filter) instead of clamped into it, which is the one thing the
//      narrow-range filter (Truong & Yuksel 2018) is defined by. Clamping lets a
//      deeper neighbour pull the surface flat at full spatial weight.
//
// Mirrors core/fluid/shaders/fluid_depth_narrow_range.fs. What it cannot check:
// that the reconstructed surface LOOKS right — only the sandbox fixture shows
// that (NEW FX tab, WATER RING).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

static float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/* Mirror of the shader's per-pixel reach. projScale = proj[1][1] * height/2. */
static float ReachPixels(float kernelRadius, float projScale, float centerDistance)
{
    float kernelPixels = kernelRadius * projScale / fmaxf(centerDistance, 0.05f);
    return fmaxf(kernelPixels * 1.25f, 2.0f);
}

/* The Gaussian's width — the thing that decides how much the surface is smoothed
 * — must be a CONTINUOUS function of depth. Only the loop bound is integer. */
static float SigmaS(float kernelRadius, float projScale, float centerDistance)
{
    return fmaxf(ReachPixels(kernelRadius, projScale, centerDistance) * 0.5f, 1.0f);
}

static int AdaptiveRadius(float kernelRadius, float projScale, float centerDistance, int ceiling)
{
    float sigma = SigmaS(kernelRadius, projScale, centerDistance);
    float bound = ceilf(sigma * 3.0f);
    return (int)(bound < (float)ceiling ? bound : (float)ceiling);
}

/* Mirror of one filtered pixel: `neighbours` samples all sitting `deltaZ` behind
 * the centre, at unit spacing out to the radius. */
static float Filtered(float centerDistance, float deltaZ, float kernelRadius,
                      int radius, int clampMode)
{
    float sigmaS = fmaxf((float)radius * 0.5f, 1.0f);
    float range = fmaxf(kernelRadius * 2.5f, 0.006f);
    float weighted = centerDistance, weightSum = 1.0f;
    for (int i = 1; i <= radius; ++i)
    {
        float spatial = expf(-0.5f * (float)i * (float)i / (sigmaS * sigmaS));
        float sample = centerDistance + deltaZ;
        if (clampMode)
        {
            weighted += Clampf(sample, centerDistance - range, centerDistance + range) * spatial;
            weightSum += spatial;
        }
        else
        {
            /* The old behaviour: raw sample, Gaussian range weight. */
            float sigmaR = fmaxf(kernelRadius * 9.0f * 0.85f, 0.004f);
            float rangeWeight = expf(-0.5f * (deltaZ / sigmaR) * (deltaZ / sigmaR));
            weighted += sample * spatial * rangeWeight;
            weightSum += spatial * rangeWeight;
        }
    }
    return weighted / weightSum;
}

/* Fraction of the run that passes through the clamp UNTOUCHED, on a surface of
 * constant slope. This is the terracing measure: every clamped sample on a slope
 * is pinned to the same bound, and a run of pinned samples is a step. `predict`
 * mirrors clamping around the local tangent plane instead of the centre depth. */
static float UnclampedFraction(float slopePerTexel, float kernelRadius, int radius, int predict)
{
    float range = fmaxf(kernelRadius * 2.5f, 0.006f);
    float maxSlope = range * 6.0f / (float)radius;         /* the shader's bound */
    float predSlope = Clampf(slopePerTexel, -maxSlope, maxSlope);
    int untouched = 0;
    for (int i = 1; i <= radius; ++i)
    {
        float sample = slopePerTexel * (float)i;
        float predicted = predict ? predSlope * (float)i : 0.0f;
        if (sample > predicted - range - 1e-6f && sample < predicted + range + 1e-6f) untouched++;
    }
    return (float)untouched / (float)radius;
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *t = (char *)malloc((size_t)n + 1);
    if (!t) { fclose(f); return NULL; }
    size_t got = fread(t, 1, (size_t)n, f); t[got] = '\0'; fclose(f); return t;
}

int main(void)
{
    int bad = 0;

    /* 720p, 45 deg fovy: proj[1][1] = 1/tan(22.5deg) = 2.414, height/2 = 360. */
    const float projScale = 2.414f * 360.0f;
    const float kernel = 0.0855f;          /* the water ring's kernel at 0.9 m */
    const int ceiling = 28;                /* HIGH tier */

    int near = AdaptiveRadius(kernel, projScale, 3.0f, ceiling);
    int mid  = AdaptiveRadius(kernel, projScale, 8.0f, ceiling);
    int far  = AdaptiveRadius(kernel, projScale, 30.0f, ceiling);
    printf("      adaptive radius  3 m: %d  8 m: %d  30 m: %d texels (ceiling %d)\n",
           near, mid, far, ceiling);

    /* Close up, a kernel projects large and the filter must reach accordingly —
     * the old fixed 10 could not, which is the bead artifact. */
    CHECK(near > 10);
    CHECK(near >= mid && mid >= far);
    CHECK(far >= 3);                        /* never collapses to a single tap */
    CHECK(near <= ceiling);                 /* the tier still bounds the cost */

    /* A deeper neighbour must move the surface. Clamping keeps a bounded but
     * FULL-weight vote; the old Gaussian range weight let the raw sample in,
     * which drags the centre far past the clamp bound instead of flattening. */
    const float centre = 5.0f, deep = 0.60f;
    float clamped = Filtered(centre, deep, kernel, near, 1);
    float gaussian = Filtered(centre, deep, kernel, near, 0);
    float range = kernel * 2.5f;
    printf("      centre 5.000  clamped %.4f  old-gaussian %.4f  (range %.4f)\n",
           clamped, gaussian, range);
    CHECK(clamped > centre + 0.02f);                 /* it does pull the surface */
    CHECK(clamped <= centre + range + 0.0001f);      /* but never beyond the range */
    CHECK(gaussian > centre + range);                /* the old one overshot it */

    /* A sample INSIDE the range is untouched by the clamp: the filter must still
     * be an average over the sheet, not a bound applied to everything. */
    float gentle = Filtered(centre, 0.05f, kernel, near, 1);
    CHECK(gentle > centre + 0.02f && gentle < centre + 0.05f + 0.0001f);

    /* Terracing. A clamp centred on the CENTRE depth pins every sample past
     * range/slope texels to the same bound, and a run of pinned samples is a
     * step — the parallel corrugation that appeared on the water body as soon
     * as the radius grew wide enough for a slope to leave the range. Clamping
     * around the local tangent plane instead leaves a constant slope untouched. */
    const float slope = 0.030f;     /* metres of depth per texel: a steep flank */
    float withPrediction = UnclampedFraction(slope, kernel, near, 1);
    float withoutPrediction = UnclampedFraction(slope, kernel, near, 0);
    printf("      slope %.3f m/texel  unclamped: tangent-plane %.0f%%  centre-only %.0f%%\n",
           slope, withPrediction * 100.0f, withoutPrediction * 100.0f);
    CHECK(withPrediction > 0.95f);
    CHECK(withoutPrediction < 0.50f);
    /* Past the slope bound the prediction stops tracking and samples clamp
     * again. That is a deliberate limit, not a defect — but it must still beat
     * clamping on the centre, or the bound is doing more harm than good. */
    const float cliff = 0.090f;
    float cliffPredicted = UnclampedFraction(cliff, kernel, near, 1);
    float cliffCentre = UnclampedFraction(cliff, kernel, near, 0);
    printf("      slope %.3f m/texel (past the bound): tangent-plane %.0f%%  centre-only %.0f%%\n",
           cliff, cliffPredicted * 100.0f, cliffCentre * 100.0f);
    CHECK(cliffPredicted > cliffCentre);
    /* A flat surface must be unaffected either way — the prediction is a fix for
     * slopes, not a licence to widen the range. */
    CHECK(UnclampedFraction(0.0f, kernel, near, 1) > 0.99f);
    CHECK(UnclampedFraction(0.0f, kernel, near, 0) > 0.99f);

    /* CONTOUR LINES. The smoothing amount is a function of depth, so if it is a
     * step function every step draws an iso-depth curve across the body — a
     * topographic contour map, which is exactly what deriving the Gaussian from
     * an integer radius produced (eight lines across a two-metre body). Sweep
     * the depth range a body actually spans and demand the width move smoothly. */
    {
        float worstJump = 0.0f, worstAt = 0.0f;
        float previous = SigmaS(kernel, projScale, 4.0f);
        for (float z = 4.002f; z <= 6.0f; z += 0.002f)
        {
            float sigma = SigmaS(kernel, projScale, z);
            float jump = fabsf(sigma - previous) / fmaxf(previous, 1e-6f);
            if (jump > worstJump) { worstJump = jump; worstAt = z; }
            previous = sigma;
        }
        printf("      sigma sweep 4-6 m: worst step %.4f%% at %.3f m\n", worstJump * 100.0f, worstAt);
        /* A 2 mm change in depth may not change the smoothing by even a percent.
         * The integer-radius version stepped by 1/28 = 3.6% at eight depths. */
        CHECK(worstJump < 0.01f);
    }

    char *fs = ReadFile("core/fluid/shaders/fluid_depth_narrow_range.fs");
    char *c = ReadFile("core/fluid/fluid_surface.c");
    if (!fs || !c) { printf("FAIL: cannot read the filter shader or fluid_surface.c\n"); bad++; }
    else
    {
        CHECK(strstr(fs, "clamp(sampleDistance, predictedDistance - range, predictedDistance + range)") != NULL);
        CHECK(strstr(fs, "centerDistance + slope * fi") != NULL);
        /* The slope estimate must stay CONTINUOUS. A hard pick between the two
         * one-sided differences is a discontinuous kernel, and iterating a
         * discontinuous kernel bands the output along the pass axis. */
        CHECK(strstr(fs, "0.5 * (gPlus + gMinus) * trust") != NULL);
        CHECK(strstr(fs, "abs(gPlus) < abs(gMinus)") == NULL);
        CHECK(strstr(fs, "int adaptiveRadius") != NULL);
        /* The reach multiplier is mirrored above; assert it so the mirror cannot
         * drift away from the shader while still reporting green. */
        CHECK(strstr(fs, "kernelPixels * 1.25") != NULL);
        /* Sigma must come from the raw projected size, never from the integer
         * radius — that is the contour-line defect. */
        CHECK(strstr(fs, "float sigmaS = max(reachPixels * 0.5, 1.0);") != NULL);
        CHECK(strstr(fs, "float(adaptiveRadius) * 0.5") == NULL);
        CHECK(strstr(fs, "sigmaS * sigmaS") != NULL);
        /* The Gaussian range weight is what the clamp replaced. */
        CHECK(strstr(fs, "rangeWeight") == NULL);
        /* The tier values are a ceiling now; if they drop back to ~10 the
         * adaptive radius is capped right back to the bead artifact. */
        CHECK(strstr(c, "GfxQuality_Get()>=GFX_HIGH?28:") != NULL);
        free(fs); free(c);
    }

    printf("%s: fluid_depth_filter_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}

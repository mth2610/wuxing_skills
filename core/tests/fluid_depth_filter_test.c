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
//   3. BOUNDARY. At the silhouette the outward samples do not exist, and DROPPING
//      them leaves a one-sided average that leans into the body — along the pass
//      axis that lean smears into a streak. Two fixes that removed filtering
//      instead of removing bias made it worse; the third contributes the
//      tangent-plane prediction and keeps the kernel's full width.
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

/* Mirror of the narrow-range filter's accumulation (Truong & Yuksel 2018), as
 * implemented in fluid_depth_narrow_range.fs.
 *
 * `surfaceOnMinus` is how many texels of surface remain on the - side before the
 * background starts, so `radius` means "interior" and 3 means "near a silhouette".
 * `pairDrop` selects the published behaviour: a background sample zeroes its
 * PARTNER's weight as well as its own.
 *
 * Returns how much of a `bump` at the centre survives the filter. Lower is more
 * smoothing; the number that matters is whether the edge matches the interior. */
static float SurvivingBump(float bump, float kernelRadius, int radius,
                           int surfaceOnMinus, int pairDrop, int *outWeightTaps)
{
    const float BACKGROUND = 1.0e6f;
    float sigmaS = fmaxf((float)radius * 0.5f, 1.0f);
    float threshold = kernelRadius * 10.5f;
    float lowerClamp = bump - kernelRadius;
    float upperPos = bump + threshold, lowerPos = bump - threshold;
    float upperNeg = upperPos, lowerNeg = lowerPos;
    float sum = bump, wsum = 1.0f;
    int taps = 1;

    for (int i = 1; i <= radius; ++i)
    {
        float w = expf(-0.5f * (float)i * (float)i / (sigmaS * sigmaS));
        float wPos = w, wNeg = w;
        /* + side always has surface, at 0 (the flat sheet the bump sits on). */
        float dPos = 0.0f;
        float dNeg = (i <= surfaceOnMinus) ? 0.0f : BACKGROUND;

        if (dPos > upperPos) { wPos = 0.0f; if (pairDrop) wNeg = 0.0f; }
        else if (dPos < lowerPos) dPos = lowerClamp;
        else { upperPos = fmaxf(upperPos, dPos + threshold); lowerPos = fminf(lowerPos, dPos - threshold); }

        if (dNeg > upperNeg) { wNeg = 0.0f; if (pairDrop) wPos = 0.0f; }
        else if (dNeg < lowerNeg) dNeg = lowerClamp;
        else { upperNeg = fmaxf(upperNeg, dNeg + threshold); lowerNeg = fminf(lowerNeg, dNeg - threshold); }

        sum += dPos * wPos + dNeg * wNeg;
        wsum += wPos + wNeg;
        if (wPos > 0.0f) taps++;
        if (wNeg > 0.0f) taps++;
    }
    if (outWeightTaps) *outWeightTaps = taps;
    return sum / wsum;
}

/* How many taps still vote when the - side has a GAP (background) between
 * `gapStart` and `gapEnd` and surface again beyond it. This is the property that
 * separates the published rule from simply breaking out of the loop: a gap must
 * silence its own ring, not end the run — a sparse splat field is full of them,
 * and terminating there is what made an earlier attempt worse. */
static int TapsAcrossGap(int radius, int gapStart, int gapEnd, int terminate)
{
    int taps = 1;
    for (int i = 1; i <= radius; ++i)
    {
        int background = (i >= gapStart && i <= gapEnd);
        if (background) { if (terminate) break; else continue; }
        taps += 2;
    }
    return taps;
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
    const float kernel = 0.0855f;          /* a representative splat kernel, metres */
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

    /* THE EDGE. What the published rule guarantees is that the average stays
     * UNBIASED: a background sample zeroes its partner's weight too, so weight
     * is removed rather than added, and what remains is symmetric.
     *
     * It does NOT guarantee the edge is smoothed as hard as the interior — the
     * kernel genuinely shrinks there. An earlier version of this test demanded
     * that equality, which is not a property of the method, and chasing it led
     * to fabricating samples (even reflection) that looked worse on screen. The
     * assertions below are what the algorithm actually promises. */
    {
        const float bump = 0.05f;          /* a splat dome at the centre */
        int interiorTaps = 0, edgePairTaps = 0, edgeSoloTaps = 0;
        float interior = SurvivingBump(bump, kernel, near, near, 1, &interiorTaps);
        float edgePair = SurvivingBump(bump, kernel, near, 3, 1, &edgePairTaps);
        float edgeSolo = SurvivingBump(bump, kernel, near, 3, 0, &edgeSoloTaps);
        printf("      bump %.3f m surviving:  interior %.5f (%d taps) | "
               "edge pair-drop %.5f (%d) | edge solo-drop %.5f (%d)\n",
               bump, interior, interiorTaps, edgePair, edgePairTaps, edgeSolo, edgeSoloTaps);

        /* The filter has to flatten a dome in the interior. */
        CHECK(interior < bump * 0.25f);
        /* Weight is REMOVED at the edge, never added: a rule that ends up with
         * more taps than the interior is fabricating data. */
        CHECK(edgePairTaps < interiorTaps);
        /* Dropping one side alone leaves its partner voting unopposed — more
         * taps than the symmetric rule, and that asymmetry is the bias. */
        CHECK(edgeSoloTaps > edgePairTaps);
    }

    /* A GAP must silence its ring, not end the run. This is the difference
     * between the published rule and breaking out of the loop, and it is what a
     * sparse splat field depends on: interior holes are everywhere, and stopping
     * at the first one removes filtering exactly where it is needed most. */
    {
        int continued = TapsAcrossGap(near, 4, 6, 0);
        int terminated = TapsAcrossGap(near, 4, 6, 1);
        printf("      3-texel gap at radius 4: continue %d taps | terminate %d taps\n",
               continued, terminated);
        CHECK(continued > terminated * 3);
        CHECK(terminated == 1 + 2 * 3);
    }

    char *fs = ReadFile("core/fluid/shaders/fluid_depth_narrow_range.fs");
    char *c = ReadFile("core/fluid/fluid_surface.c");
    if (!fs || !c) { printf("FAIL: cannot read the filter shader or fluid_surface.c\n"); bad++; }
    else
    {
        CHECK(strstr(fs, "int adaptiveRadius") != NULL);
        /* The boundary condition: a missing side contributes the prediction. It
         * must not go back to dropping (streaks) or to breaking (no filtering). */
        CHECK(strstr(fs, "|| negative >= 0.99999) break;") == NULL);
        /* The reach multiplier is mirrored above; assert it so the mirror cannot
         * drift away from the shader while still reporting green. */
        CHECK(strstr(fs, "kernelPixels * 1.25") != NULL);
        /* The published rule: a background sample zeroes its PARTNER too. */
        CHECK(strstr(fs, "if (dPos > upperPos) { wPos = 0.0; wNeg = 0.0; }") != NULL);
        CHECK(strstr(fs, "if (dNeg > upperNeg) { wNeg = 0.0; wPos = 0.0; }") != NULL);
        /* Range extension replaced the home-grown tangent-plane prediction. */
        CHECK(strstr(fs, "upperPos = max(upperPos, dPos + threshold)") != NULL);
        CHECK(strstr(fs, "predictedDistance") == NULL);
        CHECK(strstr(fs, "float sigmaS = max(reachPixels * 0.5, 1.0);") != NULL);
        /* The tier values are a ceiling now; if they drop back to ~10 the
         * adaptive radius is capped right back to the bead artifact. */
        CHECK(strstr(c, "GfxQuality_Get()>=GFX_HIGH?28:") != NULL);
        free(fs); free(c);
    }

    printf("%s: fluid_depth_filter_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}

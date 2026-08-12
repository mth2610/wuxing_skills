// core headless test — the true 2D narrow-range kernel
// (core/fluid/shaders/fluid_depth_narrow_range.fs).
//
// The depth filter ran as two 1D passes, which is the paper's `filter1D` applied
// as an approximate separation. Truong & Yuksel's own remark about the bilateral
// Gaussian — "not separable, and an approximate separation can result in visual
// artefacts" — applies to their filter too, which is why their reference
// implementation ships `filter2D` behind a switch.
//
// Measured on the fixture before this was written: the horizontal pass ALONE
// smears the reconstructed normal into horizontal ribbons and the vertical pass
// alone into vertical ones, so the residue of both is a cross-hatch beside the
// silhouette. That is the artifact this kernel removes.
//
// What is mirrored here is the tap geometry and the paper's sample rules. It
// cannot see a stripe; the sandbox does that (NEW FX tab, WATER RING and FLUID
// IMPACT, with the reconstructed normal shown as colour).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

/* Mirror of the 2D loop's tap set: a DISC of the adaptive radius, walked as
 * point-symmetric pairs over the upper half plane. */
#define FLUID_FILTER2D_MAX_RADIUS 10
static int TapCount2D(int requestedRadius, int *outPairs, int *outOffAxis)
{
    /* The shader caps the 2D reach; the adaptive radius may ask for far more. */
    int radius = requestedRadius < FLUID_FILTER2D_MAX_RADIUS
               ? requestedRadius : FLUID_FILTER2D_MAX_RADIUS;
    int taps = 1, pairs = 0, offAxis = 0;   /* the centre is always in */
    for (int dy = 0; dy <= radius; dy++)
        for (int m = 0; m <= radius; m++)
        {
            if (dy == 0 && m == 0) continue;
            if (m * m + dy * dy > radius * radius) continue;
            for (int pair = 0; pair < 2; pair++)
            {
                if (pair == 1 && (m == 0 || dy == 0)) continue;
                taps += 2; pairs++;
                if (m != 0 && dy != 0) offAxis++;
            }
        }
    if (outPairs) *outPairs = pairs;
    if (outOffAxis) *outOffAxis = offAxis;
    return taps;
}

/* The separable pair, for comparison: two passes of 1 + 2r each. */
static int TapCount1D(int radius) { return 2 * (1 + 2 * radius); }

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

    /* ---- THE POINT OF THE WHOLE CHANGE: the kernel must reach OFF-AXIS.
     * A separable pass only ever samples along its own axis, so no diagonal
     * neighbour is ever read; that is the documented cause of the cross-hatch.
     * The overwhelming majority of a disc's taps are off both axes. */
    {
        int pairs = 0, offAxis = 0;
        int taps = TapCount2D(13, &pairs, &offAxis);
        printf("      radius 13: %d taps, %d pairs, %d of them off-axis (%.0f%%)\n",
               taps, pairs, offAxis, 100.0 * (double)offAxis / (double)pairs);
        CHECK(offAxis > 0);
        CHECK((double)offAxis / (double)pairs > 0.7);
    }

    /* ---- No tap is sampled twice. On an axis the two pair directions coincide,
     * which is why the second pair is skipped there; getting that wrong
     * double-weights the axes and reintroduces a cross. */
    {
        const int R = 6, N = 2 * R + 3;
        static int hits[15][15];
        memset(hits, 0, sizeof(hits));
        for (int dy = 0; dy <= R; dy++)
            for (int m = 0; m <= R; m++)
            {
                if (dy == 0 && m == 0) continue;
                if (m * m + dy * dy > R * R) continue;
                for (int pair = 0; pair < 2; pair++)
                {
                    if (pair == 1 && (m == 0 || dy == 0)) continue;
                    int ox = (pair == 0) ? m : -m, oy = dy;
                    hits[oy + R][ox + R]++;          /* the sample */
                    hits[-oy + R][-ox + R]++;        /* its point reflection */
                }
            }
        int duplicated = 0, covered = 0;
        for (int y = 0; y < N - 2; y++)
            for (int x = 0; x < N - 2; x++)
            {
                if (hits[y][x] > 1) duplicated++;
                if (hits[y][x] == 1) covered++;
            }
        printf("      radius 6: %d distinct taps, %d sampled twice\n", covered, duplicated);
        CHECK(duplicated == 0);
        /* And the disc is actually covered — a kernel that skipped most of its
         * own area would be cheap and useless. */
        CHECK(covered > 100);
    }

    /* ---- COST MUST BE BOUNDED. This is the one that shipped broken: the disc is
     * quadratic in the radius, the radius grows as the camera closes in, and
     * unbounded it measured 112 ms/frame at close range against the separable
     * path's 31 — 9 fps. The cap is what makes the 2D kernel affordable, so the
     * tap count must stop growing however large the adaptive radius gets. */
    {
        int atCap = TapCount2D(FLUID_FILTER2D_MAX_RADIUS, NULL, NULL);
        for (int r = 4; r <= 28; r += 8)
            printf("      requested radius %2d: 2D %4d taps vs separable %3d\n",
                   r, TapCount2D(r, NULL, NULL), TapCount1D(r));
        CHECK(TapCount2D(28, NULL, NULL) == atCap);
        CHECK(TapCount2D(100, NULL, NULL) == atCap);
        /* And the cap has to be low enough to be worth having: the separable
         * pair at the tier ceiling of 28 is 114 taps, so a 2D disc costing ten
         * times that would be the regression again. */
        CHECK(atCap < 4 * TapCount1D(28));
    }

    /* ---- Anti-drift: the paper's four rules must still be in the 2D branch, and
     * the tier gate must still keep the mobile tiers off it. */
    {
        char *shader = ReadFile("core/fluid/shaders/fluid_depth_narrow_range.fs");
        if (!shader) { printf("FAIL: cannot read fluid_depth_narrow_range.fs\n"); bad++; }
        else
        {
            const char *twoD = strstr(shader, "if (u_filter2D != 0) {");
            CHECK(twoD != NULL);
            if (twoD)
            {
                /* pair rejection, both directions */
                CHECK(strstr(twoD, "if (dPos > upper[iPos]) { wPos = 0.0; wNeg = 0.0; }") != NULL);
                CHECK(strstr(twoD, "if (dNeg > upper[iNeg]) { wNeg = 0.0; wPos = 0.0; }") != NULL);
                /* clamp, not reject, below the lower bound */
                CHECK(strstr(twoD, "else if (dPos < lower[iPos]) { dPos = lowerClamp; }") != NULL);
                /* range extension */
                CHECK(strstr(twoD, "upper[iPos] = max(upper[iPos], dPos + threshold)") != NULL);
                /* a disc, and the centre excluded once */
                CHECK(strstr(twoD, "if (r2 > radiusSquared) continue;") != NULL);
                CHECK(strstr(twoD, "if (dy == 0 && m == 0) continue;") != NULL);
                CHECK(strstr(twoD, "if (pair == 1 && (m == 0 || dy == 0)) continue;") != NULL);
                /* the reach cap, and no sparse stride: subsampling the disc on a
                 * fixed lattice made the lattice itself visible as a dot grid. */
                CHECK(strstr(twoD, "min(adaptiveRadius, FLUID_FILTER2D_MAX_RADIUS)") != NULL);
                CHECK(strstr(twoD, "stride") == NULL);
            }
            free(shader);
        }
        char *host = ReadFile("core/fluid/fluid_surface.c");
        if (!host) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else
        {
            /* HIGH only. MED is the Android default and has never been measured
             * on a tiler, so it must stay on the separable path. */
            CHECK(strstr(host, "if (GfxQuality_Get() >= GFX_HIGH) {") != NULL);
            free(host);
        }
    }

    printf(bad ? "fluid_filter_2d: FAIL (%d)\n" : "fluid_filter_2d: PASS\n", bad);
    return bad ? 1 : 0;
}

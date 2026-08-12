// core headless test — sub-pixel coverage of the SSF silhouette
// (core/fluid/shaders/fluid_surface.fs).
//
// The composite discarded on a binary depth-mask test, so the body's outline was
// a one-pixel staircase with no antialiasing whatever — at any real zoom the most
// visible thing left on the surface.
//
// The soft ramp that should have feathered it already existed and sat on the
// wrong side of the discard: `surfaceCoverage` fades with thickness, thickness is
// Gaussian-blurred, so its ramp lands OUTSIDE the mask and every pixel the mask
// kept was fully opaque. Dilating the mask by one texel and taking the fringe's
// alpha from thickness therefore only moved the hard edge out by a pixel — that
// was measured on the fixture and is why the coverage below is computed from the
// MASK instead.
//
// What this mirrors is that coverage arithmetic. It cannot see a staircase; the
// sandbox does that (NEW FX tab, WATER RING, zoomed into the outer rim).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

/* Mirror of the composite. `insideNeighbours` is how many of the four cardinal
 * taps carry fluid; `dilated` means this pixel's own depth was empty and it
 * borrowed a neighbour's. */
static float MaskCoverage(int insideNeighbours, int dilated)
{
    return dilated ? (0.25f * (float)insideNeighbours)
                   : (0.5f + 0.125f * (float)insideNeighbours);
}

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

    /* ---- The interior must be EXACTLY opaque. Antialiasing that shaved a
     * fraction off every interior pixel would wash the whole body out, and the
     * body is 88% interior. */
    CHECK(fabsf(MaskCoverage(4, 0) - 1.0f) < 1e-6f);

    /* ---- A pixel whose centre is inside is at least half covered, whatever its
     * neighbours say. Letting an inside pixel reach zero would punch holes in a
     * sparse splat field rather than smooth its edge. */
    for (int n = 0; n <= 4; n++) CHECK(MaskCoverage(n, 0) >= 0.5f - 1e-6f);
    CHECK(fabsf(MaskCoverage(0, 0) - 0.5f) < 1e-6f);

    /* ---- A dilated fringe pixel has its centre OUTSIDE, so it starts at zero
     * and only its neighbours can give it coverage. */
    CHECK(fabsf(MaskCoverage(0, 1) - 0.0f) < 1e-6f);
    /* ...except when all four neighbours are fluid, which is not a fringe at all
     * but a one-pixel hole in the mask, and should be filled solid. */
    CHECK(fabsf(MaskCoverage(4, 1) - 1.0f) < 1e-6f);

    /* ---- Monotone on both branches, and the fringe is never MORE covered than
     * an inside pixel with the same neighbourhood — otherwise the dilation would
     * brighten the outline it is meant to soften. */
    for (int n = 0; n < 4; n++)
    {
        CHECK(MaskCoverage(n + 1, 0) > MaskCoverage(n, 0));
        CHECK(MaskCoverage(n + 1, 1) > MaskCoverage(n, 1));
        CHECK(MaskCoverage(n, 1) <= MaskCoverage(n, 0) + 1e-6f);
    }

    /* ---- The gradient across a straight edge. Walking out of the body along a
     * row, the coverage a straight silhouette produces is interior 1.0, then the
     * last inside pixel (3 neighbours), then the fringe (1 neighbour), then
     * nothing — a two-pixel ramp where there used to be a cliff. */
    {
        float inside = MaskCoverage(4, 0);
        float lastInside = MaskCoverage(3, 0);
        float fringe = MaskCoverage(1, 1);
        printf("      straight edge ramp: %.3f -> %.3f -> %.3f -> 0\n",
               inside, lastInside, fringe);
        CHECK(inside > lastInside && lastInside > fringe && fringe > 0.0f);
        /* Each step must be a real step; a ramp of 1.0, 0.99, 0.98 is still a
         * cliff to the eye. */
        CHECK(inside - lastInside > 0.05f);
        CHECK(lastInside - fringe > 0.20f);
    }

    /* ---- Anti-drift. All three pieces are load-bearing and none of them is
     * obvious from the rendered result: losing the dilation silently restores
     * the cliff, and losing the multiply silently restores full opacity. */
    {
        char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
        if (!shader) { printf("FAIL: cannot read fluid_surface.fs\n"); bad++; }
        else
        {
            CHECK(strstr(shader, "bool dilatedFringe = false;") != NULL);
            CHECK(strstr(shader, "fluidDepth = min(min(dilateL, dilateR), min(dilateD, dilateU));") != NULL);
            CHECK(strstr(shader, "float maskCoverage = dilatedFringe ? (0.25 * insideCount)") != NULL);
            CHECK(strstr(shader, "* intersectionVisibility * maskCoverage;") != NULL);
            /* The thickness tap has to be read BEFORE the mask test, or the
             * fringe cannot be told from empty space without a second fetch. */
            const char *tap = strstr(shader, "float thicknessProxy =");
            const char *test = strstr(shader, "if (fluidDepth >= 0.99999) {");
            CHECK(tap != NULL && test != NULL && tap < test);
            free(shader);
        }
    }

    printf(bad ? "fluid_silhouette_coverage: FAIL (%d)\n" : "fluid_silhouette_coverage: PASS\n", bad);
    return bad ? 1 : 0;
}

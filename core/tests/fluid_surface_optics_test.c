// core headless test — SSF optical-thickness behaviour (core/fluid/shaders/fluid_surface.fs).
//
// The complaint this guards: a water body that renders as opaque cyan plastic.
// Both causes are arithmetic, not rendering, so they are testable without a GPU:
//
//   1. The thickness decode saturated at its 0.16 m cap for every interior pixel
//      of a dense orb, so the silhouette carried NO thickness gradient. A body of
//      one constant optical depth cannot read as liquid.
//   2. The receiver depth gap was combined with max(), so an airborne body's
//      column was pinned at a constant 0.40 m — 2.5x the measured path. The
//      receiver may only BOUND the column (min), never create one.
//
// This mirrors the two GLSL expressions numerically and asserts the shader still
// contains them, so the mirror cannot silently drift. It cannot validate the
// rasterised thickness pass itself, the reconstruction filter, or final colour —
// those need the sandbox fixture (NEW FX tab, WATER ORB).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

/* Mirror of DecodeOpticalThickness(). */
#define FLUID_KERNEL_OVERLAP 1.5f
static float DecodeOpticalThickness(float encodedThickness)
{
    float accumulatedPath = fmaxf(encodedThickness / 16.0f, 0.0f);
    float traversedPath = accumulatedPath / FLUID_KERNEL_OVERLAP;
    return 0.16f * (1.0f - expf(-traversedPath / 0.42f));
}

/* Mirror of the receiver-gap combination in main(). */
static float WaterColumnDepth(float kernelThickness, float depthGap, int hasReceiver)
{
    if (!hasReceiver) return kernelThickness;
    return fminf(kernelThickness, fmaxf(0.022f, depthGap * 1.25f));
}

/* One splat contributes 2*radius of chord at its centre, encoded x16. */
static float EncodedThickness(int overlaps, float radius)
{
    return (float)overlaps * (2.0f * radius) * 16.0f;
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

    /* The authored water orb: 2,000 splats of r = 0.44 * 0.09 m. A ray through
     * the centre crosses many kernels; one near the rim crosses a handful. */
    const float radius = 0.44f * 0.09f;
    float rim = DecodeOpticalThickness(EncodedThickness(2, radius));
    float mid = DecodeOpticalThickness(EncodedThickness(12, radius));
    float core = DecodeOpticalThickness(EncodedThickness(24, radius));

    /* The gradient is the whole point. With the old 0.11 m knee, mid and core
     * both clamped to 0.16 and this ratio collapsed to ~1.3. */
    CHECK(core / rim > 2.0f);
    /* Doubling the traversal must still move the result: a saturated decode
     * returns the same number for 12 and 24 overlaps. */
    CHECK((core - mid) / core > 0.05f);
    /* The cap is still a cap. */
    CHECK(core < 0.16f && rim > 0.0f);
    CHECK(DecodeOpticalThickness(0.0f) == 0.0f);
    CHECK(DecodeOpticalThickness(1.0e6f) <= 0.16f);

    /* An airborne body over distant ground: the receiver must not deepen it. */
    float airborneGap = 1.6f;   /* metres of empty space below the orb */
    float column = WaterColumnDepth(core, airborneGap, 1);
    CHECK(fabsf(column - core) < 1.0e-6f);
    CHECK(column < 0.40f);              /* the old max() pinned this at 0.40 */

    /* Liquid resting on a receiver: the gap shortens the column... */
    float resting = WaterColumnDepth(core, 0.010f, 1);
    CHECK(resting < core);
    /* ...but never below the 2.2 cm floor, so a thin sheet stays visible. */
    CHECK(resting >= 0.022f - 1.0e-6f);
    /* No receiver at all leaves the measured thickness untouched. */
    CHECK(WaterColumnDepth(core, 0.0f, 0) == core);

    /* Anti-drift: the shader must still carry the load-bearing expressions. */
    char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
    if (!shader) {
        printf("FAIL: cannot read core/fluid/shaders/fluid_surface.fs\n");
        bad++;
    } else {
        CHECK(strstr(shader, "accumulatedPath / FLUID_KERNEL_OVERLAP") != NULL);
        CHECK(strstr(shader, "exp(-traversedPath / 0.42)") != NULL);
        CHECK(strstr(shader, "min(kernelThickness, max(0.022, depthGap * 1.25))") != NULL);
        /* The regression itself (0262068): the receiver creating a column. */
        CHECK(strstr(shader, "max(kernelThickness, min(0.40") == NULL);
        free(shader);
    }

    printf("%s: fluid_surface_optics_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}

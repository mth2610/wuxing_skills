// Headless contract test — the soft-depth region snapshot's blit alignment.
//
// ScreenDistort_SnapshotDepth() copies a RECTANGLE of the scene's depth attachment into
// a downscaled linear-depth target, and every consumer (soft particles, the ShieldShell
// ground contact) then samples that target with `gl_FragCoord.xy / u_resolution` — i.e.
// it assumes the copy is the identity on screen position. The blit uses a NEGATIVE source
// height, which makes DrawTexturePro read the block bottom-to-top; that is what converts
// FBO storage order back into screen order, but it mirrors WITHIN THE BLOCK, so the block
// must also be written at its mirrored position for the composition to come out as the
// identity the samplers assume.
//
// The original code wrote the block at `region.y / DOWNSCALE`. That is correct for
// exactly one region — the whole frame, which is its own mirror — and the whole frame is
// the only region it was ever exercised with, so nothing looked wrong. For a partial
// region the depth landed `H - 2y - h` screen rows from where it was read.
//
// This test is the arithmetic, not the rendering: it composes the blit's row mapping with
// the sampler's row mapping and asserts the result is the identity for partial regions.
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

#define SOFT_DEPTH_DOWNSCALE 2   /* mirrors core/screen_distort.c */

/* The destination top edge, as core/screen_distort.c computes it. */
static float SoftDepthDestY(float fullHeight, float regionY, float regionH)
{
    return (fullHeight - (regionY + regionH)) / (float)SOFT_DEPTH_DOWNSCALE;
}

/* The pre-fix formula, kept so the guard fails on the old behaviour rather than merely
   agreeing with whatever the source says today. */
static float SoftDepthDestY_Broken(float regionY)
{
    return regionY / (float)SOFT_DEPTH_DOWNSCALE;
}

/* Which SOURCE storage row ends up readable at screen row `r`, given a blit that places
   the (vertically mirrored) region block at destination top edge `destY`.
     - dest storage row for a destination raylib-y t is  (H/D) - t
     - the sampler reads texel row (H - r) / D for screen row r (top-down), because
       gl_FragCoord.y counts from the bottom
     - inside the block, fraction f from its top maps to source storage row
       (regionY + regionH) - f * regionH                                            */
static float SampledSourceRow(float fullHeight, float regionY, float regionH,
                              float destY, float screenRow)
{
    const float D = (float)SOFT_DEPTH_DOWNSCALE;
    float texelRow = (fullHeight - screenRow) / D;      /* what the sampler asks for */
    float t = fullHeight / D - texelRow;                /* dest raylib-y holding it   */
    float f = (t - destY) / (regionH / D);              /* position inside the block  */
    return (regionY + regionH) - f * regionH;
}

/* Screen row r (top-down) is source storage row H - r: FBO rows count from the bottom. */
static float ExpectedSourceRow(float fullHeight, float screenRow)
{ return fullHeight - screenRow; }

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[200000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static int NearlyEqual(float a, float b) { float d = a - b; return d < 0.02f && d > -0.02f; }

int main(void)
{
    const float H = 720.0f;

    /* A partial region — the shape a ShieldShell bounding box actually produces. */
    {
        const float y = 132.0f, h = 426.0f;
        float destY = SoftDepthDestY(H, y, h);
        int identity = 1, brokenIdentity = 1;
        float worstBrokenError = 0.0f;
        for (float r = y + 1.0f; r < y + h; r += 7.0f)
        {
            if (!NearlyEqual(SampledSourceRow(H, y, h, destY, r), ExpectedSourceRow(H, r)))
                identity = 0;
            float bad = SampledSourceRow(H, y, h, SoftDepthDestY_Broken(y), r);
            if (!NearlyEqual(bad, ExpectedSourceRow(H, r))) brokenIdentity = 0;
            float err = bad - ExpectedSourceRow(H, r);
            if (err < 0.0f) err = -err;
            if (err > worstBrokenError) worstBrokenError = err;
        }
        CHECK(identity, "a partial soft-depth region round-trips to the row it was copied from");
        CHECK(!brokenIdentity,
              "the pre-fix destination Y does NOT round-trip (the guard can fail)");
        printf("      partial region y=%.0f h=%.0f: pre-fix displacement %.0f screen rows\n",
               y, h, worstBrokenError);
        /* H - 2y - h = 720 - 264 - 426 = 30 */
        CHECK(NearlyEqual(worstBrokenError, H - 2.0f * y - h),
              "the pre-fix displacement is exactly H - 2y - h rows");
    }

    /* The whole frame is its own mirror, which is why the bug could hide: both formulas
       agree here, and this is the only region the snapshot was ever exercised with. */
    {
        float fixed = SoftDepthDestY(H, 0.0f, H);
        float broken = SoftDepthDestY_Broken(0.0f);
        CHECK(NearlyEqual(fixed, broken) && NearlyEqual(fixed, 0.0f),
              "a full-frame region is unaffected — the old code was right for that one case");
    }

    /* A region flush against the bottom of the frame maps to the top of the target. */
    {
        const float y = 400.0f, h = 320.0f;   /* y + h == H */
        CHECK(NearlyEqual(SoftDepthDestY(H, y, h), 0.0f),
              "a bottom-edge region lands at the top of the downscaled target");
    }

    /* Pin the load-bearing expression so the C mirror above cannot silently drift from
       the code it models. The mirror cannot validate the GPU blit itself — only that the
       destination rectangle is built from the region's MIRRORED y. */
    CHECK(Has("core/screen_distort.c",
              "(float)renderTex.texture.height - regionBottom"),
          "SnapshotDepth places the block at the region's mirrored Y");
    CHECK(Has("core/screen_distort.c", "mirroredY / SOFT_DEPTH_DOWNSCALE"),
          "the mirrored Y is what reaches the destination rectangle");
    CHECK(Has("core/screen_distort.c", "-s_softDepthRegion.height"),
          "the source block is still read bottom-to-top, which the mirror assumes");

    return failures ? 1 : 0;
}

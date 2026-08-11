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
    return 0.16f * (1.0f - expf(-traversedPath / 1.20f));
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

/* Mirror of the wave perturbation, both ways round. `bias` reproduces the
 * defect: a perturbation vector with a constant 1.0 in world Y, added straight
 * to a world normal with no tangent-plane projection. */
static void PerturbNormal(float n[3], float dhx, float dhy, int projected, float out[3])
{
    float w[3] = { -dhx, projected ? 0.0f : 1.0f, -dhy };
    if (projected)
    {
        float d = w[0]*n[0] + w[1]*n[1] + w[2]*n[2];
        w[0] -= n[0]*d; w[1] -= n[1]*d; w[2] -= n[2]*d;
    }
    float r[3] = { n[0] + w[0]*0.045f, n[1] + w[1]*0.045f, n[2] + w[2]*0.045f };
    float len = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    out[0] = r[0]/len; out[1] = r[1]/len; out[2] = r[2]/len;
}

static float Dot3(const float a[3], const float b[3])
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

/* Mirror of the minimum-gradient pick in the normal reconstruction.
 *
 * `substitute` reproduces the defect: a missing neighbour was replaced with the
 * CENTRE's own depth, which puts it at the same distance and therefore gives it
 * a z-difference of ~0 — so the minimum-|z| rule picked the fabricated sample
 * over the real one, every time. Returns the chosen gradient's z-component;
 * 0 means "flat along this axis", i.e. the normal forced to face the camera. */
static float PickGradientZ(float dzLeft, float dzRight, int hasLeft, int hasRight,
                           int substitute)
{
    if (substitute)
    {
        /* The fabricated side sits at the centre's depth: dz = 0. */
        float a = hasLeft ? dzLeft : 0.0f;
        float b = hasRight ? dzRight : 0.0f;
        return fabsf(a) < fabsf(b) ? a : b;
    }
    if (hasLeft && hasRight) return fabsf(dzLeft) < fabsf(dzRight) ? dzLeft : dzRight;
    if (hasLeft) return dzLeft;
    if (hasRight) return dzRight;
    return 0.0f;
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
    /* The DENSEST body the tree authors must still sit clear of the cap, or the
     * gradient is gone again: the water ring's thickest ray accumulates about
     * 3.26 x 0.40 m of chord (coverage ratio x tube traversal). Debug view 2
     * caught the previous knee failing exactly this. */
    {
        float ringCore = DecodeOpticalThickness(3.26f * 0.40f * 16.0f);
        printf("      water ring's densest ray: %.4f m (%.0f%% of the cap)\n",
               ringCore, ringCore / 0.16f * 100.0f);
        CHECK(ringCore < 0.16f * 0.70f);
    }

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

    /* The wave perturbation must be a TANGENTIAL tilt and nothing else. With the
     * old (-dh.x, 1.0, -dh.y) vector, a surface whose normal is not world-up got
     * a free push toward +Y even where the wave field was flat — a constant bias
     * modulated by a sine field, which paints its iso-lines onto the body as the
     * parallel ridges seen in the sandbox. */
    {
        float wall[3] = { 1.0f, 0.0f, 0.0f };          /* a vertical surface */
        float flatWave[2] = { 0.0f, 0.0f };            /* no wave at this point */
        float biased[3], projected[3];
        PerturbNormal(wall, flatWave[0], flatWave[1], 0, biased);
        PerturbNormal(wall, flatWave[0], flatWave[1], 1, projected);
        printf("      flat wave on a vertical surface: biased n.y = %.4f  projected n.y = %.4f\n",
               biased[1], projected[1]);
        CHECK(fabsf(biased[1]) > 0.04f);        /* the defect: a tilt from nothing */
        CHECK(fabsf(projected[1]) < 0.0001f);   /* the fix: no wave, no tilt */

        /* And with a real wave, the perturbation still tilts the surface — the
         * fix must not simply disable it. */
        float tilted[3];
        PerturbNormal(wall, 0.5f, 0.3f, 1, tilted);
        CHECK(1.0f - Dot3(tilted, wall) > 1.0e-5f);
    }

    /* The silhouette's normal. A missing neighbour must be EXCLUDED from the
     * minimum-gradient pick, never substituted with the centre's depth: the
     * substitute has a zero z-difference, so it wins the minimum every time and
     * flattens the gradient along that axis. Whether left/right or up/down is
     * the missing one changes along the edge, so the normal flips between real
     * and view-facing — vertical stripes from the x axis, horizontal from the y. */
    {
        const float realSlope = 0.08f;   /* the surface genuinely tilts here */
        /* Interior: both sides present, the smaller gradient wins as designed. */
        CHECK(fabsf(PickGradientZ(realSlope, 0.02f, 1, 1, 0) - 0.02f) < 1e-6f);
        CHECK(fabsf(PickGradientZ(realSlope, 0.02f, 1, 1, 1) - 0.02f) < 1e-6f);
        /* Edge, right side missing: the real left gradient must be used... */
        float excluded = PickGradientZ(realSlope, 0.0f, 1, 0, 0);
        float substituted = PickGradientZ(realSlope, 0.0f, 1, 0, 1);
        printf("      edge gradient, right neighbour missing: excluded %.4f  substituted %.4f\n",
               excluded, substituted);
        CHECK(fabsf(excluded - realSlope) < 1e-6f);
        /* ...and the old substitution threw it away for a flat one. */
        CHECK(fabsf(substituted) < 1e-6f);
        /* Both sides missing is the only case with no answer; flat is honest. */
        CHECK(fabsf(PickGradientZ(realSlope, 0.02f, 0, 0, 0)) < 1e-6f);
    }

    /* Anti-drift: the shader must still carry the load-bearing expressions. */
    char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
    if (!shader) {
        printf("FAIL: cannot read core/fluid/shaders/fluid_surface.fs\n");
        bad++;
    } else {
        CHECK(strstr(shader, "accumulatedPath / FLUID_KERNEL_OVERLAP") != NULL);
        CHECK(strstr(shader, "exp(-traversedPath / 1.20)") != NULL);
        CHECK(strstr(shader, "min(kernelThickness, max(0.022, depthGap * 1.25))") != NULL);
        CHECK(strstr(shader, "return vec3(-dh.x, 0.0, -dh.y);") != NULL);
        CHECK(strstr(shader, "waveSlope -= worldNormal * dot(waveSlope, worldNormal);") != NULL);
        /* The tangent-space normal that was being added to a world normal. */
        CHECK(strstr(shader, "vec3(-dh.x, 1.0") == NULL);
        /* The regression itself (0262068): the receiver creating a column. */
        CHECK(strstr(shader, "max(kernelThickness, min(0.40") == NULL);
        /* The missing neighbour must be excluded, not substituted. */
        CHECK(strstr(shader, "bool hasL = depthLeft < 0.99999;") != NULL);
        CHECK(strstr(shader, "depthLeft >= 0.99999 ? fluidDepth") == NULL);
        /* The caustic lattice added into refractedScene: a sin*sin field cubed
         * is a thin-bright-line pattern, and it painted contour bands onto the
         * body. Caustics belong on the receiver, not in this term. */
        CHECK(strstr(shader, "refractedScene += ") == NULL);
        /* Nothing in the composite may quantise world position into cells: a
         * term that is constant across a cell renders as an axis-aligned square,
         * never as the point-like glint it was meant to be. */
        CHECK(strstr(shader, "floor(worldPosition") == NULL);
        free(shader);
    }

    printf("%s: fluid_surface_optics_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}

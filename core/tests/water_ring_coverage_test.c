// core headless test — the water ring's splat budget must actually close a surface.
//
// Screen-space fluid reconstructs a surface from overlapping splats. Below an
// overlap of ~1 the body stops being a body: the filter cannot bridge the gaps,
// the normals go per-splat, and it renders as a cluster of beads. That threshold
// is arithmetic — population, kernel radius and the emitter mesh's area — so it
// is checkable here rather than by eye after a build.
//
// The numbers mirror core/composition/water/water_ring.inl. They are asserted to
// still be in the source, because a mirror that silently drifts is worse than no
// test: it would keep passing while the effect it describes fell apart.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#ifndef PI
#define PI 3.1415926535f
#endif

#define TUBE_RATIO   0.22f      /* WATER_RING_TUBE_RATIO */
#define KERNEL_RATIO 0.095f     /* kernel = radius * this */

/* Splat area over emitter-surface area. Both scale with radius^2, so the ratio
 * is scale-invariant — which is why the fixture may pick any ring size. */
static float CoverageRatio(float ringRadius, float alive)
{
    float tube = ringRadius * TUBE_RATIO;
    float surface = (2.0f*PI*ringRadius) * (2.0f*PI*tube);
    float kernel = ringRadius * KERNEL_RATIO;
    return (alive * PI * kernel * kernel) / surface;
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

    /* Tier populations from the .inl, at full density (t01 = 1). */
    const float high = 1000.0f, med = 700.0f, low = 460.0f;

    float rHigh = CoverageRatio(0.9f, high);
    float rMed  = CoverageRatio(0.9f, med);
    float rLow  = CoverageRatio(0.9f, low);
    printf("      coverage ratio  HIGH %.2f  MED %.2f  LOW %.2f\n", rHigh, rMed, rLow);

    /* Every tier must close the surface, LOW included: thinning the body is a
     * legitimate quality cut, opening holes in it is not. */
    CHECK(rLow > 1.0f);
    CHECK(rMed > rLow && rHigh > rMed);

    /* Scale invariance — the fixture picks the ring size, and it must not be
     * able to break the body by picking a big one. */
    CHECK(fabsf(CoverageRatio(0.3f, high) - CoverageRatio(3.0f, high)) < 0.001f);

    /* Half density (t01 = 0.55 floor in the .inl) still has to hold together,
     * or fading the effect in would tear it apart on the way. */
    CHECK(CoverageRatio(0.9f, low * 0.55f) > 0.80f);

    char *inl = ReadFile("core/composition/water/water_ring.inl");
    if (!inl) { printf("FAIL: cannot read water_ring.inl\n"); bad++; }
    else
    {
        CHECK(strstr(inl, "#define WATER_RING_TUBE_RATIO 0.22f") != NULL);
        CHECK(strstr(inl, "radius * 0.095f") != NULL);
        CHECK(strstr(inl, "1000.0f") != NULL && strstr(inl, "700.0f") != NULL && strstr(inl, "460.0f") != NULL);
        /* Viscosity is what makes neighbouring splats travel together; without
         * it the reconstruction has nothing to merge and the ratio above stops
         * meaning anything. */
        CHECK(strstr(inl, "FORCE_VISCOSITY") != NULL);
        /* The shape must come from the mesh emitter, not a formula. */
        CHECK(strstr(inl, "MeshAdjacency_SampleEdge") != NULL);
        /* SSF only — a billboard pass would hide what this effect is for. */
        CHECK(strstr(inl, "PARTICLE_RENDER_SURFACE_INPUT") != NULL);
        free(inl);
    }

    printf("%s: water_ring_coverage_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}

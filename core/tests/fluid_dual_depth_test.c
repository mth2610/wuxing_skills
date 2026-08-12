// core headless test — dual-depth thickness arithmetic
// (core/fluid/shaders/fluid_capture_particle{,_back}.fs + fluid_thickness_resolve.fs).
//
// Thickness used to be an additive sum of per-splat sphere chords, divided by an
// invented overlap factor and squeezed through an invented saturating knee. It is
// now a measured path: rasterize the FAR root of every splat with a MAX reduction,
// the near root with the usual MIN, and subtract. This mirrors that arithmetic
// numerically and asserts the shaders still carry the load-bearing expressions.
//
// What it CANNOT validate, and what the sandbox fixtures are for (NEW FX tab:
// WATER RING for a hollow shell, FLUID IMPACT for a dense body): that the
// rasterizer actually reduces with MAX on the back target, that the two passes
// see the same particle set, and anything about final colour. The complement-
// depth trick in particular is a claim about the DEPTH TEST, which no CPU mirror
// can exercise — the string assertions below are the only guard against it being
// silently edited out.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

/* The analytic sphere profile shared by the front and back capture shaders.
 * They MUST use the same one: their difference is only a chord if both roots
 * belong to the same surface. */
static float SphereZ(float r2)
{
    return sqrtf(fmaxf(0.0f, 1.0f - r2 * 0.90f)) * (1.0f - r2 * 0.10f);
}

/* View-space Z of a splat's near and far surface. +Z points at the eye, so the
 * near root is centre + sphereZ*r and the far root is centre - sphereZ*r. */
static float FrontViewZ(float centreZ, float radius, float r2)
{ return centreZ + SphereZ(r2) * radius; }
static float BackViewZ(float centreZ, float radius, float r2)
{ return centreZ - SphereZ(r2) * radius; }

/* fluid_thickness_resolve.fs works in positive distances from the eye. */
static float Distance(float viewZ) { return -viewZ; }
static float Thickness(float frontViewZ, float backViewZ)
{
    float t = Distance(backViewZ) - Distance(frontViewZ);
    return t > 0.0f ? t : 0.0f;
}

/* The MIN/MAX reduction the two depth targets perform over overlapping splats.
 * The back pass gets there by writing 1 - depth and letting the ordinary
 * depth test keep the smallest, which is what these two helpers model:
 * nearest-to-eye wins in front, farthest-from-eye wins behind. */
static float ReduceFront(const float *viewZ, int n)
{
    float best = viewZ[0];
    for (int i = 1; i < n; i++) if (viewZ[i] > best) best = viewZ[i];  /* nearer = larger Z */
    return best;
}
static float ReduceBack(const float *viewZ, int n)
{
    float best = viewZ[0];
    for (int i = 1; i < n; i++) if (viewZ[i] < best) best = viewZ[i];  /* farther = smaller Z */
    return best;
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
    const float radius = 0.06f;      /* one authored splat */
    const float centre = -4.0f;      /* four metres in front of the eye */

    /* ---- A single splat, dead centre: the thickness IS the sphere diameter. */
    {
        float front = FrontViewZ(centre, radius, 0.0f);
        float back = BackViewZ(centre, radius, 0.0f);
        float t = Thickness(front, back);
        printf("      one splat, centre ray: %.4f m (diameter %.4f m)\n", t, 2.0f * radius);
        CHECK(fabsf(t - 2.0f * radius) < 1e-5f);
    }

    /* ---- The same splat toward its rim: the chord SHRINKS, monotonically.
     *
     * It does not close to zero, and asserting that it did was this test's first
     * mistake. The capture profile is sqrt(1 - r2*0.90) * (1 - r2*0.10), not a
     * true sphere: it deliberately keeps a floor at the disc edge so overlapping
     * splats do not meet at a depth cliff (fluid_capture_particle.fs). A true
     * sphere would end at zero; this surface ends at 57% of the diameter, and
     * that is the reason a one-splat rim still carries visible thickness.
     * Assert what the profile promises — a monotone falloff bounded by the
     * diameter — not what a textbook sphere would. */
    {
        float previous = 2.0f * radius + 1.0f;
        for (int i = 0; i <= 100; i++) {
            float r2 = (float)i / 100.0f;
            float t = Thickness(FrontViewZ(centre, radius, r2), BackViewZ(centre, radius, r2));
            CHECK(t <= previous + 1e-6f);
            CHECK(t <= 2.0f * radius + 1e-6f);
            previous = t;
        }
        float rim = Thickness(FrontViewZ(centre, radius, 1.0f), BackViewZ(centre, radius, 1.0f));
        printf("      one splat, rim ray: %.4f m (%.0f%% of the diameter)\n",
               rim, rim / (2.0f * radius) * 100.0f);
        CHECK(rim > 0.0f);
        CHECK(rim < 0.70f * (2.0f * radius));
    }

    /* ---- Thickness is never negative anywhere across the splat. */
    for (int i = 0; i <= 100; i++) {
        float r2 = (float)i / 100.0f;
        CHECK(Thickness(FrontViewZ(centre, radius, r2), BackViewZ(centre, radius, r2)) >= 0.0f);
    }

    /* ---- A DENSE column: five splats strung along the view ray. The envelope
     * spans the nearest front root to the farthest back root, so thickness grows
     * with the column's extent — no overlap factor to divide out. */
    {
        float fronts[5], backs[5];
        for (int i = 0; i < 5; i++) {
            float z = centre - (float)i * radius;   /* each one deeper than the last */
            fronts[i] = FrontViewZ(z, radius, 0.0f);
            backs[i] = BackViewZ(z, radius, 0.0f);
        }
        float t = Thickness(ReduceFront(fronts, 5), ReduceBack(backs, 5));
        float expected = 4.0f * radius + 2.0f * radius;   /* span + one diameter */
        printf("      five stacked splats: %.4f m (expected %.4f m)\n", t, expected);
        CHECK(fabsf(t - expected) < 1e-5f);
        /* And it must exceed the single-splat case, which is the whole reason a
         * dense body can now read as thicker than a thin one. */
        CHECK(t > 2.0f * radius);
    }

    /* ---- A SHELL: two splats separated by empty space, as a surface-spawning
     * emitter (core/composition/water/water_ring.inl) produces. Dual depth
     * measures the ENVELOPE, so it reports the gap as liquid. That is a real
     * property of the method and is asserted here so it cannot be mistaken for a
     * regression later: the water ring's tube reads as its full diameter. */
    {
        float gap = 0.20f;
        float fronts[2] = { FrontViewZ(centre, radius, 0.0f),
                            FrontViewZ(centre - gap, radius, 0.0f) };
        float backs[2] = { BackViewZ(centre, radius, 0.0f),
                           BackViewZ(centre - gap, radius, 0.0f) };
        float t = Thickness(ReduceFront(fronts, 2), ReduceBack(backs, 2));
        printf("      hollow shell across a %.2f m gap: %.4f m\n", gap, t);
        CHECK(fabsf(t - (gap + 2.0f * radius)) < 1e-5f);
        /* The material actually crossed is two diameters; the envelope is more.
         * Measured, not argued — both sandbox fixtures were compared under
         * debug view 2 before the accumulation path was deleted. */
        CHECK(t > 4.0f * radius);
    }

    /* ---- Empty pixels. The two targets clear in OPPOSITE directions (front to
     * 1 = nothing in front, back to 0 = nothing behind), so a pixel the cloud
     * never touched must resolve to exactly zero rather than to the near plane.
     * Getting this backwards fills the whole screen with fluid. */
    {
        char *resolve = ReadFile("core/fluid/shaders/fluid_thickness_resolve.fs");
        if (!resolve) { printf("FAIL: cannot read fluid_thickness_resolve.fs\n"); bad++; }
        else {
            CHECK(strstr(resolve, "front >= 0.99999 || back <= 0.000001") != NULL);
            CHECK(strstr(resolve, "ViewDistance(back) - ViewDistance(front)") != NULL);
            free(resolve);
        }
        char *host = ReadFile("core/fluid/fluid_surface.c");
        if (!host) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else {
            CHECK(strstr(host, "BeginTextureMode(s_captureBack); ClearBackground(BLANK)") != NULL);
            CHECK(strstr(host, "BeginTextureMode(s_capture); ClearBackground((Color){255,0,0,0})") != NULL);
            /* Real geometry needs front-face culling to expose its far side. */
            CHECK(strstr(host, "rlSetCullFace(RL_CULL_FACE_FRONT)") != NULL);
            free(host);
        }
    }

    /* ---- Anti-drift on the capture pair: same profile, opposite Z sign, and
     * the back pass writing the complement of the depth. The complement is the
     * whole MAX reduction; nothing else in the codebase performs it. */
    {
        char *front = ReadFile("core/fluid/shaders/fluid_capture_particle.fs");
        char *back = ReadFile("core/fluid/shaders/fluid_capture_particle_back.fs");
        if (!front || !back) { printf("FAIL: cannot read the capture shader pair\n"); bad++; }
        else {
            const char *profile = "sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10)";
            CHECK(strstr(front, profile) != NULL);
            CHECK(strstr(back, profile) != NULL);
            CHECK(strstr(front, "vec3(v_offsetView, sphereZ * v_depthRadius)") != NULL);
            CHECK(strstr(back, "vec3(v_offsetView, -sphereZ * v_depthRadius)") != NULL);
            CHECK(strstr(back, "gl_FragDepth = 1.0 - depth;") != NULL);
            CHECK(strstr(front, "gl_FragDepth = 1.0 - depth;") == NULL);
        }
        free(front); free(back);
    }

    /* ---- The composite must consume metres directly. A decode reappearing here
     * means the invented constants came back. */
    {
        char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
        if (!shader) { printf("FAIL: cannot read fluid_surface.fs\n"); bad++; }
        else {
            CHECK(strstr(shader, "return max(measuredThicknessMetres, 0.0);") != NULL);
            CHECK(strstr(shader, "FLUID_KERNEL_OVERLAP") == NULL);
            CHECK(strstr(shader, "exp(-traversedPath") == NULL);
            /* Absorption is now derived from a stated reference depth. */
            CHECK(strstr(shader, "-log(materialTransmission) / FLUID_REFERENCE_DEPTH_M") != NULL);
            free(shader);
        }
    }

    printf(bad ? "fluid_dual_depth: FAIL (%d)\n" : "fluid_dual_depth: PASS\n", bad);
    return bad ? 1 : 0;
}

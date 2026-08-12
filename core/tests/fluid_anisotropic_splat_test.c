// core headless test — velocity-aligned anisotropic splats
// (core/particles/shaders/gpu/fluid_surface_capture.vs + core/fluid/shaders/fluid_pbd_surface.vs).
//
// Isotropic splats render a thin film or a rim as a string of beads. The
// published fix (Yu & Turk 2013) fits the kernel by PCA over each particle's
// neighbours, which this architecture has no neighbour search for; the splats
// are stretched along VELOCITY instead, as a proxy for the direction a coherent
// sheet is stretched in.
//
// The proxy's magnitude is a stated art parameter, not a measurement — so what
// is guarded here is the arithmetic around it: the ellipsoid's volume is
// unchanged (the one part that IS from the paper), the aspect ratio is capped,
// and both vertex stages build the same ellipsoid. Two stages feeding one
// fragment stage that disagreed on the kernel would put the PBD crown and the
// particle streams on different surfaces.
//
// It cannot judge whether the beads are gone; that is the sandbox (NEW FX tab,
// FLUID IMPACT for a crown rim, WATER RING for a swirling tube).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#ifndef PI
#define PI 3.1415926535f
#endif

#define FLUID_ANISO_REFERENCE_SPEED 3.0f
#define FLUID_ANISO_MAX_ASPECT 3.0f

/* Mirror of the aspect the two vertex stages compute. */
static float Aspect(float speed, float anisotropy)
{
    float a = 1.0f + anisotropy * speed / FLUID_ANISO_REFERENCE_SPEED;
    if (a < 1.0f) a = 1.0f;
    if (a > FLUID_ANISO_MAX_ASPECT) a = FLUID_ANISO_MAX_ASPECT;
    return a;
}
static float CrossAxis(float aspect) { return 1.0f / sqrtf(aspect); }

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

    /* ---- A particle at rest is exactly the sphere it was before. Anisotropy
     * that changed a still body would be a regression dressed as a feature. */
    {
        CHECK(fabsf(Aspect(0.0f, 1.0f) - 1.0f) < 1e-6f);
        CHECK(fabsf(CrossAxis(Aspect(0.0f, 1.0f)) - 1.0f) < 1e-6f);
    }

    /* ---- Raising the reference speed is how the effect is dialled back; at
     * zero strength it must reduce exactly to the old isotropic splat. */
    for (float speed = 0.0f; speed <= 20.0f; speed += 0.5f)
        CHECK(fabsf(Aspect(speed, 0.0f) - 1.0f) < 1e-6f);

    /* ---- VOLUME is preserved: aspect * cross^2 == 1. This is the part taken
     * from Yu & Turk's determinant normalization, and it is what stops the
     * surface swelling wherever the fluid happens to be moving fast. */
    for (float speed = 0.0f; speed <= 20.0f; speed += 0.25f)
    {
        float a = Aspect(speed, 1.0f), c = CrossAxis(a);
        CHECK(fabsf(a * c * c - 1.0f) < 1e-5f);
    }

    /* ---- The cap holds however fast the particle goes. An unbounded aspect
     * turns splats into streaks and reopens the coverage question
     * water_ring_coverage_test guards. */
    {
        CHECK(fabsf(Aspect(1.0e6f, 1.0f) - FLUID_ANISO_MAX_ASPECT) < 1e-4f);
        float reference = Aspect(FLUID_ANISO_REFERENCE_SPEED, 1.0f);
        printf("      aspect at one reference speed: %.2f   at the cap: %.2f\n",
               reference, Aspect(1.0e6f, 1.0f));
        CHECK(fabsf(reference - 2.0f) < 1e-5f);   /* one reference speed = +1 aspect */
    }

    /* ---- Projected coverage never DROPS. The screen ellipse is
     * (r*aspect) x (r*cross), so its area is pi*r^2*sqrt(aspect) — a
     * volume-preserving cigar seen side-on covers more screen than the sphere,
     * never less. Coverage can therefore only improve, which is why
     * water_ring_coverage_test's threshold still holds under anisotropy. */
    {
        float previous = 1.0f;
        for (float speed = 0.0f; speed <= 12.0f; speed += 0.5f)
        {
            float a = Aspect(speed, 1.0f);
            float area = a * CrossAxis(a);            /* in units of pi*r^2 */
            CHECK(area >= 1.0f - 1e-6f);
            CHECK(area >= previous - 1e-6f);
            previous = area;
        }
        printf("      projected area at the cap: %.3f x the isotropic splat\n", previous);
    }

    /* ---- Both vertex stages must build the SAME ellipsoid. They feed the same
     * two fragment stages, so a disagreement puts the PBD crown and the
     * particle streams on different surfaces with no error anywhere. */
    {
        char *particleVS = ReadFile("core/particles/shaders/gpu/fluid_surface_capture.vs");
        char *pbdVS = ReadFile("core/fluid/shaders/fluid_pbd_surface.vs");
        if (!particleVS || !pbdVS) { printf("FAIL: cannot read the vertex stages\n"); bad++; }
        else
        {
            const char *needles[] = {
                "#define FLUID_ANISO_REFERENCE_SPEED 3.0",
                "#define FLUID_ANISO_MAX_ASPECT 3.0",
                "inversesqrt(aspect)",
            };
            for (int i = 0; i < 3; ++i)
            {
                CHECK(strstr(particleVS, needles[i]) != NULL);
                CHECK(strstr(pbdVS, needles[i]) != NULL);
            }
            /* The stretch axis comes from the view-plane velocity in both. */
            CHECK(strstr(particleVS, "mat3(u_view)") != NULL);
            CHECK(strstr(pbdVS, "mat3(u_view)") != NULL);
            /* And both must emit the varyings the fragment stages read. */
            CHECK(strstr(particleVS, "v_offsetView") != NULL && strstr(particleVS, "v_depthRadius") != NULL);
            CHECK(strstr(pbdVS, "v_offsetView") != NULL && strstr(pbdVS, "v_depthRadius") != NULL);
        }
        free(particleVS); free(pbdVS);
    }

    /* ---- FluidSurface_RegisterEllipsoid's three radii must reach the raster.
     * The API took a Vector3 and the capture averaged it into one scalar, so
     * the signature promised anisotropy the renderer never drew. */
    {
        char *host = ReadFile("core/fluid/fluid_surface.c");
        if (!host) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else
        {
            CHECK(strstr(host, "rlScalef(sp->radii.x, sp->radii.y, sp->radii.z)") != NULL);
            CHECK(strstr(host, "(sp->radii.x + sp->radii.y + sp->radii.z) / 3.0f") == NULL);
            free(host);
        }
    }

    printf(bad ? "fluid_anisotropic_splat: FAIL (%d)\n" : "fluid_anisotropic_splat: PASS\n", bad);
    return bad ? 1 : 0;
}

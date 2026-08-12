// core headless test — the SSF capture and the SSF composite must use ONE frustum.
//
// The defect this guards, found 2026-08-12 by the LIQUID BENCH fixture:
// FluidSurface_Capture rasterized through raylib's BeginMode3D, whose projection
// comes from RL_CULL_DISTANCE_NEAR = 0.01, while FluidSurface_MakeProjection —
// the matrix the composite INVERTS to turn captured depth back into a view
// position — uses near = 1.0 (matching main.c's MyBeginMode3D, which documents
// why: below ~1.0 this project's rlFrustum renders blank).
//
// Only the CPU ellipsoid path (FluidSurface_RegisterParticle/RegisterEllipsoid)
// lets the RASTERIZER produce depth, so only it was affected; the GPU splat
// paths compute depth from their own near=1.0 projection and write gl_FragDepth.
// That path had no fixture, so the defect was invisible for as long as it
// existed. Symptom: every pixel of the body failed the composite's
// scene-occlusion test and was discarded — a body that provably registered at
// the right world position rendered nothing at all.
//
// What this mirrors: the device-depth encode/decode round trip, which is pure
// arithmetic. What it CANNOT validate: that fluid_surface.c actually calls
// rlFrustum rather than BeginMode3D — that is asserted against the source text
// below — nor anything about rasterization, culling or the composite itself.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

/* Device depth [0,1] of a view-space distance under a GL frustum. Mirrors what
 * the rasterizer stores in the capture's R channel via gl_FragCoord.z. */
static double EncodeDeviceDepth(double distance, double zNear, double zFar)
{
    double zEye = -distance;
    double ndc = (zFar + zNear) / (zFar - zNear) + (2.0 * zFar * zNear) / ((zFar - zNear) * zEye);
    return ndc * 0.5 + 0.5;
}

/* The inverse the composite performs (ReconstructViewPosition, then -z). */
static double DecodeViewDistance(double deviceDepth, double zNear, double zFar)
{
    double ndc = deviceDepth * 2.0 - 1.0;
    double a = (zFar + zNear) / (zFar - zNear);
    double b = (2.0 * zFar * zNear) / (zFar - zNear);
    return -(b / (ndc - a));
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    int bad = 0;
    const double kNear = 1.0, kFar = 1000.0;

    /* 1. Matched frustums round-trip. This is the property the fix restores. */
    const double probes[] = { 2.0, 5.0, 7.5, 12.0, 40.0 };
    for (int i = 0; i < (int)(sizeof(probes)/sizeof(probes[0])); ++i)
    {
        double d = probes[i];
        double back = DecodeViewDistance(EncodeDeviceDepth(d, kNear, kFar), kNear, kFar);
        CHECK(fabs(back - d) < 1e-6 * d);
    }

    /* 2. The MISMATCH is catastrophic, not a rounding nuisance — the reason the
     *    body was discarded rather than merely mispositioned. A body at 7.5 m
     *    written under near=0.01 and read under near=1.0 lands past 400 m. */
    {
        double written = EncodeDeviceDepth(7.5, 0.01, kFar);
        double read = DecodeViewDistance(written, kNear, kFar);
        CHECK(read > 100.0);
        printf("      near-plane mismatch: 7.5 m written at near=0.01 decodes to %.1f m\n", read);
    }

    /* 3. And it stays under the composite's "no fluid here" test (>= 0.99999),
     *    which is why the pixels were shaded at all instead of being skipped as
     *    empty — they reached the scene-occlusion discard and died there. */
    {
        double written = EncodeDeviceDepth(7.5, 0.01, kFar);
        CHECK(written < 0.99999);
        CHECK(written > 0.99);
    }

    /* 4. The capture must not go through raylib's BeginMode3D. Source-text
     *    assertion: the arithmetic above cannot see which API the C file calls,
     *    and reverting to BeginMode3D would silently restore the whole defect. */
    {
        char *src = ReadFile("core/fluid/fluid_surface.c");
        if (!src) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else
        {
            CHECK(strstr(src, "FluidSurface_BeginCaptureMode3D") != NULL);
            CHECK(strstr(src, "rlFrustum(-right, right, -top, top, 1.0, 1000.0)") != NULL);
            /* No bare BeginMode3D( call may remain in the capture path. */
            const char *p = src, *hit = NULL;
            while ((p = strstr(p, "BeginMode3D(camera)")) != NULL) { hit = p; p += 1; }
            CHECK(hit == NULL);
            free(src);
        }
    }

    /* 5. rlPushMatrix does not hand back an identity — it redirects writes to a
     *    persistent global and saves whatever that already held. The CPU
     *    ellipsoid draw must state the identity itself. (Measured: the global
     *    held a leftover VIEW matrix, so every sphere was view-transformed twice
     *    and the bodies left the screen.) */
    {
        char *src = ReadFile("core/fluid/fluid_surface.c");
        if (!src) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else
        {
            const char *draw = strstr(src, "FluidSurface_DrawEllipsoid(const");
            CHECK(draw != NULL);
            if (draw)
            {
                const char *push = strstr(draw, "rlPushMatrix();");
                const char *ident = push ? strstr(push, "rlLoadIdentity();") : NULL;
                const char *translate = push ? strstr(push, "rlTranslatef(") : NULL;
                CHECK(ident != NULL);
                /* And it must come BEFORE the translate, or it erases it. */
                CHECK(ident != NULL && translate != NULL && ident < translate);
            }
            free(src);
        }
    }

    printf(bad ? "fluid_capture_projection_test: %d failure(s)\n"
               : "PASS: fluid_capture_projection_test (0 failures)\n", bad);
    return bad ? 1 : 0;
}

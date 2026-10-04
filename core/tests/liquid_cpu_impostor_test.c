#include "core/liquid/liquid_capture_cpu.h"
#include <math.h>
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); bad++; } } while (0)

static Matrix Identity(void)
{
    Matrix m = {0};
    m.m0 = m.m5 = m.m10 = m.m15 = 1.0f;
    return m;
}

static Matrix Perspective(void)
{
    Matrix m = {0};
    m.m0 = 1.35f; m.m5 = 2.40f;
    m.m10 = -1.002002f; m.m11 = -1.0f; m.m14 = -0.2002002f;
    return m;
}

static Matrix Orthographic(void)
{
    Matrix m = Identity();
    m.m0 = 0.20f; m.m5 = 0.30f; m.m10 = -0.02f; m.m14 = -1.0f;
    return m;
}

static int CheckProjectedSurface(Vector3 center, Vector3 radii,
                                 Matrix view, Matrix projection)
{
    int bad = 0;
    Vector4 bounds;
    CHECK(LiquidCaptureCPU_ProjectBounds(center, radii, view, projection, &bounds));
    for (int latitude = 0; latitude <= 24; ++latitude) {
        float phi = 3.14159265358979323846f * (float)latitude / 24.0f;
        for (int longitude = 0; longitude < 48; ++longitude) {
            float theta = 6.28318530717958647692f * (float)longitude / 48.0f;
            Vector4 p = {center.x + radii.x*sinf(phi)*cosf(theta),
                         center.y + radii.y*cosf(phi),
                         center.z + radii.z*sinf(phi)*sinf(theta), 1.0f};
            p = LiquidCaptureCPU_Transform4(view, p);
            p = LiquidCaptureCPU_Transform4(projection, p);
            if (p.w <= 0.0f) continue;
            float x = p.x/p.w, y = p.y/p.w, z = p.z/p.w;
            if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f ||
                z < -1.0f || z > 1.0f) continue;
            CHECK(x >= bounds.x-1e-5f && x <= bounds.z+1e-5f);
            CHECK(y >= bounds.y-1e-5f && y <= bounds.w+1e-5f);
        }
    }
    return bad;
}

int main(void)
{
    int bad = 0;
    float nearRoot, farRoot;
    Vector3 direction = {0,0,-1};
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){0}, direction,
        (Vector3){0,0,-5}, (Vector3){1,1,1}, &nearRoot, &farRoot));
    CHECK(fabsf(nearRoot-4.0f)<1e-5f && fabsf(farRoot-6.0f)<1e-5f);
    CHECK(!LiquidCaptureCPU_RayEllipsoid((Vector3){2,0,0}, direction,
        (Vector3){0,0,-5}, (Vector3){1,1,1}, &nearRoot, &farRoot));
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){1,0,0}, direction,
        (Vector3){0,0,-5}, (Vector3){1,1,1}, &nearRoot, &farRoot));
    CHECK(fabsf(nearRoot-5.0f)<1e-5f && fabsf(farRoot-5.0f)<1e-5f);
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){0}, direction,
        (Vector3){0,0,-5}, (Vector3){0.5f,0.2f,2}, &nearRoot, &farRoot));
    CHECK(fabsf(nearRoot-3.0f)<1e-5f && fabsf(farRoot-7.0f)<1e-5f);
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){0}, direction,
        (Vector3){0}, (Vector3){1,1,1}, &nearRoot, &farRoot));
    CHECK(nearRoot < 0.0f && farRoot > 0.0f);
    /* A centimetre-scale kernel at the gameplay camera distance must retain its
     * chord; the direct B*B-A*C quadratic loses precision in this case. */
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){0}, direction,
        (Vector3){0,0,-20}, (Vector3){0.01f,0.01f,0.01f}, &nearRoot, &farRoot));
    CHECK(fabsf((farRoot-nearRoot)-0.02f)<1e-5f);
    CHECK(!LiquidCaptureCPU_RayEllipsoid((Vector3){0}, direction,
        (Vector3){0,0,-5}, (Vector3){1,0,1}, &nearRoot, &farRoot));

    Matrix identity = Identity(), perspective = Perspective();
    /* Inverses of the two known camera projections. The production quad
     * preparation calls this same unprojection helper at NDC depth zero. */
    Matrix inversePerspective = {0};
    inversePerspective.m0 = 1.0f/perspective.m0;
    inversePerspective.m5 = 1.0f/perspective.m5;
    inversePerspective.m11 = 1.0f/perspective.m14;
    inversePerspective.m14 = -1.0f;
    inversePerspective.m15 = perspective.m10/perspective.m14;
    Vector3 unprojected;
    CHECK(LiquidCaptureCPU_UnprojectNDC(inversePerspective,
        (Vector3){0.6f,-0.4f,0},&unprojected));
    Vector4 reprojected = LiquidCaptureCPU_Transform4(perspective,
        (Vector4){unprojected.x,unprojected.y,unprojected.z,1});
    CHECK(fabsf(reprojected.x/reprojected.w-0.6f)<1e-5f);
    CHECK(fabsf(reprojected.y/reprojected.w+0.4f)<1e-5f);
    CHECK(fabsf(reprojected.z/reprojected.w)<1e-5f);
    Matrix orthographic = Orthographic(), inverseOrthographic = Identity();
    inverseOrthographic.m0 = 1.0f/orthographic.m0;
    inverseOrthographic.m5 = 1.0f/orthographic.m5;
    inverseOrthographic.m10 = 1.0f/orthographic.m10;
    inverseOrthographic.m14 = -orthographic.m14/orthographic.m10;
    CHECK(LiquidCaptureCPU_UnprojectNDC(inverseOrthographic,
        (Vector3){0.4f,0.3f,-1},&unprojected));
    CHECK(fabsf(unprojected.x-2.0f)<1e-5f && fabsf(unprojected.y-1.0f)<1e-5f);
    CHECK(LiquidCaptureCPU_RayEllipsoid(unprojected,direction,
        (Vector3){2,1,-5},(Vector3){0.3f,0.1f,1},&nearRoot,&farRoot));
    CHECK(fabsf(nearRoot-4.0f)<1e-5f && fabsf(farRoot-6.0f)<1e-5f);
    /* Inverse-view rotation must orient the world-axis metric, rather than
     * rotating the three radii as though they were a direction vector. */
    Matrix inverseRotation = Identity();
    inverseRotation.m0 = 0.8f; inverseRotation.m8 = -0.6f;
    inverseRotation.m2 = 0.6f; inverseRotation.m10 = 0.8f;
    Vector4 offsetWorld = LiquidCaptureCPU_Transform4(inverseRotation,(Vector4){0,0,5,0});
    Vector4 directionWorld = LiquidCaptureCPU_Transform4(inverseRotation,(Vector4){0,0,-1,0});
    CHECK(LiquidCaptureCPU_RayEllipsoid((Vector3){offsetWorld.x,offsetWorld.y,offsetWorld.z},
        (Vector3){directionWorld.x,directionWorld.y,directionWorld.z},(Vector3){0},
        (Vector3){2,0.2f,0.5f},&nearRoot,&farRoot));
    CHECK(fabsf(nearRoot-(5.0f-1.0f/sqrtf(2.65f)))<1e-5f);
    CHECK(fabsf(farRoot-(5.0f+1.0f/sqrtf(2.65f)))<1e-5f);
    Vector4 bounds;
    CHECK(LiquidCaptureCPU_ProjectBounds((Vector3){0,0,-0.2f},
        (Vector3){1,0.5f,1}, identity, perspective, &bounds));
    CHECK(bounds.x == -1 && bounds.y == -1 && bounds.z == 1 && bounds.w == 1);
    CHECK(!LiquidCaptureCPU_ProjectBounds((Vector3){0,0,5},
        (Vector3){1,1,1}, identity, perspective, &bounds));
    CHECK(!LiquidCaptureCPU_ProjectBounds((Vector3){100,0,-5},
        (Vector3){0.2f,0.2f,0.2f}, identity, perspective, &bounds));
    CHECK(!LiquidCaptureCPU_ProjectBounds((Vector3){0,0,-5},
        (Vector3){1,0,1}, identity, perspective, &bounds));
    bad += CheckProjectedSurface((Vector3){2,0.4f,-4}, (Vector3){0.5f,0.2f,1.4f},
                                 identity, perspective);
    bad += CheckProjectedSurface((Vector3){0,0,-0.15f}, (Vector3){0.3f,0.08f,0.12f},
                                 identity, perspective);
    Matrix rotated = Identity();
    float c = cosf(0.63f), s = sinf(0.63f);
    rotated.m0 = c; rotated.m8 = s; rotated.m2 = -s; rotated.m10 = c;
    rotated.m12 = -0.5f; rotated.m13 = 0.25f; rotated.m14 = -5.0f;
    bad += CheckProjectedSurface((Vector3){0.8f,0.1f,0.2f},
        (Vector3){1.2f,0.14f,0.45f}, rotated, perspective);
    bad += CheckProjectedSurface((Vector3){0.8f,0.1f,0.2f},
        (Vector3){1.2f,0.14f,0.45f}, rotated, Orthographic());
    printf("liquid CPU impostor math: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}

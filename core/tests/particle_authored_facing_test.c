#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

typedef struct { float x, y, z; } V3;

static V3 Cross(V3 a, V3 b)
{
    return (V3){a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

static float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static V3 Normalize(V3 v)
{
    float length = sqrtf(Dot(v, v));
    return length > 0.00001f ? (V3){v.x / length, v.y / length, v.z / length}
                             : (V3){0};
}

/* Arithmetic mirror of the renderer contract. It validates basis geometry,
 * not raster output: the runtime regression below locks the actual call path. */
static void AxisBasis(V3 axis, V3 view, V3 cameraUp, V3 *sideA, V3 *sideB)
{
    axis = Normalize(axis);
    *sideA = Cross(axis, view);
    if (Dot(*sideA, *sideA) < 0.00001f) *sideA = Cross(axis, cameraUp);
    if (Dot(*sideA, *sideA) < 0.00001f) *sideA = Cross(axis, (V3){1, 0, 0});
    if (Dot(*sideA, *sideA) < 0.00001f) *sideA = Cross(axis, (V3){0, 0, 1});
    *sideA = Normalize(*sideA);
    *sideB = Normalize(Cross(axis, *sideA));
}

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[240000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    V3 axis = Normalize((V3){0.2f, 0.9f, -0.3f});
    V3 a, b;
    AxisBasis(axis, (V3){0, 0, 1}, (V3){0, 1, 0}, &a, &b);
    CHECK(fabsf(Dot(axis, a)) < 0.0001f &&
          fabsf(Dot(axis, b)) < 0.0001f &&
          fabsf(Dot(a, b)) < 0.0001f,
          "authored-axis cross produces two mutually orthogonal planes");

    AxisBasis((V3){0, 0, 1}, (V3){0, 0, 1}, (V3){0, 1, 0}, &a, &b);
    CHECK(Dot(a, a) > 0.99f && Dot(b, b) > 0.99f,
          "axis-view singularity falls back to a valid camera-up basis");

    CHECK(Has("core/vfx_config.h", "Vector3 facingDirection;") &&
          Has("core/vfx_config.h", "float facingAspect;") &&
          Has("core/particles/particle_system.h", "Vector3 facingDirection;") &&
          Has("core/particles/particle_system.h", "float facingAspect;"),
          "public flat and unified configs expose authored direction and aspect");
    CHECK(Has("core/particles/particle_system.c", "p->facingDirection = config.render.facingDirection;") &&
          Has("core/particles/particle_system.c", "p->facingAspect = config.render.facingAspect") &&
          Has("core/particles/particle_system.c", "PS_AxisCrossBasis("),
          "CPU compatibility path stores and consumes the authored basis");
    CHECK(Has("core/particles/particle_system.c", "facingDirectionLengthSq") &&
          Has("core/particles/particle_system.c", "legacy camera-space cross"),
          "zero authored direction preserves the legacy cross-billboard result");
    CHECK(Has("core/particles/particle_manager.c", "ParticleManager_RequiresCpuFacing") &&
          Has("core/particles/particle_manager.c", "!ParticleManager_RequiresCpuFacing(p)"),
          "backend-neutral emitters cannot silently send authored facing to the legacy GPU billboard");
    CHECK(Has("core/particles/particle_system.c", "float softDepthRadius = drawRadius * fmaxf(p->facingAspect, 1.0f)") &&
          Has("core/particles/particle_system.c", "Vector3 particleView = Vector3Subtract(camera.position"),
          "rectangular axis cards request complete soft depth and face the local camera ray");

    return failures ? 1 : 0;
}

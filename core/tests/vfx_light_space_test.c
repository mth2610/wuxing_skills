// core headless test — the SPACE a VFX point light is uploaded in.
//
// WHY THIS EXISTS. E2 wired the dynamic light pool into three lit shaders and
// lit absolutely nothing, through two sessions and ~15 build-and-look rounds.
// Everything checkable was checked and passed: the pool had lights, all three
// shaders compiled and reflected every uniform, the values reached the UBO, the
// debug sphere landed on the effect, and `fract(worldPos)` painted a clean 1 m
// grid on the ground. The fault was underneath all of it:
//
//   raylib's DrawMesh uploads  matModel = modelTransform * rlGetMatrixTransform()
//   and rlGetMatrixTransform() is the VIEW MATRIX inside a 3D pass, because
//   rlPushMatrix() in RL_MODELVIEW mode redirects rlgl's current matrix into
//   RLGL.State.transform (see main.c MyBeginMode3D).
//
// So `fragPosition = matModel * vertexPosition` is a VIEW-space position that
// every shader in the project calls "world". A light uploaded in world space is
// then tens of metres from the fragment directly beneath it, attenuation clamps
// to zero, and no surface is ever lit — with no error anywhere.
//
// The half of this that no eyeball can catch: fract() of a view-space position
// paints exactly the same convincing 1 m grid as fract() of a world-space one.
// The debug view that was supposed to prove the coordinates said "correct".
// Only a numeric check can tell the two apart, so here it is.
//
// Part 1 is arithmetic: does the world->view conversion put a light where the
// surface underneath it actually is? Part 2 guards the mirror (core/CLAUDE.md
// §3): it asserts the conversion is still present in core/vfx_light.c and that
// the falloff this test models still matches vfx_lights.glsl.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define CHECK_MSG(cond, name, fmt, ...) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

// ── minimal linear algebra, column-major like raymath ───────────────────────
typedef struct { float x, y, z; } V3;
typedef struct { float m[16]; } M4;   // m[col*4 + row]

static V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static float dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 norm(V3 a) { float l = len(a); return (l > 1e-6f) ? v3(a.x/l, a.y/l, a.z/l) : a; }
static V3 cross(V3 a, V3 b)
{
    return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

// MatrixLookAt, matching raymath's memory layout (m12/m13/m14 = translation).
static M4 look_at(V3 eye, V3 target, V3 up)
{
    V3 vz = norm(sub(eye, target));
    V3 vx = norm(cross(up, vz));
    V3 vy = cross(vz, vx);
    M4 r;
    r.m[0]=vx.x; r.m[1]=vy.x; r.m[2]=vz.x; r.m[3]=0.0f;
    r.m[4]=vx.y; r.m[5]=vy.y; r.m[6]=vz.y; r.m[7]=0.0f;
    r.m[8]=vx.z; r.m[9]=vy.z; r.m[10]=vz.z; r.m[11]=0.0f;
    r.m[12]=-dot(vx,eye); r.m[13]=-dot(vy,eye); r.m[14]=-dot(vz,eye); r.m[15]=1.0f;
    return r;
}

// raymath's Vector3Transform — the exact call VFXLight_BindToShader makes.
static V3 xform(V3 v, M4 m)
{
    return v3(m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z  + m.m[12],
              m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z  + m.m[13],
              m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14]);
}

// The falloff from core/shaders/common/vfx_lights.glsl, verbatim.
static float attenuation(V3 lightPos, V3 fragPos, float radius)
{
    float d = len(sub(lightPos, fragPos));
    float r = radius > 0.001f ? radius : 0.001f;
    float a = 1.0f - d / r;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return a * a;
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    b[got] = '\0'; fclose(f);
    return b;
}

int main(void)
{
    printf("=== vfx light space test ===\n");

    // A camera looking down at a character, roughly this project's isometric
    // rig: 10 m back, 9 m up. The exact numbers do not matter — the point is
    // that the view translation is large compared with a light's radius.
    V3 eye    = v3(19.5f, 9.1f, 26.0f);
    V3 target = v3(19.5f, 0.0f, 15.9f);
    M4 view   = look_at(eye, target, v3(0.0f, 1.0f, 0.0f));

    // A 6 m light one metre above the player, and the patch of ground directly
    // underneath it. In ANY consistent space these two are 1 m apart.
    V3 lightWorld = v3(19.46f, 1.0f, 15.90f);
    V3 groundWorld = v3(19.46f, 0.0f, 15.90f);
    const float radius = 6.0f;

    // 1. Sanity: in a single consistent space the light does light the ground.
    CHECK(attenuation(lightWorld, groundWorld, radius) > 0.6f,
          "same-space: ground under a 6 m light is strongly lit");

    // 2. THE BUG. The surface's fragPosition is view space (matModel carries the
    //    view); the light was uploaded in world space. Mixing them is what shipped.
    V3 groundView = xform(groundWorld, view);
    float mixed = attenuation(lightWorld, groundView, radius);
    CHECK_MSG(mixed == 0.0f,
              "mixed spaces: world light vs view-space fragment is completely dark",
              "attenuation=%.4f, expected 0", (double)mixed);

    // The distance that produced it — far outside any sane light radius, which
    // is why NOTHING was ever lit rather than something being merely dim.
    float mixedDist = len(sub(lightWorld, groundView));
    CHECK_MSG(mixedDist > 20.0f, "mixed spaces: the bogus distance is tens of metres",
              "distance=%.2f m", (double)mixedDist);

    // 3. THE FIX. Convert the light with the same matrix the surface got.
    V3 lightView = xform(lightWorld, view);
    float fixed_ = attenuation(lightView, groundView, radius);
    CHECK_MSG(fabsf(fixed_ - attenuation(lightWorld, groundWorld, radius)) < 1e-4f,
              "converted: view-space light reproduces the same-space attenuation",
              "got %.4f", (double)fixed_);
    CHECK(fixed_ > 0.6f, "converted: ground under the light is strongly lit");

    // 4. The view matrix is rigid, so distances (and therefore the radius) are
    //    preserved. This is why only the position is converted, not the radius.
    float dWorld = len(sub(lightWorld, groundWorld));
    float dView  = len(sub(lightView, groundView));
    CHECK_MSG(fabsf(dWorld - dView) < 1e-3f,
              "view transform preserves distance (radius needs no conversion)",
              "world %.4f vs view %.4f", (double)dWorld, (double)dView);

    // 5. THE DECOY, asserted so nobody trusts it again. fract(pos) is identical
    //    in shape for both spaces — the debug view that "proved" the coordinates
    //    were right could not have detected this bug.
    {
        float fw = groundWorld.x - floorf(groundWorld.x);
        float fv = groundView.x  - floorf(groundView.x);
        CHECK(fw >= 0.0f && fw < 1.0f && fv >= 0.0f && fv < 1.0f,
              "decoy: fract() is in [0,1) for BOTH spaces - it cannot tell them apart");
    }

    // ── mirror guards: the C and GLSL this test models must still say this ──
    {
        char *c = slurp("core/vfx_light.c");
        CHECK(c != NULL, "core/vfx_light.c readable");
        if (c)
        {
            CHECK(strstr(c, "rlGetMatrixTransform") != NULL,
                  "vfx_light.c still reads the surface space off rlGetMatrixTransform");
            CHECK(strstr(c, "Vector3Transform") != NULL,
                  "vfx_light.c still converts light positions before upload");
            free(c);
        }
    }
    {
        char *g = slurp("core/shaders/common/vfx_lights.glsl");
        CHECK(g != NULL, "vfx_lights.glsl readable");
        if (g)
        {
            CHECK(strstr(g, "clamp(1.0 - dist / max(u_vfxLightPosRadius[i].w, 0.001), 0.0, 1.0)") != NULL,
                  "glsl falloff still matches the one modelled here");
            free(g);
        }
    }
    {
        // The bind MUST happen inside the 3D pass, or rlGetMatrixTransform is
        // identity and the conversion silently becomes a no-op.
        char *m = slurp("main.c");
        CHECK(m != NULL, "main.c readable");
        if (m)
        {
            const char *bind  = strstr(m, "VFXLight_BindAll");
            const char *begin = strstr(m, "MyBeginMode3D(camera)");
            CHECK(bind != NULL && begin != NULL && bind > begin,
                  "VFXLight_BindAll is called AFTER MyBeginMode3D (inside the 3D pass)");
            free(m);
        }
    }

    printf("=== vfx light space: %d failure(s) ===\n", g_failures);
    return g_failures ? 1 : 0;
}

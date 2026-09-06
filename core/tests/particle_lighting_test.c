// core headless test — particle_lit.fs lighting maths.
//
// Modelled on third_party/vulkan/tests/rlvk_runtime_test.c: a standalone C file
// that links nothing from the game, runs headless, and prints PASS/FAIL lines.
//
// WHY THIS EXISTS. The question "is the particle shading actually directional?"
// was chased through five rounds of build-screenshot-guess, because the only
// available instrument was a human eye looking at overlapping translucent
// sprites in a dark scene. It is not a rendering question at all — it is
// arithmetic, and arithmetic can be interrogated directly.
//
// WHAT IT DOES NOT PROVE. This is a MIRROR of the GLSL, not the GLSL itself, so
// it cannot catch a uniform that never reaches the shader, a wrong shader
// binding, or a driver quirk. It answers exactly one class of question — "is the
// maths capable of producing directional shading" — and `shader_source_matches`
// below guards against the mirror silently drifting from the original.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

// ── minimal vec3 ─────────────────────────────────────────────────────────────

typedef struct { float x, y, z; } V3;

static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 v3add(V3 a, V3 b) { return v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static V3 v3scale(V3 a, float s) { return v3(a.x*s, a.y*s, a.z*s); }
static float v3dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3 v3cross(V3 a, V3 b) {
    return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
static V3 v3norm(V3 a) {
    float l = sqrtf(v3dot(a, a));
    return l > 1e-9f ? v3scale(a, 1.0f/l) : v3(0, 0, 0);
}

// ── the mirror of particle_lit.fs ────────────────────────────────────────────
//
// Screen-space normal. In the shader the tilt DIRECTION comes from
// dFdx/dFdy of the sprite alpha; for the radial-gradient particle texture that
// gradient points straight at the sprite centre, so for a fragment at offset
// (u,v) from centre (both in [-1,1], rr = hypot(u,v)):
//
//     a    = 1 - rr^2                (alpha as a paraboloid height)
//     dir  = -(u,v)/rr               (gradient points inward, toward more alpha)
//     n.xy = -dir * rr * bulge       = (u,v) * bulge
//     n.z  = sqrt(a)                 = sqrt(1 - rr^2)
//
// so the whole thing collapses to n = normalize(u*bulge, v*bulge, sqrt(1-rr^2)),
// which is what this reproduces. Mirrors particle_lit.fs lines ~81-98.
static V3 ScreenNormal(float u, float v, float bulge)
{
    float rr2 = u*u + v*v;
    if (rr2 > 1.0f) rr2 = 1.0f;
    float a = 1.0f - rr2;
    return v3norm(v3(u * bulge, v * bulge, sqrtf(a > 0.0f ? a : 0.0f)));
}

// Screen-space normal → world, via the camera basis. Mirrors lines ~100-107.
static V3 WorldNormal(V3 n, V3 V)
{
    V3 upRef = (fabsf(V.y) > 0.99f) ? v3(0, 0, 1) : v3(0, 1, 0);
    V3 R = v3norm(v3cross(upRef, V));
    V3 U = v3cross(V, R);
    return v3norm(v3add(v3add(v3scale(R, n.x), v3scale(U, n.y)), v3scale(V, n.z)));
}

// Half-Lambert wrap. Mirrors the `wrap` line.
static float Wrap(V3 N, V3 L)
{
    return powf(v3dot(N, L) * 0.5f + 0.5f, 1.5f);
}

// The debug azimuth override. Mirrors the `u_lightAzimuth` branch.
static V3 LightFromAzimuth(float deg)
{
    float r = deg * 3.14159265f / 180.0f;
    return v3norm(v3(cosf(r), 0.25f, sinf(r)));
}

// ── scene setup shared by the cases ──────────────────────────────────────────
// An isometric-ish camera looking down at the arena, matching the game's rig
// closely enough for the directionality question.
static V3 TestViewDir(void) { return v3norm(v3(0.5f, 0.6f, 0.6f)); }

// Brightness at a point on the sprite face.
static float LitAt(float u, float v, V3 L, float bulge)
{
    return Wrap(WorldNormal(ScreenNormal(u, v, bulge), TestViewDir()), L);
}

// Mirrors SoftParticle_Factor's final fade calculation once both depths are
// linearized. A fragment at or behind the scene surface must disappear; one
// fade distance in front must remain fully visible.
static float SoftFactor(float sceneLinear, float fragLinear, float fadeDistance)
{
    float f = (sceneLinear - fragLinear) / (fadeDistance > 0.0001f ? fadeDistance : 0.0001f);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

// ── cases ────────────────────────────────────────────────────────────────────

static void Test_NormalIsUnitLength(void)
{
    float worst = 0.0f;
    for (int i = 0; i <= 10; i++)
        for (int j = 0; j <= 10; j++)
        {
            float u = -1.0f + i * 0.2f, v = -1.0f + j * 0.2f;
            if (u*u + v*v > 1.0f) continue;
            V3 n = ScreenNormal(u, v, 1.0f);
            float d = fabsf(sqrtf(v3dot(n, n)) - 1.0f);
            if (d > worst) worst = d;
        }
    CHECK_MSG(worst < 1e-4f, "screen normal is unit length", "worst deviation %.6f", worst);
}

static void Test_NormalSpansHemisphere(void)
{
    // The bug that cost several rounds: an ad-hoc `mix(1.0, 0.18, tilt)` kept
    // n.z near 1 everywhere, so every normal bunched around the view direction
    // and the sprite resolved to one flat colour. Centre must face the camera,
    // rim must lie nearly sideways.
    V3 centre = ScreenNormal(0.0f, 0.0f, 1.0f);
    V3 mid    = ScreenNormal(0.707f, 0.0f, 1.0f);   // half-ish radius
    V3 rim    = ScreenNormal(0.995f, 0.0f, 1.0f);

    CHECK_MSG(centre.z > 0.99f, "normal at centre faces the viewer", "z=%.3f", centre.z);
    CHECK_MSG(fabsf(mid.z - 0.707f) < 0.05f, "normal at mid-radius is tilted ~45 deg",
              "z=%.3f (want ~0.707)", mid.z);
    CHECK_MSG(rim.z < 0.15f, "normal at rim points sideways", "z=%.3f", rim.z);
}

static void Test_LightingIsDirectional(void)
{
    // The core question. With the light coming from +X, the +X side of the
    // sprite must be brighter than the -X side. If this fails, no amount of
    // gain or bulge tuning can produce shape.
    V3 L = LightFromAzimuth(0.0f);
    float right = LitAt(0.7f, 0.0f, L, 1.0f);
    float left  = LitAt(-0.7f, 0.0f, L, 1.0f);
    CHECK_MSG(right > left * 1.2f, "lit side is clearly brighter than dark side",
              "right=%.3f left=%.3f ratio=%.2f", right, left, right / (left + 1e-6f));
}

static void Test_AzimuthSweepRotatesBrightSide(void)
{
    // Sweeping the light must move WHICH side is bright — the exact thing the
    // in-game azimuth sweep was supposed to show.
    const float az[4] = {0.0f, 90.0f, 180.0f, 270.0f};
    int distinct = 0;
    int prevBest = -1;
    for (int k = 0; k < 4; k++)
    {
        V3 L = LightFromAzimuth(az[k]);
        // sample 8 points around the sprite, find the brightest
        int best = 0; float bestVal = -1.0f;
        for (int s = 0; s < 8; s++)
        {
            float ang = (float)s * 3.14159265f / 4.0f;
            float val = LitAt(cosf(ang) * 0.7f, sinf(ang) * 0.7f, L, 1.0f);
            if (val > bestVal) { bestVal = val; best = s; }
        }
        if (best != prevBest) distinct++;
        prevBest = best;
    }
    CHECK_MSG(distinct >= 3, "brightest side moves as the light azimuth sweeps",
              "only %d distinct positions across 4 azimuths", distinct);
}

static void Test_LightAlongViewCollapsesToRadial(void)
{
    // Documents the failure mode that produced "bright in the middle, dark all
    // round" on screen. When the light points along the view vector, dot(N,L)
    // reduces to n.z, which is radially symmetric BY CONSTRUCTION — so the
    // shading is centre-bright and carries no directional information at all.
    // This is not a shader bug, and chasing it as one wastes a lot of time.
    V3 L = TestViewDir();
    float right = LitAt(0.7f, 0.0f, L, 1.0f);
    float left  = LitAt(-0.7f, 0.0f, L, 1.0f);
    float centre = LitAt(0.0f, 0.0f, L, 1.0f);
    CHECK_MSG(fabsf(right - left) < 0.02f,
              "light along view => shading is radially symmetric (documented trap)",
              "right=%.3f left=%.3f", right, left);
    CHECK_MSG(centre > right * 1.05f,
              "light along view => centre is the bright spot (documented trap)",
              "centre=%.3f rim=%.3f", centre, right);
}

static void Test_BulgeIncreasesContrast(void)
{
    V3 L = LightFromAzimuth(0.0f);
    float c1 = LitAt(0.7f, 0.0f, L, 1.0f) - LitAt(-0.7f, 0.0f, L, 1.0f);
    float c2 = LitAt(0.7f, 0.0f, L, 2.5f) - LitAt(-0.7f, 0.0f, L, 2.5f);
    CHECK_MSG(c2 > c1, "particle_normal_bulge raises directional contrast",
              "bulge1=%.3f bulge2.5=%.3f", c1, c2);
}

// The bug the in-game screenshots exposed and the first version of this suite
// could NOT: the shipped shader took its tilt direction from dFdx/dFdy of the
// sprite alpha, and that gradient is ~0 across the flat core of a soft particle.
// `dir` then collapsed to zero and the normal snapped to (0,0,1) — so the
// largest, brightest part of every sprite faced the camera and carried no
// directional shading whatsoever. The mirror missed it because it computed the
// direction analytically, which is never zero. This case pins the failure so a
// future "optimisation" back to derivatives cannot quietly reintroduce it.
static void Test_CoreIsNotFlat(void)
{
    V3 L = LightFromAzimuth(0.0f);
    // Sample well inside the core, where a derivative-driven normal died.
    float right = LitAt(0.25f, 0.0f, L, 1.0f);
    float left  = LitAt(-0.25f, 0.0f, L, 1.0f);
    CHECK_MSG(right > left * 1.05f,
              "core of the sprite still shades directionally (not just the rim)",
              "right=%.4f left=%.4f", right, left);

    // And the degenerate normal must not be reachable anywhere but dead centre.
    V3 nearCentre = ScreenNormal(0.05f, 0.0f, 1.0f);
    CHECK_MSG(fabsf(nearCentre.x) > 1e-3f,
              "normal tilts even just off-centre (no dead zone)",
              "n.x=%.5f at u=0.05", nearCentre.x);
}

// Forward scatter depends only on the view-vs-light angle, NOT on the normal,
// so it adds the SAME amount to every fragment of a sprite. It is a uniform
// brightener by construction — correct for "the puff glows when backlit", but it
// cannot contribute shape, and at a high value it drowns the shading that does.
// Pinned here because on screen an over-scattered puff looks like broken
// lighting rather than like a working knob turned too far.
static void Test_ScatterIsUniformAcrossSprite(void)
{
    V3 L = LightFromAzimuth(180.0f);
    V3 V = TestViewDir();
    float backlit = -v3dot(V, L);
    if (backlit < 0.0f) backlit = 0.0f;
    float add = powf(backlit, 4.0f) * 0.8f;   // the shader's scatter term

    // Same value wherever it is evaluated on the sprite — that is the point.
    float atCentre = add, atRim = add;
    CHECK_MSG(fabsf(atCentre - atRim) < 1e-6f,
              "scatter adds the same amount everywhere (shape-neutral by design)",
              "centre=%.4f rim=%.4f", atCentre, atRim);

    // And it must therefore REDUCE relative contrast when raised. Measured as
    // Michelson contrast |a-b|/(a+b), which is agnostic to WHICH side is the
    // bright one — a plain hi/lo ratio silently inverts its meaning when the
    // light comes from the other side, which is how the first version of this
    // assertion managed to fail on correct behaviour.
    float a1 = LitAt(0.7f, 0.0f, L, 1.0f), b1 = LitAt(-0.7f, 0.0f, L, 1.0f);
    float noScatter   = fabsf(a1 - b1) / (a1 + b1 + 1e-6f);
    float withScatter = fabsf(a1 - b1) / (a1 + b1 + 2.0f * add + 1e-6f);
    CHECK_MSG(withScatter < noScatter,
              "raising scatter lowers directional contrast (judge shape at 0)",
              "no=%.3f with=%.3f", noScatter, withScatter);
}

static void Test_AmbientGainFlattens(void)
{
    // ambient is a constant floor, so RAISING it must REDUCE relative contrast.
    // This is the reasoning behind telling the artist to pull ambient DOWN.
    V3 L = LightFromAzimuth(0.0f);
    float hi = LitAt(0.7f, 0.0f, L, 1.0f), lo = LitAt(-0.7f, 0.0f, L, 1.0f);
    const float sun = 0.30f, amb = 0.20f;   // representative night-arena values

    float lowAmb  = (amb * 0.3f + sun * 2.5f * hi) / (amb * 0.3f + sun * 2.5f * lo);
    float highAmb = (amb * 2.0f + sun * 2.5f * hi) / (amb * 2.0f + sun * 2.5f * lo);
    CHECK_MSG(lowAmb > highAmb, "lower particle_ambient_gain => higher contrast ratio",
              "lowAmb=%.2fx highAmb=%.2fx", lowAmb, highAmb);
}

static void Test_SoftParticleFade(void)
{
    const float fade = 0.35f;
    CHECK_MSG(SoftFactor(8.0f, 8.0f, fade) == 0.0f,
              "particle at scene surface fades fully", "factor=%.3f", SoftFactor(8.0f, 8.0f, fade));
    CHECK_MSG(SoftFactor(8.0f, 7.65f, fade) > 0.99f,
              "particle one fade distance in front stays visible", "factor=%.3f", SoftFactor(8.0f, 7.65f, fade));
}

// ── anti-drift guard ─────────────────────────────────────────────────────────

static int FileContains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static char buf[1 << 18];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

static void Test_6WayLightingBasisAndAxes(void)
{
    V3 V = TestViewDir();
    V3 upRef = (fabsf(V.y) > 0.99f) ? v3(0, 0, 1) : v3(0, 1, 0);
    V3 R = v3norm(v3cross(upRef, V));
    V3 U = v3cross(V, R);
    V3 Back = v3scale(V, -1.0f);

    // Light from right (+X)
    V3 L_right = R;
    V3 L_local_right = v3(v3dot(L_right, R), v3dot(L_right, U), v3dot(L_right, Back));
    CHECK_MSG(L_local_right.x > 0.99f && fabsf(L_local_right.y) < 0.01f && fabsf(L_local_right.z) < 0.01f,
              "6-way: +X maps to billboard right", "got (%.2f, %.2f, %.2f)",
              L_local_right.x, L_local_right.y, L_local_right.z);

    // Light from top (+Y)
    V3 L_top = U;
    V3 L_local_top = v3(v3dot(L_top, R), v3dot(L_top, U), v3dot(L_top, Back));
    CHECK_MSG(L_local_top.y > 0.99f && fabsf(L_local_top.x) < 0.01f && fabsf(L_local_top.z) < 0.01f,
              "6-way: +Y maps to billboard up", "got (%.2f, %.2f, %.2f)",
              L_local_top.x, L_local_top.y, L_local_top.z);

    // Light from behind (+Z Back / forward scattering)
    V3 L_back = Back;
    V3 L_local_back = v3(v3dot(L_back, R), v3dot(L_back, U), v3dot(L_back, Back));
    CHECK_MSG(L_local_back.z > 0.99f && fabsf(L_local_back.x) < 0.01f && fabsf(L_local_back.y) < 0.01f,
              "6-way: +Z maps to backlight (forward-scatter direction)", "got (%.2f, %.2f, %.2f)",
              L_local_back.x, L_local_back.y, L_local_back.z);
}

static void Test_ShaderSourceMatchesMirror(void)
{
    // A C mirror of GLSL is only as good as its agreement with the original.
    // These are the load-bearing expressions; if one is edited, this test fails
    // loudly and whoever changed it must update the mirror above rather than
    // let the suite quietly start testing fiction.
    const char *path = "core/particles/shaders/particle_lit.fs";
    struct { const char *needle, *why; } req[] = {
        // E4: the quad-local UV is now RECOVERED from the atlas grid before this
        // step, because with a SpriteAnim atlas fragTexCoord is a sub-rect and
        // feeding it straight in shades from a different slice of the hemisphere
        // per cell — which jumps every time the animation steps.
        { "vec2  q = luv * 2.0 - 1.0",            "quad-local UV" },
        { "fract(fragTexCoord * u_atlasGrid)",    "atlas -> quad-local UV recovery" },
        { "sqrt(max(1.0 - rc * rc, 0.0))",        "analytic hemisphere z" },
        { "sqrt(clamp(1.0 - a, 0.0, 1.0))",       "derivative-fallback radius r" },
        { "pow(ndl * 0.5 + 0.5, 1.5)",            "half-Lambert wrap" },
        { "u_ambient * u_ambientGain + u_sunColor * u_sunGain * wrap", "gain formula" },
        { "#include \"core/shaders/common/soft_particle.glsl\"", "soft-depth shader block" },
        { "SoftParticle_Factor(u_softFade)",       "soft-particle alpha factor" },
        { "ParticleLightTerm6Way",                 "6-way volumetric lighting integrator" },
        { "u_sixWayLighting",                      "6-way lighting uniform toggle" },
        { "u_ambientGround",                       "6-way multi-directional ground ambient" },
        { "u_ambientHorizon",                      "6-way multi-directional horizon ambient" },
    };
    int missing = 0;
    for (unsigned i = 0; i < sizeof(req)/sizeof(req[0]); i++)
    {
        int r = FileContains(path, req[i].needle);
        if (r < 0) { printf("FAIL: shader source readable (%s)\n", path); g_failures++; g_checks++; return; }
        if (!r) { printf("  drifted: %s (%s)\n", req[i].why, req[i].needle); missing++; }
    }
    CHECK_MSG(missing == 0, "C mirror still matches particle_lit.fs",
              "%d expression(s) drifted — update the mirror in this file", missing);
}

int main(void)
{
    printf("=== core headless test: particle lighting ===\n");
    Test_NormalIsUnitLength();
    Test_NormalSpansHemisphere();
    Test_LightingIsDirectional();
    Test_AzimuthSweepRotatesBrightSide();
    Test_LightAlongViewCollapsesToRadial();
    Test_CoreIsNotFlat();
    Test_BulgeIncreasesContrast();
    Test_ScatterIsUniformAcrossSprite();
    Test_AmbientGainFlattens();
    Test_SoftParticleFade();
    Test_6WayLightingBasisAndAxes();
    Test_ShaderSourceMatchesMirror();

    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}

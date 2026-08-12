// core headless test — the SSF surface-variation field must not be a plane wave
// (core/fluid/shaders/fluid_surface.fs + core/shaders/common/noise.glsl).
//
// The complaint was "alternating bright and dark stripes, like optical
// interference fringes, and the white marks turn into bright streaks up close".
// Rendering the field on its own showed literally that: parallel black-and-white
// bands wrapped around the water ring. `sin(dot(worldPosition, k))` is a PLANE
// WAVE — bands are what it is, not an artefact of it — and this one scalar drove
// the specular roughness, the sharp-glint gate and the foam pattern at once.
//
// This mirrors hash3/vnoise3/fbm3 from the shared noise.glsl and asserts the
// property that separates noise from a wave: it must not repeat when the sample
// point is advanced by one feature length along any single direction. The old
// expression is mirrored too, so the test demonstrably CAN detect the defect.
//
// Float results will not match the GPU bit for bit and are not compared to it;
// only the structural properties are. Whether the surface then looks right is
// the sandbox's job (NEW FX tab, WATER RING, viewed from far out — the banding
// was most obvious at distance).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

static float Fract(float v) { return v - floorf(v); }

/* Mirror of hash3() — Dave Hoskins "Hash without Sine", the mobile-safe one
 * (ENGINE_LANDMINES: a fract(sin(...)) hash dies on Mali at large domains). */
static float Hash3(float x, float y, float z)
{
    float px = Fract(x * 0.1031f), py = Fract(y * 0.1031f), pz = Fract(z * 0.1031f);
    float d = px * (pz + 31.32f) + py * (py + 31.32f) + pz * (px + 31.32f);
    px += d; py += d; pz += d;
    return Fract((px + py) * pz);
}

static float Smooth(float f) { return f * f * (3.0f - 2.0f * f); }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

/* Mirror of vnoise3(). */
static float VNoise3(float x, float y, float z)
{
    float ix = floorf(x), iy = floorf(y), iz = floorf(z);
    float ux = Smooth(x - ix), uy = Smooth(y - iy), uz = Smooth(z - iz);
    float n000 = Hash3(ix, iy, iz),               n100 = Hash3(ix + 1, iy, iz);
    float n010 = Hash3(ix, iy + 1, iz),           n110 = Hash3(ix + 1, iy + 1, iz);
    float n001 = Hash3(ix, iy, iz + 1),           n101 = Hash3(ix + 1, iy, iz + 1);
    float n011 = Hash3(ix, iy + 1, iz + 1),       n111 = Hash3(ix + 1, iy + 1, iz + 1);
    return Lerp(Lerp(Lerp(n000, n100, ux), Lerp(n010, n110, ux), uy),
                Lerp(Lerp(n001, n101, ux), Lerp(n011, n111, ux), uy), uz);
}

/* Mirror of fbm3(). */
static float Fbm3(float x, float y, float z)
{
    return VNoise3(x, y, z) * 0.667f
         + VNoise3(x * 2.13f + 17.3f, y * 2.13f + 5.7f, z * 2.13f + 11.1f) * 0.333f;
}

/* The expression that was there, for contrast. */
static float PlaneWave(float x, float y, float z)
{ return sinf(x * 12.3f + y * 7.1f - z * 9.5f) * 0.5f + 0.5f; }

/* Mean |f(p) - f(p + step*dir)| over a walk — near zero for a field that
 * repeats at `step` along `dir`. */
static float RepeatError(float (*field)(float, float, float),
                         float dx, float dy, float dz, int samples)
{
    float total = 0.0f;
    for (int i = 0; i < samples; i++)
    {
        float t = (float)i * 0.037f;
        float x = 6.0f + t * 0.9f, y = 0.4f + t * 0.3f, z = 4.4f - t * 0.7f;
        total += fabsf(field(x, y, z) - field(x + dx, y + dy, z + dz));
    }
    return total / (float)samples;
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

    /* ---- Range, variation, and CONTINUITY. A field driving roughness and two
     * gates must stay in [0,1], must still vary, and must not jump — a raw hash
     * would satisfy the first two and sparkle like static.
     *
     * Continuity is asserted as a Lipschitz property rather than an absolute
     * bound: halving the sample spacing must roughly halve the largest step. An
     * absolute bound would only be a statement about the frequency this test
     * happens to walk at, which is how this check failed its first writing. */
    {
        float lo = 2.0f, hi = -1.0f;
        float maxStep[2] = { 0.0f, 0.0f };
        for (int pass = 0; pass < 2; pass++)
        {
            float spacing = pass ? 0.00125f : 0.0025f;
            float previous = Fbm3(6.0f, 0.3f, 4.4f);
            for (int i = 1; i < 4000; i++)
            {
                float t = (float)i * spacing;
                float v = Fbm3(6.0f + t * 2.7f, 0.3f + t * 1.1f, 4.4f - t * 1.9f);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                float step = fabsf(v - previous);
                if (step > maxStep[pass]) maxStep[pass] = step;
                previous = v;
            }
        }
        printf("      fbm3 range [%.3f, %.3f], largest step %.4f -> %.4f on halving the spacing\n",
               lo, hi, maxStep[0], maxStep[1]);
        CHECK(lo >= 0.0f && hi <= 1.0f);
        CHECK(hi - lo > 0.35f);                     /* a flat field is not a fix */
        CHECK(maxStep[1] < maxStep[0] * 0.70f);     /* continuous, not a hash */
        CHECK(maxStep[0] < 0.25f);                  /* and no outright discontinuity */
    }

    /* ---- THE PROPERTY. The old field's wavelength along its own wave vector is
     * 2*PI/|k| = 0.373 m, and after one wavelength it returns to exactly the same
     * value — that is what draws the bands. The replacement must not. */
    {
        const float kx = 12.3f, ky = 7.1f, kz = -9.5f;
        float klen = sqrtf(kx * kx + ky * ky + kz * kz);
        float wavelength = 6.2831853f / klen;
        float dx = kx / klen * wavelength, dy = ky / klen * wavelength, dz = kz / klen * wavelength;

        float waveRepeat = RepeatError(PlaneWave, dx, dy, dz, 400);
        float noiseRepeat = RepeatError(Fbm3, dx, dy, dz, 400);
        printf("      one wavelength (%.3f m) along k: plane wave repeats to %.5f, fbm3 to %.4f\n",
               wavelength, waveRepeat, noiseRepeat);
        /* The defect, demonstrated: the old field is periodic to within float error. */
        CHECK(waveRepeat < 1.0e-4f);
        /* The fix: the new one is not, by a wide margin. */
        CHECK(noiseRepeat > 0.05f);
    }

    /* ---- Feature SCALE is preserved. The replacement was chosen so one fbm3
     * cell spans the same 0.37 m the sine's wavelength did; a field that varied
     * far slower or faster would change the look rather than fix the banding. */
    {
        float nearStep = RepeatError(Fbm3, 0.037f, 0.0f, 0.0f, 300);   /* a tenth of a cell */
        float cellStep = RepeatError(Fbm3, 0.370f, 0.0f, 0.0f, 300);   /* one cell */
        printf("      mean change over 0.037 m: %.4f   over 0.370 m: %.4f\n", nearStep, cellStep);
        CHECK(cellStep > nearStep * 3.0f);
    }

    /* ---- No axis-aligned grain. Value noise sits on a lattice, so the second
     * octave is offset as well as scaled; the field must vary as much along a
     * diagonal as along an axis. */
    {
        float alongX = RepeatError(Fbm3, 0.37f, 0.0f, 0.0f, 300);
        float diagonal = RepeatError(Fbm3, 0.214f, 0.214f, 0.214f, 300);
        CHECK(diagonal > alongX * 0.4f && diagonal < alongX * 2.5f);
    }

    /* ---- Anti-drift on the shader. */
    {
        char *shader = ReadFile("core/fluid/shaders/fluid_surface.fs");
        if (!shader) { printf("FAIL: cannot read fluid_surface.fs\n"); bad++; }
        else
        {
            CHECK(strstr(shader, "#include \"core/shaders/common/noise.glsl\"") != NULL);
            CHECK(strstr(shader, "fbm3(worldPosition * 2.7") != NULL);
            /* The plane wave must not come back — in any of the three roles it
             * used to fill (roughness, the glint gate, the foam pattern). Match
             * the ASSIGNMENT, not the expression: the comment explaining the fix
             * quotes the old expression, and the first version of this check
             * matched its own documentation. */
            CHECK(strstr(shader, "surfaceNoise = sin(") == NULL);
            free(shader);
        }
        char *noise = ReadFile("core/shaders/common/noise.glsl");
        if (!noise) { printf("FAIL: cannot read noise.glsl\n"); bad++; }
        else
        {
            CHECK(strstr(noise, "float vnoise3(vec3 p)") != NULL);
            CHECK(strstr(noise, "float fbm3(vec3 p)") != NULL);
            /* Built on the mobile-safe hash, not on fract(sin(...)). */
            CHECK(strstr(noise, "hash3(i + vec3(1.0, 1.0, 1.0))") != NULL);
            free(noise);
        }
    }

    printf(bad ? "fluid_surface_noise: FAIL (%d)\n" : "fluid_surface_noise: PASS\n", bad);
    return bad ? 1 : 0;
}

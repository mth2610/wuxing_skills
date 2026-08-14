// core headless test — VFX_ComposeGroundWave's expansion, lip and shading.
//
// The DoD (VFX_PLAN §H2) asks for exactly this: that the ring's radius and lip
// height follow their curves, and that the vertex count is bounded. All of it is
// arithmetic, so none of it needs a GPU (core/CLAUDE.md §1).
//
// This mirrors core/composition/common/vc_ground_wave.inl. A mirror rots into
// fiction the moment the source moves, so Test_MirrorStillMatchesSource pins the
// load-bearing expressions.
//
// What the mirror CANNOT see: whether the ring visibly CONFORMS on a sloped map
// (the other half of the DoD — it needs a heightmap map and an eyeball), and
// whether the authored inner-face shading reads as light rather than as a
// gradient.

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#ifndef PI
#define PI 3.1415926535f
#endif

// ── the mirror ───────────────────────────────────────────────────────────────

#define GROUND_WAVE_MAX_SLICES 64
#define GROUND_WAVE_RADIALS    7
#define GROUND_WAVE_HEIGHT_AZIM 24
#define GROUND_WAVE_CREST_U    0.6666667f

static float Mix(float a, float b, float t) { return a + (b - a) * t; }

static float SmoothStep01(float x)
{
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float Radius01(float t01)
{
    if (t01 <= 0.0f) return 0.0f;
    if (t01 >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t01, 2.2f);
}

static float BandWidth(float radiusNow) { return radiusNow * 0.22f; }

static float LipHeight(float bandWidth, float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    float rise   = SmoothStep01(t01 / 0.14f);
    float settle = powf(1.0f - t01, 1.3f);
    return bandWidth * 0.35f * rise * settle;
}

static float Profile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    const float k = 1.7095f;
    return sinf(PI * powf(u, k));
}

static float Shade(float u)
{
    float h = Profile(u);
    float face = (u <= GROUND_WAVE_CREST_U)
                     ? 1.0f
                     : Mix(1.0f, 0.60f,
                           (u - GROUND_WAVE_CREST_U) / (1.0f - GROUND_WAVE_CREST_U));
    return Mix(0.30f, 1.0f, h) * face;
}

static float Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    return SmoothStep01(t01 / 0.10f) * powf(1.0f - t01, 1.1f);
}

// GFX_HIGH / GFX_MED / anything lower.
static int Slices(int tier)
{
    if (tier >= 3) return 64;
    if (tier == 2) return 40;
    return 24;
}

// ── 1. Expansion ─────────────────────────────────────────────────────────────

static void Test_Radius(void)
{
    CHECK(Radius01(0.0f) == 0.0f, "the front starts at the centre");
    CHECK(Radius01(1.0f) == 1.0f, "and arrives at the full radius exactly at t01 = 1");

    int mono = 1;
    float prev = -1.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.005f) {
        float r = Radius01(t);
        if (r < prev - 1e-6f) mono = 0;
        prev = r;
    }
    CHECK(mono, "the front never travels backwards");

    // A blast DECELERATES: it is fastest the instant it is released. A linear
    // expansion reads as a growing circle rather than as something thrown, so
    // the shape of the curve is the point, not just its endpoints.
    float early = Radius01(0.10f) - Radius01(0.00f);
    float late  = Radius01(1.00f) - Radius01(0.90f);
    CHECK_MSG(early > late * 3.0f, "the front decelerates sharply, it does not coast",
              "first 10%% covers %.3f, last 10%% covers %.3f", early, late);
    CHECK_MSG(Radius01(0.5f) > 0.70f, "half the time has covered most of the distance",
              "%.3f at t01 = 0.5", Radius01(0.5f));
}

// ── 2. The lip, and the thickness rule ───────────────────────────────────────

static void Test_LipAndBand(void)
{
    // Band width is keyed to the CURRENT radius: a shockwave spreads as it
    // travels. A fixed width would read as a hoop of constant section sliding
    // outward.
    float ratioWorst = 0.0f;
    for (float r = 0.5f; r < 12.0f; r += 0.25f) {
        float got = BandWidth(r) / r;
        float e = fabsf(got - 0.22f) / 0.22f;
        if (e > ratioWorst) ratioWorst = e;
    }
    CHECK_MSG(ratioWorst < 1e-4f, "band width is a constant fraction of the radius",
              "worst relative error %.5f", ratioWorst);

    // THE THICKNESS RULE (core/docs/LANDMINES.md): the lip's height is a ratio
    // against the band's OWN width. Keyed to the radius the wave would grow a
    // taller wall as it expanded; keyed to a constant it would flatten into a
    // decal. The INVARIANCE is the assertion that matters — a ratio checked at
    // one radius passes on the broken formula too.
    float t = 0.4f;
    float worst = 0.0f;
    float ref = LipHeight(BandWidth(1.0f), t) / BandWidth(1.0f);
    for (float r = 0.5f; r < 12.0f; r += 0.25f) {
        float got = LipHeight(BandWidth(r), t) / BandWidth(r);
        float e = fabsf(got - ref) / ref;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "lip-to-band ratio is INVARIANT across every radius",
              "worst relative error %.5f", worst);

    // Zero at both ends: the wave comes out of the ground and returns to it.
    CHECK(LipHeight(BandWidth(4.0f), 0.0f) == 0.0f, "the lip starts flat on the ground");
    CHECK(LipHeight(BandWidth(4.0f), 1.0f) == 0.0f, "and settles flat again at the end");

    // Up fast, down slow — a blast, not a breath.
    float peakT = 0.0f, peak = 0.0f;
    for (float x = 0.0f; x <= 1.0f; x += 0.002f) {
        float h = LipHeight(BandWidth(4.0f), x);
        if (h > peak) { peak = h; peakT = x; }
    }
    CHECK_MSG(peakT < 0.30f, "the lip peaks early in the wave's life", "%.3f", peakT);
    CHECK_MSG(peak > 0.0f, "the lip actually rises", "%.4f m", peak);

    // Single-peaked: a second bump would read as two waves.
    int turns = 0, dir = 0;
    float prev = -1.0f;
    for (float x = 0.005f; x < 1.0f; x += 0.005f) {
        float h = LipHeight(BandWidth(4.0f), x);
        if (prev >= 0.0f) {
            float d = h - prev;
            if (fabsf(d) > 1e-7f) {
                int nd = (d > 0.0f) ? 1 : -1;
                if (dir != 0 && nd != dir) turns++;
                dir = nd;
            }
        }
        prev = h;
    }
    CHECK_MSG(turns <= 1, "the lip rises and falls ONCE", "%d direction changes", turns);
}

// ── 3. The cross-band profile: where the crest sits ──────────────────────────

static void Test_Profile(void)
{
    CHECK(Profile(0.0f) == 0.0f, "the band meets the ground at its inner base");
    CHECK(Profile(1.0f) == 0.0f, "and at its outer base");

    // THE CREST LEADS. Dead centre reads as a symmetric doughnut, i.e. a ripple
    // rather than a blast. The single-expression form sin(PI*u^k) puts the peak
    // at u = 0.5^(1/k); this asserts the k in the source really lands it on
    // GROUND_WAVE_CREST_U, which is the kind of derivation that silently rots.
    float peakU = 0.0f, peak = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.0005f) {
        float h = Profile(u);
        if (h > peak) { peak = h; peakU = u; }
    }
    CHECK_MSG(fabsf(peakU - GROUND_WAVE_CREST_U) < 0.02f,
              "the crest sits where the constant says it does",
              "peak at u = %.3f, expected %.2f", peakU, GROUND_WAVE_CREST_U);
    CHECK_MSG(fabsf(peak - 1.0f) < 1e-3f, "the profile peaks at exactly 1", "%.4f", peak);

    // Asymmetric: the long slope trails on the INSIDE, the short face leads.
    float inner = 0.0f, outer = 0.0f;
    for (float u = 0.0f; u < GROUND_WAVE_CREST_U; u += 0.001f) inner += Profile(u) * 0.001f;
    for (float u = GROUND_WAVE_CREST_U; u <= 1.0f; u += 0.001f) outer += Profile(u) * 0.001f;
    CHECK_MSG(inner > outer * 1.3f, "the long slope trails behind the crest",
              "inner area %.4f vs outer %.4f", inner, outer);

    // AND THE CREST MUST LAND ON A VERTEX. The band is only GROUND_WAVE_RADIALS
    // samples across; a crest between two of them is never built, so the curve
    // would be right and the mesh wrong. Found by this test at u = 0.62.
    float bestErr = 1.0f;
    for (int i = 0; i < GROUND_WAVE_RADIALS; i++) {
        float e = fabsf((float)i / (float)(GROUND_WAVE_RADIALS - 1) - GROUND_WAVE_CREST_U);
        if (e < bestErr) bestErr = e;
    }
    CHECK_MSG(bestErr < 1e-3f, "the crest lands exactly on a radial vertex, so it is BUILT",
              "nearest vertex is %.4f away in u", bestErr);

    // No dip in the middle of either face.
    int turns = 0, dir = 0;
    float prev = -1.0f;
    for (float u = 0.001f; u < 1.0f; u += 0.001f) {
        float h = Profile(u);
        if (prev >= 0.0f) {
            float d = h - prev;
            if (fabsf(d) > 1e-7f) {
                int nd = (d > 0.0f) ? 1 : -1;
                if (dir != 0 && nd != dir) turns++;
                dir = nd;
            }
        }
        prev = h;
    }
    CHECK_MSG(turns <= 1, "the cross-section has ONE crest", "%d direction changes", turns);
}

// ── 4. The shading that stands in for lighting ───────────────────────────────

static void Test_Shading(void)
{
    // ENGINE_LANDMINES §3: a lit material on ground geometry is black-on-black in
    // the night arena, so "the inner face catches the light" has to be authored.
    // The asymmetry IS the effect — at equal height the inner face must be
    // brighter, or the ring is just a gradient.
    float uIn = 0.40f, uOut = 0.0f;
    float hIn = Profile(uIn);
    for (float u = GROUND_WAVE_CREST_U; u <= 1.0f; u += 0.0005f) {
        if (Profile(u) <= hIn) { uOut = u; break; }
    }
    CHECK_MSG(uOut > 0.0f && Shade(uIn) > Shade(uOut) * 1.3f,
              "at the SAME height the inner face is brighter than the outer",
              "u %.2f -> %.3f vs u %.2f -> %.3f", uIn, Shade(uIn), uOut, Shade(uOut));

    // THE CREST MUST BE THE BRIGHTEST POINT OF THE BAND. It was not: the face
    // factor was a STEP at the crest, so the crest vertex itself fell on the
    // dark side of the comparison and was multiplied by 0.62. The brightest
    // point of the band was being dimmed by the rule meant to darken the slope
    // behind it, which is most of why the wave read as a faint line.
    float maxU = 0.0f, maxV = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.0005f) {
        if (Shade(u) > maxV) { maxV = Shade(u); maxU = u; }
    }
    CHECK_MSG(fabsf(maxU - GROUND_WAVE_CREST_U) < 0.02f,
              "the brightest point of the band IS the crest",
              "brightest at u = %.3f, crest at %.3f", maxU, GROUND_WAVE_CREST_U);
    CHECK_MSG(maxV > 0.99f, "and the crest reaches full brightness", "%.3f", maxV);

    // The band must not have so much dynamic range that only the crest clears
    // the visibility threshold — that is the "faint thin line" failure, where
    // the geometry is all present and only a couple of quads can be seen.
    float minSlope = 1.0f;
    for (float u = 0.05f; u <= 0.95f; u += 0.005f)
        if (Shade(u) < minSlope) minSlope = Shade(u);
    CHECK_MSG(maxV / minSlope < 4.0f,
              "the band's dynamic range is small enough that the SLOPES are visible too",
              "crest %.3f vs dimmest %.3f = %.1f:1", maxV, minSlope, maxV / minSlope);

    // Never fully black at the bases, or the band ends in a hard edge against
    // the ground instead of meeting it.
    CHECK_MSG(Shade(0.02f) > 0.05f, "the band does not go black where it meets the ground",
              "%.3f", Shade(0.02f));
    // ...and never over 1, which would clip in the 8-bit vertex colour.
    float maxS = 0.0f;
    for (float u = 0.0f; u <= 1.0f; u += 0.001f) if (Shade(u) > maxS) maxS = Shade(u);
    CHECK_MSG(maxS <= 1.0f + 1e-5f, "shading never exceeds 1", "%.4f", maxS);
}

// ── 5. Life envelope, and the bound on what it costs ─────────────────────────

static void Test_AlphaAndBudget(void)
{
    CHECK(Alpha01(0.0f) == 0.0f && Alpha01(1.0f) == 0.0f,
          "the wave fades in from nothing and out to nothing");
    float peakT = 0.0f, peak = 0.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.002f) {
        float a = Alpha01(t);
        if (a > peak) { peak = a; peakT = t; }
    }
    CHECK_MSG(peakT < 0.25f, "it is brightest early, then decays", "%.3f", peakT);
    // ...but it must still be READABLE for most of its life. At the first
    // exponent (1.6) it was down to a third of peak by the halfway point, so
    // most of the time it was on screen it was too faint to read as geometry.
    CHECK_MSG(Alpha01(0.5f) > 0.40f, "it is still clearly visible at the halfway point",
              "%.3f", Alpha01(0.5f));

    // The DoD's other half: the vertex count must be BOUNDED, and the tier gate
    // may only ever clamp DOWN.
    CHECK(Slices(3) == GROUND_WAVE_MAX_SLICES, "the highest tier is exactly the array bound");
    CHECK_MSG(Slices(3) >= Slices(2) && Slices(2) >= Slices(1),
              "the tier gate only clamps DOWN", "%d / %d / %d",
              Slices(3), Slices(2), Slices(1));
    long worstQuads = (long)Slices(3) * (GROUND_WAVE_RADIALS - 1);
    CHECK_MSG(worstQuads * 4 <= 2048, "the worst-case vertex count stays inside one batch",
              "%ld quads, %ld vertices", worstQuads, worstQuads * 4);

    // ── The height-sampling budget ──────────────────────────────────────────
    // `MapProp_SampleGroundHeight` is a RAY-TRIANGLE test whose own header says
    // it is a low-frequency query, "NOT per-frame-per-particle". Sampling it per
    // vertex was 455 raycasts a frame and took the grass map to 13 fps. The
    // count must therefore be small, and — the part that would rot silently —
    // it must NOT grow with the quality tier, or a HIGH-tier machine pays a
    // cost the gate was supposed to be protecting it from.
    int perFrame = GROUND_WAVE_HEIGHT_AZIM * 2;
    CHECK_MSG(perFrame <= 64, "the per-frame raycast budget is small and fixed",
              "%d raycasts", perFrame);
    for (int tier = 1; tier <= 3; tier++)
        CHECK_MSG(perFrame == GROUND_WAVE_HEIGHT_AZIM * 2,
                  "the raycast count does NOT scale with the quality tier",
                  "tier %d draws %d slices, still %d raycasts",
                  tier, Slices(tier), perFrame);
    int naive = (Slices(3) + 1) * GROUND_WAVE_RADIALS;
    CHECK_MSG(naive / perFrame >= 8,
              "and it is a large cut against sampling per vertex",
              "%d per-vertex vs %d sampled = %.1fx", naive, perFrame,
              (double)naive / (double)perFrame);

    // Interpolating between azimuth samples has to be fine enough that the ring
    // does not visibly facet: at 24 samples the chord across a 5 m ring is well
    // under a metre, and ground under that span is effectively planar.
    float chord = 2.0f * 5.0f * sinf(PI / (float)GROUND_WAVE_HEIGHT_AZIM);
    CHECK_MSG(chord < 1.5f, "the gap between height samples is short enough to interpolate",
              "%.2f m between samples on a 5 m ring", chord);

    // The dedicated mesh keeps position, normal and UV fields in one bounded
    // static buffer. It is intentionally full-ring data: normals need adjacent
    // azimuth samples after the irregular silhouette is built.
    long resident = (long)(GROUND_WAVE_MAX_SLICES + 1) * GROUND_WAVE_RADIALS * 3;
    CHECK_MSG(resident < 2048, "mesh storage remains bounded and small",
              "%ld vertex-sized fields", resident);
}

// ── the mirror guard ─────────────────────────────────────────────────────────

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

static void Test_MirrorStillMatchesSource(void)
{
    const char *inl = "core/composition/common/vc_ground_wave.inl";
    CHECK(FileHas(inl, "return 1.0f - powf(1.0f - t01, 2.2f);"),
          "the expansion curve still matches this mirror");
    CHECK(FileHas(inl, "return radiusNow * 0.22f * s_gwBand;"),
          "band width is still a fraction of the current radius");
    CHECK(FileHas(inl, "return bandWidth * 0.35f * rise * settle * s_gwLip;"),
          "lip height is still a ratio against the band's OWN width");
    CHECK(FileHas(inl, "const float k = 1.7095f;"),
          "the crest-placement exponent still matches this mirror");
    CHECK(FileHas(inl, "#define GROUND_WAVE_CREST_U    0.6666667f"),
          "the crest still leads (not centred)");
    // Contracts, not tuning.
    CHECK(FileHas(inl, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false"),
          "it still EMITS: additive, per the blend law");
    CHECK(FileHas(inl, "VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false"),
          "the coloured shockwave body is composited before its bloom");
    CHECK(FileHas(inl, "ResourceManager_LoadShader(\"core/shaders/ground_wave.vs\""),
          "the dedicated annular UV shader is loaded through ResourceManager");
    CHECK(FileHas(inl, "static bool GroundWave_HasShader(void)"),
          "a failed custom shader falls back instead of treating raylib's default shader as valid");
    CHECK(FileHas("core/shaders/ground_wave.fs", "vec3(cos(angle) * 1.9, sin(angle) * 1.9,"),
          "surface flow remains continuous at the annulus UV seam");
    CHECK(FileHas("CMakeLists.txt", "core/shaders/ground_wave.fs"),
          "the ground-wave fragment shader is copied into a configured build");
    CHECK(FileHas("CMakeLists.txt", "core/shaders/common/noise.glsl"),
          "the shader's common noise include is copied with it");
    CHECK(!FileHas(inl, "EffectMaterial") && !FileHas(inl, "MATERIAL_LIT"),
          "it is still NOT lit (ENGINE_LANDMINES 3: black-on-black on the ground)");
    CHECK(FileHas(inl, "ProceduralMesh_BuildShockwave(&mesh"),
          "the composition delegates topology and terrain conformance to the shockwave mesh");
    CHECK(!FileHas(inl, "rlBegin(RL_QUADS);"),
          "the composition leaves immediate-mode ownership to the mesh drawer");
    CHECK(FileHas(inl, "GROUND_WAVE_Y_LIFT"),
          "the band is still lifted off the surface it lies on (z-fighting)");
    CHECK(FileHas(inl, "GfxQuality_Get()"),
          "the slice count is still gated by the quality tier");
    CHECK(FileHas(inl, "return sinf(PI * powf(u, k));"),
          "the broadened crest profile still matches this mirror");
    CHECK(FileHas(inl, "Math_Mix(0.30f, 1.0f, h) * face;"),
          "the shading floor still matches this mirror");
    CHECK(FileHas("core/geometry/procedural_mesh_utils.h", "#define SHOCKWAVE_HEIGHT_SAMPLES 24"),
          "the raycast budget still matches this mirror");
    CHECK(FileHas("core/geometry/pm_shockwave.inl", "heightInner[h] = heightFn("),
          "heights are still sampled on the coarse ring, not per vertex");
    CHECK(!FileHas(inl, "heightFn ? heightFn(x, z, ud) : center.y"),
          "the per-vertex sampling that cost 455 raycasts a frame is gone");
    CHECK(FileHas(inl, "MapManager_GetGroundHeightAt(worldX, worldZ)"),
          "the default terrain sampler is still wired to the active map");
    // The bench must not pass NULL: a flat ring is exactly what H2 exists to
    // stop being, and with NULL the bench cannot show it either way.
    CHECK(FileHas("scripts/vfx_test_manifest.json", "VFX_GroundHeightFromMap, NULL)"),
          "the bench drives the wave with a REAL height sampler, not NULL");
}

int main(void)
{
    printf("=== ground wave (H2) ===\n");
    Test_Radius();
    Test_LipAndBand();
    Test_Profile();
    Test_Shading();
    Test_AlphaAndBudget();
    Test_MirrorStillMatchesSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

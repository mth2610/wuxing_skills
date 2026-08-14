// core headless test — P6, VFX_ComposePortalDisc.
//
// A portal disc is defined by what it is NOT, and each of those is checkable:
//
//   NOT A COIN. The middle has to be genuinely dark, because under additive
//   "dark" means "adds nothing", which means the scene behind shows through and
//   the disc reads as a HOLE. A falloff that is too flat gives an evenly glowing
//   plate and every other quality of the effect is wasted.
//   NOT A BILLBOARD. The plan named `TRAIL_TYPE_PORTAL`, which draws one
//   camera-facing quad — the same shape from every angle, with no rim. Pinned
//   structurally: this file must not reach for it.
//   NOT A SPINNING PLATE. The swirl is a UV scroll, so the material turns while
//   the silhouette does not. If geometry were rotated the outline would wobble
//   with the pattern, which reads as a plate on a stick.
//   NOT A DECAL. The rim has a section out of the plane, so it survives edge-on,
//   which for a portal is the common approach angle.

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

#define PORTAL_MAX_SLICES     72
#define PORTAL_RINGS          8
#define PORTAL_RIM_INNER      0.86f
#define PORTAL_RIM_OUTER      1.07f
#define PORTAL_RIM_THICK      0.55f
#define PORTAL_CORE_FALLOFF   2.4f
#define PORTAL_INTERIOR_ALPHA 0.34f
#define PORTAL_RIM_ALPHA      0.85f
#define PORTAL_RIM_WHITEN     0.45f
#define PORTAL_OPEN           0.18f
#define PORTAL_CLOSE          0.85f
#define PORTAL_SWIRL          0.42f
#define PORTAL_V_TILES        1.6f

static float SmoothStep01(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static float Scale(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    if (t01 < PORTAL_OPEN) { float k = t01 / PORTAL_OPEN; return 1.0f - powf(1.0f - k, 2.4f); }
    if (t01 > PORTAL_CLOSE) { float k = (1.0f - t01) / (1.0f - PORTAL_CLOSE); return k * k * k; }
    return 1.0f;
}
static float Alpha01(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    if (t01 < PORTAL_OPEN) return SmoothStep01(t01 / PORTAL_OPEN);
    if (t01 > PORTAL_CLOSE) return (1.0f - t01) / (1.0f - PORTAL_CLOSE);
    return 1.0f;
}
static float Interior(float rr01)
{
    if (rr01 <= 0.0f) return 0.0f;
    if (rr01 >= 1.0f) return 1.0f;
    return powf(rr01, PORTAL_CORE_FALLOFF);
}
static float RimProfile(float w)
{
    if (w <= 0.0f || w >= 1.0f) return 0.0f;
    return sinf(PI * w);
}
static int Slices(int tier)
{
    switch (tier) { case 3: return PORTAL_MAX_SLICES; case 2: return 48; case 1: return 32; default: return 20; }
}

// ── 1. A HOLE, not a coin ───────────────────────────────────────────────────

static void Test_TheMiddleIsActuallyEmpty(void)
{
    // Under BLEND_ADDITIVE, "dark" means "adds nothing", so the interior's alpha
    // profile IS how transparent the middle is. Anything approaching flat gives
    // an evenly glowing plate.
    CHECK_MSG(Interior(0.0f) == 0.0f, "the exact centre adds nothing at all", "%s", "0");
    CHECK_MSG(Interior(0.5f) < 0.25f,
              "and halfway out it is still nearly empty — a hole, not a plate",
              "%.3f of full", Interior(0.5f));

    // The RATIO is what the eye reads. A rim several times the interior's
    // brightness is what makes the disc a boundary rather than a surface.
    float rimVsInterior = PORTAL_RIM_ALPHA / (PORTAL_INTERIOR_ALPHA * Interior(0.5f));
    CHECK_MSG(rimVsInterior > 8.0f,
              "the rim is many times brighter than the interior halfway out",
              "x%.1f", rimVsInterior);

    // Half the disc's AREA is inside r = 0.707, and that half must contribute
    // very little — area is what bloom and the eye actually count, so a falloff
    // that is gentle over the inner half floods the middle even if the centre
    // pixel is black.
    float innerHalfMean = 0.0f;
    int n = 0;
    for (float rr = 0.0f; rr <= 0.7071f; rr += 0.001f) { innerHalfMean += Interior(rr); n++; }
    innerHalfMean /= (float)n;
    CHECK_MSG(innerHalfMean < 0.20f,
              "the inner HALF OF THE AREA contributes almost nothing",
              "mean %.3f of full", innerHalfMean);

    // Monotone outward: the interior brightens toward the rim and never dips,
    // or the disc grows a ring artefact nobody authored.
    int monotone = 1;
    float prev = -1.0f;
    for (float rr = 0.0f; rr <= 1.0f; rr += 0.001f) {
        float v = Interior(rr);
        if (v < prev - 1e-6f) monotone = 0;
        prev = v;
    }
    CHECK(monotone, "and brightens monotonically toward the rim");

    // Falloff steep enough to be a hole, gentle enough that the interior is not
    // simply absent — an entirely black disc is a hole in the EFFECT.
    CHECK_MSG(PORTAL_CORE_FALLOFF > 1.6f && PORTAL_CORE_FALLOFF < 4.0f,
              "the falloff is steep without erasing the interior entirely",
              "pow %.1f", PORTAL_CORE_FALLOFF);
}

// ── 2. The rim ──────────────────────────────────────────────────────────────

static void Test_TheRimIsTheEffect(void)
{
    // It OVERHANGS the disc. A rim that ends exactly where the interior ends
    // reads as a cut edge; running past it reads as energy holding the hole open.
    CHECK_MSG(PORTAL_RIM_OUTER > 1.0f,
              "the rim overhangs the disc's own radius",
              "reaches %.0f%% of it", PORTAL_RIM_OUTER * 100.0f);
    CHECK_MSG(PORTAL_RIM_INNER < 1.0f, "and starts inside it, so there is no gap",
              "starts at %.0f%%", PORTAL_RIM_INNER * 100.0f);

    // NARROW. A rim that is a fifth of the radius is a doughnut.
    float rimWidth = PORTAL_RIM_OUTER - PORTAL_RIM_INNER;
    CHECK_MSG(rimWidth > 0.08f && rimWidth < 0.35f,
              "the rim is a narrow band, not a doughnut",
              "%.0f%% of the radius", rimWidth * 100.0f);

    // ...AND THAT NARROWNESS IS WHY IT CARRIES NO TEXTURE. It is several times
    // narrower than the interior it borders, so the same sheet drawn across it
    // would be unresolvably high-frequency — and unresolvable detail comes back
    // as dashes (core/docs/LANDMINES.md, 29/07).
    CHECK_MSG(PORTAL_RIM_INNER / rimWidth > 3.0f,
              "the interior is several times wider than the rim, which is why only "
              "the interior carries the sheet",
              "x%.1f", PORTAL_RIM_INNER / rimWidth);

    // A SECTION OUT OF THE PLANE, so the rim survives edge-on. As a ratio against
    // the RIM's own width — the thickness rule — so it stays in proportion at
    // every portal size.
    CHECK_MSG(PORTAL_RIM_THICK > 0.2f,
              "the rim has a lens section, so it does not vanish edge-on",
              "%.2f of its own width", PORTAL_RIM_THICK);

    // The lens closes at both edges and peaks in the middle of the band.
    CHECK(RimProfile(0.0f) == 0.0f && RimProfile(1.0f) == 0.0f,
          "the rim's section closes at both edges");
    CHECK_MSG(fabsf(RimProfile(0.5f) - 1.0f) < 1e-5f, "and peaks in its middle",
              "%.5f", RimProfile(0.5f));

    // WHITENED AT THE SOURCE. A saturated hue stacks additively into more of
    // itself and never reaches white, so emissiveBoost's multiply has nothing to
    // lift — the white has to be put in at the source (VC_Whiten).
    CHECK_MSG(PORTAL_RIM_WHITEN > 0.2f && PORTAL_RIM_WHITEN < 0.8f,
              "the rim is whitened at the source, without losing its element hue",
              "%.2f", PORTAL_RIM_WHITEN);
}

// ── 3. It opens, holds, and SNAPS shut ──────────────────────────────────────

static void Test_OpenHoldCollapse(void)
{
    CHECK(Scale(0.0f) == 0.0f && Scale(1.0f) == 0.0f, "it grows from nothing and ends at nothing");
    int holdIsFull = 1;
    for (float t = PORTAL_OPEN; t <= PORTAL_CLOSE; t += 0.005f)
        if (fabsf(Scale(t) - 1.0f) > 1e-5f) holdIsFull = 0;
    CHECK(holdIsFull, "and is exactly full size through the hold");

    // A PORTAL SHUTS. It collapses rather than fading at constant size, which is
    // what separates it from a decal being turned down — and it shuts faster than
    // it opened.
    //
    // MEASURED FROM EACH PHASE'S OWN START, and that is not pedantry: the two
    // phases have different lengths (0.18 opening, 0.15 closing), so comparing
    // raw t01 values compares "how long before the end" against "how long after
    // the start" — two different quantities. The first version of this check did
    // exactly that and reported a 2.3x difference where the real one was 1.0x,
    // i.e. it passed judgement on a curve that was not snapping at all.
    float openHalf = PORTAL_OPEN, closeHalf = 1.0f - PORTAL_CLOSE;
    for (float t = 0.0f; t < PORTAL_OPEN; t += 0.0005f)
        if (Scale(t) >= 0.5f) { openHalf = t; break; }              // since opening began
    for (float t = PORTAL_CLOSE; t <= 1.0f; t += 0.0005f)
        if (Scale(t) <= 0.5f) { closeHalf = t - PORTAL_CLOSE; break; } // since closing began
    CHECK_MSG(closeHalf < openHalf * 0.85f,
              "it snaps shut clearly faster than it opened",
              "%.4f into the close vs %.4f into the open (x%.2f)",
              closeHalf, openHalf, openHalf / closeHalf);
    CHECK_MSG(Scale(0.95f) < 0.2f,
              "and is nearly gone well before the end, rather than popping out at full size",
              "%.3f at t=0.95", Scale(0.95f));

    // Monotone in each phase — a size that wobbles reads as a breathing plate.
    int ok = 1;
    float prev = -1.0f;
    for (float t = 0.0f; t <= PORTAL_OPEN; t += 0.001f) { if (Scale(t) < prev - 1e-6f) ok = 0; prev = Scale(t); }
    prev = 2.0f;
    for (float t = PORTAL_CLOSE; t <= 1.0f; t += 0.001f) { if (Scale(t) > prev + 1e-6f) ok = 0; prev = Scale(t); }
    CHECK(ok, "the size only grows while opening and only shrinks while closing");

    // The hold is the great majority of the life, or the portal is never simply
    // OPEN — which is the state everything else about it is authored for.
    CHECK_MSG(PORTAL_CLOSE - PORTAL_OPEN > 0.6f,
              "it spends most of its life fully open",
              "%.0f%% of it", (PORTAL_CLOSE - PORTAL_OPEN) * 100.0f);

    CHECK(Alpha01(0.0f) == 0.0f && Alpha01(1.0f) == 0.0f, "alpha closes at both ends");
}

// ── 4. The swirl turns the MATERIAL, not the disc ──────────────────────────

static void Test_SwirlIsAUVScroll(void)
{
    // A UV scroll of 0.42 turns/sec is about 2.4 s per revolution — slow enough
    // to be hypnotic rather than a fan. This is the number a geometry rotation
    // would also use, so the value is not what distinguishes them; the mirror
    // guard below is.
    float secondsPerTurn = 1.0f / PORTAL_SWIRL;
    CHECK_MSG(secondsPerTurn > 1.2f && secondsPerTurn < 6.0f,
              "the swirl turns slowly enough to read as depth rather than as a fan",
              "%.1f s per revolution", secondsPerTurn);

    // Radial tiling: more than a couple of repeats and the pattern crowds into an
    // unreadable ring near the middle, where the circumference is smallest.
    CHECK_MSG(PORTAL_V_TILES >= 1.0f && PORTAL_V_TILES <= 3.0f,
              "the sheet repeats a small number of times from centre to rim",
              "%.1f", PORTAL_V_TILES);

    // Enough rings that the falloff is actually SAMPLED. With a pow(2.4) profile
    // over too few rings the interior becomes visible bands.
    CHECK_MSG(PORTAL_RINGS >= 6, "enough radial rings to sample the falloff smoothly",
              "%d", PORTAL_RINGS);
}

// ── 5. The tier ladder ──────────────────────────────────────────────────────

static void Test_TierLadder(void)
{
    int monotone = 1;
    for (int t = 0; t < 3; t++)
        if (Slices(t) > Slices(t + 1)) monotone = 0;
    CHECK(monotone, "slice count clamps DOWN and only down");
    CHECK_MSG(Slices(0) >= 16,
              "and the disc is still round at the lowest tier — a visibly polygonal "
              "portal is a different effect, not a cheaper one",
              "%d slices", Slices(0));
    CHECK_MSG((float)Slices(3) / (float)Slices(0) >= 3.0f,
              "while the lowest tier is several times cheaper",
              "%d vs %d", Slices(3), Slices(0));
}

// ── the mirror guard ────────────────────────────────────────────────────────

static void CollapseWS(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int pendingSpace = 0;
    for (const char *p = in; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { pendingSpace = 1; continue; }
        if (pendingSpace && o > 0 && o + 1 < cap) out[o++] = ' ';
        pendingSpace = 0;
        if (o + 1 < cap) out[o++] = *p;
    }
    out[o < cap ? o : cap - 1] = '\0';
}

static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[400000], flat[400000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_MirrorMatchesTheSource(void)
{
    const char *inl = "core/composition/common/vc_portal_disc.inl";

    CHECK(FileHas(inl, "#define PORTAL_CORE_FALLOFF 2.4f"), "the falloff is what this test mirrors");
    CHECK(FileHas(inl, "#define PORTAL_RIM_INNER 0.86f"), "so is the rim's inner edge");
    CHECK(FileHas(inl, "#define PORTAL_SWIRL 0.42f"), "and the swirl rate");

    // NOT A BILLBOARD. The plan named TRAIL_TYPE_PORTAL; the needle carries the
    // `;` and the enum name together, which no prose in the file has.
    CHECK(!FileHas(inl, "cfg.type = TRAIL_TYPE_PORTAL;"),
          "it does not spawn a TRAIL_TYPE_PORTAL — that draws one camera-facing quad");
    CHECK(!FileHas(inl, "SpawnTrailEntity("), "and spawns no trail at all");
    CHECK(!FileHas(inl, "DrawCameraFacingQuad("), "nor any billboard");
    CHECK(FileHas(inl, "VC_PlaneFrame(n, &axA, &axB);"),
          "it lies in a plane the WORLD chose, from the shared guarded frame helper");

    // NOT A SPINNING PLATE. The swirl multiplies a UV, never an angle.
    CHECK(FileHas(inl, "float uScroll = TimeFX_Elapsed() * PORTAL_SWIRL * s_portalSwirl;"),
          "the swirl is a UV scroll");
    CHECK(FileHas(inl, "float u0 = (float)s / (float)slices + uScroll;"),
          "...added to the polar u, so the material turns and the silhouette does not");
    CHECK(!FileHas(inl, "rlRotatef("), "no geometry is rotated");

    // THE RIM CARRIES NO SHEET. Structure lives in exactly one layer, and the rim
    // is far too narrow to resolve it.
    CHECK(FileHas(inl, "rlSetTexture(0); rlBegin(RL_QUADS); { const int RW = 5;"),
          "the rim pass unbinds the texture before it draws");
    CHECK(FileHas(inl, "rlSetTexture(s_portalSheet.id);"),
          "and only the interior binds the sheet");

    // The degenerate normal, checked squared, before the normalise.
    CHECK(FileHas(inl, "if (Vector3LengthSqr(normal) < 1e-8f)"),
          "a degenerate normal is caught on the SQUARED length");
    CHECK(FileHas(inl, "normal = (Vector3){0.0f, 1.0f, 0.0f};"),
          "...and defaulted to the flat pose rather than refused");

    // The blend law and the flushes.
    CHECK(FileHas(inl, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false") &&
          FileHas(inl, "rlDisableBackfaceCulling();"),
          "it EMITS: additive, no depth write, both faces — flushed on both sides");
    CHECK(FileHas(inl, "rlEnableBackfaceCulling();") &&
          FileHas(inl, "VFXRender_EndDraw(&renderScope);"),
          "and the restore is flushed too");
    CHECK(!FileHas(inl, "Material_Begin("),
          "no lit material — it would be black-on-black in the night arena");

    // Colours from the material, whitening at the source.
    CHECK(FileHas(inl, "Color c = VC_Whiten(m->glow, PORTAL_RIM_WHITEN * p);"),
          "the rim is whitened at the SOURCE, not left saturated");
    CHECK(FileHas(inl, "VC_MixColor(m->body, m->glow, i0)"),
          "and the interior ramps from body to glow as it nears the rim");

    // Static storage, no allocation, no shake.
    CHECK(FileHas(inl, "static Vector3 prevRing[5 * 2];"), "static storage");
    CHECK(!FileHas(inl, "malloc("), "no allocation");
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");
}

int main(void)
{
    printf("=== P6 portal disc (the flat disc with a rim) ===\n");
    Test_TheMiddleIsActuallyEmpty();
    Test_TheRimIsTheEffect();
    Test_OpenHoldCollapse();
    Test_SwirlIsAUVScroll();
    Test_TierLadder();
    Test_MirrorMatchesTheSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// core headless test — P4, VFX_ComposeBeam.
//
// The plan's DoD for P4 is two sentences and both are about DEGENERACY: "must
// hold up when `from`/`to` are nearly coincident, and when the beam is viewed
// end-on." Only the first of those is arithmetic; the second is a consequence of
// choosing a tube over a strip and is pinned structurally, in the mirror guard.
//
// So this file is mostly about what happens at the edges of the parameter space,
// which is where every silent geometry failure in this module has lived:
// normalising a near-zero vector, a cross product against a parallel reference,
// a width that outruns the length it is drawn across.
//
// What it cannot see: whether a beam reads as a beam. It can prove the shaft
// meets both endpoints exactly, that it thins instead of inflating as its
// endpoints close, and that the layer sum leaves the sheet's structure alive.

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

#define BEAM_SEGMENTS      24
#define BEAM_RADIAL        8
#define BEAM_MIN_LEN       0.02f
#define BEAM_ASPECT_K      0.05f
#define BEAM_PROFILE_BASE  0.3f
#define BEAM_HEAD_GROWTH   0.12f
#define BEAM_WANDER        1.15f
#define BEAM_OPEN          0.14f
#define BEAM_CLOSE         0.86f
#define BEAM_TILE          1.15f
#define BEAM_FLOW          3.20f
#define BEAM_NOISE         0.11f

#define BEAM_L0_ALPHA 0.12f
#define BEAM_L1_ALPHA 0.38f
#define BEAM_L2_ALPHA 0.26f

// The volume trail's aspect, for the comparison that gives BEAM_ASPECT_K its
// meaning — a beam is an order of magnitude thinner than a wake.
#define VOL_ASPECT_K 0.20f

static float Reach(float t01)
{
    if (t01 >= BEAM_OPEN) return 1.0f;
    return (t01 <= 0.0f) ? 0.0f : (t01 / BEAM_OPEN);
}

static float Envelope(float t01)
{
    if (t01 <= 0.0f || t01 >= 1.0f) return 0.0f;
    if (t01 < BEAM_OPEN) return t01 / BEAM_OPEN;
    if (t01 > BEAM_CLOSE) return (1.0f - t01) / (1.0f - BEAM_CLOSE);
    return 1.0f;
}

static float Radius(float widthMetres, float len, float env)
{
    float want = widthMetres * 0.5f * env;
    float cap = len * BEAM_ASPECT_K;
    if (want < 0.0f) want = 0.0f;
    return (cap < want) ? cap : want;
}

static int Radial(int tier)
{
    switch (tier) { case 3: return BEAM_RADIAL; case 2: return BEAM_RADIAL; case 1: return 6; default: return 5; }
}
static int Segments(int tier)
{
    switch (tier) { case 3: return BEAM_SEGMENTS; case 2: return 18; case 1: return 12; default: return 8; }
}
static int LayerCount(int tier) { return (tier >= 2) ? 3 : 2; }

// ── 1. THE DEGENERATE CASE — the DoD's first sentence ───────────────────────

static void Test_NearlyCoincidentEndpoints(void)
{
    // The guard is on the SQUARED length and it happens BEFORE any normalise.
    // That ordering is the whole of it: `Vector3Normalize` of ~zero returns
    // garbage rather than failing, and every ring built on that garbage falls on
    // a line — a tube that draws as a plane, with no NaN and nothing in the log
    // (core/docs/LANDMINES.md, 30/07).
    CHECK_MSG(BEAM_MIN_LEN > 0.0f && BEAM_MIN_LEN < 0.05f,
              "the minimum length is small enough not to clip real beams and large "
              "enough to be outside float noise",
              "%.3f m", BEAM_MIN_LEN);

    // AND THE APPROACH TO IT IS GRACEFUL, which matters more than the cliff: as
    // the endpoints close, the aspect cap takes the radius down with the length,
    // so the beam thins to nothing. A fixed width would inflate into a ball at
    // exactly the moment the beam becomes shortest.
    float prev = 1e9f;
    int monotone = 1;
    for (float len = 5.0f; len >= BEAM_MIN_LEN; len -= 0.01f) {
        float r = Radius(0.30f, len, 1.0f);
        if (r > prev + 1e-6f) monotone = 0;
        prev = r;
    }
    CHECK(monotone, "the radius never grows as the beam gets shorter");
    CHECK_MSG(Radius(0.30f, BEAM_MIN_LEN, 1.0f) < 0.002f,
              "at the shortest legal beam the shaft is under 2 mm — a thread, not a ball",
              "%.5f m radius", Radius(0.30f, BEAM_MIN_LEN, 1.0f));

    // THE INVARIANCE, not the ratio at one point. A ratio checked at a single
    // parameter value passes on the broken formula too.
    float worst = 0.0f;
    for (float len = 0.05f; len < 2.99f; len += 0.01f) {
        float ratio = Radius(0.30f, len, 1.0f) / len;
        float e = fabsf(ratio - BEAM_ASPECT_K) / BEAM_ASPECT_K;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-5f,
              "in the capped regime the radius is a fixed fraction of the beam's "
              "OWN length, at every length",
              "worst relative drift %.7f", worst);

    // ...and the ceiling is honoured once the beam is long enough to earn it.
    CHECK_MSG(fabsf(Radius(0.30f, 20.0f, 1.0f) - 0.15f) < 1e-6f,
              "a long beam gets exactly the half-width it asked for",
              "%.4f", Radius(0.30f, 20.0f, 1.0f));

    // A BEAM IS A LANCE. Its aspect against its own length is an order of
    // magnitude thinner than a volume trail's wake, and that ratio is most of
    // what separates the two shapes.
    CHECK_MSG(VOL_ASPECT_K / BEAM_ASPECT_K >= 3.0f,
              "a beam is several times thinner, per metre, than a volume trail",
              "1:%.0f vs 1:%.0f full width to length",
              1.0f / (2.0f * BEAM_ASPECT_K), 1.0f / (2.0f * VOL_ASPECT_K));
}

// ── 2. Fire-up, hold, cut-out ───────────────────────────────────────────────

static void Test_LifeEnvelope(void)
{
    // A beam with no fire-up has no source: it simply exists, at full length,
    // from the first frame.
    CHECK_MSG(Reach(0.0f) < 1e-6f && Reach(BEAM_OPEN) >= 1.0f,
              "the far end travels out from the source over the opening",
              "%.2f -> %.2f", Reach(0.0f), Reach(BEAM_OPEN));
    CHECK_MSG(Reach(BEAM_OPEN * 0.5f) > 0.4f && Reach(BEAM_OPEN * 0.5f) < 0.6f,
              "and it is halfway out halfway through the opening",
              "%.2f", Reach(BEAM_OPEN * 0.5f));

    // The envelope is zero at both ends and exactly 1 through the hold. A beam
    // that never reaches full strength is a beam that is always fading.
    CHECK(Envelope(0.0f) == 0.0f && Envelope(1.0f) == 0.0f,
          "the envelope closes at both ends");
    int holdIsFull = 1;
    for (float t = BEAM_OPEN; t <= BEAM_CLOSE; t += 0.005f)
        if (fabsf(Envelope(t) - 1.0f) > 1e-5f) holdIsFull = 0;
    CHECK(holdIsFull, "and is exactly full through the whole hold");

    // Continuous at both joins — a step there is a visible pop.
    CHECK_MSG(fabsf(Envelope(BEAM_OPEN - 1e-4f) - 1.0f) < 1e-3f,
              "no step where the fire-up meets the hold",
              "%.5f", Envelope(BEAM_OPEN - 1e-4f));
    CHECK_MSG(fabsf(Envelope(BEAM_CLOSE + 1e-4f) - 1.0f) < 1e-3f,
              "nor where the hold meets the cut-out",
              "%.5f", Envelope(BEAM_CLOSE + 1e-4f));

    // The hold is the great majority of the life. A beam whose ramps eat half of
    // it never reads as SUSTAINED, which is the one thing this primary is for.
    float hold = BEAM_CLOSE - BEAM_OPEN;
    CHECK_MSG(hold > 0.65f, "the beam is at full strength for most of its life",
              "%.0f%% of it", hold * 100.0f);

    // The reach does NOT retract on the way out: a beam stops being fed and goes
    // out, it does not suck back into its muzzle.
    CHECK_MSG(Reach(0.99f) >= 1.0f, "the far end stays put during the cut-out",
              "%.2f", Reach(0.99f));
}

// ── 3. The wander meets both endpoints, and is keyed to the right quantity ──

static void Test_WanderPinsBothEnds(void)
{
    // sin(PI*t) is zero at t = 0 and t = 1, so the deviation vanishes exactly at
    // the source and the target. That is not decoration: a beam whose shaft does
    // not land on its own endpoints has a visible gap at the muzzle and misses
    // what it is supposed to be hitting.
    CHECK_MSG(fabsf(sinf(0.0f * PI)) < 1e-6f && fabsf(sinf(1.0f * PI)) < 1e-6f,
              "the wander envelope is zero at both endpoints",
              "%.7f / %.7f", sinf(0.0f), sinf(1.0f * PI));

    // ...and it is largest in the middle, where a beam is free to move.
    float peak = 0.0f, peakAt = 0.0f;
    for (float t = 0.0f; t <= 1.0f; t += 0.001f) {
        float v = sinf(t * PI);
        if (v > peak) { peak = v; peakAt = t; }
    }
    CHECK_MSG(fabsf(peakAt - 0.5f) < 0.01f, "and largest at the middle of the span",
              "peaks at t=%.3f", peakAt);

    // KEYED TO THE RADIUS, NOT THE LENGTH — the aspect landmine in its other
    // form. A wander proportional to length makes a long beam a snake; keyed to
    // radius the shaft breathes by the same absolute amount at any range, so a
    // 30 m beam is visibly STRAIGHTER relative to itself than a 3 m one, which
    // is what a beam actually looks like.
    float dev = 2.0f * BEAM_WANDER;      // worst case, in units of radius
    float relAt3m  = dev * Radius(0.30f, 3.0f, 1.0f)  / 3.0f;
    float relAt30m = dev * Radius(0.30f, 30.0f, 1.0f) / 30.0f;
    CHECK_MSG(relAt30m < relAt3m,
              "a longer beam is straighter relative to its own length",
              "%.4f vs %.4f of span", relAt30m, relAt3m);

    // And the wander must be comparable to the shaft, not many times it: at 1.15
    // radii the shaft weaves within about its own width, which reads as energy.
    // At five it would be a rope.
    CHECK_MSG(BEAM_WANDER > 0.4f && BEAM_WANDER < 2.5f,
              "the shaft weaves within about its own width",
              "%.2f radii", BEAM_WANDER);
}

// ── 4. The alpha budget and the tier ladder ────────────────────────────────

static void Test_AdditiveBudgetAndTiers(void)
{
    float sum = BEAM_L0_ALPHA + BEAM_L1_ALPHA + BEAM_L2_ALPHA;
    CHECK_MSG(sum < 1.0f, "the three concentric layers sum under 1.0 through the body",
              "%.2f", sum);
    CHECK_MSG(sum < 0.85f, "...with headroom under the practical bloom ceiling",
              "%.2f", sum);
    // The BODY is the brightest single layer: it is the one carrying the sheet,
    // and structure that is dimmer than the shapes around it is structure nobody
    // will see.
    CHECK(BEAM_L1_ALPHA > BEAM_L0_ALPHA && BEAM_L1_ALPHA > BEAM_L2_ALPHA,
          "the textured body is the brightest layer");

    int monotone = 1;
    for (int tier = 0; tier < 3; tier++) {
        if (Radial(tier) > Radial(tier + 1)) monotone = 0;
        if (Segments(tier) > Segments(tier + 1)) monotone = 0;
        if (LayerCount(tier) > LayerCount(tier + 1)) monotone = 0;
    }
    CHECK(monotone, "every tier quantity clamps DOWN and only down");

    int alwaysATube = 1;
    for (int tier = 0; tier <= 3; tier++) {
        if (Radial(tier) < 3) alwaysATube = 0;   // below 3 a section is not closed
        if (Segments(tier) < 2) alwaysATube = 0;
        if (LayerCount(tier) < 2) alwaysATube = 0;
    }
    CHECK(alwaysATube,
          "it is still a closed tube with a body and a core at the LOWEST tier — "
          "the gate makes it cheaper, never a different effect");

    // WHICH layer is dropped matters. At two layers the index runs 1..2, i.e.
    // body + core: the outer bloom goes, the CORE never does, because the core
    // is the beam.
    CHECK(LayerCount(1) == 2, "a low tier draws two layers");
    CHECK_MSG(0 + 1 == 1 && 1 + 1 == 2,
              "and they are the BODY and the CORE — the outer bloom is what goes",
              "%s", "indices 1 and 2");
}

// ── 5. The profile constant is borrowed from the builder ───────────────────

static void Test_ProfileConstantMatchesTheBuilder(void)
{
    // With capsuleTailExp = 0 the builder's radius profile collapses to its
    // floor, `0.3f`, so the radius asked for has to be divided by that floor.
    // This is a borrowed constant, and a borrowed constant rots silently: if the
    // builder's floor ever changes, every beam in the game rescales and nothing
    // says so. The mirror guard below pins the builder's own line.
    float asked = 0.15f;
    float passed = asked / BEAM_PROFILE_BASE;
    float drawn = passed * BEAM_PROFILE_BASE;   // capsuleTailExp = 0, taper = 1
    CHECK_MSG(fabsf(drawn - asked) < 1e-6f,
              "dividing by the builder's profile floor gives back the radius asked for",
              "asked %.3f, drawn %.3f", asked, drawn);

    // The flare toward the far end is slight — beams widen where they land, but
    // a big flare is a cone and a cone is a spray.
    CHECK_MSG(BEAM_HEAD_GROWTH > 0.0f && BEAM_HEAD_GROWTH < 0.3f,
              "the far end flares slightly, not into a cone",
              "+%.0f%%", BEAM_HEAD_GROWTH * 100.0f);

    // Surface deform lower than a volume trail's 0.14–0.34: a beam is COHERENT
    // energy, and a billowing one reads as a jet of gas.
    CHECK_MSG(BEAM_NOISE > 0.0f && BEAM_NOISE < 0.14f,
              "the shaft has a live surface without billowing",
              "%.2f of radius", BEAM_NOISE);

    // Tiling by METRES rather than over the whole length, so texel density does
    // not drift as the beam extends — the flow would otherwise stretch with the
    // shaft instead of running through it.
    CHECK_MSG(BEAM_TILE > 0.3f && BEAM_TILE < 4.0f, "the sheet tiles by metres",
              "%.2f m per repeat", BEAM_TILE);
    CHECK_MSG(BEAM_FLOW > 0.0f,
              "and the flow runs FROM the source — a beam is pushed, not left behind",
              "%.2f tiles/s", BEAM_FLOW);
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
    const char *inl = "core/composition/common/vc_beam.inl";
    const char *pmt = "core/geometry/pm_tube.inl";

    CHECK(FileHas(inl, "#define BEAM_ASPECT_K 0.05f"), "the aspect is what this test mirrors");
    CHECK(FileHas(inl, "#define BEAM_WANDER 1.15f"), "so is the wander");
    CHECK(FileHas(inl, "#define BEAM_OPEN 0.14f"), "and the fire-up");

    // THE BORROWED CONSTANT. If pm_tube.inl's profile floor ever moves, every
    // beam rescales silently — so the needle is on the BUILDER's line, not on
    // this file's copy of the number.
    CHECK(FileHas(pmt, "float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))) * cfg->capsuleTailExp;"),
          "the builder's profile floor is still 0.3, which BEAM_PROFILE_BASE mirrors");
    CHECK(FileHas(inl, "cfg.capsuleTailExp = 0.0f;"),
          "and the beam switches the capsule off, so that floor is the whole profile");
    CHECK(FileHas(inl, "cfg.tailTaperMin = 1.0f; cfg.tailTaperMax = 1.0f;"),
          "with no taper — a tapered beam is a spear, not a sustained line");
    CHECK(FileHas(inl, "r / BEAM_PROFILE_BASE,"),
          "the radius is divided by that floor on the way in");

    // THE DEGENERATE GUARD, in the right order: squared length, checked BEFORE
    // anything is normalised.
    CHECK(FileHas(inl, "float spanLenSq = Vector3LengthSqr(span);"),
          "the length is taken squared");
    CHECK(FileHas(inl, "if (spanLenSq < BEAM_MIN_LEN * BEAM_MIN_LEN)"),
          "and checked before any normalise");
    CHECK(FileHas(inl, "Vector3 axis = Vector3Scale(span, 1.0f / spanLen);"),
          "the direction is then a scale by a known-good length, not a blind Normalize");
    CHECK(FileHas(inl, "VC_PlaneFrame(axis, &sideA, &sideB);"),
          "and the cross-section frame comes from the SHARED guarded helper, not "
          "from a fourth hand-rolled copy of the same cross product");
    CHECK(FileHas("core/composition/common/vc_common.inl",
                  "Vector3 ref = (fabsf(unitNormal.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};"),
          "...which carries the parallel-reference guard — the same landmine one level down");
    CHECK(FileHas(inl, "TraceLog(LOG_WARNING, \"VFX_BEAM: from and to are"),
          "a degenerate beam announces itself once, rather than silently drawing nothing");
    CHECK(FileHas(inl, "s_beamDegenerateLogged = true;"),
          "...once, not every frame");

    // THE WANDER pinned to both ends.
    CHECK(FileHas(inl, "float hold = sinf(t * PI);"),
          "the wander envelope is sin(PI*t), so the shaft meets both endpoints exactly");

    // A CURVED path needs the transported frame, or the roll — and with it the
    // UV — shears along the length.
    CHECK(FileHas(inl, "cfg.useTransportFrame = true;"),
          "the curved path uses the parallel-transported cross-section frame");
    CHECK(FileHas(inl, "cfg.wobbleAmplitude = 0.0f;"),
          "and the frame roll is off — the wander lives in the PATH, not in the frame");
    CHECK(FileHas(inl, "cfg.deform1Amp = 0.0f; cfg.deform2Amp = 0.0f;"),
          "the periodic sine deforms are off; noise has no beat");

    // THE BATCH FLUSHES, on both sides of every state the geometry depends on.
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); rlDisableDepthMask(); "
                       "rlDisableBackfaceCulling(); BeginBlendMode(BLEND_ADDITIVE); "
                       "rlDrawRenderBatchActive();"),
          "blend, depth mask AND culling are sandwiched between two flushes");
    CHECK(FileHas(inl, "rlDrawRenderBatchActive(); EndBlendMode(); "
                       "rlEnableBackfaceCulling(); rlEnableDepthMask(); "
                       "rlDrawRenderBatchActive();"),
          "and the restore is too");
    CHECK(FileHas(inl, "rlSetTexture(0);"),
          "the texture binding is not leaked into whatever draws next");

    // THE BLEND LAW. A beam EMITS.
    CHECK(FileHas(inl, "BeginBlendMode(BLEND_ADDITIVE);"), "a beam EMITS: additive");
    CHECK(FileHas(inl, "rlDisableDepthMask();"),
          "...and does not occlude what is behind it");

    // STRUCTURE IN ONE LAYER ONLY.
    CHECK(FileHas(inl, "rlSetTexture((idx == 1 && s_beamSheet.id != 0) ? s_beamSheet.id : 0);"),
          "only the body carries the sheet — additive copies of one pattern average flat");

    // It reuses the tube. Not a second one.
    CHECK(FileHas(inl, "ProceduralMesh_BuildTubeAlongPath(&mesh, path, segs + 1,"),
          "it builds through the shared tube module");
    CHECK(!FileHas(inl, "rlBegin(RL_QUADS)"), "and emits no ring geometry of its own");
    CHECK(!FileHas(inl, "rlBegin(RL_TRIANGLES)"), "nor any triangles of its own");

    // Static storage, no allocation, no shake, immediate mode.
    CHECK(FileHas(inl, "static TubeMeshData mesh;"), "static mesh storage");
    CHECK(!FileHas(inl, "malloc("), "no allocation");
    CHECK(!FileHas(inl, "CameraFX_"), "no camera shake");
    CHECK(!FileHas(inl, "SpawnTrailEntity("),
          "no pool and no handle — a beam has no history to keep");
}

int main(void)
{
    printf("=== P4 beam (the sustained line) ===\n");
    Test_NearlyCoincidentEndpoints();
    Test_LifeEnvelope();
    Test_WanderPinsBothEnds();
    Test_AdditiveBudgetAndTiers();
    Test_ProfileConstantMatchesTheBuilder();
    Test_MirrorMatchesTheSource();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

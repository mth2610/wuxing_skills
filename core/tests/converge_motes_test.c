// core headless test — P2, VFX_ComposeConvergeMotes, the second primary
// EXTRACTED from VFX_ComposeChargeConverge rather than invented.
//
// An extraction has one acceptance criterion above all others: THE LOOK MUST NOT
// CHANGE. The numbers moved from inside the composite into a function of their
// own, and if any of them drifted in transit then the whole argument for
// extracting — "costs no visual iteration, the look is already signed off" —
// evaporates, and P2 becomes as expensive as inventing something. So most of
// this file is a transcription check against the values as they were, plus the
// arithmetic that explains why the ones that are not obvious are what they are.
//
// This is deliberately the same shape as core/tests/core_glow_test.c, which
// exists for the same reason after the FIRST extraction out of this same
// composite.
//
// What the mirror CANNOT see: whether the threads read as qi. It can only prove
// they are still launched from a mesh, still driven by forces, still emitting by
// rate, and still carrying the numbers they were signed off with.

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

#define CONVERGE_SPAWN_CLAMP 24
#define CONVERGE_TRAIL_POINTS 8
#define CONVERGE_TRAIL_STEP 0.055f
#define CONVERGE_LIFE_MIN 0.85f
#define CONVERGE_LIFE_MAX 1.35f

static float Mix(float a, float b, float t) { return a + (b - a) * t; }

// The authored ramps, as functions of t01.
static float RateMul(float t01)   { return Mix(0.6f, 1.8f, t01); }
static float Pull(float t01)      { return Mix(5.0f, 11.0f, t01); }
static float Boost(float t01)     { return Mix(2.0f, 4.5f, t01); }
static float Shell(float t01)     { return Mix(1.0f, 0.78f, t01); }

// ── 1. The tell CLOSES IN and HARDENS as it fills ───────────────────────────

static void Test_ProgressDrivesEverythingTheSameWay(void)
{
    // Every one of these is monotone in t01, and they all point the same way:
    // denser, harder, brighter, tighter. A single ramp going the other way is
    // what makes a wind-up read as losing energy rather than gaining it.
    int monotone = 1;
    float pr = -1e9f, pp = -1e9f, pb = -1e9f, ps = 1e9f;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
        if (RateMul(t) < pr || Pull(t) < pp || Boost(t) < pb || Shell(t) > ps) monotone = 0;
        pr = RateMul(t); pp = Pull(t); pb = Boost(t); ps = Shell(t);
    }
    CHECK(monotone,
          "density, pull and brightness all rise while the emitter shell tightens");

    CHECK_MSG(RateMul(1.0f) / RateMul(0.0f) >= 2.5f,
              "a full charge emits about three times the threads of an empty one",
              "x%.2f", RateMul(1.0f) / RateMul(0.0f));
    CHECK_MSG(Pull(1.0f) / Pull(0.0f) > 2.0f,
              "and pulls more than twice as hard",
              "x%.2f", Pull(1.0f) / Pull(0.0f));
    // The shell tightens VISIBLY but does not collapse: at 0.78 the launch
    // surface has moved in by a fifth, which reads without the threads losing
    // the flight they need to accelerate over.
    CHECK_MSG(Shell(1.0f) > 0.7f && Shell(1.0f) < 0.85f,
              "the shell closes in by about a fifth — visible, not a collapse",
              "%.2f", Shell(1.0f));
}

// ── 2. The attractor scales with radius, so flight time is size-invariant ───

static void Test_FlightTimeIsScaleInvariant(void)
{
    // THE REASON THE PULL IS MULTIPLIED BY radius AT ALL. Under a constant
    // acceleration a, crossing a distance d takes t = sqrt(2d/a). If a were
    // fixed, a big converge (d large) would feel sluggish and a small one would
    // snap. With a proportional to d, t = sqrt(2/k) — the same at every size.
    float worst = 0.0f;
    for (float radius = 0.2f; radius <= 5.0f; radius += 0.05f) {
        float a = radius * Pull(0.5f);
        float t = sqrtf(2.0f * radius / a);
        float ref = sqrtf(2.0f / Pull(0.5f));
        float e = fabsf(t - ref) / ref;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-5f,
              "flight time is the same at every scale — that is why the attractor "
              "is proportional to radius",
              "worst relative drift %.7f", worst);

    // And that flight time has to fit inside a thread's life, or threads die in
    // mid-air and the converge never converges.
    float flight = sqrtf(2.0f / Pull(0.0f));   // the WEAKEST pull, i.e. slowest
    CHECK_MSG(flight < CONVERGE_LIFE_MIN,
              "even the weakest pull lands a thread inside its shortest life",
              "%.2f s flight vs %.2f s life", flight, CONVERGE_LIFE_MIN);
}

// ── 3. It emits by RATE ─────────────────────────────────────────────────────

static void Test_RateNotCount(void)
{
    // A count per call makes density a function of the frame rate. This rule has
    // bitten this project in three separate effects.
    float worst = 0.0f;
    for (float fps = 20.0f; fps <= 240.0f; fps += 1.0f) {
        float perSec = 45.0f * RateMul(1.0f);
        float emitted = (1.0f / fps) * perSec * fps;   // one second of frames
        float e = fabsf(emitted - perSec) / perSec;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "the same threads/sec at any frame rate",
              "worst relative error %.6f", worst);

    // The hitch clamp must not starve a busy converge at 20 fps, and must not let
    // one long frame dump a burst.
    float wantedAt20fps = 45.0f * RateMul(1.0f) / 20.0f;
    CHECK_MSG(CONVERGE_SPAWN_CLAMP >= wantedAt20fps,
              "the clamp does not starve a 45-thread converge at 20 fps",
              "%d allowed, %.1f wanted", CONVERGE_SPAWN_CLAMP, wantedAt20fps);
    CHECK_MSG(CONVERGE_SPAWN_CLAMP <= 32,
              "and a hitch cannot dump a burst", "%d per frame", CONVERGE_SPAWN_CLAMP);
}

// ── 4. The thread is a thread, not a comet ──────────────────────────────────

static void Test_ThreadGeometry(void)
{
    // 8 points x 0.055 s is how much TRAVEL the ribbon remembers. Too short and
    // the thread is a dash; longer than the mote's own life and the tail is
    // history that never existed.
    float memory = (float)CONVERGE_TRAIL_POINTS * CONVERGE_TRAIL_STEP;
    CHECK_MSG(memory > 0.25f, "the thread remembers enough travel to read as a curve",
              "%.3f s", memory);
    CHECK_MSG(memory < CONVERGE_LIFE_MIN,
              "...and never more than the mote's own shortest life",
              "%.3f s memory vs %.2f s life", memory, CONVERGE_LIFE_MIN);

    // Life VARIES, which is what stops the whole population pulsing in lockstep.
    CHECK_MSG(CONVERGE_LIFE_MAX / CONVERGE_LIFE_MIN > 1.4f,
              "lifetimes vary enough that threads do not retire in waves",
              "x%.2f spread", CONVERGE_LIFE_MAX / CONVERGE_LIFE_MIN);
}

// ── the extraction guard ────────────────────────────────────────────────────

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

// Whitespace collapsed on BOTH sides: a reflow of the source must not break a
// needle (core/docs/LANDMINES.md, 29/07 — seventeen failed at once over column
// alignment).
static int FileHas(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char buf[300000], flat[300000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    CollapseWS(buf, flat, sizeof(flat));
    char want[1024];
    CollapseWS(needle, want, sizeof(want));
    return strstr(flat, want) != NULL;
}

static void Test_ExtractionChangedTheAddressAndNothingElse(void)
{
    const char *mot = "core/composition/common/vc_converge_motes.inl";
    const char *chg = "core/composition/common/vc_charge_converge.inl";

    // THE POINT OF THE WHOLE EXERCISE: every authored number arrived intact.
    CHECK(FileHas(mot, ".radius = (0.010f + 0.008f * Random01()) * radius * s_chargeSize,"),
          "thread thickness is the number it was extracted with");
    CHECK(FileHas(mot, ".lifetime = Math_Mix(0.85f, 1.35f, Random01()),"),
          "so is the lifetime spread");
    CHECK(FileHas(mot, ".render.emissiveBoost = Math_Mix(2.0f, 4.5f, t01),"),
          "and the brightness ramp");
    CHECK(FileHas(mot, "s_accum += GetFrameTime() * ((float)moteCount * Math_Mix(0.6f, 1.8f, t01) * s_chargeRate);"),
          "it still emits by RATE with a carried accumulator");
    CHECK(FileHas(mot, "float shell = radius * Math_Mix(1.0f, 0.78f, t01);"),
          "the emitter shell still tightens as the charge fills");
    CHECK(FileHas(mot, ".strength = radius * Math_Mix(5.0f, 11.0f, t01) * s_chargePull,"),
          "the attractor is still proportional to radius and still hardens");
    CHECK(FileHas(mot, ".strength = radius * 2.6f * s_chargeSwirl,"),
          "the vortex kept its strength");
    CHECK(FileHas(mot, ".strength = 2.2f,"),
          "and the drag kept its — drag is what makes the motion soft, not ballistic");
    CHECK(FileHas(mot, "push.y += radius * 0.12f;"),
          "the lift that makes qi peel OFF the surface survived");
    CHECK(FileHas(mot, "unsigned char alpha = (unsigned char)(90.0f + 165.0f * Random01());"),
          "so did the per-thread opacity spread");

    // The two structural decisions, which are what make it qi rather than a
    // particle system doing a spiral.
    CHECK(FileHas(mot, "SpawnParticleOnMesh(sphere, xform, (ParticleConfig){"),
          "shape still comes from a real mesh, not from a formula");
    CHECK(FileHas(mot, "Mesh m = GenMeshSphere(1.0f, 10, 16);"),
          "and it is the same sphere at the same density");
    CHECK(FileHas(mot, ".type = FORCE_GRAVITY_POINT,"),
          "motion still comes from forces, not from a parametrised path");
    CHECK(!FileHas(mot, "VC_MotionSpiralIn("),
          "and specifically NOT from the analytic spiral that read as machinery");

    // The blend law, which a primary must obey on its own terms.
    CHECK(FileHas(mot, ".render.blendMode = VFX_BLEND_ADDITIVE,"),
          "qi EMITS: additive");
    CHECK(FileHas(mot, ".render.unlit = 1,"),
          "...and unlit, so nothing multiplies it back down");
    CHECK(FileHas(mot, ".render.trailOnly = 1,"),
          "trail-only: a thread of gas, not a comet with a head");

    // Whitened at the SOURCE. A saturated hue stacks additively into more of
    // itself and never reaches white, so emissiveBoost's multiply has nothing to
    // lift — this is a one-line thing to lose in a move.
    CHECK(FileHas(mot, ".colorStart = VC_WithAlpha(VC_Whiten(m->glow, s_chargeWhite), alpha),"),
          "the thread is still whitened at the source, not left saturated");

    // Per-converge force fields. ONE shared field would yank every thread of
    // every converge toward whichever centre was written last — a bug that only
    // appears when two converges exist at once, i.e. never in a bench.
    CHECK(FileHas(mot, "#define CONVERGE_MAX_FIELDS 4"),
          "the per-centre field pool survived the move");
    CHECK(FileHas(mot, "if (s_chargeFieldUsed[i] && Vector3DistanceSqr(s_chargeFieldPos[i], center) < 0.25f) { slot = i; break; }"),
          "including the position match that keeps a centre on its own field");

    // THE DIALS KEPT THEIR KEYS. Renaming a tunable is a silent behaviour change
    // for anyone whose tuning.cfg had set it.
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_rate\", &s_chargeRate, 1.0f);"),
          "charge_rate still means what it meant");
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_white\", &s_chargeWhite, 0.88f);"),
          "and so does charge_white, at the same default");
    CHECK(FileHas(chg, "Tuning_RegisterFloat(\"charge_core\", &s_chargeCore, 1.0f);"),
          "charge_core stayed with the SCORE, which is whose decision it is");

    // ...and the composite actually got smaller. An extraction that leaves the
    // original intact is a copy, which is worse than not extracting.
    CHECK(FileHas(chg, "VFX_ComposeConvergeMotes(center, mat, radius, t01, moteCount);"),
          "the composite now CALLS the primary");
    CHECK(FileHas(chg, "VFX_ComposeCoreGlow(center, mat, radius * VC_ConvergeMotesSizeMul(), t01);"),
          "...and the other primary, still at the same scale");
    CHECK(!FileHas(chg, "SpawnParticleOnMesh("),
          "and no longer spawns anything itself");
    CHECK(!FileHas(chg, "ForceField_AddLayer("),
          "nor builds a force field");
    CHECK(!FileHas(chg, "GenMeshSphere("),
          "nor owns an emitter mesh");

    // A PURE SCORE is the finished state, and this is the mechanical statement of
    // it: the composite's body contains exactly two VFX_Compose calls and no
    // other engine call that draws or spawns.
    CHECK(!FileHas(chg, "SpawnParticle("), "the composite spawns no particles at all");
    CHECK(!FileHas(chg, "VFXLight_Spawn("),
          "and fires no second point light — the glow already brings one, and a "
          "second would double it");
}

int main(void)
{
    printf("=== P2 converge motes (primary, extracted) ===\n");
    Test_ProgressDrivesEverythingTheSameWay();
    Test_FlightTimeIsScaleInvariant();
    Test_RateNotCount();
    Test_ThreadGeometry();
    Test_ExtractionChangedTheAddressAndNothingElse();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

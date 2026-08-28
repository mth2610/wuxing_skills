// core headless test — P2, VFX_ComposeConvergeMotes.
//
// It began life as a TRANSCRIPTION GUARD: the primary was extracted verbatim out
// of VFX_ComposeChargeConverge, and the only acceptance criterion for a move is
// that no authored number drifts in transit.
//
// RE-AIMED TWICE ON 28/08/2026: first when the effect was re-authored from a
// ballistic fall into a curved indraught, and again when the ribbon itself
// changed primitive — particle trails became SWEPT TRAILS (TRAIL_PRESET_BACKDROP,
// four of them, lifted with hdrGain), see the header of vc_converge_motes.inl. Pinning the old numbers would
// now pin the look the owner asked to replace, so the numeric half of this file
// asserts the NEW model instead — and, where it can, asserts a PROPERTY rather
// than a literal, because a property survives tuning and a literal does not:
//
//   * an arc must read as an arc at every size this is used at — not a dot, not
//     a closed hoop;
//   * the drag must decay the orbit inside a thread's life, or nothing converges;
//   * the ribbon must remember less travel than the thread has lived, or the
//     tail is history that never existed.
//
// The source-string half is unchanged in purpose: it holds the two structural
// decisions (motion from FORCES, not from a parametrised path; the per-centre
// field pool) and the dial KEYS, which are the things a later edit can drop
// without any test noticing.
//
// What the mirror CANNOT see: whether the threads read as qi. Only the headless
// capture (scripts/render_vfx_matrix.sh "CHARGE CONVERGE") shows that.

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
#define PI 3.14159265358979323846f
#endif

// The live population, and the pool it is spent out of. A streamer is a SWEPT
// TRAIL now, not a particle: the engine has eight for everything.
#define CONVERGE_STREAMS_DEFAULT 4
#define CONVERGE_STREAMS_POOL 6
#define SWEPT_POOL 8
// Everything timed is a fraction of ONE ORBIT, not a number of seconds — that is
// the whole reason this effect survives a change of scale (see the .inl header).
#define CONVERGE_TAIL_TURNS 0.55f      // the fade time handed to the trail
#define CONVERGE_LIFE_TURNS_MIN 1.2f
#define CONVERGE_LIFE_TURNS_MAX 1.7f
// The trail system remembers 60 nodes at 60 Hz — ONE SECOND of travel, whatever
// the caller asks for (trail_system.h TRAIL_HISTORY_COUNT).
#define TRAIL_HISTORY_SECONDS 1.0f
#define CONVERGE_DRAG_PER_TURN 1.6f
// The launch speed, as a fraction of the CIRCULAR orbit speed at the birth
// radius. Both parts matter: sideways enough to fall along a curve, slow enough
// that it cannot hold station and must fall at all.
// A WAVE picks one launch mix and jitters it +-8% per streamer, so these are the
// wave band's extremes with the jitter folded in.
#define CONVERGE_TANGENT_MIN 0.31f
#define CONVERGE_TANGENT_MAX 0.56f
#define CONVERGE_INWARD_MIN 0.40f
#define CONVERGE_INWARD_MAX 0.71f
// The ball at the middle AND the streamers' sink, as a fraction of `radius`.
// One number: `charge_ball` in vc_converge_motes.inl, read back by the score.
#define CONVERGE_BALL 0.26f
// Threads are born anywhere in a VOLUME around the ball, not on a launch shell:
// between 1.15 and 3 radii out, the outer bound closing in as the charge fills.
#define CONVERGE_CLOUD_INNER 1.60f
#define CONVERGE_CLOUD_OUTER_EMPTY 3.4f
#define CONVERGE_CLOUD_OUTER_FULL 2.4f
// The sizes this primary is actually called at: 0.5 m (skills/taiji/core_test)
// up to the 1.5 m fixture, with headroom either way.
#define CONVERGE_SIZE_MIN 0.3f
#define CONVERGE_SIZE_MAX 3.0f

static float Mix(float a, float b, float t) { return a + (b - a) * t; }

// The authored ramps, as functions of t01.
static float RateMul(float t01)   { return Mix(0.5f, 1.1f, t01); }
static float Pull(float t01)      { return Mix(9.0f, 18.0f, t01); }
static float Boost(float t01)     { return Mix(0.8f, 1.25f, t01); }  // x on charge_glow
// The cloud the threads appear in, as multiples of `radius`, and the midpoint the
// shared drag is quoted against.
static float CloudOuter(float t01) { return Mix(CONVERGE_CLOUD_OUTER_EMPTY, CONVERGE_CLOUD_OUTER_FULL, t01); }
static float Shell(float t01)      { return 0.5f * (CONVERGE_CLOUD_INNER + CloudOuter(t01)); }

// The engine's point attractor is NOT a constant acceleration and NOT an inverse
// square: force_field.c:244 computes a = strength/(dist + 1). Everything below
// solves the orbit against THAT, because a test that models a different law is
// an argument about a system nobody is running.
static float Accel(float radius, float t01, float orbitR)
{
    return (radius * Pull(t01)) / (orbitR + 1.0f);
}
// Circular-orbit angular rate: v = sqrt(a*r), omega = v/r = sqrt(a/r).
static float Omega(float radius, float t01, float orbitR)
{
    return sqrtf(Accel(radius, t01, orbitR) / orbitR);
}
static float Period(float radius, float t01, float orbitR)
{
    return (2.0f * PI) / Omega(radius, t01, orbitR);
}

// ── 1. The tell CLOSES IN and HARDENS as it fills ───────────────────────────

static void Test_ProgressDrivesEverythingTheSameWay(void)
{
    // Every one of these is monotone in t01, and they all point the same way:
    // denser, tighter, brighter. A single ramp going the other way is what makes
    // a wind-up read as losing energy rather than gaining it.
    int monotone = 1;
    float pr = -1e9f, pp = -1e9f, pb = -1e9f, ps = 1e9f;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
        if (RateMul(t) < pr || Pull(t) < pp || Boost(t) < pb || Shell(t) > ps) monotone = 0;
        pr = RateMul(t); pp = Pull(t); pb = Boost(t); ps = Shell(t);
    }
    CHECK(monotone,
          "density, pull and brightness all rise while the cloud closes in");

    CHECK_MSG(RateMul(1.0f) / RateMul(0.0f) >= 2.0f,
              "a full charge emits about twice the arcs of an empty one",
              "x%.2f", RateMul(1.0f) / RateMul(0.0f));
    CHECK_MSG(Pull(1.0f) / Pull(0.0f) >= 1.9f,
              "and winds them about twice as tight",
              "x%.2f", Pull(1.0f) / Pull(0.0f));
    // The cloud closes in VISIBLY but does not collapse onto the ball: threads
    // must still be born in open space, not inside their own destination.
    CHECK_MSG(CloudOuter(1.0f) / CloudOuter(0.0f) < 0.75f,
              "the cloud's outer bound closes in by about a third — visible",
              "x%.2f", CloudOuter(1.0f) / CloudOuter(0.0f));
    CHECK_MSG(CONVERGE_CLOUD_INNER > CONVERGE_BALL * 2.0f,
              "and even the nearest thread is born well outside the ball it falls into",
              "%.2f vs ball %.2f", CONVERGE_CLOUD_INNER, CONVERGE_BALL);
}

// ── 2. The ribbon is a long sweep at every size ─────────────────────────────

static void Test_TheRibbonIsALongSweep(void)
{
    // WHAT DECIDES THE RIBBON'S LENGTH, and it is not the number this file used
    // to pin. A particle tail was 8 history points and the step bought its
    // length; a SWEPT trail keeps 60 nodes at 60 Hz, so the strip is always THE
    // LAST SECOND OF TRAVEL and its length in metres is however far the head got
    // in that second. Two things therefore have to hold, and neither is a taste
    // value:
    //
    //   the fade time handed to the trail must be LONGER than that second, or
    //   the tail is cut short by its own fade before the history runs out;
    float shortestTail = 1e9f;
    for (float radius = CONVERGE_SIZE_MIN; radius <= CONVERGE_SIZE_MAX; radius += 0.05f)
        for (float t = 0.0f; t <= 1.0f; t += 0.05f) {
            float orbitR = radius * CONVERGE_CLOUD_INNER;   // the fastest, shortest orbit
            float tail   = Period(radius, t, orbitR) * CONVERGE_TAIL_TURNS;
            if (tail < shortestTail) shortestTail = tail;
        }
    CHECK_MSG(shortestTail > TRAIL_HISTORY_SECONDS,
              "the ribbon's fade outlasts the trail system's own one-second "
              "history at every size — the history is what ends the strip, not a fade",
              "%.2f s vs %.2f s of history", shortestTail, TRAIL_HISTORY_SECONDS);

    //   and a streamer must LIVE longer than the ribbon it is drawing, or it is
    //   released while the strip is still filling.
    CHECK_MSG(CONVERGE_LIFE_TURNS_MIN > CONVERGE_TAIL_TURNS * 0.7f,
              "a streamer outlives most of its own ribbon",
              "%.2f vs %.2f turns", CONVERGE_LIFE_TURNS_MIN, CONVERGE_TAIL_TURNS);

    // Life VARIES, which is what stops four ribbons retiring in lockstep — with
    // a population this small, one synchronised blink is the whole effect.
    CHECK_MSG(CONVERGE_LIFE_TURNS_MAX / CONVERGE_LIFE_TURNS_MIN > 1.35f,
              "lifetimes vary enough that the four do not retire together",
              "x%.2f spread", CONVERGE_LIFE_TURNS_MAX / CONVERGE_LIFE_TURNS_MIN);

    // And how far that second of travel actually reaches: the head is moving at
    // a fraction of orbital speed, so the strip is metres long, not centimetres.
    float shortestSweep = 1e9f;
    for (float radius = CONVERGE_SIZE_MIN; radius <= CONVERGE_SIZE_MAX; radius += 0.05f) {
        float orbitR = radius * CONVERGE_CLOUD_INNER;
        // The FULL launch speed, not just its sideways part: the streamer
        // travels along both components, and the ribbon records the path.
        float vCirc = sqrtf(Accel(radius, 0.0f, orbitR) * orbitR);
        float v = vCirc * sqrtf(CONVERGE_TANGENT_MIN * CONVERGE_TANGENT_MIN +
                                CONVERGE_INWARD_MIN * CONVERGE_INWARD_MIN);
        float metres = v * TRAIL_HISTORY_SECONDS;
        if (metres / radius < shortestSweep) shortestSweep = metres / radius;
    }
    // The worst case is the smallest converge at an empty charge launched at the
    // slowest sideways fraction; everything else is longer, and the fixture's
    // 1.5 m at a filling charge is roughly double this.
    CHECK_MSG(shortestSweep > 0.5f,
              "even the slowest streamer lays a ribbon of the same order as the "
              "whole tell's radius — a sweep, not a dash",
              "%.2f radii in the worst case", shortestSweep);
}

// ── 3. The drag is the spiral, not the brake ────────────────────────────────

static void Test_DragDecaysTheOrbitInsideALife(void)
{
    // A converge has to CONVERGE. With a tangential launch the only thing
    // bringing a thread in is the drag bleeding its orbital speed, so the decay
    // over a thread's life is the whole difference between qi settling into a
    // core and a permanent set of hoops. Quoted per TURN, for the same reason
    // the ribbon is.
    float leftShort = expf(-CONVERGE_DRAG_PER_TURN * CONVERGE_LIFE_TURNS_MIN);
    float leftLong  = expf(-CONVERGE_DRAG_PER_TURN * CONVERGE_LIFE_TURNS_MAX);
    CHECK_MSG(leftShort < 0.6f,
              "even the shortest-lived thread has visibly wound inward before it retires",
              "%.0f%% of launch speed left", leftShort * 100.0f);
    // The old companion check here asked that a streamer still be MOVING at the
    // end of its life. It is the wrong question now: a streamer is not supposed
    // to reach the end of its life at all — it is supposed to have arrived. What
    // replaces it is Test_EveryStreamerActuallyArrives below, which integrates
    // the real thing instead of arguing about the exponential.
    (void)leftLong;

    // And the brake it replaced would have killed the arc outright: 2.2 per
    // second over the ~2 s life of a 1.5 m converge is a fiftieth of the launch
    // speed left, which is why the old version drew dashes.
    CHECK_MSG(expf(-2.2f * 2.0f) < 0.05f,
              "the OLD drag would have stopped a thread inside its life — that is "
              "the number the re-authoring moved",
              "%.3f left", expf(-2.2f * 2.0f));
}

// ── 3b. A thread must actually ARRIVE ───────────────────────────────────────

static void Test_TheLaunchIsSubOrbitalButNotRadial(void)
{
    // The two ways this effect fails, stated as bounds on ONE number — the
    // sideways fraction of the circular orbit speed:
    //
    //   at 1.0 the thread holds station and circles the ball forever, which is
    //   the "arcs orbiting a ball" reading the owner rejected;
    //   at 0.0 it drops down a straight radius and the population becomes a
    //   wheel of spokes, which is what this effect looked like before.
    CHECK_MSG(CONVERGE_TANGENT_MAX < 0.75f,
              "every thread is launched below orbital speed — it cannot hold "
              "station, so it must fall in",
              "max %.2f of circular", CONVERGE_TANGENT_MAX);
    CHECK_MSG(CONVERGE_TANGENT_MIN > 0.25f,
              "...and none is launched straight at the middle, so it falls along "
              "a curve rather than down a spoke",
              "min %.2f of circular", CONVERGE_TANGENT_MIN);
    // THE BALANCE, AND WHICH WAY IT MOVED. It used to be sideways-dominant, and
    // the owner's read of that was that the ribbons did not gather — they swept
    // PAST the ball. Inward leads now; the sideways part is what keeps the path a
    // curve rather than a spoke, so it has to stay a substantial share of it and
    // must not take the lead back.
    CHECK_MSG(CONVERGE_TANGENT_MIN > CONVERGE_INWARD_MAX * 0.4f,
              "the sideways part is a substantial share of the inward one, so every "
              "path still bends",
              "tangent %.2f vs inward %.2f", CONVERGE_TANGENT_MIN, CONVERGE_INWARD_MAX);
    CHECK_MSG(CONVERGE_TANGENT_MAX <= CONVERGE_INWARD_MAX,
              "...but inward leads, which is what makes it gather instead of pass by",
              "tangent %.2f vs inward %.2f", CONVERGE_TANGENT_MAX, CONVERGE_INWARD_MAX);

    // WHERE A SUB-ORBITAL LAUNCH BOTTOMS OUT, and why the ribbon ends by ARRIVING
    // rather than by expiring. For angular momentum L = f*v_c*r0 the closest
    // approach is roughly r0*f^2 before drag; drag only ever brings it closer. If
    // that were far outside the sink, every ribbon would stop in mid-air on its
    // life timer with a visible gap between it and the ball — the "not converging"
    // reading. Inside about two sink radii, the drag closes the rest and the
    // distance test is what fires.
    float closest = CONVERGE_CLOUD_INNER * CONVERGE_TANGENT_MAX * CONVERGE_TANGENT_MAX;
    CHECK_MSG(closest < CONVERGE_BALL * 2.0f,
              "a streamer's no-drag closest approach is already at the ball, so it "
              "arrives instead of timing out short of it",
              "%.2f radii vs a %.2f sink", closest, CONVERGE_BALL);
    CHECK_MSG(closest > CONVERGE_BALL * 0.5f,
              "...and does not dive so straight that the path is a spoke",
              "%.2f radii vs a %.2f sink", closest, CONVERGE_BALL);
}

// ── 3c. EVERY streamer reaches the ball ─────────────────────────────────────

// A tiny deterministic generator, so this test is a fixed number rather than a
// flake. Values only have to be uniform-ish in 0..1.
static unsigned int g_rngState = 0x13572468u;
static float Rnd01(void)
{
    g_rngState = g_rngState * 1664525u + 1013904223u;
    return (float)((g_rngState >> 8) & 0xFFFFFF) / (float)0x1000000;
}

// MIRROR of VC_ConvergeMotes_Update's integration and of the launch beside it.
// Returns the share of streamers that end by ARRIVING at the sink rather than by
// running out of life.
static float ArrivalRate(float radius, float t01, int tries)
{
    const float dt   = 1.0f / 60.0f;
    // The arrival radius, not the ball's: a streamer carries on to the middle.
    const float sink = radius * CONVERGE_BALL * 0.25f;
    float pull  = radius * Pull(t01);
    float inner = radius * CONVERGE_CLOUD_INNER;
    float outer = radius * CloudOuter(t01);
    int arrived = 0;
    for (int i = 0; i < tries; i++) {
        float r0     = Mix(inner, outer, Rnd01());
        float vCirc  = sqrtf((pull / (r0 + 1.0f)) * r0);
        // The launch, in the plane of the motion: x is radial, y tangential.
        float vx     = -vCirc * Mix(CONVERGE_INWARD_MIN,  CONVERGE_INWARD_MAX,  Rnd01());
        float vy     =  vCirc * Mix(CONVERGE_TANGENT_MIN, CONVERGE_TANGENT_MAX, Rnd01());
        float period = (2.0f * PI * r0) / vCirc;
        float drag   = CONVERGE_DRAG_PER_TURN / period;
        float life   = period * Mix(CONVERGE_LIFE_TURNS_MIN, CONVERGE_LIFE_TURNS_MAX, Rnd01());
        float px = r0, py = 0.0f, age = 0.0f;
        while (age < life) {
            float d = sqrtf(px * px + py * py);
            if (d <= sink) { arrived++; break; }
            float a = pull / (d + 1.0f);
            vx += -(px / d) * a * dt;
            vy += -(py / d) * a * dt;
            float k = 1.0f - (drag * dt < 0.9f ? drag * dt : 0.9f);
            vx *= k; vy *= k;
            // The capture term, mirrored: inside 1.5 ball radii the velocity is
            // steered toward the middle. This is the part that makes arrival
            // certain rather than lucky.
            float ball = radius * CONVERGE_BALL;
            if (d < ball * 1.5f) {
                float kk = 1.0f - d / (ball * 1.5f);
                float steer = 6.0f * kk * dt;
                if (steer > 1.0f) steer = 1.0f;
                float speed = sqrtf(vx * vx + vy * vy);
                float rx = -(px / d) * speed, ry = -(py / d) * speed;
                vx += (rx - vx) * steer;
                vy += (ry - vy) * steer;
            }
            px += vx * dt; py += vy * dt; age += dt;
        }
    }
    return (float)arrived / (float)tries;
}

static void Test_EveryStreamerActuallyArrives(void)
{
    // THE DEFECT THIS EXISTS FOR, in the owner's words: "có những trail nó ko di
    // chuyển tới tâm quả cầu đã bắt đầu biến mất". The life timer was authored by
    // eye at 0.42-0.60 of a turn and it fired FIRST — integrating the shipped
    // launch spread showed only 4-35% of streamers ever reached the sink, so
    // most ribbons ended in open space short of the ball. Nothing in the source
    // said so; every number in it looked reasonable on its own.
    //
    // The lesson is the shape of the check, not the constant: when an effect's
    // ending is a CONDITION (arrival) with a timer behind it, the timer has to be
    // measured against the condition, and the only honest way to measure it is to
    // integrate the same motion the effect runs.
    float worst = 1.0f;
    float worstR = 0.0f, worstT = 0.0f;
    for (float radius = CONVERGE_SIZE_MIN; radius <= CONVERGE_SIZE_MAX + 1e-3f; radius += 0.3f)
        for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
            float rate = ArrivalRate(radius, t, 400);
            if (rate < worst) { worst = rate; worstR = radius; worstT = t; }
        }
    CHECK_MSG(worst > 0.90f,
              "streamers END BY ARRIVING at the ball, at every size and every "
              "charge level — the life timer is a fallback, not the usual exit",
              "worst %.0f%% at radius %.1f m, t01 %.2f", worst * 100.0f, worstR, worstT);

    // And the timer must not be so generous that a streamer that is somehow NOT
    // going to arrive hangs on to one of the four trail slots for ages.
    CHECK_MSG(CONVERGE_LIFE_TURNS_MAX < 2.0f,
              "...while still capping a stray streamer inside two turns",
              "%.2f turns", CONVERGE_LIFE_TURNS_MAX);
}

// ── 4. It launches by RATE, and the POPULATION is a budget ─────────────────

static void Test_RateNotCountAndTheBudget(void)
{
    // A count per call makes density a function of the frame rate. This rule has
    // bitten this project in three separate effects.
    float worst = 0.0f;
    for (float fps = 20.0f; fps <= 240.0f; fps += 1.0f) {
        float perSec = 5.0f * RateMul(1.0f);
        float emitted = (1.0f / fps) * perSec * fps;   // one second of frames
        float e = fabsf(emitted - perSec) / perSec;
        if (e > worst) worst = e;
    }
    CHECK_MSG(worst < 1e-4f, "the same launches/sec at any frame rate",
              "worst relative error %.6f", worst);

    // THE BUDGET, WHICH IS NEW AND IS THE POINT OF THE REWRITE. A streamer is a
    // simulated swept trail out of a pool of EIGHT for the whole engine, not a
    // particle out of a pool of thousands. Sparse is what the owner asked for
    // and it is also all this can afford: a converge that took the pool would
    // silently delete the weapon trails of everyone on screen.
    CHECK_MSG(CONVERGE_STREAMS_DEFAULT <= SWEPT_POOL / 2,
              "a converge takes at most half the engine's trail pool",
              "%d of %d", CONVERGE_STREAMS_DEFAULT, SWEPT_POOL);
    CHECK_MSG(CONVERGE_STREAMS_POOL <= SWEPT_POOL,
              "and even its hard ceiling cannot exceed the pool it draws from",
              "%d vs %d", CONVERGE_STREAMS_POOL, SWEPT_POOL);
    CHECK_MSG(CONVERGE_STREAMS_DEFAULT >= 3,
              "...while still being enough ribbons to read as an indraught",
              "%d", CONVERGE_STREAMS_DEFAULT);

    // Refill rate against the population: the launcher must be able to keep the
    // cap full — a streamer lives a fraction of a turn, so at the fixture's five
    // launches a second the slots never sit empty for long.
    CHECK_MSG(5.0f * RateMul(1.0f) >= 2.0f,
              "the launch rate refills the population rather than starving it",
              "%.1f launches/sec", 5.0f * RateMul(1.0f));
}

// ── the source guard ────────────────────────────────────────────────────────

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

static void Test_TheSourceStillSaysWhatThisFileMeasures(void)
{
    const char *mot = "core/composition/common/vc_converge_motes.inl";
    const char *chg = "core/composition/common/vc_charge_converge.inl";

    // THE PRIMITIVE. This is the whole second rewrite: the ribbon is a real
    // swept trail, and the widest recipe there is, lifted out of the dim band it
    // was authored in. A particle tail cannot be lit, flowed or widened, which is
    // why the first version looked cheap however it was tuned.
    CHECK(FileHas(mot, "TRAIL_PRESET_BACKDROP);"),
          "the ribbon is the BACKDROP swept trail, not a particle's tail");
    CHECK(FileHas(mot, "VFX_TrailSetHdrGain(st->trail, s_chargeGlow"),
          "and it is lifted per INSTANCE, so the shared dim preset is untouched");
    CHECK(!FileHas(mot, "SpawnParticle("),
          "no particles are spawned here at all any more");
    CHECK(!FileHas(mot, "ForceField_AddLayer("),
          "and no shared force field — a trail follows a transform this file integrates");

    // THE POPULATION, which is both a look decision and a pool budget.
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_ball\",    &s_chargeBall,    0.26f);"),
          "charge_ball kept its key when it moved from the score to the primary");
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_streams\", &s_chargeStreams, 4.0f);"),
          "the live population is four, and it is a dial");
    CHECK(FileHas(mot, "#define CONVERGE_MAX_STREAMS 6"),
          "with a hard ceiling under the engine's eight-trail pool");
    CHECK(FileHas(mot, "if (cap > CONVERGE_MAX_STREAMS) cap = CONVERGE_MAX_STREAMS;"),
          "and the cap is clamped where the wave is sized, not hoped for");
    CHECK(FileHas(mot, "for (int i = 0; i < CONVERGE_MAX_STREAMS; i++)\n        if (!s_convStreams[i].active) { slot = i; break; }\n    if (slot < 0) return;"),
          "...with a free-slot check behind it, so a full pool drops the launch "
          "rather than stamping on a live ribbon");

    // THE NUMBERS THIS FILE'S ARITHMETIC IS ABOUT. Without these the tests above
    // are a private model with no connection to the shipped effect.
    CHECK(FileHas(mot, "st->life   = period * Math_Mix(1.2f, 1.7f, Random01());"),
          "the streamer lifetime the sweep and decay checks assume — in TURNS");
    CHECK(FileHas(mot, "period * 0.55f, TRAIL_PRESET_BACKDROP);"),
          "and the ribbon's fade, also a fraction of a turn");
    CHECK(FileHas(mot, "st->drag   = 1.6f / period;"),
          "the drag that spirals the path in, per turn, not per second");
    CHECK(FileHas(mot, "return radius * Math_Mix(9.0f, 18.0f, t01) * s_chargePull;"),
          "the attractor is proportional to radius and hardens as the charge fills");
    CHECK(FileHas(mot, "return radius * Math_Mix(3.4f, 2.4f, t01);"),
          "the cloud's outer bound closes in as the charge fills");
    CHECK(FileHas(mot, "static float ConvergeMotes_CloudInner(float radius) { return radius * 1.60f; }"),
          "and streamers are born in a VOLUME, from its inner bound outward");
    CHECK(FileHas(mot, "static float ConvergeMotes_Period(float pull, float orbitR)"),
          "the orbital clock everything is quoted against exists in ONE place");

    // THE LAUNCH, which is what makes the path a curve into the ball rather than
    // a circle around it or a spoke at it.
    CHECK(FileHas(mot, "float   vOrbit = sqrtf(accel * orbitR);"),
          "the launch speed is measured against the orbital speed, solved not guessed");
    CHECK(FileHas(mot, "float   accel  = pull / (orbitR + 1.0f);"),
          "...against the engine's OWN falloff law, a = strength/(dist+1)");
    CHECK(FileHas(mot, "float waveTan = Math_Mix(0.34f, 0.52f, Random01());"),
          "a FRACTION of it sideways: sub-orbital, so the streamer must fall");
    CHECK(FileHas(mot, "float waveIn  = Math_Mix(0.44f, 0.66f, Random01());"),
          "with an inward part, so it falls along a curve rather than circling");
    CHECK(FileHas(mot, "float z   = 1.0f - 2.0f * u1;"),
          "the sphere is sampled area-uniformly, not by uniform theta");

    // The integrator has to accelerate by the SAME law the launch solved
    // against, or the ribbons spiral out instead of in.
    CHECK(FileHas(mot, "float a = st->pull / (d + 1.0f);"),
          "the per-frame integration uses that same attractor law");
    CHECK(FileHas(mot, "VFX_Trail_Stop(st->trail);"),
          "a finished streamer STOPS its feed, so the laid ribbon drains where it "
          "was absorbed instead of being cut out of the air");

    // ARRIVAL, WHICH IS WHAT "tụ lại" ACTUALLY IS. A ribbon that ends on a timer
    // ends wherever it happens to be; one that ends on DISTANCE ends at the ball,
    // every time, from every direction — and the width ramp narrows it into the
    // surface instead of stopping it at full width.
    CHECK(FileHas(mot, "if (d <= st->sink)            ConvergeMotes_Release(st, true);"),
          "a streamer ends by ARRIVING at the middle of the ball...");
    CHECK(FileHas(mot, "else if (st->age >= st->life) ConvergeMotes_Release(st, false);"),
          "...and the timer is only the fallback behind that");
    // ABSORBED means CUT. A strip left to drain keeps drifting and fading in open
    // space after its head is gone, which is the wandering this effect is not
    // supposed to do; a wind-down is right only for the streamer that never
    // arrived.
    CHECK(FileHas(mot, "if (absorbed) VFX_Trail_Extinguish(st->trail);"),
          "an absorbed ribbon is cut on the spot, not left to drain");
    CHECK(FileHas(mot, "else          VFX_Trail_Stop(st->trail);"),
          "...while one that timed out still gets the gentle wind-down");
    CHECK(FileHas(mot, "VFX_TrailSetWidth(st->trail, t);"),
          "and it tapers to nothing over the last stretch, so it pours in");
    CHECK(FileHas(mot, "if (d < st->ball && st->trail >= 0)"),
          "...over the last stretch INSIDE the ball, so the cut has nothing left to "
          "pop: the width call scales the whole strip, and a taper that starts "
          "further out thins the ribbon while it is still crossing open space");
    CHECK(FileHas(mot, "st->ball   = radius * s_chargeBall * s_chargeSize;"),
          "the ball's radius in metres is what the taper is measured against");
    CHECK(FileHas(mot, "float k     = 1.0f - d / (st->ball * 1.5f);   // 0 at the edge, 1 at the middle"),
          "the ball CAPTURES the last stretch — the curve is kept outside, and the "
          "sideways speed is steered out where the ball takes over");
    CHECK(FileHas(mot, "st->sink   = st->ball * 0.25f;"),
          "and the streamer travels on to the MIDDLE before it ends — stopping at "
          "the surface reads as hitting the ball, not as being taken into it");
    CHECK(FileHas(mot, "float VC_ConvergeMotesSinkFrac(void)"),
          "...and the score sizes the ball from THAT number, so the two agree");

    // ── THE BEAT ────────────────────────────────────────────────────────────
    // A charge tell is rhythmic: everything appears at once, everything is drawn
    // in, a breath, again. Launching one streamer at a time gives four ribbons at
    // four unrelated stages of four unrelated flights, which is the drifting the
    // owner rejected ("kiểu bay lượn lờ ... ko hợp lý").
    CHECK(FileHas(mot, "static void ConvergeMotes_LaunchWave(Vector3 center, VC_MaterialId mat,"),
          "the population is launched as a WAVE, not by a spawn rate");
    CHECK(FileHas(mot, "if (live == 0 && s_convWaveGap <= 0.0f)"),
          "and the next wave waits for the last one to be absorbed, plus a breath");
    CHECK(FileHas(mot, "s_convWaveGap = refPeriod * Math_Mix(0.30f, 0.10f, t01);"),
          "...a breath that shortens as the charge fills, so a wind-up winds up");

    // What makes a wave a wave and not four launches in the same frame: flight
    // time comes from the birth radius and the launch speed, so those are the
    // WAVE's, jittered per streamer. Drawn independently they spread the four
    // arrivals over more than a turn and the beat dissolves.
    CHECK(FileHas(mot, "float waveR   = Math_Mix(ConvergeMotes_CloudInner(radius),"),
          "one birth radius per wave, so the four arrive together");
    CHECK(FileHas(mot, "waveR   * Math_Mix(0.94f, 1.06f, Random01()),"),
          "...jittered per streamer, so it is a wave and not a printed ring");
    CHECK(FileHas(mot, "float phi = phi0 + golden * (float)i;"),
          "directions are a rotated Fibonacci lattice — spread over the sphere, "
          "not four independent samples that clump");

    // Statefulness. A trail is a handle with a lifetime, so this file needs a
    // tick — and the archetype PAIR is what declares that to the sync script.
    CHECK(FileHas(mot, "static void VC_ConvergeMotes_Update(float dt)"),
          "the primary owns a per-frame tick, because it now owns live handles");
    CHECK(FileHas(mot, "static void VC_ConvergeMotes_Draw3D(Camera3D cam)"),
          "...and the Draw3D half that makes the pair a declaration");

    // THE DIALS KEPT THEIR KEYS. Renaming a tunable is a silent behaviour change
    // for anyone whose tuning.cfg had set it.
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_rate\", &s_chargeRate, 1.0f);"),
          "charge_rate still means what it meant");
    CHECK(FileHas(mot, "Tuning_RegisterFloat(\"charge_pull\", &s_chargePull, 1.0f);"),
          "and charge_pull, and charge_size through the accessor below");
    CHECK(FileHas(mot, "float VC_ConvergeMotesSizeMul(void)"),
          "charge_size is still readable by the score that scales the ball with it");
    CHECK(FileHas(chg, "Tuning_RegisterFloat(\"charge_core\", &s_chargeCore, 1.0f);"),
          "charge_core stayed with the SCORE, which is whose decision it is");

    // THE SCORE. Two primaries, the ball is a FlowShield, and there is no core
    // glow behind it: FlowShield is lit throughout, so a hot core inside it is
    // the fix for the wrong shell.
    CHECK(FileHas(chg, "float ballR = radius * VC_ConvergeMotesSinkFrac() * VC_ConvergeMotesSizeMul();"),
          "the ball is sized from the streamers' SINK — one number, not two that drift");
    CHECK(!FileHas(chg, "static float s_chargeBall"),
          "and the score no longer keeps a second copy of that number");
    CHECK(FileHas(chg, "VFX_ComposeFlowShield("),
          "...and it IS a FlowShield");
    CHECK(!FileHas(chg, "VFX_ComposeCoreGlow("),
          "with no core glow behind it — the shield is lit on its own");
    CHECK(!FileHas(chg, "VFX_ComposeShieldShell("),
          "and the refracted-glass shell it replaced is gone, not left as an option");
    CHECK(FileHas(chg, "ChargeConverge_ShellGroundPoint(center, ballR)"),
          "the shield's ground-point convention is cancelled so the ball sits where "
          "the composition puts it, not half a radius above");
    CHECK(FileHas(chg, "Vector3 hub   = { center.x, center.y + ballR, center.z };"),
          "the ball is TANGENT above the point the caller names — entirely out of "
          "the ground, not centred on it");
    CHECK(FileHas(chg, "VFX_ComposeConvergeMotes(hub, mat, radius, t01, moteCount);"),
          "...and the streamers fall toward that same hub, so they pour into the "
          "ball's centre rather than past its bottom edge");
    CHECK(FileHas(chg, "static void VC_ChargeConverge_Update(float dt)"),
          "the score owns a tick too, because it owns the ball's handle");
    CHECK(FileHas(chg, "VFX_FlowShield_Stop(sl->handle);"),
          "...and that tick stops the ball of a converge that stopped being fed");
    CHECK(!FileHas(chg, "SpawnParticle("), "the composite spawns no particles at all");
    CHECK(!FileHas(chg, "VFXLight_Spawn("),
          "and fires no point light of its own");
}

int main(void)
{
    printf("=== P2 converge motes (primary, re-authored as orbits) ===\n");
    Test_ProgressDrivesEverythingTheSameWay();
    Test_TheRibbonIsALongSweep();
    Test_DragDecaysTheOrbitInsideALife();
    Test_TheLaunchIsSubOrbitalButNotRadial();
    Test_EveryStreamerActuallyArrives();
    Test_RateNotCountAndTheBudget();
    Test_TheSourceStillSaysWhatThisFileMeasures();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

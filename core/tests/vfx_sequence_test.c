// core headless test — VFX_Sequence's beat scheduler (Đợt E / E3).
//
// The scheduler is pure logic: given beats at times t and a stream of dt, which
// beats fire, in what order, and how many times. That is arithmetic, not a
// rendering question, so per core/CLAUDE.md's debugging table it must never be
// verified by taking a screenshot.
//
// Two failures matter more than the rest, and both are invisible on screen:
//
//   1. A long frame EATS a beat. If a frame spike (or a hitstop, or a debugger
//      pause) makes dt bigger than the gap between two beats, a scheduler that
//      fires "the next beat" per frame silently drops one. The beat you lose
//      that way is whichever is densest — in an ER envelope that is the burst
//      cluster, i.e. the hitstop and the shake. The effect still plays, just
//      feels wrong, and nothing in the log says why.
//   2. Beats fire OUT OF ORDER when several land in the same frame. Light before
//      shake and shake before light look identical in a still frame and
//      completely different in motion.
//
// This mirrors the scheduler rather than linking it (vfx_sequence.c pulls in
// raylib, the light pool, camera FX, post FX...). A mirror rots into fiction, so
// the last section asserts the load-bearing lines still exist in the real .c —
// the guard core/CLAUDE.md §3 requires.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define CHECK_MSG(cond, name, fmt, ...) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

// ── the mirror ───────────────────────────────────────────────────────────────
#define MAX_BEATS 24

typedef struct { float t; int fired; int id; } Beat;
typedef struct {
    Beat  beats[MAX_BEATS];
    int   count;
    float clock;
    float lastBeatT;
    int   playing;
} Seq;

static int  g_fireLog[64];
static int  g_fireCount;

static void seq_add(Seq *s, float t, int id)
{
    if (s->count >= MAX_BEATS) return;
    if (t < 0.0f) t = 0.0f;
    s->beats[s->count].t = t;
    s->beats[s->count].fired = 0;
    s->beats[s->count].id = id;
    s->count++;
    if (t > s->lastBeatT) s->lastBeatT = t;
}

// Mirror of VFX_SeqPlay's insertion sort.
static void seq_play(Seq *s)
{
    for (int i = 1; i < s->count; i++) {
        Beat key = s->beats[i];
        int j = i - 1;
        while (j >= 0 && s->beats[j].t > key.t) {
            s->beats[j + 1] = s->beats[j];
            j--;
        }
        s->beats[j + 1] = key;
    }
    s->clock = 0.0f;
    s->playing = 1;
}

// Mirror of VFX_Sequence_Update's per-sequence body.
static void seq_update(Seq *s, float dt)
{
    if (!s->playing) return;
    s->clock += dt;
    int allFired = 1;
    for (int b = 0; b < s->count; b++) {
        if (s->beats[b].fired) continue;
        if (s->beats[b].t <= s->clock) {
            s->beats[b].fired = 1;
            if (g_fireCount < 64) g_fireLog[g_fireCount++] = s->beats[b].id;
        } else {
            allFired = 0;
            break;
        }
    }
    if (allFired && s->clock >= s->lastBeatT) s->playing = 0;
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
    printf("=== vfx sequence scheduler test ===\n");

    // 1. Beats authored OUT of order must fire in time order.
    {
        Seq s = {0}; g_fireCount = 0;
        seq_add(&s, 0.30f, 3);
        seq_add(&s, 0.00f, 1);
        seq_add(&s, 0.10f, 2);
        seq_play(&s);
        for (int i = 0; i < 40; i++) seq_update(&s, 1.0f / 60.0f);
        CHECK_MSG(g_fireCount == 3, "out-of-order authoring: all 3 beats fire",
                  "fired %d", g_fireCount);
        CHECK(g_fireCount == 3 && g_fireLog[0] == 1 && g_fireLog[1] == 2 && g_fireLog[2] == 3,
              "out-of-order authoring: they fire in TIME order, not insertion order");
    }

    // 2. THE FRAME-SPIKE CASE. One 0.5 s frame jumps over an entire dense
    //    burst cluster. Every beat must still fire, in order, in that one frame.
    {
        Seq s = {0}; g_fireCount = 0;
        seq_add(&s, 0.00f, 1);
        seq_add(&s, 0.15f, 2);   // burst: these three are 1 frame apart at 60fps
        seq_add(&s, 0.16f, 3);
        seq_add(&s, 0.17f, 4);
        seq_add(&s, 0.40f, 5);
        seq_play(&s);
        seq_update(&s, 0.5f);    // a single catastrophic frame
        CHECK_MSG(g_fireCount == 5, "frame spike: a 0.5s frame drops NO beats",
                  "fired %d of 5", g_fireCount);
        int ordered = 1;
        for (int i = 1; i < g_fireCount; i++)
            if (g_fireLog[i] < g_fireLog[i - 1]) ordered = 0;
        CHECK(ordered, "frame spike: the beats it catches up on stay in order");
    }

    // 3. No beat fires twice, ever — a re-fired hitstop would lock the game.
    {
        Seq s = {0}; g_fireCount = 0;
        seq_add(&s, 0.05f, 1);
        seq_play(&s);
        for (int i = 0; i < 120; i++) seq_update(&s, 1.0f / 60.0f);
        CHECK_MSG(g_fireCount == 1, "a beat fires exactly once across many frames",
                  "fired %d times", g_fireCount);
    }

    // 4. A beat at t=0 fires on the FIRST update, not the second. An envelope's
    //    anticipation beat is at 0; one frame late is a visible hitch at 60fps.
    {
        Seq s = {0}; g_fireCount = 0;
        seq_add(&s, 0.0f, 1);
        seq_play(&s);
        seq_update(&s, 1.0f / 60.0f);
        CHECK(g_fireCount == 1, "a t=0 beat fires on the first update");
    }

    // 5. The sequence retires only after its LAST beat, so a long tail is not
    //    truncated by an early "everything fired" check.
    {
        Seq s = {0}; g_fireCount = 0;
        seq_add(&s, 0.0f, 1);
        seq_add(&s, 1.0f, 2);
        seq_play(&s);
        seq_update(&s, 0.5f);
        CHECK(s.playing == 1, "still playing while a later beat is pending");
        seq_update(&s, 0.6f);
        CHECK(s.playing == 0 && g_fireCount == 2, "retires once the last beat has fired");
    }

    // 6. Overflow is bounded and drops the EXTRA, never corrupts the array.
    {
        Seq s = {0}; g_fireCount = 0;
        for (int i = 0; i < MAX_BEATS + 10; i++) seq_add(&s, (float)i * 0.01f, i);
        CHECK_MSG(s.count == MAX_BEATS, "beat overflow is clamped to the pool size",
                  "count %d", s.count);
    }

    // ── mirror guards: the real .c must still do what is modelled above ──────
    {
        char *c = slurp("core/composition/vfx_sequence.c");
        CHECK(c != NULL, "core/composition/vfx_sequence.c readable");
        if (c) {
            // The no-drop loop: `continue` past fired ones, fire while t <= clock,
            // and `break` at the first future beat (valid only because sorted).
            CHECK(strstr(c, "if (s->beats[b].t <= s->clock)") != NULL,
                  "real scheduler still fires every beat whose t has passed (no-drop)");
            CHECK(strstr(c, "break;   // sorted: everything after this is later still") != NULL,
                  "real scheduler still relies on sorted order to stop early");
            CHECK(strstr(c, "while (j >= 0 && s->beats[j].t > key.t)") != NULL,
                  "real VFX_SeqPlay still sorts beats by time");
            CHECK(strstr(c, "s->beats[b].fired = true;") != NULL,
                  "real scheduler still marks a beat fired (fires once)");
            free(c);
        }
    }
    {
        char *h = slurp("core/composition/vfx_sequence.h");
        CHECK(h != NULL, "vfx_sequence.h readable");
        if (h) {
            // The clock choice is a documented decision, not an accident — if the
            // header stops saying so, the landmine has been silently reintroduced.
            CHECK(strstr(h, "SCALED dt") != NULL,
                  "header still documents which clock sequences run on");
            free(h);
        }
    }

    printf("=== vfx sequence: %d failure(s) ===\n", g_failures);
    return g_failures ? 1 : 0;
}

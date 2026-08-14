/* Guard: VFX simulation must advance on the ENGINE's frame delta, never on
 * raylib's wall-clock GetFrameTime().
 *
 * The bug this locks down (14/08/2026) was not a wrong image — it was a
 * measuring instrument that silently lied. main.c pins dt to 1/60 on the
 * headless capture paths so a run is reproducible, but VFX code re-read
 * GetFrameTime() itself, and headless also skips SetTargetFPS, so that call
 * returned free-running wall-clock time. Consequence: `--render-vfx` produced a
 * different image every run. Four consecutive captures of one fixture at one
 * setting differed MORE than the parameter being swept across its whole range,
 * so a bloom sweep measured nothing at all and read as a plausible result.
 *
 * The nastiest part, and the reason this guard is a source check rather than a
 * numeric one: a capture can look perfectly stable because the effect has
 * ALREADY FINISHED. LIGHTNING IMPACT gave identical bytes at --warmup 30 and
 * divergent bytes at --warmup 8, where it still had content. "I verified
 * determinism" is worthless unless verified at the warmup being measured at.
 *
 * What this test cannot validate: that the runtime values actually match. It
 * asserts the wiring, which is where the defect lived. Real proof is two
 * captures of one fixture at a content-bearing warmup having equal checksums.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1u);
    if (s == NULL) { fclose(f); return NULL; }
    size_t got = fread(s, 1, (size_t)n, f);
    fclose(f);
    s[got] = '\0';
    return s;
}

static int Require(const char *source, const char *needle, const char *message)
{
    if (source != NULL && strstr(source, needle) != NULL) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int HasWallClock(const char *path)
{
    char *text = ReadFile(path);
    int hit = (text != NULL && strstr(text, "GetFrameTime()") != NULL);
    free(text);
    return hit;
}

/* Recurse so a NEWLY ADDED composition file is covered without anyone
 * remembering to list it here — the failure mode is silent, so an opt-in list
 * would rot into a guard that passes while the bug walks back in. */
static int ScanTreeForWallClock(const char *dir, int *failed)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    int scanned = 0;
    if (d == NULL) {
        fprintf(stderr, "FAIL: cannot open %s\n", dir);
        (*failed)++;
        return 0;
    }
    while ((entry = readdir(d)) != NULL) {
        char path[1024];
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (entry->d_type == DT_DIR) {
            scanned += ScanTreeForWallClock(path, failed);
            continue;
        }
        int isSource = (len > 2 && strcmp(name + len - 2, ".c") == 0) ||
                       (len > 4 && strcmp(name + len - 4, ".inl") == 0);
        if (!isSource) continue;
        scanned++;
        if (HasWallClock(path)) {
            fprintf(stderr, "FAIL: %s advances VFX state on wall-clock "
                            "GetFrameTime() — use TimeFX_RawDelta()\n", path);
            (*failed)++;
        }
        /* GetTime() is the same defect wearing a different name: wall-clock
         * seconds since InitWindow, used as shader animation phase. Pinning the
         * delta does nothing for it, which is why it survived the first fix and
         * only showed up when captures were actually compared. */
        {
            char *text = ReadFile(path);
            int hit = (text != NULL && strstr(text, "GetTime()") != NULL);
            free(text);
            if (hit) {
                fprintf(stderr, "FAIL: %s drives animation phase from wall-clock "
                                "GetTime() — use TimeFX_Elapsed()\n", path);
                (*failed)++;
            }
        }
    }
    closedir(d);
    return scanned;
}

int main(void)
{
    int failed = 0;
    char *timeFxH = ReadFile("core/time_fx.h");
    char *timeFxC = ReadFile("core/time_fx.c");
    char *mainC = ReadFile("main.c");

    /* ---- The accessor exists and is published once per frame ------------- */
    failed += Require(timeFxH, "float TimeFX_RawDelta(void)",
                      "core must expose one authoritative frame delta");
    failed += Require(timeFxH, "void  TimeFX_SetRawDelta(float rawDt)",
                      "the frame delta must have a single publisher");
    failed += Require(mainC, "TimeFX_SetRawDelta(rawDt)",
                      "main.c must publish the delta it pins, or the pin reaches nothing");
    failed += Require(mainC, "headlessFixedStep ? (1.0f / 60.0f) : GetFrameTime()",
                      "the headless paths must still pin the step to 1/60");
    /* A pinned clock is only HALF of reproducibility. raylib seeds its RNG from
     * time(NULL) in InitWindow, so Random01() — particle jitter, lifetime, size
     * — walks a different sequence every run. With the step pinned but the RNG
     * free, particle-heavy fixtures still varied more between two identical
     * runs than the parameter under test did across its whole range. */
    failed += Require(mainC, "SetRandomSeed(20260814u)",
                      "headless capture must also pin the RNG, or particle "
                      "fixtures stay irreproducible");
    /* TWO generators, not one. raylib's SetRandomSeed does not touch libc's
     * stream, which InitWindow seeds from time(NULL) and which
     * core/atmosphere.c, core/skill_helper.c and galaxy_spiral_skill draw from
     * directly. Seeding only one leaves scattered sprites moving between runs. */
    failed += Require(mainC, "srand(20260814u)",
                      "libc's rand() is a second stream and needs its own seed");
    failed += Require(timeFxH, "float TimeFX_Elapsed(void)",
                      "animation phase needs a pinned elapsed clock too");
    failed += Require(mainC, "TimeFX_SetDeterministic(headlessMode)",
                      "capture mode must be announced to wall-clock-driven gates");
    /* The third leg. A gate that sheds load on wall-clock frame cost decides
     * differently between two runs, so the CONTENT changes — the capture is
     * irreproducible even with time and RNG both pinned. */
    failed += Require(ReadFile("core/fluid/fluid_surface.c"),
                      "!TimeFX_IsDeterministic()",
                      "the fluid load-shed gate must not decide a capture's content");
    /* A zero default would freeze every accumulator and look like a dead
     * effect rather than missing wiring. */
    failed += Require(timeFxC, "s_rawDelta   = 1.0f / 60.0f",
                      "the delta must default to a sane step, never 0");

    /* ---- No VFX simulation reads the wall clock -------------------------- */
    int scanned = ScanTreeForWallClock("core/composition", &failed);
    if (scanned < 10) {
        fprintf(stderr, "FAIL: only %d composition sources scanned — the walk "
                        "is not reaching the tree\n", scanned);
        failed++;
    }
    if (HasWallClock("sandbox/vfx_test.c")) {
        fprintf(stderr, "FAIL: the VFX tester drives every fixture; wall-clock "
                        "dt there desynchronises ALL of them\n");
        failed++;
    }

    /* ---- The deliberate exceptions stay exceptions -----------------------
     * These WANT wall-clock: a perf counter or a frame-budget gate fed a pinned
     * 1/60 would measure a constant and report a healthy frame forever. Pinning
     * them is the mirror-image mistake, so it gets its own assertion rather
     * than trusting that nobody runs a blanket search-and-replace. */
    if (!HasWallClock("core/post_fx.c")) {
        fprintf(stderr, "FAIL: post_fx perf sampling must keep wall-clock time\n");
        failed++;
    }
    if (!HasWallClock("core/fluid/fluid_surface.c")) {
        fprintf(stderr, "FAIL: the fluid frame-budget gate must keep wall-clock time\n");
        failed++;
    }

    free(timeFxH); free(timeFxC); free(mainC);
    if (failed != 0) {
        puts("frame delta determinism: FAIL");
        return 1;
    }
    printf("PASS: VFX advances on the pinned engine delta (%d composition "
           "sources scanned), perf/budget gates keep wall-clock\n", scanned);
    return 0;
}

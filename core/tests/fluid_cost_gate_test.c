// core headless test — SSF admission rules (FluidSurface_RequestBody).
//
// SSF's cost is almost entirely PER FRAME, not per body, so the expensive
// decision is "does the surface run at all this frame". Before 2026-08-12
// nothing arbitrated that: any number of skills could submit streams and
// nothing skipped SSF when the frame was already over budget.
//
// What this mirrors: the admission arithmetic and the projected-size formula.
//
// What it CANNOT validate: that a rejected caller actually falls back to
// ordinary particles (that is each composer's own code — the water ring spawns
// the same torus with visible colours instead of the alpha-0 the SSF path
// uses), nor the real cost of anything. It asserts the DECISION, not the price.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

#define PRIORITY_MINION   0
#define PRIORITY_BASIC    1
#define PRIORITY_CAST     2
#define PRIORITY_ULTIMATE 3
#define MIN_RADIUS_PX 16.0f
#define BUDGET_MS 26.0f
#define STAMP_TTL 0.10

/* Mirror of FluidSurface_ProjectedRadiusPx (perspective branch). */
static float ProjectedRadiusPx(float distance, float worldRadius,
                               float fovyDegrees, int screenHeight)
{
    if (distance <= worldRadius) return 1e9f;
    float halfFovTan = tanf(fovyDegrees * 0.5f * (float)(M_PI / 180.0));
    return worldRadius / (distance * halfFovTan) * (float)screenHeight * 0.5f;
}

/* Mirror of FluidSurface_RequestBody, in the same order the C does its tests. */
static int Admit(int priority, float frameMs, double now, double surfaceRunStamp,
                 float projectedPx)
{
    if (priority <= PRIORITY_MINION) return 0;
    if (frameMs > BUDGET_MS && priority < PRIORITY_ULTIMATE) return 0;
    if (priority <= PRIORITY_BASIC && (now - surfaceRunStamp) > STAMP_TTL) return 0;
    if (projectedPx < MIN_RADIUS_PX) return 0;
    return 1;
}

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    int bad = 0;
    const float healthy = 14.0f, overBudget = 30.0f;
    const double now = 100.0;
    const double running = now - 0.02;      /* composited two frames ago */
    const double idle    = now - 5.0;       /* nothing has run for seconds */
    const float big = 120.0f, tiny = 4.0f;

    /* --- Ownership by priority --- */
    /* A minion never gets a surface. Not when the frame is cheap, not when the
     * body fills the screen, not when a surface is already running. */
    CHECK(!Admit(PRIORITY_MINION, healthy, now, running, big));
    CHECK(!Admit(PRIORITY_MINION, healthy, now, running, 1e9f));

    /* A basic attack may JOIN a running surface but may never switch one on.
     * This is what makes basic attacks affordable: the frame's fixed cost is
     * already being paid, so its marginal cost is splat area alone. */
    CHECK(Admit(PRIORITY_BASIC, healthy, now, running, big));
    CHECK(!Admit(PRIORITY_BASIC, healthy, now, idle, big));

    /* A cast may switch one on. */
    CHECK(Admit(PRIORITY_CAST, healthy, now, idle, big));

    /* Over budget, a cast is dropped and only a boss ultimate survives. */
    CHECK(!Admit(PRIORITY_CAST, overBudget, now, running, big));
    CHECK(!Admit(PRIORITY_BASIC, overBudget, now, running, big));
    CHECK(Admit(PRIORITY_ULTIMATE, overBudget, now, idle, big));

    /* Budget is tested BEFORE size, so a huge body cannot talk an over-budget
     * frame into a surface. */
    CHECK(!Admit(PRIORITY_CAST, overBudget, now, running, 1e9f));

    /* --- Cull by projected size --- */
    CHECK(!Admit(PRIORITY_CAST, healthy, now, running, tiny));
    CHECK(!Admit(PRIORITY_ULTIMATE, healthy, now, running, tiny));

    /* The formula itself, against the authored fixtures. The water ring is
     * 0.9 m with a 0.12 tube ratio, viewed from ~7.6 m at 45 deg on a 720-line
     * target: comfortably admitted. */
    {
        float ring = ProjectedRadiusPx(7.56f, 0.9f * 1.12f, 45.0f, 720);
        CHECK(ring > MIN_RADIUS_PX * 4.0f);
        printf("      water ring at 7.6 m projects to %.0f px radius\n", ring);
        /* The same ring seen from far across the arena is not worth a surface. */
        float distant = ProjectedRadiusPx(90.0f, 0.9f * 1.12f, 45.0f, 720);
        CHECK(distant < MIN_RADIUS_PX);
        /* Doubling the distance halves the projected radius. */
        float a = ProjectedRadiusPx(10.0f, 1.0f, 45.0f, 720);
        float b = ProjectedRadiusPx(20.0f, 1.0f, 45.0f, 720);
        CHECK(fabsf(a - 2.0f * b) < 1e-3f);
    }
    /* A camera inside the body is emphatically not "small". */
    CHECK(ProjectedRadiusPx(0.5f, 2.0f, 45.0f, 720) > 1e8f);

    /* --- The anti-latch property --- */
    /* This one exists because the first implementation DID latch: the budget
     * and "is a surface running" were flags rolled inside
     * FluidSurface_Composite, which main.c only calls when something was
     * submitted. One slow start-up frame closed the gate, Composite then never
     * ran, the flags were never updated, and the water ring was deleted for
     * good. The state the gate reads must age out on its own.
     *
     * Asserted as: a gate that was closed by an over-budget frame reopens as
     * soon as the frame time recovers, with NO other state having changed. */
    CHECK(!Admit(PRIORITY_CAST, overBudget, now, idle, big));
    CHECK(Admit(PRIORITY_CAST, healthy, now, idle, big));
    /* And a stale "surface is running" claim expires rather than persisting. */
    CHECK(Admit(PRIORITY_BASIC, healthy, now, now - STAMP_TTL * 0.5, big));
    CHECK(!Admit(PRIORITY_BASIC, healthy, now, now - STAMP_TTL * 2.0, big));

    /* Source assertions: the arithmetic above cannot see WHERE the C reads its
     * inputs from, and reading them from a cache updated only inside Composite
     * is exactly the latch. */
    {
        char *src = ReadFile("core/fluid/fluid_surface.c");
        if (!src) { printf("FAIL: cannot read fluid_surface.c\n"); bad++; }
        else
        {
            const char *fn = strstr(src, "bool FluidSurface_RequestBody");
            CHECK(fn != NULL);
            if (fn)
            {
                const char *end = strstr(fn, "\nFluidLiquidDesc");
                size_t len = end ? (size_t)(end - fn) : strlen(fn);
                char *body = (char *)malloc(len + 1);
                memcpy(body, fn, len); body[len] = '\0';
                /* Live reads, not cached flags. */
                CHECK(strstr(body, "GetFrameTime()") != NULL);
                CHECK(strstr(body, "s_surfaceRunStamp") != NULL);
                free(body);
            }
            /* The stamp must be set where a composite actually completes. */
            CHECK(strstr(src, "s_surfaceRunStamp=GetTime();") != NULL);
            free(src);
        }
    }

    printf(bad ? "fluid_cost_gate_test: %d failure(s)\n"
               : "PASS: fluid_cost_gate_test (0 failures)\n", bad);
    return bad ? 1 : 0;
}

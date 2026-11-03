/* Headless source audit — core/composition/water/vc_guided_liquid.inl.
 *
 * Why a source test: this composition was rewritten from "a spline position
 * plus a decorative arc" to "an exact ballistic guide frame + real bodies
 * integrated against a motion field". Visual review cannot tell a real force
 * integration from a convincing lerp, and the previous version regressed back
 * to per-frame RNG and positional convergence more than once. These are the
 * invariants that separate the two implementations, so they are asserted here
 * rather than trusted to review. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

static char *ReadFile(const char *path, long *outSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (outSize) *outSize = (long)got;
    return buf;
}

int main(void)
{
    long size = 0;
    char *src = ReadFile("core/composition/water/vc_guided_liquid.inl", &size);
    CHECK(src != NULL && size > 1000, "guided liquid composition is readable");
    if (!src) { printf("\nFAIL: source missing\n"); return 1; }

    /* ── The physics it must be built from ──────────────────────────────── */
    CHECK(strstr(src, "LiquidBallistic_SolveApex") != NULL,
          "uses the exact apex projectile solver");
    CHECK(strstr(src, "LiquidBallistic_SolveSpeed") != NULL,
          "uses the exact speed projectile solver");
    CHECK(strstr(src, "LiquidBallistic_Sample") != NULL,
          "samples the ballistic solution for position and velocity");
    CHECK(strstr(src, "MotionFields_CreateField") != NULL,
          "creates a typed motion field for the medium");
    CHECK(strstr(src, "MotionFields_SetTransform") != NULL,
          "moves that field with the guide frame");
    CHECK(strstr(src, "frameVelocityMps") != NULL,
          "publishes the frame velocity so the medium carries the body");
    CHECK(strstr(src, "angularVelocityRadPerSec") != NULL,
          "publishes the body's rotation to the medium");
    CHECK(strstr(src, "MotionFields_SampleSpatialBodyAtOffset") != NULL,
          "samples the medium for every droplet");
    CHECK(strstr(src, "MotionBody_AdvanceFieldVelocity") != NULL,
          "advances droplets with the shared body integrator");
    CHECK(strstr(src, "MotionBody_GetPhysicalProperties") != NULL,
          "derives drag/buoyancy properties from the real body");
    CHECK(strstr(src, "windSusceptibility = 1.0f") != NULL,
          "enables the quadratic drag term (0 would snap droplets to the air)");
    CHECK(strstr(src, "LiquidMedium_StableFraction") != NULL,
          "uses Weber-driven fragmentation instead of a hard-coded fade");
    CHECK(strstr(src, "LiquidMedium_YieldSpeedMps") != NULL,
          "uses the Bingham yield velocity for sticking and spread");
    CHECK(strstr(src, "LiquidImpact_SpawnWater") != NULL,
          "hands the real incoming velocity to the engine impact model");
    CHECK(strstr(src, "event.externalBody") != NULL && strstr(src, "event.forceFieldOnly") != NULL,
          "impact is registered as an external, force-field-driven body");

    /* ── The update path must not contain a cheat ───────────────────────── */
    const char *update = strstr(src, "void VC_GuidedLiquid_Update(");
    CHECK(update != NULL, "update entry point exists");
    if (update) {
        CHECK(strstr(update, "MotionFields_SampleSpatialBodyAtOffset") != NULL,
              "the update loop really samples the medium (not just at create)");
        CHECK(strstr(update, "MotionBody_AdvanceFieldVelocity") != NULL,
              "the update loop integrates bodies with forces");
        CHECK(strstr(update, "Vector3Lerp") == NULL,
              "no positional lerp anywhere in the flight integration");
        CHECK(strstr(update, "GuidedLiquid_RandomFloat") == NULL,
              "no per-frame RNG on the flight path (arcs are fixed at spawn)");
        CHECK(strstr(update, "Vector3Distance(d->pos, inst->curTarget)") != NULL,
              "jet droplets arrive by real distance to the target");
        CHECK(strstr(update, "launchT >= inst->launch.durationSec") != NULL,
              "body droplets arrive when the ballistic frame does");
    }

    /* The old per-frame arc jitter drew a new random height every frame; the
     * deterministic spawn RNG must stay in the compose path only. */
    const char *flying = strstr(src, "case DROPLET_STATE_FLYING");
    CHECK(flying != NULL, "flying state exists");
    if (flying && update) {
        long flyingOffset = (long)(flying - src);
        long updateOffset = (long)(update - src);
        CHECK(flyingOffset > updateOffset, "flying state lives in the update path");
    }

    free(src);

    printf("\n%s: %d checks, %d failures\n",
           g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

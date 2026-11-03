/* Headless test — core/liquid/liquid_ballistic.h (exact projectile solutions).
 *
 * The guided-liquid frame is built from these solutions, so the property that
 * matters is not "the arc looks nice" but "the closed form actually lands on
 * the target and its derivative is a velocity": position at durationSec must
 * equal the target, and the sampled velocity must equal d/dt of the sampled
 * position. A spline lerp passes neither. */
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "core/liquid/liquid_ballistic.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

#define CHECK_NEAR(a, b, tol, name) do { \
    g_checks++; \
    float _a = (a), _b = (b); \
    if (fabsf(_a - _b) <= (tol)) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [%.9g vs %.9g]\n", name, (double)_a, (double)_b); g_failures++; } \
} while (0)

static float VecDist(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

int main(void)
{
    const float g = 9.80665f;

    /* ── Apex-authored lob ──────────────────────────────────────────────── */
    LiquidBallisticSolution apex;
    Vector3 from = {0.0f, 0.0f, 0.0f};
    Vector3 to = {10.0f, 0.0f, 0.0f};
    CHECK(LiquidBallistic_SolveApex(from, to, 3.0f, g, &apex), "apex solve succeeds");
    CHECK_NEAR(apex.verticalSpeedMps, sqrtf(2.0f * g * 3.0f), 1e-3f,
               "apex solve derives vy from the authored apex");
    CHECK_NEAR(apex.horizontalSpeedMps, 10.0f / apex.durationSec, 1e-3f,
               "horizontal speed covers the distance in the solved time");
    CHECK_NEAR(LiquidBallistic_ApexAboveOriginM(&apex), 3.0f, 1e-2f,
               "solved trajectory peaks at the authored height");

    Vector3 p, v, a;
    LiquidBallistic_Sample(&apex, apex.durationSec, &p, &v, &a);
    CHECK(VecDist(p, to) < 1e-3f, "apex shot lands on the target at durationSec");
    CHECK_NEAR(a.y, -g, 1e-4f, "acceleration is gravity at every instant");

    /* Apex height, measured from the sample, must match the author's request. */
    float tApex = apex.verticalSpeedMps / g;
    LiquidBallistic_Sample(&apex, tApex, &p, NULL, NULL);
    CHECK_NEAR(p.y, 3.0f, 1e-2f, "sampled peak height equals the authored apex");

    /* Velocity is the derivative of position: central difference must match. */
    float t = 0.37f * apex.durationSec;
    float dt = 1e-4f;
    Vector3 p0, p1, vMid;
    LiquidBallistic_Sample(&apex, t - dt, &p0, NULL, NULL);
    LiquidBallistic_Sample(&apex, t + dt, &p1, NULL, NULL);
    LiquidBallistic_Sample(&apex, t, NULL, &vMid, NULL);
    Vector3 fd = {(p1.x - p0.x) / (2.0f * dt),
                  (p1.y - p0.y) / (2.0f * dt),
                  (p1.z - p0.z) / (2.0f * dt)};
    CHECK(VecDist(fd, vMid) < 1e-2f, "sampled velocity is the derivative of position");

    /* ── Target above the launcher: apex is raised, never complex ───────── */
    LiquidBallisticSolution high;
    Vector3 highTarget = {6.0f, 4.0f, 0.0f};
    CHECK(LiquidBallistic_SolveApex(from, highTarget, 0.5f, g, &high),
          "apex solve raises the apex to clear a high target");
    LiquidBallistic_Sample(&high, high.durationSec, &p, NULL, NULL);
    CHECK(VecDist(p, highTarget) < 1e-3f, "raised-apex shot still lands on target");

    /* ── Speed-authored shot ────────────────────────────────────────────── */
    LiquidBallisticSolution speed;
    Vector3 far = {20.0f, 0.0f, 0.0f};
    CHECK(LiquidBallistic_SolveSpeed(from, far, 10.0f, g, &speed), "speed solve succeeds");
    CHECK_NEAR(speed.durationSec, 2.0f, 1e-5f, "speed solve derives the flight time");
    CHECK_NEAR(speed.verticalSpeedMps, 0.5f * g * 2.0f, 1e-3f,
               "speed solve derives the elevation needed to land");
    LiquidBallistic_Sample(&speed, speed.durationSec, &p, NULL, NULL);
    CHECK(VecDist(p, far) < 1e-3f, "speed shot lands on the target");
    CHECK_NEAR(LiquidBallistic_ApexAboveOriginM(&speed),
               (0.5f * g * speed.durationSec) * (0.5f * g * speed.durationSec) / (2.0f * g),
               1e-2f, "speed shot apex follows from g and the flight time");

    /* ── Degenerate and post-arrival cases ──────────────────────────────── */
    LiquidBallisticSolution degenerate;
    CHECK(!LiquidBallistic_SolveApex(from, from, 2.0f, g, &degenerate),
          "coincident points are rejected (no aim direction)");
    CHECK(!LiquidBallistic_SolveApex(from, to, 2.0f, 0.0f, &degenerate),
          "zero gravity is rejected");
    CHECK(!LiquidBallistic_SolveSpeed(from, far, 0.0f, g, &degenerate),
          "speed below the minimum is rejected");
    CHECK(!LiquidBallistic_SolveSpeed(from, far, 10.0f, -3.0f, &degenerate),
          "negative gravity is rejected");

    LiquidBallistic_Sample(&apex, apex.durationSec + 0.5f, &p, &v, NULL);
    CHECK(p.y < 0.0f, "past arrival the shot keeps falling (gravity is not switched off)");
    CHECK(v.y < 0.0f, "past arrival the vertical velocity is downward");

    CHECK(LiquidBallistic_MinimumFlightTimeMps(from, far, g) > 0.0f,
          "minimum flight time is positive for a non-zero distance");

    printf("\n%s: %d checks, %d failures\n",
           g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

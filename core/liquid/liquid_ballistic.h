#ifndef CORE_LIQUID_BALLISTIC_H
#define CORE_LIQUID_BALLISTIC_H

/* Exact projectile solutions for launched liquid.
 *
 * A "guided" liquid still obeys gravity between steering impulses, so the one
 * thing its guide frame must never do is interpolate position along a spline:
 * that produces a constant-speed straight line with a decorative arc added on
 * top, and its derivative is not a velocity at all. These helpers solve the
 * closed-form ballistic problem instead, so the guide frame carries a real
 * velocity and a real acceleration (always the gravity vector) and a VFX that
 * uses it cannot drift out of phase with `PHYSICS_GRAVITY_MPS2`.
 *
 * Two parameterisations are needed because they answer different authoring
 * questions, and both are exact:
 *
 *   Apex    "lob over that wall"      -> apex height above the launch point is
 *                                        authored, flight time is derived.
 *   Speed   "fire it at N m/s"        -> horizontal muzzle speed is authored,
 *                                        the launch elevation is derived so the
 *                                        shot still lands on the target.
 *
 * Dependency-free (raylib types only, no allocation) so it is testable
 * headlessly — see core/tests/liquid_ballistic_test.c.
 */

#include "raylib.h"
#include <math.h>
#include <stdbool.h>

#define LIQUID_BALLISTIC_MIN_SPEED_MPS 0.05f
#define LIQUID_BALLISTIC_MIN_DISTANCE_M 0.01f

typedef struct LiquidBallisticSolution {
    Vector3 origin;           /* launch point, metres */
    Vector3 horizontalDir;    /* unit vector in the XZ plane */
    float horizontalSpeedMps; /* constant (drag-free) horizontal speed */
    float verticalSpeedMps;   /* launch vertical speed, may be negative */
    float gravityMps2;        /* positive magnitude */
    float durationSec;        /* time to reach the target */
} LiquidBallisticSolution;

static inline float LiquidBallistic_HorizontalDistance(Vector3 from, Vector3 to)
{
    float dx = to.x - from.x;
    float dz = to.z - from.z;
    return sqrtf(dx * dx + dz * dz);
}

static inline bool LiquidBallistic_ValidGravity(float gravityMps2)
{
    return isfinite(gravityMps2) && gravityMps2 > 0.0f;
}

/* Apex-authored solution. `apexAboveOriginM` is measured from the launch point,
 * never from the target, so lowering the target does not silently change the
 * lob height. The solve raises the apex if needed to clear a target above the
 * launcher, so it can never return a complex root. */
static inline bool LiquidBallistic_SolveApex(Vector3 from, Vector3 to, float apexAboveOriginM,
                                             float gravityMps2,
                                             LiquidBallisticSolution *out)
{
    if (!out || !LiquidBallistic_ValidGravity(gravityMps2) ||
        !isfinite(from.x) || !isfinite(from.y) || !isfinite(from.z) ||
        !isfinite(to.x) || !isfinite(to.y) || !isfinite(to.z))
        return false;
    float distance = LiquidBallistic_HorizontalDistance(from, to);
    if (distance < LIQUID_BALLISTIC_MIN_DISTANCE_M) return false;
    float dy = to.y - from.y;
    float apex = apexAboveOriginM;
    if (!isfinite(apex)) apex = 0.0f;
    /* Must clear the target by a margin, otherwise vy^2 - 2*g*dy <= 0. */
    float floorApex = dy + 0.20f > 0.05f ? dy + 0.20f : 0.05f;
    if (apex < floorApex) apex = floorApex;

    float vy = sqrtf(2.0f * gravityMps2 * apex);
    float disc = vy * vy - 2.0f * gravityMps2 * dy;
    if (!(disc > 0.0f)) return false;
    float duration = (vy + sqrtf(disc)) / gravityMps2;
    if (!(duration > 0.0f) || !isfinite(duration)) return false;

    *out = (LiquidBallisticSolution){
        .origin = from,
        .horizontalDir = (Vector3){(to.x - from.x) / distance, 0.0f, (to.z - from.z) / distance},
        .horizontalSpeedMps = distance / duration,
        .verticalSpeedMps = vy,
        .gravityMps2 = gravityMps2,
        .durationSec = duration};
    return true;
}

/* Speed-authored solution: the horizontal muzzle speed is fixed and the launch
 * elevation is solved so the projectile still lands on `to`. This is the honest
 * form of "the jet flies at 32 m/s" — gravity still curves it. */
static inline bool LiquidBallistic_SolveSpeed(Vector3 from, Vector3 to, float horizontalSpeedMps,
                                              float gravityMps2,
                                              LiquidBallisticSolution *out)
{
    if (!out || !LiquidBallistic_ValidGravity(gravityMps2) ||
        !isfinite(horizontalSpeedMps) ||
        horizontalSpeedMps < LIQUID_BALLISTIC_MIN_SPEED_MPS)
        return false;
    float distance = LiquidBallistic_HorizontalDistance(from, to);
    if (distance < LIQUID_BALLISTIC_MIN_DISTANCE_M) return false;
    float dy = to.y - from.y;
    float duration = distance / horizontalSpeedMps;
    if (!(duration > 0.0f) || !isfinite(duration)) return false;
    float vy = (dy + 0.5f * gravityMps2 * duration * duration) / duration;

    *out = (LiquidBallisticSolution){
        .origin = from,
        .horizontalDir = (Vector3){(to.x - from.x) / distance, 0.0f, (to.z - from.z) / distance},
        .horizontalSpeedMps = horizontalSpeedMps,
        .verticalSpeedMps = vy,
        .gravityMps2 = gravityMps2,
        .durationSec = duration};
    return true;
}

/* Analytic state at `t` seconds after launch: position, velocity and the
 * (constant) acceleration. t may exceed duration; the caller decides what the
 * frame does after arrival. */
static inline void LiquidBallistic_Sample(const LiquidBallisticSolution *s, float t, Vector3 *position,
                                          Vector3 *velocity, Vector3 *acceleration)
{
    if (!s) return;
    if (!isfinite(t)) t = 0.0f;
    Vector3 v = {
        s->horizontalDir.x * s->horizontalSpeedMps,
        s->verticalSpeedMps - s->gravityMps2 * t,
        s->horizontalDir.z * s->horizontalSpeedMps};
    if (position) {
        position->x = s->origin.x + s->horizontalDir.x * s->horizontalSpeedMps * t;
        position->y = s->origin.y + s->verticalSpeedMps * t - 0.5f * s->gravityMps2 * t * t;
        position->z = s->origin.z + s->horizontalDir.z * s->horizontalSpeedMps * t;
    }
    if (velocity) *velocity = v;
    if (acceleration) *acceleration = (Vector3){0.0f, -s->gravityMps2, 0.0f};
}

/* Peak height above the launch point, i.e. what the author asked for. */
static inline float LiquidBallistic_ApexAboveOriginM(const LiquidBallisticSolution *s)
{
    if (!s || s->gravityMps2 <= 0.0f) return 0.0f;
    if (s->verticalSpeedMps <= 0.0f) return 0.0f;
    return s->verticalSpeedMps * s->verticalSpeedMps / (2.0f * s->gravityMps2);
}

/* Shortest possible flight time between two points in vacuum, used to budget
 * sub-arrival phases without an authored speed. */
static inline float LiquidBallistic_MinimumFlightTimeMps(Vector3 from, Vector3 to, float gravityMps2)
{
    if (!LiquidBallistic_ValidGravity(gravityMps2)) return 0.0f;
    Vector3 delta = {to.x - from.x, to.y - from.y, to.z - from.z};
    float length = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    return length > 0.0f ? sqrtf(2.0f * length / gravityMps2) : 0.0f;
}

#endif /* CORE_LIQUID_BALLISTIC_H */

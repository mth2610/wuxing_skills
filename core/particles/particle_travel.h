#ifndef CORE_PARTICLES_PARTICLE_TRAVEL_H
#define CORE_PARTICLES_PARTICLE_TRAVEL_H

#include "raylib.h"

#include <math.h>
#include <stdbool.h>

#ifndef CORE_FORCE_FIELD_TYPE_DECLARED
typedef struct ForceField ForceField;
#define CORE_FORCE_FIELD_TYPE_DECLARED 1
#endif

#define PARTICLE_TRAVEL_MAX_WAYPOINTS 16

/* Shared, caller-owned route for guided particles.
 *
 * `points` are world-space intermediate/final waypoints. If `target` is not
 * NULL, its current value is appended as the final waypoint every frame, which
 * permits a moving target without rebuilding emitters. All pointers must remain
 * valid until the last particle using the route dies.
 *
 * Defaults: speed <= 0 keeps the current speed (or 1 m/s from rest), steering
 * <= 0 uses 8/s, waypointRadius <= 0 uses 0.10 m, and targetRadius <= 0 uses
 * waypointRadius. maxAcceleration <= 0 means no acceleration clamp. */
typedef struct ParticleTravelPath {
    const Vector3 *points;
    int pointCount;
    const Vector3 *target;
    float speed;
    float steering;
    float maxAcceleration;
    float waypointRadius;
    float targetRadius;
    /* Optional post-arrival phase. When set, the particle remains alive at the
     * target, receives the entry kick/offset, then is driven by this field. */
    const ForceField *arrivalForceField;
    float arrivalOffset;
    float arrivalKick;
    /* Incoming velocity multiplier on arrival. Zero keeps the default 1. */
    float arrivalVelocityScale;
    /* Seconds to evaluate arrivalForceField; zero keeps it active indefinitely. */
    float arrivalForceDuration;
} ParticleTravelPath;

static inline int ParticleTravel_WaypointCount(const ParticleTravelPath *path)
{
    int count, maxPoints;
    if (!path) return 0;
    count = path->pointCount;
    if (count < 0 || !path->points) count = 0;
    maxPoints = PARTICLE_TRAVEL_MAX_WAYPOINTS - (path->target ? 1 : 0);
    if (count > maxPoints) count = maxPoints;
    if (path->target) count++;
    return count;
}

static inline Vector3 ParticleTravel_GetWaypoint(const ParticleTravelPath *path,
                                                  int index)
{
    int pointCount;
    if (!path) return (Vector3){0};
    pointCount = (path->points && path->pointCount > 0) ? path->pointCount : 0;
    if (pointCount > PARTICLE_TRAVEL_MAX_WAYPOINTS - (path->target ? 1 : 0))
        pointCount = PARTICLE_TRAVEL_MAX_WAYPOINTS - (path->target ? 1 : 0);
    if (index >= 0 && index < pointCount) return path->points[index];
    if (path->target && index == pointCount &&
        index < PARTICLE_TRAVEL_MAX_WAYPOINTS)
        return *path->target;
    return (Vector3){0};
}

static inline float ParticleTravel_SegmentDistanceSq(Vector3 a, Vector3 b,
                                                       Vector3 point)
{
    float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    float apx = point.x - a.x, apy = point.y - a.y, apz = point.z - a.z;
    float denom = abx * abx + aby * aby + abz * abz;
    float t = denom > 1e-8f ? (apx * abx + apy * aby + apz * abz) / denom : 0.0f;
    float dx, dy, dz;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    dx = a.x + abx * t - point.x;
    dy = a.y + aby * t - point.y;
    dz = a.z + abz * t - point.z;
    return dx * dx + dy * dy + dz * dz;
}

static inline void ParticleTravel_ApplyImpactEntry(const ParticleTravelPath *path,
                                                    Vector3 *position,
                                                    Vector3 *velocity)
{
    Vector3 direction;
    float length;
    if (!path || !position || !velocity) return;
    direction = *velocity;
    length = sqrtf(direction.x * direction.x + direction.y * direction.y +
                   direction.z * direction.z);
    if (length <= 1e-5f) direction = (Vector3){1.0f, 0.0f, 0.0f};
    else {
        float invLength = 1.0f / length;
        direction.x *= invLength;
        direction.y *= invLength;
        direction.z *= invLength;
    }
    if (path->arrivalOffset > 0.0f)
    {
        position->x += direction.x * path->arrivalOffset;
        position->y += direction.y * path->arrivalOffset;
        position->z += direction.z * path->arrivalOffset;
    }
    if (path->arrivalVelocityScale > 0.0f)
    {
        velocity->x *= path->arrivalVelocityScale;
        velocity->y *= path->arrivalVelocityScale;
        velocity->z *= path->arrivalVelocityScale;
    }
    if (path->arrivalKick != 0.0f)
    {
        velocity->x += direction.x * path->arrivalKick;
        velocity->y += direction.y * path->arrivalKick;
        velocity->z += direction.z * path->arrivalKick;
    }
}

/* Apply path steering after external forces/drag, integrate once, advance the
 * route, and return true on final-target arrival. The swept test prevents fast
 * particles tunnelling through a small target between frames. */
static inline bool ParticleTravel_Step(const ParticleTravelPath *path, float dt,
                                       Vector3 *position, Vector3 *velocity,
                                       int *waypointIndex)
{
    int count, waypoint, finalIndex;
    Vector3 goal, desired, delta, next;
    float dx, dy, dz, distance, speed, steering, blend, changeLength;
    float waypointRadius, targetRadius, hitRadius;

    if (!path || !position || !velocity || !waypointIndex || dt <= 0.0f)
        return false;
    count = ParticleTravel_WaypointCount(path);
    if (count <= 0) return false;
    finalIndex = count - 1;
    waypoint = *waypointIndex;
    if (waypoint < 0) waypoint = 0;
    if (waypoint > finalIndex) waypoint = finalIndex;

    waypointRadius = path->waypointRadius > 0.0f ? path->waypointRadius : 0.10f;
    targetRadius = path->targetRadius > 0.0f ? path->targetRadius : waypointRadius;

    /* Consume waypoints already occupied at the start of the step. */
    while (waypoint < finalIndex) {
        goal = ParticleTravel_GetWaypoint(path, waypoint);
        dx = position->x - goal.x;
        dy = position->y - goal.y;
        dz = position->z - goal.z;
        if (dx * dx + dy * dy + dz * dz > waypointRadius * waypointRadius)
            break;
        waypoint++;
    }

    goal = ParticleTravel_GetWaypoint(path, waypoint);
    dx = goal.x - position->x;
    dy = goal.y - position->y;
    dz = goal.z - position->z;
    distance = sqrtf(dx * dx + dy * dy + dz * dz);
    speed = path->speed;
    if (speed <= 0.0f) {
        speed = sqrtf(velocity->x * velocity->x + velocity->y * velocity->y +
                      velocity->z * velocity->z);
        if (speed <= 1e-4f) speed = 1.0f;
    }
    if (distance > 1e-5f) {
        desired = (Vector3){dx * speed / distance, dy * speed / distance,
                            dz * speed / distance};
    } else {
        desired = (Vector3){0};
    }

    steering = path->steering > 0.0f ? path->steering : 8.0f;
    blend = 1.0f - expf(-steering * dt);
    delta = (Vector3){(desired.x - velocity->x) * blend,
                      (desired.y - velocity->y) * blend,
                      (desired.z - velocity->z) * blend};
    changeLength = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (path->maxAcceleration > 0.0f &&
        changeLength > path->maxAcceleration * dt && changeLength > 1e-6f) {
        float scale = path->maxAcceleration * dt / changeLength;
        delta.x *= scale;
        delta.y *= scale;
        delta.z *= scale;
    }
    velocity->x += delta.x;
    velocity->y += delta.y;
    velocity->z += delta.z;
    next = (Vector3){position->x + velocity->x * dt,
                     position->y + velocity->y * dt,
                     position->z + velocity->z * dt};

    hitRadius = waypoint == finalIndex ? targetRadius : waypointRadius;
    if (ParticleTravel_SegmentDistanceSq(*position, next, goal) <= hitRadius * hitRadius) {
        if (waypoint == finalIndex) {
            *position = goal;
            *waypointIndex = waypoint;
            return true;
        }
        waypoint++;
    }

    *position = next;
    *waypointIndex = waypoint;
    return false;
}

#endif /* CORE_PARTICLES_PARTICLE_TRAVEL_H */

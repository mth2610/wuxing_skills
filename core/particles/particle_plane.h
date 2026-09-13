#ifndef CORE_PARTICLE_PLANE_H
#define CORE_PARTICLE_PLANE_H

#include "raylib.h"
#include <math.h>
#include <stdbool.h>

/* One-sided infinite receiver, resolved after integration. Projecting the
 * endpoint prevents tunnelling through this plane even at high speed. This is
 * not a sweep against finite props or terrain. No neighbour solve is involved. */
static inline bool ParticlePlane_Resolve(Vector3 point, Vector3 normal,
    float radius, float restitution, float tangentRetention,
    Vector3 *position, Vector3 *velocity)
{
    float length = sqrtf(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
    if (length < 1e-6f) return false;
    normal.x /= length; normal.y /= length; normal.z /= length;
    float distance = (position->x-point.x)*normal.x +
                     (position->y-point.y)*normal.y +
                     (position->z-point.z)*normal.z;
    radius = fmaxf(radius, 0.0f);
    if (distance >= radius) return false;
    float correction = radius - distance;
    position->x += normal.x*correction;
    position->y += normal.y*correction;
    position->z += normal.z*correction;
    float speed = velocity->x*normal.x + velocity->y*normal.y + velocity->z*normal.z;
    if (speed < 0.0f) {
        float bounce = -speed*fminf(fmaxf(restitution, 0.0f), 1.0f);
        float retain = fminf(fmaxf(tangentRetention, 0.0f), 1.0f);
        velocity->x = (velocity->x-normal.x*speed)*retain + normal.x*bounce;
        velocity->y = (velocity->y-normal.y*speed)*retain + normal.y*bounce;
        velocity->z = (velocity->z-normal.z*speed)*retain + normal.z*bounce;
    }
    return true;
}
#endif

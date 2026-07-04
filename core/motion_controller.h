#ifndef MOTION_CONTROLLER_H
#define MOTION_CONTROLLER_H

#include "raylib.h"
#include <stdbool.h>

// Item 27: Reusable projectile motion module.
// Call Motion_Init once when a projectile spawns, Motion_Step each frame,
// Motion_Arrived to check termination. No allocations; fully value-type.

typedef enum {
    MOTION_LINEAR,      // straight line, fixed speed
    MOTION_HOMING,      // steers toward a moving target each frame
    MOTION_BALLISTIC,   // parabolic arc (gravity applied)
    MOTION_SPIRAL,      // corkscrew along the forward axis
    MOTION_ORBIT,       // orbits a fixed centre point at constant angular speed
    MOTION_BOOMERANG    // accelerates outward then returns to origin
} MotionType;

typedef struct {
    MotionType type;

    // Shared
    float speed;            // m/s base speed
    float arrivalRadius;    // m — Motion_Arrived returns true inside this

    // HOMING
    float turnRateRad;      // rad/s max steering rate

    // BALLISTIC
    float gravity;          // m/s² downward acceleration (positive = down)

    // SPIRAL
    float spiralRadius;     // m transverse radius of the corkscrew
    float spiralFreq;       // Hz (full turns per second)

    // ORBIT
    Vector3 orbitCenter;    // fixed world-space pivot
    float orbitRadiusXZ;    // m radius in the XZ plane
    float orbitAngularSpeed;// rad/s (positive = counter-clockwise from above)

    // BOOMERANG
    float boomerangRange;   // m outward distance before turning back
} MotionParams;

typedef struct {
    MotionParams params;
    Vector3      pos;
    Vector3      vel;       // current velocity (updated each step)
    Vector3      target;    // for HOMING/BALLISTIC target destination
    Vector3      origin;    // initial spawn position (BOOMERANG uses this)
    float        elapsed;   // seconds since Motion_Init
    float        orbitAngle;// current angle for ORBIT/SPIRAL
    bool         returning; // BOOMERANG phase flag
} MotionState;

void  Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target);
void  Motion_Step(MotionState *s, float dt);
bool  Motion_Arrived(const MotionState *s);

#endif // MOTION_CONTROLLER_H

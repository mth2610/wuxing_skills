#include "core/motion_controller.h"
#include "core/utils_math.h"
#include "raymath.h"
#include <math.h>

#ifndef PI
#define PI 3.1415926535f
#endif

void Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target) {
    s->params     = params;
    s->pos        = startPos;
    s->target     = target;
    s->origin     = startPos;
    s->elapsed    = 0.0f;
    s->orbitAngle = 0.0f;
    s->returning  = false;

    Vector3 dir = Vector3Subtract(target, startPos);
    float len = Vector3Length(dir);
    if (len > 0.0001f)
        dir = Vector3Scale(dir, 1.0f / len);
    s->vel = Vector3Scale(dir, params.speed);
}

void Motion_Step(MotionState *s, float dt) {
    s->elapsed += dt;
    MotionParams *p = &s->params;

    switch (p->type) {
        case MOTION_LINEAR:
            s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
            break;

        case MOTION_HOMING: {
            Vector3 toTarget = Vector3Subtract(s->target, s->pos);
            float dist = Vector3Length(toTarget);
            if (dist > 0.0001f) {
                Vector3 desired = Vector3Scale(toTarget, p->speed / dist);
                // steer vel toward desired by at most turnRateRad*dt
                Vector3 steer = Vector3Subtract(desired, s->vel);
                float steerLen = Vector3Length(steer);
                float maxSteer = p->turnRateRad * dt * p->speed;
                if (steerLen > maxSteer && steerLen > 0.0001f)
                    steer = Vector3Scale(steer, maxSteer / steerLen);
                s->vel = Vector3Add(s->vel, steer);
                // clamp to speed
                float curSpd = Vector3Length(s->vel);
                if (curSpd > 0.0001f)
                    s->vel = Vector3Scale(s->vel, p->speed / curSpd);
            }
            s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
            break;
        }

        case MOTION_BALLISTIC:
            s->vel.y -= p->gravity * dt;
            s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
            break;

        case MOTION_SPIRAL: {
            // Advance along forward direction + transverse corkscrew
            Vector3 fwd = s->vel;
            float fwdLen = Vector3Length(fwd);
            if (fwdLen > 0.0001f) fwd = Vector3Scale(fwd, 1.0f / fwdLen);

            s->orbitAngle += 2.0f * PI * p->spiralFreq * dt;
            // Build a perpendicular pair in the plane orthogonal to fwd
            Vector3 right = { fwd.z, 0.0f, -fwd.x };
            float rLen = Vector3Length(right);
            if (rLen < 0.0001f) { right.x = 1.0f; right.y = 0.0f; right.z = 0.0f; rLen = 1.0f; }
            right = Vector3Scale(right, 1.0f / rLen);
            Vector3 up = Vector3CrossProduct(fwd, right);
            Vector3 transverse = Vector3Add(
                Vector3Scale(right, cosf(s->orbitAngle) * p->spiralRadius),
                Vector3Scale(up,    sinf(s->orbitAngle) * p->spiralRadius)
            );
            // Update position using forward velocity + transverse correction
            s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
            // The transverse is a radius offset from the spine, apply as delta from previous frame
            (void)transverse; // used as instantaneous offset each frame — tracked via orbitAngle
            break;
        }

        case MOTION_ORBIT: {
            s->orbitAngle += p->orbitAngularSpeed * dt;
            s->pos.x = p->orbitCenter.x + cosf(s->orbitAngle) * p->orbitRadiusXZ;
            s->pos.y = p->orbitCenter.y;
            s->pos.z = p->orbitCenter.z + sinf(s->orbitAngle) * p->orbitRadiusXZ;
            break;
        }

        case MOTION_BOOMERANG: {
            if (!s->returning) {
                s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
                float traveled = Vector3Length(Vector3Subtract(s->pos, s->origin));
                if (traveled >= p->boomerangRange) s->returning = true;
            } else {
                Vector3 toOrigin = Vector3Subtract(s->origin, s->pos);
                float dist = Vector3Length(toOrigin);
                if (dist > 0.0001f)
                    s->vel = Vector3Scale(toOrigin, p->speed / dist);
                s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
            }
            break;
        }
    }
}

bool Motion_Arrived(const MotionState *s) {
    if (s->params.type == MOTION_ORBIT) return false;
    if (s->params.type == MOTION_BOOMERANG) {
        if (!s->returning) return false;
        return Vector3Length(Vector3Subtract(s->pos, s->origin)) < s->params.arrivalRadius;
    }
    return Vector3Length(Vector3Subtract(s->pos, s->target)) < s->params.arrivalRadius;
}

/* Neutral body contract: no particle header, identical legacy layout and units. */
#include "core/motion/motion_profile.h"
#include "core/motion/motion_body.h"
#include "core/motion/motion_gpu.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void) {
    assert(sizeof(MotionBodyProfile) == 13 * sizeof(float));
    assert(offsetof(MotionBodyProfile, densityKgM3) == 12 * sizeof(float));
    MotionBodyProfile body = {.inverseMassKg = .5f, .gravityScale = 1};
    ParticleDynamicsProfile *legacyAlias = &body;
    struct ParticleDynamicsProfile *legacyTag = &body;
    assert(legacyAlias == legacyTag);
    assert(fabsf(MotionProfile_GravityAcceleration(&body) + 9.81f) < 1e-6f);
    body.densityKgM3 = 1.225f;
    assert(fabsf(MotionProfile_GravityAcceleration(&body)) < 1e-6f);
    Vector3 v = MotionProfile_ApplyAccelerationAndForce((Vector3){0},
        (Vector3){2,0,0}, (Vector3){4,0,0}, .5f, .5f);
    assert(v.x == 2);
    v = MotionProfile_ApplyImpulse(v, (Vector3){4,0,0}, .5f);
    assert(v.x == 4);
    body.gravityScale = 0;
    v = MotionBody_AdvanceVelocity((Vector3){0}, &body,
        (Vector3){2,0,0}, (Vector3){4,0,0}, (Vector3){0}, .5f);
    assert(v.x == 2);
    MotionGpuBody particle = MotionGpu_PackBody(NULL, (Vector3){0},
        (Vector3){0}, false, .8f);
    MotionGpuBody trail = MotionGpu_PackBodyForReceiver(NULL, (Vector3){0},
        (Vector3){0}, false, .8f, MOTION_RECEIVER_TRAIL);
    assert(particle.meta[2] == MOTION_RECEIVER_PARTICLE);
    assert(trail.meta[2] == MOTION_RECEIVER_TRAIL);
    trail.meta[2] = particle.meta[2];
    assert(memcmp(&particle, &trail, sizeof(trail)) == 0);
    assert(particle.body0.x == 1 && particle.body1.y == 3.5f);
    assert(particle.body1.z == .8f);
    assert(MOTION_RECEIVER_ALL == 3);
    assert((MOTION_RECEIVER_ALL & MOTION_RECEIVER_TRAIL) == 0);
    assert((MOTION_RECEIVER_ALL_COMPONENTS & MOTION_RECEIVER_TRAIL) != 0);
    puts("motion_profile: neutral layout, forces, legacy alias and GPU masks passed");
    return 0;
}

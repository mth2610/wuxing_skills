#ifndef CORE_PARTICLES_MOTION_CAPABILITIES_H
#define CORE_PARTICLES_MOTION_CAPABILITIES_H
#include "core/particles/particle_system.h"
/* Modern spatial fields have a compute mirror. Legacy captured routes and
 * CPU event producers keep their original solver until an event queue exists. */
static inline bool ParticleMotion_RequiresCpuDynamics(const ParticleConfig *particle)
{
    if(!ParticleDynamics_IsEnabled(particle->physics.dynamics) &&
        !particle->physics.receiveMotionFields && !particle->physics.spatialMotionOnly) return false;
    return !particle->physics.spatialMotionOnly || particle->physics.initialGuide ||
        particle->travelPath || particle->physics.followTarget ||
        particle->onTargetEmitCount>0 || particle->physics.onCollisionEmitCount>0 ||
        particle->onDeathEmitCount>0 || particle->onLiveEmitRate>0 ||
        particle->render.gradient || particle->render.shader.id || particle->animation.spriteAnim ||
        particle->animation.radiusCurve || particle->animation.speedCurve ||
        particle->animation.alphaCurve || particle->animation.emissiveCurve ||
        particle->radiusCurve || particle->speedCurve || particle->alphaCurve || particle->emissiveCurve ||
        particle->rotation!=0 || particle->angularVelocity!=0 ||
        particle->trailLength>0 || particle->render.trailLength>0 ||
        particle->meshModel.meshCount>0 || particle->render.meshModel.meshCount>0 ||
        particle->animation.spriteFlipX || particle->animation.spriteFlipY ||
        particle->render.blendMode==VFX_BLEND_PREMULTIPLIED;
}

#endif

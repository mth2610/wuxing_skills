#ifndef CORE_PARTICLES_MOTION_CAPABILITIES_H
#define CORE_PARTICLES_MOTION_CAPABILITIES_H
#include "core/particles/particle_system.h"
#include "core/emitter/emitter_gpu.h"
/* Modern spatial fields and emitter-owned visual leaf live/death births have
 * compute implementations. Legacy capture, collision/arrival emission and
 * recursive child configurations retain CPU simulation. */
static inline bool ParticleMotion_RequiresCpuDynamics(const ParticleConfig *particle)
{
    if(!EmissionGpu_ConfigSupported(particle)) return true;
    if(!ParticleDynamics_IsEnabled(particle->physics.dynamics) &&
        !particle->physics.receiveMotionFields && !particle->physics.spatialMotionOnly) return false;
    return !particle->physics.spatialMotionOnly || particle->physics.initialGuide ||
        particle->travelPath || particle->physics.followTarget ||
        particle->onTargetEmitCount>0 || particle->physics.onCollisionEmitCount>0 ||
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

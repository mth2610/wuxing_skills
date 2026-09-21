#ifndef CORE_PARTICLES_PARTICLE_DYNAMICS_H
#define CORE_PARTICLES_PARTICLE_DYNAMICS_H

/* Opt-in physical-motion data. A NULL profile preserves the legacy particle
 * integrator exactly; profiles are caller-owned immutable data and require no
 * allocation. Units are metres, seconds, kilograms, Newtons and N*s. */
#include "raylib.h"
#include <stdbool.h>
#include <math.h>
#include <stddef.h>

typedef struct ParticleDynamicsProfile {
    /* Positive kg^-1. It affects only force-in-Newtons and impulse inputs;
     * ForceField and WindZone remain acceleration fields independent of mass. */
    float inverseMassKg;
    float gravityScale;
    /* Simplified isotropic linear drag rate k in s^-1: v *= exp(-k * dt). */
    float linearDragPerSecond;
    float terminalSpeedMps;       /* <= 0 means no terminal-speed clamp. */
    float windAccelerationScale;  /* Multiplies WindZone acceleration. */
    float windCouplingHz;         /* Exponential relaxation rate to airflow. */
    float windSusceptibility;
    float steeringFrequencyHz;    /* Critically damped spline PD frequency. */
    float maxSteeringAccelMps2;   /* <= 0 disables the profile steering clamp. */
} ParticleDynamicsProfile;

static inline bool ParticleDynamics_IsEnabled(const ParticleDynamicsProfile *profile)
{
    return profile != NULL;
}

static inline Vector3 ParticleDynamics_ApplyImpulse(Vector3 velocity,
                                                      Vector3 impulseNs,
                                                      float inverseMassKg)
{
    return (Vector3){velocity.x + impulseNs.x * inverseMassKg,
                     velocity.y + impulseNs.y * inverseMassKg,
                     velocity.z + impulseNs.z * inverseMassKg};
}

static inline Vector3 ParticleDynamics_ApplyLinearDrag(Vector3 velocity,
                                                         float dragPerSecond,
                                                         float dt)
{
    float factor = (dragPerSecond > 0.0f && dt > 0.0f)
                       ? expf(-dragPerSecond * dt) : 1.0f;
    return (Vector3){velocity.x * factor, velocity.y * factor, velocity.z * factor};
}

#endif

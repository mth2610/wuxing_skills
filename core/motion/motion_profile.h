#ifndef CORE_MOTION_PROFILE_H
#define CORE_MOTION_PROFILE_H

/* Component-neutral physical-motion data. A NULL profile leaves motion
 * integration to the caller; profiles are caller-owned immutable data and require no
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
    /* Optional quadratic aerodynamic response relative to air; replaces
     * windCouplingHz when area/coefficient are positive. Units m^2, unitless,
     * kg/m^3. Zero area retains exponential artistic airflow relaxation. */
    float aerodynamicAreaM2;
    float aerodynamicDragCoefficient;
    float airDensityKgM3;
    /* Positive body density enables air buoyancy: displaced volume=m/rho.
     * Zero preserves legacy gravity without buoyancy. kg/m^3. */
    float densityKgM3;
} MotionBodyProfile;
/* Keep the historical typedef and struct tag source-compatible. No particle
 * storage, emission or rendering contract belongs in this header. */
typedef MotionBodyProfile ParticleDynamicsProfile;

/* Gravity and Archimedes buoyancy use the same ambient gravity. Density=0
 * retains the old gravity-only profile; air defaults to 1.225 kg/m^3. */
static inline float MotionProfile_GravityAcceleration(const MotionBodyProfile *body)
{
    float rhoAir = body->airDensityKgM3 > 0 ? body->airDensityKgM3 : 1.225f;
    float buoyancyRatio = body->densityKgM3 > 0 ? rhoAir / body->densityKgM3 : 0;
    return 9.81f * body->gravityScale * (buoyancyRatio - 1);
}

static inline bool MotionProfile_IsEnabled(const MotionBodyProfile *profile)
{
    return profile != NULL;
}

static inline Vector3 MotionProfile_ApplyImpulse(Vector3 velocity,
                                                      Vector3 impulseNs,
                                                      float inverseMassKg)
{
    return (Vector3){velocity.x + impulseNs.x * inverseMassKg,
                     velocity.y + impulseNs.y * inverseMassKg,
                     velocity.z + impulseNs.z * inverseMassKg};
}

/* Acceleration fields use m/s^2 directly. Only the force input is converted
 * from Newtons through inverse mass, so mass never changes ForceField/WindZone
 * response. */
static inline Vector3 MotionProfile_ApplyAccelerationAndForce(
    Vector3 velocity, Vector3 accelerationMps2, Vector3 forceNewtons,
    float inverseMassKg, float dt)
{
    Vector3 total = accelerationMps2;
    if (inverseMassKg > 0.0f) {
        total.x += forceNewtons.x * inverseMassKg;
        total.y += forceNewtons.y * inverseMassKg;
        total.z += forceNewtons.z * inverseMassKg;
    }
    return (Vector3){velocity.x + total.x * dt, velocity.y + total.y * dt,
                     velocity.z + total.z * dt};
}

static inline Vector3 MotionProfile_ApplyLinearDrag(Vector3 velocity,
                                                         float dragPerSecond,
                                                         float dt)
{
    float factor = (dragPerSecond > 0.0f && dt > 0.0f)
                       ? expf(-dragPerSecond * dt) : 1.0f;
    return (Vector3){velocity.x * factor, velocity.y * factor, velocity.z * factor};
}

static inline Vector3 MotionProfile_ClampTerminalSpeed(Vector3 velocity,
                                                            float terminalSpeedMps)
{
    float speed = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y + velocity.z*velocity.z);
    if (terminalSpeedMps > 0.0f && speed > terminalSpeedMps)
        return (Vector3){velocity.x * terminalSpeedMps / speed,
                         velocity.y * terminalSpeedMps / speed,
                         velocity.z * terminalSpeedMps / speed};
    return velocity;
}

static inline Vector3 MotionProfile_CoupleToAirflow(Vector3 velocity,
                                                         Vector3 airflowVelocity,
                                                         float couplingHz, float dt)
{
    float alpha = couplingHz > 0.0f && dt > 0.0f ? 1.0f - expf(-couplingHz * dt) : 0.0f;
    return (Vector3){velocity.x + (airflowVelocity.x - velocity.x) * alpha,
                     velocity.y + (airflowVelocity.y - velocity.y) * alpha,
                     velocity.z + (airflowVelocity.z - velocity.z) * alpha};
}

/* Legacy inline names remain visible to old transitive Motion includes. */
/* Gravity and Archimedes buoyancy use the same ambient gravity. Density=0
 * retains the old gravity-only profile; air defaults to 1.225 kg/m^3. */
static inline float ParticleDynamics_GravityAcceleration(const ParticleDynamicsProfile *body)
{
    return MotionProfile_GravityAcceleration(body);
}

static inline bool ParticleDynamics_IsEnabled(const ParticleDynamicsProfile *profile)
{
    return MotionProfile_IsEnabled(profile);
}

static inline Vector3 ParticleDynamics_ApplyImpulse(Vector3 velocity,
                                                      Vector3 impulseNs,
                                                      float inverseMassKg)
{
    return MotionProfile_ApplyImpulse(velocity, impulseNs, inverseMassKg);
}

/* Acceleration fields use m/s^2 directly. Only the force input is converted
 * from Newtons through inverse mass, so mass never changes ForceField/WindZone
 * response. */
static inline Vector3 ParticleDynamics_ApplyAccelerationAndForce(
    Vector3 velocity, Vector3 accelerationMps2, Vector3 forceNewtons,
    float inverseMassKg, float dt)
{
    return MotionProfile_ApplyAccelerationAndForce(velocity, accelerationMps2, forceNewtons, inverseMassKg, dt);
}

static inline Vector3 ParticleDynamics_ApplyLinearDrag(Vector3 velocity,
                                                         float dragPerSecond,
                                                         float dt)
{
    return MotionProfile_ApplyLinearDrag(velocity, dragPerSecond, dt);
}

static inline Vector3 ParticleDynamics_ClampTerminalSpeed(Vector3 velocity,
                                                            float terminalSpeedMps)
{
    return MotionProfile_ClampTerminalSpeed(velocity, terminalSpeedMps);
}

static inline Vector3 ParticleDynamics_CoupleToAirflow(Vector3 velocity,
                                                         Vector3 airflowVelocity,
                                                         float couplingHz, float dt)
{
    return MotionProfile_CoupleToAirflow(velocity, airflowVelocity, couplingHz, dt);
}

#endif

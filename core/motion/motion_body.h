#ifndef CORE_MOTION_BODY_H
#define CORE_MOTION_BODY_H
#include "core/motion/motion_path.h"
#include "core/particles/particle_dynamics.h"
/* Shared translational integrator; callers sample fields and resolve their
 * own geometry contacts. Acceleration fields never depend on mass. */
static inline Vector3 MotionBody_AdvanceVelocity(
    Vector3 velocity, const ParticleDynamicsProfile *body, Vector3 acceleration,
    Vector3 forceNewtons, Vector3 airflow, float dt) {
  if (!body || dt <= 0)
    return velocity;
  float inverseMass = body->inverseMassKg > 0 ? body->inverseMassKg : 1;
  acceleration.y += ParticleDynamics_GravityAcceleration(body);
  velocity = ParticleDynamics_ApplyAccelerationAndForce(
      velocity, acceleration, forceNewtons, inverseMass, dt);
  velocity =
      ParticleDynamics_ApplyLinearDrag(velocity, body->linearDragPerSecond, dt);
  if (body->aerodynamicAreaM2 > 0 && body->aerodynamicDragCoefficient > 0) {
    /* Backward-Euler solution of quadratic drag after the external-force kick.
     * Unlike an exact drag-only split, this preserves terminal equilibrium
     * when gravity/forces act continuously. It stays
     * dissipative at large dt instead of reversing a light leaf's velocity.
     * Geometry consumers can supply a projected area from their orientation.
     * Quadratic drag replaces linear airflow relaxation, never stacks it. */
    Vector3 relative = MotionVec_Sub(velocity, airflow);
    float rho = body->airDensityKgM3 > 0 ? body->airDensityKgM3 : 1.225f;
    float k = 0.5f * rho * body->aerodynamicDragCoefficient *
              body->aerodynamicAreaM2 * inverseMass *
              fmaxf(body->windSusceptibility, 0);
    velocity = MotionVec_Add(
        airflow,
        MotionVec_Scale(
            relative,
            2 / (1 + sqrtf(1 + 4 * k * MotionVec_Length(relative) * dt))));
  } else if (body->windCouplingHz > 0 && body->windSusceptibility > 0) {
    velocity = ParticleDynamics_CoupleToAirflow(
        velocity, airflow, body->windCouplingHz * body->windSusceptibility, dt);
  }
  return ParticleDynamics_ClampTerminalSpeed(velocity, body->terminalSpeedMps);
}
#endif

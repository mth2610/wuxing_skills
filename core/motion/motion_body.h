#ifndef CORE_MOTION_BODY_H
#define CORE_MOTION_BODY_H
#include "core/motion/motion_path.h"
#include "core/motion/physical_field.h"
#include "core/particles/particle_dynamics.h"
static inline BodyPhysicalProperties MotionBody_GetPhysicalProperties(const ParticleDynamicsProfile *p) {
  BodyPhysicalProperties b={0};
  if(!p) return b;
  b.massKg=p->inverseMassKg>0?1/p->inverseMassKg:1;
  b.densityKgM3=p->densityKgM3;
  b.volumeM3=b.densityKgM3>0?b.massKg/b.densityKgM3:0;
  b.projectedAreaM2=p->aerodynamicAreaM2;
  b.dragCoefficient=p->aerodynamicDragCoefficient; b.immersionFraction=1;
  return b;
}
static inline MediumProperties MotionBody_GetMediumProperties(const ParticleDynamicsProfile *p) {
  MediumProperties m={0};
  if(!p) return m;
  m.densityKgM3=p->airDensityKgM3>0?p->airDensityKgM3:1.225f;
  m.gravityMps2=(Vector3){0,-9.81f*p->gravityScale,0};
  return m;
}
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
/* Authored drag/buoyancy replace automatic material approximations so each
 * physical contribution is integrated exactly once. */
static inline Vector3 MotionBody_AdvanceFieldVelocity(Vector3 velocity,
    const ParticleDynamicsProfile *profile,Vector3 acceleration,Vector3 force,
    const FieldSample *sample,Vector3 ordinaryAir,float dt) {
  if(!profile || !sample) return velocity;
  ParticleDynamicsProfile body=*profile;
  if(sample->hasDragForce) { body.aerodynamicAreaM2=0; body.windCouplingHz=0; }
  if(sample->hasBuoyancyForce) body.densityKgM3=0;
  Vector3 air=sample->mediumIsAbsolute?sample->mediumVelocityMps:
    MotionVec_Add(ordinaryAir,sample->mediumVelocityMps);
  velocity=MotionBody_AdvanceVelocity(velocity,&body,
    MotionVec_Add(acceleration,sample->accelerationMps2),
    MotionVec_Add(force,MotionVec_Sub(sample->forceNewtons,sample->dragForceNewtons)),air,dt);
  if(sample->hasDragForce && dt>0 && sample->dragCoefficientKgPerM>0) {
    float inverseMass=body.inverseMassKg>0?body.inverseMassKg:1;
    Vector3 relative=MotionVec_Sub(velocity,sample->dragMediumVelocityMps);
    float k=sample->dragCoefficientKgPerM*inverseMass;
    velocity=MotionVec_Add(sample->dragMediumVelocityMps,
      MotionVec_Scale(relative,2/(1+sqrtf(1+4*k*MotionVec_Length(relative)*dt))));
  }
  return ParticleDynamics_ClampTerminalSpeed(velocity,body.terminalSpeedMps);
}
#endif

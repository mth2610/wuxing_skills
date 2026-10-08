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
static inline Vector3 MotionBody_ResponseMultiply(const FieldControllerResponse *r,Vector3 v) {
  return (Vector3){r->diagonal.x*v.x+r->offDiagonal.x*v.y+r->offDiagonal.y*v.z,
    r->offDiagonal.x*v.x+r->diagonal.y*v.y+r->offDiagonal.z*v.z,
    r->offDiagonal.y*v.x+r->offDiagonal.z*v.y+r->diagonal.z*v.z};
}
/* Solve a symmetric positive definite 3x3 system by LDL transpose. One solve
 * for unsaturated guidance; at most count+1 when force limits activate. */
static inline Vector3 MotionBody_SolveResponse(Vector3 diagonal,Vector3 off,Vector3 rhs) {
  float l10=off.x/diagonal.x,l20=off.y/diagonal.x;
  float d1=diagonal.y-l10*off.x;
  float l21=(off.z-l20*off.x)/d1;
  float d2=diagonal.z-l20*off.y-l21*l21*d1;
  float y1=rhs.y-l10*rhs.x,y2=rhs.z-l20*rhs.x-l21*y1;
  float z=y2/d2,y=y1/d1-l21*z;
  return (Vector3){rhs.x/diagonal.x-l10*y-l20*z,y,z};
}
static inline Vector3 MotionBody_AdvanceControllers(Vector3 velocity,Vector3 externalAcceleration,
    const FieldSample *sample,float inverseMass,float dt) {
  float scale=inverseMass*dt;
  /* Common moving-sphere case: one isotropic response needs three scalar
   * divisions, no active-set scratch or general matrix factorization. */
  if(sample->controllerCount==1) {
    const FieldControllerResponse *r=&sample->controllers[0];
    if(r->offDiagonal.x==0 && r->offDiagonal.y==0 && r->offDiagonal.z==0) {
      Vector3 rhs=MotionVec_Add(MotionVec_Scale(externalAcceleration,dt),
          MotionVec_Scale(r->driveNewtons,scale));
      Vector3 delta={rhs.x/(1+scale*r->diagonal.x),
        rhs.y/(1+scale*r->diagonal.y),rhs.z/(1+scale*r->diagonal.z)};
      Vector3 force=MotionVec_Sub(r->driveNewtons,MotionBody_ResponseMultiply(r,delta));
      if(MotionVec_Dot(force,force)>r->maxForceNewtons*r->maxForceNewtons)
        delta=MotionVec_Add(MotionVec_Scale(externalAcceleration,dt),
            MotionVec_Scale(Field_LimitVector(force,r->maxForceNewtons),scale));
      return MotionVec_Add(velocity,delta);
    }
  }
  bool limited[FIELD_MAX_CONTROLLER_RESPONSES]={0};
  Vector3 fixed[FIELD_MAX_CONTROLLER_RESPONSES]={{0}};
  Vector3 delta={0};
  for(int pass=0;pass<=sample->controllerCount;pass++) {
    Vector3 diagonal={1,1,1},off={0},rhs=MotionVec_Scale(externalAcceleration,dt);
    for(int i=0;i<sample->controllerCount;i++) {
      const FieldControllerResponse *r=&sample->controllers[i];
      rhs=MotionVec_Add(rhs,MotionVec_Scale(limited[i]?fixed[i]:r->driveNewtons,scale));
      if(!limited[i]) {
        diagonal=MotionVec_Add(diagonal,MotionVec_Scale(r->diagonal,scale));
        off=MotionVec_Add(off,MotionVec_Scale(r->offDiagonal,scale));
      }
    }
    delta=MotionBody_SolveResponse(diagonal,off,rhs);
    bool changed=false;
    for(int i=0;i<sample->controllerCount;i++) if(!limited[i]) {
      const FieldControllerResponse *r=&sample->controllers[i];
      Vector3 force=MotionVec_Sub(r->driveNewtons,MotionBody_ResponseMultiply(r,delta));
      if(MotionVec_Dot(force,force)>r->maxForceNewtons*r->maxForceNewtons) {
        fixed[i]=Field_LimitVector(force,r->maxForceNewtons);
        limited[i]=true;changed=true;
      }
    }
    if(!changed) break;
  }
  return MotionVec_Add(velocity,delta);
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
  if(sample->controllerCount>0 && dt>0) {
    float inverseMass=body.inverseMassKg>0?body.inverseMassKg:1;
    Vector3 externalForce=MotionVec_Add(force,
        MotionVec_Sub(sample->forceNewtons,sample->dragForceNewtons));
    for(int i=0;i<sample->controllerCount;i++)
      externalForce=MotionVec_Sub(externalForce,sample->controllers[i].sampledForceNewtons);
    Vector3 externalAcceleration=MotionVec_Add(MotionVec_Add(acceleration,
        sample->accelerationMps2),MotionVec_Scale(externalForce,inverseMass));
    externalAcceleration.y+=ParticleDynamics_GravityAcceleration(&body);
    velocity=MotionBody_AdvanceControllers(velocity,externalAcceleration,sample,inverseMass,dt);
    /* Reuse the existing material drag/terminal step without a second kick. */
    body.gravityScale=0;
    velocity=MotionBody_AdvanceVelocity(velocity,&body,(Vector3){0},(Vector3){0},air,dt);
  } else
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

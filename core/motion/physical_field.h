#ifndef CORE_PHYSICAL_FIELD_H
#define CORE_PHYSICAL_FIELD_H
#include <stddef.h>
#include "core/motion/motion_flow.h"
#define FIELD_MAX_FORCE_LAWS 8
/* All geometry is metres in a rigid local frame. Scaling is explicit in
 * geometry dimensions; transforms never scale positions or velocities. */
typedef enum { FIELD_SPHERE, FIELD_CAPSULE, FIELD_BOX, FIELD_PATH_TUBE } FieldShape;
typedef struct FieldVolume {
  FieldShape shape;
  float radiusM, coreFraction;
  Vector3 halfExtentsM, capsuleStart, capsuleEnd;
  MotionPath path;
} FieldVolume;
typedef struct FieldTransform {
  Vector3 position, axisX, axisY, axisZ;
  Vector3 frameVelocityMps, angularVelocityRadPerSec;
} FieldTransform;
typedef enum { FIELD_TRAJECTORY_STATIC, FIELD_TRAJECTORY_PATH } FieldTrajectoryMode;
typedef struct FieldTrajectory {
  FieldTrajectoryMode mode;
  MotionPath path;
  float speedMps;
} FieldTrajectory;
typedef struct FieldLifetime {
  float startDelaySec, durationSec, attackSec, fadeSec;
} FieldLifetime;
typedef struct BodyPhysicalProperties {
  float massKg, densityKgM3, volumeM3, projectedAreaM2, dragCoefficient;
  float immersionFraction;
} BodyPhysicalProperties;
typedef struct MediumProperties {
  float densityKgM3, dynamicViscosityPaS;
  Vector3 velocityMps, gravityMps2;
} MediumProperties;
typedef enum { RECEIVER_FREE, RECEIVER_ROOTED, RECEIVER_STATIC,
               RECEIVER_KINEMATIC, RECEIVER_TRACER } ReceiverMode;
typedef struct ReceiverConstraints {
  ReceiverMode mode;
  /* Root material springs are solved by the receiver, not the field. */
  Vector3 permittedAxes;
} ReceiverConstraints;
/* Signed radial magnitude: positive attracts, negative repels. No automatic
 * centrifugal force is introduced in this world-space inertial solver.
 * PATH_GUIDE applies a capped, critically damped force toward the nearest
 * point on a FIELD_PATH_TUBE centerline. */
typedef enum { FORCE_LAW_NEWTONS, FORCE_LAW_ACCELERATION,
               FORCE_LAW_RADIAL_ATTRACTION, FORCE_LAW_SPRING,
               FORCE_LAW_BUOYANCY, FORCE_LAW_DRAG, FORCE_LAW_PATH_GUIDE,
               FORCE_LAW_MOVING_GUIDE, FORCE_LAW_CURL_FORCE } ForceLawType;
typedef enum { GUIDE_MANUAL, GUIDE_LOOSE, GUIDE_BALANCED, GUIDE_TIGHT } GuidePreset;
/* Compiled actuator properties. Reference-body compilation is a project
 * convention, not a material constant. Actual receiver mass still controls
 * acceleration; these budgets never scale per receiver at sampling time. */
typedef struct GuideTuning {
  float maxForceNewtons, forwardForceNewtons, stiffnessNPerM, settlingTimeSec;
  float turbulenceForceNewtons, turbulenceSpeedMps, eddyLengthM;
} GuideTuning;
static inline GuideTuning GuideTuning_Derive(const BodyPhysicalProperties *body,
    float radiusM,float speedMps,float swirlMps,float turbulenceMps,
    float gravityMps2,GuidePreset preset) {
  GuideTuning out={0};
  if(!body || !isfinite(body->massKg) || body->massKg<=0 ||
      !isfinite(radiusM) || radiusM<=0 || !isfinite(speedMps) || speedMps<0 ||
      !isfinite(swirlMps) || !isfinite(turbulenceMps) || turbulenceMps<0 ||
      !isfinite(gravityMps2) || preset<GUIDE_LOOSE || preset>GUIDE_TIGHT) return out;
  float factor=preset==GUIDE_LOOSE?.75f:preset==GUIDE_TIGHT?1.5f:1;
  /* Reserve eight percent of support radius for gravity deflection, leaving
   * room for formation offsets and the field's smooth boundary falloff. Transit,
   * rotational timescales set a second lower response bound. Turbulence is
   * an independent disturbance: increasing it must not stiffen its opponent. */
  float omega=factor*fmaxf(sqrtf(fabsf(gravityMps2)/(radiusM*.08f)),
      2*fmaxf(speedMps,fabsf(swirlMps))/radiusM);
  omega=fmaxf(omega,.5f);
  out.stiffnessNPerM=body->massKg*omega*omega;
  out.maxForceNewtons=2*out.stiffnessNPerM*radiusM;
  out.forwardForceNewtons=body->massKg*omega*speedMps;
  out.settlingTimeSec=6/omega;
  /* Guided clouds need coherent integral-scale rolls, not many independent
   * small eddies across the formation. Derive the largest eddy from support. */
  out.eddyLengthM=radiusM;
  /* Requested noise uses the inertial scale m*U^2/L. Reserve 75% of force
   * authority for guidance (authoring convention). Saturated turnover follows
   * attainable speed, so increasing the request cannot hide noise by speeding
   * it up beyond the actuator's response. Never changes stiffness/damping. */
  out.turbulenceForceNewtons=fminf(body->massKg*turbulenceMps*turbulenceMps/out.eddyLengthM,
      out.maxForceNewtons*.25f);
  out.turbulenceSpeedMps=sqrtf(out.turbulenceForceNewtons*out.eddyLengthM/body->massKg);
  return out;
}
/* Controller coefficients belong to the actuator, never the body material.
 * Stiffness derives from the force budget and guide geometry. */
typedef struct GuideController {
  float maxForceNewtons, dampingRatio;
} GuideController;
typedef struct ForceLaw {
  ForceLawType type;
  Vector3 forceNewtons, accelerationMps2, center;
  float magnitudeNewtons, springStiffnessNPerM, dampingNsPerM;
  /* Independent cap for the PATH_GUIDE tangent velocity controller. */
  float forwardForceNewtons;
  /* CURL_FORCE: separate actuator noise, not airflow/drag. magnitudeNewtons
   * caps the force; procedural.turbulenceSpeedMps sets turnover and the curl
   * normalization. Zero speed disables it; swirl is unused. */
  MotionFlowDesc procedural;
} ForceLaw;
typedef struct FlowField {
  Vector3 velocityMps, axis;
  MotionFlowDesc procedural;
  int priority;
  /* Explicit compatible-media blend weight; higher priorities replace lower.
   * Zero means default weight 1, not a disabled field. Disable via amplitudes. */
  float blendWeight;
  bool enabled;
  /* Add ordinary medium velocity once, before priority blending. Default false
   * retains absolute authored flow. Used for ambient-Wind-aware moving VFX. */
  bool addBackgroundVelocity;
  /* Desired A-to-B speed for FIELD_PATH_TUBE, in m/s. Drives tangent airflow
   * and the optional PATH_GUIDE force controller. */
  float followSpeedMps;
} FlowField;
typedef struct FieldDesc {
  FieldVolume volume;
  FieldTransform transform;
  FieldTrajectory trajectory;
  FieldLifetime lifetime;
  ForceLaw forceLaws[FIELD_MAX_FORCE_LAWS];
  int forceLawCount;
  FlowField flow;
  unsigned int receiverMask;
  /* Path tubes can retain each receiver's captured cross-section lane; no
   * particle-particle forces or per-sample allocations are required. */
  bool preservePathLanes;
  /* Moving-guide spheres retain each receiver's entry offset. Swirl rotates
   * the force target (angular rate=swirlSpeedMps/radiusM), not its position.
   * Capture uses the same four bounded formation slots as path lanes. */
  bool preserveSphereOffsets;
  /* Opt-in rotating tube lanes; manual/legacy fixed lanes stay unchanged. */
  bool rotatePathLanes;
} FieldDesc;
typedef struct FieldSample {
  Vector3 forceNewtons, accelerationMps2, mediumVelocityMps;
  float mediumWeight;
  int mediumPriority;
  bool mediumIsAbsolute;
  bool hasDragForce, hasBuoyancyForce;
  /* Raw drag remains in forceNewtons for solver sampling. The body adapter
   * removes this component and uses its coefficient in an implicit drag step. */
  Vector3 dragForceNewtons, dragMediumVelocityMps;
  float dragCoefficientKgPerM;
} FieldSample;
/* Shared material approximation: equivalent fully immersed sphere; visual
 * particle size remains independent. Projected area matches sphere Cd. */
static inline BodyPhysicalProperties BodyPhysicalProperties_Sphere(float massKg,float densityKgM3,float dragCoefficient) {
  BodyPhysicalProperties b={.massKg=massKg,.densityKgM3=densityKgM3,
      .dragCoefficient=dragCoefficient,.immersionFraction=1};
  if(massKg>0 && densityKgM3>0) {
    b.volumeM3=massKg/densityKgM3;
    float radius=cbrtf(b.volumeM3*0.2387324146f);
    b.projectedAreaM2=3.1415926536f*radius*radius;
  }
  return b;
}
/* Thin lamina quantities use ONE-SIDED planform area. projectedAreaM2 is
 * broadside area paired with Cd; it is not total two-sided surface area.
 * Effective density is areal mass / thickness, including internal porosity.
 * Cd=1.28 is the low-speed broadside flat-plate approximation, not an
 * orientation-averaged flutter coefficient.
 * Species, hydration and orientation vary; presets below are representative
 * authored defaults, not universal measured material constants. */
typedef struct ThinLaminaMaterial {
  float arealMassKgM2, thicknessM, dragCoefficient;
} ThinLaminaMaterial;
typedef enum {
  BODY_LAMINA_LEAF_DRY,
  BODY_LAMINA_LEAF_FRESH,
  BODY_LAMINA_PETAL_FRESH
} BodyLaminaPreset;
static inline ThinLaminaMaterial BodyLaminaMaterial_Preset(BodyLaminaPreset preset) {
  switch(preset) {
    case BODY_LAMINA_LEAF_DRY: return (ThinLaminaMaterial){.060f,.00020f,1.28f};
    case BODY_LAMINA_LEAF_FRESH: return (ThinLaminaMaterial){.200f,.00020f,1.28f};
    /* Petal fresh mass is a representative project assumption, not a value
     * measured by the leaf/petal thickness references cited in API_GUIDE. */
    case BODY_LAMINA_PETAL_FRESH: return (ThinLaminaMaterial){.150f,.00020f,1.28f};
  }
  return (ThinLaminaMaterial){0};
}
static inline BodyPhysicalProperties BodyPhysicalProperties_Lamina(float areaM2,
    const ThinLaminaMaterial *material) {
  BodyPhysicalProperties b={0};
  if(!material || !isfinite(areaM2) || areaM2<=0 ||
      !isfinite(material->arealMassKgM2) || material->arealMassKgM2<=0 ||
      !isfinite(material->thicknessM) || material->thicknessM<=0 ||
      !isfinite(material->dragCoefficient) || material->dragCoefficient<0) return b;
  b.massKg=areaM2*material->arealMassKgM2;
  b.volumeM3=areaM2*material->thicknessM;
  b.densityKgM3=material->arealMassKgM2/material->thicknessM;
  b.projectedAreaM2=areaM2; b.dragCoefficient=material->dragCoefficient;
  b.immersionFraction=1;
  if(!isfinite(b.massKg) || !isfinite(b.volumeM3) || !isfinite(b.densityKgM3))
    return (BodyPhysicalProperties){0};
  return b;
}
/* Compatibility convenience: representative 120 cm^2 dry leaf. New consumers
 * should derive planform area from geometry and choose a hydration preset. */
static inline BodyPhysicalProperties BodyPhysicalProperties_Leaf(void) {
  ThinLaminaMaterial material=BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_DRY);
  return BodyPhysicalProperties_Lamina(.012f,&material);
}
static inline FieldTransform FieldTransform_Identity(void) {
  return (FieldTransform){.axisX={1,0,0},.axisY={0,1,0},.axisZ={0,0,1}};
}
static inline Vector3 FieldTransform_Vector(const FieldTransform *t, Vector3 v) {
  return MotionVec_Add(MotionVec_Scale(t->axisX,v.x),
      MotionVec_Add(MotionVec_Scale(t->axisY,v.y),MotionVec_Scale(t->axisZ,v.z)));
}
static inline Vector3 FieldTransform_LocalVector(const FieldTransform *t, Vector3 v) {
  return (Vector3){MotionVec_Dot(v,t->axisX),MotionVec_Dot(v,t->axisY),MotionVec_Dot(v,t->axisZ)};
}
static inline float FieldLifetime_Weight(const FieldLifetime *l,float age) {
  age-=l->startDelaySec;
  if (age<0 || age>=l->durationSec) return 0;
  float w=1;
  if(l->attackSec>0) w*=Motion_Clamp(age/l->attackSec,0,1);
  if(l->fadeSec>0) w*=Motion_Clamp((l->durationSec-age)/l->fadeSec,0,1);
  return w*w*(3-2*w);
}
static inline FieldTransform FieldTrajectory_Transform(const FieldDesc *d,float age) {
  FieldTransform t=d->transform;
  if(d->trajectory.mode==FIELD_TRAJECTORY_PATH) {
    MotionPathSample p=MotionPath_Sample(&d->trajectory.path,
      fmaxf(age-d->lifetime.startDelaySec,0)*d->trajectory.speedMps);
    t.position=MotionVec_Add(t.position,FieldTransform_Vector(&d->transform,p.position));
    if(p.distance<d->trajectory.path.length)
      t.frameVelocityMps=MotionVec_Add(t.frameVelocityMps,
        FieldTransform_Vector(&d->transform,MotionVec_Scale(p.tangent,d->trajectory.speedMps)));
  }
  return t;
}
static inline float FieldVolume_NormalizedDistanceAt(const FieldVolume *v,Vector3 p,
    const MotionPathSample *pathSample) {
  if(v->shape==FIELD_BOX) return fmaxf(fabsf(p.x)/v->halfExtentsM.x,
      fmaxf(fabsf(p.y)/v->halfExtentsM.y,fabsf(p.z)/v->halfExtentsM.z));
  if(v->shape==FIELD_CAPSULE)
    return Motion_SegmentDistance(v->capsuleStart,v->capsuleEnd,p)/v->radiusM;
  if(v->shape==FIELD_PATH_TUBE) {
    MotionPathSample q=pathSample ? *pathSample :
      MotionPath_Project(&v->path,p,0,v->path.count-2);
    return MotionVec_Length(MotionVec_Sub(p,q.position))/v->radiusM;
  }
  return MotionVec_Length(p)/v->radiusM;
}
static inline float FieldVolume_WeightAt(const FieldVolume *v,Vector3 p,
    const MotionPathSample *pathSample) {
  if(!v || v->shape<FIELD_SPHERE || v->shape>FIELD_PATH_TUBE ||
      !isfinite(v->coreFraction) || v->coreFraction<0 || v->coreFraction>=1 ||
      (v->shape==FIELD_BOX && (!isfinite(v->halfExtentsM.x) || !isfinite(v->halfExtentsM.y) || !isfinite(v->halfExtentsM.z))) ||
      !isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
      (v->shape==FIELD_BOX && (v->halfExtentsM.x<=0 || v->halfExtentsM.y<=0 || v->halfExtentsM.z<=0)) ||
      (v->shape!=FIELD_BOX && (!isfinite(v->radiusM) || v->radiusM<=0)) ||
      (v->shape==FIELD_PATH_TUBE && (v->path.count<2 || v->path.count>MOTION_PATH_MAX_POINTS))) return 0;
  float x=Motion_Clamp((FieldVolume_NormalizedDistanceAt(v,p,pathSample)-v->coreFraction)/
      fmaxf(1-v->coreFraction,0.0001f),0,1);
  return 1-x*x*(3-2*x);
}
static inline float FieldVolume_Weight(const FieldVolume *v,Vector3 p) {
  return FieldVolume_WeightAt(v,p,NULL);
}
static inline Vector3 ForceLaw_Evaluate(const ForceLaw *law,Vector3 p,Vector3 v,
    const BodyPhysicalProperties *body,const MediumProperties *medium,
    Vector3 flow) {
  switch(law->type) {
    case FORCE_LAW_NEWTONS: return law->forceNewtons;
    case FORCE_LAW_ACCELERATION: return law->accelerationMps2;
    case FORCE_LAW_RADIAL_ATTRACTION:
      return MotionVec_Scale(MotionVec_Normalize(MotionVec_Sub(law->center,p)),law->magnitudeNewtons);
    case FORCE_LAW_SPRING:
      return MotionVec_Sub(MotionVec_Scale(MotionVec_Sub(law->center,p),law->springStiffnessNPerM),
                           MotionVec_Scale(v,law->dampingNsPerM));
    case FORCE_LAW_BUOYANCY: {
      float volume=body->volumeM3>0?body->volumeM3:
        body->densityKgM3>0?body->massKg/body->densityKgM3:0;
      return MotionVec_Scale(medium->gravityMps2,
        -medium->densityKgM3*volume*Motion_Clamp(body->immersionFraction,0,1));
    }
    case FORCE_LAW_PATH_GUIDE:
    case FORCE_LAW_CURL_FORCE:
    case FORCE_LAW_MOVING_GUIDE: return (Vector3){0}; /* Contextual controllers below. */
    case FORCE_LAW_DRAG: {
      Vector3 relative=MotionVec_Sub(flow,v);
      return MotionVec_Scale(relative,0.5f*medium->densityKgM3*body->dragCoefficient*
          body->projectedAreaM2*MotionVec_Length(relative));
    }
  }
  return (Vector3){0};
}
/* Flow channels blend; force and acceleration channels accumulate. */
static inline void FieldSample_Combine(FieldSample *out,const FieldSample *in) {
  out->hasDragForce=out->hasDragForce || in->hasDragForce;
  out->hasBuoyancyForce=out->hasBuoyancyForce || in->hasBuoyancyForce;
  out->dragForceNewtons=MotionVec_Add(out->dragForceNewtons,in->dragForceNewtons);
  out->dragCoefficientKgPerM+=in->dragCoefficientKgPerM;
  if(in->hasDragForce) out->dragMediumVelocityMps=in->dragMediumVelocityMps;
  out->forceNewtons=MotionVec_Add(out->forceNewtons,in->forceNewtons);
  out->accelerationMps2=MotionVec_Add(out->accelerationMps2,in->accelerationMps2);
  if(in->mediumWeight<=0) return;
  if(out->mediumWeight<=0 || in->mediumPriority>out->mediumPriority) {
    out->mediumVelocityMps=in->mediumVelocityMps;
    out->mediumWeight=in->mediumWeight; out->mediumPriority=in->mediumPriority;
    out->mediumIsAbsolute=in->mediumIsAbsolute;
  } else if(in->mediumPriority==out->mediumPriority) {
    float sum=out->mediumWeight+in->mediumWeight;
    out->mediumVelocityMps=MotionVec_Scale(MotionVec_Add(
      MotionVec_Scale(out->mediumVelocityMps,out->mediumWeight),
      MotionVec_Scale(in->mediumVelocityMps,in->mediumWeight)),1/sum);
    out->mediumWeight=sum;
    out->mediumIsAbsolute=out->mediumIsAbsolute || in->mediumIsAbsolute;
  }
}
/* Generic procedural flow uses the existing decorrelated Perlin potential,
 * windowed by the actual support volume BEFORE taking curl. This preserves
 * the curl construction rather than multiplying completed turbulence velocity
 * by falloff. Box edges and path joints remain piecewise smooth; this is
 * procedural flow, not a pressure or incompressibility solver. */
static inline float FieldFlow_CharacteristicRadius(const FieldVolume *v) {
  return v->shape==FIELD_BOX?fminf(v->halfExtentsM.x,
    fminf(v->halfExtentsM.y,v->halfExtentsM.z)):v->radiusM;
}
static inline Vector3 FieldFlow_PotentialAt(const FlowField *flow,const FieldVolume *volume,
    Vector3 p,float time,const MotionPathSample *pathSample) {
  /* Curl samples are only a small eddy-length step from the receiver. Reuse
   * its nearest path point to avoid six full path projections per sample. */
  float weight=FieldVolume_WeightAt(volume,p,pathSample);
  if(weight<=0 || flow->procedural.turbulenceSpeedMps==0) return (Vector3){0};
  float eddy=flow->procedural.eddyLengthM>0?flow->procedural.eddyLengthM:
    FieldFlow_CharacteristicRadius(volume)*.25f;
  if(eddy<=0 || !isfinite(eddy)) return (Vector3){0};
  float phase=time*flow->procedural.turbulenceSpeedMps/eddy;
  Vector3 q=MotionVec_Add(MotionVec_Scale(p,1/eddy),
      (Vector3){phase*.37f,phase*.51f,phase*.29f});
  Vector3 noise={Noise_Perlin3D(q.x,q.y,q.z),
    Noise_Perlin3D(q.x+31.416f,q.y+31.416f,q.z+31.416f),
    Noise_Perlin3D(q.x+67.234f,q.y+67.234f,q.z+67.234f)};
  return MotionVec_Scale(noise,flow->procedural.turbulenceSpeedMps*eddy*weight);
}
static inline Vector3 FieldFlow_Potential(const FlowField *flow,const FieldVolume *volume,
    Vector3 p,float time) {
  return FieldFlow_PotentialAt(flow,volume,p,time,NULL);
}
static inline Vector3 FieldFlow_EvaluateAt(const FlowField *flow,const FieldVolume *volume,
    Vector3 p,float time,const MotionPathSample *pathSample) {
  Vector3 out={0};
  float weight=FieldVolume_WeightAt(volume,p,pathSample);
  if(weight<=0 || !MotionFlow_IsValid(&flow->procedural)) return out;
  float radius=FieldFlow_CharacteristicRadius(volume);
  Vector3 center={0},axis=MotionVec_Normalize(flow->axis);
  if(MotionVec_Length(axis)<.1f) axis=(Vector3){0,1,0};
  if(volume->shape==FIELD_PATH_TUBE) {
    MotionPathSample q=pathSample ? *pathSample :
      MotionPath_Project(&volume->path,p,0,volume->path.count-2);
    center=q.position; axis=q.tangent;
  } else if(volume->shape==FIELD_CAPSULE) {
    Vector3 delta=MotionVec_Sub(volume->capsuleEnd,volume->capsuleStart);
    float square=MotionVec_Dot(delta,delta);
    float along=square>1e-12f?Motion_Clamp(MotionVec_Dot(MotionVec_Sub(p,volume->capsuleStart),delta)/square,0,1):0;
    center=MotionVec_Add(volume->capsuleStart,MotionVec_Scale(delta,along));
    if(square>1e-12f) axis=MotionVec_Normalize(delta);
  }
  if (volume->shape == FIELD_PATH_TUBE && flow->followSpeedMps != 0.0f) {
    MotionPathSample nearest = pathSample ? *pathSample :
      MotionPath_Project(&volume->path, p, 0, volume->path.count - 2);
    out = MotionVec_Scale(nearest.tangent, flow->followSpeedMps * weight);
  }
  if(flow->procedural.swirlSpeedMps!=0 && radius>0) {
    Vector3 radial=MotionFlow_Radial(p,center,axis);
    out=MotionVec_Add(out,MotionVec_Scale(MotionVec_Cross(axis,radial),
      flow->procedural.swirlSpeedMps/radius*weight));
  }
  if(flow->procedural.turbulenceSpeedMps>0) {
    float eddy=flow->procedural.eddyLengthM>0?flow->procedural.eddyLengthM:radius*.25f;
    float h=eddy*.02f;
    if(h<=0 || !isfinite(h)) return out;
    Vector3 dx={h,0,0},dy={0,h,0},dz={0,0,h};
    Vector3 ax=MotionVec_Sub(FieldFlow_PotentialAt(flow,volume,MotionVec_Add(p,dx),time,pathSample),
                            FieldFlow_PotentialAt(flow,volume,MotionVec_Sub(p,dx),time,pathSample));
    Vector3 ay=MotionVec_Sub(FieldFlow_PotentialAt(flow,volume,MotionVec_Add(p,dy),time,pathSample),
                            FieldFlow_PotentialAt(flow,volume,MotionVec_Sub(p,dy),time,pathSample));
    Vector3 az=MotionVec_Sub(FieldFlow_PotentialAt(flow,volume,MotionVec_Add(p,dz),time,pathSample),
                            FieldFlow_PotentialAt(flow,volume,MotionVec_Sub(p,dz),time,pathSample));
    out=MotionVec_Add(out,MotionVec_Scale((Vector3){ay.z-az.y,az.x-ax.z,ax.y-ay.x},1/(2*h)));
  }
  return out;
}
static inline Vector3 FieldFlow_Evaluate(const FlowField *flow,const FieldVolume *volume,
    Vector3 p,float time) {
  return FieldFlow_EvaluateAt(flow,volume,p,time,NULL);
}
/* Raw field evaluation has no capture, emission, contact or arrival effects.
 * Spatial falloff applies once per contribution; lifetime scales completed
 * curl velocity uniformly in space, preserving its construction. */
static inline bool Field_FiniteVector(Vector3 p) {
  return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}
static inline Vector3 Field_LimitVector(Vector3 value, float maximum) {
  float length = MotionVec_Length(value);
  return length > maximum && length > 1e-8f
      ? MotionVec_Scale(value, maximum / length) : value;
}
/* Backward-Euler spring/damper force. dt=0 is the continuous force query.
 * Spatial/lifetime weight scales stiffness; critical damping is derived from
 * that effective stiffness before the solve. */
static inline Vector3 Guide_ImplicitForce(Vector3 error,Vector3 relativeVelocity,
    float stiffness,float mass,float weight,float dt) {
  stiffness*=weight;
  float damping=2*sqrtf(stiffness*mass);
  float denominator=1+(damping+stiffness*dt)*dt/mass;
  return MotionVec_Scale(MotionVec_Sub(MotionVec_Scale(error,stiffness),
      MotionVec_Scale(relativeVelocity,damping+stiffness*dt)),1/denominator);
}
static inline Vector3 FieldGuide_RotateOffset(const FieldDesc *d,Vector3 offset,float age) {
  if(!d->flow.enabled || d->flow.procedural.swirlSpeedMps==0) return offset;
  Vector3 axis=MotionVec_Normalize(d->flow.axis);
  if(MotionVec_Length(axis)<.1f) axis=(Vector3){0,1,0};
  float angle=d->flow.procedural.swirlSpeedMps/d->volume.radiusM*age;
  float c=cosf(angle),s=sinf(angle);
  return MotionVec_Add(MotionVec_Scale(offset,c),MotionVec_Add(
      MotionVec_Scale(MotionVec_Cross(axis,offset),s),
      MotionVec_Scale(axis,MotionVec_Dot(axis,offset)*(1-c))));
}
static inline Vector3 FieldGuide_RotatePathLane(const FieldDesc *d,Vector3 lane,float age) {
  if(!d->rotatePathLanes || !d->flow.enabled) return lane;
  float angle=d->flow.procedural.swirlSpeedMps/d->volume.radiusM*age;
  float c=cosf(angle),s=sinf(angle);
  return (Vector3){lane.x,lane.y*c-lane.z*s,lane.y*s+lane.z*c};
}
static inline FieldSample Field_EvaluateStepPass(const FieldDesc *d,float age,
    Vector3 position,Vector3 velocity,const BodyPhysicalProperties *body,
    const MediumProperties *medium,bool includeFlow,bool filterDrag,bool dragPass,
    const Vector3 *pathLaneOffset,float dt) {
  FieldSample out={0};
  if(!d || !body || !medium || !isfinite(dt) || dt<0 || d->forceLawCount<0 || d->forceLawCount>FIELD_MAX_FORCE_LAWS ||
      !isfinite(age) || !isfinite(body->massKg) || body->massKg<0 ||
      !isfinite(body->projectedAreaM2) || body->projectedAreaM2<0 ||
      !isfinite(body->densityKgM3) || body->densityKgM3<0 ||
      !isfinite(body->volumeM3) || body->volumeM3<0 ||
      !isfinite(body->dragCoefficient) || body->dragCoefficient<0 ||
      !isfinite(body->immersionFraction) || body->immersionFraction<0 || body->immersionFraction>1 ||
      !isfinite(medium->densityKgM3) || medium->densityKgM3<0 ||
      !Field_FiniteVector(position) || !Field_FiniteVector(velocity) ||
      !Field_FiniteVector(medium->velocityMps) || !Field_FiniteVector(medium->gravityMps2)) return out;
  FieldTransform t=FieldTrajectory_Transform(d,age);
  Vector3 offset=MotionVec_Sub(position,t.position);
  Vector3 p=FieldTransform_LocalVector(&t,offset);
  float lifetime=FieldLifetime_Weight(&d->lifetime,age);
  MotionPathSample nearestPath;
  const MotionPathSample *pathSample=NULL;
  if(d->volume.shape==FIELD_PATH_TUBE) {
    nearestPath=MotionPath_Project(&d->volume.path,p,0,d->volume.path.count-2);
    pathSample=&nearestPath;
  }
  float w=FieldVolume_WeightAt(&d->volume,p,pathSample)*lifetime;
  if(w<=0) return out;
  Vector3 flow=medium->velocityMps;
  if(includeFlow && d->flow.enabled) {
    Vector3 local=MotionVec_Add(MotionVec_Scale(d->flow.velocityMps,w),
      MotionVec_Scale(FieldFlow_EvaluateAt(&d->flow,&d->volume,p,age,pathSample),lifetime));
    flow=MotionVec_Add(MotionVec_Scale(MotionVec_Add(t.frameVelocityMps,
        MotionVec_Cross(t.angularVelocityRadPerSec,offset)),w),FieldTransform_Vector(&t,local));
    if(d->flow.addBackgroundVelocity) flow=MotionVec_Add(flow,medium->velocityMps);
    out.mediumVelocityMps=flow;
    out.mediumWeight=w*(d->flow.blendWeight>0?d->flow.blendWeight:1);
    out.mediumPriority=d->flow.priority; out.mediumIsAbsolute=true;
  }
  for(int i=0;i<d->forceLawCount;++i) {
    ForceLaw law=d->forceLaws[i];
    if(filterDrag && ((law.type==FORCE_LAW_DRAG)!=dragPass)) continue;
    if(!Field_FiniteVector(law.forceNewtons) || !Field_FiniteVector(law.accelerationMps2) ||
       !Field_FiniteVector(law.center) || !isfinite(law.magnitudeNewtons) ||
       !isfinite(law.springStiffnessNPerM) || !isfinite(law.dampingNsPerM) ||
       !isfinite(law.forwardForceNewtons) || law.forwardForceNewtons<0) return (FieldSample){0};
    if(law.type==FORCE_LAW_DRAG) out.hasDragForce=true;
    if(law.type==FORCE_LAW_BUOYANCY) out.hasBuoyancyForce=true;
    /* Authored forces/centers are local; gravity and medium are inertial. */
    law.forceNewtons=FieldTransform_Vector(&t,law.forceNewtons);
    law.accelerationMps2=FieldTransform_Vector(&t,law.accelerationMps2);
    law.center=MotionVec_Add(t.position,FieldTransform_Vector(&t,law.center));
    Vector3 f;
    if (law.type == FORCE_LAW_CURL_FORCE) {
      if(!MotionFlow_IsValid(&law.procedural) || law.magnitudeNewtons<0)
        return (FieldSample){0};
      float speed=law.procedural.turbulenceSpeedMps;
      FlowField noise={0};noise.procedural=law.procedural;
      noise.procedural.swirlSpeedMps=0;
      Vector3 curl=speed>0 && law.magnitudeNewtons>0?FieldFlow_EvaluateAt(&noise,&d->volume,p,age,pathSample):(Vector3){0};
      /* Force authority fades with support like the guide, preventing the
       * derivative of the windowed potential from dominating at the boundary.
       * This is a bounded actuator force, not an incompressible airflow claim. */
      Vector3 raw=MotionVec_Scale(curl,speed>0?law.magnitudeNewtons*lifetime/speed:0);
      f=FieldTransform_Vector(&t,Field_LimitVector(raw,law.magnitudeNewtons*w));
    } else if (law.type == FORCE_LAW_PATH_GUIDE && d->volume.shape == FIELD_PATH_TUBE) {
      MotionPathSample nearest = *pathSample;
      Vector3 tangent = FieldTransform_Vector(&t, nearest.tangent);
      Vector3 pathGoal=nearest.position;
      Vector3 rotatedLane={0};
      if(pathLaneOffset)
      {
        rotatedLane=FieldGuide_RotatePathLane(d,*pathLaneOffset,age);
        pathGoal=MotionVec_Add(pathGoal,MotionPath_WorldOffset(nearest,rotatedLane));
      }
      Vector3 toPath = MotionVec_Sub(
          MotionVec_Add(t.position, FieldTransform_Vector(&t, pathGoal)), position);
      Vector3 normalOffset = MotionVec_Sub(toPath,
          MotionVec_Scale(tangent, MotionVec_Dot(toPath, tangent)));
      Vector3 relativeVelocity = MotionVec_Sub(velocity,
          MotionVec_Add(t.frameVelocityMps, MotionVec_Cross(t.angularVelocityRadPerSec, offset)));
      float tangentSpeed = MotionVec_Dot(relativeVelocity, tangent);
      relativeVelocity = MotionVec_Sub(relativeVelocity,
          MotionVec_Scale(tangent, tangentSpeed));
      if(pathLaneOffset && d->rotatePathLanes && d->flow.enabled) {
        Vector3 laneWorld=FieldTransform_Vector(&t,MotionPath_WorldOffset(nearest,rotatedLane));
        relativeVelocity=MotionVec_Sub(relativeVelocity,
            MotionVec_Scale(MotionVec_Cross(tangent,laneWorld),
                d->flow.procedural.swirlSpeedMps/d->volume.radiusM));
      }
      float stiffness = law.springStiffnessNPerM > 0
          ? law.springStiffnessNPerM : law.magnitudeNewtons / d->volume.radiusM;
      float mass = body->massKg > 0 ? body->massKg : 1.0f;
      Vector3 dampingVelocity=relativeVelocity;
      if(!d->rotatePathLanes && d->flow.enabled && d->flow.procedural.swirlSpeedMps!=0) {
        /* A path guide must damp motion toward/away from its centreline without
         * cancelling the circumferential velocity that forms the authored swirl. */
        Vector3 swirlRadial=MotionVec_Sub(p,nearest.position);
        swirlRadial=MotionVec_Sub(swirlRadial,
            MotionVec_Scale(nearest.tangent,MotionVec_Dot(swirlRadial,nearest.tangent)));
        if(MotionVec_Length(swirlRadial)<1e-5f && pathLaneOffset)
          swirlRadial=MotionPath_WorldOffset(nearest,*pathLaneOffset);
        float radialSq=MotionVec_Dot(swirlRadial,swirlRadial);
        if(radialSq>1e-10f) {
          Vector3 radial=MotionVec_Scale(FieldTransform_Vector(&t,swirlRadial),1/sqrtf(radialSq));
          dampingVelocity=MotionVec_Scale(radial,MotionVec_Dot(relativeVelocity,radial));
        }
      }
      Vector3 lateralForce = Guide_ImplicitForce(normalOffset,dampingVelocity,
          stiffness,mass,w,dt);
      Vector3 forwardForce = {0};
      float targetSpeed = d->flow.followSpeedMps;
      if (law.forwardForceNewtons > 0 && fabsf(targetSpeed) > 1e-5f) {
        /* A bounded target-velocity servo pushes toward B, then brakes if a
         * particle exceeds the authored speed. It does not depend on drag. */
        float gain = law.forwardForceNewtons / fabsf(targetSpeed);
        float force = Motion_Clamp((targetSpeed - tangentSpeed) * gain /
                                   (1+gain*w*dt/mass),
                                   -law.forwardForceNewtons,
                                   law.forwardForceNewtons);
        forwardForce = MotionVec_Scale(tangent, force);
      }
      f = Field_LimitVector(lateralForce, law.magnitudeNewtons*w);
      f = MotionVec_Add(f, MotionVec_Scale(forwardForce, w));
    } else if(law.type==FORCE_LAW_MOVING_GUIDE) {
      float mass=body->massKg>0?body->massKg:1;
      float stiffness=law.springStiffnessNPerM>0?law.springStiffnessNPerM:
          law.magnitudeNewtons/d->volume.radiusM;
      Vector3 goal=law.center;
      Vector3 goalVelocity=MotionVec_Add(t.frameVelocityMps,
          MotionVec_Cross(t.angularVelocityRadPerSec,offset));
      if(pathLaneOffset) {
        Vector3 lane=FieldGuide_RotateOffset(d,*pathLaneOffset,age);
        goal=MotionVec_Add(goal,FieldTransform_Vector(&t,lane));
        if(d->flow.enabled && d->flow.procedural.swirlSpeedMps!=0) {
          Vector3 axis=MotionVec_Normalize(d->flow.axis);
          if(MotionVec_Length(axis)<.1f) axis=(Vector3){0,1,0};
          Vector3 spin=MotionVec_Scale(MotionVec_Cross(axis,lane),
              d->flow.procedural.swirlSpeedMps/d->volume.radiusM);
          goalVelocity=MotionVec_Add(goalVelocity,FieldTransform_Vector(&t,spin));
        }
      }
      f=Field_LimitVector(Guide_ImplicitForce(MotionVec_Sub(goal,position),
          MotionVec_Sub(velocity,goalVelocity),stiffness,mass,w,dt),
          law.magnitudeNewtons*w);
    } else {
      f = MotionVec_Scale(ForceLaw_Evaluate(&law, position,
          law.type == FORCE_LAW_SPRING
              ? MotionVec_Sub(velocity, MotionVec_Add(t.frameVelocityMps,
                  MotionVec_Cross(t.angularVelocityRadPerSec, offset)))
              : velocity, body, medium, flow), w);
    }
    if(law.type==FORCE_LAW_DRAG) {
      out.dragForceNewtons=MotionVec_Add(out.dragForceNewtons,f);
      out.dragMediumVelocityMps=flow;
      out.dragCoefficientKgPerM+=w*0.5f*medium->densityKgM3*body->dragCoefficient*body->projectedAreaM2;
    }
    if(law.type==FORCE_LAW_ACCELERATION) out.accelerationMps2=MotionVec_Add(out.accelerationMps2,f);
    else out.forceNewtons=MotionVec_Add(out.forceNewtons,f);
  }
  return out;
}
static inline FieldSample Field_EvaluatePass(const FieldDesc *d,float age,
    Vector3 position,Vector3 velocity,const BodyPhysicalProperties *body,
    const MediumProperties *medium,bool includeFlow,bool filterDrag,bool dragPass,
    const Vector3 *pathLaneOffset) {
  return Field_EvaluateStepPass(d,age,position,velocity,body,medium,includeFlow,
      filterDrag,dragPass,pathLaneOffset,0);
}
static inline FieldSample Field_EvaluateStep(const FieldDesc *d,float age,
    Vector3 position,Vector3 velocity,const BodyPhysicalProperties *body,
    const MediumProperties *medium,float dt) {
  return Field_EvaluateStepPass(d,age,position,velocity,body,medium,true,false,false,NULL,dt);
}
static inline FieldSample Field_Evaluate(const FieldDesc *d,float age,
    Vector3 position,Vector3 velocity,const BodyPhysicalProperties *body,
    const MediumProperties *medium) {
  return Field_EvaluatePass(d,age,position,velocity,body,medium,true,false,false,NULL);
}
#endif

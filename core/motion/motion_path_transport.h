#ifndef CORE_MOTION_PATH_TRANSPORT_H
#define CORE_MOTION_PATH_TRANSPORT_H
#include "core/motion/physical_field.h"
#include <stdint.h>
typedef uint32_t MotionFieldHandle;
/* Prescribed transport is independent of force integration. Field zero disables
 * transport; distance = startDistanceM + speedMps * component age. */
typedef struct MotionPathTransport {
  MotionFieldHandle field;
  float speedMps, startDistanceM;
  Vector3 laneOffset;
  bool respondToField; /* Bounded transverse response; zero keeps prescribed transport. */
  bool captureBirthLane; /* Capture transverse birth offset once; never recapture. */
} MotionPathTransport;
typedef struct MotionPathTransportSnapshot {
  const MotionPath *path;
  FieldTransform transform;
  const FieldDesc *field; /* Borrowed selected route field; NULL for pure sampling. */
  float ageSec;
} MotionPathTransportSnapshot;
static inline Vector3 MotionPathTransport_Derivative(const MotionPath *p,int knot) {
  int a=knot>0?knot-1:knot,b=knot<p->count-1?knot+1:knot;
  return MotionVec_Scale(MotionVec_Sub(p->points[b],p->points[a]),
      1.0f/(p->distance[b]-p->distance[a]));
}
static inline Vector3 MotionPathTransport_CaptureLane(
    const MotionPathTransportSnapshot *view,Vector3 worldBirth) {
  if(!view || !view->path || view->path->count<2) return (Vector3){0};
  const MotionPath *p=view->path;
  Vector3 t=MotionVec_Normalize(MotionPathTransport_Derivative(p,0));
  Vector3 n=MotionPath_TransportNormal(p->normals[0],t,t);
  Vector3 local=FieldTransform_LocalVector(&view->transform,
      MotionVec_Sub(worldBirth,view->transform.position));
  Vector3 offset=MotionVec_Sub(local,p->points[0]);
  return (Vector3){0,MotionVec_Dot(offset,n),MotionVec_Dot(offset,MotionVec_Cross(t,n))};
}
/* C1 Hermite interpolation in the authored polyline arc-distance coordinate,
 * not an exact reparameterization of the smoothed curve. Binary lookup O(logN).
 * Lane frame interpolates knot derivatives/normals with zero endpoint slope,
 * preserving C1 offset lanes as well as the centreline. Negative/past-end
 * distances clamp, permitting a ribbon to grow and drain without extrapolation.
 * Invalid snapshots/nonfinite arguments return zero. No allocation/state. */
static inline MotionPathSample MotionPathTransport_Frame(
    const MotionPathTransportSnapshot *view,float distance) {
  const MotionPath *p=view?view->path:NULL;
  if(!p || p->count<2 || p->count>MOTION_PATH_MAX_POINTS || !isfinite(distance))
    return (MotionPathSample){0};
  float d=Motion_Clamp(distance,0,p->length);
  int lo=0,hi=p->count-1;
  while(hi-lo>1) {int mid=(lo+hi)/2;if(p->distance[mid]<=d) lo=mid;else hi=mid;}
  if(lo==p->count-1) lo--;
  int b=lo+1;
  float span=p->distance[b]-p->distance[lo],t=(d-p->distance[lo])/span;
  float t2=t*t,t3=t2*t;
  Vector3 aSlope=MotionPathTransport_Derivative(p,lo),bSlope=MotionPathTransport_Derivative(p,b);
  Vector3 position=MotionVec_Add(
      MotionVec_Add(MotionVec_Scale(p->points[lo],2*t3-3*t2+1),MotionVec_Scale(p->points[b],-2*t3+3*t2)),
      MotionVec_Add(MotionVec_Scale(aSlope,span*(t3-2*t2+t)),MotionVec_Scale(bSlope,span*(t3-t2))));
  float blend=t2*(3-2*t);
  Vector3 tangent=MotionVec_Normalize(MotionVec_Add(MotionVec_Scale(aSlope,1-blend),MotionVec_Scale(bSlope,blend)));
  Vector3 normal=MotionVec_Add(MotionVec_Scale(p->normals[lo],1-blend),MotionVec_Scale(p->normals[b],blend));
  normal=MotionPath_TransportNormal(normal,tangent,tangent);
  return (MotionPathSample){.position=position,.tangent=tangent,.normal=normal,
    .binormal=MotionVec_Cross(tangent,normal),.distance=d,.segment=lo};
}
static inline Vector3 MotionPathTransport_ToWorld(const MotionPathTransportSnapshot *view,
    MotionPathSample q,Vector3 lane) {
  return MotionVec_Add(view->transform.position,FieldTransform_Vector(&view->transform,
    MotionVec_Add(q.position,MotionPath_WorldOffset(q,lane))));
}
static inline Vector3 MotionPathTransport_Sample(
    const MotionPathTransportSnapshot *view,float distance,Vector3 lane) {
  if(!view || !view->path || view->path->count<2 || view->path->count>MOTION_PATH_MAX_POINTS || !isfinite(distance) || !Field_FiniteVector(lane))
    return (Vector3){0};
  return MotionPathTransport_ToWorld(view,MotionPathTransport_Frame(view,distance),lane);
}
/* State lives in the normal/binormal plane, not world space. Forward progress is
 * supplied by the caller and cannot be altered by field forces. No heap, path
 * projection, cloth constraints or renderer-specific types. */
typedef struct MotionPathTransportState {Vector3 offsetM,velocityMps;} MotionPathTransportState;
static inline Vector3 MotionPathTransport_Advance(const MotionPathTransportSnapshot *view,
    float distance,Vector3 birthLane,float lagSec,float dt,
    const BodyPhysicalProperties *body,float airDensity,Vector3 externalAccelerationMps2,MotionPathTransportState *state) {
  if(!view || !view->field || !view->path || !state || !body ||
     !isfinite(dt) || dt<=0 || !isfinite(distance) || !isfinite(lagSec) ||
     !Field_FiniteVector(birthLane) || body->massKg<=0 || !isfinite(body->massKg))
    return MotionPathTransport_Sample(view,distance,birthLane);
  const FieldDesc *f=view->field;
  MotionPathSample q=MotionPathTransport_Frame(view,distance);
  if(distance<=0 || distance>=view->path->length) {
    *state=(MotionPathTransportState){0};
    return MotionPathTransport_ToWorld(view,q,(Vector3){0});
  }
  float radius=f->volume.radiusM;
  if(radius<=0) return MotionPathTransport_ToWorld(view,q,(Vector3){0});
  float angle=f->flow.enabled?f->flow.procedural.swirlSpeedMps/radius*(view->ageSec-lagSec):0;
  float c=cosf(angle),sn=sinf(angle);
  Vector3 base={0,birthLane.y*c-birthLane.z*sn,birthLane.y*sn+birthLane.z*c};
  base=Field_LimitVector(base,radius);
  float room=fmaxf(0,radius-MotionVec_Length(base));
  Vector3 lane=MotionVec_Add(base,state->offsetM);
  Vector3 local=MotionVec_Add(q.position,MotionPath_WorldOffset(q,lane));
  Vector3 velocity=MotionPath_WorldOffset(q,state->velocityMps);
  float life=FieldLifetime_Weight(&f->lifetime,view->ageSec);
  float w=FieldVolume_WeightAt(&f->volume,local,&q)*life;
  Vector3 force={0},acc={0},air={0};
  float stiffness=0;
  MediumProperties medium={.densityKgM3=airDensity>0?airDensity:1.225f,
      .gravityMps2=FieldTransform_LocalVector(&view->transform,externalAccelerationMps2)};
  FlowField flow=f->flow;flow.procedural.swirlSpeedMps=0;flow.followSpeedMps=0;
  if(flow.enabled) air=MotionVec_Add(MotionVec_Scale(flow.velocityMps,w),
      MotionVec_Scale(FieldFlow_EvaluateAt(&flow,&f->volume,local,view->ageSec,&q),life));
  for(int j=0;j<f->forceLawCount;j++) {
    ForceLaw law=f->forceLaws[j];
    if(law.type==FORCE_LAW_PATH_GUIDE) {
      stiffness=fmaxf(stiffness,law.springStiffnessNPerM>0?law.springStiffnessNPerM:law.magnitudeNewtons/radius);
    } else if(law.type==FORCE_LAW_CURL_FORCE) {
      float speed=law.procedural.turbulenceSpeedMps;
      if(speed>0 && law.magnitudeNewtons>0 && w>0) {
        FlowField noise={.procedural=law.procedural};
        Vector3 curl=FieldFlow_EvaluateAt(&noise,&f->volume,local,view->ageSec,&q);
        force=MotionVec_Add(force,Field_LimitVector(MotionVec_Scale(curl,
          law.magnitudeNewtons*life/speed),law.magnitudeNewtons*w));
      }
    } else if(law.type==FORCE_LAW_ACCELERATION)
      acc=MotionVec_Add(acc,MotionVec_Scale(law.accelerationMps2,w));
    else if(law.type!=FORCE_LAW_DRAG && law.type!=FORCE_LAW_MOVING_GUIDE)
      force=MotionVec_Add(force,MotionVec_Scale(ForceLaw_Evaluate(&law,local,velocity,body,&medium,air),w));
  }
  /* Medium turbulence remains m/s; the receiver's aerodynamic area/Cd turn it
   * into Newtons. Automatic guidance curl is already a Newton actuator above. */
  Vector3 relative=MotionVec_Sub(air,velocity);
  float drag=.5f*medium.densityKgM3*body->projectedAreaM2*body->dragCoefficient;
  force=MotionVec_Add(force,MotionVec_Scale(relative,drag*MotionVec_Length(relative)));
  Vector3 acceleration=MotionVec_Add(medium.gravityMps2,MotionVec_Add(acc,MotionVec_Scale(force,1/body->massKg)));
  Vector3 lateral={0,MotionVec_Dot(acceleration,q.normal),MotionVec_Dot(acceleration,q.binormal)};
  float omega=sqrtf(stiffness/body->massKg);
  if(omega<=0) omega=1; /* A bounded route still returns without an authored guide law. */
  float denom=1+2*omega*dt+omega*omega*dt*dt;
  state->velocityMps=MotionVec_Scale(MotionVec_Add(state->velocityMps,
      MotionVec_Scale(MotionVec_Sub(lateral,MotionVec_Scale(state->offsetM,omega*omega)),dt)),1/denom);
  state->offsetM=MotionVec_Add(state->offsetM,MotionVec_Scale(state->velocityMps,dt));
  float len=MotionVec_Length(state->offsetM);
  if(len>room) {
    Vector3 normal=MotionVec_Scale(state->offsetM,1/len);
    state->offsetM=MotionVec_Scale(normal,room);
    float outward=MotionVec_Dot(state->velocityMps,normal);
    if(outward>0) state->velocityMps=MotionVec_Sub(state->velocityMps,MotionVec_Scale(normal,outward));
  }
  float edge=Motion_Clamp(fminf(q.distance,view->path->length-q.distance)/radius,0,1);
  edge=edge*edge*(3-2*edge); /* Exact A/B arrival; grow/drain without endpoint fans. */
  lane=MotionVec_Scale(MotionVec_Add(base,state->offsetM),edge);
  return MotionPathTransport_ToWorld(view,q,lane);
}
#endif

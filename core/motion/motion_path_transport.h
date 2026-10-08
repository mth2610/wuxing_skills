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
  bool captureBirthLane; /* Capture transverse birth offset once; never recapture. */
} MotionPathTransport;
typedef struct MotionPathTransportSnapshot {
  const MotionPath *path;
  FieldTransform transform;
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
static inline Vector3 MotionPathTransport_Sample(
    const MotionPathTransportSnapshot *view,float distance,Vector3 laneOffset) {
  const MotionPath *p=view?view->path:NULL;
  if(!p || p->count<2 || p->count>MOTION_PATH_MAX_POINTS || !isfinite(distance) ||
      !isfinite(laneOffset.x) || !isfinite(laneOffset.y) || !isfinite(laneOffset.z))
    return (Vector3){0};
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
  Vector3 local=MotionVec_Add(position,MotionVec_Add(MotionVec_Scale(tangent,laneOffset.x),
      MotionVec_Add(MotionVec_Scale(normal,laneOffset.y),MotionVec_Scale(MotionVec_Cross(tangent,normal),laneOffset.z))));
  return MotionVec_Add(view->transform.position,FieldTransform_Vector(&view->transform,local));
}
#endif

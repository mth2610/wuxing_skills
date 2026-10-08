#ifndef CORE_MOTION_GPU_H
#define CORE_MOTION_GPU_H
#include "core/motion/motion_fields.h"
#include <string.h>
/* Versioned sidecar ABI; the existing 144-byte render particle ABI is unchanged.
 * Every member occupies complete std430 vec4/uvec4 lanes. Handles stay uint32. */
#define MOTION_GPU_ABI_VERSION 1
#define MOTION_GPU_MAX_FIELDS MOTION_FIELDS_MAX_TARGETS
typedef struct MotionGpuLaw {
  Vector4 forceType, accelerationMagnitude, centerStiffness, params;
  Vector4 procedural;
} MotionGpuLaw;
typedef struct MotionGpuField {
  uint32_t identity[4]; /* generation handle, shape, receiver mask, lane flags */
  Vector4 volume, halfExtents, capsuleA, capsuleB;
  Vector4 position, axisX, axisY, axisZ, frameVelocity, angularVelocity;
  Vector4 flowVelocity, flowAxis, flowParams;
  Vector4 trajectory[MOTION_PATH_MAX_POINTS]; /* xyz point, w arc distance */
  Vector4 points[MOTION_PATH_MAX_POINTS], tangents[MOTION_PATH_MAX_POINTS], normals[MOTION_PATH_MAX_POINTS];
  MotionGpuLaw laws[FIELD_MAX_FORCE_LAWS];
} MotionGpuField;
typedef struct MotionGpuScene {
  uint32_t meta[4]; /* count, ABI version, reserved, reserved */
  Vector4 zoneDirectionStrength, zoneNoise; /* global acceleration adapter */
  MotionGpuField fields[MOTION_GPU_MAX_FIELDS];
} MotionGpuScene;
typedef struct MotionGpuBody {
  uint32_t meta[4]; /* enabled, capture spatial offsets, receiver mask, reserved */
  Vector4 body0, body1, body2, body3;
  Vector4 acceleration, force;
  uint32_t laneHandles[4];
  Vector4 laneOffsets[4];
} MotionGpuBody;
typedef char MotionGpuLawLayout[(sizeof(MotionGpuLaw)==80)?1:-1];
typedef char MotionGpuFieldLayout[(sizeof(MotionGpuField)==4960)?1:-1];
typedef char MotionGpuBodyLayout[(sizeof(MotionGpuBody)==192)?1:-1];
static inline Vector4 MotionGpu_V4(Vector3 p,float w) { return (Vector4){p.x,p.y,p.z,w}; }
static inline void MotionGpu_PackField(const FieldDesc *d,MotionFieldHandle handle,
    float age,MotionGpuField *out) {
  memset(out,0,sizeof(*out));
  int trajectoryCount=d->trajectory.mode==FIELD_TRAJECTORY_PATH?d->trajectory.path.count:0;
  int volumeCount=d->volume.shape==FIELD_PATH_TUBE?d->volume.path.count:0;
  if(trajectoryCount<0) trajectoryCount=0;
  if(volumeCount<0) volumeCount=0;
  if(trajectoryCount>MOTION_PATH_MAX_POINTS) trajectoryCount=MOTION_PATH_MAX_POINTS;
  if(volumeCount>MOTION_PATH_MAX_POINTS) volumeCount=MOTION_PATH_MAX_POINTS;
  out->identity[0]=handle;out->identity[1]=(uint32_t)d->volume.shape;
  out->identity[2]=d->receiverMask?d->receiverMask:MOTION_RECEIVER_ALL;
  out->identity[3]=(d->preserveSphereOffsets?1u:0u)|(d->preservePathLanes?2u:0u)|(d->rotatePathLanes?4u:0u);
  out->volume=(Vector4){d->volume.radiusM,d->volume.coreFraction,age,d->flow.enabled?1:0};
  out->halfExtents=MotionGpu_V4(d->volume.halfExtentsM,(float)volumeCount);
  out->capsuleA=MotionGpu_V4(d->volume.capsuleStart,d->flow.followSpeedMps);
  out->capsuleB=MotionGpu_V4(d->volume.capsuleEnd,(float)d->flow.priority);
  out->position=MotionGpu_V4(d->transform.position,d->trajectory.speedMps);
  out->axisX=MotionGpu_V4(d->transform.axisX,(float)trajectoryCount);
  out->axisY=MotionGpu_V4(d->transform.axisY,d->lifetime.startDelaySec);
  out->axisZ=MotionGpu_V4(d->transform.axisZ,d->lifetime.durationSec);
  out->frameVelocity=MotionGpu_V4(d->transform.frameVelocityMps,d->lifetime.attackSec);
  out->angularVelocity=MotionGpu_V4(d->transform.angularVelocityRadPerSec,d->lifetime.fadeSec);
  out->flowVelocity=MotionGpu_V4(d->flow.velocityMps,d->flow.blendWeight>0?d->flow.blendWeight:1);
  out->flowAxis=MotionGpu_V4(d->flow.axis,d->flow.addBackgroundVelocity?1:0);
  out->flowParams=(Vector4){d->flow.procedural.turbulenceSpeedMps,d->flow.procedural.swirlSpeedMps,
    d->flow.procedural.eddyLengthM,(float)d->forceLawCount};
  for(int i=0;i<trajectoryCount;i++)
    out->trajectory[i]=MotionGpu_V4(d->trajectory.path.points[i],d->trajectory.path.distance[i]);
  for(int i=0;i<volumeCount;i++) {
    out->points[i]=MotionGpu_V4(d->volume.path.points[i],d->volume.path.distance[i]);
    out->tangents[i]=MotionGpu_V4(d->volume.path.tangents[i],0);
    out->normals[i]=MotionGpu_V4(d->volume.path.normals[i],0);
  }
  for(int i=0;i<d->forceLawCount;i++) {
    const ForceLaw *l=&d->forceLaws[i];MotionGpuLaw *g=&out->laws[i];
    if(l->type==FORCE_LAW_DRAG) out->identity[3]|=8u;
    g->forceType=MotionGpu_V4(l->forceNewtons,(float)l->type);
    g->accelerationMagnitude=MotionGpu_V4(l->accelerationMps2,l->magnitudeNewtons);
    g->centerStiffness=MotionGpu_V4(l->center,l->springStiffnessNPerM);
    g->params=(Vector4){l->dampingNsPerM,l->forwardForceNewtons,0,0};
    g->procedural=(Vector4){l->procedural.turbulenceSpeedMps,l->procedural.swirlSpeedMps,l->procedural.eddyLengthM,0};
  }
}
static inline MotionGpuBody MotionGpu_PackBody(const ParticleDynamicsProfile *profile,
    Vector3 acceleration,Vector3 force,bool captureOffsets,float windInfluence) {
  ParticleDynamicsProfile fallback={.inverseMassKg=1,.windCouplingHz=3.5f,.windSusceptibility=windInfluence};
  const ParticleDynamicsProfile *p=profile?profile:&fallback;
  MotionGpuBody out={0};out.meta[0]=1;out.meta[1]=captureOffsets?1:0;out.meta[2]=MOTION_RECEIVER_PARTICLE;
  out.body0=(Vector4){p->inverseMassKg>0?p->inverseMassKg:1,p->gravityScale,p->linearDragPerSecond,p->terminalSpeedMps};
  out.body1=(Vector4){p->windAccelerationScale,p->windCouplingHz,p->windSusceptibility,p->steeringFrequencyHz};
  out.body2=(Vector4){p->aerodynamicAreaM2,p->aerodynamicDragCoefficient,p->airDensityKgM3,p->densityKgM3};
  out.body3=(Vector4){p->maxSteeringAccelMps2,0,0,0};
  out.acceleration=MotionGpu_V4(acceleration,0);out.force=MotionGpu_V4(force,0);
  return out;
}
/* Copies only world-space spatial fields. Captured legacy guides and their
 * callbacks remain a distinct CPU contract. Upload meta + active fields. */
void MotionFields_PackGpu(MotionGpuScene *out);
#endif

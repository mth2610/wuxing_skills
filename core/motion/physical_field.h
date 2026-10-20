#ifndef CORE_PHYSICAL_FIELD_H
#define CORE_PHYSICAL_FIELD_H
#include <stddef.h>
#include "core/motion/motion_flow.h"
#define FIELD_MAX_FORCE_LAWS 8

/* All geometry is metres in a rigid local frame. Scaling is explicit in
 * geometry dimensions; transforms never scale positions or velocities. */
typedef enum
{
  FIELD_SPHERE,
  FIELD_CAPSULE,
  FIELD_BOX,
  FIELD_PATH_TUBE
} FieldShape;
typedef struct FieldVolume
{
  FieldShape shape;
  float radiusM, coreFraction;
  Vector3 halfExtentsM, capsuleStart, capsuleEnd;
  MotionPath path;
} FieldVolume;
typedef struct FieldTransform
{
  Vector3 position, axisX, axisY, axisZ;
  Vector3 frameVelocityMps, angularVelocityRadPerSec;
} FieldTransform;
typedef enum
{
  FIELD_TRAJECTORY_STATIC,
  FIELD_TRAJECTORY_PATH
} FieldTrajectoryMode;
typedef struct FieldTrajectory
{
  FieldTrajectoryMode mode;
  MotionPath path;
  float speedMps;
} FieldTrajectory;
typedef struct FieldLifetime
{
  float startDelaySec, durationSec, attackSec, fadeSec;
} FieldLifetime;
typedef struct BodyPhysicalProperties
{
  float massKg, densityKgM3, volumeM3, projectedAreaM2, dragCoefficient;
  float immersionFraction;
} BodyPhysicalProperties;
typedef struct MediumProperties
{
  float densityKgM3, dynamicViscosityPaS;
  Vector3 velocityMps, gravityMps2;
} MediumProperties;
typedef enum
{
  RECEIVER_FREE,
  RECEIVER_ROOTED,
  RECEIVER_STATIC,
  RECEIVER_KINEMATIC,
  RECEIVER_TRACER
} ReceiverMode;
typedef struct ReceiverConstraints
{
  ReceiverMode mode;
  /* Root material springs are solved by the receiver, not the field. */
  Vector3 permittedAxes;
} ReceiverConstraints;

typedef enum
{
  FORCE_LAW_NEWTONS,
  FORCE_LAW_ACCELERATION,
  FORCE_LAW_RADIAL_ATTRACTION,
  FORCE_LAW_SPRING,
  FORCE_LAW_BUOYANCY,
  FORCE_LAW_DRAG,
  FORCE_LAW_PATH_GUIDE,
  FORCE_LAW_MOVING_GUIDE,
  FORCE_LAW_CURL_FORCE
} ForceLawType;
typedef enum
{
  GUIDE_MANUAL,
  GUIDE_LOOSE,
  GUIDE_BALANCED,
  GUIDE_TIGHT
} GuidePreset;

typedef struct GuideTuning
{
  float maxForceNewtons, forwardForceNewtons, stiffnessNPerM, settlingTimeSec;
  float turbulenceForceNewtons, turbulenceSpeedMps, eddyLengthM;
} GuideTuning;

static inline GuideTuning GuideTuning_Derive(const BodyPhysicalProperties *body,
                                             float radiusM, float speedMps, float swirlMps, float turbulenceMps,
                                             float gravityMps2, GuidePreset preset)
{
  GuideTuning out = {0};
  if (!body || !isfinite(body->massKg) || body->massKg <= 0 ||
      !isfinite(radiusM) || radiusM <= 0 || !isfinite(speedMps) || speedMps < 0 ||
      !isfinite(swirlMps) || !isfinite(turbulenceMps) || turbulenceMps < 0 ||
      !isfinite(gravityMps2) || preset < GUIDE_LOOSE || preset > GUIDE_TIGHT)
    return out;
  float factor = preset == GUIDE_LOOSE ? 0.75f : preset == GUIDE_TIGHT ? 1.5f
                                                                       : 1.0f;
  float omega = factor * fmaxf(sqrtf(fabsf(gravityMps2) / (radiusM * 0.08f)),
                               2.0f * fmaxf(speedMps, fabsf(swirlMps)) / radiusM);
  omega = fmaxf(omega, 0.5f);
  out.stiffnessNPerM = body->massKg * omega * omega;
  out.maxForceNewtons = 2.0f * out.stiffnessNPerM * radiusM;
  out.forwardForceNewtons = body->massKg * omega * speedMps;
  out.settlingTimeSec = 6.0f / omega;
  out.eddyLengthM = radiusM;
  out.turbulenceForceNewtons = fminf(
      body->massKg * turbulenceMps * turbulenceMps / out.eddyLengthM,
      out.maxForceNewtons * 0.25f);
  out.turbulenceSpeedMps =
      sqrtf(out.turbulenceForceNewtons * out.eddyLengthM / body->massKg);
  return out;
}

typedef struct GuideController
{
  float maxForceNewtons, dampingRatio;
} GuideController;

typedef struct ForceLaw
{
  ForceLawType type;
  Vector3 forceNewtons, accelerationMps2, center;
  float magnitudeNewtons, springStiffnessNPerM, dampingNsPerM;
  float forwardForceNewtons;
  MotionFlowDesc procedural;
} ForceLaw;

typedef struct FlowField
{
  Vector3 velocityMps, axis;
  MotionFlowDesc procedural;
  int priority;
  float blendWeight;
  bool enabled;
  bool addBackgroundVelocity;
  float followSpeedMps;
} FlowField;

typedef struct FieldDesc
{
  FieldVolume volume;
  FieldTransform transform;
  FieldTrajectory trajectory;
  FieldLifetime lifetime;
  ForceLaw forceLaws[FIELD_MAX_FORCE_LAWS];
  int forceLawCount;
  FlowField flow;
  unsigned int receiverMask;
  bool preservePathLanes;
  bool preserveSphereOffsets;
  bool rotatePathLanes;
} FieldDesc;

#define FIELD_MAX_CONTROLLER_RESPONSES 8
typedef struct FieldControllerResponse
{
  Vector3 driveNewtons, sampledForceNewtons;
  Vector3 diagonal, offDiagonal;
  float maxForceNewtons;
} FieldControllerResponse;

typedef struct FieldSample
{
  Vector3 forceNewtons, accelerationMps2, mediumVelocityMps;
  float mediumWeight;
  int mediumPriority;
  bool mediumIsAbsolute;
  bool hasDragForce, hasBuoyancyForce;
  Vector3 dragForceNewtons, dragMediumVelocityMps;
  float dragCoefficientKgPerM;
  int controllerCount;
  FieldControllerResponse controllers[FIELD_MAX_CONTROLLER_RESPONSES];
} FieldSample;

static inline BodyPhysicalProperties BodyPhysicalProperties_Sphere(
    float massKg, float densityKgM3, float dragCoefficient)
{
  BodyPhysicalProperties b = {.massKg = massKg,
                              .densityKgM3 = densityKgM3,
                              .dragCoefficient = dragCoefficient,
                              .immersionFraction = 1};
  if (massKg > 0 && densityKgM3 > 0)
  {
    b.volumeM3 = massKg / densityKgM3;
    float radius = cbrtf(b.volumeM3 * 0.2387324146f);
    b.projectedAreaM2 = 3.1415926536f * radius * radius;
  }
  return b;
}

typedef struct ThinLaminaMaterial
{
  float arealMassKgM2, thicknessM, dragCoefficient;
} ThinLaminaMaterial;

typedef enum
{
  BODY_LAMINA_LEAF_DRY,
  BODY_LAMINA_LEAF_FRESH,
  BODY_LAMINA_PETAL_FRESH
} BodyLaminaPreset;

static inline ThinLaminaMaterial BodyLaminaMaterial_Preset(BodyLaminaPreset preset)
{
  switch (preset)
  {
  case BODY_LAMINA_LEAF_DRY:
    return (ThinLaminaMaterial){0.060f, 0.00020f, 1.28f};
  case BODY_LAMINA_LEAF_FRESH:
    return (ThinLaminaMaterial){0.200f, 0.00020f, 1.28f};
  case BODY_LAMINA_PETAL_FRESH:
    return (ThinLaminaMaterial){0.150f, 0.00020f, 1.28f};
  }
  return (ThinLaminaMaterial){0};
}

static inline BodyPhysicalProperties BodyPhysicalProperties_Lamina(
    float areaM2, const ThinLaminaMaterial *material)
{
  BodyPhysicalProperties b = {0};
  if (!material || !isfinite(areaM2) || areaM2 <= 0 ||
      !isfinite(material->arealMassKgM2) || material->arealMassKgM2 <= 0 ||
      !isfinite(material->thicknessM) || material->thicknessM <= 0 ||
      !isfinite(material->dragCoefficient) || material->dragCoefficient < 0)
    return b;
  b.massKg = areaM2 * material->arealMassKgM2;
  b.volumeM3 = areaM2 * material->thicknessM;
  b.densityKgM3 = material->arealMassKgM2 / material->thicknessM;
  b.projectedAreaM2 = areaM2;
  b.dragCoefficient = material->dragCoefficient;
  b.immersionFraction = 1;
  if (!isfinite(b.massKg) || !isfinite(b.volumeM3) || !isfinite(b.densityKgM3))
    return (BodyPhysicalProperties){0};
  return b;
}

static inline BodyPhysicalProperties BodyPhysicalProperties_Leaf(void)
{
  ThinLaminaMaterial material = BodyLaminaMaterial_Preset(BODY_LAMINA_LEAF_DRY);
  return BodyPhysicalProperties_Lamina(0.012f, &material);
}

static inline FieldTransform FieldTransform_Identity(void)
{
  return (FieldTransform){.axisX = {1, 0, 0}, .axisY = {0, 1, 0}, .axisZ = {0, 0, 1}};
}

static inline Vector3 FieldTransform_Vector(const FieldTransform *t, Vector3 v)
{
  return (Vector3){
      t->axisX.x * v.x + t->axisY.x * v.y + t->axisZ.x * v.z,
      t->axisX.y * v.x + t->axisY.y * v.y + t->axisZ.y * v.z,
      t->axisX.z * v.x + t->axisY.z * v.y + t->axisZ.z * v.z};
}

static inline Vector3 FieldTransform_LocalVector(const FieldTransform *t, Vector3 v)
{
  return (Vector3){
      MotionVec_Dot(v, t->axisX),
      MotionVec_Dot(v, t->axisY),
      MotionVec_Dot(v, t->axisZ)};
}

/* Chuẩn hóa vector trục nhanh: nếu đã là vector đơn vị (phổ biến nhất là {0,1,0})
 * thì trả về ngay không cần gọi sqrtf(). */
static inline Vector3 Field_NormalizeAxisOrUp(Vector3 axis)
{
  float lenSq = MotionVec_Dot(axis, axis);
  if (lenSq < 0.01f)
    return (Vector3){0, 1, 0};
  if (fabsf(lenSq - 1.0f) <= 1e-5f)
    return axis;
  return MotionVec_Scale(axis, 1.0f / sqrtf(lenSq));
}

static inline float FieldLifetime_Weight(const FieldLifetime *l, float age)
{
  age -= l->startDelaySec;
  if (age < 0.0f || age >= l->durationSec)
    return 0.0f;
  float w = 1.0f;
  if (l->attackSec > 0.0f && age < l->attackSec)
    w *= Motion_Clamp(age / l->attackSec, 0.0f, 1.0f);
  if (l->fadeSec > 0.0f && (l->durationSec - age) < l->fadeSec)
    w *= Motion_Clamp((l->durationSec - age) / l->fadeSec, 0.0f, 1.0f);
  return w * w * (3.0f - 2.0f * w);
}

static inline FieldTransform FieldTrajectory_TransformFrom(
    const FieldDesc *d, const FieldTransform *base, float age)
{
  FieldTransform t = *base;
  if (d->trajectory.mode == FIELD_TRAJECTORY_PATH)
  {
    MotionPathSample p = MotionPath_Sample(
        &d->trajectory.path,
        fmaxf(age - d->lifetime.startDelaySec, 0.0f) * d->trajectory.speedMps);
    t.position = MotionVec_Add(t.position, FieldTransform_Vector(base, p.position));
    if (p.distance < d->trajectory.path.length)
      t.frameVelocityMps = MotionVec_Add(
          t.frameVelocityMps,
          FieldTransform_Vector(base, MotionVec_Scale(p.tangent, d->trajectory.speedMps)));
  }
  return t;
}

static inline FieldTransform FieldTrajectory_Transform(const FieldDesc *d, float age)
{
  return FieldTrajectory_TransformFrom(d, &d->transform, age);
}

static inline float FieldVolume_NormalizedDistanceAt(
    const FieldVolume *v, Vector3 p, const MotionPathSample *pathSample)
{
  if (v->shape == FIELD_BOX)
    return fmaxf(fabsf(p.x) / v->halfExtentsM.x,
                 fmaxf(fabsf(p.y) / v->halfExtentsM.y, fabsf(p.z) / v->halfExtentsM.z));
  if (v->shape == FIELD_CAPSULE)
    return Motion_SegmentDistance(v->capsuleStart, v->capsuleEnd, p) / v->radiusM;
  if (v->shape == FIELD_PATH_TUBE)
  {
    MotionPathSample q = pathSample
                             ? *pathSample
                             : MotionPath_Project(&v->path, p, 0, v->path.count - 2);
    return MotionVec_Length(MotionVec_Sub(p, q.position)) / v->radiusM;
  }
  return MotionVec_Length(p) / v->radiusM;
}

static inline float FieldVolume_WeightFastAt(
    const FieldVolume *v, Vector3 p, const MotionPathSample *pathSample)
{
  float normDist;
  if (v->shape == FIELD_SPHERE)
  {
    float r = v->radiusM;
    float distSq = MotionVec_Dot(p, p);
    if (distSq >= r * r)
      return 0.0f;
    float coreR = r * v->coreFraction;
    if (distSq <= coreR * coreR)
      return 1.0f;
    normDist = sqrtf(distSq) / r;
  }
  else if (v->shape == FIELD_PATH_TUBE)
  {
    MotionPathSample q = pathSample
                             ? *pathSample
                             : MotionPath_Project(&v->path, p, 0, v->path.count - 2);
    Vector3 diff = MotionVec_Sub(p, q.position);
    float r = v->radiusM;
    float distSq = MotionVec_Dot(diff, diff);
    if (distSq >= r * r)
      return 0.0f;
    float coreR = r * v->coreFraction;
    if (distSq <= coreR * coreR)
      return 1.0f;
    normDist = sqrtf(distSq) / r;
  }
  else if (v->shape == FIELD_BOX)
  {
    normDist = fmaxf(fabsf(p.x) / v->halfExtentsM.x,
                     fmaxf(fabsf(p.y) / v->halfExtentsM.y,
                           fabsf(p.z) / v->halfExtentsM.z));
    if (normDist >= 1.0f)
      return 0.0f;
    if (normDist <= v->coreFraction)
      return 1.0f;
  }
  else
  {
    float dist = Motion_SegmentDistance(v->capsuleStart, v->capsuleEnd, p);
    if (dist >= v->radiusM)
      return 0.0f;
    normDist = dist / v->radiusM;
    if (normDist <= v->coreFraction)
      return 1.0f;
  }
  float x = (normDist - v->coreFraction) / fmaxf(1.0f - v->coreFraction, 0.0001f);
  x = Motion_Clamp(x, 0.0f, 1.0f);
  return 1.0f - x * x * (3.0f - 2.0f * x);
}

static inline float FieldVolume_WeightAt(const FieldVolume *v, Vector3 p,
                                         const MotionPathSample *pathSample)
{
  if (!v || v->shape < FIELD_SPHERE || v->shape > FIELD_PATH_TUBE ||
      !isfinite(v->coreFraction) || v->coreFraction < 0 || v->coreFraction >= 1 ||
      (v->shape == FIELD_BOX &&
       (!isfinite(v->halfExtentsM.x) || !isfinite(v->halfExtentsM.y) ||
        !isfinite(v->halfExtentsM.z) || v->halfExtentsM.x <= 0 ||
        v->halfExtentsM.y <= 0 || v->halfExtentsM.z <= 0)) ||
      !isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
      (v->shape != FIELD_BOX && (!isfinite(v->radiusM) || v->radiusM <= 0)) ||
      (v->shape == FIELD_PATH_TUBE &&
       (v->path.count < 2 || v->path.count > MOTION_PATH_MAX_POINTS)))
    return 0.0f;
  return FieldVolume_WeightFastAt(v, p, pathSample);
}

static inline float FieldVolume_Weight(const FieldVolume *v, Vector3 p)
{
  return FieldVolume_WeightAt(v, p, NULL);
}

static inline Vector3 ForceLaw_Evaluate(
    const ForceLaw *law, Vector3 p, Vector3 v,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    Vector3 flow)
{
  switch (law->type)
  {
  case FORCE_LAW_NEWTONS:
    return law->forceNewtons;
  case FORCE_LAW_ACCELERATION:
    return law->accelerationMps2;
  case FORCE_LAW_RADIAL_ATTRACTION:
    return MotionVec_Scale(MotionVec_Normalize(MotionVec_Sub(law->center, p)),
                           law->magnitudeNewtons);
  case FORCE_LAW_SPRING:
    return MotionVec_Sub(
        MotionVec_Scale(MotionVec_Sub(law->center, p), law->springStiffnessNPerM),
        MotionVec_Scale(v, law->dampingNsPerM));
  case FORCE_LAW_BUOYANCY:
  {
    float volume = body->volumeM3 > 0      ? body->volumeM3
                   : body->densityKgM3 > 0 ? body->massKg / body->densityKgM3
                                           : 0.0f;
    return MotionVec_Scale(
        medium->gravityMps2,
        -medium->densityKgM3 * volume * Motion_Clamp(body->immersionFraction, 0, 1));
  }
  case FORCE_LAW_PATH_GUIDE:
  case FORCE_LAW_CURL_FORCE:
  case FORCE_LAW_MOVING_GUIDE:
    return (Vector3){0};
  case FORCE_LAW_DRAG:
  {
    Vector3 relative = MotionVec_Sub(flow, v);
    return MotionVec_Scale(
        relative, 0.5f * medium->densityKgM3 * body->dragCoefficient *
                      body->projectedAreaM2 * MotionVec_Length(relative));
  }
  }
  return (Vector3){0};
}

/* Flow channels blend; force and acceleration channels accumulate. */
static inline void FieldSample_Combine(FieldSample *out, const FieldSample *in)
{
  for (int i = 0; i < in->controllerCount &&
                  out->controllerCount < FIELD_MAX_CONTROLLER_RESPONSES;
       i++)
    out->controllers[out->controllerCount++] = in->controllers[i];
  out->hasDragForce = out->hasDragForce || in->hasDragForce;
  out->hasBuoyancyForce = out->hasBuoyancyForce || in->hasBuoyancyForce;
  out->dragForceNewtons = MotionVec_Add(out->dragForceNewtons, in->dragForceNewtons);
  if (in->hasDragForce)
  {
    float newDragCoeff = out->dragCoefficientKgPerM + in->dragCoefficientKgPerM;
    if (out->dragCoefficientKgPerM <= 0.0f || newDragCoeff <= 1e-12f)
    {
      out->dragMediumVelocityMps = in->dragMediumVelocityMps;
    }
    else if (in->dragCoefficientKgPerM > 0.0f)
    {
      out->dragMediumVelocityMps = MotionVec_Scale(
          MotionVec_Add(
              MotionVec_Scale(out->dragMediumVelocityMps, out->dragCoefficientKgPerM),
              MotionVec_Scale(in->dragMediumVelocityMps, in->dragCoefficientKgPerM)),
          1.0f / newDragCoeff);
    }
    out->dragCoefficientKgPerM = newDragCoeff;
  }
  out->forceNewtons = MotionVec_Add(out->forceNewtons, in->forceNewtons);
  out->accelerationMps2 = MotionVec_Add(out->accelerationMps2, in->accelerationMps2);
  if (in->mediumWeight <= 0.0f)
    return;
  if (out->mediumWeight <= 0.0f || in->mediumPriority > out->mediumPriority)
  {
    out->mediumVelocityMps = in->mediumVelocityMps;
    out->mediumWeight = in->mediumWeight;
    out->mediumPriority = in->mediumPriority;
    out->mediumIsAbsolute = in->mediumIsAbsolute;
  }
  else if (in->mediumPriority == out->mediumPriority)
  {
    float sum = out->mediumWeight + in->mediumWeight;
    out->mediumVelocityMps = MotionVec_Scale(
        MotionVec_Add(MotionVec_Scale(out->mediumVelocityMps, out->mediumWeight),
                      MotionVec_Scale(in->mediumVelocityMps, in->mediumWeight)),
        1.0f / sum);
    out->mediumWeight = sum;
    out->mediumIsAbsolute = out->mediumIsAbsolute || in->mediumIsAbsolute;
  }
}

static inline float FieldFlow_CharacteristicRadius(const FieldVolume *v)
{
  return v->shape == FIELD_BOX
             ? fminf(v->halfExtentsM.x, fminf(v->halfExtentsM.y, v->halfExtentsM.z))
             : v->radiusM;
}

static inline Vector3 FieldFlow_PotentialAt(
    const FlowField *flow, const FieldVolume *volume, Vector3 p, float time,
    const MotionPathSample *pathSample)
{
  float weight = FieldVolume_WeightFastAt(volume, p, pathSample);
  if (weight <= 0.0f || flow->procedural.turbulenceSpeedMps == 0.0f)
    return (Vector3){0};
  float eddy = flow->procedural.eddyLengthM > 0.0f
                   ? flow->procedural.eddyLengthM
                   : FieldFlow_CharacteristicRadius(volume) * 0.25f;
  if (eddy <= 0.0f || !isfinite(eddy))
    return (Vector3){0};
  float invEddy = 1.0f / eddy;
  float phase = time * flow->procedural.turbulenceSpeedMps * invEddy;
  Vector3 q = {p.x * invEddy + phase * 0.37f,
               p.y * invEddy + phase * 0.51f,
               p.z * invEddy + phase * 0.29f};
  Vector3 noise = {Noise_Perlin3D(q.x, q.y, q.z),
                   Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f),
                   Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f)};
  return MotionVec_Scale(noise, flow->procedural.turbulenceSpeedMps * eddy * weight);
}

static inline Vector3 FieldFlow_Potential(
    const FlowField *flow, const FieldVolume *volume, Vector3 p, float time)
{
  return FieldFlow_PotentialAt(flow, volume, p, time, NULL);
}

/* Kiểm tra nhanh xem mọi điểm lân cận p +/- h có nằm hoàn toàn trong vùng lõi
 * (trọng số == 1.0f) hay không để bỏ qua 6 lần gọi FieldVolume_WeightFastAt. */
static inline bool FieldVolume_IsDeepInCore(
    const FieldVolume *v, Vector3 p, float h, const MotionPathSample *pathSample)
{
  if (v->coreFraction <= 0.0f)
    return false;
  if (v->shape == FIELD_SPHERE)
  {
    float shrunkCore = v->radiusM * v->coreFraction - h;
    return shrunkCore > 0.0f && MotionVec_Dot(p, p) <= shrunkCore * shrunkCore;
  }
  if (v->shape == FIELD_PATH_TUBE && pathSample)
  {
    float shrunkCore = v->radiusM * v->coreFraction - h;
    if (shrunkCore <= 0.0f)
      return false;
    Vector3 diff = MotionVec_Sub(p, pathSample->position);
    return MotionVec_Dot(diff, diff) <= shrunkCore * shrunkCore;
  }
  if (v->shape == FIELD_BOX)
  {
    return (fabsf(p.x) + h <= v->halfExtentsM.x * v->coreFraction) &&
           (fabsf(p.y) + h <= v->halfExtentsM.y * v->coreFraction) &&
           (fabsf(p.z) + h <= v->halfExtentsM.z * v->coreFraction);
  }
  return false;
}

static inline Vector3 FieldFlow_EvaluateWithWeightAt(
    const FlowField *flow, const FieldVolume *volume, Vector3 p, float time,
    const MotionPathSample *pathSample, float weight)
{
  Vector3 out = {0};
  if (weight <= 0.0f || !MotionFlow_IsValid(&flow->procedural))
    return out;
  float radius = FieldFlow_CharacteristicRadius(volume);
  Vector3 center = {0}, axis = Field_NormalizeAxisOrUp(flow->axis);

  if (volume->shape == FIELD_PATH_TUBE)
  {
    center = pathSample->position;
    axis = pathSample->tangent;
    if (flow->followSpeedMps != 0.0f)
    {
      out = MotionVec_Scale(pathSample->tangent, flow->followSpeedMps * weight);
    }
  }
  else if (volume->shape == FIELD_CAPSULE)
  {
    Vector3 delta = MotionVec_Sub(volume->capsuleEnd, volume->capsuleStart);
    float square = MotionVec_Dot(delta, delta);
    float along =
        square > 1e-12f
            ? Motion_Clamp(MotionVec_Dot(MotionVec_Sub(p, volume->capsuleStart), delta) /
                               square,
                           0.0f, 1.0f)
            : 0.0f;
    center = MotionVec_Add(volume->capsuleStart, MotionVec_Scale(delta, along));
    if (square > 1e-12f)
      axis = MotionVec_Scale(delta, 1.0f / sqrtf(square));
  }

  if (flow->procedural.swirlSpeedMps != 0.0f && radius > 0.0f)
  {
    Vector3 radial = MotionFlow_Radial(p, center, axis);
    out = MotionVec_Add(
        out, MotionVec_Scale(MotionVec_Cross(axis, radial),
                             flow->procedural.swirlSpeedMps / radius * weight));
  }

  if (flow->procedural.turbulenceSpeedMps > 0.0f)
  {
    float eddy = flow->procedural.eddyLengthM > 0.0f
                     ? flow->procedural.eddyLengthM
                     : radius * 0.25f;
    float h = eddy * 0.02f;
    if (h <= 0.0f || !isfinite(h))
      return out;

    float invEddy = 1.0f / eddy;
    float phase = time * flow->procedural.turbulenceSpeedMps * invEddy;
    Vector3 q = {p.x * invEddy + phase * 0.37f,
                 p.y * invEddy + phase * 0.51f,
                 p.z * invEddy + phase * 0.29f};
    float step = 0.02f;
    float amp = flow->procedural.turbulenceSpeedMps * eddy;
    float scale = amp / (2.0f * h);

    float ax_y, ax_z, ay_x, ay_z, az_x, az_y;
    if (weight >= 1.0f && FieldVolume_IsDeepInCore(volume, p, h, pathSample))
    {
      /* Fast-path: cả 6 điểm dịch chuyển đều nằm trong vùng lõi (weight == 1.0f) */
      ax_y = Noise_Perlin3D(q.x + step + 31.416f, q.y + 31.416f, q.z + 31.416f) -
             Noise_Perlin3D(q.x - step + 31.416f, q.y + 31.416f, q.z + 31.416f);
      ax_z = Noise_Perlin3D(q.x + step + 67.234f, q.y + 67.234f, q.z + 67.234f) -
             Noise_Perlin3D(q.x - step + 67.234f, q.y + 67.234f, q.z + 67.234f);

      ay_x = Noise_Perlin3D(q.x, q.y + step, q.z) -
             Noise_Perlin3D(q.x, q.y - step, q.z);
      ay_z = Noise_Perlin3D(q.x + 67.234f, q.y + step + 67.234f, q.z + 67.234f) -
             Noise_Perlin3D(q.x + 67.234f, q.y - step + 67.234f, q.z + 67.234f);

      az_x = Noise_Perlin3D(q.x, q.y, q.z + step) -
             Noise_Perlin3D(q.x, q.y, q.z - step);
      az_y = Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + step + 31.416f) -
             Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z - step + 31.416f);
    }
    else
    {
      float wxPos = FieldVolume_WeightFastAt(volume, (Vector3){p.x + h, p.y, p.z}, pathSample);
      float wxNeg = FieldVolume_WeightFastAt(volume, (Vector3){p.x - h, p.y, p.z}, pathSample);
      float wyPos = FieldVolume_WeightFastAt(volume, (Vector3){p.x, p.y + h, p.z}, pathSample);
      float wyNeg = FieldVolume_WeightFastAt(volume, (Vector3){p.x, p.y - h, p.z}, pathSample);
      float wzPos = FieldVolume_WeightFastAt(volume, (Vector3){p.x, p.y, p.z + h}, pathSample);
      float wzNeg = FieldVolume_WeightFastAt(volume, (Vector3){p.x, p.y, p.z - h}, pathSample);

      ax_y = (wxPos > 0.0f ? wxPos * Noise_Perlin3D(q.x + step + 31.416f, q.y + 31.416f, q.z + 31.416f) : 0.0f) -
             (wxNeg > 0.0f ? wxNeg * Noise_Perlin3D(q.x - step + 31.416f, q.y + 31.416f, q.z + 31.416f) : 0.0f);
      ax_z = (wxPos > 0.0f ? wxPos * Noise_Perlin3D(q.x + step + 67.234f, q.y + 67.234f, q.z + 67.234f) : 0.0f) -
             (wxNeg > 0.0f ? wxNeg * Noise_Perlin3D(q.x - step + 67.234f, q.y + 67.234f, q.z + 67.234f) : 0.0f);

      ay_x = (wyPos > 0.0f ? wyPos * Noise_Perlin3D(q.x, q.y + step, q.z) : 0.0f) -
             (wyNeg > 0.0f ? wyNeg * Noise_Perlin3D(q.x, q.y - step, q.z) : 0.0f);
      ay_z = (wyPos > 0.0f ? wyPos * Noise_Perlin3D(q.x + 67.234f, q.y + step + 67.234f, q.z + 67.234f) : 0.0f) -
             (wyNeg > 0.0f ? wyNeg * Noise_Perlin3D(q.x + 67.234f, q.y - step + 67.234f, q.z + 67.234f) : 0.0f);

      az_x = (wzPos > 0.0f ? wzPos * Noise_Perlin3D(q.x, q.y, q.z + step) : 0.0f) -
             (wzNeg > 0.0f ? wzNeg * Noise_Perlin3D(q.x, q.y, q.z - step) : 0.0f);
      az_y = (wzPos > 0.0f ? wzPos * Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + step + 31.416f) : 0.0f) -
             (wzNeg > 0.0f ? wzNeg * Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z - step + 31.416f) : 0.0f);
    }

    out = MotionVec_Add(
        out, (Vector3){(ay_z - az_y) * scale,
                       (az_x - ax_z) * scale,
                       (ax_y - ay_x) * scale});
  }
  return out;
}

static inline Vector3 FieldFlow_EvaluateAt(
    const FlowField *flow, const FieldVolume *volume, Vector3 p, float time,
    const MotionPathSample *pathSample)
{
  MotionPathSample localSample;
  if (volume && volume->shape == FIELD_PATH_TUBE && !pathSample)
  {
    if (volume->path.count < 2 || volume->path.count > MOTION_PATH_MAX_POINTS)
      return (Vector3){0};
    localSample = MotionPath_Project(&volume->path, p, 0, volume->path.count - 2);
    pathSample = &localSample;
  }
  float weight = FieldVolume_WeightAt(volume, p, pathSample);
  return FieldFlow_EvaluateWithWeightAt(flow, volume, p, time, pathSample, weight);
}

static inline Vector3 FieldFlow_Evaluate(
    const FlowField *flow, const FieldVolume *volume, Vector3 p, float time)
{
  return FieldFlow_EvaluateAt(flow, volume, p, time, NULL);
}

static inline bool Field_FiniteVector(Vector3 p)
{
  return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static inline Vector3 Field_LimitVector(Vector3 value, float maximum)
{
  float lenSq = MotionVec_Dot(value, value);
  return (lenSq > maximum * maximum && lenSq > 1e-16f)
             ? MotionVec_Scale(value, maximum / sqrtf(lenSq))
             : value;
}

static inline Vector3 Guide_ImplicitForce(
    Vector3 error, Vector3 relativeVelocity, float stiffness, float mass,
    float weight, float dt)
{
  stiffness *= weight;
  float damping = 2.0f * sqrtf(stiffness * mass);
  float denominator = 1.0f + (damping + stiffness * dt) * dt / mass;
  return MotionVec_Scale(
      MotionVec_Sub(MotionVec_Scale(error, stiffness),
                    MotionVec_Scale(relativeVelocity, damping + stiffness * dt)),
      1.0f / denominator);
}

static inline void FieldSample_AddController(
    FieldSample *sample, Vector3 drive, Vector3 sampled, float isotropic,
    float axial, Vector3 axis, float cap, float dt)
{
  if (dt <= 0.0f || sample->controllerCount >= FIELD_MAX_CONTROLLER_RESPONSES)
    return;
  FieldControllerResponse *r = &sample->controllers[sample->controllerCount++];
  r->driveNewtons = drive;
  r->sampledForceNewtons = sampled;
  r->maxForceNewtons = cap;
  r->diagonal = (Vector3){isotropic + axial * axis.x * axis.x,
                          isotropic + axial * axis.y * axis.y,
                          isotropic + axial * axis.z * axis.z};
  r->offDiagonal = (Vector3){axial * axis.x * axis.y,
                             axial * axis.x * axis.z,
                             axial * axis.y * axis.z};
}

static inline Vector3 FieldGuide_RotateOffset(const FieldDesc *d, Vector3 offset,
                                              float age)
{
  if (!d->flow.enabled || d->flow.procedural.swirlSpeedMps == 0.0f)
    return offset;
  Vector3 axis = Field_NormalizeAxisOrUp(d->flow.axis);
  float angle =
      d->flow.procedural.swirlSpeedMps / fmaxf(d->volume.radiusM, 1e-5f) * age;
  float c = cosf(angle), s = sinf(angle);
  return MotionVec_Add(
      MotionVec_Scale(offset, c),
      MotionVec_Add(MotionVec_Scale(MotionVec_Cross(axis, offset), s),
                    MotionVec_Scale(axis, MotionVec_Dot(axis, offset) * (1.0f - c))));
}

static inline Vector3 FieldGuide_RotatePathLane(const FieldDesc *d, Vector3 lane,
                                                float age)
{
  if (!d->rotatePathLanes || !d->flow.enabled)
    return lane;
  float angle =
      d->flow.procedural.swirlSpeedMps / fmaxf(d->volume.radiusM, 1e-5f) * age;
  float c = cosf(angle), s = sinf(angle);
  return (Vector3){lane.x, lane.y * c - lane.z * s, lane.y * s + lane.z * c};
}

/* Struct lưu kết quả hình học đã tính ở Pass 0 để tái sử dụng cho
 * Motion_PhysicalPathLane, Pass 0 (Non-Drag) và Pass 1 (Drag). */
typedef struct FieldPreparedQuery
{
  FieldTransform frame;
  Vector3 worldOffset;
  Vector3 localPos;
  MotionPathSample nearestPath;
  bool hasPathSample;
  float lifetimeWeight;
  float volumeWeight;
  float combinedWeight;
} FieldPreparedQuery;

static inline bool Field_PrepareQueryFast(
    const FieldDesc *d, float age, Vector3 position,
    const FieldTransform *preparedFrame, FieldPreparedQuery *out)
{
  float lifetime = FieldLifetime_Weight(&d->lifetime, age);
  if (lifetime <= 0.0f)
    return false;
  out->frame = preparedFrame ? *preparedFrame : FieldTrajectory_Transform(d, age);
  out->worldOffset = MotionVec_Sub(position, out->frame.position);
  out->localPos = FieldTransform_LocalVector(&out->frame, out->worldOffset);
  out->hasPathSample = false;

  const MotionPathSample *pathPtr = NULL;
  if (d->volume.shape == FIELD_PATH_TUBE)
  {
    if (d->volume.path.count < 2 || d->volume.path.count > MOTION_PATH_MAX_POINTS)
      return false;
    out->nearestPath = MotionPath_Project(&d->volume.path, out->localPos, 0,
                                          d->volume.path.count - 2);
    out->hasPathSample = true;
    pathPtr = &out->nearestPath;
  }

  float volWeight = FieldVolume_WeightFastAt(&d->volume, out->localPos, pathPtr);
  if (volWeight <= 0.0f)
    return false;

  out->lifetimeWeight = lifetime;
  out->volumeWeight = volWeight;
  out->combinedWeight = volWeight * lifetime;
  return out->combinedWeight > 0.0f;
}

/* Đánh giá trực tiếp lực cản (Drag) cho Pass 1 khi đã biết trọng số combinedWeight
 * mà không cần biến đổi lại tọa độ hay chiếu lại hình học. */
static inline void Field_AccumulateDragPassFast(
    const FieldDesc *d, float combinedWeight, int dragLawCount,
    Vector3 velocity, const BodyPhysicalProperties *body,
    const MediumProperties *medium, FieldSample *ioSample)
{
  if (dragLawCount <= 0 || combinedWeight <= 0.0f)
    return;
  Vector3 flow = medium->velocityMps;
  if (ioSample->mediumWeight > 0.0f)
  {
    flow = ioSample->mediumIsAbsolute
               ? ioSample->mediumVelocityMps
               : MotionVec_Add(medium->velocityMps, ioSample->mediumVelocityMps);
  }
  Vector3 relative = MotionVec_Sub(flow, velocity);
  float relSpeed = MotionVec_Length(relative);
  float baseDragCoeff =
      0.5f * medium->densityKgM3 * body->dragCoefficient * body->projectedAreaM2;
  float totalWeight = combinedWeight * (float)dragLawCount;
  float stepDragCoeff = totalWeight * baseDragCoeff;
  Vector3 dragForce = MotionVec_Scale(relative, stepDragCoeff * relSpeed);

  ioSample->hasDragForce = true;
  ioSample->dragForceNewtons = MotionVec_Add(ioSample->dragForceNewtons, dragForce);
  ioSample->forceNewtons = MotionVec_Add(ioSample->forceNewtons, dragForce);

  float prevCoeff = ioSample->dragCoefficientKgPerM;
  float newCoeff = prevCoeff + stepDragCoeff;
  if (prevCoeff <= 0.0f || newCoeff <= 1e-12f)
  {
    ioSample->dragMediumVelocityMps = flow;
  }
  else if (stepDragCoeff > 0.0f)
  {
    ioSample->dragMediumVelocityMps = MotionVec_Scale(
        MotionVec_Add(MotionVec_Scale(ioSample->dragMediumVelocityMps, prevCoeff),
                      MotionVec_Scale(flow, stepDragCoeff)),
        1.0f / newCoeff);
  }
  ioSample->dragCoefficientKgPerM = newCoeff;
}

static inline FieldSample Field_EvaluatePreparedCore(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    bool includeFlow, bool filterDrag, bool dragPass,
    const Vector3 *pathLaneOffset, float dt, const FieldPreparedQuery *q,
    bool validateLaws)
{
  FieldSample out = {0};
  const FieldTransform *t = &q->frame;
  Vector3 p = q->localPos;
  const MotionPathSample *pathSample =
      q->hasPathSample ? &q->nearestPath : NULL;
  float lifetime = q->lifetimeWeight;
  float volWeight = q->volumeWeight;
  float w = q->combinedWeight;

  Vector3 frameVelocityAtPoint = MotionVec_Add(
      t->frameVelocityMps,
      MotionVec_Cross(t->angularVelocityRadPerSec, q->worldOffset));

  Vector3 flow = medium->velocityMps;
  if (includeFlow && d->flow.enabled)
  {
    Vector3 evalFlow = FieldFlow_EvaluateWithWeightAt(
        &d->flow, &d->volume, p, age, pathSample, volWeight);
    Vector3 local = MotionVec_Add(MotionVec_Scale(d->flow.velocityMps, w),
                                  MotionVec_Scale(evalFlow, lifetime));
    flow = MotionVec_Add(MotionVec_Scale(frameVelocityAtPoint, w),
                         FieldTransform_Vector(t, local));
    if (d->flow.addBackgroundVelocity)
      flow = MotionVec_Add(flow, medium->velocityMps);
    out.mediumVelocityMps = flow;
    out.mediumWeight =
        w * (d->flow.blendWeight > 0.0f ? d->flow.blendWeight : 1.0f);
    out.mediumPriority = d->flow.priority;
    out.mediumIsAbsolute = true;
  }

  for (int i = 0; i < d->forceLawCount; ++i)
  {
    ForceLaw law = d->forceLaws[i];
    if (filterDrag && ((law.type == FORCE_LAW_DRAG) != dragPass))
      continue;
    if (validateLaws)
    {
      if (!Field_FiniteVector(law.forceNewtons) ||
          !Field_FiniteVector(law.accelerationMps2) ||
          !Field_FiniteVector(law.center) || !isfinite(law.magnitudeNewtons) ||
          !isfinite(law.springStiffnessNPerM) || !isfinite(law.dampingNsPerM) ||
          !isfinite(law.forwardForceNewtons) || law.forwardForceNewtons < 0.0f)
        return (FieldSample){0};
    }

    if (law.type == FORCE_LAW_DRAG)
      out.hasDragForce = true;
    if (law.type == FORCE_LAW_BUOYANCY)
      out.hasBuoyancyForce = true;

    Vector3 f;
    if (law.type == FORCE_LAW_CURL_FORCE)
    {
      if (validateLaws &&
          (!MotionFlow_IsValid(&law.procedural) || law.magnitudeNewtons < 0.0f))
        return (FieldSample){0};
      float speed = law.procedural.turbulenceSpeedMps;
      FlowField noise = {0};
      noise.procedural = law.procedural;
      noise.procedural.swirlSpeedMps = 0.0f;
      Vector3 curl =
          (speed > 0.0f && law.magnitudeNewtons > 0.0f)
              ? FieldFlow_EvaluateWithWeightAt(&noise, &d->volume, p, age,
                                               pathSample, volWeight)
              : (Vector3){0};
      Vector3 raw = MotionVec_Scale(
          curl, speed > 0.0f ? law.magnitudeNewtons * lifetime / speed : 0.0f);
      f = FieldTransform_Vector(t, Field_LimitVector(raw, law.magnitudeNewtons * w));
    }
    else if (law.type == FORCE_LAW_PATH_GUIDE &&
             d->volume.shape == FIELD_PATH_TUBE)
    {
      MotionPathSample nearest = *pathSample;
      Vector3 tangent = FieldTransform_Vector(t, nearest.tangent);
      Vector3 pathGoal = nearest.position;
      Vector3 rotatedLane = {0};
      if (pathLaneOffset)
      {
        rotatedLane = FieldGuide_RotatePathLane(d, *pathLaneOffset, age);
        pathGoal =
            MotionVec_Add(pathGoal, MotionPath_WorldOffset(nearest, rotatedLane));
      }
      Vector3 toPath = MotionVec_Sub(
          MotionVec_Add(t->position, FieldTransform_Vector(t, pathGoal)),
          position);
      Vector3 normalOffset = MotionVec_Sub(
          toPath, MotionVec_Scale(tangent, MotionVec_Dot(toPath, tangent)));
      Vector3 relativeVelocity = MotionVec_Sub(velocity, frameVelocityAtPoint);
      float tangentSpeed = MotionVec_Dot(relativeVelocity, tangent);
      relativeVelocity = MotionVec_Sub(
          relativeVelocity, MotionVec_Scale(tangent, tangentSpeed));
      if (pathLaneOffset && d->rotatePathLanes && d->flow.enabled)
      {
        Vector3 laneWorld =
            FieldTransform_Vector(t, MotionPath_WorldOffset(nearest, rotatedLane));
        relativeVelocity = MotionVec_Sub(
            relativeVelocity,
            MotionVec_Scale(MotionVec_Cross(tangent, laneWorld),
                            d->flow.procedural.swirlSpeedMps /
                                fmaxf(d->volume.radiusM, 1e-5f)));
      }
      float stiffness =
          law.springStiffnessNPerM > 0.0f
              ? law.springStiffnessNPerM
              : law.magnitudeNewtons / fmaxf(d->volume.radiusM, 1e-5f);
      float mass = body->massKg > 0.0f ? body->massKg : 1.0f;
      Vector3 dampingVelocity = relativeVelocity;
      Vector3 dampingAxis = tangent;
      bool radialDamping = false;
      if (!d->rotatePathLanes && d->flow.enabled &&
          d->flow.procedural.swirlSpeedMps != 0.0f)
      {
        Vector3 swirlRadial = MotionVec_Sub(p, nearest.position);
        swirlRadial = MotionVec_Sub(
            swirlRadial,
            MotionVec_Scale(nearest.tangent,
                            MotionVec_Dot(swirlRadial, nearest.tangent)));
        if (MotionVec_Dot(swirlRadial, swirlRadial) < 1e-10f && pathLaneOffset)
          swirlRadial = MotionPath_WorldOffset(nearest, *pathLaneOffset);
        float radialSq = MotionVec_Dot(swirlRadial, swirlRadial);
        if (radialSq > 1e-10f)
        {
          Vector3 radial = MotionVec_Scale(
              FieldTransform_Vector(t, swirlRadial), 1.0f / sqrtf(radialSq));
          dampingVelocity =
              MotionVec_Scale(radial, MotionVec_Dot(relativeVelocity, radial));
          dampingAxis = radial;
          radialDamping = true;
        }
      }
      Vector3 lateralForce = Guide_ImplicitForce(normalOffset, dampingVelocity,
                                                 stiffness, mass, w, dt);
      float resistance = 2.0f * sqrtf(stiffness * w * mass) + stiffness * w * dt;
      Vector3 lateralDrive =
          MotionVec_Scale(lateralForce, 1.0f + resistance * dt / mass);
      lateralForce = Field_LimitVector(lateralForce, law.magnitudeNewtons * w);
      FieldSample_AddController(
          &out, lateralDrive, lateralForce, radialDamping ? 0.0f : resistance,
          radialDamping ? resistance : -resistance, dampingAxis,
          law.magnitudeNewtons * w, dt);
      Vector3 forwardForce = {0};
      float targetSpeed = d->flow.followSpeedMps;
      if (law.forwardForceNewtons > 0.0f && fabsf(targetSpeed) > 1e-5f)
      {
        float gain = law.forwardForceNewtons / fabsf(targetSpeed);
        float force = Motion_Clamp(
            (targetSpeed - tangentSpeed) * gain / (1.0f + gain * w * dt / mass),
            -law.forwardForceNewtons, law.forwardForceNewtons);
        forwardForce = MotionVec_Scale(tangent, force);
        FieldSample_AddController(
            &out,
            MotionVec_Scale(tangent, (targetSpeed - tangentSpeed) * gain * w),
            MotionVec_Scale(forwardForce, w), 0.0f, gain * w, tangent,
            law.forwardForceNewtons * w, dt);
      }
      f = MotionVec_Add(lateralForce, MotionVec_Scale(forwardForce, w));
    }
    else if (law.type == FORCE_LAW_MOVING_GUIDE)
    {
      float mass = body->massKg > 0.0f ? body->massKg : 1.0f;
      float stiffness =
          law.springStiffnessNPerM > 0.0f
              ? law.springStiffnessNPerM
              : law.magnitudeNewtons / fmaxf(d->volume.radiusM, 1e-5f);
      Vector3 goal = MotionVec_Add(t->position, FieldTransform_Vector(t, law.center));
      Vector3 goalVelocity = frameVelocityAtPoint;
      if (pathLaneOffset)
      {
        Vector3 lane = FieldGuide_RotateOffset(d, *pathLaneOffset, age);
        goal = MotionVec_Add(goal, FieldTransform_Vector(t, lane));
        if (d->flow.enabled && d->flow.procedural.swirlSpeedMps != 0.0f)
        {
          Vector3 axis = Field_NormalizeAxisOrUp(d->flow.axis);
          Vector3 spin = MotionVec_Scale(
              MotionVec_Cross(axis, lane),
              d->flow.procedural.swirlSpeedMps / fmaxf(d->volume.radiusM, 1e-5f));
          goalVelocity =
              MotionVec_Add(goalVelocity, FieldTransform_Vector(t, spin));
        }
      }
      f = Field_LimitVector(
          Guide_ImplicitForce(MotionVec_Sub(goal, position),
                              MotionVec_Sub(velocity, goalVelocity), stiffness,
                              mass, w, dt),
          law.magnitudeNewtons * w);
      float resistance = 2.0f * sqrtf(stiffness * w * mass) + stiffness * w * dt;
      Vector3 drive = MotionVec_Sub(
          MotionVec_Scale(MotionVec_Sub(goal, position), stiffness * w),
          MotionVec_Scale(MotionVec_Sub(velocity, goalVelocity), resistance));
      FieldSample_AddController(&out, drive, f, resistance, 0.0f, (Vector3){0},
                                law.magnitudeNewtons * w, dt);
    }
    else
    {
      /* Chỉ biến đổi đúng vector mà law.type thực sự sử dụng */
      if (law.type == FORCE_LAW_NEWTONS)
        law.forceNewtons = FieldTransform_Vector(t, law.forceNewtons);
      else if (law.type == FORCE_LAW_ACCELERATION)
        law.accelerationMps2 = FieldTransform_Vector(t, law.accelerationMps2);
      else if (law.type == FORCE_LAW_RADIAL_ATTRACTION ||
               law.type == FORCE_LAW_SPRING)
        law.center = MotionVec_Add(t->position, FieldTransform_Vector(t, law.center));

      f = MotionVec_Scale(
          ForceLaw_Evaluate(
              &law, position,
              law.type == FORCE_LAW_SPRING
                  ? MotionVec_Sub(velocity, frameVelocityAtPoint)
                  : velocity,
              body, medium, flow),
          w);
    }
    if (law.type == FORCE_LAW_DRAG)
    {
      out.dragForceNewtons = MotionVec_Add(out.dragForceNewtons, f);
      out.dragMediumVelocityMps = flow;
      out.dragCoefficientKgPerM +=
          w * 0.5f * medium->densityKgM3 * body->dragCoefficient * body->projectedAreaM2;
    }
    if (law.type == FORCE_LAW_ACCELERATION)
      out.accelerationMps2 = MotionVec_Add(out.accelerationMps2, f);
    else
      out.forceNewtons = MotionVec_Add(out.forceNewtons, f);
  }
  return out;
}

static inline FieldSample Field_EvaluateFrameStepPass(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    bool includeFlow, bool filterDrag, bool dragPass,
    const Vector3 *pathLaneOffset, float dt, const FieldTransform *frame)
{
  FieldSample out = {0};
  if (!d || !body || !medium || !isfinite(dt) || dt < 0.0f ||
      d->forceLawCount < 0 || d->forceLawCount > FIELD_MAX_FORCE_LAWS ||
      !isfinite(age) || !isfinite(body->massKg) || body->massKg < 0.0f ||
      !isfinite(body->projectedAreaM2) || body->projectedAreaM2 < 0.0f ||
      !isfinite(body->densityKgM3) || body->densityKgM3 < 0.0f ||
      !isfinite(body->volumeM3) || body->volumeM3 < 0.0f ||
      !isfinite(body->dragCoefficient) || body->dragCoefficient < 0.0f ||
      !isfinite(body->immersionFraction) || body->immersionFraction < 0.0f ||
      body->immersionFraction > 1.0f || !isfinite(medium->densityKgM3) ||
      medium->densityKgM3 < 0.0f || !Field_FiniteVector(position) ||
      !Field_FiniteVector(velocity) || !Field_FiniteVector(medium->velocityMps) ||
      !Field_FiniteVector(medium->gravityMps2))
    return out;

  /* Giữ kiểm tra hợp lệ của volume cho các lệnh gọi trực tiếp từ bên ngoài */
  const FieldVolume *v = &d->volume;
  if (v->shape < FIELD_SPHERE || v->shape > FIELD_PATH_TUBE ||
      !isfinite(v->coreFraction) || v->coreFraction < 0.0f ||
      v->coreFraction >= 1.0f ||
      (v->shape == FIELD_BOX &&
       (!isfinite(v->halfExtentsM.x) || !isfinite(v->halfExtentsM.y) ||
        !isfinite(v->halfExtentsM.z) || v->halfExtentsM.x <= 0.0f ||
        v->halfExtentsM.y <= 0.0f || v->halfExtentsM.z <= 0.0f)) ||
      (v->shape != FIELD_BOX && (!isfinite(v->radiusM) || v->radiusM <= 0.0f)))
    return out;

  FieldPreparedQuery q;
  if (!Field_PrepareQueryFast(d, age, position, frame, &q))
    return out;

  return Field_EvaluatePreparedCore(d, age, position, velocity, body, medium,
                                    includeFlow, filterDrag, dragPass,
                                    pathLaneOffset, dt, &q, true);
}

static inline FieldSample Field_EvaluateStepPass(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    bool includeFlow, bool filterDrag, bool dragPass,
    const Vector3 *pathLaneOffset, float dt)
{
  return Field_EvaluateFrameStepPass(d, age, position, velocity, body, medium,
                                     includeFlow, filterDrag, dragPass,
                                     pathLaneOffset, dt, NULL);
}

static inline FieldSample Field_EvaluatePass(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    bool includeFlow, bool filterDrag, bool dragPass,
    const Vector3 *pathLaneOffset)
{
  return Field_EvaluateStepPass(d, age, position, velocity, body, medium,
                                includeFlow, filterDrag, dragPass,
                                pathLaneOffset, 0);
}

static inline FieldSample Field_EvaluateStep(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    float dt)
{
  return Field_EvaluateStepPass(d, age, position, velocity, body, medium, true,
                                false, false, NULL, dt);
}

static inline FieldSample Field_Evaluate(
    const FieldDesc *d, float age, Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium)
{
  return Field_EvaluatePass(d, age, position, velocity, body, medium, true,
                            false, false, NULL);
}
#endif
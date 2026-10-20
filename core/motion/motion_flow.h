#ifndef CORE_MOTION_FLOW_H
#define CORE_MOTION_FLOW_H
#include "core/force_field.h"
#include "core/motion/motion_path.h"

typedef struct MotionFlowDesc
{
  float turbulenceSpeedMps, swirlSpeedMps;
  float eddyLengthM;
} MotionFlowDesc;

typedef enum
{
  MOTION_FLOW_TUBE,
  MOTION_FLOW_SPHERE
} MotionFlowDomain;

static inline bool MotionFlow_IsValid(const MotionFlowDesc *flow)
{
  return flow && isfinite(flow->turbulenceSpeedMps) &&
         flow->turbulenceSpeedMps >= 0 && isfinite(flow->swirlSpeedMps) &&
         isfinite(flow->eddyLengthM) && flow->eddyLengthM >= 0;
}

static inline float MotionFlow_BandNorm(float x)
{
  x = Motion_Clamp(x, 0.0f, 1.0f);
  return 1.0f - x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}

static inline float MotionFlow_Band(float distance, float radius)
{
  return MotionFlow_BandNorm(distance / radius);
}

static inline Vector3 MotionFlow_Radial(Vector3 p, Vector3 center,
                                        Vector3 axis)
{
  Vector3 d = MotionVec_Sub(p, center);
  return MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
}

static inline float MotionFlow_BandFromDistSq(float distSq, float radiusSq,
                                              float invRadius)
{
  if (distSq >= radiusSq)
    return 0.0f;
  return MotionFlow_BandNorm(sqrtf(distSq) * invRadius);
}

static inline float MotionFlow_BandAt(Vector3 p, Vector3 center, Vector3 axis,
                                      float radius, MotionFlowDomain domain)
{
  Vector3 d = domain == MOTION_FLOW_SPHERE ? MotionVec_Sub(p, center)
                                           : MotionFlow_Radial(p, center, axis);
  float distSq = MotionVec_Dot(d, d);
  if (distSq >= radius * radius)
    return 0.0f;
  return MotionFlow_Band(sqrtf(distSq), radius);
}

static inline Vector3 MotionFlow_Potential(const MotionFlowDesc *flow,
                                           Vector3 p, Vector3 center,
                                           Vector3 axis, float radius,
                                           float time,
                                           MotionFlowDomain domain)
{
  float band = MotionFlow_BandAt(p, center, axis, radius, domain);
  if (band == 0.0f)
    return (Vector3){0};
  float length = flow->eddyLengthM > 0.0f ? flow->eddyLengthM : radius * 0.25f;
  float invLength = 1.0f / length;
  float phase = time * flow->turbulenceSpeedMps * invLength;
  Vector3 q =
      MotionVec_Add(MotionVec_Scale(p, invLength),
                    (Vector3){phase * 0.37f, phase * 0.51f, phase * 0.29f});
  Vector3 noise = {Noise_Perlin3D(q.x, q.y, q.z),
                   Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f),
                   Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f)};
  return MotionVec_Scale(noise, flow->turbulenceSpeedMps * length * band);
}

static inline Vector3 MotionFlow_Evaluate(const MotionFlowDesc *flow, Vector3 p,
                                          Vector3 center, Vector3 direction,
                                          float radius, float time,
                                          MotionFlowDomain domain)
{
  Vector3 result = {0};
  if (!MotionFlow_IsValid(flow) || !isfinite(radius) || radius <= 0.0f)
    return result;
  if (flow->swirlSpeedMps == 0.0f && flow->turbulenceSpeedMps <= 0.0f)
    return result;

  Vector3 axis = MotionVec_Normalize(direction);
  if (MotionVec_Dot(axis, axis) < 0.01f)
    axis = (Vector3){0, 1, 0};

  Vector3 d = MotionVec_Sub(p, center);
  Vector3 radial = MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
  Vector3 dVec = (domain == MOTION_FLOW_SPHERE) ? d : radial;
  float distSq = MotionVec_Dot(dVec, dVec);
  float radiusSq = radius * radius;
  float invRadius = 1.0f / radius;

  if (flow->swirlSpeedMps != 0.0f && distSq < radiusSq)
  {
    result = MotionVec_Scale(MotionVec_Cross(axis, radial),
                             flow->swirlSpeedMps * invRadius *
                                 MotionFlow_BandNorm(sqrtf(distSq) * invRadius));
  }

  if (flow->turbulenceSpeedMps > 0.0f)
  {
    float h = radius * 0.005f;
    float maxReach = radius + h;
    /* Thoát sớm nếu điểm p nằm ngoài vùng bán kính mở rộng (radius + h) */
    if (distSq >= maxReach * maxReach)
      return result;

    float length = flow->eddyLengthM > 0.0f ? flow->eddyLengthM : radius * 0.25f;
    float invLength = 1.0f / length;
    float scale = flow->turbulenceSpeedMps * length;
    float phase = time * flow->turbulenceSpeedMps * invLength;

    float step = h * invLength;
    float qx0 = p.x * invLength + phase * 0.37f;
    float qy0 = p.y * invLength + phase * 0.51f;
    float qz0 = p.z * invLength + phase * 0.29f;

    float qx1 = qx0 + 31.416f, qy1 = qy0 + 31.416f, qz1 = qz0 + 31.416f;
    float qx2 = qx0 + 67.234f, qy2 = qy0 + 67.234f, qz2 = qz0 + 67.234f;

    /* Tính bình phương khoảng cách tại 6 điểm dịch chuyển trực tiếp từ distSq và dVec */
    float hSq = h * h;
    float twoH = 2.0f * h;
    bool isSphere = (domain == MOTION_FLOW_SPHERE);
    float hSqX = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.x * axis.x);
    float hSqY = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.y * axis.y);
    float hSqZ = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.z * axis.z);

    float dSqPx = fmaxf(0.0f, distSq + twoH * dVec.x + hSqX);
    float dSqMx = fmaxf(0.0f, distSq - twoH * dVec.x + hSqX);
    float bpx = MotionFlow_BandFromDistSq(dSqPx, radiusSq, invRadius) * scale;
    float bmx = MotionFlow_BandFromDistSq(dSqMx, radiusSq, invRadius) * scale;
    float ax_y = 0.0f, ax_z = 0.0f;
    if (bpx != 0.0f)
    {
      ax_y += Noise_Perlin3D(qx1 + step, qy1, qz1) * bpx;
      ax_z += Noise_Perlin3D(qx2 + step, qy2, qz2) * bpx;
    }
    if (bmx != 0.0f)
    {
      ax_y -= Noise_Perlin3D(qx1 - step, qy1, qz1) * bmx;
      ax_z -= Noise_Perlin3D(qx2 - step, qy2, qz2) * bmx;
    }

    float dSqPy = fmaxf(0.0f, distSq + twoH * dVec.y + hSqY);
    float dSqMy = fmaxf(0.0f, distSq - twoH * dVec.y + hSqY);
    float bpy = MotionFlow_BandFromDistSq(dSqPy, radiusSq, invRadius) * scale;
    float bmy = MotionFlow_BandFromDistSq(dSqMy, radiusSq, invRadius) * scale;
    float ay_x = 0.0f, ay_z = 0.0f;
    if (bpy != 0.0f)
    {
      ay_x += Noise_Perlin3D(qx0, qy0 + step, qz0) * bpy;
      ay_z += Noise_Perlin3D(qx2, qy2 + step, qz2) * bpy;
    }
    if (bmy != 0.0f)
    {
      ay_x -= Noise_Perlin3D(qx0, qy0 - step, qz0) * bmy;
      ay_z -= Noise_Perlin3D(qx2, qy2 - step, qz2) * bmy;
    }

    float dSqPz = fmaxf(0.0f, distSq + twoH * dVec.z + hSqZ);
    float dSqMz = fmaxf(0.0f, distSq - twoH * dVec.z + hSqZ);
    float bpz = MotionFlow_BandFromDistSq(dSqPz, radiusSq, invRadius) * scale;
    float bmz = MotionFlow_BandFromDistSq(dSqMz, radiusSq, invRadius) * scale;
    float az_x = 0.0f, az_y = 0.0f;
    if (bpz != 0.0f)
    {
      az_x += Noise_Perlin3D(qx0, qy0, qz0 + step) * bpz;
      az_y += Noise_Perlin3D(qx1, qy1, qz1 + step) * bpz;
    }
    if (bmz != 0.0f)
    {
      az_x -= Noise_Perlin3D(qx0, qy0, qz0 - step) * bmz;
      az_y -= Noise_Perlin3D(qx1, qy1, qz1 - step) * bmz;
    }

    result = MotionVec_Add(
        result,
        MotionVec_Scale((Vector3){ay_z - az_y, az_x - ax_z, ax_y - ay_x},
                        1.0f / twoH));
  }
  return result;
}
#endif
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
static inline float MotionFlow_Band(float distance, float radius)
{
  float x = Motion_Clamp(distance / radius, 0.0f, 1.0f);
  return 1.0f - x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}
static inline Vector3 MotionFlow_Radial(Vector3 p, Vector3 center,
                                        Vector3 axis)
{
  Vector3 d = MotionVec_Sub(p, center);
  return MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
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
  Vector3 axis = MotionVec_Normalize(direction);
  if (MotionVec_Dot(axis, axis) < 0.01f)
    axis = (Vector3){0, 1, 0};
  Vector3 radial = MotionFlow_Radial(p, center, axis);
  if (flow->swirlSpeedMps != 0.0f)
  {
    Vector3 dVec = domain == MOTION_FLOW_SPHERE ? MotionVec_Sub(p, center) : radial;
    float distSq = MotionVec_Dot(dVec, dVec);
    if (distSq < radius * radius)
    {
      result = MotionVec_Scale(MotionVec_Cross(axis, radial),
                               flow->swirlSpeedMps / radius *
                                   MotionFlow_Band(sqrtf(distSq), radius));
    }
  }
  if (flow->turbulenceSpeedMps > 0.0f)
  {
    float h = radius * 0.005f;
    float length = flow->eddyLengthM > 0.0f ? flow->eddyLengthM : radius * 0.25f;
    float invLength = 1.0f / length;
    float scale = flow->turbulenceSpeedMps * length;
    float phase = time * flow->turbulenceSpeedMps * invLength;
    Vector3 phaseVec = {phase * 0.37f, phase * 0.51f, phase * 0.29f};

    /* Chỉ tính đúng 2 kênh Perlin cần thiết cho mỗi trục sai phân (12 thay vì 18 lệnh Perlin) */
    Vector3 ppx = {p.x + h, p.y, p.z}, pmx = {p.x - h, p.y, p.z};
    float bpx = MotionFlow_BandAt(ppx, center, axis, radius, domain) * scale;
    float bmx = MotionFlow_BandAt(pmx, center, axis, radius, domain) * scale;
    float ax_y = 0.0f, ax_z = 0.0f;
    if (bpx != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(ppx, invLength), phaseVec);
      ax_y += Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f) * bpx;
      ax_z += Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f) * bpx;
    }
    if (bmx != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(pmx, invLength), phaseVec);
      ax_y -= Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f) * bmx;
      ax_z -= Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f) * bmx;
    }

    Vector3 ppy = {p.x, p.y + h, p.z}, pmy = {p.x, p.y - h, p.z};
    float bpy = MotionFlow_BandAt(ppy, center, axis, radius, domain) * scale;
    float bmy = MotionFlow_BandAt(pmy, center, axis, radius, domain) * scale;
    float ay_x = 0.0f, ay_z = 0.0f;
    if (bpy != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(ppy, invLength), phaseVec);
      ay_x += Noise_Perlin3D(q.x, q.y, q.z) * bpy;
      ay_z += Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f) * bpy;
    }
    if (bmy != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(pmy, invLength), phaseVec);
      ay_x -= Noise_Perlin3D(q.x, q.y, q.z) * bmy;
      ay_z -= Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f) * bmy;
    }

    Vector3 ppz = {p.x, p.y, p.z + h}, pmz = {p.x, p.y, p.z - h};
    float bpz = MotionFlow_BandAt(ppz, center, axis, radius, domain) * scale;
    float bmz = MotionFlow_BandAt(pmz, center, axis, radius, domain) * scale;
    float az_x = 0.0f, az_y = 0.0f;
    if (bpz != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(ppz, invLength), phaseVec);
      az_x += Noise_Perlin3D(q.x, q.y, q.z) * bpz;
      az_y += Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f) * bpz;
    }
    if (bmz != 0.0f)
    {
      Vector3 q = MotionVec_Add(MotionVec_Scale(pmz, invLength), phaseVec);
      az_x -= Noise_Perlin3D(q.x, q.y, q.z) * bmz;
      az_y -= Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f) * bmz;
    }

    result = MotionVec_Add(
        result,
        MotionVec_Scale((Vector3){ay_z - az_y, az_x - ax_z, ax_y - ay_x},
                        1.0f / (2.0f * h)));
  }
  return result;
}
#endif
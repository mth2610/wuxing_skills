#ifndef CORE_MOTION_FLOW_H
#define CORE_MOTION_FLOW_H
#include "core/force_field.h"
#include "core/motion/motion_path.h"
/* Characteristic speeds in m/s, not acceleration or per-frame random kicks.
 * Swirl is signed (negative reverses rotation); turbulence is nonnegative.
 * Geometry sets eddy scale and turnover time. Zero skips each component.
 * This is procedural flow, not a pressure/incompressibility solver. */
typedef struct MotionFlowDesc {
  float turbulenceSpeedMps, swirlSpeedMps;
} MotionFlowDesc;
typedef enum { MOTION_FLOW_TUBE, MOTION_FLOW_SPHERE } MotionFlowDomain;
static inline bool MotionFlow_IsValid(const MotionFlowDesc *flow) {
  return flow && isfinite(flow->turbulenceSpeedMps) &&
         flow->turbulenceSpeedMps >= 0 && isfinite(flow->swirlSpeedMps);
}
static inline float MotionFlow_Band(float distance, float radius) {
  float x = Motion_Clamp(distance / radius, 0, 1);
  return 1 - x * x * x * (x * (6 * x - 15) + 10);
}
static inline Vector3 MotionFlow_Radial(Vector3 p, Vector3 center,
                                        Vector3 axis) {
  Vector3 d = MotionVec_Sub(p, center);
  return MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
}
static inline Vector3 MotionFlow_Potential(const MotionFlowDesc *flow,
                                           Vector3 p, Vector3 center,
                                           Vector3 axis, float radius,
                                           float time,
                                           MotionFlowDomain domain) {
  Vector3 d = domain == MOTION_FLOW_SPHERE ? MotionVec_Sub(p, center)
                                           : MotionFlow_Radial(p, center, axis);
  float band = MotionFlow_Band(MotionVec_Length(d), radius);
  if (band == 0)
    return (Vector3){0};
  float length = radius * 0.25f;
  float phase = time * flow->turbulenceSpeedMps / length;
  Vector3 q =
      MotionVec_Add(MotionVec_Scale(p, 1 / length),
                    (Vector3){phase * 0.37f, phase * 0.51f, phase * 0.29f});
  /* Vector potential has dimensions m^2/s; its spatial curl is m/s.
   * Window the potential before taking curl (windowing velocity adds sinks).
   * Same decorrelated Perlin components as the engine's curl primitive. */
  Vector3 noise = {Noise_Perlin3D(q.x, q.y, q.z),
                   Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f),
                   Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f)};
  return MotionVec_Scale(noise, flow->turbulenceSpeedMps * length * band);
}
/* Spatially smooth, deterministic velocity. A fixed sphere/axis domain has a
 * divergence-free curl component to finite-difference precision. A guide's
 * steering, curved coordinate frame and radius profile are separate terms. */
static inline Vector3 MotionFlow_Evaluate(const MotionFlowDesc *flow, Vector3 p,
                                          Vector3 center, Vector3 direction,
                                          float radius, float time,
                                          MotionFlowDomain domain) {
  Vector3 result = {0};
  if (!MotionFlow_IsValid(flow) || !isfinite(radius) || radius <= 0)
    return result;
  Vector3 axis = MotionVec_Normalize(direction);
  if (MotionVec_Length(axis) < 0.1f)
    axis = (Vector3){0, 1, 0};
  Vector3 radial = MotionFlow_Radial(p, center, axis);
  if (flow->swirlSpeedMps != 0) {
    float distance = domain == MOTION_FLOW_SPHERE
                         ? MotionVec_Length(MotionVec_Sub(p, center))
                         : MotionVec_Length(radial);
    /* Solid-body core: v=omega cross r, finite and zero on the axis. */
    result = MotionVec_Scale(MotionVec_Cross(axis, radial),
                             flow->swirlSpeedMps / radius *
                                 MotionFlow_Band(distance, radius));
  }
  if (flow->turbulenceSpeedMps > 0) {
    float h = radius * 0.005f;
    Vector3 dx = {h, 0, 0}, dy = {0, h, 0}, dz = {0, 0, h};
    Vector3 ax =
        MotionVec_Sub(MotionFlow_Potential(flow, MotionVec_Add(p, dx), center,
                                           axis, radius, time, domain),
                      MotionFlow_Potential(flow, MotionVec_Sub(p, dx), center,
                                           axis, radius, time, domain));
    Vector3 ay =
        MotionVec_Sub(MotionFlow_Potential(flow, MotionVec_Add(p, dy), center,
                                           axis, radius, time, domain),
                      MotionFlow_Potential(flow, MotionVec_Sub(p, dy), center,
                                           axis, radius, time, domain));
    Vector3 az =
        MotionVec_Sub(MotionFlow_Potential(flow, MotionVec_Add(p, dz), center,
                                           axis, radius, time, domain),
                      MotionFlow_Potential(flow, MotionVec_Sub(p, dz), center,
                                           axis, radius, time, domain));
    result = MotionVec_Add(
        result,
        MotionVec_Scale((Vector3){ay.z - az.y, az.x - ax.z, ax.y - ay.x},
                        1 / (2 * h)));
  }
  return result;
}
#endif

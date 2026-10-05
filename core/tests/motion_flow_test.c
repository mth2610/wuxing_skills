/* Exercises the production field registry without rendering. */
#include "core/motion/motion_fields.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "raylib.h"
#include "raymath.h"

static inline Vector3 Vector3Add(Vector3 a, Vector3 b) {
  return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vector3 Vector3Subtract(Vector3 a, Vector3 b) {
  return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline Vector3 Vector3Scale(Vector3 v, float scale) {
  return (Vector3){v.x * scale, v.y * scale, v.z * scale};
}

static inline float Vector3Length(Vector3 v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float Vector3LengthSqr(Vector3 v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
  return (Vector3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x};
}

static inline float Vector3DotProduct(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

#include "core/force_field.c"
#include "core/motion/motion_body.h"
#include "core/motion/motion_flow.h"
static int failed;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    if (!(c)) {                                                                \
      printf("FAIL: %s\n", m);                                                 \
      failed++;                                                                \
    } else                                                                     \
      printf("PASS: %s\n", m);                                                 \
  } while (0)
int main(void) {
  MotionFlowDesc flow = {0};
  Vector3 p = {0.3f, 0.2f, 0.4f}, origin = {0}, axis = {0, 1, 0};
  CHECK(MotionVec_Length(MotionFlow_Evaluate(&flow, p, origin, axis, 2, 1,
                                             MOTION_FLOW_TUBE)) == 0,
        "zero turbulence and swirl produce exactly zero disturbance");
  flow.swirlSpeedMps = 2;
  Vector3 v =
      MotionFlow_Evaluate(&flow, p, origin, axis, 2, 1, MOTION_FLOW_TUBE);
  CHECK(fabsf(MotionVec_Dot(v, axis)) < 1e-5f &&
            fabsf(MotionVec_Dot(v, p)) < 1e-5f,
        "swirl is tangent to the axis and radial offset");
  CHECK(MotionVec_Length(MotionFlow_Evaluate(&flow, origin, origin, axis, 2, 1,
                                             MOTION_FLOW_TUBE)) == 0,
        "solid-body vortex core has no central singularity");
  flow.swirlSpeedMps = 0;
  flow.turbulenceSpeedMps = 1;
  v = MotionFlow_Evaluate(&flow, p, origin, axis, 2, 1, MOTION_FLOW_SPHERE);
  Vector3 same =
      MotionFlow_Evaluate(&flow, p, origin, axis, 2, 1, MOTION_FLOW_SPHERE);
  CHECK(MotionVec_Length(v) > 0.001f &&
            MotionVec_Length(MotionVec_Sub(v, same)) == 0,
        "spatial curl flow is nonzero and repeatable without RNG state");
  Vector3 next =
      MotionFlow_Evaluate(&flow, MotionVec_Add(p, (Vector3){0.0001f, 0, 0}),
                          origin, axis, 2, 1.0001f, MOTION_FLOW_SPHERE);
  CHECK(MotionVec_Length(MotionVec_Sub(v, next)) < 0.01f,
        "flow is continuous across space and time");
  CHECK(MotionVec_Length(MotionFlow_Evaluate(&flow, (Vector3){4, 0, 0}, origin,
                                             axis, 2, 1, MOTION_FLOW_SPHERE)) ==
            0,
        "target curl potential is compactly supported");
  p = (Vector3){1.8f, 0.2f, 0.4f};
  float h = 0.005f * 2, div = 0;
  for (int i = 0; i < 3; i++) {
    Vector3 delta = {0};
    if (i == 0)
      delta.x = h;
    else if (i == 1)
      delta.y = h;
    else
      delta.z = h;
    Vector3 a = MotionFlow_Evaluate(&flow, MotionVec_Add(p, delta), origin,
                                    axis, 2, 1, MOTION_FLOW_SPHERE);
    Vector3 b = MotionFlow_Evaluate(&flow, MotionVec_Sub(p, delta), origin,
                                    axis, 2, 1, MOTION_FLOW_SPHERE);
    div += MotionVec_Dot(MotionVec_Sub(a, b), delta) / (2 * h * h);
  }
  CHECK(fabsf(div) < 0.002f, "modulating the potential preserves discrete curl "
                             "divergence near a target boundary");
  ParticleDynamicsProfile body = {.inverseMassKg = 250,
                                  .gravityScale = 1,
                                  .densityKgM3 = 600,
                                  .airDensityKgM3 = 1.2f,
                                  .aerodynamicAreaM2 = 0.01f,
                                  .aerodynamicDragCoefficient = 1,
                                  .windSusceptibility = 1};
  float terminal = sqrtf(-ParticleDynamics_GravityAcceleration(&body) / 1.5f);
  v = MotionBody_AdvanceVelocity((Vector3){0, -terminal, 0}, &body,
                                 (Vector3){0}, (Vector3){0}, (Vector3){0},
                                 0.25f);
  CHECK(fabsf(v.y + terminal) < 0.0001f,
        "combined gravity and quadratic drag preserve the physical terminal "
        "equilibrium at large dt");
  printf("motion flow: %s\n", failed ? "FAIL" : "PASS");
  return failed != 0;
}

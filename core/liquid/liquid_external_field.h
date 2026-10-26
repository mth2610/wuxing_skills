#ifndef CORE_LIQUID_EXTERNAL_FIELD_H
#define CORE_LIQUID_EXTERNAL_FIELD_H
#include "core/motion/physical_field.h"
#include <math.h>

/* Optional solver-owned boundary. A solver samples world-space external fields
 * once at its velocity prediction stage, before pressure/density/neighbour
 * constraints. This API never advances position, resolves contacts, or supplies
 * incompressibility, cohesion or dynamic viscosity. SSF is rendering only.
 * Existing CPU/GPU liquid solvers do not opt into this callback automatically;
 * GPU use requires a matching packed sampler or an explicit CPU fallback. */
typedef void (*LiquidExternalFieldSampler)(Vector3 position, Vector3 velocity,
    const BodyPhysicalProperties *body, const MediumProperties *medium,
    const ReceiverConstraints *constraints, float dt, void *userData,
    FieldSample *outSample);

/* mediumVelocityMps remains a velocity channel. A solver needing aerodynamic
 * response asks for an explicit drag law; this boundary never adds u to a. */
static inline Vector3 LiquidExternalField_SampleAcceleration(
    LiquidExternalFieldSampler sampler, void *userData, Vector3 position,
    Vector3 velocity, const BodyPhysicalProperties *body,
    const MediumProperties *medium, float dt, FieldSample *outSample)
{
    FieldSample sample = {0};
    ReceiverConstraints constraints = {.mode = RECEIVER_FREE, .permittedAxes = {1.0f, 1.0f, 1.0f}};
    if (sampler && body && medium && dt > 0.0f)
        sampler(position, velocity, body, medium, &constraints, dt, userData, &sample);
    if (outSample) *outSample = sample;
    float inverseMass = (body && body->massKg > 1e-6f) ? (1.0f / body->massKg) : 0.0f;
    Vector3 total = MotionVec_Add(sample.accelerationMps2,
                                  MotionVec_Scale(sample.forceNewtons, inverseMass));
    if (!isfinite(total.x) || !isfinite(total.y) || !isfinite(total.z))
        return (Vector3){0.0f, 0.0f, 0.0f};
    return total;
}
#endif

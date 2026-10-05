#include "core/motion/motion_fields.h"
#include "core/wind/wind_system.h"
#include <stdlib.h>
#include <string.h>
#ifndef PI
#define PI 3.14159265358979323846f
#endif

typedef struct {
  MotionGuideDesc desc;
  MotionFieldHandle handle;
  float age, distance;
  bool fired;
} MotionGuideRuntime;
typedef struct {
  MotionTargetDesc desc;
  MotionFieldHandle handle;
  float age;
} MotionTargetRuntime;
static MotionGuideRuntime s_guides[MOTION_FIELDS_MAX_GUIDES];
static MotionTargetRuntime s_targets[MOTION_FIELDS_MAX_TARGETS];
static uint32_t s_generation;
static float s_time;
static void Motion_Trace(const char *event, MotionFieldHandle handle,
                         int detail) {
#ifndef CORE_HEADLESS_TEST
  const char *trace = getenv("WUXING_MOTION_TRACE");
  if (trace && trace[0] && trace[0] != '0')
    TraceLog(LOG_INFO, "[MOTION] %s handle=%u detail=%d guides=%d targets=%d",
             event, handle, detail, MotionFields_GetGuideCount(),
             MotionFields_GetTargetCount());
#else
  (void)event;
  (void)handle;
  (void)detail;
#endif
}
static MotionFieldHandle Motion_NewHandle(int slot) {
  s_generation = (s_generation + 1) & 0x00ffffffu;
  if (!s_generation)
    s_generation = 1;
  return (s_generation << 8) | (uint32_t)(slot + 1);
}
static MotionGuideRuntime *Motion_FindGuide(MotionFieldHandle h) {
  int i = (int)(h & 255u) - 1;
  return h && i >= 0 && i < MOTION_FIELDS_MAX_GUIDES && s_guides[i].handle == h
             ? &s_guides[i]
             : NULL;
}
static MotionTargetRuntime *Motion_FindTarget(MotionFieldHandle h) {
  int i = (int)(h & 255u) - 1 - MOTION_FIELDS_MAX_GUIDES;
  return h && i >= 0 && i < MOTION_FIELDS_MAX_TARGETS &&
                 s_targets[i].handle == h
             ? &s_targets[i]
             : NULL;
}
static float Motion_Envelope(float age, float duration, float attack,
                             float fade) {
  float w = 1;
  if (attack > 0)
    w *= Motion_Clamp(age / attack, 0, 1);
  if (fade > 0)
    w *= Motion_Clamp((duration - age) / fade, 0, 1);
  return w * w * (3 - 2 * w);
}
static float Motion_Profile(float start, float end, float progress) {
  return (start > 0 ? start : 1) * (1 - progress) +
         (end > 0 ? end : 1) * progress;
}
static float Motion_GuideRadius(const MotionGuideDesc *d, float distance) {
  return d->radius *
         Motion_Profile(d->radiusScaleStart, d->radiusScaleEnd,
                        Motion_Clamp(distance / d->path.length, 0, 1));
}
static float Motion_GuideWeight(const MotionGuideRuntime *g, Vector3 p,
                                const MotionReceiver *r) {
  if (r->arrived)
    return 1;
  MotionPathSample f =
      MotionPath_Project(&g->desc.path, p, r->segment, r->segment + 2);
  if (g->desc.formation == MOTION_FORMATION_SHELL)
    f = MotionPath_Sample(&g->desc.path, g->distance);
  float radius = Motion_GuideRadius(&g->desc, f.distance);
  const float core = 0.65f;
  float x = Motion_Clamp(
      (MotionVec_Length(MotionVec_Sub(p, f.position)) / radius - core) /
          (1 - core),
      0, 1);
  float weight = 1 - x * x * (3 - 2 * x);
  if (g->desc.mode == MOTION_GUIDE_PULSE &&
      g->desc.formation == MOTION_FORMATION_STREAM) {
    float edge = fabsf(f.distance - g->distance) /
                 fmaxf(g->desc.pulseLength * 0.5f, 0.005f);
    x = Motion_Clamp((edge - core) / (1 - core), 0, 1);
    weight *= 1 - x * x * (3 - 2 * x);
  }
  return weight;
}
MotionGuideDesc MotionGuide_Default(void) {
  MotionGuideDesc d = {0};
  d.duration = 4;
  d.radius = 1.5f;
  d.pulseLength = 2;
  d.speed = 4;
  d.maxForceNewtons = 0.32f;
  d.receiverMask = MOTION_RECEIVER_ALL;
  d.arrival.radius = 0.25f;
  d.arrival.orbitRadius = 0.8f;
  d.arrival.orbitAxis = (Vector3){0, 1, 0};
  return d;
}
MotionTargetDesc MotionTarget_Default(void) {
  MotionTargetDesc d = {0};
  d.duration = 2;
  d.radius = 3;
  d.receiverMask = MOTION_RECEIVER_ALL;
  return d;
}
void MotionFields_Reset(void) {
  memset(s_guides, 0, sizeof(s_guides));
  memset(s_targets, 0, sizeof(s_targets));
  s_time = 0;
}
void MotionFields_Update(float dt) {
  if (!isfinite(dt) || dt <= 0)
    return;
  s_time += dt;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; i++)
    if (s_guides[i].handle) {
      s_guides[i].distance =
          fminf(s_guides[i].desc.path.length,
                s_guides[i].distance +
                    dt * s_guides[i].desc.speed *
                        Motion_Profile(s_guides[i].desc.speedScaleStart,
                                       s_guides[i].desc.speedScaleEnd,
                                       s_guides[i].distance /
                                           s_guides[i].desc.path.length));
      s_guides[i].age += dt;
      if (s_guides[i].age >= s_guides[i].desc.duration)
        s_guides[i].handle = 0;
    }
  for (int i = 0; i < MOTION_FIELDS_MAX_TARGETS; i++)
    if (s_targets[i].handle) {
      s_targets[i].age += dt;
      if (s_targets[i].age >= s_targets[i].desc.duration)
        s_targets[i].handle = 0;
    }
}
static bool Motion_FiniteVector(Vector3 v) {
  return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static bool Motion_ValidTarget(const MotionTargetDesc *d) {
  return d && isfinite(d->duration) && d->duration > 0 &&
         isfinite(d->radius) && d->radius > 0 &&
         d->field.layerCount >= 0 && d->field.layerCount <= FORCE_FIELD_MAX_LAYERS &&
         Motion_FiniteVector(d->center) && Motion_FiniteVector(d->flowAxis) &&
         MotionFlow_IsValid(&d->flow) && isfinite(d->airflow.strength) &&
         Motion_FiniteVector(d->airflow.direction) &&
         isfinite(d->airflow.inwardPull) &&
         d->airflow.type >= VORTICLE_LINEAR_GUST &&
         d->airflow.type <= VORTICLE_TURBULENCE &&
         isfinite(d->attackTime) && d->attackTime >= 0 &&
         isfinite(d->fadeTime) && d->fadeTime >= 0;
}
MotionFieldHandle MotionFields_CreateGuide(const MotionGuideDesc *d) {
  if (!d || !isfinite(d->duration) || d->duration <= 0 ||
      !isfinite(d->radius) || d->radius <= 0 || !isfinite(d->speed) ||
      d->speed <= 0 || !isfinite(d->maxForceNewtons) ||
      d->maxForceNewtons <= 0 || d->field.layerCount < 0 ||
      d->field.layerCount > FORCE_FIELD_MAX_LAYERS ||
      d->formation < MOTION_FORMATION_STREAM ||
      d->formation > MOTION_FORMATION_SHELL ||
      d->mode < MOTION_GUIDE_SUSTAINED || d->mode > MOTION_GUIDE_PULSE ||
      !isfinite(d->pulseLength) || d->pulseLength <= 0 ||
      !isfinite(d->radiusScaleStart) || d->radiusScaleStart < 0 ||
      !isfinite(d->radiusScaleEnd) || d->radiusScaleEnd < 0 ||
      !isfinite(d->speedScaleStart) || d->speedScaleStart < 0 ||
      !isfinite(d->speedScaleEnd) || d->speedScaleEnd < 0 ||
      !MotionFlow_IsValid(&d->flow) || !MotionFlow_IsValid(&d->arrival.flow) ||
      !Motion_FiniteVector(d->arrival.orbitAxis) ||
      !Motion_FiniteVector(d->arrival.impulseNs) ||
      !isfinite(d->arrival.orbitRadius) || d->arrival.orbitRadius < 0 ||
      !isfinite(d->attackTime) || d->attackTime < 0 || !isfinite(d->fadeTime) ||
      d->fadeTime < 0 || d->arrival.targetCount < 0 ||
      d->arrival.targetCount > MOTION_ARRIVAL_MAX_TARGETS ||
      d->arrival.mode < MOTION_ARRIVAL_RELEASE ||
      d->arrival.mode > MOTION_ARRIVAL_ORBIT || !isfinite(d->arrival.radius) ||
      d->arrival.radius <= 0)
    return 0;
  for (int i = 0; i < d->arrival.targetCount; i++)
    if (!Motion_ValidTarget(&d->arrival.targets[i]))
      return 0;
  MotionPath checked;
  if (!MotionPath_Build(&checked, d->path.points, d->path.count))
    return 0;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; i++)
    if (!s_guides[i].handle) {
      s_guides[i] =
          (MotionGuideRuntime){.desc = *d, .handle = Motion_NewHandle(i)};
      s_guides[i].desc.path = checked;
      if (!s_guides[i].desc.receiverMask)
        s_guides[i].desc.receiverMask = MOTION_RECEIVER_ALL;
      Motion_Trace("guide_created", s_guides[i].handle, (int)d->mode);
      return s_guides[i].handle;
    }
  return 0;
}
MotionFieldHandle MotionFields_CreateTarget(const MotionTargetDesc *d) {
  if (!Motion_ValidTarget(d))
    return 0;
  for (int i = 0; i < MOTION_FIELDS_MAX_TARGETS; i++)
    if (!s_targets[i].handle) {
      s_targets[i] = (MotionTargetRuntime){
          .desc = *d, .handle = Motion_NewHandle(MOTION_FIELDS_MAX_GUIDES + i)};
      if (!s_targets[i].desc.receiverMask)
        s_targets[i].desc.receiverMask = MOTION_RECEIVER_ALL;
      Motion_Trace(
          "target_created", s_targets[i].handle,
          (d->field.layerCount > 0 ? 1 : 0) |
              (d->airflow.strength != 0 ? 2 : 0) |
              (d->flow.swirlSpeedMps != 0 || d->flow.turbulenceSpeedMps > 0
                   ? 4
                   : 0));
      return s_targets[i].handle;
    }
  return 0;
}
bool MotionFields_IsAlive(MotionFieldHandle h) {
  return Motion_FindGuide(h) || Motion_FindTarget(h);
}
void MotionFields_Stop(MotionFieldHandle h) {
  MotionGuideRuntime *g = Motion_FindGuide(h);
  MotionTargetRuntime *t = Motion_FindTarget(h);
  if (g)
    g->handle = 0;
  if (t)
    t->handle = 0;
}
int MotionFields_GetGuideCount(void) {
  int n = 0;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; i++)
    n += s_guides[i].handle != 0;
  return n;
}
int MotionFields_GetTargetCount(void) {
  int n = 0;
  for (int i = 0; i < MOTION_FIELDS_MAX_TARGETS; i++)
    n += s_targets[i].handle != 0;
  return n;
}
static bool Motion_InGuide(const MotionGuideRuntime *g, Vector3 p,
                           MotionPathSample *projection) {
  *projection = MotionPath_Project(&g->desc.path, p, 0, g->desc.path.count - 2);
  if (MotionVec_Length(MotionVec_Sub(p, projection->position)) >
      Motion_GuideRadius(&g->desc, projection->distance))
    return false;
  if (g->desc.formation == MOTION_FORMATION_SHELL) {
    MotionPathSample centre = MotionPath_Sample(&g->desc.path, g->distance);
    if (MotionVec_Length(MotionVec_Sub(p, centre.position)) >
        Motion_GuideRadius(&g->desc, centre.distance))
      return false;
  }
  if (g->desc.mode == MOTION_GUIDE_PULSE) {
    float centre = Motion_Clamp(g->distance, 0, g->desc.path.length);
    if (fabsf(projection->distance - centre) >
        fmaxf(g->desc.pulseLength, 0.01f) * 0.5f)
      return false;
  }
  return true;
}
bool MotionFields_Capture(MotionFieldHandle h, Vector3 p, MotionReceiver *r) {
  MotionGuideRuntime *g = Motion_FindGuide(h);
  if (!g || !r)
    return false;
  MotionPathSample f =
      MotionPath_Project(&g->desc.path, p, 0, g->desc.path.count - 2);
  if (g->desc.formation == MOTION_FORMATION_SHELL)
    f = MotionPath_Sample(&g->desc.path,
                          Motion_Clamp(g->distance, 0, g->desc.path.length));
  *r = (MotionReceiver){
      .guide = h, .distance = f.distance, .segment = f.segment};
  if (g->desc.formation == MOTION_FORMATION_SHELL)
    r->localOffset =
        MotionPath_LocalOffset(f, MotionVec_Limit(MotionVec_Sub(p, f.position),
                                                  g->desc.radius * 0.9f));
  if (g->desc.formation == MOTION_FORMATION_STREAM &&
      g->desc.preserveStreamLanes) {
    r->localOffset = MotionPath_LocalOffset(
        f, MotionVec_Limit(MotionVec_Sub(p, f.position),
                           Motion_GuideRadius(&g->desc, f.distance) * 0.6f));
    r->localOffset.x = 0;
  }
  return true;
}
static Vector3 Motion_RotateLane(MotionPathSample f, Vector3 local,
                                 const MotionFlowDesc *flow, float radius,
                                 float age) {
  float radial = sqrtf(local.y * local.y + local.z * local.z);
  float angle =
      flow->swirlSpeedMps / radius * MotionFlow_Band(radial, radius) * age;
  float c = cosf(angle), sn = sinf(angle);
  return MotionPath_WorldOffset(f,
                                (Vector3){local.x, local.y * c - local.z * sn,
                                          local.y * sn + local.z * c});
}
static Vector3 Motion_GuideForce(MotionGuideRuntime *g, Vector3 p, Vector3 v,
                                 float massKg, float dt, MotionReceiver *r) {
  MotionGuideDesc *d = &g->desc;
  if (r->arrived && (d->arrival.mode == MOTION_ARRIVAL_RELEASE ||
                     d->arrival.mode == MOTION_ARRIVAL_DESTROY))
    return (Vector3){0};
  r->flowTime += dt;
  MotionPathSample f;
  Vector3 goal, desired, feedForward = {0};
  bool transverseOnly = false;
  float radius;
  if (r->arrived) {
    f = MotionPath_Sample(&d->path, d->path.length);
    radius = Motion_GuideRadius(d, f.distance);
    Vector3 axis = MotionVec_Normalize(d->arrival.orbitAxis);
    if (MotionVec_Length(axis) < 0.1f)
      axis = (Vector3){0, 1, 0};
    Vector3 initial = MotionPath_WorldOffset(f, r->localOffset);
    Vector3 radialOffset = MotionFlow_Radial(initial, (Vector3){0}, axis);
    float omega = d->arrival.flow.swirlSpeedMps / radius *
                  MotionFlow_Band(MotionVec_Length(initial), radius);
    float angle = omega * r->flowTime;
    Vector3 offset = MotionVec_Add(
        MotionVec_Sub(initial, radialOffset),
        MotionVec_Add(MotionVec_Scale(radialOffset, cosf(angle)),
                      MotionVec_Scale(MotionVec_Cross(axis, radialOffset), sinf(angle))));
    feedForward = MotionVec_Scale(
        MotionFlow_Radial(offset, (Vector3){0}, axis), -massKg * omega * omega);
    goal = MotionVec_Add(f.position, offset);
    desired = MotionFlow_Evaluate(&d->arrival.flow, p, f.position,
                                  d->arrival.orbitAxis, radius, s_time,
                                  MOTION_FLOW_SPHERE);
    if (d->arrival.mode == MOTION_ARRIVAL_ORBIT) {
      Vector3 axis = MotionVec_Normalize(d->arrival.orbitAxis);
      if (MotionVec_Length(axis) < 0.1f)
        axis = (Vector3){0, 1, 0};
      Vector3 radial = MotionFlow_Radial(p, f.position, axis);
      if (MotionVec_Length(radial) < 0.001f)
        radial = MotionPath_TransportNormal((Vector3){1, 0, 0}, axis, axis);
      radial = MotionVec_Normalize(radial);
      float orbitRadius = fmaxf(d->arrival.orbitRadius, 0.05f);
      goal = MotionVec_Add(f.position, MotionVec_Scale(radial, orbitRadius));
      MotionFlowDesc noise = d->arrival.flow;
      noise.swirlSpeedMps = 0;
      desired =
          MotionVec_Add(MotionVec_Scale(MotionVec_Cross(axis, radial),
                                        d->arrival.flow.swirlSpeedMps),
                        MotionFlow_Evaluate(&noise, p, f.position, axis, radius,
                                            s_time, MOTION_FLOW_SPHERE));
      feedForward = MotionVec_Scale(
          radial, -massKg * d->arrival.flow.swirlSpeedMps *
                      d->arrival.flow.swirlSpeedMps / orbitRadius);
    }
  } else {
    f = MotionPath_Project(&d->path, p, r->segment, r->segment + 2);
    r->distance = fmaxf(r->distance, f.distance);
    r->segment = f.segment;
    float s = d->formation == MOTION_FORMATION_SHELL
                  ? g->distance
                  : r->distance + d->radius * 0.25f;
    f = MotionPath_Sample(&d->path, s);
    radius = Motion_GuideRadius(d, f.distance);
    Vector3 offset =
        Motion_RotateLane(f, r->localOffset, &d->flow, radius, r->flowTime);
    goal = MotionVec_Add(f.position, offset);
    float speed =
        d->speed * Motion_Profile(d->speedScaleStart, d->speedScaleEnd,
                                  f.distance / d->path.length);
    if (f.distance >= d->path.length - 0.01f)
      speed = 0;
    desired =
        MotionVec_Add(MotionVec_Scale(f.tangent, speed),
                      MotionFlow_Evaluate(&d->flow, p, f.position, f.tangent,
                                          radius, s_time, MOTION_FLOW_TUBE));
    transverseOnly = d->formation == MOTION_FORMATION_STREAM &&
                     f.distance < d->path.length - 0.01f;
    if (d->flow.swirlSpeedMps != 0 && MotionVec_Length(offset) > 0) {
      Vector3 radial = MotionFlow_Radial(MotionVec_Add(f.position, offset),
                                         f.position, f.tangent);
      float omega = d->flow.swirlSpeedMps / radius *
                    MotionFlow_Band(MotionVec_Length(radial), radius);
      feedForward = MotionVec_Scale(radial, -massKg * omega * omega);
    }
  }
  /* Design stiffness from force budget/tube width; this is an actuator, not
   * a natural material constant. Physical damping c=2*sqrt(k*m). */
  float stiffness = d->maxForceNewtons / fmaxf(radius * 0.25f, 0.001f);
  float damping = 2 * sqrtf(stiffness * massKg);
  Vector3 error = MotionVec_Sub(goal, p), dv = MotionVec_Sub(desired, v);
  Vector3 along = {0};
  if (transverseOnly) {
    error = MotionVec_Sub(
        error, MotionVec_Scale(f.tangent, MotionVec_Dot(error, f.tangent)));
    Vector3 tangentDv =
        MotionVec_Scale(f.tangent, MotionVec_Dot(dv, f.tangent));
    along = MotionVec_Scale(tangentDv, damping / (1 + damping * dt / massKg));
    dv = MotionVec_Sub(dv, tangentDv);
  }
  Vector3 force = MotionVec_Scale(
      MotionVec_Add(
          MotionVec_Add(
              MotionVec_Scale(MotionVec_Add(error, MotionVec_Scale(dv, dt)),
                              stiffness),
              MotionVec_Scale(dv, damping)),
          feedForward),
      1 / (1 + damping * dt / massKg + stiffness * dt * dt / massKg));
  force = MotionVec_Add(force, along);
  return MotionVec_Scale(
      MotionVec_Limit(force, d->maxForceNewtons),
      Motion_Envelope(g->age, d->duration, d->attackTime, d->fadeTime) *
          Motion_GuideWeight(g, p, r));
}
void MotionFields_Sample(Vector3 p, Vector3 v, float massKg, float dt,
                         unsigned int mask, MotionReceiver *r,
                         MotionFieldSample *out) {
  if (!out)
    return;
  *out = (MotionFieldSample){0};
  if (!isfinite(massKg) || massKg <= 0 || !isfinite(dt) || dt < 0)
    return;
  MotionReceiver local = {0};
  if (!r)
    r = &local;
  MotionGuideRuntime *bound = Motion_FindGuide(r->guide);
  if (!bound || !(bound->desc.receiverMask & mask)) {
    *r = (MotionReceiver){0};
    bound = NULL;
  }
  if (bound && !r->arrived) {
    MotionPathSample f =
        MotionPath_Project(&bound->desc.path, p, r->segment, r->segment + 2);
    if (MotionVec_Length(MotionVec_Sub(p, f.position)) >
        Motion_GuideRadius(&bound->desc, f.distance)) {
      *r = (MotionReceiver){0};
      bound = NULL;
    }
  }
  if (bound && !r->arrived && bound->desc.mode == MOTION_GUIDE_PULSE &&
      bound->desc.formation != MOTION_FORMATION_SHELL) {
    MotionPathSample f;
    if (!Motion_InGuide(bound, p, &f)) {
      *r = (MotionReceiver){0};
      bound = NULL;
    }
  }
  if (!bound)
    for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; i++) {
      MotionGuideRuntime *g = &s_guides[i];
      MotionPathSample f;
      if (g->handle && (g->desc.receiverMask & mask) &&
          Motion_InGuide(g, p, &f)) {
        MotionFields_Capture(g->handle, p, r);
        bound = g;
        break;
      }
    }
  if (bound) {
    out->forceNewtons = Motion_GuideForce(bound, p, v, massKg, dt, r);
    out->captured = !r->arrived;
    if (bound->desc.field.layerCount > 0 && !r->arrived) {
      MotionPathSample f = MotionPath_Sample(&bound->desc.path, r->distance);
      ForceField field = bound->desc.field;
      for (int j = 0; j < field.layerCount; j++) {
        field.layers[j].origin = MotionVec_Add(
            f.position, MotionPath_WorldOffset(f, field.layers[j].origin));
        field.layers[j].direction =
            MotionPath_WorldOffset(f, field.layers[j].direction);
      }
      out->forceNewtons = MotionVec_Add(
          out->forceNewtons,
          MotionVec_Scale(
              ForceField_Evaluate(&field, p, v, s_time, f.position, f.tangent),
              Motion_GuideWeight(bound, p, r) *
                  Motion_Envelope(bound->age, bound->desc.duration,
                                  bound->desc.attackTime,
                                  bound->desc.fadeTime)));
    }
  }
  for (int i = 0; i < MOTION_FIELDS_MAX_TARGETS; i++) {
    MotionTargetRuntime *t = &s_targets[i];
    MotionTargetDesc *d = &t->desc;
    if (!t->handle || !(d->receiverMask & mask))
      continue;
    float dist = MotionVec_Length(MotionVec_Sub(p, d->center));
    if (dist >= d->radius)
      continue;
    float w = 1 - dist / d->radius;
    w = w * w * (3 - 2 * w) *
        Motion_Envelope(t->age, d->duration, d->attackTime, d->fadeTime);
    out->airflowVelocity = MotionVec_Add(
        out->airflowVelocity,
        MotionVec_Scale(
            MotionFlow_Evaluate(&d->flow, p, d->center, d->flowAxis, d->radius,
                                s_time, MOTION_FLOW_SPHERE),
            Motion_Envelope(t->age, d->duration, d->attackTime, d->fadeTime)));
    if (d->airflow.strength != 0) {
      VorticleData vort = d->airflow;
      vort.position = d->center;
      vort.radius = d->radius;
      vort.active = true;
      vort.maxLifetime = d->duration;
      vort.lifetime = fmaxf(d->duration - t->age, 0);
      out->airflowVelocity = MotionVec_Add(
          out->airflowVelocity,
          MotionVec_Scale(Wind_EvaluateVorticleVelocity(&vort, p, s_time),
                          Motion_Envelope(t->age, d->duration, d->attackTime,
                                          d->fadeTime)));
    }
    if (d->field.layerCount > 0)
      out->forceNewtons = MotionVec_Add(
          out->forceNewtons,
          MotionVec_Scale(ForceField_Evaluate(&d->field, p, v, s_time,
                                              d->center, (Vector3){0, 1, 0}),
                          w));
  }
}
bool MotionFields_GetArrival(MotionFieldHandle h, MotionArrivalProfile *out) {
  MotionGuideRuntime *g = Motion_FindGuide(h);
  if (!g || !out)
    return false;
  *out = g->desc.arrival;
  return true;
}
MotionArrivalMode MotionFields_AdvanceReceiver(MotionReceiver *r,
                                               Vector3 before, Vector3 after,
                                               Vector3 velocity) {
  if (!r || r->arrived)
    return MOTION_ARRIVAL_RELEASE;
  MotionGuideRuntime *g = Motion_FindGuide(r->guide);
  if (!g)
    return MOTION_ARRIVAL_RELEASE;
  MotionPathSample f = MotionPath_Sample(&g->desc.path, g->desc.path.length);
  Vector3 goal = MotionVec_Add(
      f.position,
      Motion_RotateLane(f, r->localOffset, &g->desc.flow,
                        Motion_GuideRadius(&g->desc, f.distance), r->flowTime));
  if (Motion_SegmentDistance(before, after, goal) >
      fmaxf(g->desc.arrival.radius, 0.01f))
    return MOTION_ARRIVAL_RELEASE;
  /* On loops the terminal can be close to the start; require route progress. */
  if (g->desc.formation == MOTION_FORMATION_STREAM &&
      r->distance < g->desc.path.length - fmaxf(g->desc.radius * 0.75f,
                                                g->desc.arrival.radius * 2))
    return MOTION_ARRIVAL_RELEASE;
  if (g->desc.formation == MOTION_FORMATION_SHELL &&
      g->distance < g->desc.path.length - g->desc.arrival.radius)
    return MOTION_ARRIVAL_RELEASE;
  /* Start the independent target phase without jumping a rotating lane. */
  r->localOffset = MotionPath_LocalOffset(f, MotionVec_Sub(goal, f.position));
  r->flowTime = 0;
  r->arrived = true;
  MotionArrivalMode action = g->desc.arrival.mode;
  if (!g->fired) {
    g->fired = true;
    Motion_Trace("arrival", g->handle, (int)g->desc.arrival.mode);
    for (int i = 0;
         i < g->desc.arrival.targetCount && i < MOTION_ARRIVAL_MAX_TARGETS;
         i++) {
      MotionTargetDesc d = g->desc.arrival.targets[i];
      d.center = MotionVec_Add(d.center, f.position);
      for (int j = 0; j < d.field.layerCount && j < FORCE_FIELD_MAX_LAYERS; j++)
        d.field.layers[j].origin =
            MotionVec_Add(d.field.layers[j].origin, f.position);
      if (!MotionFields_CreateTarget(&d))
        Motion_Trace("target_rejected", g->handle, i);
    }
    if (g->desc.arrival.callback) {
      MotionArrivalEvent event = {r->guide, f.position, after, velocity};
      g->desc.arrival.callback(&event, g->desc.arrival.userData);
    }
  }
  return action;
}

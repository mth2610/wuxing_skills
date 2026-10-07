#include "core/motion/motion_fields.h"
#include "core/wind/wind_system.h"
#include <stdlib.h>
#include <float.h>
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
  FieldDesc physical;
  bool typed;
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
/* Snapshot bridge: no ring-pool spawning, stale indices, or per-tracer rebuilds. */
static void Motion_PublishWind(void) {
  VorticleData sources[MOTION_FIELDS_MAX_GUIDES * 9];
  int count = 0;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; ++i) {
    MotionGuideRuntime *g = &s_guides[i];
    MotionGuideDesc *d = &g->desc;
    if (!g->handle || !d->affectWind) continue;
    int nodes = d->mode == MOTION_GUIDE_PULSE ? 1 : 3;
    float envelope = Motion_Envelope(g->age, d->duration, d->attackTime, d->fadeTime);
    for (int node = 0; node < nodes; ++node) {
      float distance = nodes == 1 ? g->distance : d->path.length * (node + 0.5f) / nodes;
      MotionPathSample f = MotionPath_Sample(&d->path, distance);
      float radius = Motion_GuideRadius(d, distance);
      float speeds[] = {d->speed * Motion_Profile(d->speedScaleStart, d->speedScaleEnd, distance / d->path.length),
                        d->flow.swirlSpeedMps, d->flow.turbulenceSpeedMps};
      for (int component = 0; component < 3; ++component) {
        if (speeds[component] == 0 || envelope == 0) continue;
        sources[count++] = (VorticleData){
            .position = f.position,
            .direction = component == 2 ? (Vector3){1.0f / radius, 1.0f, 0.0f} : f.tangent,
            .radius = radius, .strength = speeds[component] * envelope,
            .type = component == 0 ? VORTICLE_LINEAR_GUST : component == 1 ? VORTICLE_VORTEX : VORTICLE_TURBULENCE,
            .lifetime = 1, .maxLifetime = 1, .active = true};
      }
    }
  }
  Wind_SetMotionAirflow(sources, count);
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
  Wind_SetMotionAirflow(NULL, 0);
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
      if (s_targets[i].age >= (s_targets[i].typed ?
          s_targets[i].physical.lifetime.startDelaySec + s_targets[i].physical.lifetime.durationSec : s_targets[i].desc.duration))
        s_targets[i].handle = 0;
    }
  Motion_PublishWind();
}
static bool Motion_FiniteVector(Vector3 v) {
  return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static bool Motion_ValidPhysical(const FieldDesc *d);
/* Motion's historic layers are explicitly Newton-valued compatibility data.
 * Sampling-only velocity textures, damping and contacts are not force laws. */
static bool Motion_ValidLegacyLayers(const ForceField *f) {
  if(f->layerCount<0 || f->layerCount>FORCE_FIELD_MAX_LAYERS) return false;
  for(int i=0;i<f->layerCount;++i)
    if(f->layers[i].type==FORCE_VECTOR_TEXTURE || f->layers[i].type==FORCE_VISCOSITY ||
       f->layers[i].type==FORCE_RECEIVER_PLANE) return false;
  return true;
}
static bool Motion_ValidTarget(const MotionTargetDesc *d) {
  return d && Motion_ValidLegacyLayers(&d->field) && (!d->usePhysicalField || Motion_ValidPhysical(&d->physicalField)) && isfinite(d->duration) && d->duration > 0 &&
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
  if (!d || !Motion_ValidLegacyLayers(&d->field) || (d->usePhysicalField && !Motion_ValidPhysical(&d->physicalField)) ||
      !isfinite(d->controller.maxForceNewtons) || d->controller.maxForceNewtons<0 ||
      !isfinite(d->controller.dampingRatio) || d->controller.dampingRatio<0 ||
      !isfinite(d->duration) || d->duration <= 0 ||
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
      if(s_guides[i].desc.controller.maxForceNewtons<=0)
        s_guides[i].desc.controller=(GuideController){d->maxForceNewtons,1};
      if (!s_guides[i].desc.receiverMask)
        s_guides[i].desc.receiverMask = MOTION_RECEIVER_ALL;
      Motion_PublishWind();
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
  Motion_PublishWind();
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
  float stiffness = d->controller.maxForceNewtons / fmaxf(radius * 0.25f, 0.001f);
  float damping = 2 * d->controller.dampingRatio * sqrtf(stiffness * massKg);
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
      MotionVec_Limit(force, d->controller.maxForceNewtons),
      Motion_Envelope(g->age, d->duration, d->attackTime, d->fadeTime) *
          Motion_GuideWeight(g, p, r));
}
static void Motion_SampleTargets(Vector3 p, Vector3 v, unsigned int mask,
                                  MotionFieldSample *out) {
  for (int i = 0; i < MOTION_FIELDS_MAX_TARGETS; i++) {
    MotionTargetRuntime *t = &s_targets[i];
    MotionTargetDesc *d = &t->desc;
    if (t->typed || !t->handle || !(d->receiverMask & mask))
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

static void Motion_AddGuideLayers(MotionGuideRuntime *g, Vector3 p, Vector3 v,
                                  MotionReceiver *r, MotionFieldSample *out) {
  if (g->desc.field.layerCount <= 0 || r->arrived) return;
  MotionPathSample f = MotionPath_Sample(&g->desc.path, r->distance);
  ForceField field = g->desc.field;
  for (int j = 0; j < field.layerCount; ++j) {
    field.layers[j].origin = MotionVec_Add(f.position, MotionPath_WorldOffset(f, field.layers[j].origin));
    field.layers[j].direction = MotionPath_WorldOffset(f, field.layers[j].direction);
  }
  out->forceNewtons = MotionVec_Add(out->forceNewtons,
      MotionVec_Scale(ForceField_Evaluate(&field, p, v, s_time, f.position, f.tangent),
          Motion_GuideWeight(g, p, r) * Motion_Envelope(g->age, g->desc.duration,
              g->desc.attackTime, g->desc.fadeTime)));
}

static void Motion_SampleAnchoredLegacy(Vector3 p, Vector3 v, float massKg, float dt,
                                 unsigned int mask, MotionFieldSample *out) {
  if (!out) return;
  *out = (MotionFieldSample){0};
  if (!Motion_FiniteVector(p) || !Motion_FiniteVector(v) ||
      !isfinite(massKg) || massKg <= 0 || !isfinite(dt) || dt < 0) return;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; ++i) {
    MotionGuideRuntime *g = &s_guides[i];
    MotionPathSample f;
    if (!g->handle || !(g->desc.receiverMask & mask) || !Motion_InGuide(g, p, &f)) continue;
    MotionReceiver r = {.guide = g->handle, .distance = f.distance, .segment = f.segment};
    out->forceNewtons = MotionVec_Add(out->forceNewtons,
        Motion_GuideForce(g, p, v, massKg, dt, &r));
    out->captured = true; // Spatial influence only; no owned capture state.
    Motion_AddGuideLayers(g, p, v, &r, out);
  }
  Motion_SampleTargets(p, v, mask, out);
}

static void Motion_ExpandBounds(Vector3 p, float radius, Vector3 *lo, Vector3 *hi) {
  lo->x = fminf(lo->x, p.x-radius); lo->y = fminf(lo->y, p.y-radius); lo->z = fminf(lo->z, p.z-radius);
  hi->x = fmaxf(hi->x, p.x+radius); hi->y = fmaxf(hi->y, p.y+radius); hi->z = fmaxf(hi->z, p.z+radius);
}
bool MotionFields_GetAnchoredBounds(unsigned int mask, Vector3 *lo, Vector3 *hi) {
  if (!lo || !hi) return false;
  *lo = (Vector3){FLT_MAX,FLT_MAX,FLT_MAX}; *hi = (Vector3){-FLT_MAX,-FLT_MAX,-FLT_MAX};
  bool found = false;
  for (int i = 0; i < MOTION_FIELDS_MAX_GUIDES; ++i) {
    MotionGuideRuntime *g = &s_guides[i]; MotionGuideDesc *d = &g->desc;
    if (!g->handle || !(d->receiverMask & mask)) continue;
    float radius = fmaxf(Motion_GuideRadius(d,0), Motion_GuideRadius(d,d->path.length));
    float start = 0, end = d->path.length;
    if (d->formation == MOTION_FORMATION_SHELL) start = end = g->distance;
    else if (d->mode == MOTION_GUIDE_PULSE) {
      start = fmaxf(0,g->distance-d->pulseLength*.5f);
      end = fminf(d->path.length,g->distance+d->pulseLength*.5f);
    }
    Motion_ExpandBounds(MotionPath_Sample(&d->path,start).position,radius,lo,hi);
    Motion_ExpandBounds(MotionPath_Sample(&d->path,end).position,radius,lo,hi);
    for (int j=0;j<d->path.count;++j)
      if (d->path.distance[j]>=start && d->path.distance[j]<=end)
        Motion_ExpandBounds(d->path.points[j],radius,lo,hi);
    found = true;
  }
  for (int i=0;i<MOTION_FIELDS_MAX_TARGETS;++i) {
    MotionTargetRuntime *t=&s_targets[i];
    if (!t->typed && t->handle && (t->desc.receiverMask & mask)) {
      Motion_ExpandBounds(t->desc.center,t->desc.radius,lo,hi); found=true;
    }
  }
  for(int i=0;i<MOTION_FIELDS_MAX_TARGETS;++i) {
    MotionTargetRuntime *t=&s_targets[i];
    if(!t->handle) continue;
    FieldDesc *d=t->typed?&t->physical:t->desc.usePhysicalField?&t->desc.physicalField:NULL;
    if(!d || !((d->receiverMask?d->receiverMask:MOTION_RECEIVER_ALL) & mask)) continue;
    FieldTransform frame=FieldTrajectory_Transform(d,t->age);
    float radius=d->volume.shape==FIELD_BOX?MotionVec_Length(d->volume.halfExtentsM):d->volume.radiusM;
    if(d->volume.shape==FIELD_PATH_TUBE) {
      for(int j=0;j<d->volume.path.count;++j)
        Motion_ExpandBounds(MotionVec_Add(frame.position,FieldTransform_Vector(&frame,d->volume.path.points[j])),radius,lo,hi);
    } else if(d->volume.shape==FIELD_CAPSULE) {
      Motion_ExpandBounds(MotionVec_Add(frame.position,FieldTransform_Vector(&frame,d->volume.capsuleStart)),radius,lo,hi);
      Motion_ExpandBounds(MotionVec_Add(frame.position,FieldTransform_Vector(&frame,d->volume.capsuleEnd)),radius,lo,hi);
    } else Motion_ExpandBounds(frame.position,radius,lo,hi);
    found=true;
  }
  if (!found) *lo=*hi=(Vector3){0};
  return found;
}

static void Motion_SampleLegacy(Vector3 p, Vector3 v, float massKg, float dt,
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
    Motion_AddGuideLayers(bound, p, v, r, out);
  }
  Motion_SampleTargets(p, v, mask, out);
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
      if(d.usePhysicalField)
        d.physicalField.transform.position=MotionVec_Add(d.physicalField.transform.position,f.position);
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


FieldDesc MotionField_Default(void) {
  FieldDesc d={0};
  d.volume.shape=FIELD_SPHERE; d.volume.radiusM=1;
  d.transform=FieldTransform_Identity(); d.lifetime.durationSec=2;
  d.receiverMask=MOTION_RECEIVER_ALL; d.flow.axis=(Vector3){0,1,0};
  return d;
}
static bool Motion_ValidPhysical(const FieldDesc *d) {
  if (!d || d->forceLawCount<0 || d->forceLawCount>FIELD_MAX_FORCE_LAWS ||
      d->volume.shape<FIELD_SPHERE || d->volume.shape>FIELD_PATH_TUBE ||
      !isfinite(d->lifetime.durationSec) || d->lifetime.durationSec<=0 ||
      !isfinite(d->lifetime.startDelaySec) || d->lifetime.startDelaySec<0 ||
      !isfinite(d->lifetime.attackSec) || d->lifetime.attackSec<0 ||
      !isfinite(d->lifetime.fadeSec) || d->lifetime.fadeSec<0 ||
      !isfinite(d->volume.coreFraction) || d->volume.coreFraction<0 || d->volume.coreFraction>=1 ||
      !MotionFlow_IsValid(&d->flow.procedural) ||
      !Motion_FiniteVector(d->volume.capsuleStart) || !Motion_FiniteVector(d->volume.capsuleEnd) ||
      !Motion_FiniteVector(d->transform.axisX) || !Motion_FiniteVector(d->transform.axisY) ||
      !Motion_FiniteVector(d->transform.axisZ) || !Motion_FiniteVector(d->transform.position) ||
      !Motion_FiniteVector(d->transform.frameVelocityMps) ||
      !Motion_FiniteVector(d->transform.angularVelocityRadPerSec)) return false;
  const FieldTransform *t=&d->transform;
  if (fabsf(MotionVec_Length(t->axisX)-1)>0.001f ||
      fabsf(MotionVec_Length(t->axisY)-1)>0.001f ||
      fabsf(MotionVec_Length(t->axisZ)-1)>0.001f ||
      fabsf(MotionVec_Dot(t->axisX,t->axisY))>0.001f ||
      MotionVec_Dot(MotionVec_Cross(t->axisX,t->axisY),t->axisZ)<0.999f) return false;
  if (d->volume.shape==FIELD_BOX) {
    if (!Motion_FiniteVector(d->volume.halfExtentsM) || d->volume.halfExtentsM.x<=0 ||
        d->volume.halfExtentsM.y<=0 || d->volume.halfExtentsM.z<=0) return false;
  } else if (!isfinite(d->volume.radiusM) || d->volume.radiusM<=0) return false;
  MotionPath checked;
  if(d->volume.shape==FIELD_PATH_TUBE && !MotionPath_Build(&checked,d->volume.path.points,d->volume.path.count)) return false;
  if(d->trajectory.mode<FIELD_TRAJECTORY_STATIC || d->trajectory.mode>FIELD_TRAJECTORY_PATH) return false;
  if(d->trajectory.mode==FIELD_TRAJECTORY_PATH &&
      (!isfinite(d->trajectory.speedMps) || d->trajectory.speedMps<0 ||
       !MotionPath_Build(&checked,d->trajectory.path.points,d->trajectory.path.count))) return false;
  for(int i=0;i<d->forceLawCount;++i) {
    const ForceLaw *l=&d->forceLaws[i];
    if(l->type<FORCE_LAW_NEWTONS || l->type>FORCE_LAW_PATH_GUIDE ||
       !Motion_FiniteVector(l->forceNewtons) || !Motion_FiniteVector(l->accelerationMps2) || !Motion_FiniteVector(l->center) ||
       !isfinite(l->magnitudeNewtons) || !isfinite(l->springStiffnessNPerM) ||
       l->springStiffnessNPerM<0 || !isfinite(l->dampingNsPerM) || l->dampingNsPerM<0 ||
       !isfinite(l->forwardForceNewtons) || l->forwardForceNewtons<0) return false;
    if(l->type==FORCE_LAW_PATH_GUIDE &&
       (d->volume.shape!=FIELD_PATH_TUBE || l->magnitudeNewtons<0)) return false;
  }
  return isfinite(d->flow.followSpeedMps) &&
         Motion_FiniteVector(d->flow.velocityMps) && Motion_FiniteVector(d->flow.axis) &&
      isfinite(d->flow.blendWeight) && d->flow.blendWeight>=0;
}
MotionFieldHandle MotionFields_CreateField(const FieldDesc *d) {
  if(!Motion_ValidPhysical(d)) return MOTION_FIELD_INVALID;
  for(int i=0;i<MOTION_FIELDS_MAX_TARGETS;++i) if(!s_targets[i].handle) {
    MotionTargetRuntime *t=&s_targets[i];
    *t=(MotionTargetRuntime){.physical=*d,.typed=true,
      .handle=Motion_NewHandle(MOTION_FIELDS_MAX_GUIDES+i)};
    if(!t->physical.receiverMask) t->physical.receiverMask=MOTION_RECEIVER_ALL;
    if(d->volume.shape==FIELD_PATH_TUBE)
      MotionPath_Build(&t->physical.volume.path,d->volume.path.points,d->volume.path.count);
    if(d->trajectory.mode==FIELD_TRAJECTORY_PATH)
      MotionPath_Build(&t->physical.trajectory.path,d->trajectory.path.points,d->trajectory.path.count);
    return t->handle;
  }
  return MOTION_FIELD_INVALID;
}

MotionFieldHandle MotionFields_SpawnStaticAttractor(Vector3 center, float radiusM,
                                                    float pullStrengthN, float durationSec,
                                                    float attackSec, float fadeSec) {
  if (!Motion_FiniteVector(center) || radiusM <= 0.0f || durationSec <= 0.0f)
    return MOTION_FIELD_INVALID;
  FieldDesc d = MotionField_Default();
  d.volume.shape = FIELD_SPHERE;
  d.volume.radiusM = radiusM;
  d.volume.coreFraction = 0.2f;
  d.transform.position = center;
  d.lifetime.durationSec = durationSec;
  d.lifetime.attackSec = fmaxf(attackSec, 0.0f);
  d.lifetime.fadeSec = fmaxf(fadeSec, 0.0f);
  d.receiverMask = MOTION_RECEIVER_ALL;
  if (pullStrengthN != 0.0f) {
    d.forceLaws[d.forceLawCount++] = (ForceLaw){
        .type = FORCE_LAW_RADIAL_ATTRACTION,
        .center = (Vector3){0, 0, 0},
        .magnitudeNewtons = pullStrengthN};
  }
  return MotionFields_CreateField(&d);
}

MotionFieldHandle MotionFields_SpawnMovingGuide(const MotionPath *path, float speedMps,
                                                float radiusM, float pullStrengthN,
                                                float swirlSpeedMps, float turbulenceSpeedMps,
                                                float durationSec) {
  if (!path || path->count < 2 || speedMps < 0.0f || radiusM <= 0.0f || durationSec <= 0.0f)
    return MOTION_FIELD_INVALID;
  FieldDesc d = MotionField_Default();
  d.volume.shape = FIELD_SPHERE;
  d.volume.radiusM = radiusM;
  d.volume.coreFraction = 0.25f;
  d.trajectory.mode = FIELD_TRAJECTORY_PATH;
  d.trajectory.path = *path;
  d.trajectory.speedMps = speedMps;
  d.lifetime.durationSec = durationSec;
  d.lifetime.attackSec = 0.2f;
  d.lifetime.fadeSec = 0.3f;
  d.receiverMask = MOTION_RECEIVER_ALL;

  if (pullStrengthN != 0.0f) {
    d.forceLaws[d.forceLawCount++] = (ForceLaw){
        .type = FORCE_LAW_RADIAL_ATTRACTION,
        .center = (Vector3){0, 0, 0},
        .magnitudeNewtons = pullStrengthN};
  }
  if (speedMps > 0.0f || swirlSpeedMps != 0.0f || turbulenceSpeedMps > 0.0f) {
    d.flow.enabled = true;
    d.flow.addBackgroundVelocity = true;
    d.flow.axis = MotionPath_Sample(path, 0).tangent;
    d.flow.procedural.swirlSpeedMps = swirlSpeedMps;
    d.flow.procedural.turbulenceSpeedMps = turbulenceSpeedMps;
    d.flow.procedural.eddyLengthM = radiusM * 0.3f;
    d.flow.blendWeight = 1.0f;
  }
  return MotionFields_CreateField(&d);
}

MotionFieldHandle MotionFields_SpawnStaticVortex(Vector3 center, Vector3 axis, float radiusM,
                                                 float swirlSpeedMps, float inwardPullN,
                                                 float durationSec, float attackSec, float fadeSec) {
  if (!Motion_FiniteVector(center) || radiusM <= 0.0f || durationSec <= 0.0f)
    return MOTION_FIELD_INVALID;
  FieldDesc d = MotionField_Default();
  d.volume.shape = FIELD_SPHERE;
  d.volume.radiusM = radiusM;
  d.volume.coreFraction = 0.15f;
  d.transform.position = center;
  d.lifetime.durationSec = durationSec;
  d.lifetime.attackSec = fmaxf(attackSec, 0.0f);
  d.lifetime.fadeSec = fmaxf(fadeSec, 0.0f);
  d.receiverMask = MOTION_RECEIVER_ALL;

  if (inwardPullN != 0.0f) {
    d.forceLaws[d.forceLawCount++] = (ForceLaw){
        .type = FORCE_LAW_RADIAL_ATTRACTION,
        .center = (Vector3){0, 0, 0},
        .magnitudeNewtons = inwardPullN};
  }
  if (swirlSpeedMps != 0.0f) {
    d.flow.enabled = true;
    d.flow.axis = MotionVec_Length(axis) > 0.1f ? MotionVec_Normalize(axis) : (Vector3){0, 1, 0};
    d.flow.procedural.swirlSpeedMps = swirlSpeedMps;
    d.flow.procedural.eddyLengthM = radiusM * 0.25f;
    d.flow.blendWeight = 1.0f;
  }
  return MotionFields_CreateField(&d);
}

static void Motion_ComposePhysical(const FieldDesc *d,float age,Vector3 p,Vector3 v,
    const BodyPhysicalProperties *body,const MediumProperties *medium,FieldSample *out,bool dragPass) {
  bool hasPassLaw=false;
  for(int j=0;j<d->forceLawCount;++j)
    if((d->forceLaws[j].type==FORCE_LAW_DRAG)==dragPass) {
      hasPassLaw=true;
      break;
    }
  bool includeFlow=!dragPass;
  if(!hasPassLaw && !(includeFlow && d->flow.enabled)) return;
  MediumProperties resolved=*medium;
  if(dragPass) {
    if(out->mediumWeight>0) resolved.velocityMps=out->mediumIsAbsolute?out->mediumVelocityMps:
      MotionVec_Add(medium->velocityMps,out->mediumVelocityMps);
  }
  FieldSample sample=Field_EvaluatePass(d,age,p,v,body,&resolved,includeFlow,true,dragPass);
  FieldSample_Combine(out,&sample);
}
static void Motion_SamplePhysical(Vector3 p,Vector3 v,const BodyPhysicalProperties *body,
    const MediumProperties *medium,unsigned int mask,FieldSample *out) {
  /* Resolve media first; all authored drag laws then consume that single
   * resolved velocity. Overlapping drag laws remain explicit force additions. */
  for(int pass=0;pass<2;++pass) {
    for(int i=0;i<MOTION_FIELDS_MAX_TARGETS;++i) {
      MotionTargetRuntime *t=&s_targets[i];
      if(!t->handle) continue;
      const FieldDesc *d=t->typed?&t->physical:t->desc.usePhysicalField?&t->desc.physicalField:NULL;
      if(!d || !((d->receiverMask?d->receiverMask:MOTION_RECEIVER_ALL) & mask)) continue;
      Motion_ComposePhysical(d,t->age,p,v,body,medium,out,pass!=0);
    }
    for(int i=0;i<MOTION_FIELDS_MAX_GUIDES;++i) {
      MotionGuideRuntime *g=&s_guides[i]; MotionPathSample projection;
      if(!g->handle || !g->desc.usePhysicalField || !(g->desc.receiverMask & mask) ||
         !Motion_InGuide(g,p,&projection)) continue;
      FieldDesc d=g->desc.physicalField;
      MotionPathSample f=MotionPath_Sample(&g->desc.path,projection.distance);
      d.transform.position=MotionVec_Add(f.position,MotionPath_WorldOffset(f,d.transform.position));
      d.transform.axisX=MotionPath_WorldOffset(f,d.transform.axisX);
      d.transform.axisY=MotionPath_WorldOffset(f,d.transform.axisY);
      d.transform.axisZ=MotionPath_WorldOffset(f,d.transform.axisZ);
      Motion_ComposePhysical(&d,g->age,p,v,body,medium,out,pass!=0);
    }
  }
}
static bool Motion_ValidBodySample(const BodyPhysicalProperties *b,const MediumProperties *m,
    const ReceiverConstraints *c) {
  return b && m && c && isfinite(b->massKg) && (b->massKg>0 || (b->massKg==0 && c->mode==RECEIVER_TRACER)) &&
    isfinite(b->densityKgM3) && b->densityKgM3>=0 && isfinite(b->volumeM3) && b->volumeM3>=0 &&
    isfinite(b->projectedAreaM2) && b->projectedAreaM2>=0 && isfinite(b->dragCoefficient) && b->dragCoefficient>=0 &&
    isfinite(b->immersionFraction) && b->immersionFraction>=0 && b->immersionFraction<=1 &&
    isfinite(m->densityKgM3) && m->densityKgM3>=0 && isfinite(m->dynamicViscosityPaS) && m->dynamicViscosityPaS>=0 &&
    Motion_FiniteVector(m->velocityMps) && Motion_FiniteVector(m->gravityMps2) &&
    c->mode>=RECEIVER_FREE && c->mode<=RECEIVER_TRACER && Motion_FiniteVector(c->permittedAxes) &&
    c->permittedAxes.x>=0 && c->permittedAxes.x<=1 && c->permittedAxes.y>=0 && c->permittedAxes.y<=1 &&
    c->permittedAxes.z>=0 && c->permittedAxes.z<=1;
}
static void Motion_ProjectResponse(const ReceiverConstraints *c,FieldSample *out) {
  if(c->mode==RECEIVER_TRACER) out->forceNewtons=out->accelerationMps2=(Vector3){0};
  if(c->mode==RECEIVER_ROOTED) {
    out->dragForceNewtons.x*=c->permittedAxes.x; out->dragForceNewtons.y*=c->permittedAxes.y; out->dragForceNewtons.z*=c->permittedAxes.z;
    out->forceNewtons.x*=c->permittedAxes.x; out->forceNewtons.y*=c->permittedAxes.y; out->forceNewtons.z*=c->permittedAxes.z;
    out->accelerationMps2.x*=c->permittedAxes.x; out->accelerationMps2.y*=c->permittedAxes.y; out->accelerationMps2.z*=c->permittedAxes.z;
  }
}
void MotionFields_SampleExternalBody(Vector3 p,Vector3 v,const BodyPhysicalProperties *body,
    const MediumProperties *medium,const ReceiverConstraints *c,unsigned int mask,FieldSample *out) {
  if(!out) return;
  *out=(FieldSample){0};
  if(!Motion_ValidBodySample(body,medium,c) || !Motion_FiniteVector(p) || !Motion_FiniteVector(v) ||
      c->mode==RECEIVER_STATIC || c->mode==RECEIVER_KINEMATIC) return;
  Motion_SamplePhysical(p,v,body,medium,mask,out);
  Motion_ProjectResponse(c,out);
}
void MotionFields_SampleBody(Vector3 p,Vector3 v,const BodyPhysicalProperties *body,
    const MediumProperties *medium,const ReceiverConstraints *constraints,float dt,
    unsigned int mask,MotionReceiver *receiver,FieldSample *out) {
  if(!out) return;
  *out=(FieldSample){0};
  if(!Motion_ValidBodySample(body,medium,constraints) ||
      !Motion_FiniteVector(p) || !Motion_FiniteVector(v) || !isfinite(dt) || dt<0 ||
      constraints->mode==RECEIVER_STATIC || constraints->mode==RECEIVER_KINEMATIC) return;
  MotionFieldSample legacy={0};
  if(constraints->mode==RECEIVER_TRACER) { /* Wind owns legacy tracer publication. */ }
  else if(constraints->mode==RECEIVER_ROOTED || constraints->mode==RECEIVER_TRACER)
    Motion_SampleAnchoredLegacy(p,v,body->massKg,dt,mask,&legacy);
  else Motion_SampleLegacy(p,v,body->massKg,dt,mask,receiver,&legacy);
  out->forceNewtons=legacy.forceNewtons; out->accelerationMps2=legacy.accelerationMps2;
  out->mediumVelocityMps=legacy.airflowVelocity;
  if(MotionVec_Length(legacy.airflowVelocity)>0) {
    out->mediumWeight=1; out->mediumPriority=-2147483647;
  }
  Motion_SamplePhysical(p,v,body,medium,mask,out);
  Motion_ProjectResponse(constraints,out);
}

/* Legacy receiver adapter: mass is known, area/density are not. Body-dependent
 * drag/buoyancy therefore require SampleBody; force/acceleration/flow still
 * sample correctly. No approximation silently invents area or volume. */
void MotionFields_Sample(Vector3 p,Vector3 v,float mass,float dt,unsigned int mask,
    MotionReceiver *receiver,MotionFieldSample *out) {
  if(!out) return;
  BodyPhysicalProperties body={.massKg=mass,.immersionFraction=1};
  MediumProperties medium={.densityKgM3=1.225f,.gravityMps2={0,-9.81f,0}};
  ReceiverConstraints constraints={.mode=RECEIVER_FREE}; FieldSample sample;
  MotionFields_SampleBody(p,v,&body,&medium,&constraints,dt,mask,receiver,&sample);
  *out=(MotionFieldSample){.forceNewtons=sample.forceNewtons,
    .accelerationMps2=sample.accelerationMps2,.airflowVelocity=sample.mediumVelocityMps,
    .captured=receiver && receiver->guide && !receiver->arrived};
}
void MotionFields_SampleAnchored(Vector3 p,Vector3 v,float mass,float dt,unsigned int mask,
    MotionFieldSample *out) {
  if(!out) return;
  BodyPhysicalProperties body={.massKg=mass,.immersionFraction=1};
  MediumProperties medium={.densityKgM3=1.225f,.gravityMps2={0,-9.81f,0}};
  ReceiverConstraints constraints={.mode=RECEIVER_ROOTED,.permittedAxes={1,1,1}};
  FieldSample sample;
  MotionFields_SampleBody(p,v,&body,&medium,&constraints,dt,mask,NULL,&sample);
  *out=(MotionFieldSample){.forceNewtons=sample.forceNewtons,
    .accelerationMps2=sample.accelerationMps2,.airflowVelocity=sample.mediumVelocityMps,
    .captured=MotionVec_Length(sample.forceNewtons)>0};
}

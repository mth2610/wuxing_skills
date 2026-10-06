/* Configured composition; motion math and arrival events live in core/motion.
 */
#include "core/motion/motion_body.h"
#include "core/path_spline.h"
#include "core/composition/vc_emission.h"
#define VC_GUIDED_MAX_STREAMS 8
typedef struct {
  bool active;
  ParticleEmitterHandle emitter;
  VFX_EmissionSchedule emission;
  ParticleDynamicsProfile body;
} VC_GuidedStream;
static VC_GuidedStream s_guidedStreams[VC_GUIDED_MAX_STREAMS];
static MeshAdjacency s_guidedSourceMesh;
static bool s_guidedSourceReady;
VFX_GuidedParticleConfig VFX_GuidedParticle_DefaultConfig(void) {
  VFX_GuidedParticleConfig c = {0};
  c.source = (Vector3){-2, 1, 0};
  c.target = (Vector3){2, 1, 0};
  c.formation = MOTION_FORMATION_SHELL;
  c.duration = 4;
  c.targetLifetime = 2;
  c.count = 512;
  c.emissionRate = 160;
  c.formationRadius = 0.10f;
  c.particleRadius = 0.11f;
  c.massKg = 0.004f;
  c.speed = 12;
  c.guideRadius = 1.2f;
  c.maxForceNewtons = 0.32f;
  c.pulseLength = 1.0f;
  c.material = VC_MAT_LIGHTNING;
  c.densityKgM3 = 600;
  return c;
}
static const char *s_guidedFormationNames[] = {"STREAM", "SPHERE SHELL"};
static const char *s_guidedArrivalNames[] = {"RELEASE", "DISAPPEAR", "HOLD",
                                             "ORBIT"};
static const char *s_guidedTargetNames[] = {"NONE", "BLAST", "FLOW"};
static const char *s_guidedModeNames[] = {"SUSTAINED", "TRAVELLING PULSE"};
int VFX_GuidedParticle_GetParams(VFX_GuidedParticleConfig *c, VFX_ParamDef *out,
                                 int max) {
  if (!c || !out || max <= 0)
    return 0;
  int n = 0;
  const char *group = "Guide";
#define GUIDE_ENUM(label, member, names)                                       \
  do {                                                                         \
    if (n < max)                                                               \
      out[n++] = (VFX_ParamDef){                                               \
          .name = label,                                                       \
          .group = group,                                                   \
          .type = VFX_PARAM_ENUM,                                              \
          .valPtr = &c->member,                                                \
          .minInt = 0,                                                         \
          .maxInt = (int)(sizeof(names) / sizeof(names[0])) - 1,               \
          .enumNames = names,                                                  \
          .enumCount = (int)(sizeof(names) / sizeof(names[0]))};               \
  } while (0)
#define GUIDE_FLOAT(label, member, lo, hi, step)                               \
  do {                                                                         \
    if (n < max)                                                               \
      out[n++] = (VFX_ParamDef){.name = label,                                 \
                                .group = group,                             \
                                .type = VFX_PARAM_FLOAT,                       \
                                .valPtr = &c->member,                          \
                                .minFloat = lo,                                \
                                .maxFloat = hi,                                \
                                .stepFloat = step};                            \
  } while (0)
  if (!c->guideOverride) {
    GUIDE_ENUM("Formation", formation, s_guidedFormationNames);
    GUIDE_ENUM("Guide mode", guideMode, s_guidedModeNames);
    GUIDE_FLOAT("Guide lifetime s", duration, 0.1f, 30, 0.5f);
    GUIDE_FLOAT("Speed m/s", speed, 0.2f, 20, 0.5f);
    GUIDE_FLOAT("Guide radius m", guideRadius, 0.1f, 8, 0.1f);
    GUIDE_FLOAT("Guide force N", maxForceNewtons, 0.01f, 5, 0.05f);
    if (c->guideMode == MOTION_GUIDE_PULSE)
      GUIDE_FLOAT("Pulse length m", pulseLength, 0.1f, 8, 0.1f);
    GUIDE_FLOAT("Turbulence m/s", motionFlow.turbulenceSpeedMps, 0, 8, 0.2f);
    GUIDE_FLOAT("Swirl m/s", motionFlow.swirlSpeedMps, -8, 8, 0.5f);
    group = "Arrival";
    GUIDE_ENUM("Arrival", arrival, s_guidedArrivalNames);
    if (c->arrival == MOTION_ARRIVAL_HOLD || c->arrival == MOTION_ARRIVAL_ORBIT) {
      GUIDE_FLOAT("Arrival turbulence m/s", arrivalFlow.turbulenceSpeedMps, 0, 8, 0.2f);
      GUIDE_FLOAT("Arrival swirl m/s", arrivalFlow.swirlSpeedMps, -8, 8, 0.5f);
    }
    if (!c->targetOverrides) {
      group = "Target";
      GUIDE_ENUM("Target field", targetPreset, s_guidedTargetNames);
      if (c->targetPreset != VFX_GUIDED_TARGET_NONE ||
          c->targetFlow.swirlSpeedMps != 0 || c->targetFlow.turbulenceSpeedMps > 0) {
        GUIDE_FLOAT("Target lifetime s", targetLifetime, 0.1f, 30, 0.5f);
        GUIDE_FLOAT("Target turbulence m/s", targetFlow.turbulenceSpeedMps, 0, 8, 0.2f);
        GUIDE_FLOAT("Target swirl m/s", targetFlow.swirlSpeedMps, -8, 8, 0.5f);
      }
    }
  }
  group = "Emission";
  if (n < max)
    out[n++] = (VFX_ParamDef){.name="Burst count", .group=group,
        .type=VFX_PARAM_INT, .valPtr=&c->count, .minInt=0, .maxInt=2048};
  GUIDE_FLOAT("Emit duration s", emitDuration, 0, 20, 0.5f);
  if (c->emitDuration > 0)
    GUIDE_FLOAT("Emission rate /s", emissionRate, 0, 1000, 25);
  bool emitting = c->count > 0 || (c->emitDuration > 0 && c->emissionRate > 0);
  if (emitting) {
    if (!c->emissionSource)
      GUIDE_FLOAT("Source radius m", formationRadius, 0.01f, 2, 0.025f);
    if (!c->particleTemplate) {
      group = "Appearance";
      GUIDE_FLOAT("Particle radius m", particleRadius, 0.005f, 0.2f, 0.005f);
    }
    if (!c->bodyOverride && (!c->particleTemplate || !c->particleTemplate->physics.dynamics)) {
      group = "Body";
      if (n < max) out[n++] = (VFX_ParamDef){.name="Custom body properties",
          .group=group, .type=VFX_PARAM_BOOL, .valPtr=&c->showCustomBodyProperties};
      if (c->showCustomBodyProperties) {
        GUIDE_FLOAT("Density kg/m3", densityKgM3, 0.1f, 2000, 100);
        GUIDE_FLOAT("Mass kg", massKg, 0.000001f, 1, 0.0001f);
      }
    }
  }
#undef GUIDE_ENUM
#undef GUIDE_FLOAT
  return n;
}
static void VC_GuidedParticle_Update(float dt) {
  if (dt <= 0)
    return;
  for (int i = 0; i < VC_GUIDED_MAX_STREAMS; i++) {
    VC_GuidedStream *s = &s_guidedStreams[i];
    if (!s->active)
      continue;
    int n = VFX_EmissionAdvance(&s->emission, dt, 2048);
    if (n > 0)
      ParticleManager_Emit(s->emitter, n);
    if (VFX_EmissionComplete(&s->emission)) {
      ParticleManager_DestroyEmitter(s->emitter);
      s->active = false;
    }
  }
}
static MotionTargetDesc VC_GuidedTarget(VFX_GuidedTargetPreset preset,
                                        float duration, MotionFlowDesc flow) {
  MotionTargetDesc d = MotionTarget_Default();
  d.duration = duration;
  d.flow = flow;
  if (preset == VFX_GUIDED_TARGET_BLAST) {
    d.radius = 4.5f;
    d.airflow = (VorticleData){.type = VORTICLE_RADIAL_BLAST,
                               .strength = 7.5f,
                               .direction = {0, 1, 0}};
  }
  return d;
}
static bool VC_GuidedSettingsValid(const VFX_GuidedParticleConfig *c) {
  if (!c || c->count < 0 || c->count > 2048 ||
      !isfinite(c->emitDuration) || c->emitDuration < 0 ||
      (c->emitDuration > 0 && (!isfinite(c->emissionRate) || c->emissionRate < 0)))
    return false;
  bool emitting = c->count > 0 || (c->emitDuration > 0 && c->emissionRate > 0);
  bool defaultBody = !c->bodyOverride &&
      (!c->particleTemplate || !c->particleTemplate->physics.dynamics);
  if (emitting && (c->renderMode < PARTICLE_RENDER_BILLBOARD ||
      c->renderMode > PARTICLE_RENDER_SURFACE_INPUT ||
      (!c->emissionSource && (!isfinite(c->formationRadius) || c->formationRadius < 0)) ||
      (!c->particleTemplate && (!isfinite(c->particleRadius) || c->particleRadius <= 0)) ||
      (defaultBody && (!isfinite(c->massKg) || c->massKg <= 0 ||
          !isfinite(c->densityKgM3) || c->densityKgM3 <= 0))))
    return false;
  if (emitting && c->bodyOverride && (!c->particleTemplate || !c->particleTemplate->physics.dynamics) &&
      (!isfinite(c->bodyOverride->massKg) || c->bodyOverride->massKg <= 0 ||
       !isfinite(c->bodyOverride->densityKgM3) || c->bodyOverride->densityKgM3 < 0 ||
       !isfinite(c->bodyOverride->projectedAreaM2) || c->bodyOverride->projectedAreaM2 < 0 ||
       !isfinite(c->bodyOverride->dragCoefficient) || c->bodyOverride->dragCoefficient < 0 ||
       !isfinite(c->bodyOverride->volumeM3) || c->bodyOverride->volumeM3 < 0 ||
       !isfinite(c->bodyOverride->immersionFraction) || c->bodyOverride->immersionFraction < 0 ||
       c->bodyOverride->immersionFraction > 1))
    return false;
  if (emitting && c->bodyOverride && (!c->particleTemplate || !c->particleTemplate->physics.dynamics)) {
    float derivedVolume = c->bodyOverride->densityKgM3 > 0 ?
        c->bodyOverride->massKg / c->bodyOverride->densityKgM3 : 0;
    if (c->bodyOverride->immersionFraction != 1 ||
        fabsf(c->bodyOverride->volumeM3-derivedVolume) >
            fmaxf(1e-12f, derivedVolume*1e-5f)) return false;
  }
  if (!c->guideOverride && (!MotionFlow_IsValid(&c->motionFlow) ||
      !MotionFlow_IsValid(&c->arrivalFlow) ||
      (!c->targetOverrides && (!MotionFlow_IsValid(&c->targetFlow) ||
       c->targetPreset < VFX_GUIDED_TARGET_NONE || c->targetPreset > VFX_GUIDED_TARGET_FLOW))))
    return false;
  return true;
}
MotionFieldHandle
VFX_ComposeGuidedParticleEx(const VFX_GuidedParticleConfig *c) {
  if (c && c->surfaceStreamOut)
    *c->surfaceStreamOut = (ParticleRenderStream){0};
  if (!VC_GuidedSettingsValid(c)) return 0;
  VC_GuidedStream *stream = NULL;
  if (c->emitDuration > 0 && c->emissionRate > 0) {
    for (int i = 0; i < VC_GUIDED_MAX_STREAMS; i++)
      if (!s_guidedStreams[i].active) {
        stream = &s_guidedStreams[i];
        break;
      }
    if (!stream)
      return 0;
  }
  MotionGuideDesc guide =
      c->guideOverride ? *c->guideOverride : MotionGuide_Default();
  if (!c->guideOverride) {
    Vector3 delta = MotionVec_Sub(c->target, c->source);
    float len = MotionVec_Length(delta);
    if (len < 0.001f)
      return 0;
    Vector3 up = (Vector3){0, 1, 0};
    Vector3 a = MotionVec_Add(c->source,
                              MotionVec_Add(MotionVec_Scale(delta, 0.33f),
                                            MotionVec_Scale(up, len * 0.06f)));
    Vector3 b = MotionVec_Add(c->source,
                              MotionVec_Add(MotionVec_Scale(delta, 0.67f),
                                            MotionVec_Scale(up, len * 0.02f)));
    Vector3 points[33];
    for (int i = 0; i < 33; i++)
      points[i] = GetBezierPoint(c->source, a, b, c->target, (float)i / 32);
    if (!MotionPath_Build(&guide.path, points, 33))
      return 0;
    guide.formation = c->formation;
    guide.mode = c->guideMode;
    guide.duration = c->duration;
    guide.radius = c->guideRadius;
    guide.speed = c->speed;
    guide.maxForceNewtons = c->maxForceNewtons;
    guide.controller = (GuideController){.maxForceNewtons = c->maxForceNewtons,
                                        .dampingRatio = 1.0f};
    guide.pulseLength = c->pulseLength;
    guide.preserveStreamLanes = true;
    guide.flow = c->motionFlow;
    guide.arrival.flow = c->arrivalFlow;
    if (!c->targetOverrides && c->targetPreset == VFX_GUIDED_TARGET_NONE &&
        c->arrivalFlow.swirlSpeedMps == 0 && c->arrivalFlow.turbulenceSpeedMps == 0)
      guide.arrival.flow = c->targetFlow; /* Legacy post-arrival adapter. */
    guide.arrival.mode = c->arrival;
    guide.arrival.radius = 0.25f;
    if (!c->targetOverrides && (c->targetPreset != VFX_GUIDED_TARGET_NONE ||
        c->targetFlow.swirlSpeedMps != 0 ||
        c->targetFlow.turbulenceSpeedMps > 0)) {
      guide.arrival.targetCount = 1;
      guide.arrival.targets[0] =
          VC_GuidedTarget(c->targetPreset, c->targetLifetime, c->targetFlow);
    }
  }
  if (c->targetOverrides) {
    guide.arrival.targetCount = (int)Motion_Clamp(
        (float)c->targetOverrideCount, 0, MOTION_ARRIVAL_MAX_TARGETS);
    for (int i = 0; i < guide.arrival.targetCount; i++)
      guide.arrival.targets[i] = c->targetOverrides[i];
  }
  if (c->fieldOverride) {
    guide.physicalField = *c->fieldOverride;
    guide.usePhysicalField = true;
  }
  MotionFieldHandle h = MotionFields_CreateGuide(&guide);
  if (!h)
    return 0;
  if (c->count == 0 && !stream)
    return h;
  if (!c->emissionSource && !s_guidedSourceReady) {
    Mesh mesh = GenMeshSphere(1, 16, 10);
    MeshAdjacency_Build(&s_guidedSourceMesh, mesh);
    UnloadMesh(mesh);
    s_guidedSourceReady = true;
  }
  ParticleDynamicsProfile body = {0};
  if (!c->particleTemplate || !c->particleTemplate->physics.dynamics) {
    BodyPhysicalProperties physicalBody = c->bodyOverride ? *c->bodyOverride :
        BodyPhysicalProperties_Sphere(c->massKg, c->densityKgM3, 0.47f);
    body = (ParticleDynamicsProfile){.inverseMassKg = 1 / physicalBody.massKg,
        .gravityScale = 1, .densityKgM3 = physicalBody.densityKgM3,
        .windSusceptibility = 1, .aerodynamicAreaM2 = physicalBody.projectedAreaM2,
        .aerodynamicDragCoefficient = physicalBody.dragCoefficient};
  }
  float bodyLifetime = guide.duration;
  float targetTail = 0;
  for (int i = 0; i < guide.arrival.targetCount; i++)
    targetTail = fmaxf(targetTail, guide.arrival.targets[i].duration);
  bodyLifetime += targetTail;
  const VFX_ElementMaterial *material = VFX_Material(c->material);
  ParticleConfig p =
      c->particleTemplate
          ? *c->particleTemplate
          : (ParticleConfig){.position = c->source,
                             .lifetime = bodyLifetime,
                             .radius = c->particleRadius,
                             .colorStart = VC_WithAlpha(material->body, 220),
                             .colorEnd = VC_WithAlpha(material->body, 0),
                             .render = {.blendMode = VFX_BLEND_ALPHA,
                                        .unlit = 1,
                                        .emissiveBoost = 1.6f}};
  p.physics.receiveMotionFields = true;
  p.physics.initialGuide = h;
  if (!p.physics.dynamics)
    p.physics.dynamics = &body;
  ParticleEmitterDesc desc = {
      .simulationPolicy = PARTICLE_SIM_AUTO,
      .renderMode = c->renderMode,
      .particle = p,
      .moduleFlags = PARTICLE_MODULE_PATH_FOLLOW,
      .debugName = "Guided particles",
      .source = {.type = PARTICLE_SOURCE_MESH_EDGE,
                 .mesh = &s_guidedSourceMesh,
                 .transform = MatrixMultiply(
                     MatrixScale(c->formationRadius, c->formationRadius,
                                 c->formationRadius),
                     MatrixTranslate(c->source.x, c->source.y, c->source.z))}};
  if (c->emissionSource)
    desc.source = *c->emissionSource;
  if (stream) {
    *stream = (VC_GuidedStream){.active = true,
                                .emission = {.durationSeconds = c->emitDuration,
                                             .ratePerSecond = c->emissionRate},
                                .body = *p.physics.dynamics};
    desc.particle.physics.dynamics = &stream->body;
  }
  ParticleEmitterHandle emitter = ParticleManager_CreateEmitter(&desc);
  if (emitter == PARTICLE_EMITTER_INVALID) {
    MotionFields_Stop(h);
    if (stream)
      stream->active = false;
    return 0;
  }
  if (c->count > 0)
    ParticleManager_Emit(emitter, c->count);
  if (c->surfaceStreamOut)
    ParticleManager_GetSurfaceStream(emitter, c->surfaceStreamOut);
  if (stream)
    stream->emitter = emitter;
  else
    ParticleManager_DestroyEmitter(emitter);
  return h;
}
void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target) {
  VFX_GuidedParticleConfig c = VFX_GuidedParticle_DefaultConfig();
  c.source = source;
  c.target = target;
  /* Compatibility demonstration explicitly opts into its blast. No engine
   * receiver or unconfigured guided composition creates one implicitly. */
  c.targetPreset = VFX_GUIDED_TARGET_BLAST;
  c.targetLifetime = 0.75f;
  VFX_ComposeGuidedParticleEx(&c);
}

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
  c.duration = 4;
  c.count = 512;
  c.emissionRate = 160;
  c.formationRadius = 0.10f;
  c.particleRadius = 0.11f;
  c.massKg = 0.004f;
  c.speed = 3;
  c.drag = 2;
  c.guideRadius = 1.2f;
  c.maxForceNewtons = 0.32f;
  c.material = VC_MAT_LIGHTNING;
  c.densityKgM3 = 600;
  return c;
}
int VFX_GuidedParticle_GetParams(VFX_GuidedParticleConfig *c, VFX_ParamDef *out,
                                 int max) {
  if (!c || !out || max <= 0) return 0;
  int n = 0;
  const char *group = "Field";
#define GUIDE_FLOAT(label, member, lo, hi, step) \
  do { if (n < max) out[n++] = (VFX_ParamDef){.name=label, .group=group, \
    .type=VFX_PARAM_FLOAT, .valPtr=&c->member, .minFloat=lo, \
    .maxFloat=hi, .stepFloat=step}; } while (0)
  /* Override identity is fixed for a cast; changing numeric values never
   * inserts/removes rows or changes selection to another property. */
  if (!c->fieldOverride) {
    GUIDE_FLOAT("Speed m/s", speed, 0, 20, .25f);
    GUIDE_FLOAT("Field radius m", guideRadius, .1f, 8, .1f);
    GUIDE_FLOAT("Pull force N", maxForceNewtons, 0, 5, .02f);
    GUIDE_FLOAT("Swirl m/s", swirlSpeed, -8, 8, .5f);
    GUIDE_FLOAT("Turbulence m/s", turbulenceSpeed, 0, 8, .2f);
    GUIDE_FLOAT("Lifetime s", duration, .1f, 30, .5f);
  }
  group = "Emission";
  if (n < max) out[n++] = (VFX_ParamDef){.name="Burst count", .group=group,
    .type=VFX_PARAM_INT, .valPtr=&c->count, .minInt=0, .maxInt=2048};
  GUIDE_FLOAT("Emit duration s", emitDuration, 0, 20, .5f);
  GUIDE_FLOAT("Emission rate /s", emissionRate, 0, 1000, 25);
  if (!c->emissionSource)
    GUIDE_FLOAT("Source radius m", formationRadius, 0, 2, .025f);
  if (!c->particleTemplate) {
    group = "Particle";
    GUIDE_FLOAT("Particle radius m", particleRadius, .005f, .2f, .005f);
    GUIDE_FLOAT("Drag /s", drag, 0, 20, .25f);
    GUIDE_FLOAT("Mass kg", massKg, .000001f, 1, .0001f);
    GUIDE_FLOAT("Density kg/m3", densityKgM3, .1f, 2000, 100);
  }
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
static bool VC_GuidedSettingsValid(const VFX_GuidedParticleConfig *c) {
  if (!c || !Field_FiniteVector(c->source) || c->count < 0 || c->count > 2048 ||
      !isfinite(c->emitDuration) || c->emitDuration < 0 ||
      (c->emitDuration > 0 && (!isfinite(c->emissionRate) || c->emissionRate < 0)))
    return false;
  if (!c->fieldOverride && (!Field_FiniteVector(c->target) ||
      !isfinite(c->speed) || c->speed < 0 || !isfinite(c->duration) || c->duration <= 0 ||
      !isfinite(c->guideRadius) || c->guideRadius <= 0 ||
      !isfinite(c->maxForceNewtons) || c->maxForceNewtons < 0 ||
      !isfinite(c->swirlSpeed) || !isfinite(c->turbulenceSpeed) || c->turbulenceSpeed < 0))
    return false;
  bool emitting = c->count > 0 || (c->emitDuration > 0 && c->emissionRate > 0);
  if (emitting && (c->renderMode < PARTICLE_RENDER_BILLBOARD ||
      c->renderMode > PARTICLE_RENDER_SURFACE_INPUT ||
      (!c->emissionSource && (!isfinite(c->formationRadius) || c->formationRadius < 0)) ||
      (!c->particleTemplate && (!isfinite(c->particleRadius) || c->particleRadius <= 0 ||
        !isfinite(c->massKg) || c->massKg <= 0 || !isfinite(c->densityKgM3) || c->densityKgM3 <= 0 ||
        !isfinite(c->drag) || c->drag < 0)))) return false;
  return true;
}
static bool VC_GuidedBuildField(const VFX_GuidedParticleConfig *c, FieldDesc *field) {
  if (c->fieldOverride) { *field = *c->fieldOverride; return true; }
  *field = MotionField_Default();
  field->volume.radiusM = c->guideRadius;
  field->volume.coreFraction = .25f;
  field->transform.position = c->source;
  field->lifetime.durationSec = c->duration;
  field->lifetime.attackSec = fminf(.1f, c->duration * .1f);
  field->lifetime.fadeSec = fminf(.3f, c->duration * .1f);
  field->forceLawCount = 1;
  field->forceLaws[0] = (ForceLaw){.type=FORCE_LAW_RADIAL_ATTRACTION,
      .magnitudeNewtons=c->maxForceNewtons};
  Vector3 delta = MotionVec_Sub(c->target, c->source);
  float len = MotionVec_Length(delta);
  if (len > .001f && c->speed > 0) {
    Vector3 up = {0, 1, 0};
    Vector3 a = MotionVec_Add(MotionVec_Scale(delta, .33f), MotionVec_Scale(up, len * .06f));
    Vector3 b = MotionVec_Add(MotionVec_Scale(delta, .67f), MotionVec_Scale(up, len * .02f));
    Vector3 points[33];
    for (int i=0; i<33; i++)
      points[i] = GetBezierPoint((Vector3){0}, a, b, delta, (float)i/32);
    if (!MotionPath_Build(&field->trajectory.path, points, 33)) return false;
    field->trajectory.mode = FIELD_TRAJECTORY_PATH;
    field->trajectory.speedMps = c->speed;
  }
  field->flow.enabled = true; /* Includes centre velocity even without swirl. */
  field->flow.addBackgroundVelocity = true;
  field->flow.axis = len > .001f ? MotionVec_Normalize(delta) : (Vector3){0,1,0};
  field->flow.procedural.swirlSpeedMps = c->swirlSpeed;
  field->flow.procedural.turbulenceSpeedMps = c->turbulenceSpeed;
  field->flow.procedural.eddyLengthM = c->guideRadius * .3f;
  return true;
}
static ParticleDynamicsProfile VC_GuidedBody(const VFX_GuidedParticleConfig *c) {
  /* The simple control is a linear response to relative airflow. A template
   * can instead supply mass/area/Cd quadratic aerodynamics without stacking. */
  return (ParticleDynamicsProfile){.inverseMassKg=1/c->massKg,
      .densityKgM3=c->densityKgM3, .gravityScale=1,
      .windSusceptibility=1, .windCouplingHz=c->drag};
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
  FieldDesc field;
  if (!VC_GuidedBuildField(c, &field)) return 0;
  MotionFieldHandle h = MotionFields_CreateField(&field);
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
  if (!c->particleTemplate) body = VC_GuidedBody(c);
  float bodyLifetime = field.lifetime.startDelaySec + field.lifetime.durationSec;
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
  p.physics.initialGuide = 0; /* Spatial sampling only; never captured. */
  if (!c->particleTemplate)
    p.physics.dynamics = &body;
  ParticleEmitterDesc desc = {
      .simulationPolicy = PARTICLE_SIM_AUTO,
      .renderMode = c->renderMode,
      .particle = p,
      .moduleFlags = 0,
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
                                .body = p.physics.dynamics ? *p.physics.dynamics : body};
    if (p.physics.dynamics) desc.particle.physics.dynamics = &stream->body;
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
  VFX_ComposeGuidedParticleEx(&c);
}

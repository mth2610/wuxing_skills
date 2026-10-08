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
  c.emissionRate = 0; /* Deprecated; continuous rate is derived from count/time. */
  c.formationRadius = 0.45f;
  c.particleRadius = 0.11f;
  c.massKg = 0.004f;
  c.speed = 12;
  c.drag = 2;
  c.guideRadius = 1.2f;
  c.maxForceNewtons = 0.32f;
  c.forwardForceNewtons = 0.08f;
  c.swirlSpeed = 14.4f;
  c.turbulenceSpeed = 9.6f;
  c.material = VC_MAT_LIGHTNING;
  c.densityKgM3 = 600;
  c.gravityScale = 1;
  c.guidancePreset = GUIDE_BALANCED;
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
    static const char * const guidanceNames[]={"Manual","Loose","Balanced","Tight"};
    const char *speedName=c->emitDuration>0?"Stream speed m/s":"Field travel speed m/s";
    if(n<max) out[n++]=(VFX_ParamDef){.name=speedName,.group=group,
      .type=VFX_PARAM_FLOAT,.valPtr=&c->speed,.minFloat=0,.maxFloat=60,.stepFloat=1.0f};
    GUIDE_FLOAT("Field radius m", guideRadius, .1f, 8, .1f);
    if(n<max) out[n++]=(VFX_ParamDef){.name="Guidance",
      .group=group,.type=VFX_PARAM_ENUM,.valPtr=&c->guidancePreset,
      .minInt=GUIDE_MANUAL,.maxInt=GUIDE_TIGHT,
      .enumNames=guidanceNames,.enumCount=4};
    if(c->guidancePreset==GUIDE_MANUAL) {
      GUIDE_FLOAT("Pull force N", maxForceNewtons, 0, 5, .02f);
      GUIDE_FLOAT("Forward force N", forwardForceNewtons, 0, 5, .02f);
    }
    /* Absolute m/s API; inspector speed edits preserve authored flow ratios.
     * Bounds grow with travel speed instead of imposing a slow-effect ceiling. */
    float flowMax=fmaxf(24,2*c->speed);
    GUIDE_FLOAT("Swirl m/s", swirlSpeed, -flowMax, flowMax, .5f);
    GUIDE_FLOAT("Turbulence m/s", turbulenceSpeed, 0, flowMax, .5f);
    if(c->guidancePreset==GUIDE_MANUAL)
      GUIDE_FLOAT("Minimum field life s", duration, .1f, 30, .5f);
  }
  group = "Emission";
  if (n < max) out[n++] = (VFX_ParamDef){.name="Particle count", .group=group,
    .type=VFX_PARAM_INT, .valPtr=&c->count, .minInt=0, .maxInt=2048};
  GUIDE_FLOAT("Emission time s", emitDuration, 0, 20, .5f);
  if (!c->emissionSource)
    GUIDE_FLOAT("Source radius m", formationRadius, 0, 2, .025f);
  if (!c->particleTemplate) {
    group = "Particle";
    GUIDE_FLOAT("Particle radius m", particleRadius, .005f, .2f, .005f);
    if(c->guidancePreset==GUIDE_MANUAL)
      GUIDE_FLOAT("Drag /s", drag, 0, 20, .25f);
    GUIDE_FLOAT("Mass kg", massKg, .000001f, 1, .0001f);
    GUIDE_FLOAT("Density kg/m3", densityKgM3, .1f, 2000, 100);
    GUIDE_FLOAT("Gravity scale", gravityScale, -2, 2, .1f);
  }
#undef GUIDE_FLOAT
  return n;
}
static float VC_GuidedEmissionRate(const VFX_GuidedParticleConfig *c) {
  return c && c->emitDuration>0 && c->count>0
      ? (float)c->count/c->emitDuration : 0;
}
static bool VC_GuidedUsesTimedEmission(const VFX_GuidedParticleConfig *c) {
  return c && c->emitDuration>0 && c->count>0;
}
static int VC_GuidedInitialBurstCount(const VFX_GuidedParticleConfig *c) {
  return c && c->emitDuration<=0 ? c->count : 0;
}
static GuideTuning VC_GuidedTuning(const VFX_GuidedParticleConfig *c) {
  BodyPhysicalProperties reference={.massKg=c->massKg};
  ParticleDynamicsProfile referenceProfile={.densityKgM3=c->densityKgM3,
      .gravityScale=c->gravityScale};
  float gravity=ParticleDynamics_GravityAcceleration(&referenceProfile);
  if(c->particleTemplate) {
    const ParticleDynamicsProfile *p=c->particleTemplate->physics.dynamics;
    reference=p?MotionBody_GetPhysicalProperties(p):(BodyPhysicalProperties){.massKg=1};
    gravity=p?ParticleDynamics_GravityAcceleration(p):0;
  }
  return GuideTuning_Derive(&reference,c->guideRadius,c->speed,c->swirlSpeed,
      c->turbulenceSpeed,gravity,(GuidePreset)c->guidancePreset);
}
static float VC_GuidedEstimatedTransitTime(const VFX_GuidedParticleConfig *c,
    float pathLength,bool continuous) {
  if(!c || pathLength<=0 || c->speed<=1e-5f) return 0;
  float travel=pathLength/c->speed;
  if(c->guidancePreset!=GUIDE_MANUAL)
    return travel+VC_GuidedTuning(c).settlingTimeSec;
  if(!continuous) return travel;
  float mass=c->massKg,couplingHz=c->drag;
  if(c->particleTemplate) {
    const ParticleDynamicsProfile *d=c->particleTemplate->physics.dynamics;
    if(!d) return travel;
    mass=d->inverseMassKg>0?1.0f/d->inverseMassKg:0;
    couplingHz=d->windCouplingHz;
  }
  float responseHz=fmaxf(couplingHz,0);
  if(mass>0 && c->forwardForceNewtons>0)
    responseHz+=c->forwardForceNewtons/(mass*c->speed);
  if(responseHz>1e-5f) travel+=1.0f/responseHz;
  return travel;
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
      c->guidancePreset<GUIDE_MANUAL || c->guidancePreset>GUIDE_TIGHT ||
      !isfinite(c->emitDuration) || c->emitDuration < 0)
    return false;
  if(c->emitDuration>0 && c->count>0 &&
     (!isfinite(VC_GuidedEmissionRate(c)) || VC_GuidedEmissionRate(c)<=0)) return false;
  if (!c->fieldOverride && (!Field_FiniteVector(c->target) ||
      !isfinite(c->speed) || c->speed < 0 || !isfinite(c->duration) || c->duration <= 0 ||
      !isfinite(c->guideRadius) || c->guideRadius <= 0 ||
      (c->guidancePreset==GUIDE_MANUAL && (!isfinite(c->maxForceNewtons) || c->maxForceNewtons < 0 ||
      !isfinite(c->forwardForceNewtons) || c->forwardForceNewtons < 0)) ||
      !isfinite(c->swirlSpeed) || !isfinite(c->turbulenceSpeed) || c->turbulenceSpeed < 0))
    return false;
  bool emitting = c->count > 0;
  if (emitting && (c->renderMode < PARTICLE_RENDER_BILLBOARD ||
      c->renderMode > PARTICLE_RENDER_SURFACE_INPUT ||
      (!c->emissionSource && (!isfinite(c->formationRadius) || c->formationRadius < 0)) ||
      (!c->particleTemplate && (!isfinite(c->particleRadius) || c->particleRadius <= 0 ||
        !isfinite(c->massKg) || c->massKg <= 0 || !isfinite(c->densityKgM3) || c->densityKgM3 <= 0 ||
        (c->guidancePreset==GUIDE_MANUAL && (!isfinite(c->drag) || c->drag < 0)) || !isfinite(c->gravityScale) ||
        c->gravityScale < -2 || c->gravityScale > 2)))) return false;
  return true;
}
static bool VC_GuidedBuildField(const VFX_GuidedParticleConfig *c, FieldDesc *field) {
  if (c->fieldOverride) { *field = *c->fieldOverride; return true; }
  GuideTuning tuning=VC_GuidedTuning(c);
  bool automatic=c->guidancePreset!=GUIDE_MANUAL;
  float pull=automatic?tuning.maxForceNewtons:c->maxForceNewtons;
  float forward=automatic?tuning.forwardForceNewtons:c->forwardForceNewtons;
  float stiffness=automatic?tuning.stiffnessNPerM:pull/c->guideRadius;
  if(!isfinite(pull) || !isfinite(forward) || !isfinite(stiffness) ||
      (automatic && stiffness<=0)) return false;
  *field = MotionField_Default();
  field->volume.radiusM = c->guideRadius;
  field->volume.coreFraction = .25f;
  field->transform.position = c->source;
  bool continuous = c->emitDuration > 0;
  float fadeSec = fminf(.3f, c->duration * .1f);
  Vector3 delta = MotionVec_Sub(c->target, c->source);
  float len = MotionVec_Length(delta);
  float pathLength=0;
  if (len > .001f) {
    /* Visible lateral bow and lift in a stable orthonormal frame. The path
     * remains valid for vertical casts; fieldOverride keeps caller paths exact. */
    Vector3 tangent=MotionVec_Scale(delta,1/len);
    Vector3 side=MotionVec_Cross((Vector3){0,1,0},tangent);
    if(MotionVec_Length(side)<.1f) side=MotionVec_Cross((Vector3){1,0,0},tangent);
    side=MotionVec_Normalize(side);
    Vector3 lift=MotionVec_Cross(tangent,side);
    Vector3 bow=MotionVec_Add(MotionVec_Scale(side,len*.45f),
                             MotionVec_Scale(lift,len*.18f));
    Vector3 a=MotionVec_Add(MotionVec_Scale(delta,.28f),bow);
    Vector3 b=MotionVec_Add(MotionVec_Scale(delta,.72f),bow);
    Vector3 points[33];
    for (int i=0; i<33; i++)
      points[i] = GetBezierPoint((Vector3){0}, a, b, delta, (float)i/32);
    MotionPath path;
    if (!MotionPath_Build(&path, points, 33)) return false;
    pathLength=path.length;
    field->forceLawCount = 1;
    if (continuous) {
      /* Continuous births share one stationary corridor from A to B. */
      field->volume.shape = FIELD_PATH_TUBE;
      field->volume.path = path;
      field->preservePathLanes = true;
      field->rotatePathLanes = automatic;
      field->forceLaws[0] = (ForceLaw){
          .type=FORCE_LAW_PATH_GUIDE,
          .magnitudeNewtons=pull,
          .springStiffnessNPerM=stiffness,
          .forwardForceNewtons=forward};
      field->flow.followSpeedMps = c->speed;
    } else {
      /* A burst gets a spherical field that travels along the same path. */
      field->trajectory.mode = FIELD_TRAJECTORY_PATH;
      field->trajectory.path = path;
      field->trajectory.speedMps = c->speed;
      field->forceLaws[0] = (ForceLaw){.type=automatic?FORCE_LAW_MOVING_GUIDE:FORCE_LAW_RADIAL_ATTRACTION,
          .magnitudeNewtons=pull,.springStiffnessNPerM=stiffness};
      field->preserveSphereOffsets=automatic;
    }
  } else {
    field->forceLawCount = 1;
    field->forceLaws[0] = (ForceLaw){.type=automatic?FORCE_LAW_MOVING_GUIDE:FORCE_LAW_RADIAL_ATTRACTION,
        .magnitudeNewtons=pull,.springStiffnessNPerM=stiffness};
    field->preserveSphereOffsets=automatic;
  }
  field->flow.enabled = true;
  field->flow.addBackgroundVelocity = true;
  field->flow.axis = len > .001f ? MotionVec_Normalize(delta) : (Vector3){0,1,0};
  field->flow.procedural.swirlSpeedMps = c->swirlSpeed;
  /* Automatic guidance is a force-field actuator, not atmospheric turbulence.
   * Compile one fixed Newton budget from reference mass and eddy turnover:
   * F=m_ref*U^2/L. Receiver mass remains free to determine acceleration.
   * Manual mode retains the legacy airflow meaning. Do not apply both. */
  field->flow.procedural.turbulenceSpeedMps = automatic?0:c->turbulenceSpeed;
  if(automatic && c->turbulenceSpeed>0) {
    if(!isfinite(tuning.turbulenceForceNewtons)) return false;
    field->forceLaws[field->forceLawCount++]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,
      .magnitudeNewtons=tuning.turbulenceForceNewtons,
      .procedural={.turbulenceSpeedMps=tuning.turbulenceSpeedMps,
                   .eddyLengthM=tuning.eddyLengthM}};
  }
  field->flow.procedural.eddyLengthM = c->guideRadius * .3f;
  float transitSec=VC_GuidedEstimatedTransitTime(c,pathLength,continuous);
  float emissionSec=continuous?c->emitDuration:0;
  float requiredLife=emissionSec+transitSec+fadeSec;
  if(!isfinite(requiredLife)) return false;
  /* `duration` remains an authorable minimum/fallback. Routed fields also live
   * through the last emitted particle's estimated A-to-B transit and fade. */
  field->lifetime.durationSec=automatic && pathLength>0 && c->speed>1e-5f?requiredLife:
      fmaxf(c->duration,requiredLife);
  field->lifetime.attackSec=fminf(.1f,c->duration*.1f);
  field->lifetime.fadeSec=fadeSec;
  return true;
}
static ParticleDynamicsProfile VC_GuidedBody(const VFX_GuidedParticleConfig *c) {
  if(c->guidancePreset!=GUIDE_MANUAL) {
    BodyPhysicalProperties b=BodyPhysicalProperties_Sphere(c->massKg,c->densityKgM3,.47f);
    return (ParticleDynamicsProfile){.inverseMassKg=1/c->massKg,
      .densityKgM3=c->densityKgM3,.gravityScale=c->gravityScale,
      .windSusceptibility=1,.aerodynamicAreaM2=b.projectedAreaM2,
      .aerodynamicDragCoefficient=b.dragCoefficient};
  }
  /* The simple control is a linear response to relative airflow. A template
   * can instead supply mass/area/Cd quadratic aerodynamics without stacking. */
  return (ParticleDynamicsProfile){.inverseMassKg=1/c->massKg,
      .densityKgM3=c->densityKgM3, .gravityScale=c->gravityScale,
      .windSusceptibility=1, .windCouplingHz=c->drag};
}
MotionFieldHandle
VFX_ComposeGuidedParticleEx(const VFX_GuidedParticleConfig *c) {
  if (c && c->surfaceStreamOut)
    *c->surfaceStreamOut = (ParticleRenderStream){0};
  if (!VC_GuidedSettingsValid(c)) return 0;
  VC_GuidedStream *stream = NULL;
  if (VC_GuidedUsesTimedEmission(c)) {
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
  p.physics.spatialMotionOnly = true;
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
                                             .ratePerSecond = VC_GuidedEmissionRate(c)},
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
  int initialBurst=VC_GuidedInitialBurstCount(c);
  if (initialBurst>0)
    ParticleManager_Emit(emitter, initialBurst);
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

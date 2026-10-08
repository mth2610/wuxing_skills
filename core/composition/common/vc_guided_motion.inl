#ifndef VC_GUIDED_MOTION_INL
#define VC_GUIDED_MOTION_INL
/* Composition owns casts; Emitter owns births, Motion owns movement,
 * Particle/Trail own their state and geometry. */
#include "core/motion/motion_body.h"
#include "core/path_spline.h"
#include "core/emitter/emitter_sinks.h"
#include "core/emitter/particle_source.h"
#define VC_GUIDED_MAX_STREAMS 8
typedef struct {
  bool active;
  ParticleEmitterHandle emitter;
  EmissionHandle particles, trails;
  ParticleDynamicsProfile body;
  ParticleConfig particle;
  TrailRibbonConfig ribbon;
  EmissionParticleSourceAdapter source;
  EmissionParticleSink sink;
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
  c.trailCount=8; c.trailLength=.8f; c.trailWidth=.08f; c.trailNodes=24;
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
static void VC_GuidedStream_Clear(VC_GuidedStream *s) {
  Emission_Destroy(s->particles); Emission_Destroy(s->trails);
  ParticleManager_DestroyEmitter(s->emitter);
  s->particles=s->trails=EMISSION_HANDLE_INVALID;
  s->emitter=PARTICLE_EMITTER_INVALID; s->active=false;
}
static bool VC_GuidedSchedule_Update(EmissionHandle *h,float dt) {
  if(!*h) return true;
  EmissionStats stats;
  if(!Emission_Step(*h,(Vector3){0},dt) || !Emission_GetStats(*h,&stats) || !stats.emitting) {
    Emission_Destroy(*h); *h=EMISSION_HANDLE_INVALID; return true;
  }
  return false;
}
static void VC_GuidedMotion_Update(float dt) {
  if(!isfinite(dt) || dt<=0) return;
  for(int i=0;i<VC_GUIDED_MAX_STREAMS;i++) {
    VC_GuidedStream *s=&s_guidedStreams[i];
    if(!s->active) continue;
    bool particlesDone=VC_GuidedSchedule_Update(&s->particles,dt);
    bool trailsDone=VC_GuidedSchedule_Update(&s->trails,dt);
    if(particlesDone && trailsDone) VC_GuidedStream_Clear(s);
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
  field->receiverMask = MOTION_RECEIVER_ALL_COMPONENTS;
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
VFX_GuidedMotionConfig VFX_GuidedMotion_DefaultConfig(void) {
  VFX_GuidedMotionConfig c=VFX_GuidedParticle_DefaultConfig();
  c.output=VFX_GUIDED_BOTH;
  return c;
}
int VFX_GuidedMotion_GetParams(VFX_GuidedMotionConfig *c,VFX_ParamDef *out,int max) {
  if(!c || !out || max<=0) return 0;
  static const char *const outputNames[]={"Particles","Trails","Particles + trails"};
  int n=0;
  out[n++]=(VFX_ParamDef){.name="Output",.group="Emission",.type=VFX_PARAM_ENUM,
    .valPtr=&c->output,.minInt=VFX_GUIDED_PARTICLES,.maxInt=VFX_GUIDED_BOTH,
    .enumNames=outputNames,.enumCount=3};
  VFX_ParamDef base[32];
  int count=VFX_GuidedParticle_GetParams(c,base,32);
  for(int i=0;i<count && n<max;i++) {
    bool particleRow=base[i].valPtr==&c->count || base[i].valPtr==&c->particleRadius;
    if(c->output==VFX_GUIDED_TRAILS && particleRow) continue;
    if(!strcmp(base[i].group,"Particle") && base[i].valPtr!=&c->particleRadius) base[i].group="Body";
    out[n++]=base[i];
  }
#define GUIDED_TRAIL_PARAM(label,member,typeValue,lo,hi,step) \
  do {if(n<max) out[n++]=(VFX_ParamDef){.name=label,.group="Trail", \
    .type=typeValue,.valPtr=&c->member,.minFloat=lo,.maxFloat=hi,.stepFloat=step, \
    .minInt=(int)(lo),.maxInt=(int)(hi)};} while(0)
  if(c->output!=VFX_GUIDED_PARTICLES) {
    GUIDED_TRAIL_PARAM("Trail count",trailCount,VFX_PARAM_INT,0,64,1);
    if(!c->trailTemplate) {
      GUIDED_TRAIL_PARAM("Trail length m",trailLength,VFX_PARAM_FLOAT,.05f,4,.05f);
      GUIDED_TRAIL_PARAM("Trail width m",trailWidth,VFX_PARAM_FLOAT,.005f,.5f,.005f);
      GUIDED_TRAIL_PARAM("Trail nodes",trailNodes,VFX_PARAM_INT,2,60,1);
    }
  }
#undef GUIDED_TRAIL_PARAM
  return n;
}
static bool VC_GuidedMotion_Valid(const VFX_GuidedMotionConfig *c) {
  if(!c || c->output<VFX_GUIDED_PARTICLES || c->output>VFX_GUIDED_BOTH) return false;
  VFX_GuidedParticleConfig check=*c;
  if(c->output==VFX_GUIDED_TRAILS) {
    check.count=c->trailCount; check.particleRadius=1; check.renderMode=PARTICLE_RENDER_BILLBOARD;
  }
  if(!VC_GuidedSettingsValid(&check)) return false;
  if(c->output!=VFX_GUIDED_PARTICLES && (c->trailCount<0 || c->trailCount>64 ||
      (!c->trailTemplate && (!isfinite(c->trailLength) || c->trailLength<=0 ||
       !isfinite(c->trailWidth) || c->trailWidth<=0 || c->trailNodes<2 ||
       c->trailNodes>TRAIL_RIBBON_MAX_NODES)))) return false;
  return true;
}
MotionFieldHandle VFX_ComposeGuidedMotionEx(const VFX_GuidedMotionConfig *c) {
  if(c && c->surfaceStreamOut) *c->surfaceStreamOut=(ParticleRenderStream){0};
  if(!VC_GuidedMotion_Valid(c)) return MOTION_FIELD_INVALID;
  int particles=c->output==VFX_GUIDED_TRAILS?0:
    (c->emitDuration>0?c->count:VC_GuidedInitialBurstCount(c));
  int trails=c->output==VFX_GUIDED_PARTICLES?0:c->trailCount;
  VC_GuidedStream *s=NULL;
  if(particles || trails) {
    for(int i=0;i<VC_GUIDED_MAX_STREAMS;i++) if(!s_guidedStreams[i].active) {s=&s_guidedStreams[i];break;}
    if(!s) return MOTION_FIELD_INVALID;
  }
  FieldDesc field;
  if(!VC_GuidedBuildField(c,&field)) return MOTION_FIELD_INVALID;
  MotionFieldHandle h=MotionFields_CreateField(&field);
  if(!h || !s) return h;
  *s=(VC_GuidedStream){.active=true,.emitter=PARTICLE_EMITTER_INVALID};
  if(!c->emissionSource && !s_guidedSourceReady) {
    Mesh mesh=GenMeshSphere(1,16,10);
    MeshAdjacency_Build(&s_guidedSourceMesh,mesh); UnloadMesh(mesh);
    s_guidedSourceReady=true;
  }
  s->source.source=c->emissionSource?*c->emissionSource:(ParticleEmissionSource){
    .type=PARTICLE_SOURCE_MESH_EDGE,.mesh=&s_guidedSourceMesh,
    .transform=MatrixMultiply(MatrixScale(c->formationRadius,c->formationRadius,c->formationRadius),
      MatrixTranslate(c->source.x,c->source.y,c->source.z))};
  s->source.defaultPosition=c->particleTemplate?c->particleTemplate->position:c->source;
  s->body=c->particleTemplate
    ?(c->particleTemplate->physics.dynamics?*c->particleTemplate->physics.dynamics:(ParticleDynamicsProfile){0})
    :VC_GuidedBody(c);
  const VFX_ElementMaterial *material=VFX_Material(c->material);
  float bodyLifetime=field.lifetime.startDelaySec+field.lifetime.durationSec;
  s->particle=c->particleTemplate?*c->particleTemplate:(ParticleConfig){
    .position=c->source,.lifetime=bodyLifetime,.radius=c->particleRadius,
    .colorStart=VC_WithAlpha(material->body,220),.colorEnd=VC_WithAlpha(material->body,0),
    .render={.blendMode=VFX_BLEND_ALPHA,.unlit=1,.emissiveBoost=1.6f}};
  ParticleConfig *p=&s->particle;
  ParticleConfig_Unify(p);
  if(c->particleTemplate) s->source.defaultPosition=p->position;
  p->physics.receiveMotionFields=true; p->physics.spatialMotionOnly=true;
  p->physics.initialGuide=0;
  if(!c->particleTemplate || p->physics.dynamics) p->physics.dynamics=&s->body;
  s->ribbon=c->trailTemplate?*c->trailTemplate:TrailRibbon_Default();
  if(!c->trailTemplate) {
    s->ribbon.nodeCount=c->trailNodes; s->ribbon.lengthM=c->trailLength;
    s->ribbon.widthM=c->trailWidth; s->ribbon.lifetimeSec=bodyLifetime;
    s->ribbon.color=VC_WithAlpha(material->body,180);
    s->ribbon.material.body=s->body;
    s->ribbon.mode=c->trailAttachment?TRAIL_RIBBON_HEAD_ANCHORED:TRAIL_RIBBON_FREE;
    s->ribbon.attachment=c->trailAttachment;
    Vector3 delta=MotionVec_Sub(c->source,c->target);
    s->ribbon.tailDirection=MotionVec_Length(delta)>.001f?MotionVec_Normalize(delta):(Vector3){0,-1,0};
  }
  if(particles) {
    ParticleEmitterDesc desc={.simulationPolicy=PARTICLE_SIM_AUTO,.renderMode=c->renderMode,
      .particle=*p,.debugName="Guided motion particles",
      .source={.type=PARTICLE_SOURCE_CONFIG_POSITION}};
    s->emitter=ParticleManager_CreateEmitter(&desc);
    if(s->emitter==PARTICLE_EMITTER_INVALID || ParticleManager_GetEmitterStatus(s->emitter)!=PARTICLE_EMITTER_OK) goto fail;
    s->sink.emitter=s->emitter;
  }
  EmissionConfig emission={.schedule=(VC_GuidedUsesTimedEmission(c) || (c->emitDuration>0 && trails>0))
      ?EMISSION_TIMED_COUNT:EMISSION_BURST,
    .duration=c->emitDuration,.seed=0x47554944u,.sampleSource=EmissionSource_Particle,
    .source=&s->source,.callbackBudget=2048};
  if(particles) {
    emission.kind=EMISSION_PARTICLE; emission.count=(uint32_t)particles;
    emission.sink=EmissionSink_Particle; emission.sinkUser=&s->sink; emission.spawnTemplate=p;
    s->particles=Emission_Create(&emission,(Vector3){0});
    if(!s->particles) goto fail;
  }
  if(trails) {
    emission.kind=EMISSION_RIBBON; emission.count=(uint32_t)trails;
    emission.sink=EmissionSink_Ribbon; emission.sinkUser=NULL; emission.spawnTemplate=&s->ribbon;
    s->trails=Emission_Create(&emission,(Vector3){0});
    if(!s->trails) goto fail;
  }
  if(particles && c->surfaceStreamOut) ParticleManager_GetSurfaceStream(s->emitter,c->surfaceStreamOut);
  if(c->emitDuration==0) {
    VC_GuidedSchedule_Update(&s->particles,0); VC_GuidedSchedule_Update(&s->trails,0);
    VC_GuidedStream_Clear(s);
  }
  return h;
fail:
  VC_GuidedStream_Clear(s); MotionFields_Stop(h); return MOTION_FIELD_INVALID;
}
MotionFieldHandle VFX_ComposeGuidedParticleEx(const VFX_GuidedParticleConfig *c) {
  if(!c) return MOTION_FIELD_INVALID;
  VFX_GuidedMotionConfig legacy=*c; legacy.output=VFX_GUIDED_PARTICLES;
  return VFX_ComposeGuidedMotionEx(&legacy);
}
void VFX_ComposeGuidedMotion(Vector3 source,Vector3 target) {
  VFX_GuidedMotionConfig c=VFX_GuidedMotion_DefaultConfig();
  c.source=source; c.target=target; VFX_ComposeGuidedMotionEx(&c);
}
void VFX_ComposeGuidedParticle(Vector3 source, Vector3 target) {
  VFX_GuidedParticleConfig c=VFX_GuidedParticle_DefaultConfig();
  c.source=source; c.target=target; VFX_ComposeGuidedParticleEx(&c);
}
#endif

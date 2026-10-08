#!/usr/bin/env python3
"""Run production Guided settings/inspector with data-only dependency stubs."""
import pathlib,re,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
s=(ROOT/'core/composition/common/vc_guided_motion.inl').read_text()
h=(ROOT/'core/composition/visual_composer.h').read_text()
def function(name):
 m=re.search(r'^(?:static bool|static float|static int|static ParticleDynamicsProfile|static MotionFieldRecipe|static GuideTuning|VFX_GuidedParticleConfig|int) '+name+r'\(',s,re.M); start=s.index('{',m.start()); depth=1; end=start+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
enums='\n'.join(re.search(r'typedef enum \{[^}]*\} '+name+r';',h,re.S).group() for name in ('VFX_GuidedOutput','VFX_GuidedPattern','VFX_GuidedTrailStyle','VFX_GuidedTrailMotion'))
config=re.search(r'typedef struct VFX_GuidedParticleConfig \{.*?\} VFX_GuidedParticleConfig;',h,re.S).group()
stubs=r'''
#include "core/motion/motion_fields.h"
#include "core/composition/vc_emission.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int VC_MaterialId;
typedef enum {PARTICLE_RENDER_BILLBOARD=0,PARTICLE_RENDER_SURFACE_INPUT=3} ParticleRenderMode;
typedef struct {int placeholder;} ParticleRenderStream;
typedef struct {struct {ParticleDynamicsProfile *dynamics;} physics;} ParticleConfig;
typedef struct {int placeholder;} ParticleEmissionSource;
typedef struct {int placeholder;} Camera3D;
typedef struct {int placeholder;} Mesh;
typedef struct {int meshCount;} Model;
typedef struct {int placeholder;} Material;
typedef int BlendMode;
enum {BLEND_ALPHA,BLEND_ADDITIVE,BLEND_ALPHA_PREMULTIPLY};
#include "core/trails/trail_ribbon.h"
#include "core/motion/motion_recipe.h"
#include "core/composition/common/vc_params.h"
#include "core/motion/motion_body.h"
enum {VC_MAT_LIGHTNING};
FieldDesc MotionField_Default(void) {
 return (FieldDesc){.transform=FieldTransform_Identity(),.receiverMask=MOTION_RECEIVER_ALL};
}
static Vector3 GetBezierPoint(Vector3 p,Vector3 a,Vector3 b,Vector3 q,float t) {
 float u=1-t;
 return MotionVec_Add(MotionVec_Add(MotionVec_Scale(p,u*u*u),MotionVec_Scale(a,3*u*u*t)),
   MotionVec_Add(MotionVec_Scale(b,3*u*t*t),MotionVec_Scale(q,t*t*t)));
}
'''
main=r'''
int main(void) {
 VFX_GuidedParticleConfig c=VFX_GuidedParticle_DefaultConfig();
 assert(VC_GuidedSettingsValid(&c) && c.guidancePreset==GUIDE_BALANCED);
 c.swirlSpeed=0;c.turbulenceSpeed=0;
 c.guidancePreset=GUIDE_MANUAL;
 VFX_ParamDef a[32],b[32];int n=VFX_GuidedParticle_GetParams(&c,a,32);assert(n==16);
 assert(c.formationRadius==.45f && c.gravityScale==1);
 c.count=512;c.emitDuration=2;
 assert(fabsf(VC_GuidedEmissionRate(&c)-256)<1e-6f);
 assert(VC_GuidedUsesTimedEmission(&c) && VC_GuidedInitialBurstCount(&c)==0);
 VFX_EmissionSchedule schedule={.durationSeconds=c.emitDuration,
   .ratePerSecond=VC_GuidedEmissionRate(&c)};
 int emitted=0,firstFrame=VFX_EmissionAdvance(&schedule,1.0f/60.0f,2048);
 emitted+=firstFrame;
 for(int i=1;i<120;i++)emitted+=VFX_EmissionAdvance(&schedule,1.0f/60.0f,2048);
 assert(firstFrame>0 && firstFrame<10 && emitted==c.count && VFX_EmissionComplete(&schedule));
 c.count=0;
 assert(VFX_GuidedParticle_GetParams(&c,b,32)==n);
 for(int i=0;i<n;i++)assert(a[i].valPtr==b[i].valPtr &&
   (i==0 || !strcmp(a[i].name,b[i].name)));
 assert(!strcmp(a[0].name,"Field travel speed m/s") &&
   !strcmp(b[0].name,"Stream speed m/s"));
 assert(VFX_GuidedParticle_GetParams(&c,b,3)==3);
 assert(VFX_GuidedParticle_GetParams(NULL,b,32)==0);
 c.emitDuration=0;c.count=512;
 assert(!VC_GuidedUsesTimedEmission(&c) && VC_GuidedInitialBurstCount(&c)==512);
 c.count=1;
 c.count=1;c.drag=-1;assert(!VC_GuidedSettingsValid(&c));
 c.drag=NAN;assert(!VC_GuidedSettingsValid(&c));
 c.drag=0;assert(VC_GuidedSettingsValid(&c));
 c.forwardForceNewtons=-1;assert(!VC_GuidedSettingsValid(&c));
 c.forwardForceNewtons=.08f;
 ParticleDynamicsProfile body=VC_GuidedBody(&c);
 assert(body.gravityScale==1);c.gravityScale=0;body=VC_GuidedBody(&c);assert(body.gravityScale==0);
 Vector3 v=MotionBody_AdvanceVelocity((Vector3){10,0,0},&body,(Vector3){0},(Vector3){0},(Vector3){3,0,0},.1f);
 assert(v.x==10);
 c.drag=2;body=VC_GuidedBody(&c);
 v=MotionBody_AdvanceVelocity((Vector3){10,0,0},&body,(Vector3){0},(Vector3){0},(Vector3){3,0,0},.1f);
 assert(fabsf(v.x-(3+7*expf(-.2f)))<1e-5f);
 FieldDesc d;assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_SPHERE && d.trajectory.mode==FIELD_TRAJECTORY_PATH &&
   d.flow.enabled && d.flow.addBackgroundVelocity && d.flow.followSpeedMps==0);
 FieldTransform moving=FieldTrajectory_Transform(&d,.2f);
 assert(moving.position.x>c.source.x && moving.frameVelocityMps.x>0);
 BodyPhysicalProperties physical=BodyPhysicalProperties_Sphere(.004f,600,0);
 MediumProperties medium={.velocityMps={0,0,2}};
 Vector3 offCenter=MotionVec_Add(moving.position,(Vector3){0,0,.2f});
 FieldSample sample=Field_Evaluate(&d,.2f,offCenter,(Vector3){0},&physical,&medium);
 assert(sample.forceNewtons.z<0 &&
   MotionVec_Length(MotionVec_Sub(MotionVec_Sub(sample.mediumVelocityMps,medium.velocityMps),moving.frameVelocityMps))<1e-4f);
 sample=Field_Evaluate(&d,.2f,moving.position,(Vector3){0},&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)<1e-3f &&
   sample.mediumVelocityMps.x>0);
 sample=Field_Evaluate(&d,.2f,moving.position,moving.frameVelocityMps,&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)<1e-5f);
 sample=Field_Evaluate(&d,c.duration,offCenter,(Vector3){0},&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)==0 && sample.mediumWeight==0);
 c.count=0;c.duration=.5f;c.emitDuration=2;
 assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_PATH_TUBE && d.trajectory.mode==FIELD_TRAJECTORY_STATIC &&
   d.preservePathLanes &&
   d.flow.followSpeedMps==c.speed &&
   d.forceLaws[0].forwardForceNewtons==c.forwardForceNewtons);
 float transit=VC_GuidedEstimatedTransitTime(&c,d.volume.path.length,true);
 assert(fabsf(d.lifetime.durationSec-(c.emitDuration+transit+d.lifetime.fadeSec))<1e-5f &&
   fabsf(FieldLifetime_Weight(&d.lifetime,c.emitDuration+transit)-1)<1e-5f);
 MotionPathSample pathPoint=MotionPath_Sample(&d.volume.path,d.volume.path.length*.5f);
 assert(pathPoint.position.x>0 && pathPoint.tangent.x>.9f);
 Vector3 offPath=MotionVec_Add(MotionVec_Add(c.source,pathPoint.position),(Vector3){0,0,.2f});
 sample=Field_Evaluate(&d,.2f,offPath,(Vector3){0},&physical,&medium);
 MotionPathSample projected=MotionPath_Project(&d.volume.path,MotionVec_Sub(offPath,c.source),0,d.volume.path.count-2);
 assert(sample.forceNewtons.z<0 &&
   MotionVec_Length(MotionVec_Sub(MotionVec_Sub(sample.mediumVelocityMps,medium.velocityMps),MotionVec_Scale(projected.tangent,c.speed)))<1e-3f);
 sample=Field_Evaluate(&d,.2f,MotionVec_Add(c.source,pathPoint.position),(Vector3){0},&physical,&medium);
 assert(MotionVec_Dot(sample.forceNewtons,pathPoint.tangent)>0 &&
   sample.mediumVelocityMps.x>c.speed*.9f);
 c.duration=.1f;c.emitDuration=0;
 assert(VC_GuidedBuildField(&c,&d));
 assert(d.trajectory.mode==FIELD_TRAJECTORY_PATH &&
   fabsf(d.lifetime.durationSec-(d.trajectory.path.length/c.speed+d.lifetime.fadeSec))<1e-5f);
 c.duration=4;
 c.target=c.source;assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_SPHERE && d.trajectory.mode==FIELD_TRAJECTORY_STATIC);
 c.count=1;
 FieldDesc replacement=d;c.fieldOverride=&replacement;c.speed=NAN;c.duration=NAN;
 assert(VC_GuidedSettingsValid(&c) && VC_GuidedBuildField(&c,&d));
 assert(VFX_GuidedParticle_GetParams(&c,b,32)==8);
 ParticleConfig particle={0};c.particleTemplate=&particle;c.massKg=NAN;c.densityKgM3=NAN;c.particleRadius=NAN;c.drag=NAN;
 assert(VC_GuidedSettingsValid(&c));
 c.particleTemplate=NULL;assert(!VC_GuidedSettingsValid(&c));
 c=VFX_GuidedParticle_DefaultConfig();
 assert(VFX_GuidedParticle_GetParams(&c,b,32)==12);
 /* Controls must allow proportionate swirl/noise at skill speed. */
 c.speed=40;VFX_GuidedParticle_GetParams(&c,b,32);
 for(int i=0;i<12;i++) if(b[i].valPtr==&c.swirlSpeed || b[i].valPtr==&c.turbulenceSpeed)
   assert(b[i].maxFloat>=2*c.speed);
 c=VFX_GuidedParticle_DefaultConfig();
 assert(VC_GuidedBuildField(&c,&d));
 MotionPathSample middle=MotionPath_Sample(&d.trajectory.path,d.trajectory.path.length*.5f);
 Vector3 chord=MotionVec_Sub(c.target,c.source);
 Vector3 chordMid=MotionVec_Scale(chord,.5f);
 assert(MotionVec_Length(MotionVec_Sub(middle.position,chordMid))>MotionVec_Length(chord)*.3f);
 assert(MotionVec_Length(MotionVec_Sub(MotionPath_Sample(&d.trajectory.path,0).position,(Vector3){0}))<1e-6f);
 assert(MotionVec_Length(MotionVec_Sub(MotionPath_Sample(&d.trajectory.path,d.trajectory.path.length).position,chord))<1e-5f);
 VFX_GuidedParticleConfig vertical=c;vertical.target=MotionVec_Add(c.source,(Vector3){0,4,0});
 assert(VC_GuidedBuildField(&vertical,&d));
 assert(d.trajectory.path.length>4.5f);
 assert(VC_GuidedBuildField(&c,&d));
 VFX_GuidedParticleConfig tracking=c;tracking.swirlSpeed=0;tracking.turbulenceSpeed=0;
 FieldDesc routed;assert(VC_GuidedBuildField(&tracking,&routed));
 ParticleDynamicsProfile tracked=VC_GuidedBody(&tracking);
 BodyPhysicalProperties trackedBody=MotionBody_GetPhysicalProperties(&tracked);
 MediumProperties air={.densityKgM3=1.225f,.gravityMps2={0,-9.81f,0}};
 Vector3 position=tracking.source,velocity={0};float dt=1.f/120,maxError=0,maxBow=0;
 float transitSeconds=routed.trajectory.path.length/tracking.speed;
 for(int step=0;step*dt<transitSeconds;step++) {
   float age=step*dt;
   FieldSample force=Field_EvaluateStep(&routed,age,position,velocity,&trackedBody,&air,dt);
   velocity=MotionBody_AdvanceFieldVelocity(velocity,&tracked,(Vector3){0},(Vector3){0},&force,(Vector3){0},dt);
   position=MotionVec_Add(position,MotionVec_Scale(velocity,dt));
   FieldTransform expected=FieldTrajectory_Transform(&routed,age+dt);
   maxError=fmaxf(maxError,MotionVec_Length(MotionVec_Sub(position,expected.position)));
   maxBow=fmaxf(maxBow,fabsf(position.z-tracking.source.z));
 }
 assert(maxError<tracking.guideRadius*.5f && maxBow>MotionVec_Length(chord)*.2f);
 printf("Curved skill tracking: max lag %.4f m, lateral bow %.4f m\n",maxError,maxBow);
 body=VC_GuidedBody(&c);
 assert(body.aerodynamicAreaM2>0 && body.aerodynamicDragCoefficient==.47f && body.windCouplingHz==0);
 assert(VC_GuidedBuildField(&c,&d) && d.preserveSphereOffsets &&
   d.forceLaws[0].type==FORCE_LAW_MOVING_GUIDE);
 GuideTuning compiled=VC_GuidedTuning(&c);
 assert(d.forceLaws[0].magnitudeNewtons==compiled.maxForceNewtons);
 /* A guided field's turbulence is an actuator disturbance in Newtons,
  * independent of medium drag: one cast must receive it even in vacuum. */
 c.source=(Vector3){0};c.target=c.source;c.swirlSpeed=0;c.turbulenceSpeed=0;
 Vector3 probe={.13f,.07f,.19f};
 MediumProperties vacuum={0};
 BodyPhysicalProperties receiver={.massKg=c.massKg};
 assert(VC_GuidedBuildField(&c,&d));
 FieldSample quiet=Field_Evaluate(&d,.37f,probe,(Vector3){0},&receiver,&vacuum);
 c.turbulenceSpeed=8;assert(VC_GuidedBuildField(&c,&d));
 FieldSample noisy=Field_Evaluate(&d,.37f,probe,(Vector3){0},&receiver,&vacuum);
 assert(MotionVec_Length(MotionVec_Sub(noisy.forceNewtons,quiet.forceNewtons))>.01f);
 assert(MotionVec_Length(noisy.mediumVelocityMps)<1e-6f);
 receiver.massKg*=4;
 FieldSample heavy=Field_Evaluate(&d,.37f,probe,(Vector3){0},&receiver,&vacuum);
 assert(MotionVec_Length(MotionVec_Sub(heavy.forceNewtons,noisy.forceNewtons))<1e-6f);
 c=VFX_GuidedParticle_DefaultConfig();
 c.emitDuration=2;assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_PATH_TUBE && d.forceLaws[0].forwardForceNewtons>0);
 /* Automatic settings do not consume legacy force/drag controls. */
 c.maxForceNewtons=NAN;c.forwardForceNewtons=NAN;c.drag=NAN;
 assert(VC_GuidedSettingsValid(&c) && VC_GuidedBuildField(&c,&d));
 c.speed=0;c.emitDuration=0;
 assert(VC_GuidedBuildField(&c,&d) && d.lifetime.durationSec>=c.duration);
 c.speed=3;c.densityKgM3=.1f;
 GuideTuning buoyant=VC_GuidedTuning(&c);
 assert(buoyant.maxForceNewtons>compiled.maxForceNewtons);
 c.guidancePreset=4;assert(!VC_GuidedSettingsValid(&c));
 puts("PASS: production Guided stable inspector, real relative-air drag, static/moving fields, expiry and override precedence");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-guided-config-') as temp:
 p=pathlib.Path(temp);(p/'test.c').write_text(stubs+(ROOT/'core/force_field.c').read_text().split('static inline float ValueHash')[0]+enums+config+''.join(function(name) for name in ['VFX_GuidedParticle_DefaultConfig','VFX_GuidedParticle_GetParams','VC_GuidedEmissionRate','VC_GuidedUsesTimedEmission','VC_GuidedInitialBurstCount','VC_GuidedRecipe','VC_GuidedTuning','VC_GuidedEstimatedTransitTime','VC_GuidedSettingsValid','VC_GuidedBuildField','VC_GuidedBody'])+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I'+str(ROOT),'-I'+str(ROOT/'core/tests/stubs'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

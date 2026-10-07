#!/usr/bin/env python3
"""Run production Guided settings/inspector with data-only dependency stubs."""
import pathlib,re,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
s=(ROOT/'core/composition/common/vc_guided_particle.inl').read_text()
h=(ROOT/'core/composition/visual_composer.h').read_text()
def function(name):
 m=re.search(r'^(?:static bool|static ParticleDynamicsProfile|VFX_GuidedParticleConfig|int) '+name+r'\(',s,re.M); start=s.index('{',m.start()); depth=1; end=start+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
config=re.search(r'typedef struct VFX_GuidedParticleConfig \{.*?\} VFX_GuidedParticleConfig;',h,re.S).group()
stubs=r'''
#include "core/motion/motion_fields.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int VC_MaterialId;
typedef enum {PARTICLE_RENDER_BILLBOARD=0,PARTICLE_RENDER_SURFACE_INPUT=3} ParticleRenderMode;
typedef struct {int placeholder;} ParticleRenderStream;
typedef struct {struct {ParticleDynamicsProfile *dynamics;} physics;} ParticleConfig;
typedef struct {int placeholder;} ParticleEmissionSource;
#include "core/composition/common/vc_params.h"
#include "core/motion/motion_body.h"
enum {VC_MAT_LIGHTNING};
static FieldDesc MotionField_TestDefault(void) {
 return (FieldDesc){.transform=FieldTransform_Identity(),.receiverMask=MOTION_RECEIVER_ALL};
}
#define MotionField_Default MotionField_TestDefault
static Vector3 GetBezierPoint(Vector3 p,Vector3 a,Vector3 b,Vector3 q,float t) {
 float u=1-t;
 return MotionVec_Add(MotionVec_Add(MotionVec_Scale(p,u*u*u),MotionVec_Scale(a,3*u*u*t)),
   MotionVec_Add(MotionVec_Scale(b,3*u*t*t),MotionVec_Scale(q,t*t*t)));
}
'''
main=r'''
int main(void) {
 VFX_GuidedParticleConfig c=VFX_GuidedParticle_DefaultConfig();
 assert(VC_GuidedSettingsValid(&c));
 VFX_ParamDef a[32],b[32];int n=VFX_GuidedParticle_GetParams(&c,a,32);assert(n==15);
 c.count=0;c.emitDuration=1;c.emissionRate=0;
 assert(VFX_GuidedParticle_GetParams(&c,b,32)==n);
 for(int i=0;i<n;i++)assert(a[i].valPtr==b[i].valPtr && !strcmp(a[i].name,b[i].name));
 assert(VFX_GuidedParticle_GetParams(&c,b,3)==3);
 assert(VFX_GuidedParticle_GetParams(NULL,b,32)==0);
 c.count=1;c.drag=-1;assert(!VC_GuidedSettingsValid(&c));
 c.drag=NAN;assert(!VC_GuidedSettingsValid(&c));
 c.drag=0;assert(VC_GuidedSettingsValid(&c));
 c.forwardForceNewtons=-1;assert(!VC_GuidedSettingsValid(&c));
 c.forwardForceNewtons=.08f;
 ParticleDynamicsProfile body=VC_GuidedBody(&c);
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
 assert(sample.forceNewtons.z<0 && sample.mediumVelocityMps.x>c.speed*.9f &&
   fabsf(sample.mediumVelocityMps.z-2)<1e-5f);
 sample=Field_Evaluate(&d,.2f,moving.position,(Vector3){0},&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)<1e-3f &&
   sample.mediumVelocityMps.x>0);
 sample=Field_Evaluate(&d,.2f,moving.position,moving.frameVelocityMps,&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)<1e-5f);
 sample=Field_Evaluate(&d,c.duration,offCenter,(Vector3){0},&physical,&medium);
 assert(MotionVec_Length(sample.forceNewtons)==0 && sample.mediumWeight==0);
 c.count=0;c.duration=.5f;c.emitDuration=2;c.emissionRate=100;
 assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_PATH_TUBE && d.trajectory.mode==FIELD_TRAJECTORY_STATIC &&
   d.flow.followSpeedMps==c.speed &&
   d.forceLaws[0].forwardForceNewtons==c.forwardForceNewtons);
 assert(fabsf(d.lifetime.durationSec-2.05f)<1e-6f &&
   fabsf(FieldLifetime_Weight(&d.lifetime,2)-1)<1e-5f);
 MotionPathSample pathPoint=MotionPath_Sample(&d.volume.path,d.volume.path.length*.5f);
 assert(pathPoint.position.x>0 && pathPoint.tangent.x>.9f);
 Vector3 offPath=MotionVec_Add(MotionVec_Add(c.source,pathPoint.position),(Vector3){0,0,.2f});
 sample=Field_Evaluate(&d,.2f,offPath,(Vector3){0},&physical,&medium);
 assert(sample.forceNewtons.z<0 && sample.mediumVelocityMps.x>c.speed*.9f &&
   fabsf(sample.mediumVelocityMps.z-2)<1e-5f);
 sample=Field_Evaluate(&d,.2f,MotionVec_Add(c.source,pathPoint.position),(Vector3){0},&physical,&medium);
 assert(MotionVec_Dot(sample.forceNewtons,pathPoint.tangent)>0 &&
   sample.mediumVelocityMps.x>c.speed*.9f);
 c.duration=4;c.emitDuration=0;c.emissionRate=0;
 c.target=c.source;assert(VC_GuidedBuildField(&c,&d));
 assert(d.volume.shape==FIELD_SPHERE && d.trajectory.mode==FIELD_TRAJECTORY_STATIC);
 c.count=1;
 FieldDesc replacement=d;c.fieldOverride=&replacement;c.speed=NAN;c.duration=NAN;
 assert(VC_GuidedSettingsValid(&c) && VC_GuidedBuildField(&c,&d));
 assert(VFX_GuidedParticle_GetParams(&c,b,32)==8);
 ParticleConfig particle={0};c.particleTemplate=&particle;c.massKg=NAN;c.densityKgM3=NAN;c.particleRadius=NAN;c.drag=NAN;
 assert(VC_GuidedSettingsValid(&c));
 c.particleTemplate=NULL;assert(!VC_GuidedSettingsValid(&c));
 puts("PASS: production Guided stable inspector, real relative-air drag, static/moving fields, expiry and override precedence");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-guided-config-') as temp:
 p=pathlib.Path(temp);(p/'test.c').write_text(stubs+(ROOT/'core/force_field.c').read_text().split('static inline float ValueHash')[0]+config+''.join(function(name) for name in ['VFX_GuidedParticle_DefaultConfig','VFX_GuidedParticle_GetParams','VC_GuidedSettingsValid','VC_GuidedBuildField','VC_GuidedBody'])+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I'+str(ROOT),'-I'+str(ROOT/'core/tests/stubs'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

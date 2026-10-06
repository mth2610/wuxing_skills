#!/usr/bin/env python3
"""Run production Guided settings/inspector with data-only dependency stubs."""
import pathlib,re,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
s=(ROOT/'core/composition/common/vc_guided_particle.inl').read_text()
h=(ROOT/'core/composition/visual_composer.h').read_text()
def function(name):
 m=re.search(r'^(?:static bool|int) '+name+r'\(',s,re.M); start=s.index('{',m.start()); depth=1; end=start+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
config=re.search(r'typedef struct VFX_GuidedParticleConfig \{.*?\} VFX_GuidedParticleConfig;',h,re.S).group()
enum=re.search(r'typedef enum \{\s*VFX_GUIDED_TARGET_NONE.*?\} VFX_GuidedTargetPreset;',h,re.S).group()
names=s[s.index('static const char *s_guidedFormationNames'):s.index('int VFX_GuidedParticle_GetParams')]
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
enum {VFX_PARAM_ENUM,VFX_PARAM_FLOAT,VFX_PARAM_INT,VFX_PARAM_BOOL};
typedef struct {const char *name,*group;int type;void *valPtr;int minInt,maxInt;const char **enumNames;int enumCount;float minFloat,maxFloat,stepFloat;} VFX_ParamDef;
'''
main=r'''
static bool HasParam(VFX_GuidedParticleConfig *c,const char *name) {
 VFX_ParamDef params[64];int n=VFX_GuidedParticle_GetParams(c,params,64);
 for(int i=0;i<n;i++)if(!strcmp(params[i].name,name))return true;
 return false;
}
int main(void) {
 VFX_GuidedParticleConfig c={.count=0,.massKg=NAN,.densityKgM3=NAN,.particleRadius=NAN,.formationRadius=NAN,.renderMode=999};
 assert(VC_GuidedSettingsValid(&c));
 assert(!HasParam(&c,"Particle radius m")&&!HasParam(&c,"Mass kg")&&!HasParam(&c,"Source radius m"));
 c.count=1;assert(!VC_GuidedSettingsValid(&c));
 c=(VFX_GuidedParticleConfig){.count=1,.massKg=.004f,.densityKgM3=600,.particleRadius=.03f,.formationRadius=.1f,.emitDuration=1,.emissionRate=160};
 assert(VC_GuidedSettingsValid(&c));
 assert(HasParam(&c,"Burst count")&&HasParam(&c,"Emission rate /s"));
 assert(!HasParam(&c,"Mass kg")&&!HasParam(&c,"Density kg/m3"));
 c.showCustomBodyProperties=true;assert(HasParam(&c,"Mass kg")&&HasParam(&c,"Density kg/m3"));
 ParticleDynamicsProfile profile={0};ParticleConfig particle={.physics={.dynamics=&profile}};ParticleEmissionSource source={0};
 c.particleTemplate=&particle;c.emissionSource=&source;c.particleRadius=NAN;c.massKg=NAN;c.densityKgM3=NAN;c.formationRadius=NAN;
 BodyPhysicalProperties invalid={.massKg=NAN};c.bodyOverride=&invalid;
 assert(VC_GuidedSettingsValid(&c));assert(!HasParam(&c,"Mass kg")&&!HasParam(&c,"Particle radius m")&&!HasParam(&c,"Source radius m"));
 MotionGuideDesc guide={0};c.guideOverride=&guide;c.targetPreset=999;c.motionFlow.swirlSpeedMps=NAN;c.arrivalFlow.swirlSpeedMps=NAN;
 assert(VC_GuidedSettingsValid(&c));assert(!HasParam(&c,"Guide lifetime s")&&!HasParam(&c,"Target field"));
 c.particleTemplate=NULL;c.bodyOverride=&invalid;c.particleRadius=.03f;assert(!VC_GuidedSettingsValid(&c));
 BodyPhysicalProperties valid=BodyPhysicalProperties_Sphere(.004f,600,.47f);c.bodyOverride=&valid;assert(VC_GuidedSettingsValid(&c));
 valid.immersionFraction=.5f;assert(!VC_GuidedSettingsValid(&c));
 valid.immersionFraction=1;valid.volumeM3*=2;assert(!VC_GuidedSettingsValid(&c));
 c.bodyOverride=NULL;c.count=0;c.emitDuration=0;c.emissionRate=NAN;assert(VC_GuidedSettingsValid(&c));
 puts("PASS: production Guided consumed-branch validation, field-only bodies, override precedence, reduced inspector, simultaneous burst/stream and explicit unsupported-body rejection");
}
'''
with tempfile.TemporaryDirectory(prefix='wuxing-guided-config-') as temp:
 p=pathlib.Path(temp);(p/'test.c').write_text(stubs+enum+config+names+function('VFX_GuidedParticle_GetParams')+function('VC_GuidedSettingsValid')+main)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-I'+str(ROOT),'-I'+str(ROOT/'core/tests/stubs'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

#!/usr/bin/env python3
"""Exercise production Guided Motion and Emitter with component allocator stubs.

This verifies composition ownership and birth scheduling, not GPU simulation,
rendering or visual acceptance. Component resources remain deliberately opaque.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = (ROOT / 'core/composition/visual_composer.h').read_text()
ENUMS = '\n'.join(re.search(r'typedef enum \{[^}]*\} '+name+r';', HEADER, re.S).group() for name in ('VFX_GuidedOutput','VFX_GuidedPattern','VFX_GuidedTrailStyle','VFX_GuidedTrailMotion'))
CONFIG = ENUMS + re.search(r'typedef struct VFX_GuidedParticleConfig \{.*?\} VFX_GuidedParticleConfig;', HEADER, re.S).group() + '\ntypedef VFX_GuidedParticleConfig VFX_GuidedMotionConfig;\n'
STUBS = r'''
#include "raylib.h"
typedef struct {int placeholder;} Material;
typedef int BlendMode;
enum {BLEND_ALPHA,BLEND_ADDITIVE,BLEND_ALPHA_PREMULTIPLY};
typedef struct {int placeholder;} Mesh;
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#define WHITE ((Color){255,255,255,255})
#include "core/motion/motion_fields.h"
#include "core/particles/particle_manager.h"
#include "core/trails/trail_ribbon.h"
#include "core/trails/trail_recipe.h"
#include "core/presets/vc_material.h"
#include "core/composition/common/vc_params.h"
#include "core/emitter/emitter.h"
#include "core/mesh_adjacency.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ParticleEmitterDesc descriptors[128];
static bool emitterActive[128], failEmitter, failField;
static ParticleConfig particles[4096];
static TrailRibbonConfig ribbons[4096];
static int particleCount,ribbonCount,fieldCount,fieldStopped,styleCalls;
static TrailPresetId lastStyle;
bool VFX_TrailRibbonApplyPreset(TrailRibbonConfig *r,TrailPresetId preset,VC_MaterialId mat){
 (void)mat;styleCalls++;lastStyle=preset;r->appearance.enabled=true;r->appearance.layerCount=3;return true;
}
static FieldDesc capturedField;
static MotionFrameRegistry frames;
static MotionFrameHandle boundFrame;
static FieldTransform boundLocal;
static int frameBinds;
MotionFrameHandle MotionFrame_Create(Matrix m){return MotionFrameRegistry_Create(&frames,m);}
bool MotionFrame_Update(MotionFrameHandle h,Matrix m,float dt,bool cut){return MotionFrameRegistry_Update(&frames,h,m,dt,cut);}
void MotionFrame_Destroy(MotionFrameHandle h){MotionFrameRegistry_Destroy(&frames,h);}
void MotionFrame_Reset(void){MotionFrameRegistry_Reset(&frames);}
bool MotionFrame_Snapshot(MotionFrameHandle h,Vector3 offset,MotionFrameSnapshot *s){return MotionFrameRegistry_Snapshot(&frames,h,offset,s);}
bool MotionFields_BindFrame(MotionFieldHandle h,MotionFrameHandle f,const FieldTransform *local){
 (void)h;MotionFrameSnapshot s;
 if(!MotionFrame_Snapshot(f,(Vector3){0},&s) || !MotionFrame_IsRigidTransform(&s.transform))return false;
 boundFrame=f;boundLocal=*local;frameBinds++;return true;
}
ParticleEmitterHandle ParticleManager_CreateEmitter(const ParticleEmitterDesc *d) {
 if(failEmitter)return PARTICLE_EMITTER_INVALID;
 for(int i=0;i<128;i++)if(!emitterActive[i]){descriptors[i]=*d;emitterActive[i]=true;return i;}
 return PARTICLE_EMITTER_INVALID;
}
void ParticleManager_DestroyEmitter(ParticleEmitterHandle h){if(h==PARTICLE_EMITTER_INVALID)return;assert(h>=0&&h<128&&emitterActive[h]);emitterActive[h]=false;}
ParticleEmitterStatus ParticleManager_GetEmitterStatus(ParticleEmitterHandle h){return h>=0&&h<128&&emitterActive[h]?PARTICLE_EMITTER_OK:PARTICLE_EMITTER_INVALID_HANDLE;}
void ParticleManager_EmitBatch(ParticleEmitterHandle h,const ParticleConfig *p,int n){assert(emitterActive[h]);for(int i=0;i<n;i++){assert(particleCount<4096);particles[particleCount++]=p[i];}}
bool ParticleManager_GetSurfaceStream(ParticleEmitterHandle h,ParticleRenderStream *out){*out=(ParticleRenderStream){.emitter=h};return true;}
MotionFieldHandle MotionFields_CreateField(const FieldDesc *d){if(failField)return 0;capturedField=*d;return ++fieldCount;}
void MotionFields_Stop(MotionFieldHandle h){assert(h);fieldStopped++;}
int TrailRibbon_Spawn(const TrailRibbonConfig *r){assert(ribbonCount<4096);ribbons[ribbonCount]=*r;return ribbonCount++;}
TrailRibbonConfig TrailRibbon_Default(void){return (TrailRibbonConfig){.nodeCount=16,.tailDirection={-1,0,0},.lengthM=.5f,.widthM=.05f,.lifetimeSec=1,.color=WHITE,.material=TrailRibbonMaterial_Default()};}
const VFX_ElementMaterial *VFX_Material(VC_MaterialId id){(void)id;static VFX_ElementMaterial m={.body={180,200,240,255}};return &m;}
Vector3 GetBezierPoint(Vector3 p,Vector3 a,Vector3 b,Vector3 q,float t){float u=1-t;return MotionVec_Add(MotionVec_Add(MotionVec_Scale(p,u*u*u),MotionVec_Scale(a,3*u*u*t)),MotionVec_Add(MotionVec_Scale(b,3*u*t*t),MotionVec_Scale(q,t*t*t)));}
FieldDesc MotionField_Default(void){return (FieldDesc){.transform=FieldTransform_Identity(),.receiverMask=MOTION_RECEIVER_ALL,.volume={.shape=FIELD_SPHERE}};}
static Matrix MatrixScale(float x,float y,float z){return (Matrix){.m0=x,.m5=y,.m10=z,.m15=1};}
static Matrix MatrixTranslate(float x,float y,float z){return (Matrix){.m0=1,.m5=1,.m10=1,.m15=1,.m12=x,.m13=y,.m14=z};}
/* Affine equivalent of raymath MatrixMultiply: local a followed by parent b. */
static Matrix MatrixMultiply(Matrix a,Matrix b){
 Vector3 x=MotionVec_Sub(MotionFrame_TransformPoint(b,(Vector3){a.m0,a.m1,a.m2}),(Vector3){b.m12,b.m13,b.m14});
 Vector3 y=MotionVec_Sub(MotionFrame_TransformPoint(b,(Vector3){a.m4,a.m5,a.m6}),(Vector3){b.m12,b.m13,b.m14});
 Vector3 z=MotionVec_Sub(MotionFrame_TransformPoint(b,(Vector3){a.m8,a.m9,a.m10}),(Vector3){b.m12,b.m13,b.m14});
 Vector3 p=MotionFrame_TransformPoint(b,(Vector3){a.m12,a.m13,a.m14});
 return (Matrix){.m0=x.x,.m1=x.y,.m2=x.z,.m4=y.x,.m5=y.y,.m6=y.z,.m8=z.x,.m9=z.y,.m10=z.z,.m12=p.x,.m13=p.y,.m14=p.z,.m15=1};
}
static Vector3 Vector3Transform(Vector3 p,Matrix m){return MotionFrame_TransformPoint(m,p);}
static Mesh GenMeshSphere(float r,int rings,int slices){(void)r;(void)rings;(void)slices;return (Mesh){0};}
static void UnloadMesh(Mesh m){(void)m;}
void MeshAdjacency_Build(MeshAdjacency *a,Mesh m){(void)m;*a=(MeshAdjacency){.count=2,.vertices={{1,0,0},{-1,0,0}},.neighborCount={1,1},.neighbors={{1},{0}}};}
Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *a){(void)a;assert(0);return (Vector3){0};}
Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *a){(void)a;assert(0);return (Vector3){0};}
static void Near(float a,float b){assert(fabsf(a-b)<.0001f);}
'''
# Exercise the production compatibility ownership wrappers without a renderer.
TRAIL_SOURCE = (ROOT / 'core/trails/trail_ribbon.c').read_text()
def trail_function(name):
    match = re.search(r'^(?:TrailAttachmentHandle|void) '+name+r'\(', TRAIL_SOURCE, re.M)
    start = TRAIL_SOURCE.index('{', match.start()); end = start+1; depth = 1
    while depth:
        depth += (TRAIL_SOURCE[end]=='{')-(TRAIL_SOURCE[end]=='}'); end += 1
    return TRAIL_SOURCE[match.start():end]
OWNERSHIP = r'''
static TrailAttachmentHandle s_ownedAttachments[TRAIL_ATTACHMENT_CAPACITY];
#include "core/trails/trail_ribbon_gpu.h"
static struct {bool active;int gpuSlot;} s_ribbons[TRAIL_RIBBON_GPU_CAPACITY];
void TrailRibbonGpu_Kill(int slot){(void)slot;assert(0);}
''' + ''.join(trail_function(name) for name in ('TrailAttachment_Create','TrailAttachment_Destroy','TrailRibbonSystem_Reset'))
MAIN = r'''
static int ActiveEmitters(void){int n=0;for(int i=0;i<128;i++)n+=emitterActive[i];return n;}
static int ActiveCasts(void){int n=0;for(int i=0;i<VC_GUIDED_MAX_STREAMS;i++)n+=s_guidedStreams[i].active;return n;}
static void Reset(void){assert(!ActiveCasts()&&!ActiveEmitters());EmissionSystem_Init();particleCount=ribbonCount=fieldCount=fieldStopped=0;failEmitter=failField=false;styleCalls=0;MotionFrame_Reset();boundFrame=0;frameBinds=0;}
static bool Ignore(void *u,const EmissionSpawn *s){(void)u;(void)s;return true;}
int main(void) {
 Reset();
 VFX_GuidedMotionConfig c=VFX_GuidedMotion_DefaultConfig();
 assert(c.output==VFX_GUIDED_BOTH&&c.trailMotion==VFX_GUIDED_TRAIL_MOTION_GUIDED);
 assert(!VC_GuidedUsesTimedEmission(&c)&&VC_GuidedInitialBurstCount(&c)==c.count);
 c.count=10;c.trailCount=4;c.emitDuration=1;c.formationRadius=.2f;
 MotionFieldHandle h=VFX_ComposeGuidedMotionEx(&c);
 assert(h&&particleCount==0&&ribbonCount==0&&ActiveCasts()==1);
 assert(capturedField.receiverMask&MOTION_RECEIVER_TRAIL);
 VC_GuidedMotion_Update(0);VC_GuidedMotion_Update(NAN);assert(particleCount==0&&ribbonCount==0);
 VC_GuidedMotion_Update(.25f);assert(particleCount==2&&ribbonCount==1);
 MotionFields_Stop(h);c.count=2048;c.trailCount=64;c.source=(Vector3){999,999,999};
 for(int i=0;i<3;i++)VC_GuidedMotion_Update(.25f);
 assert(particleCount==10&&ribbonCount==4&&!ActiveCasts()&&!ActiveEmitters());
 for(int i=0;i<10;i++){
   assert(MotionVec_Length(MotionVec_Sub(particles[i].position,(Vector3){-2,1,0}))<=.2001f);
   assert(particles[i].physics.receiveMotionFields&&particles[i].physics.spatialMotionOnly&&!particles[i].physics.initialGuide);
   Near(particles[i].position.x,particles[i].physics.position.x);
 }
 for(int i=0;i<4;i++){
   assert(MotionVec_Length(MotionVec_Sub(ribbons[i].headPosition,(Vector3){-2,1,0}))<=.2001f);
   Near(ribbons[i].headPosition.x,particles[i].position.x);
   assert(ribbons[i].mode==TRAIL_RIBBON_FREE);
 }
 assert(fieldStopped==1); /* End of births does not stop the independent field. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=2048;c.trailCount=4;c.emitDuration=0;
 assert(VFX_ComposeGuidedMotionEx(&c));assert(particleCount==2048&&ribbonCount==4&&!ActiveCasts()&&!ActiveEmitters());
 Reset();c.emitDuration=1;assert(VFX_ComposeGuidedMotionEx(&c));assert(particleCount==0&&ribbonCount==0);
 VC_GuidedMotion_Update(20);assert(particleCount==2048&&ribbonCount==4&&!ActiveCasts());
 /* Burst trails must be transported along the whole spline, not a moving sphere. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;
 c.trailCount=1;c.count=0;c.emitDuration=0;c.swirlSpeed=0;c.turbulenceSpeed=0;c.formationRadius=0;
 assert(VFX_ComposeGuidedMotionEx(&c));
 assert(capturedField.volume.shape==FIELD_PATH_TUBE&&capturedField.trajectory.mode==FIELD_TRAJECTORY_STATIC);
 assert(!capturedField.preserveSphereOffsets&&capturedField.forceLaws[0].type==FORCE_LAW_PATH_GUIDE);
 Vector3 expectedTail=MotionVec_Scale(capturedField.volume.path.tangents[0],-1);
 assert(MotionVec_Length(MotionVec_Sub(ribbons[0].tailDirection,expectedTail))<1e-5f);
 assert(ribbonCount==1&&ribbons[0].mode==TRAIL_RIBBON_FREE);
 assert(ribbons[0].pathTransport.field&&ribbons[0].pathTransport.speedMps==c.speed&&ribbons[0].pathTransport.captureBirthLane);
 assert(ribbons[0].pathTransport.respondToField);
 Near(ribbons[0].headPosition.x,c.source.x);
 float expectedLife=VC_GuidedEstimatedTransitTime(&c,capturedField.volume.path.length,true)+
   c.trailLength/c.speed+fminf(.3f,c.duration*.1f);
 Near(capturedField.lifetime.durationSec,expectedLife);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;
 c.trailMotion=VFX_GUIDED_TRAIL_MOTION_SPLINE;
 assert(VFX_ComposeGuidedMotionEx(&c));assert(ribbons[0].pathTransport.field&&!ribbons[0].pathTransport.respondToField);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;
 c.trailMotion=VFX_GUIDED_TRAIL_MOTION_FIELD;
 assert(VFX_ComposeGuidedMotionEx(&c));assert(ribbonCount&&ribbons[0].pathTransport.field==0);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;c.trailMotion=3;
 assert(!VFX_ComposeGuidedMotionEx(&c));
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_PARTICLES;
 assert(VFX_ComposeGuidedMotionEx(&c));assert(capturedField.volume.shape==FIELD_SPHERE);
 /* Shared appearance selection is independent of physics and copied into timed births. */
 const TrailPresetId expectedStyles[]={TRAIL_PRESET_ENERGY,TRAIL_PRESET_SMOKE,TRAIL_PRESET_BLADE,TRAIL_PRESET_WATER};
 for(int style=1;style<=4;style++) {
   Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;
   c.trailCount=2;c.emitDuration=1;c.trailStyle=style;c.trailLength=1.7f;
   assert(VFX_ComposeGuidedMotionEx(&c));assert(styleCalls==1&&lastStyle==expectedStyles[style-1]);
   c.trailStyle=0;VC_GuidedMotion_Update(1);
   assert(ribbonCount==2&&ribbons[0].appearance.enabled&&ribbons[1].appearance.layerCount==3);
   Near(ribbons[0].lengthM,1.7f);assert(ribbons[0].mode==TRAIL_RIBBON_FREE);
 }
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;c.trailStyle=5;
 assert(!VFX_ComposeGuidedMotionEx(&c)&&fieldCount==0&&styleCalls==0);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_PARTICLES;c.trailStyle=5;
 assert(VFX_ComposeGuidedMotionEx(&c)&&styleCalls==0&&ribbonCount==0);
 /* Explicit source, body and templates are copied before the caller mutates them. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=2;c.trailCount=2;c.emitDuration=1;
 ParticleDynamicsProfile body={.inverseMassKg=23,.gravityScale=-.2f};
 ParticleConfig particle={.position={8,9,10},.lifetime=.7f,.radius=.037f,.physics={.dynamics=&body}};
 TrailRibbonConfig ribbon=TrailRibbon_Default();ribbon.mode=TRAIL_RIBBON_HEAD_ANCHORED;
 ribbon.attachment=1234;ribbon.lifetimeSec=9;ribbon.widthM=.019f;ribbon.backend=TRAIL_RIBBON_GPU_ONLY;
 ParticleEmissionSource source={.type=PARTICLE_SOURCE_POINT,.point={2,3,4}};
 c.particleTemplate=&particle;c.trailTemplate=&ribbon;c.emissionSource=&source;c.trailStyle=999;
 assert(VFX_ComposeGuidedMotionEx(&c));
 particle.radius=99;particle.lifetime=99;body.inverseMassKg=99;ribbon.attachment=99;ribbon.widthM=99;source.point.x=99;
 VC_GuidedMotion_Update(.5f);
 assert(particleCount==1&&ribbonCount==1);Near(particles[0].radius,.037f);Near(particles[0].lifetime,.7f);
 Near(particles[0].physics.dynamics->inverseMassKg,23);Near(particles[0].position.x,2);
 assert(ribbons[0].attachment==1234&&ribbons[0].mode==TRAIL_RIBBON_HEAD_ANCHORED&&ribbons[0].backend==TRAIL_RIBBON_GPU_ONLY);
 assert(styleCalls==0&&!ribbons[0].appearance.enabled);Near(ribbons[0].widthM,.019f);Near(ribbons[0].lifetimeSec,9);Near(ribbons[0].headPosition.x,2);
 VC_GuidedMotion_Update(1);assert(particleCount==2&&ribbonCount==2&&!ActiveCasts());
 /* Default attached ribbons retain external anchor ownership; trails-only needs no particle allocator. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.output=VFX_GUIDED_TRAILS;c.trailCount=3;c.trailAttachment=4321;
 failEmitter=true;assert(VFX_ComposeGuidedMotionEx(&c));assert(!ActiveEmitters()&&particleCount==0&&ribbonCount==3);
 assert(ribbons[0].mode==TRAIL_RIBBON_HEAD_ANCHORED&&ribbons[0].attachment==4321);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=5;c.trailCount=3;
 assert(VFX_ComposeGuidedParticleEx(&c));assert(particleCount==5&&ribbonCount==0);
 assert(VFX_GuidedParticle_DefaultConfig().output==VFX_GUIDED_PARTICLES);
 /* Failure must release every cast/scheduler/particle handle acquired so far. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=1;c.trailCount=1;c.emitDuration=1;
 failField=true;assert(!VFX_ComposeGuidedMotionEx(&c)&&!ActiveCasts()&&!ActiveEmitters()&&fieldStopped==0);
 failField=false;failEmitter=true;assert(!VFX_ComposeGuidedMotionEx(&c)&&!ActiveCasts()&&!ActiveEmitters()&&fieldStopped==1);
 failEmitter=false;
 EmissionHandle held[EMITTER_SCHEDULER_CAPACITY];
 EmissionConfig cfg={.schedule=EMISSION_BURST,.count=1,.sink=Ignore};
 for(int i=0;i<EMITTER_SCHEDULER_CAPACITY-1;i++){held[i]=Emission_Create(&cfg,(Vector3){0});assert(held[i]);}
 assert(!VFX_ComposeGuidedMotionEx(&c)&&!ActiveCasts()&&!ActiveEmitters()&&fieldStopped==2);
 held[EMITTER_SCHEDULER_CAPACITY-1]=Emission_Create(&cfg,(Vector3){0});assert(held[EMITTER_SCHEDULER_CAPACITY-1]);
 assert(!Emission_Create(&cfg,(Vector3){0}));
 for(int i=0;i<EMITTER_SCHEDULER_CAPACITY;i++)assert(Emission_Destroy(held[i]));
 for(int i=0;i<VC_GUIDED_MAX_STREAMS;i++)assert(VFX_ComposeGuidedMotionEx(&c));
 int before=fieldCount;assert(!VFX_ComposeGuidedMotionEx(&c)&&fieldCount==before);
 VC_GuidedMotion_Update(1);assert(!ActiveCasts()&&!ActiveEmitters());
 assert(VFX_ComposeGuidedMotionEx(&c));VC_GuidedMotion_Update(1);
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=c.trailCount=0;
 assert(VFX_ComposeGuidedMotionEx(&c)&&!ActiveCasts()&&!ActiveEmitters());
 c.output=99;assert(!VFX_ComposeGuidedMotionEx(&c));
 /* Resets respect caller-owned generic frames and legacy trail-owned handles. */
 Reset();MotionFrameHandle shared=MotionFrame_Create(MatrixTranslate(0,0,0));
 TrailAttachmentHandle owned=TrailAttachment_Create(MatrixTranslate(0,0,0));
 TrailRibbonSystem_Reset();MotionFrameSnapshot ownershipSnapshot;
 assert(MotionFrame_Snapshot(shared,(Vector3){0},&ownershipSnapshot));
 assert(!MotionFrame_Snapshot(owned,(Vector3){0},&ownershipSnapshot));
 MotionFrame_Destroy(shared);
 /* Orbit uses a single sphere; airflow supplies velocity without an actuator. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=c.trailCount=0;c.emitDuration=1;
 c.motionPattern=VFX_GUIDED_ORBIT;assert(VFX_ComposeGuidedMotionEx(&c));
 assert(capturedField.volume.shape==FIELD_SPHERE && capturedField.volume.path.count==0 &&
   capturedField.trajectory.mode==FIELD_TRAJECTORY_STATIC && capturedField.preserveSphereOffsets);
 assert(capturedField.forceLaws[0].type==FORCE_LAW_MOVING_GUIDE);
 Reset();c.motionPattern=VFX_GUIDED_AIRFLOW;c.speed=3;c.source=(Vector3){0};c.target=(Vector3){0,0,2};
 assert(VFX_ComposeGuidedMotionEx(&c));assert(capturedField.forceLawCount==0 && !capturedField.preserveSphereOffsets);
 Near(capturedField.flow.velocityMps.z,3);Near(capturedField.flow.velocityMps.x,0);
 assert(capturedField.volume.shape==FIELD_SPHERE && capturedField.volume.path.count==0);
 /* Timed births follow rigid translation/rotation and inherit point velocity. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=c.trailCount=4;c.emitDuration=1;
 c.source=(Vector3){1,0,0};c.target=(Vector3){2,0,0};
 particle=(ParticleConfig){.position={1,0,0},.velocity={1,0,0},.lifetime=2,.radius=.02f};
 ribbon=TrailRibbon_Default();ribbon.initialVelocity=(Vector3){1,0,0};ribbon.tailDirection=(Vector3){1,0,0};
 source=(ParticleEmissionSource){.type=PARTICLE_SOURCE_CONFIG_POSITION};
 c.particleTemplate=&particle;c.trailTemplate=&ribbon;c.emissionSource=&source;
 Matrix frameMatrix=MatrixTranslate(0,0,0);c.frame=MotionFrame_Create(frameMatrix);
 ribbon.mode=TRAIL_RIBBON_HEAD_ANCHORED;ribbon.attachment=c.frame;
 assert(VFX_ComposeGuidedMotionEx(&c));assert(frameBinds==1 && boundFrame==c.frame);Near(boundLocal.position.x,1);
 frameMatrix.m0=0;frameMatrix.m1=1;frameMatrix.m4=-1;frameMatrix.m5=0;frameMatrix.m12=2;
 assert(MotionFrame_Update(c.frame,frameMatrix,.25f,false));VC_GuidedMotion_Update(.25f);
 assert(particleCount==1 && ribbonCount==1);Near(particles[0].position.x,2);Near(particles[0].position.y,1);
 Near(particles[0].velocity.x,4);Near(particles[0].velocity.y,5);
 Near(particles[0].physics.velocity.x,4);Near(ribbons[0].initialVelocity.y,5);Near(ribbons[0].tailDirection.y,1);
 frameMatrix.m12=3;assert(MotionFrame_Update(c.frame,frameMatrix,.25f,false));VC_GuidedMotion_Update(.25f);
 Near(particles[1].position.x,3);Near(particles[1].velocity.x,4);Near(particles[1].velocity.y,1);
 MotionFrame_Destroy(c.frame);VC_GuidedMotion_Update(.25f);VC_GuidedMotion_Update(.25f);
 assert(particleCount==4 && ribbonCount==4 && !ActiveCasts());
 assert(ribbons[0].mode==TRAIL_RIBBON_HEAD_ANCHORED && ribbons[1].mode==TRAIL_RIBBON_HEAD_ANCHORED);
 assert(ribbons[2].mode==TRAIL_RIBBON_FREE && ribbons[3].mode==TRAIL_RIBBON_FREE);
 assert(!ribbons[2].attachment && !ribbons[3].attachment);
 for(int i=2;i<4;i++){Near(particles[i].position.x,3);Near(particles[i].position.y,1);Near(particles[i].velocity.x,0);Near(particles[i].velocity.y,1);}
 /* Stale initial frames cannot leak fields, cast slots or allocators. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.frame=MotionFrame_Create(MatrixTranslate(0,0,0));MotionFrame_Destroy(c.frame);
 assert(!VFX_ComposeGuidedMotionEx(&c));assert(!ActiveCasts() && !ActiveEmitters() && fieldStopped==1);
 /* Zero mesh transform means identity before composing the shared frame. */
 Reset();c=VFX_GuidedMotion_DefaultConfig();c.count=c.trailCount=1;
 MeshAdjacency mesh={.count=1,.vertices={{1,0,0}}};
 source=(ParticleEmissionSource){.type=PARTICLE_SOURCE_MESH_VERTEX,.mesh=&mesh};c.emissionSource=&source;
 c.frame=MotionFrame_Create(frameMatrix);assert(VFX_ComposeGuidedMotionEx(&c));
 Near(particles[0].position.x,3);Near(particles[0].position.y,1);Near(ribbons[0].headPosition.x,3);Near(ribbons[0].headPosition.y,1);
 puts("Guided Motion production composition: independent totals, copied ownership, shared sources, compatibility and rollback PASS");
}
'''

with tempfile.TemporaryDirectory(prefix='wuxing-guided-motion-') as temp:
    p = pathlib.Path(temp)
    code = STUBS + CONFIG + OWNERSHIP
    code += '\n#include "core/emitter/emitter_sources.c"\n#include "core/emitter/particle_source.c"\n#include "core/emitter/emitter.c"\n#include "core/emitter/emitter_sinks.c"\n'
    code += '\n#include "core/composition/common/vc_guided_motion.inl"\n' + MAIN
    (p / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-I'+str(ROOT),
                    '-I'+str(ROOT/'core/tests/stubs'), str(p/'test.c'), '-lm',
                    '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)

#include "core/trails/trail_ribbon.h"
#include "core/trails/trail_ribbon_gpu.h"
#include "core/trails/trail_system.h"
#include "core/motion/motion_fields.h"
#include "core/wind/wind_system.h"
#include "core/resource_manager.h"
#include "core/vfx_render.h"
#include "rlgl.h"
#include <string.h>

typedef struct {
    bool active;
    int trailId,gpuSlot;
    TrailRibbonConfig config;
    TrailRibbonState state;
    MotionReceiver receivers[TRAIL_RIBBON_MAX_NODES];
} ModernRibbon;
static ModernRibbon s_ribbons[TRAIL_RIBBON_GPU_CAPACITY];
static TrailAttachmentRegistry s_attachments;
static float s_time;
static Shader s_cpuShader;

static ModernRibbon *FindRibbon(int id) {
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++)
        if(s_ribbons[i].active && s_ribbons[i].trailId==id) return &s_ribbons[i];
    return NULL;
}
TrailRibbonConfig TrailRibbon_Default(void) {
    TrailRibbonConfig c={0};
    c.material=TrailRibbonMaterial_Default();c.nodeCount=24;
    c.tailDirection=(Vector3){0,-1,0};c.lengthM=2;c.widthM=.12f;c.lifetimeSec=5;
    c.color=(Color){255,255,255,255};return c;
}
TrailAttachmentHandle TrailAttachment_Create(Matrix transform) {
    return TrailAttachmentRegistry_Create(&s_attachments,transform);
}
bool TrailAttachment_Update(TrailAttachmentHandle handle,Matrix transform,float dt,bool discontinuity) {
    return TrailAttachmentRegistry_Update(&s_attachments,handle,transform,dt,discontinuity);
}
void TrailAttachment_Destroy(TrailAttachmentHandle handle) {TrailAttachmentRegistry_Destroy(&s_attachments,handle);}
static bool SampleAnchor(const ModernRibbon *r,TrailRibbonAnchor *anchor) {
    return TrailAttachmentRegistry_Snapshot(&s_attachments,r->config.attachment,r->config.attachmentOffset,anchor);
}
static void CopyRenderHistory(ModernRibbon *r,TrailEntity *t) {
    t->position=r->state.position[0];t->velocity=r->state.velocity[0];
    t->historyCount=r->state.count;t->historyHead=r->state.count-1;
    for(int i=0;i<r->state.count;i++) {
        int h=r->state.count-1-i;
        t->history[h]=r->state.position[i];t->nodeVelocity[h]=r->state.velocity[i];
    }
}
int TrailRibbon_Spawn(const TrailRibbonConfig *c) {
    if(!c||!isfinite(c->widthM)||c->widthM<=0||!isfinite(c->lifetimeSec)||c->lifetimeSec<=0||
       c->backend<TRAIL_RIBBON_AUTO||c->backend>TRAIL_RIBBON_GPU_ONLY||
       !isfinite(c->material.stretchCompliance)||c->material.stretchCompliance<0||
       !isfinite(c->material.bendCompliance)||!Field_FiniteVector(c->attachmentOffset)) return -1;
    const float *body=(const float *)&c->material.body;
    for(unsigned i=0;i<sizeof(c->material.body)/sizeof(float);i++) if(!isfinite(body[i])) return -1;
    ModernRibbon *r=NULL;
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) if(!s_ribbons[i].active) {r=&s_ribbons[i];break;}
    if(!r) return -1;
    TrailRibbonState state;
    Vector3 head=c->headPosition;
    if(c->mode==TRAIL_RIBBON_HEAD_ANCHORED) {
        TrailRibbonAnchor anchor;
        if(!TrailAttachmentRegistry_Snapshot(&s_attachments,c->attachment,c->attachmentOffset,&anchor)) return -1;
        head=anchor.position;
    }
    if(!TrailRibbon_Initialize(&state,c->nodeCount,head,c->tailDirection,c->lengthM,c->initialVelocity,c->mode)) return -1;
    int gpu=-1;
    if(c->backend!=TRAIL_RIBBON_CPU_ONLY) gpu=TrailRibbonGpu_Spawn(&state,&c->material);
    if(c->backend==TRAIL_RIBBON_GPU_ONLY && gpu<0) return -1;
    TrailConfig legacy={0};legacy.type=TRAIL_TYPE_FOLLOWER;legacy.pos=head;
    legacy.life=c->lifetimeSec;legacy.thick=c->widthM*.5f;legacy.tint=c->color;
    legacy.tex=c->texture;legacy.trailLength=c->nodeCount;legacy.useCustomBlendMode=true;
    legacy.blendMode=BLEND_ALPHA;legacy.disableInnerCore=true;
    int id=SpawnTrailEntity(legacy);
    if(id<0) {if(gpu>=0) TrailRibbonGpu_Kill(gpu);return -1;}
    memset(r,0,sizeof(*r));r->active=true;r->trailId=id;r->gpuSlot=gpu;r->config=*c;r->state=state;
    CopyRenderHistory(r,GetTrail(id));return id;
}
bool TrailRibbon_ReleaseHead(int id) {
    ModernRibbon *r=FindRibbon(id);if(!r) return false;
    TrailRibbonAnchor anchor;SampleAnchor(r,&anchor);
    TrailRibbon_Release(&r->state,&anchor);
    r->config.mode=TRAIL_RIBBON_FREE;
    if(r->gpuSlot>=0) TrailRibbonGpu_Release(r->gpuSlot,&anchor);
    return true;
}
const TrailRibbonState *TrailRibbon_GetState(int id) {
    ModernRibbon *r=FindRibbon(id);return r&&r->gpuSlot<0?&r->state:NULL;
}
TrailRibbonBackend TrailRibbon_GetBackend(int id) {
    ModernRibbon *r=FindRibbon(id);return r&&r->gpuSlot>=0?TRAIL_RIBBON_GPU_ONLY:TRAIL_RIBBON_CPU_ONLY;
}
void TrailRibbonSystem_Reset(void) {
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) if(s_ribbons[i].active&&s_ribbons[i].gpuSlot>=0)
        TrailRibbonGpu_Kill(s_ribbons[i].gpuSlot);
    memset(s_ribbons,0,sizeof(s_ribbons));TrailAttachmentRegistry_Reset(&s_attachments);
}
void TrailRibbonSystem_BeginUpdate(float dt,float time) {s_time=time;TrailRibbonGpu_BeginUpdate(dt,time);}
static void SampleMotion(void *user,Vector3 position,Vector3 velocity,float dt,int node,float offset,
    FieldSample *sample,Vector3 *air) {
    ModernRibbon *r=user;const MotionBodyProfile *profile=&r->config.material.body;
    BodyPhysicalProperties body=MotionBody_GetPhysicalProperties(profile);
    MediumProperties medium=Wind_EvaluateBackgroundMedium(position,s_time+offset,
        (Vector3){0,-9.81f*profile->gravityScale,0});
    if(profile->airDensityKgM3>0) medium.densityKgM3=profile->airDensityKgM3;
    ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
    MotionFields_SampleSpatialBodyAtOffset(position,velocity,&body,&medium,&constraints,dt,offset,
        r->config.material.receiverMask?r->config.material.receiverMask:MOTION_RECEIVER_TRAIL,
        &r->receivers[node],sample);
    *air=medium.velocityMps;
    if(WindZone_IsActive()) sample->accelerationMps2=MotionVec_Add(sample->accelerationMps2,
        MotionVec_Scale(WindZone_Evaluate(position,velocity,s_time+offset),profile->windAccelerationScale*profile->windSusceptibility));
}
bool TrailRibbonSystem_Update(int id,float dt) {
    ModernRibbon *r=FindRibbon(id);if(!r) return false;
    TrailRibbonAnchor anchor={0};
    if(r->config.mode==TRAIL_RIBBON_HEAD_ANCHORED && !SampleAnchor(r,&anchor)) {
        TrailRibbon_Release(&r->state,NULL);r->config.mode=TRAIL_RIBBON_FREE;
    }
    if(r->gpuSlot>=0) TrailRibbonGpu_Update(r->gpuSlot,dt,&anchor);
    else {
        TrailRibbon_Advance(&r->state,&r->config.material,dt,&anchor,SampleMotion,r);
        CopyRenderHistory(r,GetTrail(id));
    }
    return true;
}
void TrailRibbonSystem_Kill(int id) {
    ModernRibbon *r=FindRibbon(id);if(!r) return;
    if(r->gpuSlot>=0) TrailRibbonGpu_Kill(r->gpuSlot);
    r->active=false;
}
bool TrailRibbonSystem_IsModern(int id) {return FindRibbon(id)!=NULL;}
void TrailRibbonSystem_Draw(Camera3D camera,int layerFilter) {
    if(layerFilter==1) return; /* Plain physical ribbons contribute alpha body. */
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) {
        ModernRibbon *r=&s_ribbons[i];if(!r->active) continue;
        TrailEntity *t=GetTrail(r->trailId);if(!t||!t->active) continue;
        Color color=r->config.color;
        color.a=(unsigned char)(color.a*fminf(1,t->lifetime/t->maxLifetime));
        if(r->gpuSlot>=0) {
            TrailRibbonGpu_Draw(r->gpuSlot,camera,r->config.widthM,color,r->config.texture);continue;
        }
        if(!s_cpuShader.id) s_cpuShader=ResourceManager_LoadShader(NULL,"core/trails/shaders/trail_ribbon_gpu.fs");
        RibbonPoint points[TRAIL_RIBBON_MAX_NODES];
        for(int n=0;n<r->state.count;n++) points[n]=(RibbonPoint){.position=r->state.position[n],
            .halfWidth=r->config.widthM*.5f,.tint=color};
        Ribbon_ComputeArcLengthUV(points,r->state.count);
        rlDrawRenderBatchActive();BeginBlendMode(BLEND_ALPHA);BeginShaderMode(s_cpuShader);
        rlDisableDepthMask();
        DrawRibbonStrip(points,r->state.count,r->config.texture,camera);
        rlDrawRenderBatchActive();rlEnableDepthMask();EndShaderMode();EndBlendMode();
    }
}
void TrailRibbonSystem_Unload(void) {TrailRibbonSystem_Reset();TrailRibbonGpu_Unload();s_cpuShader=(Shader){0};}

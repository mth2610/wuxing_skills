#include "core/trails/trail_ribbon.h"
#include "core/trails/trail_ribbon_gpu.h"
#include "core/trails/trail_system.h"
#include "core/motion/motion_fields.h"
#include "core/wind/wind_system.h"
#include "core/resource_manager.h"
#include "core/vfx_render.h"
#include "rlgl.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    bool active;
    int trailId,gpuSlot;
    TrailRibbonConfig config;
    TrailRibbonState state;
    float pathDistanceM;
    MotionPathTransportState pathState[TRAIL_RIBBON_MAX_NODES];
    MotionReceiver receivers[TRAIL_RIBBON_MAX_NODES];
} ModernRibbon;
static ModernRibbon s_ribbons[TRAIL_RIBBON_GPU_CAPACITY];
static TrailAttachmentHandle s_ownedAttachments[TRAIL_ATTACHMENT_CAPACITY];
static float s_time;
static Shader s_cpuShader,s_cpuAppearanceShader;

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
    for(int i=0;i<TRAIL_ATTACHMENT_CAPACITY;i++) {
        MotionFrameSnapshot snapshot;
        if(s_ownedAttachments[i] && !MotionFrame_Snapshot(s_ownedAttachments[i],(Vector3){0},&snapshot))
            s_ownedAttachments[i]=0;
        if(!s_ownedAttachments[i]) {
            TrailAttachmentHandle handle=MotionFrame_Create(transform);
            s_ownedAttachments[i]=handle;return handle;
        }
    }
    return 0;
}
bool TrailAttachment_Update(TrailAttachmentHandle handle,Matrix transform,float dt,bool discontinuity) {
    return MotionFrame_Update(handle,transform,dt,discontinuity);
}
void TrailAttachment_Destroy(TrailAttachmentHandle handle) {
    MotionFrame_Destroy(handle);
    for(int i=0;i<TRAIL_ATTACHMENT_CAPACITY;i++) if(s_ownedAttachments[i]==handle) s_ownedAttachments[i]=0;
}
static bool SampleAnchor(const ModernRibbon *r,TrailRibbonAnchor *anchor) {
    MotionFrameSnapshot s;
    bool valid=MotionFrame_Snapshot(r->config.attachment,r->config.attachmentOffset,&s);
    *anchor=(TrailRibbonAnchor){.previousPosition=s.previousPosition,.position=s.position,
        .velocity=s.velocity,.valid=s.valid,.discontinuity=s.discontinuity};
    return valid;
}
static void CopyRenderHistory(ModernRibbon *r,TrailEntity *t) {
    t->position=r->state.position[0];t->velocity=r->state.velocity[0];
    float arc=0;
    for(int n=r->state.count-1;n>=0;n--) {
        if(n<r->state.count-1) arc+=Vector3Distance(r->state.position[n],r->state.position[n+1]);
        t->nodeUV[r->state.count-1-n]=arc;
    }
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
    if(!TrailRibbonAppearance_IsValid(&c->appearance)) return -1;
    const float *body=(const float *)&c->material.body;
    for(unsigned i=0;i<sizeof(c->material.body)/sizeof(float);i++) if(!isfinite(body[i])) return -1;
    ModernRibbon *r=NULL;
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) if(!s_ribbons[i].active) {r=&s_ribbons[i];break;}
    if(!r) return -1;
    TrailRibbonState state;
    Vector3 head=c->headPosition;
    if(c->mode==TRAIL_RIBBON_HEAD_ANCHORED) {
        TrailRibbonAnchor anchor;
        MotionFrameSnapshot frame;
        if(!MotionFrame_Snapshot(c->attachment,c->attachmentOffset,&frame)) return -1;
        anchor.position=frame.position;
        head=anchor.position;
    }
    if(!TrailRibbon_Initialize(&state,c->nodeCount,head,c->tailDirection,c->lengthM,c->initialVelocity,c->mode)) return -1;
    MotionPathTransport transport=c->pathTransport;
    if(transport.field) {
        MotionPathTransportSnapshot view;
        if(c->mode!=TRAIL_RIBBON_FREE || !isfinite(transport.speedMps) || transport.speedMps<0 ||
           !isfinite(transport.startDistanceM) || !Field_FiniteVector(transport.laneOffset) ||
           !MotionFields_GetPathTransport(transport.field,&view)) return -1;
        if(transport.captureBirthLane) transport.laneOffset=MotionVec_Add(transport.laneOffset,
            MotionPathTransport_CaptureLane(&view,head));
        for(int n=0;n<state.count;n++) {
            state.position[n]=MotionPathTransport_Sample(&view,transport.startDistanceM-n*state.restLength[1],
                transport.respondToField?(Vector3){0}:transport.laneOffset);
            state.previous[n]=state.position[n];state.velocity[n]=(Vector3){0};
        }
    }
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
    r->config.pathTransport=transport;r->pathDistanceM=transport.startDistanceM;
    if(gpu>=0 && transport.field) TrailRibbonGpu_SetPathTransport(gpu,&transport,r->pathDistanceM);
    TrailEntity *entity=GetTrail(id);
    if(c->appearance.enabled) {
        const TrailRibbonAppearance *a=&r->config.appearance;
        entity->material=a->material;entity->deform=a->deform;entity->deform.mode=0;
        entity->layers=a->layers;entity->layerCount=a->layerCount;
        entity->widthEnvelope=a->widthEnvelope;entity->widthCurve=a->widthCurve;
        entity->alphaCurve=a->alphaCurve;entity->gradient=a->gradient;
        entity->ribbonMode=a->ribbonMode;entity->fixedNormal=a->fixedNormal;
        entity->blendMode=a->blendMode;entity->useCustomBlendMode=true;
    }
    CopyRenderHistory(r,entity);
    if(getenv("WUXING_TRAIL_MOTION_TRACE"))
        TraceLog(LOG_INFO,"TRAIL_MOTION: trail=%d backend=%s nodes=%d mode=%s",id,
            gpu>=0?"GPU":"CPU",state.count,
            state.mode==TRAIL_RIBBON_HEAD_ANCHORED?"anchored":"free");
    return id;
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
    memset(s_ribbons,0,sizeof(s_ribbons));
    for(int i=0;i<TRAIL_ATTACHMENT_CAPACITY;i++) {
        MotionFrame_Destroy(s_ownedAttachments[i]);s_ownedAttachments[i]=0;
    }
}
void TrailRibbonSystem_BeginUpdate(float dt,float time) {s_time=time;TrailRibbonGpu_BeginUpdate(dt,time);}
void TrailRibbonSystem_EndUpdate(void) {TrailRibbonGpu_EndUpdate();}
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
    if(r->config.pathTransport.field) {
        MotionPathTransportSnapshot view;
        if(MotionFields_GetPathTransport(r->config.pathTransport.field,&view) && isfinite(dt) && dt>0) {
            r->pathDistanceM=fminf(view.path->length+r->config.lengthM,
                r->pathDistanceM+r->config.pathTransport.speedMps*dt);
            if(r->gpuSlot>=0) TrailRibbonGpu_SetPathTransport(r->gpuSlot,&r->config.pathTransport,r->pathDistanceM);
            else {
                BodyPhysicalProperties body=MotionBody_GetPhysicalProperties(&r->config.material.body);
                for(int n=0;n<r->state.count;n++) {
                    Vector3 old=r->state.position[n];r->state.previous[n]=old;
                    float distance=r->pathDistanceM-n*r->state.restLength[1];
                    if(r->config.pathTransport.respondToField) {
                        float lag=r->config.pathTransport.speedMps>1e-5f?
                            n*r->state.restLength[1]/r->config.pathTransport.speedMps:0;
                        r->state.position[n]=MotionPathTransport_Advance(&view,distance,
                            r->config.pathTransport.laneOffset,lag,dt,&body,
                            r->config.material.body.airDensityKgM3,
                            (Vector3){0,-9.81f*r->config.material.body.gravityScale,0},&r->pathState[n]);
                    } else r->state.position[n]=MotionPathTransport_Sample(&view,
                        distance,r->config.pathTransport.laneOffset);
                    r->state.velocity[n]=MotionVec_Scale(MotionVec_Sub(r->state.position[n],old),1/dt);
                }
                CopyRenderHistory(r,GetTrail(id));
            }
        }
        return true;
    }
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
static void DrawAppearance(ModernRibbon *r,TrailEntity *t,Camera3D camera,int filter) {
    bool emits=t->blendMode!=BLEND_ALPHA;
    if(filter==1&&!emits) return;
    if(!s_cpuAppearanceShader.id && r->gpuSlot<0) s_cpuAppearanceShader=ResourceManager_LoadShader(
        "core/trails/shaders/trail_deform.vs","core/trails/shaders/trail_deform.fs");
    int count=r->state.count,layers=t->layerCount>0?t->layerCount:1;
    float life=fminf(1,t->lifetime/t->maxLifetime);
    for(int layer=0;layer<layers;layer++) {
        if(filter==0&&emits&&layers>=2&&layer!=1) continue;
        TrailLayer fallback={0};const TrailLayer *ly=t->layerCount?&t->layers[layer]:&fallback;
        float widthMul=ly->widthMul>0?ly->widthMul:1;
        float alpha=TrailRibbon_LayerAlpha(t,ly,filter);
        bool whiten=!(filter==0&&emits&&t->material.bodyOpacity>0);
        Vector4 colors[TRAIL_RIBBON_MAX_NODES];float widths[TRAIL_RIBBON_MAX_NODES];
        RibbonPoint points[TRAIL_RIBBON_MAX_NODES];
        for(int n=0;n<count;n++) {
            float head=1-(float)n/(count-1);
            Color c=t->gradient?ColorGradient_Sample(t->gradient,head):r->config.color;
            if(whiten&&ly->whiten>0) {
                float w=fminf(ly->whiten,1);c.r+=(255-c.r)*w;c.g+=(255-c.g)*w;c.b+=(255-c.b)*w;
            }
            float a=alpha*life;
            if(t->alphaCurve) a*=fminf(1,fmaxf(0,SkillCurve_Eval(t->alphaCurve,head)));
            if(ly->headAlphaPow>0) a*=powf(head,ly->headAlphaPow);
            c.a=(unsigned char)fminf(255,fmaxf(0,c.a*a));
            widths[n]=TrailRibbon_WidthEnvelope(t,head,s_time);
            /* CPU profiled primitive applies this same policy at submission. */
            Color gpuColor=VFXContrast_ApplyColor(c,t->material.contrastProfile,
                filter==0||!emits?VFX_CONTRAST_BODY:VFX_CONTRAST_EMISSION);
            colors[n]=(Vector4){gpuColor.r/255.f,gpuColor.g/255.f,gpuColor.b/255.f,gpuColor.a/255.f};
            points[n]=(RibbonPoint){.position=r->state.position[n],.halfWidth=r->config.widthM*.5f*widthMul*widths[n],
                .v=(float)n/(count-1),.tint=c};
        }
        Texture2D tex=ly->texture?*ly->texture:r->config.texture;
        if(r->gpuSlot>=0) {
            TrailRibbonGpu_DrawAppearance(r->gpuSlot,camera,r->config.widthM*widthMul,tex,t,filter,colors,widths);
        } else {
            rlDrawRenderBatchActive();BeginBlendMode(filter==0?BLEND_ALPHA:t->blendMode);
            BeginShaderMode(s_cpuAppearanceShader);TrailRibbon_BindAppearance(s_cpuAppearanceShader,t,camera,filter);
            rlDisableDepthMask();
            DrawRibbonStripDeformedProfiledEx(points,count,tex,camera,t->ribbonMode,t->fixedNormal,
                t->material.contrastProfile,filter==0||!emits?VFX_CONTRAST_BODY:VFX_CONTRAST_EMISSION);
            rlDrawRenderBatchActive();rlEnableDepthMask();EndShaderMode();EndBlendMode();
        }
    }
}
void TrailRibbonSystem_Draw(Camera3D camera,int layerFilter) {
    for(int i=0;i<TRAIL_RIBBON_GPU_CAPACITY;i++) {
        ModernRibbon *r=&s_ribbons[i];if(!r->active) continue;
        TrailEntity *t=GetTrail(r->trailId);if(!t||!t->active) continue;
        if(r->config.appearance.enabled) {DrawAppearance(r,t,camera,layerFilter);continue;}
        if(layerFilter==1) continue;
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
void TrailRibbonSystem_Unload(void) {TrailRibbonSystem_Reset();TrailRibbonGpu_Unload();s_cpuShader=(Shader){0};s_cpuAppearanceShader=(Shader){0};}

#include "emitter_gpu.h"
#include "emitter_gpu_lifetime.h"
#if defined(GRAPHICS_API_VULKAN) || defined(WUXING_USE_VULKAN)
#include "core/shading/shader_preprocessor.h"
#include "third_party/vulkan/rlvk.h"
#include <string.h>

static unsigned int s_program,s_children,s_bodies,s_events,s_templates,s_counters;
static int s_dtLoc,s_countLoc;
static EmissionGpuParent s_parents[EMISSION_GPU_PARENT_CAPACITY];
static EmissionGpuTemplate s_leaf[EMISSION_GPU_TEMPLATE_CAPACITY];
static unsigned int s_refs[EMISSION_GPU_TEMPLATE_CAPACITY];
static unsigned char s_dirty[EMISSION_GPU_PARENT_CAPACITY];
static bool s_templatesDirty,s_childrenPossible;
static unsigned int s_blends;
static EmissionGpuLifetime s_lifetime;

static EmissionGpuTemplate PackLeaf(const ParticleConfig *source)
{
    ParticleConfig p=*source;ParticleConfig_Unify(&p);
    EmissionGpuTemplate t={0};
    t.body=MotionGpu_PackBody(p.physics.dynamics,p.physics.initialAccelerationMps2,
        p.physics.constantForceNewtons,p.physics.receiveMotionFields,p.windInfluence);
    if(p.physics.dynamics && p.drag>0 && t.body.body0.z<=0) t.body.body0.z=p.drag;
    Vector3 v=MotionVec_Add(p.velocity,MotionVec_Scale(p.physics.initialImpulseNs,t.body.body0.x));
    float boost=p.render.emissiveBoost>0?p.render.emissiveBoost:1;
    t.particle.data[0]=(Vector4){0,0,0,p.radius};
    t.particle.data[1]=(Vector4){v.x,v.y,v.z,p.drag};
    t.particle.data[2]=(Vector4){boost,boost,boost,p.colorStart.a/255.0f};
    t.particle.data[3]=(Vector4){boost,boost,boost,p.colorEnd.a/255.0f};
    t.particle.data[4]=(Vector4){p.lifetime,p.lifetime,0,1};
    t.particle.data[5]=(Vector4){-1,p.stretchStrength,p.collisionEnabled?p.collisionElasticity:-1,p.collisionFloorY};
    t.particle.data[6]=(Vector4){0,0,-1,0};
    t.particle.data[7]=(Vector4){0,0,p.windInfluence,(float)p.render.blendMode};
    t.inheritance.x=p.velocityInheritance;
    return t;
}
static unsigned int Retain(const ParticleConfig *p)
{
    EmissionGpuTemplate t=PackLeaf(p);
    for(unsigned int i=0;i<EMISSION_GPU_TEMPLATE_CAPACITY;i++)
        if(s_refs[i] && !memcmp(&s_leaf[i],&t,sizeof(t))) {s_refs[i]++;return i+1;}
    for(unsigned int i=0;i<EMISSION_GPU_TEMPLATE_CAPACITY;i++) if(!s_refs[i]) {
        s_leaf[i]=t;s_refs[i]=1;s_templatesDirty=true;return i+1;
    }
    return 0;
}
void EmissionGpu_ReleaseParent(unsigned int slot)
{
    if(slot>=EMISSION_GPU_PARENT_CAPACITY) return;
    EmissionGpuParent *p=&s_parents[slot];
    if(p->meta[1] || p->meta[2]) EmissionGpuLifetime_Retire(&s_lifetime);
    for(int k=1;k<=2;k++) if(p->meta[k] && s_refs[p->meta[k]-1]) s_refs[p->meta[k]-1]--;
    p->meta[1]=p->meta[2]=0;p->timing=(Vector4){0};s_dirty[slot]=1;
}
bool EmissionGpu_SetParent(unsigned int slot,const ParticleConfig *cfg,int owner)
{
    (void)owner;
    if(slot>=EMISSION_GPU_PARENT_CAPACITY) return false;
    if(!cfg || (!cfg->onDeathEmitCount && !cfg->onLiveEmitRate)) {
        EmissionGpu_ReleaseParent(slot);return true;
    }
    if(!EmissionGpu_ConfigSupported(cfg)) return false;
    /* Ordinary particle/liquid scenes never allocate the child pool or compile
     * its shader. Batch births may introduce children after emitter creation. */
    if(!s_program && !EmissionGpu_Init()) return false;
    unsigned int live=cfg->onLiveEmitRate>0?Retain(cfg->onLiveEmit):0;
    unsigned int death=cfg->onDeathEmitCount>0?Retain(cfg->onDeathEmit):0;
    if((cfg->onLiveEmitRate>0 && !live) || (cfg->onDeathEmitCount>0 && !death)) {
        if(live) s_refs[live-1]--;
        if(death) s_refs[death-1]--;
        return false;
    }
    EmissionGpu_ReleaseParent(slot);
    EmissionGpuParent *p=&s_parents[slot];
    if(++p->meta[0]==0) p->meta[0]=1;
    p->meta[1]=live;p->meta[2]=death;
    p->meta[3]=(unsigned int)cfg->onDeathEmitCount;
    p->timing=(Vector4){cfg->onLiveEmitRate,0,1,0};
    s_childrenPossible=true;
    float life=live?s_leaf[live-1].particle.data[4].y:0;
    if(death) life=fmaxf(life,s_leaf[death-1].particle.data[4].y);
    EmissionGpuLifetime_Bind(&s_lifetime,life);
    if(p->meta[1]) s_blends|=1u<<(int)s_leaf[p->meta[1]-1].particle.data[7].w;
    if(p->meta[2]) s_blends|=1u<<(int)s_leaf[p->meta[2]-1].particle.data[7].w;
    return true;
}
bool EmissionGpu_Init(void)
{
    if(s_program) return true;
    memset(s_parents,0,sizeof(s_parents));memset(s_refs,0,sizeof(s_refs));
    memset(s_dirty,0,sizeof(s_dirty));s_childrenPossible=false;s_blends=0;
    s_lifetime=(EmissionGpuLifetime){0};
    char *source=ShaderPreprocessor_Load("core/emitter/shaders/emitter_gpu.comp");
    if(!source) return false;
    unsigned int shader=rlLoadShader(source,RL_COMPUTE_SHADER);MemFree(source);
    if(!shader || shader==~0u) return false;
    s_program=rlLoadShaderProgramCompute(shader);rlUnloadShader(shader);
    if(!s_program || s_program==~0u) {s_program=0;return false;}
    s_dtLoc=rlGetLocationUniform(s_program,"u_dt");
    s_countLoc=rlGetLocationUniform(s_program,"u_parentCount");
    /* Explicit zero initialization: resident GPU allocations need not be zeroed. */
    static EmissionGpuParticle zeroChildren[EMISSION_GPU_CHILD_CAPACITY];
    static MotionGpuBody zeroBodies[EMISSION_GPU_CHILD_CAPACITY];
    unsigned int counters[4]={0};
    s_children=rlLoadShaderBuffer(sizeof(zeroChildren),zeroChildren,RL_DYNAMIC_DRAW);
    s_bodies=rlLoadShaderBuffer(sizeof(zeroBodies),zeroBodies,RL_DYNAMIC_DRAW);
    s_events=rlLoadShaderBuffer(sizeof(s_parents),s_parents,RL_DYNAMIC_DRAW);
    s_templates=rlLoadShaderBuffer(sizeof(s_leaf),s_leaf,RL_DYNAMIC_DRAW);
    s_counters=rlLoadShaderBuffer(sizeof(counters),counters,RL_DYNAMIC_DRAW);
    unsigned int *allocated[]={&s_children,&s_bodies,&s_events,&s_templates,&s_counters};
    bool valid=true;
    for(unsigned int i=0;i<sizeof(allocated)/sizeof(allocated[0]);i++)
        if(!*allocated[i] || *allocated[i]==~0u) {*allocated[i]=0;valid=false;}
    if(!valid || s_dtLoc<0 || s_countLoc<0) {
        EmissionGpu_Unload();return false;
    }
    return true;
}
void EmissionGpu_Dispatch(unsigned int parentBuffer,unsigned int count,float dt)
{
    if(!s_program || !s_childrenPossible || count==0 || dt<=0) return;
    /* Parent lifetime bookkeeping bounds the latest possible birth. Retain the
     * entire maximum child lifetime after the last parent retires, then stop
     * idle dispatch/draw work without querying GPU state. */
    if(!EmissionGpuLifetime_Advance(&s_lifetime,dt)) {
        s_childrenPossible=false;s_blends=0;return;
    }
    for(unsigned int i=0;i<EMISSION_GPU_PARENT_CAPACITY;i++) if(s_dirty[i]) {
        rlUpdateShaderBuffer(s_events,&s_parents[i],sizeof(s_parents[i]),i*sizeof(s_parents[i]));s_dirty[i]=0;
    }
    if(s_templatesDirty) {
        rlUpdateShaderBuffer(s_templates,s_leaf,sizeof(s_leaf),0);s_templatesDirty=false;
    }
    unsigned int zero=0;
    /* Cursor remains GPU-owned. Reset only this dispatch's budget counters. */
    rlUpdateShaderBuffer(s_counters,&zero,sizeof(zero),sizeof(zero));
    rlEnableShader(s_program);int n=(int)count;
    rlSetUniform(s_dtLoc,&dt,RL_SHADER_UNIFORM_FLOAT,1);
    rlSetUniform(s_countLoc,&n,RL_SHADER_UNIFORM_INT,1);
    rlBindShaderBuffer(parentBuffer,0);rlBindShaderBuffer(s_children,1);
    rlBindShaderBuffer(s_bodies,2);rlBindShaderBuffer(s_events,3);
    rlBindShaderBuffer(s_templates,4);rlBindShaderBuffer(s_counters,5);
    rlComputeShaderDispatch((count+255)/256,1,1);rlDisableShader();
}
unsigned int EmissionGpu_ChildBuffer(void) {return s_children;}
unsigned int EmissionGpu_ChildBodyBuffer(void) {return s_bodies;}
bool EmissionGpu_HasChildren(void) {return s_childrenPossible;}
unsigned int EmissionGpu_BlendMask(void) {return s_blends;}
void EmissionGpu_Unload(void)
{
    unsigned int *ids[]={&s_children,&s_bodies,&s_events,&s_templates,&s_counters};
    for(unsigned int i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(*ids[i]) {
        rlUnloadShaderBuffer(*ids[i]);*ids[i]=0;
    }
    if(s_program) {rlUnloadShaderProgram(s_program);s_program=0;}
    s_childrenPossible=false;
}

#else
bool EmissionGpu_Init(void) {return false;}
void EmissionGpu_Unload(void) {}
bool EmissionGpu_SetParent(unsigned int slot,const ParticleConfig *p,int owner)
{
    (void)slot;(void)owner;
    return !p || (!p->onDeathEmitCount && !p->onLiveEmitRate);
}
void EmissionGpu_ReleaseParent(unsigned int slot) {(void)slot;}
void EmissionGpu_Dispatch(unsigned int buffer,unsigned int count,float dt)
{(void)buffer;(void)count;(void)dt;}
unsigned int EmissionGpu_ChildBuffer(void) {return 0;}
unsigned int EmissionGpu_ChildBodyBuffer(void) {return 0;}
bool EmissionGpu_HasChildren(void) {return false;}
unsigned int EmissionGpu_BlendMask(void) {return 0;}
#endif

#include "core/emitter/particle_children.h"
#include <math.h>
#include <string.h>
typedef struct {
    uint32_t generation;
    bool active;
    ParticleConfig templates[4];
    int counts[4];
    float liveRate, liveTimer;
} ChildPolicy;
static ChildPolicy s_policies[EMISSION_PARTICLE_PARENT_CAPACITY];
static EmissionChildSpawnFn s_spawn;
static EmissionChildRandomFn s_random;
static void *s_user;
static EmissionChildrenStats s_stats;
typedef struct {
    uint32_t generation,refs;
    const ParticleConfig *identity;
    ParticleConfig config;
} ImpactPolicy;
static ImpactPolicy s_impacts[64];
static ChildPolicy *Lookup(EmissionChildrenHandle h) {
    unsigned index=(unsigned)h;
    if(!index || index>EMISSION_PARTICLE_PARENT_CAPACITY) return NULL;
    ChildPolicy *p=&s_policies[index-1];
    return p->active && p->generation==(uint32_t)(h>>32)?p:NULL;
}
static uint32_t NextGeneration(uint32_t n) {return n==UINT32_MAX?1:n+1;}
void EmissionChildren_Init(EmissionChildSpawnFn spawn,EmissionChildRandomFn random,void *user) {
    EmissionImpact_Init();
    for(int i=0;i<EMISSION_PARTICLE_PARENT_CAPACITY;i++) {
        s_policies[i].active=false;
        s_policies[i].generation=NextGeneration(s_policies[i].generation);
    }
    s_spawn=spawn;s_random=random;s_user=user;s_stats=(EmissionChildrenStats){0};
}
static void CopyLeaf(ParticleConfig *out,const ParticleConfig *in) {
    *out=*in;
    ParticleConfig_Unify(out);
    out->onDeathEmit=out->onLiveEmit=out->onTargetEmit=out->onCollisionEmit=NULL;
    out->onDeathEmitCount=out->onTargetEmitCount=out->onCollisionEmitCount=0;
    out->onLiveEmitRate=0;
    out->physics.onCollisionEmit=out->physics.onTargetEmit=NULL;
    out->physics.onCollisionEmitCount=out->physics.onTargetEmitCount=0;
}
EmissionChildrenHandle EmissionChildren_Bind(int slot,const ParticleConfig *parent) {
    if(slot<0 || slot>=EMISSION_PARTICLE_PARENT_CAPACITY || !parent || !s_spawn) return 0;
    ChildPolicy *p=&s_policies[slot];
    p->active=false;p->generation=NextGeneration(p->generation);
    memset(p->counts,0,sizeof(p->counts));p->liveRate=p->liveTimer=0;
    const ParticleConfig *templates[4]={parent->onLiveEmit,parent->onDeathEmit,
        parent->physics.onCollisionEmit,parent->physics.onTargetEmit};
    const int counts[4]={1,parent->onDeathEmitCount,
        parent->physics.onCollisionEmitCount,parent->physics.onTargetEmitCount};
    for(int i=0;i<4;i++) if(templates[i] && counts[i]>0 &&
        (i!=EMISSION_EVENT_LIVE || (isfinite(parent->onLiveEmitRate)&&parent->onLiveEmitRate>0))) {
        CopyLeaf(&p->templates[i],templates[i]);p->counts[i]=counts[i];p->active=true;
    }
    p->liveRate=p->counts[0]?parent->onLiveEmitRate:0;
    return p->active?((uint64_t)p->generation<<32)|(unsigned)(slot+1):0;
}
void EmissionChildren_Release(EmissionChildrenHandle h) {ChildPolicy *p=Lookup(h);if(p)p->active=false;}
static bool FiniteVector(Vector3 v) {return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static float Random01(void) {return s_random?s_random(s_user):.5f;}
static void Submit(const ParticleConfig *config,const EmissionEvent *event,float t) {
    ParticleConfig child=*config;
    child.position=(Vector3){event->position.x-event->stepDisplacement.x*(1-t),
        event->position.y-event->stepDisplacement.y*(1-t),
        event->position.z-event->stepDisplacement.z*(1-t)};
    if(event->kind==EMISSION_EVENT_COLLISION) {
        float angle=Random01()*6.283185307179586f;
        float speed=sqrtf(child.velocity.x*child.velocity.x+child.velocity.y*child.velocity.y+child.velocity.z*child.velocity.z);
        speed=speed>0?speed*(.3f+.7f*Random01()):Random01()*.4f+.1f;
        child.velocity.x+=cosf(angle)*speed;
        child.velocity.y+=Random01()*.25f+.05f;
        child.velocity.z+=sinf(angle)*speed;
    } else {
        child.velocity.x+=event->velocity.x*child.velocityInheritance;
        child.velocity.y+=event->velocity.y*child.velocityInheritance;
        child.velocity.z+=event->velocity.z*child.velocityInheritance;
    }
    child.physics.position=child.position;child.physics.velocity=child.velocity;
    s_spawn(s_user,child);s_stats.submitted++;
}
void EmissionChildren_Publish(EmissionChildrenHandle h,const EmissionEvent *event) {
    ChildPolicy *p=Lookup(h);
    if(!p || !event || event->kind<EMISSION_EVENT_LIVE || event->kind>EMISSION_EVENT_ARRIVAL ||
        !FiniteVector(event->position)||!FiniteVector(event->velocity)||!FiniteVector(event->stepDisplacement)) return;
    int kind=event->kind,count=p->counts[kind];
    if(!count) return;
    if(kind==EMISSION_EVENT_LIVE) {
        if(!isfinite(event->dt)||event->dt<=0) return;
        float interval=1/p->liveRate;
        if(!isfinite(p->liveTimer+event->dt)) return;
        p->liveTimer+=event->dt;
        float total=p->liveTimer;
        int emitted=0;
        while(p->liveTimer>=interval && emitted<10) {
            p->liveTimer-=interval;
            Submit(&p->templates[kind],event,(total-p->liveTimer)/total);emitted++;
        }
        if(p->liveTimer>=interval) {
            double lost=floor((double)p->liveTimer/interval);
            uint64_t available=UINT64_MAX-s_stats.dropped;
            s_stats.dropped+=lost>=(double)available?available:(uint64_t)lost;
            p->liveTimer=0;
        }
    } else {
        int admitted=count>EMISSION_PARTICLE_EVENT_BUDGET?EMISSION_PARTICLE_EVENT_BUDGET:count;
        s_stats.dropped+=(uint64_t)(count-admitted);
        for(int i=0;i<admitted;i++) Submit(&p->templates[kind],event,1);
    }
}
EmissionChildrenStats EmissionChildren_GetStats(void) {return s_stats;}
void EmissionImpact_Init(void) {
    for(int i=0;i<64;i++) {
        s_impacts[i].refs=0;s_impacts[i].identity=NULL;
        s_impacts[i].generation=NextGeneration(s_impacts[i].generation);
    }
}
EmissionChildrenHandle EmissionImpact_Retain(const ParticleConfig *config) {
    if(!config) return 0;
    ParticleConfig leaf;CopyLeaf(&leaf,config);
    int freeSlot=-1;
    for(int i=0;i<64;i++) {
        ImpactPolicy *p=&s_impacts[i];
        if(p->refs && p->identity==config && !memcmp(&p->config,&leaf,sizeof(leaf))) {
            if(p->refs==UINT32_MAX) return 0;
            p->refs++;return ((uint64_t)p->generation<<32)|(unsigned)(i+1);
        }
        if(!p->refs && freeSlot<0) freeSlot=i;
    }
    if(freeSlot<0) return 0;
    ImpactPolicy *p=&s_impacts[freeSlot];
    p->config=leaf;p->identity=config;p->refs=1;p->generation=NextGeneration(p->generation);
    return ((uint64_t)p->generation<<32)|(unsigned)(freeSlot+1);
}
static ImpactPolicy *ImpactLookup(EmissionChildrenHandle h) {
    unsigned i=(unsigned)h;
    if(!i || i>64) return NULL;
    ImpactPolicy *p=&s_impacts[i-1];
    return p->refs && p->generation==(uint32_t)(h>>32)?p:NULL;
}
void EmissionImpact_Release(EmissionChildrenHandle h) {
    ImpactPolicy *p=ImpactLookup(h);if(p && !--p->refs) p->identity=NULL;
}
void EmissionImpact_Publish(EmissionChildrenHandle h,int count,const EmissionEvent *event) {
    ImpactPolicy *p=ImpactLookup(h);
    if(!p || !s_spawn || !event || event->kind!=EMISSION_EVENT_ARRIVAL || count<=0 ||
        !FiniteVector(event->position)||!FiniteVector(event->velocity)) return;
    ParticleConfig config=p->config;
    int admitted=count>EMISSION_PARTICLE_EVENT_BUDGET?EMISSION_PARTICLE_EVENT_BUDGET:count;
    s_stats.dropped+=(uint64_t)(count-admitted);
    for(int i=0;i<admitted;i++) Submit(&config,event,1);
}
void EmissionLegacy_CollisionDust(Vector3 position,EmissionRandomIntFn random,void *user) {
    if(!s_spawn || !random || !FiniteVector(position)) return;
    static const SkillCurve curve={.stops={{0,.2f},{.5f,1},{1,1.2f}},.count=3};
    /* Exact legacy dust colours, retained as compatibility material data. */
    ParticleConfig dust={.position=position,.lifetime=.4f,.radius=.12f,
        .colorStart={200,200,200,140},.colorEnd={220,220,220,0},.radiusCurve=&curve};
    for(int i=0;i<3;i++) {
        float angle=random(0,359,user)*.017453292519943295f;
        float speed=random(100,200,user)*.01f;
        dust.velocity=(Vector3){cosf(angle)*speed,random(80,180,user)*.01f,sinf(angle)*speed};
        s_spawn(s_user,dust);s_stats.submitted++;
    }
}

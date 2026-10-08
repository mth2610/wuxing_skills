#include "raylib.h"
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#define WHITE ((Color){255,255,255,255})
#include "core/emitter/particle_children.c"
#include <assert.h>
#include <stdio.h>
static ParticleConfig births[300];
static int count;
static void Spawn(void *user,ParticleConfig child) {assert(user==&count);births[count++%300]=child;}
static float Random(void *user) {(void)user;return .5f;}
static int RandomInt(int min,int max,void *user) {(void)max;(void)user;return min;}
static void Near(float a,float b) {assert(fabsf(a-b)<.0001f);}
int main(void) {
    EmissionChildren_Init(Spawn,Random,&count);
    ParticleConfig leaf={.velocity={2,0,0},.lifetime=1,.velocityInheritance=.5f};
    leaf.onLiveEmit=&leaf;leaf.onLiveEmitRate=2;
    ParticleConfig parent={.onLiveEmit=&leaf,.onLiveEmitRate=4,
        .onDeathEmit=&leaf,.onDeathEmitCount=3};
    parent.physics.onCollisionEmit=&leaf;parent.physics.onCollisionEmitCount=2;
    parent.physics.onTargetEmit=&leaf;parent.physics.onTargetEmitCount=1;
    EmissionChildrenHandle h=EmissionChildren_Bind(12,&parent);
    assert(h);
    EmissionEvent event={.kind=EMISSION_EVENT_LIVE,.position={5,1,2},
        .velocity={4,2,0},.stepDisplacement={1,0,0},.dt=.125f};
    EmissionChildren_Publish(h,&event);assert(count==0);
    EmissionChildren_Publish(h,&event);assert(count==1);
    Near(births[0].position.x,5);Near(births[0].velocity.x,4);Near(births[0].velocity.y,1);
    Near(births[0].physics.velocity.x,4);
    assert(!births[0].onLiveEmit&&!births[0].onDeathEmit&&!births[0].physics.onTargetEmit);
    /* Multiple interval births retain the exact old pre-integration sampling. */
    event.dt=.5f;EmissionChildren_Publish(h,&event);assert(count==3);
    Near(births[1].position.x,4.5f);Near(births[2].position.x,5);
    event.kind=EMISSION_EVENT_ARRIVAL;EmissionChildren_Publish(h,&event);assert(count==4);
    Near(births[3].position.x,5);Near(births[3].physics.position.x,5);
    /* Templates copied at bind; source mutation does not change pending births. */
    leaf.velocity.x=99;
    event.kind=EMISSION_EVENT_DEATH;EmissionChildren_Publish(h,&event);assert(count==7);
    Near(births[4].velocity.x,4);
    event.kind=EMISSION_EVENT_COLLISION;EmissionChildren_Publish(h,&event);assert(count==9);
    Near(births[7].velocity.x,.7f);Near(births[7].velocity.y,.175f);
    event.kind=EMISSION_EVENT_LIVE;event.dt=100;
    EmissionChildren_Publish(h,&event);assert(count==19);
    assert(EmissionChildren_GetStats().dropped==390);
    EmissionChildren_Release(h);EmissionChildren_Publish(h,&event);assert(count==19);
    EmissionChildrenHandle next=EmissionChildren_Bind(12,&parent);assert(next && next!=h);
    EmissionChildren_Publish(h,&event);assert(count==19);
    event.kind=EMISSION_EVENT_DEATH;EmissionChildren_Publish(next,&event);assert(count==22);
    EmissionChildren_Init(Spawn,Random,&count);EmissionChildren_Publish(next,&event);assert(count==22);
    parent.onDeathEmitCount=1000;h=EmissionChildren_Bind(0,&parent);
    EmissionChildren_Publish(h,&event);assert(count==278);
    assert(EmissionChildren_GetStats().dropped==744);
    assert(!EmissionChildren_Bind(-1,&parent));assert(!EmissionChildren_Bind(2000,&parent));
    EmissionChildrenHandle impact=EmissionImpact_Retain(&leaf);
    assert(impact && EmissionImpact_Retain(&leaf)==impact);
    event.kind=EMISSION_EVENT_ARRIVAL;
    EmissionImpact_Publish(impact,2,&event);assert(count==280);
    EmissionImpact_Release(impact);EmissionImpact_Publish(impact,1,&event);assert(count==281);
    EmissionImpact_Release(impact);EmissionImpact_Publish(impact,1,&event);assert(count==281);
    EmissionChildrenHandle newImpact=EmissionImpact_Retain(&leaf);assert(newImpact!=impact);
    EmissionImpact_Init();EmissionImpact_Publish(newImpact,1,&event);assert(count==281);
    EmissionLegacy_CollisionDust((Vector3){1,2,3},RandomInt,NULL);assert(count==284);
    Near(births[281].velocity.x,1);Near(births[281].velocity.y,.8f);Near(births[281].velocity.z,0);
    Near(births[281].position.y,2);assert(births[281].radiusCurve->count==3);
    puts("particle child policies: cadence, inheritance, snapshots, bounded depth/budget, reuse PASS");
}

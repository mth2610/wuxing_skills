#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "core/emitter/emitter.c"
/* Only Mesh's forward declaration is needed by the topology header. */
typedef struct Mesh Mesh;
#include "core/emitter/emitter_sources.c"

static int children;
static bool rejectBirth;
static EmissionSpawn samples[EMITTER_MAX_SPAWNS_PER_STEP];
static bool Spawn(void *user, const EmissionSpawn *spawn) {
    assert(user == &children);
    if (rejectBirth) return false;
    samples[children % EMITTER_MAX_SPAWNS_PER_STEP] = *spawn;
    ++children;
    return true;
}
static EmissionConfig Config(EmissionSchedule schedule) {
    return (EmissionConfig){.kind=EMISSION_RIBBON, .schedule=schedule,
        .sink=Spawn, .sinkUser=&children};
}
static void Reset(void) { EmissionSystem_Init(); children=0; rejectBirth=false; }
static void Near(float a, float b) { assert(fabsf(a-b) < 0.0001f); }
int main(void) {
    Vector3 zero={0};
    EmissionStats stats;
    Reset();
    EmissionConfig c=Config(EMISSION_TIMED_COUNT);
    c.count=7; c.duration=1;
    EmissionHandle h=Emission_Create(&c, zero);
    assert(h && children==0);
    assert(Emission_Step(h, zero, 0)); assert(children==0);
    assert(Emission_Step(h, zero, .5f)); assert(children==3);
    assert(Emission_Step(h, zero, 2)); assert(children==7);
    assert(Emission_GetStats(h,&stats) && !stats.emitting && stats.scheduled==7);
    assert(samples[0].kind==EMISSION_RIBBON);
    assert(Emission_Stop(h)); assert(Emission_Step(h,zero,10)); assert(children==7);
    assert(Emission_Destroy(h));
    EmissionHandle replacement=Emission_Create(&c,zero);
    assert(replacement!=h && !Emission_Stop(h));
    EmissionSystem_Init(); assert(!Emission_Step(replacement,zero,0));

    Reset(); c=Config(EMISSION_DISTANCE); c.spacing=1;
    h=Emission_Create(&c,zero);
    assert(Emission_Step(h,(Vector3){.4f,0,0},.1f)); assert(children==0);
    assert(Emission_Step(h,(Vector3){2.6f,0,0},.1f)); assert(children==2);
    Near(samples[0].sample.position.x,1); Near(samples[1].sample.position.x,2);
    assert(Emission_Step(h,(Vector3){3.2f,0,0},.1f)); assert(children==3);
    Near(samples[2].sample.position.x,3);
    assert(Emission_Stop(h)); assert(Emission_Step(h,(Vector3){100,0,0},1)); assert(children==3);

    Reset(); c=Config(EMISSION_RATE); c.rate=4; c.duration=.5f;
    h=Emission_Create(&c,zero);
    assert(Emission_Step(h,zero,.125f)); assert(children==0);
    assert(Emission_Step(h,zero,2)); assert(children==2);
    assert(Emission_GetStats(h,&stats) && !stats.emitting);
    assert(!Emission_Step(h,zero,NAN)); assert(!Emission_Step(h,zero,-1));

    Reset(); c=Config(EMISSION_BURST); c.count=1000;
    h=Emission_Create(&c,zero);
    assert(Emission_Step(h,zero,0)); assert(children==EMITTER_MAX_SPAWNS_PER_STEP);
    assert(Emission_GetStats(h,&stats) && stats.scheduled==1000 && stats.dropped==744);
    assert(Emission_Step(h,zero,0)); assert(children==256);
    Reset(); rejectBirth=true; c.count=4; h=Emission_Create(&c,zero);
    assert(Emission_Step(h,zero,0)); assert(Emission_GetStats(h,&stats));
    assert(stats.rejected==4 && stats.accepted==0 && children==0);

    Reset(); c=Config(EMISSION_BURST); c.count=1;
    for(int i=0;i<EMITTER_SCHEDULER_CAPACITY;++i) assert(Emission_Create(&c,zero));
    assert(!Emission_Create(&c,zero));
    Reset(); c=Config(EMISSION_TIMED_COUNT); c.duration=0;
    assert(!Emission_Create(&c,zero)); c.duration=1; c.kind=(EmissionKind)99;
    assert(!Emission_Create(&c,zero));

    static MeshAdjacency adj;
    adj.count=2; adj.vertices[0]=(Vector3){0,0,0}; adj.vertices[1]=(Vector3){2,0,0};
    adj.neighborCount[0]=adj.neighborCount[1]=1;
    adj.neighbors[0][0]=1; adj.neighbors[1][0]=0;
    EmissionMeshSource mesh={.adjacency=&adj,.sampling=EMISSION_MESH_EDGE,
        .transform={.m0=1,.m5=1,.m10=1,.m15=1,.m12=4}};
    EmissionSample a,b; uint32_t seedA=27, seedB=27;
    assert(EmissionSource_Mesh(&mesh,&seedA,&a));
    assert(EmissionSource_Mesh(&mesh,&seedB,&b));
    Near(a.position.x,b.position.x); assert(a.position.x>=4 && a.position.x<=6);
    Near(a.normal.y,1);
    mesh.adjacency=NULL; assert(!EmissionSource_Mesh(&mesh,&seedA,&a));
    puts("emitter scheduler: counts, carry, handles, drain, overflow, mesh PASS");
    return 0;
}

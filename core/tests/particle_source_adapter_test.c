#include "raylib.h"
#define WHITE ((Color){255,255,255,255})
typedef struct Mesh Mesh;
typedef struct {int meshCount;} Model;
typedef struct {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#include "core/mesh_adjacency.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
/* Only the old compatibility function needs these Raylib/global-RNG helpers.
 * Any use by the generic adapter fails, keeping seeded sampling independent. */
Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *mesh)
{ (void)mesh; assert(!"generic adapter used global RNG"); return (Vector3){0}; }
Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *mesh)
{ (void)mesh; assert(!"generic adapter used global RNG"); return (Vector3){0}; }
static Vector3 Vector3Transform(Vector3 p, Matrix m)
{
    return (Vector3){m.m0*p.x+m.m4*p.y+m.m8*p.z+m.m12,
        m.m1*p.x+m.m5*p.y+m.m9*p.z+m.m13,
        m.m2*p.x+m.m6*p.y+m.m10*p.z+m.m14};
}
#include "core/emitter/emitter_sources.c"
#include "core/emitter/particle_source.c"
#include "core/emitter/emitter.c"

static EmissionSample births[2][16];
static int counts[2];
static bool Sink(void *user,const EmissionSpawn *spawn)
{
    int lane=*(int *)user;
    assert(counts[lane]<16);
    births[lane][counts[lane]++]=spawn->sample;
    return true;
}
static void Near(Vector3 a,Vector3 b)
{ assert(fabsf(a.x-b.x)<.00001f && fabsf(a.y-b.y)<.00001f && fabsf(a.z-b.z)<.00001f); }
int main(void)
{
    EmissionParticleSourceAdapter adapter={.defaultPosition={3,4,5}};
    EmissionSample sample;
    uint32_t seed=27;
    assert(EmissionSource_Particle(&adapter,&seed,&sample));
    Near(sample.position,adapter.defaultPosition); Near(sample.normal,(Vector3){0,1,0});
    assert(seed==27);
    adapter.source.type=PARTICLE_SOURCE_POINT; adapter.source.point=(Vector3){7,8,9};
    assert(EmissionSource_Particle(&adapter,&seed,&sample));
    Near(sample.position,adapter.source.point); assert(seed==27);
    assert(!EmissionSource_Particle(NULL,&seed,&sample));
    assert(!EmissionSource_Particle(&adapter,NULL,&sample));
    assert(!EmissionSource_Particle(&adapter,&seed,NULL));
    adapter.source.type=(ParticleEmissionSourceType)99;
    assert(!EmissionSource_Particle(&adapter,&seed,&sample));

    static MeshAdjacency mesh;
    mesh.count=2; mesh.vertices[0]=(Vector3){1,2,3}; mesh.vertices[1]=(Vector3){5,6,7};
    mesh.neighborCount[0]=mesh.neighborCount[1]=1;
    mesh.neighbors[0][0]=1; mesh.neighbors[1][0]=0;
    adapter.source.type=PARTICLE_SOURCE_MESH_VERTEX; adapter.source.mesh=&mesh;
    /* Legacy zero matrix becomes identity, preserving geometry instead of
     * collapsing every birth to the origin. */
    assert(EmissionSource_Particle(&adapter,&seed,&sample));
    Near(sample.position,mesh.vertices[seed%2]); Near(sample.normal,(Vector3){0,1,0});
    adapter.source.type=PARTICLE_SOURCE_MESH_EDGE;
    adapter.source.transform=(Matrix){.m0=2,.m5=3,.m10=4,.m15=1,.m12=10};
    EmissionMeshSource reference={.adjacency=&mesh,.transform=adapter.source.transform,
        .sampling=EMISSION_MESH_EDGE};
    uint32_t copy=seed;
    EmissionSample expected;
    assert(EmissionSource_Mesh(&reference,&copy,&expected));
    assert(EmissionSource_Particle(&adapter,&seed,&sample));
    assert(seed==copy); Near(sample.position,expected.position); Near(sample.normal,expected.normal);
    adapter.source.mesh=NULL; assert(!EmissionSource_Particle(&adapter,&seed,&sample));
    adapter.source.mesh=&mesh;

    /* Independent particle/ribbon schedulers consume identical source samples
     * despite different stepping cadence; source storage has no RNG state. */
    EmissionSystem_Init();
    int lanes[2]={0,1};
    EmissionConfig config={.kind=EMISSION_PARTICLE,.schedule=EMISSION_TIMED_COUNT,
        .count=8,.duration=1,.seed=321,.sampleSource=EmissionSource_Particle,
        .source=&adapter,.sink=Sink,.sinkUser=&lanes[0]};
    Vector3 origin={20,30,40};
    EmissionHandle particles=Emission_Create(&config,origin);
    config.kind=EMISSION_RIBBON; config.sinkUser=&lanes[1];
    EmissionHandle ribbons=Emission_Create(&config,origin);
    assert(particles && ribbons);
    assert(Emission_Step(particles,origin,1));
    for(int i=0;i<4;++i) assert(Emission_Step(ribbons,origin,.25f));
    assert(counts[0]==8 && counts[1]==8);
    for(int i=0;i<8;++i) Near(births[0][i].position,births[1][i].position);
    assert(births[0][0].position.x>=32 && births[0][0].position.x<=40);
    puts("particle source adapter: defaults, seeded mesh, transform, shared scheduler parity PASS");
    return 0;
}

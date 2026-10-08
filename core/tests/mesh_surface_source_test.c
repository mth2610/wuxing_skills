#include "raylib.h"
/* Production source uses these geometry fields only; the game sees Raylib's
 * complete borrowed structs. No raylib functions are mocked or linked. */
typedef struct Mesh {
    int vertexCount, triangleCount;
    float *vertices, *normals, *animVertices, *animNormals;
    unsigned short *indices;
} Mesh;
typedef struct Model { int meshCount; Mesh *meshes; } Model;
#include "core/emitter/mesh_surface_source.c"
#include <assert.h>
#include <stdio.h>

static Matrix Identity(void) { return (Matrix){.m0=1,.m5=1,.m10=1,.m15=1}; }
static void Near(float a, float b) { assert(fabsf(a-b)<0.00001f); }
int main(void)
{
    float vertices[]={0,0,0, 1,0,0, 0,1,0};
    float normals[]={1,1,0, 1,1,0, 1,1,0};
    float animated[]={0,0,2, 1,0,2, 0,1,2};
    float animatedNormals[]={0,0,1, 0,0,1, 0,0,1};
    unsigned short indices[]={0,1,2};
    Mesh mesh={.vertexCount=3,.triangleCount=1,.vertices=vertices,
        .normals=normals,.indices=indices};
    EmissionMeshSurfaceSource source={.mesh=&mesh,.transform=Identity()};
    EmissionSample sample, duplicate;
    uint32_t seed=27, copy=27;
    assert(EmissionSource_MeshSurface(&source,&seed,&sample));
    assert(EmissionSource_MeshSurface(&source,&copy,&duplicate));
    assert(seed==copy);
    Near(sample.position.x,duplicate.position.x); Near(sample.position.y,duplicate.position.y);
    assert(sample.position.x>=0 && sample.position.y>=0 && sample.position.x+sample.position.y<=1);
    /* Frozen legacy LCG and reflected barycentric order (tri, u, v). */
    uint32_t expected=27;
    expected=expected*1664525u+1013904223u;
    expected=expected*1664525u+1013904223u;
    float u=(float)(expected>>8)*(1.0f/16777216.0f);
    expected=expected*1664525u+1013904223u;
    float v=(float)(expected>>8)*(1.0f/16777216.0f);
    if(u+v>1) { u=1-u; v=1-v; }
    Near(sample.position.x,u); Near(sample.position.y,v); assert(seed==expected);
    Near(sample.normal.x,sqrtf(.5f)); Near(sample.normal.y,sqrtf(.5f));

    /* Nonuniform scale: a diagonal normal must transform as (1/2,1,0),
     * rather than the legacy wrong (2,1,0). Translation does not affect it. */
    source.transform.m0=2; source.transform.m12=3;
    copy=27; assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    Near(sample.position.x,3+u*2); Near(sample.normal.x,1/sqrtf(5));
    Near(sample.normal.y,2/sqrtf(5));
    /* Rotation and shear expose transposed-cofactor errors. Matrix maps
     * x to +y and y to -x + .5y. Inverse-transpose (1,1,0)=(-.5,1,0). */
    source.transform=(Matrix){.m1=1,.m4=-1,.m5=.5f,.m10=1,.m15=1};
    assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    Near(sample.normal.x,-1/sqrtf(5)); Near(sample.normal.y,2/sqrtf(5));
    source.transform=Identity(); source.transform.m0=-1;
    assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    Near(sample.normal.x,-sqrtf(.5f)); Near(sample.normal.y,sqrtf(.5f));
    /* Inverse-transpose property: normal stays perpendicular to both
     * transformed tangents under rotated, sheared, nonuniform scale. */
    source.transform=(Matrix){.m0=.7f,.m1=1.4f,.m2=.2f,
        .m4=-.8f,.m5=.9f,.m6=.3f,.m8=.4f,.m9=.2f,.m10=2,.m15=1};
    assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    Matrix transform=source.transform;
    Vector3 tangentA={transform.m0-transform.m4,transform.m1-transform.m5,transform.m2-transform.m6};
    Vector3 tangentB={transform.m8,transform.m9,transform.m10};
    Near(sample.normal.x*tangentA.x+sample.normal.y*tangentA.y+sample.normal.z*tangentA.z,0);
    Near(sample.normal.x*tangentB.x+sample.normal.y*tangentB.y+sample.normal.z*tangentB.z,0);

    mesh.animVertices=animated; mesh.animNormals=animatedNormals;
    source.transform=Identity();
    assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    Near(sample.position.z,2); Near(sample.normal.z,1);
    for(int i=0;i<3;i++) animated[i*3+2]=3;
    assert(EmissionSource_MeshSurface(&source,&copy,&sample)); Near(sample.position.z,3);
    mesh.indices=NULL; mesh.triangleCount=0;
    for(int k=0;k<30;k++) {
        assert(EmissionSource_MeshSurface(&source,&copy,&sample));
        assert((sample.position.x==0 && sample.position.y==0) ||
            (sample.position.x==1 && sample.position.y==0) ||
            (sample.position.x==0 && sample.position.y==1));
    }
    mesh.animVertices=NULL; mesh.animNormals=NULL; mesh.normals=NULL;
    assert(EmissionSource_MeshSurface(&source,&copy,&sample)); Near(sample.normal.y,1);
    Mesh meshes[2]={mesh,mesh}; Model model={.meshes=meshes,.meshCount=2};
    source.mesh=NULL; source.model=&model; copy=27;
    assert(EmissionSource_MeshSurface(&source,&copy,&sample));
    expected=27; EmissionSeed_Next(&expected); EmissionSeed_Next(&expected); assert(copy==expected);
    model.meshes=NULL; assert(!EmissionSource_MeshSurface(&source,&copy,&sample));
    source.mesh=&mesh; mesh.indices=indices; mesh.triangleCount=1; indices[2]=3;
    sample.position=(Vector3){91,92,93};
    assert(!EmissionSource_MeshSurface(&source,&copy,&sample)); Near(sample.position.x,91);
    indices[2]=2; source.transform.m5=0;
    assert(!EmissionSource_MeshSurface(&source,&copy,&sample));
    source.transform=Identity(); vertices[0]=NAN;
    assert(!EmissionSource_MeshSurface(&source,&copy,&sample)); vertices[0]=0;
    assert(!EmissionSource_MeshSurface(NULL,&copy,&sample));
    assert(!EmissionSource_MeshSurface(&source,NULL,&sample));
    assert(!EmissionSource_MeshSurface(&source,&copy,NULL));

    EmissionBacklogClock clock={0};
    assert(EmissionBacklog_Step(&clock,16,1,.03125f,8)==0); Near(clock.carry,.5f);
    assert(EmissionBacklog_Step(&clock,16,1,.03125f,8)==1); Near(clock.carry,0);
    assert(EmissionBacklog_Step(&clock,36,1,1,8)==8); Near(clock.carry,28);
    assert(EmissionBacklog_Step(&clock,0,1,0,8)==8); Near(clock.carry,20);
    EmissionBacklog_Queue(&clock,1);
    assert(EmissionBacklog_Step(&clock,16,1,0,8)==1); Near(clock.carry,0);
    assert(EmissionBacklog_Step(&clock,NAN,1,1,8)==0); Near(clock.carry,0);
    assert(EmissionBacklog_Step(&clock,16,1,-1,8)==0); Near(clock.carry,0);
    float legacyCarry=0; unsigned int legacyTotal=0,actualTotal=0;
    for(int i=0;i<300;i++) {
        float dt=i==40?.8f:1.0f/60;
        float rate=i<140?36:16;
        float gain=i<180?.43f:.81f;
        legacyCarry+=dt*rate*gain;
        int count=(int)legacyCarry; if(count>8) count=8;
        legacyCarry-=(float)count; legacyTotal+=(unsigned int)count;
        actualTotal+=EmissionBacklog_Step(&clock,rate,gain,dt,8);
        assert(actualTotal==legacyTotal); Near(clock.carry,legacyCarry);
    }
    puts("mesh surface: live geometry, seeded sampling, transforms, rejection and backlog PASS");
    return 0;
}

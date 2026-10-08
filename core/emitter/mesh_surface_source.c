#include "core/emitter/mesh_surface_source.h"
#include <math.h>
#include <stddef.h>

uint32_t EmissionSeed_Next(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}
float EmissionSeed_Random01(uint32_t *seed)
{
    return (float)(EmissionSeed_Next(seed) >> 8) * (1.0f / 16777216.0f);
}
static bool Surface_Finite(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static Vector3 Surface_Normalize(Vector3 v)
{
    float length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (!(length > 0.0f) || !isfinite(length)) return (Vector3){0,1,0};
    return (Vector3){v.x/length,v.y/length,v.z/length};
}
static Vector3 Surface_Vertex(const float *values, int i)
{
    return (Vector3){values[i*3],values[i*3+1],values[i*3+2]};
}
static Vector3 Surface_Barycentric(const float *values, int i, int j, int k,
                                 float a, float b, float c)
{
    return (Vector3){a*values[i*3]+b*values[j*3]+c*values[k*3],
        a*values[i*3+1]+b*values[j*3+1]+c*values[k*3+1],
        a*values[i*3+2]+b*values[j*3+2]+c*values[k*3+2]};
}
bool EmissionSource_MeshSurface(void *source, uint32_t *seed, EmissionSample *sample)
{
    const EmissionMeshSurfaceSource *s = source;
    const Mesh *mesh;
    const float *vertices, *normals;
    Vector3 p, n = {0,1,0};
    EmissionSample result;
    Matrix t;
    float c0,c1,c2,c4,c5,c6,c8,c9,c10,det;
    if (!s || !seed || !sample) return false;
    mesh = s->mesh;
    if (!mesh && s->model && s->model->meshes && s->model->meshCount > 0)
        mesh = &s->model->meshes[EmissionSeed_Next(seed) % (uint32_t)s->model->meshCount];
    if (!mesh || mesh->vertexCount <= 0) return false;
    vertices = mesh->animVertices ? mesh->animVertices : mesh->vertices;
    normals = mesh->animNormals ? mesh->animNormals : mesh->normals;
    if (!vertices) return false;
    if (mesh->indices && mesh->triangleCount > 0) {
        size_t tri = EmissionSeed_Next(seed) % (uint32_t)mesh->triangleCount;
        int i=mesh->indices[tri*3], j=mesh->indices[tri*3+1], k=mesh->indices[tri*3+2];
        float u,v,a;
        if (i>=mesh->vertexCount || j>=mesh->vertexCount || k>=mesh->vertexCount) return false;
        u=EmissionSeed_Random01(seed); v=EmissionSeed_Random01(seed);
        if (u+v>1.0f) { u=1.0f-u; v=1.0f-v; }
        a=1.0f-u-v;
        p=Surface_Barycentric(vertices,i,j,k,a,u,v);
        if (normals) n=Surface_Normalize(Surface_Barycentric(normals,i,j,k,a,u,v));
    } else {
        int i=(int)(EmissionSeed_Next(seed) % (uint32_t)mesh->vertexCount);
        p=Surface_Vertex(vertices,i);
        if (normals) n=Surface_Normalize(Surface_Vertex(normals,i));
    }
    t=s->transform;
    result.position=(Vector3){t.m0*p.x+t.m4*p.y+t.m8*p.z+t.m12,
        t.m1*p.x+t.m5*p.y+t.m9*p.z+t.m13,t.m2*p.x+t.m6*p.y+t.m10*p.z+t.m14};
    /* Cofactor matrix = determinant * inverse-transpose. */
    c0=t.m5*t.m10-t.m9*t.m6; c4=t.m9*t.m2-t.m1*t.m10; c8=t.m1*t.m6-t.m5*t.m2;
    c1=t.m8*t.m6-t.m4*t.m10; c5=t.m0*t.m10-t.m8*t.m2; c9=t.m4*t.m2-t.m0*t.m6;
    c2=t.m4*t.m9-t.m8*t.m5; c6=t.m8*t.m1-t.m0*t.m9; c10=t.m0*t.m5-t.m4*t.m1;
    det=t.m0*c0+t.m4*c4+t.m8*c8;
    if (!isfinite(det) || det==0.0f) return false;
    n=(Vector3){(c0*n.x+c4*n.y+c8*n.z)/det,
        (c1*n.x+c5*n.y+c9*n.z)/det,(c2*n.x+c6*n.y+c10*n.z)/det};
    if (!Surface_Finite(result.position) || !Surface_Finite(n)) return false;
    result.normal=Surface_Normalize(n);
    *sample=result;
    return true;
}
unsigned int EmissionBacklog_Step(EmissionBacklogClock *clock, float rate, float gain,
                                float dt, unsigned int cap)
{
    unsigned int count;
    float carry;
    if (!clock || !isfinite(rate) || rate<0 || !isfinite(gain) || gain<0 || !isfinite(dt) || dt<0 || !cap) return 0;
    carry=clock->carry+dt*rate*gain;
    if (!isfinite(carry) || carry<0) return 0;
    count = carry >= (float)cap ? cap : (unsigned int)carry;
    clock->carry=carry-(float)count;
    return count;
}
void EmissionBacklog_Queue(EmissionBacklogClock *clock, float count)
{
    if (clock && isfinite(count) && count>=0) clock->carry=count;
}

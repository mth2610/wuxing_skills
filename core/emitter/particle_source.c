#include "core/emitter/particle_source.h"
#include "core/particles/particle_system.h"
#include "core/mesh_adjacency.h"
#include "raymath.h"
static bool Emission_IsZeroMatrix(Matrix m)
{
    return m.m0 == 0.0f && m.m1 == 0.0f && m.m2 == 0.0f && m.m3 == 0.0f &&
           m.m4 == 0.0f && m.m5 == 0.0f && m.m6 == 0.0f && m.m7 == 0.0f &&
           m.m8 == 0.0f && m.m9 == 0.0f && m.m10 == 0.0f && m.m11 == 0.0f &&
           m.m12 == 0.0f && m.m13 == 0.0f && m.m14 == 0.0f && m.m15 == 0.0f;
}

bool Emission_ApplyParticleSource(const ParticleEmissionSource *source,ParticleConfig *particle) {
    if(!source||!particle) return false;
    Vector3 position;
    if(source->type==PARTICLE_SOURCE_CONFIG_POSITION) return true;
    if(source->type==PARTICLE_SOURCE_POINT) position=source->point;
    else if((source->type==PARTICLE_SOURCE_MESH_VERTEX || source->type==PARTICLE_SOURCE_MESH_EDGE)
            && source->mesh && source->mesh->count>0) {
        position=source->type==PARTICLE_SOURCE_MESH_VERTEX
            ?MeshAdjacency_SampleVertex(source->mesh):MeshAdjacency_SampleEdge(source->mesh);
        if(!Emission_IsZeroMatrix(source->transform)) position=Vector3Transform(position,source->transform);
    } else return false;
    particle->position=position;particle->physics.position=position;
    return true;
}

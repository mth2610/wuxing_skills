#include "core/emitter/particle_source.h"
#include "core/emitter/emitter_sources.h"
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

bool EmissionSource_Particle(void *source, uint32_t *seed, EmissionSample *sample)
{
    const EmissionParticleSourceAdapter *adapter = source;
    if (!adapter || !seed || !sample) return false;
    const ParticleEmissionSource *s = &adapter->source;
    if (s->type == PARTICLE_SOURCE_CONFIG_POSITION || s->type == PARTICLE_SOURCE_POINT) {
        sample->position = s->type == PARTICLE_SOURCE_POINT ? s->point : adapter->defaultPosition;
        sample->normal = (Vector3){0.0f, 1.0f, 0.0f};
        return true;
    }
    if (s->type != PARTICLE_SOURCE_MESH_VERTEX && s->type != PARTICLE_SOURCE_MESH_EDGE)
        return false;
    EmissionMeshSource mesh = {
        .adjacency = s->mesh,
        .transform = s->transform,
        .sampling = s->type == PARTICLE_SOURCE_MESH_VERTEX ? EMISSION_MESH_VERTEX : EMISSION_MESH_EDGE
    };
    if (Emission_IsZeroMatrix(mesh.transform))
        mesh.transform = (Matrix){.m0=1.0f, .m5=1.0f, .m10=1.0f, .m15=1.0f};
    return EmissionSource_Mesh(&mesh, seed, sample);
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

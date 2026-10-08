#ifndef WUXING_EMITTER_SOURCES_H
#define WUXING_EMITTER_SOURCES_H
#include "core/emitter/emitter.h"
struct MeshAdjacency;
typedef enum EmissionMeshSampling {
    EMISSION_MESH_VERTEX, EMISSION_MESH_EDGE
} EmissionMeshSampling;
typedef struct EmissionMeshSource {
    const struct MeshAdjacency *adjacency; /* Borrowed; NULL rejects safely. */
    Matrix transform; /* Explicit local-to-world rotation/scale; identity required by default. */
    EmissionMeshSampling sampling;
} EmissionMeshSource;
/* Uses reusable MeshAdjacency topology. Vertex-uniform then neighbor-uniform
 * edge selection, matching legacy utility distribution (not area weighted).
 * Seeded independently of global raylib RNG. Geometry must remain live; owner
 * must clear adjacency before destroying/reusing it. No resource ownership.
 * Translation is included in offset, then scheduler adds its world origin.
 * Adjacency lacks normals; output normal is transformed local +Y. */
bool EmissionSource_Mesh(void *source, uint32_t *seed, EmissionSample *sample);
#endif

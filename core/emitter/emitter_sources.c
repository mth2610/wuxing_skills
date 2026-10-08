#include "core/emitter/emitter_sources.h"
#include "core/mesh_adjacency.h"
#include <stddef.h>
#include <math.h>

static uint32_t EmissionSource_Random(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}
bool EmissionSource_Mesh(void *source, uint32_t *seed, EmissionSample *sample) {
    EmissionMeshSource *s = source;
    if (!s || !seed || !sample || !s->adjacency ||
        s->adjacency->count <= 0 || s->adjacency->count > MAX_TOPOLOGY_VERTICES ||
        (s->sampling != EMISSION_MESH_VERTEX && s->sampling != EMISSION_MESH_EDGE)) return false;
    const MeshAdjacency *a = s->adjacency;
    uint32_t index = EmissionSource_Random(seed) % (uint32_t)a->count;
    Vector3 p = a->vertices[index];
    if (s->sampling == EMISSION_MESH_EDGE && a->neighborCount[index] > 0) {
        if (a->neighborCount[index] > MAX_VERTEX_NEIGHBORS) return false;
        uint32_t n = EmissionSource_Random(seed) % a->neighborCount[index];
        uint32_t other = a->neighbors[index][n];
        if (other >= (uint32_t)a->count) return false;
        float t = (float)(EmissionSource_Random(seed) >> 8) / 16777216.0f;
        Vector3 q = a->vertices[other];
        p.x += (q.x - p.x)*t; p.y += (q.y - p.y)*t; p.z += (q.z - p.z)*t;
    }
    Matrix m = s->transform;
    sample->position = (Vector3){
        m.m0*p.x + m.m4*p.y + m.m8*p.z + m.m12,
        m.m1*p.x + m.m5*p.y + m.m9*p.z + m.m13,
        m.m2*p.x + m.m6*p.y + m.m10*p.z + m.m14};
    double length = sqrt((double)m.m4*m.m4 + (double)m.m5*m.m5 + (double)m.m6*m.m6);
    if (!(length > 0.0) || !isfinite(length)) return false;
    sample->normal = (Vector3){(float)(m.m4/length), (float)(m.m5/length), (float)(m.m6/length)};
    return true;
}

#ifndef WUXING_MESH_SURFACE_SOURCE_H
#define WUXING_MESH_SURFACE_SOURCE_H
#include "core/emitter/emitter.h"

/* Borrowed live geometry, including animated vertex/normal arrays. Explicit
 * mesh takes precedence; otherwise choose uniformly among model meshes. The
 * caller supplies the complete world transform (Model.transform is not added).
 * Indexed triangles are triangle-uniform, not area weighted. Nonindexed
 * geometry retains legacy vertex-uniform sampling. No resource ownership. */
typedef struct EmissionMeshSurfaceSource {
    const Mesh *mesh;
    const Model *model;
    Matrix transform;
} EmissionMeshSurfaceSource;
/* Seeded source with the legacy mesh emitter's exact LCG sequence. Invalid
 * indices, absent geometry, singular transforms or nonfinite output reject
 * the birth safely. Normal uses inverse-transpose under nonuniform scale.
 * Position includes translation; use zero scheduler origin for world output. */
bool EmissionSource_MeshSurface(void *source, uint32_t *seed, EmissionSample *sample);
uint32_t EmissionSeed_Next(uint32_t *seed);
float EmissionSeed_Random01(uint32_t *seed);

/* Compatibility clock for existing effects: cap callbacks per update while
 * retaining hitch backlog. This differs deliberately from Emission_Step's
 * consume-and-count overflow policy. New effects should use the scheduler.
 * Zero dt drains existing backlog, preserving the legacy behavior. */
typedef struct EmissionBacklogClock { float carry; } EmissionBacklogClock;
unsigned int EmissionBacklog_Step(EmissionBacklogClock *clock, float rate, float gain,
                                float dt, unsigned int cap);
/* Replace carry, including a compatibility variant-change startup birth. */
void EmissionBacklog_Queue(EmissionBacklogClock *clock, float count);
#endif

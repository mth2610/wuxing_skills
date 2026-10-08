#ifndef CORE_EMITTER_PARTICLE_SOURCE_H
#define CORE_EMITTER_PARTICLE_SOURCE_H
#include "raylib.h"
#include "core/emitter/emitter.h"
#include <stdbool.h>
// Source-compatible particle adapter declarations; generic sources use emitter_sources.h.
struct MeshAdjacency;
typedef enum ParticleEmissionSourceType {
    /* Backward-compatible default: use ParticleConfig.position. */
    PARTICLE_SOURCE_CONFIG_POSITION = 0,
    PARTICLE_SOURCE_POINT,
    PARTICLE_SOURCE_MESH_VERTEX,
    PARTICLE_SOURCE_MESH_EDGE
} ParticleEmissionSourceType;

/* Emitter-level source sampled once per emitted particle. Mesh modes use a
 * prebuilt MeshAdjacency, so spawning stays O(1); source data is caller-owned
 * and must outlive the emitter. */
typedef struct ParticleEmissionSource {
    ParticleEmissionSourceType type;
    Vector3 point;
    const struct MeshAdjacency *mesh;
    Matrix transform;
} ParticleEmissionSource;

/* Generic scheduler adapter. CONFIG_POSITION emits defaultPosition; POINT
 * emits source.point; mesh modes use independently seeded adjacency sampling.
 * Positions are offsets from the scheduler origin, including mesh translation.
 * Use a zero scheduler origin for absolute legacy positions. A zero mesh matrix
 * retains the legacy identity default. Borrowed topology must outlive scheduler. */
typedef struct EmissionParticleSourceAdapter {
    ParticleEmissionSource source;
    Vector3 defaultPosition;
} EmissionParticleSourceAdapter;
bool EmissionSource_Particle(void *source, uint32_t *seed, EmissionSample *sample);

struct ParticleConfig;
bool Emission_ApplyParticleSource(const ParticleEmissionSource *source, struct ParticleConfig *particle);
#endif

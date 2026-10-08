#ifndef CORE_EMITTER_PARTICLE_SOURCE_H
#define CORE_EMITTER_PARTICLE_SOURCE_H
#include "raylib.h"
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

struct ParticleConfig;
bool Emission_ApplyParticleSource(const ParticleEmissionSource *source, struct ParticleConfig *particle);
#endif

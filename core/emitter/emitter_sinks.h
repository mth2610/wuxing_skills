#ifndef CORE_EMITTER_SINKS_H
#define CORE_EMITTER_SINKS_H
#include "core/emitter/emitter.h"
#include "core/particles/particle_manager.h"
#include "core/trails/trail_ribbon.h"
#include "core/granular/granular_sim.h"
/* Optional component adapters; the scheduler itself includes no component API.
 * Particle handle must use PARTICLE_SOURCE_CONFIG_POSITION so the sampled
 * spawn position is not replaced. A true result means command submission,
 * not a readback-confirmed GPU allocation. spawnTemplate is ParticleConfig. */
typedef struct {
    ParticleEmitterHandle emitter;
    float initialSpeed;          /* Radial launch speed along normal: >0 outward, <0 inward */
    Vector3 center;              /* Emitter center for radial calculation */
    Vector3 swirlAxis;           /* Swirl axis for tangential flow */
    float swirlSpeed;            /* Tangential swirl velocity */
} EmissionParticleSink;
bool EmissionSink_Particle(void *user,const EmissionSpawn *spawn);
/* spawnTemplate is TrailRibbonConfig. Free chains start at sampled position;
 * anchored chains use their attachment. user may be NULL or EmissionRibbonSink*. */
typedef struct {
    int trailAlign;              /* VFX_GuidedTrailAlign */
    float trailAlignAngleDeg;    /* 0 deg = radial, 90 deg = tangent */
    float trailVelocityAngleDeg; /* 0 deg = radial, 90 deg = tangent */
    float initialSpeed;          /* speed applied along velocity direction */
    Vector3 center;              /* Center of the emitter / sphere */
    Vector3 swirlAxis;           /* Swirl axis for tangential flow */
    float swirlSpeed;            /* Tangential swirl velocity */
} EmissionRibbonSink;
bool EmissionSink_Ribbon(void *user,const EmissionSpawn *spawn);

/* spawnTemplate is GranularGrainState (carries materialId, radius, lifetime, flags).
 * Batches births into `batch` and flushes in 1 SSBO upload via EmissionGranularSink_Flush
 * (or automatically when batchCount reaches 256). Works seamlessly with
 * EmissionSource_MeshSurface for shedding sand/soil/leaves/shards off any Mesh/Model. */
#define EMISSION_GRANULAR_BATCH_CAP 256
typedef struct {
    GranularGrainState batch[EMISSION_GRANULAR_BATCH_CAP];
    int batchCount;
    Vector3 receiverPoint;
    Vector3 receiverNormal;
    float initialSpeed;          /* Launch speed along sample normal or radial */
    Vector3 center;              /* Emitter center for radial/swirl calculation */
    Vector3 swirlAxis;           /* Swirl axis for tangential flow */
    float swirlSpeed;            /* Tangential swirl velocity */
} EmissionGranularSink;
bool EmissionSink_Granular(void *user, const EmissionSpawn *spawn);
int  EmissionGranularSink_Flush(EmissionGranularSink *sink);
#endif

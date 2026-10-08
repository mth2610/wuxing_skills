#ifndef CORE_EMITTER_SINKS_H
#define CORE_EMITTER_SINKS_H
#include "core/emitter/emitter.h"
#include "core/particles/particle_manager.h"
#include "core/trails/trail_ribbon.h"
/* Optional component adapters; the scheduler itself includes no component API.
 * Particle handle must use PARTICLE_SOURCE_CONFIG_POSITION so the sampled
 * spawn position is not replaced. A true result means command submission,
 * not a readback-confirmed GPU allocation. spawnTemplate is ParticleConfig. */
typedef struct {ParticleEmitterHandle emitter;} EmissionParticleSink;
bool EmissionSink_Particle(void *user,const EmissionSpawn *spawn);
/* spawnTemplate is TrailRibbonConfig. Free chains start at sampled position;
 * anchored chains use their attachment. user may be NULL. */
bool EmissionSink_Ribbon(void *user,const EmissionSpawn *spawn);
#endif

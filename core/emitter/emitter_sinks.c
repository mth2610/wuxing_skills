#include "core/emitter/emitter_sinks.h"
bool EmissionSink_Particle(void *user,const EmissionSpawn *spawn) {
    EmissionParticleSink *sink=user;
    if(!sink||!spawn||!spawn->spawnTemplate||
       (spawn->kind!=EMISSION_PARTICLE&&spawn->kind!=EMISSION_MESH)||
       ParticleManager_GetEmitterStatus(sink->emitter)!=PARTICLE_EMITTER_OK) return false;
    ParticleConfig particle=*(const ParticleConfig *)spawn->spawnTemplate;
    particle.position=particle.physics.position=spawn->sample.position;
    ParticleManager_EmitBatch(sink->emitter,&particle,1);
    return true;
}
bool EmissionSink_Ribbon(void *user,const EmissionSpawn *spawn) {
    (void)user;
    if(!spawn||spawn->kind!=EMISSION_RIBBON||!spawn->spawnTemplate) return false;
    TrailRibbonConfig config=*(const TrailRibbonConfig *)spawn->spawnTemplate;
    config.headPosition=spawn->sample.position;
    return TrailRibbon_Spawn(&config)>=0;
}

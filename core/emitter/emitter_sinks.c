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
    if(!spawn||spawn->kind!=EMISSION_RIBBON||!spawn->spawnTemplate) return false;
    TrailRibbonConfig config=*(const TrailRibbonConfig *)spawn->spawnTemplate;
    config.headPosition=spawn->sample.position;
    const EmissionRibbonSink *sink=(const EmissionRibbonSink *)user;
    if(sink && sink->trailAlign!=0) {
        Vector3 N=spawn->sample.normal;
        float nLen=sqrtf(N.x*N.x+N.y*N.y+N.z*N.z);
        if(nLen>1e-5f) { N.x/=nLen; N.y/=nLen; N.z/=nLen; }
        else N=(Vector3){0,1,0};

        /* Form an orthonormal tangent vector T perpendicular to N */
        Vector3 up=(fabsf(N.y)<0.9f)?(Vector3){0,1,0}:(Vector3){1,0,0};
        Vector3 T=(Vector3){up.y*N.z-up.z*N.y, up.z*N.x-up.x*N.z, up.x*N.y-up.y*N.x};
        float tLen=sqrtf(T.x*T.x+T.y*T.y+T.z*T.z);
        if(tLen>1e-5f) { T.x/=tLen; T.y/=tLen; T.z/=tLen; }
        else T=(Vector3){1,0,0};

        float alignRad=0.0f;
        if(sink->trailAlign==1) alignRad=0.0f; /* RADIAL: parallel to N */
        else if(sink->trailAlign==2) alignRad=3.14159265f*0.5f; /* SURFACE: 90 deg, tangent T */
        else if(sink->trailAlign==3) alignRad=sink->trailAlignAngleDeg*(3.14159265f/180.0f);

        /* Trail tail direction: points opposite to head extrusion */
        float cosA=cosf(alignRad), sinA=sinf(alignRad);
        config.tailDirection=(Vector3){-(cosA*N.x+sinA*T.x), -(cosA*N.y+sinA*T.y), -(cosA*N.z+sinA*T.z)};

        /* Initial velocity orientation if specified */
        if(sink->initialSpeed>0.0f) {
            float velRad=sink->trailVelocityAngleDeg*(3.14159265f/180.0f);
            if(sink->trailAlign==1 && sink->trailVelocityAngleDeg==0.0f) velRad=0.0f;
            else if(sink->trailAlign==2 && sink->trailVelocityAngleDeg==0.0f) velRad=3.14159265f*0.5f;
            float cosV=cosf(velRad), sinV=sinf(velRad);
            Vector3 vDir=(Vector3){cosV*N.x+sinV*T.x, cosV*N.y+sinV*T.y, cosV*N.z+sinV*T.z};
            config.initialVelocity=(Vector3){vDir.x*sink->initialSpeed, vDir.y*sink->initialSpeed, vDir.z*sink->initialSpeed};
        }
    }
    return TrailRibbon_Spawn(&config)>=0;
}


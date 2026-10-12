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
        Vector3 N=(Vector3){0,1,0};
        /* Calculate radial vector from center to spawn position */
        Vector3 radDiff=(Vector3){spawn->sample.position.x - sink->center.x,
                                  spawn->sample.position.y - sink->center.y,
                                  spawn->sample.position.z - sink->center.z};
        float radLen=sqrtf(radDiff.x*radDiff.x + radDiff.y*radDiff.y + radDiff.z*radDiff.z);
        if(radLen>1e-4f) {
            N=(Vector3){radDiff.x/radLen, radDiff.y/radLen, radDiff.z/radLen};
        } else {
            Vector3 sN=spawn->sample.normal;
            float sLen=sqrtf(sN.x*sN.x + sN.y*sN.y + sN.z*sN.z);
            if(sLen>1e-4f) N=(Vector3){sN.x/sLen, sN.y/sLen, sN.z/sLen};
        }

        /* Form an orthonormal tangent vector T perpendicular to N */
        Vector3 up=(fabsf(N.y)<0.85f)?(Vector3){0,1,0}:(Vector3){1,0,0};
        Vector3 T=(Vector3){up.y*N.z-up.z*N.y, up.z*N.x-up.x*N.z, up.x*N.y-up.y*N.x};
        float tLen=sqrtf(T.x*T.x+T.y*T.y+T.z*T.z);
        if(tLen>1e-4f) { T.x/=tLen; T.y/=tLen; T.z/=tLen; }
        else T=(Vector3){1,0,0};

        float alignRad=0.0f;
        if(sink->trailAlign==1) alignRad=0.0f; /* RADIAL: 0 deg, parallel to radial normal N */
        else if(sink->trailAlign==2) alignRad=3.14159265f*0.5f; /* SURFACE: 90 deg, tangent T */
        else if(sink->trailAlign==3) alignRad=sink->trailAlignAngleDeg*(3.14159265f/180.0f);

        /* Trail tail direction: points backward from head motion.
         * For outward motion (or positive initialSpeed), tail points backward toward center.
         * For inward motion (negative initialSpeed < 0), tail points outward away from center. */
        float cosA=cosf(alignRad), sinA=sinf(alignRad);
        float tailSign=(sink->initialSpeed<0.0f)?1.0f:-1.0f;
        config.tailDirection=(Vector3){tailSign*(cosA*N.x+sinA*T.x), tailSign*(cosA*N.y+sinA*T.y), tailSign*(cosA*N.z+sinA*T.z)};

        /* Initial velocity orientation */
        if(fabsf(sink->initialSpeed)>1e-4f) {
            float velRad=0.0f;
            if(sink->trailAlign==1) velRad=0.0f; /* RADIAL velocity */
            else if(sink->trailAlign==2) velRad=sink->trailVelocityAngleDeg*(3.14159265f/180.0f);
            else if(sink->trailAlign==3) velRad=sink->trailVelocityAngleDeg*(3.14159265f/180.0f);
            float cosV=cosf(velRad), sinV=sinf(velRad);
            Vector3 vDir=(Vector3){cosV*N.x+sinV*T.x, cosV*N.y+sinV*T.y, cosV*N.z+sinV*T.z};
            config.initialVelocity=(Vector3){vDir.x*sink->initialSpeed, vDir.y*sink->initialSpeed, vDir.z*sink->initialSpeed};
        }
    }
    return TrailRibbon_Spawn(&config)>=0;
}


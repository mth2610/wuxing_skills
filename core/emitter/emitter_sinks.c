#include "core/emitter/emitter_sinks.h"
bool EmissionSink_Particle(void *user,const EmissionSpawn *spawn) {
    EmissionParticleSink *sink=user;
    if(!sink||!spawn||!spawn->spawnTemplate||
       (spawn->kind!=EMISSION_PARTICLE&&spawn->kind!=EMISSION_MESH)||
       ParticleManager_GetEmitterStatus(sink->emitter)!=PARTICLE_EMITTER_OK) return false;
    ParticleConfig particle=*(const ParticleConfig *)spawn->spawnTemplate;
    particle.position=particle.physics.position=spawn->sample.position;
    bool hasRad = fabsf(sink->initialSpeed) > 1e-4f;
    bool hasSwirl = fabsf(sink->swirlSpeed) > 1e-4f;
    if(hasRad || hasSwirl) {
        Vector3 radDiff=(Vector3){spawn->sample.position.x - sink->center.x,
                                  spawn->sample.position.y - sink->center.y,
                                  spawn->sample.position.z - sink->center.z};
        float radLen=sqrtf(radDiff.x*radDiff.x + radDiff.y*radDiff.y + radDiff.z*radDiff.z);
        Vector3 N=(Vector3){0,1,0};
        if(radLen>1e-4f) {
            N=(Vector3){radDiff.x/radLen, radDiff.y/radLen, radDiff.z/radLen};
        } else {
            Vector3 sN=spawn->sample.normal;
            float sLen=sqrtf(sN.x*sN.x + sN.y*sN.y + sN.z*sN.z);
            if(sLen>1e-4f) N=(Vector3){sN.x/sLen, sN.y/sLen, sN.z/sLen};
        }
        Vector3 vel = (Vector3){N.x*sink->initialSpeed, N.y*sink->initialSpeed, N.z*sink->initialSpeed};
        if(hasSwirl) {
            Vector3 axis = sink->swirlAxis;
            float axLen = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
            if(axLen > 1e-4f) {
                axis = (Vector3){axis.x/axLen, axis.y/axLen, axis.z/axLen};
                /* Tangential direction along swirl axis: axis x N */
                Vector3 swirlT = (Vector3){axis.y*N.z - axis.z*N.y,
                                           axis.z*N.x - axis.x*N.z,
                                           axis.x*N.y - axis.y*N.x};
                float stLen = sqrtf(swirlT.x*swirlT.x + swirlT.y*swirlT.y + swirlT.z*swirlT.z);
                if(stLen > 1e-4f) {
                    swirlT = (Vector3){swirlT.x/stLen, swirlT.y/stLen, swirlT.z/stLen};
                    vel.x += swirlT.x * sink->swirlSpeed;
                    vel.y += swirlT.y * sink->swirlSpeed;
                    vel.z += swirlT.z * sink->swirlSpeed;
                }
            }
        }
        particle.velocity=particle.physics.velocity=vel;
    }
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

        /* Determine tangential reference vector T */
        Vector3 T=(Vector3){0,0,0};
        bool hasSwirl = fabsf(sink->swirlSpeed) > 1e-4f;
        if(hasSwirl) {
            Vector3 axis = sink->swirlAxis;
            float axLen = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
            if(axLen > 1e-4f) {
                axis = (Vector3){axis.x/axLen, axis.y/axLen, axis.z/axLen};
                /* Swirl tangential direction: axis x N */
                T = (Vector3){axis.y*N.z - axis.z*N.y,
                              axis.z*N.x - axis.x*N.z,
                              axis.x*N.y - axis.y*N.x};
                float tLen = sqrtf(T.x*T.x + T.y*T.y + T.z*T.z);
                if(tLen > 1e-4f) { T.x/=tLen; T.y/=tLen; T.z/=tLen; }
                else T = (Vector3){0,0,0};
            }
        }
        if(T.x==0.0f && T.y==0.0f && T.z==0.0f) {
            Vector3 up=(fabsf(N.y)<0.85f)?(Vector3){0,1,0}:(Vector3){1,0,0};
            T=(Vector3){up.y*N.z-up.z*N.y, up.z*N.x-up.x*N.z, up.x*N.y-up.y*N.x};
            float tLen=sqrtf(T.x*T.x+T.y*T.y+T.z*T.z);
            if(tLen>1e-4f) { T.x/=tLen; T.y/=tLen; T.z/=tLen; }
            else T=(Vector3){1,0,0};
        }

        /* When swirl is present, the natural stream tangent incorporates swirl speed.
         * For RADIAL (align=1), automatically angle the ribbon spine and velocity along the spiral streamline. */
        float alignRad=0.0f;
        float velRad=0.0f;
        bool autoSpiral = hasSwirl && (sink->trailAlign==1);

        if(autoSpiral) {
            /* Angle between radial direction and spiral streamline */
            float speedMag = fabsf(sink->initialSpeed);
            if(speedMag > 1e-4f) {
                float spiralAngle = atan2f(sink->swirlSpeed, speedMag);
                alignRad = spiralAngle;
                velRad = spiralAngle;
            } else {
                alignRad = 3.14159265f*0.5f;
                velRad = 3.14159265f*0.5f;
            }
        } else {
            if(sink->trailAlign==1) alignRad=0.0f; /* RADIAL: 0 deg, parallel to radial normal N */
            else if(sink->trailAlign==2) alignRad=3.14159265f*0.5f; /* SURFACE: 90 deg, tangent T */
            else if(sink->trailAlign==3) alignRad=sink->trailAlignAngleDeg*(3.14159265f/180.0f);

            if(sink->trailAlign==1) velRad=0.0f; /* RADIAL velocity */
            else if(sink->trailAlign==2) velRad=sink->trailVelocityAngleDeg*(3.14159265f/180.0f);
            else if(sink->trailAlign==3) velRad=sink->trailVelocityAngleDeg*(3.14159265f/180.0f);
        }

        /* Trail tail direction: points backward from head motion.
         * For outward motion (or positive initialSpeed), tail points backward toward center.
         * For inward motion (negative initialSpeed < 0), tail points outward away from center. */
        float cosA=cosf(alignRad), sinA=sinf(alignRad);
        float tailSign=(sink->initialSpeed<0.0f)?1.0f:-1.0f;
        config.tailDirection=(Vector3){tailSign*(cosA*N.x+sinA*T.x), tailSign*(cosA*N.y+sinA*T.y), tailSign*(cosA*N.z+sinA*T.z)};

        /* Initial velocity orientation */
        bool hasRadVel = fabsf(sink->initialSpeed)>1e-4f;
        if(hasRadVel || hasSwirl) {
            float cosV=cosf(velRad), sinV=sinf(velRad);
            if(autoSpiral) {
                /* Direct vector blend of radial velocity and tangential swirl velocity */
                Vector3 v = (Vector3){N.x*sink->initialSpeed + T.x*sink->swirlSpeed,
                                      N.y*sink->initialSpeed + T.y*sink->swirlSpeed,
                                      N.z*sink->initialSpeed + T.z*sink->swirlSpeed};
                config.initialVelocity = v;
                /* Align tail direction strictly opposite to birth velocity so trail is born smooth */
                float vLen = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
                if(vLen > 1e-4f) {
                    config.tailDirection = (Vector3){-v.x/vLen, -v.y/vLen, -v.z/vLen};
                }
            } else {
                Vector3 vDir=(Vector3){cosV*N.x+sinV*T.x, cosV*N.y+sinV*T.y, cosV*N.z+sinV*T.z};
                config.initialVelocity=(Vector3){vDir.x*sink->initialSpeed, vDir.y*sink->initialSpeed, vDir.z*sink->initialSpeed};
                if(hasSwirl) {
                    config.initialVelocity.x += T.x * sink->swirlSpeed;
                    config.initialVelocity.y += T.y * sink->swirlSpeed;
                    config.initialVelocity.z += T.z * sink->swirlSpeed;
                }
            }
        }
    }
    return TrailRibbon_Spawn(&config)>=0;
}


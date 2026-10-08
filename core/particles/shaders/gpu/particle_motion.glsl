// Compatibility adapter for the legacy particle compute entry point.
#include "core/motion/shaders/motion_fields.glsl"
float fieldNoiseScalar(vec3 p) { return motionPhysicalNoise?motionPerlin(p):noiseScalar(p); }
vec3 fieldCurlNoise(vec3 p) { return motionPhysicalNoise?mLegacyCurl(p):curlNoise3(p); }
void motionIntegrate(uint idx,inout GpuParticleData particle) {
    MotionGpuBody body=motionBodies[idx];
    vec3 pos=particle.pos_radius.xyz,vel=particle.vel_drag.xyz;
    float remaining=min(u_dt,.25);
    while(remaining>1e-6) {
        float offset=-remaining,dt=min(remaining,1.0/120.0),time=u_time+offset;remaining-=dt;
        float viscosity=0.0;
        vec3 externalAcceleration=evalForceField(int(particle.ff_data.x),pos,vel,time,viscosity);
        motionAdvanceStep(pos,vel,body,evalMotionBackgroundWind(pos,time),
            externalAcceleration,viscosity,time,dt,offset);
    }
    particle.pos_radius.xyz=pos;particle.vel_drag.xyz=vel;motionBodies[idx]=body;
}

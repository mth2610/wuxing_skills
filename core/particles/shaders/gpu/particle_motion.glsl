// Compatibility adapter for the legacy particle compute entry point.
#include "core/motion/shaders/motion_fields.glsl"
float fieldNoiseScalar(vec3 p) { return motionPhysicalNoise?motionPerlin(p):noiseScalar(p); }
vec3 fieldCurlNoise(vec3 p) { return motionPhysicalNoise?mLegacyCurl(p):curlNoise3(p); }
void motionIntegrate(uint idx,inout GpuParticleData particle) {
    MotionGpuBody body=motionBodies[idx];
    vec3 pos=particle.pos_radius.xyz,vel=particle.vel_drag.xyz;
    float dt=min(u_dt,1.0/30.0);
    float viscosity=0.0;
    vec3 externalAcceleration=vec3(0.0);
    int ffIdx=int(particle.ff_data.x);
    if(ffIdx>=0) externalAcceleration=evalForceField(ffIdx,pos,vel,u_time,viscosity);
    motionAdvanceStep(pos,vel,body,evalMotionBackgroundWind(pos,u_time),
        externalAcceleration,viscosity,u_time,dt,0.0);
    particle.pos_radius.xyz=pos;particle.vel_drag.xyz=vel;motionBodies[idx]=body;
}

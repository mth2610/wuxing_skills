#version 430 core
struct FluidParticle {
    vec4 position_radius;
    vec4 velocity_life;
    vec4 phase_seed_impact_pad;
};
layout(std430,binding=0) readonly buffer FluidState { FluidParticle particles[]; };
in vec3 vertexPosition;
uniform mat4 u_view,u_projection;
out vec3 v_centerView; out vec2 v_corner; out float v_radius; out float v_life;
void main(){
    FluidParticle p=particles[gl_InstanceID];
    v_centerView=(u_view*vec4(p.position_radius.xyz,1.0)).xyz;
    /* Airborne particles need generous optical overlap.  Once the heavy body
     * touches the receiver, a slightly tighter kernel exposes its irregular
     * particle boundary instead of closing it into one perfect SSF disc. */
    bool settledBody=p.phase_seed_impact_pad.x<0.5
                  && p.phase_seed_impact_pad.z>0.5;
    float visualScale=settledBody?1.72:2.10;
    v_radius=p.position_radius.w*visualScale;
    v_life=p.velocity_life.w;
    v_corner=vertexPosition.xy;
    gl_Position=u_projection*vec4(v_centerView+vec3(v_corner*v_radius,0.0),1.0);
}

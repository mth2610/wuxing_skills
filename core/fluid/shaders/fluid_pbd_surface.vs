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
    /* A reconstructed surface needs overlapping supports. The former 1.72
     * settled scale exposed every sphere to the lighting pass, so the result
     * still read as shaded beads instead of one liquid sheet. */
    float visualScale=settledBody?2.02:2.10;
    /* Equal-radius kernels on one receiver plane expose their packing period
     * through SSF normals. Stable per-particle optical radius jitter removes
     * that correlation without injecting simulation energy. */
    float opticalScale=mix(0.90,1.10,p.phase_seed_impact_pad.w);
    v_radius=p.position_radius.w*visualScale*opticalScale;
    v_life=p.velocity_life.w;
    v_corner=vertexPosition.xy;
    gl_Position=u_projection*vec4(v_centerView+vec3(v_corner*v_radius,0.0),1.0);
}

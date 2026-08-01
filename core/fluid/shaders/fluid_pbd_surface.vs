#version 430 core
struct FluidParticle { vec4 position_radius; vec4 velocity_life; };
layout(std430,binding=0) readonly buffer FluidState { FluidParticle particles[]; };
in vec3 vertexPosition;
uniform mat4 u_view,u_projection;
out vec3 v_centerView; out vec2 v_corner; out float v_radius;
void main(){ FluidParticle p=particles[gl_InstanceID]; v_centerView=(u_view*vec4(p.position_radius.xyz,1.0)).xyz; v_radius=p.position_radius.w; v_corner=vertexPosition.xy; gl_Position=u_projection*vec4(v_centerView+vec3(v_corner*v_radius,0.0),1.0); }

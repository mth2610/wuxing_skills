#version 430 core
// Surface-input vertex stage. This is deliberately not the regular billboard
// vertex shader: the fragment stage reconstructs an analytic sphere from the
// view-space centre/radius, producing a curved depth field for SSF.
struct GpuParticleData {
    vec4 pos_radius;
    vec4 vel_drag;
    vec4 color_start;
    vec4 color_end;
    vec4 life_data;
    vec4 ff_data;
    vec4 route_data;
};
layout(std430, binding = 0) readonly buffer ParticleBuffer { GpuParticleData particles[]; };

in vec3 vertexPosition;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_filterEmitter;
uniform float u_filterRenderMode;
out vec3 v_centerView;
out vec2 v_corner;
out float v_radius;

void main() {
    vec4 life = particles[gl_InstanceID].life_data;
    vec4 route = particles[gl_InstanceID].route_data;
    if (life.w < 0.5 || life.y <= 0.0 ||
        (u_filterEmitter >= 0.0 && abs(route.x - u_filterEmitter) > 0.25) ||
        (u_filterRenderMode >= 0.0 && abs(route.y - u_filterRenderMode) > 0.25)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_centerView = vec3(0.0); v_corner = vec2(0.0); v_radius = 0.0;
        return;
    }
    vec4 pr = particles[gl_InstanceID].pos_radius;
    v_centerView = (u_view * vec4(pr.xyz, 1.0)).xyz;
    v_radius = pr.w;
    v_corner = vertexPosition.xy;
    // Conservative screen quad which encloses the view-facing sphere.
    gl_Position = u_projection * vec4(v_centerView + vec3(v_corner * v_radius, 0.0), 1.0);
}

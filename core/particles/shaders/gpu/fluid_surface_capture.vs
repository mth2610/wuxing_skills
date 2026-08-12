#version 430 core
// Surface-input vertex stage. This is deliberately not the regular billboard
// vertex shader: the fragment stage reconstructs an analytic ellipsoid from the
// view-space centre and the axes built here, producing a curved depth field for
// SSF.
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
/* The splat is an ELLIPSOID now, so one radius no longer describes it: the
 * fragment stage needs the actual view-plane offset (which the rasterizer
 * interpolates for it) and the depth semi-axis separately. */
out vec2 v_offsetView;
out float v_depthRadius;
/* Both fragment stages this vertex shader is paired with (fluid_capture_particle.fs,
 * fluid_capture_particle_back.fs) open with `if (v_life <= 0.0) discard;`. Without
 * this output that input has no producer: GL leaves it undefined, and under rlvk it
 * is demoted to a Private variable — a zero read there discards the entire fluid
 * capture, so the SSF surface silently loses every GPU-backend particle.
 * life_data.x is the remaining life (see particle_gpu.comp), matching
 * fluid_pbd_surface.vs's v_life. */
out float v_life;

/* Isotropic splats render a thin film or a rim as a string of beads, because
 * each particle contributes a circle and the circles do not touch along the
 * direction the sheet runs in. The published fix (Yu & Turk 2013) fits an
 * anisotropic kernel by PCA over each particle's neighbours; this path has no
 * neighbour search by design, so it uses the direction the particle is MOVING as
 * a proxy for the direction its neighbourhood is stretched in — which is what a
 * coherent sheet or rim actually does.
 *
 * The proxy is honest about what it is: the aspect ratio cannot be measured
 * without neighbours, so it is stated instead. One reference speed of motion
 * buys one extra unit of aspect, and the ratio is capped — an unbounded aspect
 * turns splats into streaks and reopens the coverage question that
 * core/tests/water_ring_coverage_test.c guards.
 *
 * The two cross-axes shrink by 1/sqrt(aspect) so the ellipsoid's volume is
 * unchanged (aspect * (1/sqrt(aspect))^2 == 1) — that determinant normalization
 * IS from Yu & Turk, and it is what stops the surface swelling wherever the
 * fluid happens to be moving fast. */
#define FLUID_ANISO_REFERENCE_SPEED 3.0
#define FLUID_ANISO_MAX_ASPECT 3.0

void main() {
    vec4 life = particles[gl_InstanceID].life_data;
    vec4 route = particles[gl_InstanceID].route_data;
    if (life.w < 0.5 || life.y <= 0.0 ||
        (u_filterEmitter >= 0.0 && abs(route.x - u_filterEmitter) > 0.25) ||
        (u_filterRenderMode >= 0.0 && abs(route.y - u_filterRenderMode) > 0.25)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_centerView = vec3(0.0); v_corner = vec2(0.0);
        v_offsetView = vec2(0.0); v_depthRadius = 0.0; v_life = 0.0;
        return;
    }
    vec4 pr = particles[gl_InstanceID].pos_radius;
    v_centerView = (u_view * vec4(pr.xyz, 1.0)).xyz;
    v_corner = vertexPosition.xy;
    v_life = life.x;

    // Velocity in the view plane: the same space the quad is built in.
    vec3 velocityView = mat3(u_view) * particles[gl_InstanceID].vel_drag.xyz;
    float speed = length(velocityView.xy);
    float aspect = clamp(1.0 + speed / FLUID_ANISO_REFERENCE_SPEED,
                         1.0, FLUID_ANISO_MAX_ASPECT);
    float cross = inversesqrt(aspect);
    vec2 axis = speed > 1e-4 ? velocityView.xy / speed : vec2(1.0, 0.0);
    vec2 perpendicular = vec2(-axis.y, axis.x);

    v_offsetView = (axis * (v_corner.x * aspect) + perpendicular * (v_corner.y * cross))
                 * pr.w;
    v_depthRadius = pr.w * cross;
    // The quad is the ellipse's own bounding parallelogram, so it stays tight.
    gl_Position = u_projection * vec4(v_centerView + vec3(v_offsetView, 0.0), 1.0);
}

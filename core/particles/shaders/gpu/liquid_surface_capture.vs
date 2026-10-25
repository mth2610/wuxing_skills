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
    vec4 route_data;     // x=emitter id, y=render mode, z=travel slot, w=waypoint
    vec4 impact_data;
    vec4 formation_data;
};
layout(std430, binding = 0) readonly buffer ParticleBuffer { GpuParticleData particles[]; };
struct SurfaceIndex { uint particleIndex; float materialId; };
layout(std430, binding = 1) readonly buffer SurfaceIndexBuffer { SurfaceIndex surfaceIndices[]; };

in vec3 vertexPosition;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_filterEmitter;
uniform float u_filterRenderMode;
uniform int u_surfaceIndexed;
uniform float u_materialId;
flat out float v_materialId;
out vec3 v_centerView;
out vec2 v_corner;
/* The splat is an ELLIPSOID now, so one radius no longer describes it: the
 * fragment stage needs the actual view-plane offset (which the rasterizer
 * interpolates for it) and the depth semi-axis separately. */
out vec2 v_offsetView;
out float v_depthRadius;
/* Both fragment stages this vertex shader is paired with (liquid_capture_particle.fs,
 * liquid_capture_particle_back.fs) open with `if (v_life <= 0.0) discard;`.
 * life_data.x is the remaining life (see particle_gpu.comp), matching
 * liquid_pbd_surface.vs's v_life. */
out float v_life;

#define LIQUID_ANISO_REFERENCE_SPEED 3.0
#define LIQUID_ANISO_INV_REF_SPEED (1.0 / 3.0)
#define LIQUID_ANISO_MAX_ASPECT 3.0

void main() {
    uint particleIndex = uint(gl_InstanceID);
    v_materialId = u_materialId;
    if (u_surfaceIndexed != 0) {
        particleIndex = surfaceIndices[gl_InstanceID].particleIndex;
        v_materialId = surfaceIndices[gl_InstanceID].materialId;
    }

    vec4 life = particles[particleIndex].life_data;
    vec4 route = particles[particleIndex].route_data;
    vec4 pr = particles[particleIndex].pos_radius;

    // Early vertex-stage rejection: also reject expired (life.x <= 0.0) or
    // degenerate radius (pr.w <= 1e-5) particles so the rasterizer never runs.
    if (life.w < 0.5 || life.x <= 0.0 || life.y <= 0.0 || pr.w <= 1e-5 ||
        (u_filterEmitter >= 0.0 && abs(route.x - u_filterEmitter) > 0.25) ||
        (u_filterRenderMode >= 0.0 && abs(route.y - u_filterRenderMode) > 0.25)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_centerView = vec3(0.0);
        v_corner = vec2(0.0);
        v_offsetView = vec2(0.0);
        v_depthRadius = 0.0;
        v_life = 0.0;
        return;
    }

    v_centerView = (u_view * vec4(pr.xyz, 1.0)).xyz;
    v_corner = vertexPosition.xy;
    v_life = life.x;

    // Velocity in the view plane: use inversesqrt(speedSq) to avoid separate sqrt + div.
    // Renamed `cross` -> `crossScale` to avoid shadowing GLSL built-in `cross()`.
    vec3 velocityView = mat3(u_view) * particles[particleIndex].vel_drag.xyz;
    float speedSq = dot(velocityView.xy, velocityView.xy);
    float invSpeed = (speedSq > 1e-8) ? inversesqrt(speedSq) : 0.0;
    float speed = speedSq * invSpeed;

    float aspect = clamp(1.0 + speed * LIQUID_ANISO_INV_REF_SPEED,
                         1.0, LIQUID_ANISO_MAX_ASPECT);
    float crossScale = inversesqrt(aspect);
    vec2 axis = (speedSq > 1e-8) ? (velocityView.xy * invSpeed) : vec2(1.0, 0.0);
    vec2 perpendicular = vec2(-axis.y, axis.x);

    v_offsetView = (axis * (v_corner.x * aspect) + perpendicular * (v_corner.y * crossScale))
                 * pr.w;
    v_depthRadius = pr.w * crossScale;
    // The quad is the ellipse's own bounding parallelogram, so it stays tight.
    gl_Position = u_projection * vec4(v_centerView + vec3(v_offsetView, 0.0), 1.0);
}

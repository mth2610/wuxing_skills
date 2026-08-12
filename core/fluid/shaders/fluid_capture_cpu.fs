#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform mat4 u_projection;
// u_capture_params: xy = view-space center XY, z = view Z (negative), w = radius
uniform vec4 u_capture_params;
/* Liquid-table slot of the emitter being rasterized; see fluid_capture.fs. */
uniform float u_materialId;

void main() {
    // fragTexCoord is [0,1] from the rlBegin quad. Convert to [-1,+1] local UV.
    vec2 q = fragTexCoord * 2.0 - 1.0;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;

    float radius = u_capture_params.w;
    float viewCX = u_capture_params.x;
    float viewCY = u_capture_params.y;
    float viewCZ = u_capture_params.z; // negative (camera looks -Z)

    // Reconstruct sphere surface in view space.
    // Smooth profile: flatten the Z near the edge to avoid sharp depth jumps
    // between overlapping splats (same formula as fluid_capture_particle.fs).
    float sphereZ = sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10);
    vec3 surfaceView = vec3(viewCX + q.x * radius,
                            viewCY + q.y * radius,
                            viewCZ + sphereZ * radius);
    vec4 clip = u_projection * vec4(surfaceView, 1.0);
    float depth = clip.z / clip.w * 0.5 + 0.5;
    gl_FragDepth = depth;
    float coverage = smoothstep(1.0, 0.75, r2);
    finalColor = vec4(depth, coverage, u_materialId, 1.0);
}

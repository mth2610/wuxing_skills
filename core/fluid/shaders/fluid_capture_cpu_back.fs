#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform mat4 u_projection;
// u_capture_params: xy = view-space center XY, z = view Z (negative), w = radius
uniform vec4 u_capture_params;

// CPU-fallback twin of fluid_capture_particle_back.fs: same far root, same
// complement-depth MAX reduction. See that file for why the depth is inverted.
void main() {
    vec2 q = fragTexCoord * 2.0 - 1.0;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;

    float radius = u_capture_params.w;
    float sphereZ = sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10);
    vec3 surfaceView = vec3(u_capture_params.x + q.x * radius,
                            u_capture_params.y + q.y * radius,
                            u_capture_params.z - sphereZ * radius);
    if (surfaceView.z > -0.001) discard;
    vec4 clip = u_projection * vec4(surfaceView, 1.0);
    float depth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    gl_FragDepth = 1.0 - depth;
    finalColor = vec4(depth, 1.0, 0.0, 1.0);
}

#version 330
// CPU fallback thickness: reconstruct sphere chord from texcoord-based UV.
// Receives the same rlBegin quad layout as the capture shader.
in vec2 fragTexCoord;
out vec4 finalColor;

uniform vec4 u_capture_params; // xy = center XY view, z = view Z, w = radius

void main() {
    vec2 q = fragTexCoord * 2.0 - 1.0;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;
    float radius = u_capture_params.w;
    float pathLength = 2.0 * radius * sqrt(max(0.0, 1.0 - r2));
    finalColor = vec4(pathLength * 16.0, 0.0, 0.0, 1.0);
}

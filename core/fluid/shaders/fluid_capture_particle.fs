#version 330
in vec3 v_centerView;
in vec2 v_corner;
in float v_radius;
out vec4 finalColor;
uniform mat4 u_projection;

// GPU surface particles are billboards. Produce a filled circular splat here,
// independent of the artist texture and its alpha convention.
void main() {
    vec2 q = v_corner;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;
    // Camera looks down -Z in view space; +Z is the visible hemisphere.
    vec3 surfaceView = v_centerView + vec3(q * v_radius,
                                            sqrt(max(0.0, 1.0 - r2)) * v_radius);
    vec4 clip = u_projection * vec4(surfaceView, 1.0);
    float depth = clip.z / clip.w * 0.5 + 0.5;
    gl_FragDepth = depth;
    float coverage = smoothstep(1.0, 0.82, r2);
    finalColor = vec4(depth, coverage, 0.0, 1.0);
}

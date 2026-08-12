#version 330
in vec3 v_centerView;
in vec2 v_corner;
in vec2 v_offsetView;
in float v_depthRadius;
in float v_life;
out vec4 finalColor;
uniform mat4 u_projection;

// GPU surface particles are billboards. Produce a filled elliptical splat here,
// independent of the artist texture and its alpha convention.
void main() {
    if (v_life <= 0.0) discard;
    vec2 q = v_corner;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;
    // Camera looks down -Z in view space; +Z is the visible hemisphere.
    // Use a smooth depth profile that rounds near particle centers and blends
    // softly near boundaries to avoid sharp depth step creases between overlapping splats.
    float sphereZ = sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10);
    /* v_corner stays the UNIT-disc coordinate, so the coverage test and the
     * profile above are unchanged by anisotropy; the ellipsoid enters through
     * the view-plane offset the vertex stage already stretched and through the
     * separate depth semi-axis. Stretching the quad without stretching this
     * reconstruction would draw a clipped circle, not an ellipsoid. */
    vec3 surfaceView = v_centerView + vec3(v_offsetView, sphereZ * v_depthRadius);
    vec4 clip = u_projection * vec4(surfaceView, 1.0);
    float depth = clip.z / clip.w * 0.5 + 0.5;
    gl_FragDepth = depth;
    float coverage = smoothstep(1.0, 0.75, r2);
    finalColor = vec4(depth, coverage, 0.0, 1.0);
}

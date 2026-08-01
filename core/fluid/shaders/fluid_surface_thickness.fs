#version 330
in vec2 v_corner;
in float v_radius;
out vec4 finalColor;

// Optical path length through a sphere, accumulated additively over all
// splats. The scale maps metres to a compact Beer-Lambert thickness range.
void main() {
    float r2 = dot(v_corner, v_corner);
    if (r2 >= 1.0) discard;
    float pathLength = 2.0 * v_radius * sqrt(max(0.0, 1.0 - r2));
    finalColor = vec4(pathLength * 1.8, 0.0, 0.0, 1.0);
}

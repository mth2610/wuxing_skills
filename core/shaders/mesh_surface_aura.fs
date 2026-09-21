#version 330
#include "core/shaders/common/fs_header.glsl"
uniform vec4 u_materialColor;
uniform float u_rimWidth;
uniform float u_rimIntensity;
uniform float u_opacity;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 v = normalize(-fragPosition);
    float rim = pow(clamp(1.0 - max(dot(n, v), 0.0), 0.0, 1.0), max(u_rimWidth, 0.05));
    float alpha = smoothstep(0.08, 0.92, rim) * clamp(u_opacity, 0.0, 1.0);
    finalColor = vec4(u_materialColor.rgb * (0.30 + rim * u_rimIntensity), alpha);
}

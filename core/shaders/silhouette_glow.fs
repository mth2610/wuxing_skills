#version 330
#include "core/shaders/common/fs_header.glsl"

uniform vec4  u_glowColor;
uniform float u_rimPower;
uniform float u_rimStrength;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragPosition);
    float NdotV = clamp(abs(dot(N, V)), 0.0, 1.0);
    float rim = pow(1.0 - NdotV, u_rimPower) * u_rimStrength;

    // Core incandescent body: 100% pure glowing emission + outer fresnel rim
    vec3 rgb = u_glowColor.rgb * (1.0 + rim * 1.5);
    finalColor = vec4(rgb, u_glowColor.a);
}

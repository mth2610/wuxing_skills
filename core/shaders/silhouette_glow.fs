#version 330
#include "core/shaders/common/fs_header.glsl"

uniform vec4  u_glowColor;
uniform float u_rimPower;
uniform float u_rimStrength;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragPosition);
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float rim = pow(1.0 - NdotV, u_rimPower) * u_rimStrength;

    // A mesh aura is a contour, not a second opaque blue character. Coverage
    // comes exclusively from the Fresnel rim so the original material remains
    // legible and the Plasma Wisp layer supplies the interior motion.
    float coverage = smoothstep(0.05, 0.72, rim) * u_glowColor.a;
    vec3 rgb = u_glowColor.rgb * (0.35 + rim * 0.65);
    finalColor = vec4(rgb, coverage);
}

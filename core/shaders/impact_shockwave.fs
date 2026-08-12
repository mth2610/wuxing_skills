#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform float u_opacity;
uniform float u_emission;

void main()
{
    float angle = fragTexCoord.x * 6.28318530718;
    float band = fragTexCoord.y;
    // Circular domain makes the moving breakup continuous at U=0/1.
    vec3 domain = vec3(cos(angle) * 2.1, sin(angle) * 2.1,
                       band * 5.0 - u_time * 0.65);
    float breakup = fbm3(domain);
    float threads = smoothstep(0.50, 0.82, 1.0 - abs(breakup * 2.0 - 1.0));
    float lens = sin(3.1415926535 * clamp(band, 0.0, 1.0));
    float coverage = lens * mix(0.48, 1.0, threads);
    vec3 color = mix(u_bodyColor.rgb, u_glowColor.rgb, threads * 0.64);
    finalColor = vec4(color * u_emission, coverage * u_opacity * u_bodyColor.a);
}

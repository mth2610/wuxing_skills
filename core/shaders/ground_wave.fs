#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform float u_opacity;
uniform float u_surfaceIntensity;

void main()
{
    float u = fragTexCoord.x;
    float v = fragTexCoord.y;

    // `u` wraps at the annulus seam. Sampling through cos/sin makes the noise
    // domain continuous there, unlike raw UV noise which creates one obvious
    // vertical scar at U=0/1.
    float angle = u * 6.28318530718;
    vec3 flowDomain = vec3(cos(angle) * 1.9, sin(angle) * 1.9,
                           v * 5.0 - u_time * 0.55);
    float flow = fbm3(flowDomain);
    float filaments = smoothstep(0.54, 0.80, 1.0 - abs(flow * 2.0 - 1.0));

    // Same leading crest semantics as the geometry: the mesh profile creates
    // the height, this profile only determines surface coverage and colour.
    float profile = sin(3.1415926535 * pow(clamp(v, 0.0, 1.0), 1.7095));
    float crest = smoothstep(0.48, 0.92, profile);
    float coverage = profile * mix(0.52, 1.0, filaments);
    vec3 color = mix(u_bodyColor.rgb, u_glowColor.rgb,
                     clamp(crest * 0.72 + filaments * 0.28, 0.0, 1.0));

    finalColor = vec4(color * u_surfaceIntensity,
                      coverage * u_opacity * u_bodyColor.a);
}

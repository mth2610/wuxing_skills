#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform vec4 u_hotColor;
uniform float u_intensity;

void main()
{
    vec2 p = (fragTexCoord - vec2(0.5)) * 2.0;
    float r = length(p);
    if (r >= 1.0) discard;

    float theta = atan(p.y, p.x);
    // Static, fine lines rather than a rotating lens flare. `fwidth` widens
    // the analytical line by its screen-space derivative, keeping the pattern
    // smooth at every distance instead of aliasing into hard triangular rays.
    float rayWave = abs(sin(theta * 16.0 + sin(theta * 5.0) * 0.25));
    float rayWidth = 0.022 + fwidth(rayWave) * 1.8;
    float rays = 1.0 - smoothstep(0.0, rayWidth, rayWave);
    float radialFade = pow(max(0.0, 1.0 - r), 2.40);
    float shimmer = rays * radialFade;
    float halo = pow(max(0.0, 1.0 - r * r), 2.35) * 0.34;
    float core = exp(-r * r * 18.0);
    float coverage = clamp(halo + shimmer * 0.14 + core * 0.50, 0.0, 1.0);
    vec3 emission = mix(u_glowColor.rgb, u_hotColor.rgb, core);
    float coreMask = (core * 0.95 + shimmer * 0.10) * mix(0.65, 1.0, u_intensity);
    finalColor = VFX_ResolvePremultiplied(u_bodyColor.rgb, 0.22, coverage,
                                          emission, coreMask,
                                          mix(1.4, 4.0, u_intensity));
}

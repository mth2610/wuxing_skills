#version 330
#ifdef GL_ES
precision highp float;
#endif

#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2  v_local;
in float v_radial;
in vec3  v_worldPos;

out vec4 finalColor;

uniform float u_time;
uniform float u_power;
uniform float u_implosion;
uniform vec3  u_lightDir;
uniform vec3  u_litColor;
uniform vec3  u_midColor;
uniform vec3  u_shadowColor;

void main()
{
    if (u_power <= 0.01) discard;

    float ang = atan(v_local.y, v_local.x);
    float spiralAng1 = ang * 3.0 - u_time * 3.4 + log(v_radial + 0.22) * 5.2;
    float spiralAng2 = ang * 5.0 - u_time * 5.2 + log(v_radial + 0.18) * 7.6;

    // Seamless circular polar noise sampling (zero radial seam at -PI/PI)
    vec2 p1 = vec2(cos(spiralAng1), sin(spiralAng1)) * (1.2 + v_radial * 2.4) + vec2(u_time * 0.4, -u_time * 0.3);
    vec2 p2 = vec2(cos(spiralAng2), sin(spiralAng2)) * (1.8 + v_radial * 3.6) - vec2(u_time * 0.6, u_time * 0.5);

    float s1 = fbm2(p1);
    float s2 = vnoise(p2);
    float flow = smoothstep(0.20, 0.84, s1 * 0.64 + s2 * 0.36);

    // Ultra-soft inward & outward Gaussian radial fade (zero flat sticker ring!)
    float radFade = smoothstep(0.0, 0.24, v_radial) * pow(1.0 - smoothstep(0.14, 1.0, v_radial), 1.9);
    float alpha = flow * radFade * (0.56 + 0.14 * clamp(u_implosion, 0.0, 1.0)) * clamp(u_power, 0.0, 1.18);
    if (alpha < 0.008) discard;

    // Macro directional sun shading across the domed dust bank
    vec3 macroN = normalize(vec3(v_local.x, 0.65, v_local.y));
    float ndotl = dot(macroN, normalize(u_lightDir)) * 0.5 + 0.5;
    vec3 col = mix(u_shadowColor, u_midColor, smoothstep(0.10, 0.60, ndotl));
    col = mix(col, u_litColor, smoothstep(0.48, 0.94, ndotl));
    // Darker heavy scoured soil right near vortex foot
    col = mix(u_shadowColor * 0.80, col, smoothstep(0.0, 0.45, v_radial));

    finalColor = VFX_ResolveBody(col, 1.0, clamp(alpha, 0.0, 0.72));
}

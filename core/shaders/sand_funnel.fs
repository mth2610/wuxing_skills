#version 330
#ifdef GL_ES
precision highp float;
#endif

#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2  v_uv;
in float v_h;
in vec3  v_worldPos;
in vec3  v_worldNormal;

out vec4 finalColor;

uniform float u_time;
uniform float u_power;
uniform float u_implosion;
uniform vec3  u_camPos;
uniform vec3  u_lightDir;
uniform vec3  u_litColor;
uniform vec3  u_midColor;
uniform vec3  u_shadowColor;
uniform vec3  u_rimColor;

uniform float u_uvScaleU;
uniform float u_uvScaleV;
uniform float u_scrollU;
uniform float u_scrollV;
uniform float u_shearSlope;
uniform float u_opacity;
uniform float u_erosionMin;
uniform float u_erosionMax;
uniform float u_fresnelPow;
uniform float u_coreShadowBoost;

// Seamless periodic silky anisotropic sand curtains (zero Worley bubbles, zero seam at u=0/1)
float sf_silkyCurtain(float uAngle, float vHeight, float tScrollU, float tScrollV, float freqU, float freqV)
{
    float ang = (uAngle - tScrollU) * 6.2831853;
    vec2 ring = vec2(cos(ang), sin(ang)) * (freqU * 0.55);
    float vCoord = vHeight * freqV - tScrollV;

    // Low-frequency wind shear domain warp
    float wx = vnoise(vec2(ring.x * 1.4 + ring.y * 0.8, vCoord * 0.45)) - 0.5;
    float wy = vnoise(vec2(ring.y * 1.4 - ring.x * 0.8 + 4.7, vCoord * 0.45 + 2.3)) - 0.5;

    float flow1 = fbm2(vec2(ring.x * 1.6 + wx * 0.9, vCoord * 1.35 + wy * 0.45));
    float flow2 = vnoise(vec2(ring.y * 2.8 - wx * 0.7 + 7.1, vCoord * 2.4 + wy * 0.35));
    return clamp(flow1 * 0.68 + flow2 * 0.32, 0.0, 1.0);
}

void main()
{
    if (u_power <= 0.005) discard;

    float shearU1 = v_uv.x * u_uvScaleU + v_h * u_shearSlope;
    float s1 = sf_silkyCurtain(shearU1, v_h, u_time * u_scrollU, u_time * u_scrollV, 2.2, u_uvScaleV * 2.8);
    float ang2 = ((v_uv.x * u_uvScaleU * 1.5 + v_h * u_shearSlope * 1.45) - u_time * u_scrollU * 1.42) * 6.2831853;
    float s2 = vnoise(vec2(cos(ang2) * 1.7, sin(ang2) * 1.7 + v_h * u_uvScaleV * 4.2 - u_time * u_scrollV * 1.32));

    float flow = s1 * 0.64 + s2 * 0.36;
    float curtain = smoothstep(u_erosionMin, u_erosionMax, flow);

    // Smooth Fresnel edge dissolution eliminates geometric cylinder silhouette
    vec3 N = normalize(v_worldNormal);
    vec3 V = normalize(u_camPos - v_worldPos);
    float ndotv = abs(dot(N, V));
    float fresnelDissolve = pow(clamp(ndotv, 0.0, 1.0), u_fresnelPow);

    // Smooth top & bottom atmospheric fades
    float vFade = smoothstep(0.0, 0.11, v_h) * (1.0 - smoothstep(0.68, 1.0, v_h));

    float alpha = curtain * fresnelDissolve * vFade * u_opacity * clamp(u_power, 0.0, 1.2);
    if (alpha < 0.008) discard;

    // Macro-volume directional sun shading (deep umber shadow -> warm midtone -> sunlit ochre)
    vec3 L = normalize(u_lightDir);
    float ndotl = dot(N, L);
    float litT = clamp(ndotl * 0.5 + 0.5, 0.0, 1.0);
    float shade = clamp(litT + (s1 - 0.5) * 0.18, 0.0, 1.0);

    vec3 col = mix(u_shadowColor, u_midColor, smoothstep(0.08, 0.56, shade));
    col = mix(col, u_litColor, smoothstep(0.46, 0.94, shade));

    // Heavy ground-base scoured soil darkening + Haboob implosion core darkening
    float baseEarthDark = (1.0 - smoothstep(0.0, 0.36, v_h)) * (0.30 * u_coreShadowBoost + u_implosion * 0.28);
    col = mix(col, u_shadowColor * 0.76, clamp(baseEarthDark, 0.0, 0.64));

    // Backlit golden Mie sun-rim translucency
    float fwdScatter = pow(max(dot(-V, L), 0.0), 3.4);
    float rimEdge = pow(1.0 - ndotv, 2.0);
    float rimFactor = (fwdScatter * 0.48 + rimEdge * 0.26 * smoothstep(-0.15, 0.8, ndotl)) * (0.45 + 0.55 * s2);
    col += u_rimColor * rimFactor * 0.50;

    finalColor = VFX_ResolveBody(col, 1.0, clamp(alpha, 0.0, 0.82));
}

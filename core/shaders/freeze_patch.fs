#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 v_worldPos;
in vec4 fragColor;

uniform vec3 u_center;
uniform float u_radius;
uniform float u_maxRadius;
uniform float u_alpha;
uniform float u_waterMask;
uniform vec3 u_iceColor;
uniform vec3 u_frostColor;
uniform vec3 u_lightColor;

void main() {
    vec2 p = fragTexCoord;
    float dist = length(p);

    // 1. Domain-warped dendritic frost tendrils for organic jagged perimeter
    float fbmLow = fbm2(p * 1.65 + vec2(3.7, -2.1));
    float fbmHigh = fbm2(p * 4.2 + vec2(-5.2, 1.9));
    vec2 q = p + vec2(fbmLow - 0.5, fbmHigh - 0.5) * 0.55;
    float dendrite = fbm2(q * 3.4);
    float frostGrain = vnoise(q * 9.5);

    float organicEdge = (dendrite - 0.44) * (0.48 * max(u_maxRadius, 0.5) + 0.22);
    float radialMask = 1.0 - smoothstep(u_radius * 0.42, u_radius + 0.12, dist - organicEdge);
    float alphaBase = radialMask * u_alpha;
    if (alphaBase < 0.005) discard;

    // 2. Radial shattering impact cracks radiating from crystal root
    float ang = atan(p.y, p.x);
    float warpAng = ang + (fbmLow - 0.5) * 0.55 + (fbmHigh - 0.5) * 0.25;
    float rayA = 1.0 - smoothstep(0.0, 0.14, abs(sin(warpAng * 7.0 + dist * 1.4)));
    float rayB = 1.0 - smoothstep(0.0, 0.09, abs(sin(warpAng * 13.0 - dist * 2.1 + 1.3)));
    float crackFade = 1.0 - smoothstep(0.15, max(u_radius * 0.92, 0.25), dist);
    float radialCracks = clamp(rayA * 0.85 + rayB * 0.65, 0.0, 1.0) * crackFade;

    // 3. Glazed ice core -> feathery hoarfrost dendrites
    float coreGlaze = 1.0 - smoothstep(0.08, max(u_radius * 0.58, 0.15), dist - (fbmLow - 0.5) * 0.25);
    float frostVeins = smoothstep(0.46, 0.78, dendrite * 0.72 + frostGrain * 0.38);
    float frostMix = clamp(radialCracks * 0.88 + frostVeins * 0.72 + (1.0 - coreGlaze) * 0.35, 0.0, 1.0);

    vec3 N = normalize(fragNormal + vec3((fbmLow - 0.5) * 0.26, 0.0, (frostGrain - 0.5) * 0.20));
    vec3 V = normalize(viewPos - v_worldPos);
    vec3 L = normalize(u_lightDir);
    vec3 H = normalize(L + V);

    float ndotl = max(dot(N, L), 0.0);
    float ndoth = max(dot(N, H), 0.0);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    float specSharp = pow(ndoth, 64.0) * 0.90;
    float specBroad = pow(ndoth, 16.0) * 0.16;

    vec3 baseTint = mix(u_iceColor, u_frostColor, frostMix);
    vec3 lit = baseTint * (0.46 + 0.54 * ndotl * u_lightColor)
             + u_lightColor * (specSharp + specBroad) * (0.55 + 0.45 * coreGlaze)
             + u_frostColor * fresnel * 0.24;

    float waterBoost = clamp(u_waterMask, 0.0, 1.0);
    float patternOpacity = clamp(coreGlaze * 0.90 + radialCracks * 0.95 + frostVeins * 0.80, 0.0, 1.0);
    float outAlpha = clamp(alphaBase * mix(0.92, 0.98, waterBoost) * patternOpacity, 0.0, 1.0);

    finalColor = VFX_ResolveBody(lit, 1.0, outAlpha);
}

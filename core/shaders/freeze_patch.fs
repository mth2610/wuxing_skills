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

vec2 hash22_fp(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// Returns distance to nearest Voronoi cell border (F2 - F1).
float voronoiEdgeDist(vec2 x) {
    vec2 n = floor(x);
    vec2 f = fract(x);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = hash22_fp(n + g);
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return sqrt(d2) - sqrt(d1);
}

void main() {
    vec2 p = v_worldPos.xz - u_center.xz;
    float dist = length(p);

    float fbmLow = fbm2(p * 1.45 + vec2(3.7, -2.1));
    float fbmHigh = fbm2(p * 3.8 + vec2(-5.2, 1.9));
    float fbm = fbmLow * 0.65 + fbmHigh * 0.35;

    float fingerWarp = (fbm - 0.5) * (0.28 * max(u_maxRadius, 0.5) + 0.16);
    float radialMask = 1.0 - smoothstep(u_radius * 0.70, u_radius + 0.08, dist - fingerWarp);
    float alphaBase = radialMask * u_alpha * fragColor.a;
    if (alphaBase < 0.005) discard;

    float cellPrimary = voronoiEdgeDist(p * 2.15);
    float cellFine = voronoiEdgeDist(p * 4.9 + vec2(11.3, 5.7));
    float crackPrimary = 1.0 - smoothstep(0.015, 0.080, cellPrimary);
    float crackFine = 1.0 - smoothstep(0.010, 0.045, cellFine);

    // Outer advancing rim turns into bright hoarfrost rime; interior stays glassy ice
    float rimeZone = smoothstep(u_radius * 0.42, max(u_radius * 0.96, 0.05), dist - fingerWarp * 0.5);
    float frostMix = clamp(rimeZone * 0.80 + crackPrimary * 0.72 + crackFine * 0.35 + fbmHigh * 0.18, 0.0, 1.0);

    vec3 N = normalize(fragNormal + vec3((fbm - 0.5) * 0.24, 0.0, (crackFine - 0.5) * 0.18));
    vec3 V = normalize(viewPos - v_worldPos);
    vec3 L = normalize(u_lightDir);
    vec3 H = normalize(L + V);

    float ndotl = max(dot(N, L), 0.0);
    float ndoth = max(dot(N, H), 0.0);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    float specSharp = pow(ndoth, 64.0) * 0.85;
    float specBroad = pow(ndoth, 14.0) * 0.16;

    vec3 baseTint = mix(u_iceColor, u_frostColor, frostMix);
    vec3 lit = baseTint * (0.44 + 0.56 * ndotl * u_lightColor)
             + u_lightColor * (specSharp + specBroad) * (1.0 - rimeZone * 0.45)
             + u_frostColor * fresnel * 0.26;

    float waterBoost = clamp(u_waterMask, 0.0, 1.0);
    float outAlpha = clamp(alphaBase * mix(0.78, 0.95, waterBoost)
                         * (0.68 + 0.32 * clamp(crackPrimary + rimeZone, 0.0, 1.0)), 0.0, 1.0);

    finalColor = VFX_ResolveBody(lit, 1.0, outAlpha);
}

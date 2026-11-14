#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 v_localPos;

uniform vec4  u_baseColor;
uniform vec4  u_rimColor;
uniform float u_partMode;  // 0.0 = blade, 1.0 = gold guard/pommel, 2.0 = core gem
uniform float u_phase;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragPosition);

    // Double-sided normal alignment + sharp Fresnel term
    float ndotv = dot(N, V);
    if (ndotv < 0.0) {
        N = -N;
        ndotv = -ndotv;
    }
    float NoV = clamp(ndotv, 0.0, 1.0);
    float fresnel = pow(1.0 - NoV, 2.6);
    float sharpFresnel = pow(1.0 - NoV, 4.2);

    // Studio dual-key reflections in view space for crisp metallic sparkle
    vec3 R = reflect(-V, N);
    vec3 L1 = normalize(vec3(0.45, 0.78, 0.42));
    vec3 L2 = normalize(vec3(-0.62, 0.35, 0.70));
    float spec1 = pow(max(dot(R, L1), 0.0), 42.0);
    float spec2 = pow(max(dot(R, L2), 0.0), 64.0);
    float skyBand = smoothstep(-0.25, 0.65, R.y);

    // Metallic body shading: dark-to-bright facet contrast (keeps darken% high on white bg)
    float facetDiff = 0.30 + 0.70 * max(dot(N, L1), 0.0);
    vec3 albedo = u_baseColor.rgb;
    vec3 envColor = mix(albedo * 0.32, albedo * 1.35 + vec3(0.18, 0.16, 0.12), skyBand);
    vec3 color = mix(albedo * facetDiff * 0.55, envColor, 0.62);

    // Crisp mirror specular glints
    color += (vec3(1.4, 1.35, 1.2) + u_rimColor.rgb * 0.6) * (spec1 * 1.45 + spec2 * 1.15);

    if (u_partMode < 0.5) {
        // Blade: subtle micro-damascus sheen + traveling Kim Quang shimmer sweep
        float wave = sin(v_localPos.z * 26.0 + sin(v_localPos.x * 90.0) * 1.8) * 0.5 + 0.5;
        color *= mix(0.86, 1.16, wave);

        float sweepZ = fract(u_time * 0.55 + u_phase * 0.17) * 2.2 - 0.55;
        float sweep = exp(-pow((v_localPos.z - sweepZ) * 4.5, 2.0));
        color += u_rimColor.rgb * sweep * 1.35;

        // Sharp razor-edge & central spine ridge highlight
        float tipGlint = smoothstep(0.65, 1.15, v_localPos.z);
        color += u_rimColor.rgb * tipGlint * 1.10;
    } else if (u_partMode > 1.5) {
        // Core Gem: incandescent jewel core
        float pulse = 0.85 + 0.20 * sin(u_time * 5.0 + u_phase);
        color = mix(u_rimColor.rgb * 2.2, vec3(3.4, 3.1, 2.4), NoV) * pulse;
    }

    // Concentrated Fresnel edge glow -> blooms cleanly around edges & ridges
    vec3 rimGlow = u_rimColor.rgb * (fresnel * 2.1 + sharpFresnel * 2.8);
    color += rimGlow;

    finalColor = VFX_ResolveBody(color, 1.0, 1.0);
}

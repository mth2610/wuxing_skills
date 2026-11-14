#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 v_worldPos;

uniform float u_fade;
uniform vec4  u_deepColor;
uniform vec4  u_crestColor;
uniform vec4  u_foamColor;

void main() {
    float depth01 = clamp(fragTexCoord.x, 0.0, 1.0);
    float foam    = clamp(fragTexCoord.y, 0.0, 1.0);

    // Smoothly feather outer thin edges so no grid boundary ever shows
    float edgeAlpha = smoothstep(0.03, 0.22, depth01);
    if (edgeAlpha * u_fade <= 0.004) discard;

    // World-space dual-scrolling FBM water ripples (locked to world, never slides with camera)
    vec2 uv1 = v_worldPos.xz * 1.45 + vec2(u_time * 1.25, -u_time * 0.90);
    vec2 uv2 = v_worldPos.xz * 2.85 + vec2(-u_time * 0.95, u_time * 1.35);
    float h0 = fbm2(uv1) + 0.5 * fbm2(uv2);
    float hx = fbm2(uv1 + vec2(0.07, 0.0)) + 0.5 * fbm2(uv2 + vec2(0.07, 0.0));
    float hz = fbm2(uv1 + vec2(0.0, 0.07)) + 0.5 * fbm2(uv2 + vec2(0.0, 0.07));
    vec2 rippleGrad = vec2(h0 - hx, h0 - hz) * 4.5;

    vec3 N = perturbNormal(normalize(fragNormal), rippleGrad, 0.45);
    // In MyBeginMode3D, fragPosition is in view space (camera at origin)
    vec3 V = normalize(-fragPosition);
    vec3 L = normalize(vec3(0.45, 0.78, 0.42));

    float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 3.6);
    float specSharp = calcSpecular(N, L, V, 192.0) * 3.0;
    float specSoft  = calcSpecular(N, L, V, 32.0)  * 0.28;
    float sunLit    = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);

    // Natural deep river/jade-water absorption (no neon cyan plastic look)
    vec3 shallowCol = u_crestColor.rgb * 0.55;
    vec3 deepCol    = u_deepColor.rgb * 0.55;
    vec3 waterTint  = mix(shallowCol, deepCol, pow(depth01, 0.80));
    waterTint *= (0.68 + 0.36 * sunLit);

    // Crisp sub-surface caustic web
    float causticNoise = fbm2(v_worldPos.xz * 2.2 - vec2(u_time * 1.5, u_time * 1.1));
    float caustic = smoothstep(0.48, 0.76, causticNoise) * (1.0 - foam * 0.8) * 0.35;
    waterTint += vec3(0.32, 0.75, 0.82) * caustic;

    // Sky Fresnel reflection + sun glints
    vec3 skyReflect = vec3(0.58, 0.78, 0.90);
    vec3 color = mix(waterTint, skyReflect, fresnel * 0.55);
    color += vec3(specSharp + specSoft) * (1.0 - foam * 0.6);

    // Aerated lacy breaking crest foam (multi-octave FBM breakup, no solid white band)
    float fn1 = fbm2(v_worldPos.xz * 3.6 + vec2(u_time * 1.5, -u_time * 1.1));
    float fn2 = fbm2(v_worldPos.xz * 8.2 - vec2(u_time * 2.1, u_time * 1.4));
    float lacyFoam = smoothstep(0.36, 0.72, foam * 0.85 + fn1 * 0.45 + fn2 * 0.25 - 0.30);
    float crestCore = smoothstep(0.62, 0.95, foam);
    float foamMask = clamp(max(lacyFoam * 0.82, crestCore), 0.0, 1.0) * edgeAlpha;
    color = mix(color, u_foamColor.rgb * (0.85 + 0.18 * sunLit), foamMask * 0.85);

    // Clear water transparency: see grass/ground underneath in body, denser at crest/grazing/foam
    float bodyAlpha = mix(0.08, 0.34, pow(depth01, 0.85));
    float alpha = mix(bodyAlpha, 0.65, fresnel);
    alpha = max(alpha, foamMask * 0.80);
    alpha *= edgeAlpha * u_fade;

    finalColor = VFX_ResolveBody(color, 1.0, clamp(alpha, 0.0, 0.90));
}

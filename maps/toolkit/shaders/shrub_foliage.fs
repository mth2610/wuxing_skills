#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;     // rgb = albedo, a = part mask (0=bark, 0.5=accent/thorn, 1.0=foliage)
in vec4 v_shrubParams; // x = rootHeight, y = dryness, z = scorch, w = frost
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb;
    float partMask = fragColor.a;
    float foliageMask = smoothstep(0.45, 0.95, partMask);
    float rootHeight = v_shrubParams.x;
    float dryness = v_shrubParams.y;
    float scorch = v_shrubParams.z;
    float frost = v_shrubParams.w;

    vec3 N = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    if (!gl_FrontFacing) N = vec3(-N.x, max(abs(N.y), 0.20), -N.z);

    vec3 toCam = u_viewPos - fragPosition;
    float distSq = max(dot(toCam, toCam), 1e-6);
    vec3 V = toCam * inversesqrt(distSq);
    vec3 L = -u_lightDir * inversesqrt(max(dot(u_lightDir, u_lightDir), 1e-8));

    // Physiological dryness, scorch, and alpine frost modulation
    if (dryness > 0.01) {
        vec3 aridOlive = albedo * vec3(1.06, 0.98, 0.78);
        albedo = mix(albedo, aridOlive, dryness * foliageMask * 0.45);
    }
    vec3 emberGlow = vec3(0.0);
    if (scorch > 0.01) {
        vec3 charCol = vec3(0.06, 0.055, 0.05);
        albedo = mix(albedo, charCol, scorch * (0.55 + 0.35 * rootHeight));
        float tipEmber = smoothstep(0.65, 0.98, rootHeight) * smoothstep(0.45, 0.95, scorch);
        emberGlow = vec3(1.65, 0.38, 0.06) * (tipEmber * 0.55);
    }
    if (frost > 0.01) {
        float snowSlope = smoothstep(0.18, 0.72, N.y) * smoothstep(0.20, 0.85, rootHeight);
        vec3 rimeColor = vec3(0.84, 0.91, 0.97);
        albedo = mix(albedo, rimeColor, snowSlope * frost * 0.72);
    }

    float dynCov = 0.0;
    float shadow = (v_lightSpace.w > 0.0)
        ? MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, N, L, dynCov)
        : 1.0;
    float cloudVis = 0.92;
    if (distSq < 900.0) {
        cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, fragPosition), 0.92, clamp((distSq - 400.0) * 0.002, 0.0, 1.0));
    }
    float directVis = shadow * cloudVis;

    // Wrap diffuse for foliage volume; crisp Lambert for woody stems/thorns
    float ndlRaw = dot(N, L);
    float ndl = mix(max(ndlRaw, 0.0), clamp((ndlRaw + 0.42) * 0.7042, 0.0, 1.0), foliageMask);

    // Waxy cuticle / silver-sage Fresnel sheen (stronger in arid scrub)
    float oneMinusNV = clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0);
    float fresnel = (oneMinusNV * oneMinusNV) * (oneMinusNV * oneMinusNV);
    float sageSheen = fresnel * mix(0.14, 0.32, dryness) * foliageMask * (1.0 - scorch * 0.8);

    // Backlit Mie Subsurface Scattering through leaf margins & succulent tips
    float backLit = max(dot(V, -L), 0.0);
    float backScatter = backLit * backLit * backLit * foliageMask * (1.0 - scorch * 0.9);
    vec3 sssTint = mix(vec3(1.20, 1.38, 0.65), vec3(1.18, 1.02, 0.68), dryness);
    vec3 sss = u_lightColor * (albedo * sssTint * (backScatter * 0.38 * directVis));

    // Ground root contact AO + subtle leaf-sprig rim separation
    float rootAO = mix(0.42, 1.0, smoothstep(0.0, 0.34, rootHeight));
    float leafTipBoost = mix(0.92, 1.12, smoothstep(0.35, 0.95, rootHeight)) * foliageMask + (1.0 - foliageMask);

    vec3 hemi = Environment_HemisphereIrradiance(N, u_ambientColor);
    vec3 lit = albedo * leafTipBoost * (hemi + u_lightColor * (ndl * directVis)) * rootAO;
    lit += u_lightColor * (sageSheen * mix(0.45, 1.0, directVis)) + sss + emberGlow;

    if (distSq < 1225.0) {
        lit += VFXLights_Accumulate(fragPosition, N, albedo);
    }

    finalColor = vec4(lit, 1.0);
}

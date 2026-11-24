#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;    // .rgb = vertex albedo * tint * AO, .a = canopy mask (0=bark, 1=foliage)
in vec2 fragTexCoord; // .x < 0.0 => bark (y=AO); .x >= 0.0 => foliage atlas UV
in float v_localY;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform sampler2D u_treeFoliageAtlas;
uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform float u_treeSnowAmount;

out vec4 finalColor;

float Tree_Hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float Tree_Noise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Tree_Hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = Tree_Hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = Tree_Hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = Tree_Hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = Tree_Hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = Tree_Hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = Tree_Hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = Tree_Hash31(i + vec3(1.0, 1.0, 1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

void main()
{
    float canopyMask = fragColor.a;
    vec3 albedo = fragColor.rgb;
    float ao = 1.0;

    if (fragTexCoord.x >= 0.0) {
        vec4 tex = texture(u_treeFoliageAtlas, fragTexCoord);
        if (tex.a < 0.42)
            discard;
        albedo = tex.rgb * fragColor.rgb;
    } else {
        // Procedural longitudinal bark fissures + root-collar contact AO
        float fissure = sin(fragPosition.y * 14.0 + sin(fragPosition.x * 9.5 + fragPosition.z * 9.5) * 2.2) * 0.5 + 0.5;
        float fineGrain = sin(fragPosition.x * 23.0 - fragPosition.z * 19.0 + fragPosition.y * 6.0) * 0.5 + 0.5;
        albedo *= mix(0.80, 1.12, fissure * 0.72 + fineGrain * 0.28);
        float rootAO = mix(0.46, 1.0, smoothstep(0.0, 0.65, v_localY));
        ao = rootAO * clamp(fragTexCoord.y, 0.35, 1.0);
    }

    vec3 N = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    // Flip normal on back-face of double-sided foliage fronds so undersides stay rich dark evergreen!
    if (!gl_FrontFacing) N = -N;

    // Nature_UpdateShader uploads u_lightDir pointing upward toward the sun (y > 0)
    vec3 L = u_lightDir * inversesqrt(max(dot(u_lightDir, u_lightDir), 1e-8));
    vec3 toCam = u_viewPos - fragPosition;
    float distSq = max(dot(toCam, toCam), 1e-6);
    vec3 V = toCam * inversesqrt(distSq);

    // Crisp World-Space Snow Accumulation calibrated so u_treeSnowAmount=0.70 => ~70% snow / ~30% green needles
    float snowMask = 0.0;
    if (u_treeSnowAmount > 0.02) {
        float upDot = N.y;
        float nMacro = Tree_Noise3D(fragPosition * 2.2);
        float nMicro = Tree_Noise3D(fragPosition * 8.5);
        float breakup = (nMacro * 0.62 + nMicro * 0.38) - 0.5;
        float snowSens = mix(0.55, 1.0, canopyMask);
        float slopeCut = mix(0.66, 0.48, canopyMask);
        float effectiveSnow = clamp(u_treeSnowAmount * snowSens, 0.0, 1.0);
        float thresh = mix(1.06, slopeCut, effectiveSnow);
        snowMask = smoothstep(thresh - 0.10, thresh + 0.10, upDot + breakup * 0.44);
        snowMask *= smoothstep(0.02, 0.12, u_treeSnowAmount);
        // Strict vertical guard: never bleed snow onto undersides
        snowMask *= smoothstep(0.12, 0.38, upDot);
        vec3 crispSnow = mix(vec3(0.86, 0.92, 0.98), vec3(0.97, 0.99, 1.0), smoothstep(0.35, 0.92, upDot));
        albedo = mix(albedo, crispSnow, snowMask * clamp(u_treeSnowAmount * 1.15, 0.0, 0.92));
    }

    float dynCov = 0.0;
    float shadow = (v_lightSpace.w > 0.0)
        ? MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, N, L, dynCov)
        : 1.0;
    float cloudVis = 0.92;
    if (distSq < 1600.0) {
        cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, fragPosition), 0.92, clamp((distSq - 900.0) / 700.0, 0.0, 1.0));
    }
    float directVis = shadow * cloudVis;

    // Wrap diffuse for lush foliage fronds; crisp Lambert for woody bark
    float ndlRaw = dot(N, L);
    float ndl = mix(max(ndlRaw, 0.0), clamp((ndlRaw + 0.42) * 0.704225, 0.0, 1.0), canopyMask);

    // Backlit Mie Subsurface Scattering through needles/leaves & snow edges
    float backDot = max(dot(V, -L), 0.0);
    float backLit = backDot * backDot * backDot;
    float oneMinusNV = clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0);
    float rimDot = oneMinusNV * oneMinusNV * oneMinusNV;
    vec3 sssTint = mix(albedo * vec3(1.28, 1.44, 0.80), vec3(0.68, 0.88, 1.0), snowMask);
    float sssAmt = mix(0.46 * canopyMask, 0.32, snowMask);
    vec3 sss = u_lightColor * sssTint * ((backLit * 0.48 + rimDot * 0.16) * sssAmt * directVis);

    // Sunlit snow micro-crystal sparkle glints
    vec3 sparkle = vec3(0.0);
    if (snowMask > 0.15 && distSq < 900.0) {
        float sunFace = smoothstep(0.05, 0.45, ndlRaw);
        vec3 cell = floor(fragPosition * 18.0 + V * 2.5);
        float spk = smoothstep(0.965, 0.995, Tree_Hash31(cell));
        sparkle = u_lightColor * (spk * snowMask * sunFace * 0.55 * directVis);
    }

    vec3 hemi = Environment_HemisphereIrradiance(N, u_ambientColor);
    vec3 lit = albedo * (hemi + u_lightColor * (ndl * directVis)) * ao + sss + sparkle;

    if (distSq < 1600.0) {
        lit += VFXLights_Accumulate(fragPosition, N, albedo);
    }

    finalColor = vec4(lit, 1.0);
}

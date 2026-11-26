#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;    // .rgb = vertex albedo * tint, .a = canopy mask (0=bark, 1=foliage)
in vec2 fragTexCoord; // .x < 0.0 => bark (y=continuous root-to-tip t); .x >= 0.0 => foliage atlas UV
in float v_localY;
in float v_lodVis;
in vec4 v_treeState;  // x = dryness, y = scorch, z = canopyShell [0..1], w = leafRank [0..1]
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

float Tree_Bayer4x4(vec2 fragXY)
{
    int x = int(mod(floor(fragXY.x), 4.0));
    int y = int(mod(floor(fragXY.y), 4.0));
    int idx = y * 4 + x;
    const float kBayer[16] = float[16](
        0.0625, 0.5625, 0.1875, 0.6875,
        0.8125, 0.3125, 0.9375, 0.4375,
        0.2500, 0.7500, 0.1250, 0.6250,
        1.0000, 0.5000, 0.8750, 0.3750
    );
    return kBayer[idx];
}

void main()
{
    // Screen-door Bayer dithered LOD cross-fade (zero overhead outside the 6m transition band!)
    if (v_lodVis < 0.995) {
        if (v_lodVis <= 0.005 || v_lodVis < Tree_Bayer4x4(gl_FragCoord.xy))
            discard;
    }

    float canopyMask = fragColor.a;
    vec3 albedo = fragColor.rgb;
    float dryness = v_treeState.x;
    float scorch = v_treeState.y;
    float canopyShell = v_treeState.z;
    float leafRank = v_treeState.w;
    float ao = 1.0;

    vec3 toCam = u_viewPos - fragPosition;
    float distSq = max(dot(toCam, toCam), 1e-6);
    vec3 V = toCam * inversesqrt(distSq);

    vec3 emberGlow = vec3(0.0);

    if (fragTexCoord.x >= 0.0) {
        vec4 tex = texture(u_treeFoliageAtlas, fragTexCoord);
        // Distance-compensated cutoff preserves thin bamboo/pine/blossom silhouettes across mipmaps
        float cutoff = mix(0.40, 0.22, clamp(distSq * (1.0 / 4900.0), 0.0, 1.0));
        if (tex.a < cutoff)
            discard;
        albedo = tex.rgb * fragColor.rgb;

        // 3D Canopy volumetric shell occlusion (rich shadowed core -> luminous sunlit outer crown)
        ao = mix(0.56, 1.06, smoothstep(0.05, 0.92, canopyShell));

        // Progressive chlorophyll senescence: lush -> golden amber -> parched russet straw
        if (dryness > 0.02) {
            float tipSen = clamp(dryness * 1.18 + (leafRank - 0.5) * 0.28, 0.0, 1.0);
            vec3 autumnGold = vec3(0.84, 0.60, 0.20) * (0.68 + 0.36 * canopyShell);
            vec3 parchedStraw = vec3(0.48, 0.35, 0.20) * (0.62 + 0.32 * canopyShell);
            vec3 dryCol = mix(autumnGold, parchedStraw, smoothstep(0.32, 0.85, tipSen));
            albedo = mix(albedo, dryCol, smoothstep(0.04, 0.72, tipSen) * 0.84);
        }
    } else {
        // Continuous procedural bark (zero per-segment UV banding!)
        float barkT = clamp(fragTexCoord.y, 0.0, 1.0);
        bool isPaleBirch = (albedo.r > 0.58 && albedo.b > 0.52);
        if (isPaleBirch) {
            float ringWave = sin(fragPosition.y * 26.0 + sin(fragPosition.x * 11.0 + fragPosition.z * 11.0) * 1.6);
            float lenticel = smoothstep(0.74, 0.95, ringWave) * smoothstep(0.15, 0.85, Tree_Noise3D(fragPosition * 6.5));
            float paperGrain = sin(fragPosition.y * 9.0 + fragPosition.x * 4.0) * 0.5 + 0.5;
            albedo = mix(albedo * mix(0.92, 1.06, paperGrain), vec3(0.20, 0.19, 0.21), lenticel * 0.72);
            // Dark rough basal birch collar near root
            float basalCollar = 1.0 - smoothstep(0.05, 0.95, v_localY);
            albedo = mix(albedo, vec3(0.18, 0.16, 0.15), basalCollar * 0.68);
        } else {
            // Vertical longitudinal bark furrows (high-frequency in XZ, elongated along Y)
            float warp = Tree_Noise3D(vec3(fragPosition.x * 3.8, fragPosition.y * 1.2, fragPosition.z * 3.8));
            float vertFurrow = sin((fragPosition.x * 16.0 - fragPosition.z * 14.0) + warp * 5.2) * 0.5 + 0.5;
            float fineGrain = Tree_Noise3D(vec3(fragPosition.x * 11.0, fragPosition.y * 3.2, fragPosition.z * 11.0));
            albedo *= mix(0.76, 1.14, vertFurrow * 0.62 + fineGrain * 0.38);
        }

        // Sun-bleached weathered deadwood patina as tree dries
        if (dryness > 0.05) {
            float crack = smoothstep(0.58, 0.92, Tree_Noise3D(fragPosition * 9.0));
            vec3 bleachedWood = vec3(0.42, 0.39, 0.36) * mix(0.74, 1.14, crack);
            albedo = mix(albedo, bleachedWood, smoothstep(0.20, 1.0, dryness) * 0.64);
        }

        // Charred carbon bark + restrained upper-twig ember fissures when scorched
        if (scorch > 0.02) {
            vec3 charCol = vec3(0.06, 0.055, 0.052);
            albedo = mix(albedo, charCol, scorch * (0.62 + 0.32 * barkT));
            float emberNoise = Tree_Noise3D(fragPosition * 8.5);
            float emberMask = smoothstep(0.55, 0.96, barkT) * smoothstep(0.52, 0.92, scorch) * smoothstep(0.58, 0.88, emberNoise);
            emberGlow = vec3(1.55, 0.36, 0.06) * (emberMask * 0.62);
        }

        if (u_treeSnowAmount > 0.35) {
            float baseSnowCollar = (1.0 - smoothstep(0.05, 0.58, v_localY)) * smoothstep(0.35, 0.70, u_treeSnowAmount);
            albedo = mix(albedo, vec3(0.88, 0.92, 0.98), baseSnowCollar * 0.88);
            float rootAO = mix(0.84, 1.0, smoothstep(0.0, 0.65, v_localY));
            ao = rootAO * mix(0.82, 1.0, barkT);
        } else {
            float rootAO = mix(0.62, 1.0, smoothstep(0.0, 0.72, v_localY));
            ao = rootAO * mix(0.84, 1.0, barkT);
        }
    }

    vec3 N = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    // Soften back-facing foliage normals so canopy undersides retain gentle volumetric lift
    if (!gl_FrontFacing) {
        N = -N;
        if (canopyMask > 0.5) {
            N = normalize(mix(N, vec3(0.0, 0.34, 0.0), 0.34));
        }
    }

    // Analytic leaf-vein micro-normal perturbation from atlas luminance derivatives (zero extra sampler!)
    if (canopyMask > 0.5 && distSq < 900.0) {
        float luma = dot(albedo, vec3(0.299, 0.587, 0.114));
        float nearFade = 1.0 - smoothstep(225.0, 900.0, distSq);
        vec3 dPdx = dFdx(fragPosition);
        vec3 dPdy = dFdy(fragPosition);
        vec3 bump = (dPdx * dFdx(luma) + dPdy * dFdy(luma));
        float bLenSq = dot(bump, bump);
        if (bLenSq > 1e-8) {
            N = normalize(N - bump * (min(inversesqrt(bLenSq), 12.0) * 0.14 * nearFade));
        }
    }

    vec3 L = u_lightDir * inversesqrt(max(dot(u_lightDir, u_lightDir), 1e-8));

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
        float thresh = mix(1.06, slopeCut + 0.16, effectiveSnow);
        snowMask = smoothstep(thresh - 0.10, thresh + 0.08, upDot + breakup * 0.48);
        snowMask *= smoothstep(0.02, 0.12, u_treeSnowAmount);
        snowMask *= smoothstep(0.28, 0.56, upDot);
        vec3 crispSnow = mix(vec3(0.88, 0.93, 0.99), vec3(0.98, 0.99, 1.0), smoothstep(0.45, 0.92, upDot));
        albedo = mix(albedo, crispSnow, snowMask * clamp(u_treeSnowAmount * 0.95, 0.0, 0.72));
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

    // Wrap diffuse for lush foliage clouds + thin-leaf diffuse transmission
    float ndlRaw = dot(N, L);
    float ndl = mix(max(ndlRaw, 0.0), clamp((ndlRaw + 0.44) * 0.6944, 0.0, 1.0), canopyMask);
    float leafTransmit = max(-ndlRaw, 0.0) * 0.30 * canopyMask * directVis * (1.0 - dryness * 0.65);

    // Backlit Mie Subsurface Scattering through needles/blossoms/leaves & snow edges
    float backDot = max(dot(V, -L), 0.0);
    float backLit = backDot * backDot * backDot;
    float oneMinusNV = clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0);
    float rimDot = oneMinusNV * oneMinusNV * oneMinusNV;
    vec3 leafSssTint = mix(albedo * vec3(1.24, 1.18, 1.06), albedo * vec3(1.18, 0.94, 0.62), dryness);
    vec3 sssTint = mix(leafSssTint, vec3(0.68, 0.88, 1.0), snowMask);
    float sssAmt = mix(0.42 * canopyMask * (1.0 - dryness * 0.55), 0.32, snowMask);
    vec3 sss = u_lightColor * sssTint * ((backLit * 0.46 + rimDot * 0.16) * sssAmt * directVis)
             + albedo * u_lightColor * leafTransmit;

    // GPU Gems 3 Ch.4 waxy leaf cuticle specular (broad modified Blinn-Phong + Fresnel grazing sheen)
    vec3 leafSpec = vec3(0.0);
    if (canopyMask > 0.5 && distSq < 1600.0) {
        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float specPow = pow(ndh, 24.0);
        float waxyMask = (1.0 - dryness * 0.72) * (1.0 - snowMask * 0.85) * smoothstep(0.0, 0.35, ndlRaw);
        float fresnelSheen = rimDot * 0.18;
        leafSpec = u_lightColor * ((specPow * 0.24 + fresnelSheen) * waxyMask * directVis * (0.45 + 0.55 * canopyShell));
    }

    // Sunlit snow micro-crystal sparkle glints
    vec3 sparkle = vec3(0.0);
    if (snowMask > 0.15 && distSq < 900.0) {
        float sunFace = smoothstep(0.05, 0.45, ndlRaw);
        vec3 cell = floor(fragPosition * 18.0 + V * 2.5);
        float spk = smoothstep(0.965, 0.995, Tree_Hash31(cell));
        sparkle = u_lightColor * (spk * snowMask * sunFace * 0.55 * directVis);
    }

    vec3 treeBounce = mix(vec3(0.55, 0.45, 0.40), vec3(0.82, 0.89, 1.00), smoothstep(0.25, 0.70, u_treeSnowAmount));
    vec3 hemi = Environment_HemisphereIrradianceTinted(N, u_ambientColor, treeBounce);
    vec3 lit = albedo * (hemi + u_lightColor * (ndl * directVis)) * ao + sss + leafSpec + sparkle + emberGlow;

    if (distSq < 1600.0) {
        lit += VFXLights_Accumulate(fragPosition, N, albedo);
    }

    finalColor = vec4(lit, 1.0);
}

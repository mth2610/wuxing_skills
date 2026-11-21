#version 330
#include "core/shaders/common/lighting.glsl"
// Đợt E / E2 — spell/effect point lights. Same shared block as the ground and
// the character so a fireball agrees with every surface it stands on.
#include "core/shaders/common/vfx_lights.glsl"
#define MAP_SHADOW_NORMAL_OFFSET_TEXELS 1.8
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

// ============================================================
// WUXING — prop_lit Fragment Shader (CORE_ISSUES.md Item 36)
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragTangent;
in vec3 fragBitangent;
in float fragLocalY;

uniform sampler2D texture0;   // diffuse/albedo (raylib MATERIAL_MAP_DIFFUSE convention)
uniform sampler2D texture2;   // tangent-space normal map (MATERIAL_MAP_NORMAL convention)
uniform sampler2D texture3;   // roughness, single channel .r (MATERIAL_MAP_ROUGHNESS convention)

uniform vec4 colDiffuse;      // raylib standard tint uniform (DrawModelEx's tint param)

uniform vec3 u_lightDir;      // surface -> light, shader/view space (PropLit_UpdateLighting)
uniform vec3 u_lightColor;    // sun color, normalized 0..1
uniform vec3 u_ambientColor;  // ambient floor color, normalized 0..1
uniform vec3 u_viewPos;       // camera position in the same shader/view space
uniform float u_rockAridMode; // 0.0 = lush moss/woodland, 1.0 = arid earth/Ninh Thuan granite
uniform float u_rockVolcanicMode; // 0.0 = normal, 1.0 = volcanic basalt/obsidian/sulfur
uniform sampler2D u_cloudNoise;
uniform mat4 u_cloudWorldFromShader;

out vec4 finalColor;

// Reoriented Normal Mapping (Barré-Brisebois & Hill): stacks micro-grain onto macro rock facets without flattening.
vec3 PropBlendRNM(vec3 n1, vec3 n2) {
    vec3 t = n1 + vec3(0.0, 0.0, 1.0);
    vec3 u = n2 * vec3(-1.0, -1.0, 1.0);
    vec3 r = t * dot(t, u) - u * t.z;
    return r * inversesqrt(max(dot(r, r), 1e-8));
}

void main() {
    // [VULKAN / SPIR-V FIX]: Tính trước đạo hàm UV trong luồng đồng nhất
    vec2 uvDx = dFdx(fragTexCoord);
    vec2 uvDy = dFdy(fragTexCoord);

    // [TỐI ƯU ALU]: Tính khoảng cách và hướng nhìn camera (bảo toàn contract test_prop_lighting_space)
    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    float camDist = length(u_viewPos - fragPosition);
    vec3 viewDir = toCam * invCamDist;

    mat3 worldRot = mat3(u_cloudWorldFromShader);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    vec3 worldSmoothN = worldRot * smoothN;
    float isRiverStone = (colDiffuse.a < 0.985) ? 1.0 : smoothstep(0.01, 0.05, colDiffuse.b - colDiffuse.r);

    // Biplanar texture projection (Quilez): 2 taps instead of 3, with continuous world-space derivatives
    vec3 absWorldN = abs(worldSmoothN);
    float biplanarTiling = (isRiverStone > 0.5) ? 0.82 : 0.68;
    vec2 uvY = worldPosition.xz * biplanarTiling;
    bool useXPlane = (absWorldN.x > absWorldN.z);
    vec2 uvH = (useXPlane ? worldPosition.zy : worldPosition.xy) * biplanarTiling;
    float wY = clamp((absWorldN.y - 0.35) * (1.0 / 0.50), 0.0, 1.0);
    float wH = 1.0 - wY;

    vec2 uvDxY = dFdx(uvY), uvDyY = dFdy(uvY);
    vec2 dxX = dFdx(worldPosition.zy) * biplanarTiling;
    vec2 dyX = dFdy(worldPosition.zy) * biplanarTiling;
    vec2 dxZ = dFdx(worldPosition.xy) * biplanarTiling;
    vec2 dyZ = dFdy(worldPosition.xy) * biplanarTiling;
    vec2 uvDxH = useXPlane ? dxX : dxZ;
    vec2 uvDyH = useXPlane ? dyX : dyZ;

    vec4 texY = textureGrad(texture0, uvY, uvDxY, uvDyY);
    vec4 texH = textureGrad(texture0, uvH, uvDxH, uvDyH);
    vec4 albedo = (texY * wY + texH * wH);

    // Natural mineral lithology calibration: preserve genuine granite, river stone & scree tones
    albedo.rgb = mix(albedo.rgb, vec3(dot(albedo.rgb, vec3(0.3333333))), 0.08) * 0.96;
    albedo.rgb *= colDiffuse.rgb;
    if (u_rockAridMode > 0.5) {
        // Mali-safe sin-less 3D granite mineral variation preserving crisp texture & RNM relief
        vec3 gP1 = worldPosition * 2.4;
        vec3 i1 = floor(gP1); vec3 f1 = fract(gP1); f1 = f1 * f1 * (3.0 - 2.0 * f1);
        vec3 h0 = fract(i1 * vec3(0.1031, 0.1030, 0.0973));
        float c0 = fract((h0.x + h0.y) * h0.z + dot(h0, vec3(0.37, 0.71, 0.53)));
        float c1 = fract((h0.x + 0.1031 + h0.y) * h0.z + 0.19);
        float c2 = fract((h0.x + h0.y + 0.1030) * h0.z + 0.43);
        float gCoarse = mix(mix(c0, c1, f1.x), c2, f1.y * 0.6 + f1.z * 0.4);
        float texGrain = dot(albedo.rgb, vec3(0.3333333));
        vec3 feldsparBase = mix(vec3(0.68, 0.56, 0.44), vec3(0.86, 0.74, 0.58), gCoarse * 0.62 + 0.20);
        // Darken only natural texture crevices for warm biotite mineral flecks
        float biotiteMask = (1.0 - smoothstep(0.22, 0.46, texGrain)) * 0.42;
        vec3 procGranite = mix(feldsparBase, vec3(0.32, 0.25, 0.20), biotiteMask);
        albedo.rgb = mix(albedo.rgb, procGranite * (0.76 + 0.48 * texGrain), u_rockAridMode * 0.38);
    }

    vec3 normY = textureGrad(texture2, uvY, uvDxY, uvDyY).rgb * 2.0 - 1.0;
    vec3 normH = textureGrad(texture2, uvH, uvDxH, uvDyH).rgb * 2.0 - 1.0;
    vec3 macroNorm = normY * wY + normH * wH;
    macroNorm *= inversesqrt(max(dot(macroNorm, macroNorm), 1e-8));
    vec3 normalSample = macroNorm;

    // High-frequency crystalline mineral grain & micro-facet detail via RNM (< 24m)
    float detailLuma = 0.5;
    if (camDistSq < 576.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 576.0)) * 0.65;
        vec2 detailUV = uvH * 2.8 + vec2(0.17, 0.31);
        vec2 detailDx = uvDxH * 2.8;
        vec2 detailDy = uvDyH * 2.8;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        detailNorm.xy *= 0.85 * detailWeight;
        detailNorm *= inversesqrt(max(dot(detailNorm, detailNorm), 1e-8));
        normalSample = PropBlendRNM(macroNorm, detailNorm);
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        detailLuma = dot(detailAlb, vec3(0.3333333));
        albedo.rgb *= mix(vec3(1.0), clamp(detailAlb * 1.32, vec3(0.72), vec3(1.26)), detailWeight * 0.42);
    }

    // Bảo vệ chống suy biến vector tiếp tuyến T (NaN) và bảo toàn chiều Bitangent (handedness)
    vec3 rawT = fragTangent - smoothN * dot(smoothN, fragTangent);
    float tLenSq = dot(rawT, rawT);
    vec3 T;
    if (tLenSq > 1e-8) {
        T = rawT * inversesqrt(tLenSq);
    } else {
        vec3 refAxis = (abs(smoothN.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 fallbackT = cross(refAxis, smoothN);
        T = fallbackT * inversesqrt(max(dot(fallbackT, fallbackT), 1e-8));
    }
    vec3 rawB = cross(smoothN, T);
    vec3 B = (dot(rawB, fragBitangent) < 0.0) ? -rawB : rawB;

    normalSample.xy *= 1.45;
    normalSample *= inversesqrt(max(dot(normalSample, normalSample), 1e-8));
    vec3 normal = T * normalSample.x + B * normalSample.y + smoothN * normalSample.z;
    vec3 worldNormal = worldRot * normal;

    float roughY = textureGrad(texture3, uvY, uvDxY, uvDyY).r;
    float roughH = textureGrad(texture3, uvH, uvDxH, uvDyH).r;
    float roughness = clamp(roughY * wY + roughH * wH, 0.08, 0.98);

    // Safe fallback if a map forgot to push PropLit_UpdateLighting()
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 worldLightDir = worldRot * lightDir;
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // Coherent world-space geological strata & fracture cavity variation (no high-freq dotted contours)
    float strataNoise = sin(worldPosition.y * 3.4 + sin(worldPosition.x * 0.32 + worldPosition.z * 0.26) * 1.45) * 0.5 + 0.5;
    float fineStrata = sin(worldPosition.y * 10.5 + sin(worldPosition.x * 0.85 - worldPosition.z * 0.72) * 0.65) * 0.5 + 0.5;
    float fractureWave = abs(sin(worldPosition.x * 1.35 + worldPosition.y * 0.95 + sin(worldPosition.z * 1.15) * 1.25));
    float fractureCavity = smoothstep(0.0, mix(0.28, 0.36, u_rockAridMode), fractureWave);
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.86, 0.83, 0.80), vec3(1.10, 1.06, 1.00), strataNoise * 0.65 + fineStrata * 0.35);
    rockAlbedo *= mix(mix(0.72, 0.92, u_rockAridMode), 1.03, fractureCavity);

    // Top-surface moss & lichen colonization (or subtle desert varnish & sand dusting in arid mode)
    float warpP = sin(worldPosition.x * 0.55 - worldPosition.z * 0.48) * 1.35;
    float mossPatch = sin(worldPosition.x * 1.35 + cos(worldPosition.z * 1.15 + warpP) * 1.4) * 0.5 + 0.5;
    float altitudeMoss = (1.0 - smoothstep(9.5, 13.5, worldPosition.y)) * 0.40 + 0.60;
    float crevicePocket = (1.0 - fractureCavity) * 0.28 + (1.0 - macroNorm.z) * 0.24;
    float topMossMask = smoothstep(0.62, 0.88, worldNormal.y + crevicePocket * 0.14)
                      * smoothstep(0.08, 0.38, fragLocalY)
                      * smoothstep(0.32, 0.78, mossPatch * 0.55 + albedo.g * 0.45)
                      * mix(altitudeMoss, 1.0, u_rockAridMode);
    topMossMask *= (1.0 - isRiverStone * 0.92);
    vec3 lushCrownMoss = mix(vec3(0.13, 0.22, 0.08), vec3(0.20, 0.32, 0.11), mossPatch * 0.7 + detailLuma * 0.3);
    vec3 lichenTone = vec3(0.46, 0.45, 0.24);
    lushCrownMoss = mix(lushCrownMoss, lichenTone, smoothstep(0.56, 0.88, albedo.r) * 0.50);
    vec3 aridCrownVarnish = mix(vec3(0.54, 0.44, 0.33), vec3(0.65, 0.53, 0.38), smoothstep(0.40, 0.80, mossPatch * 0.65 + detailLuma * 0.35));
    vec3 crownMossAlbedo = mix(lushCrownMoss, aridCrownVarnish, u_rockAridMode);
    if (u_rockVolcanicMode > 0.5) {
        vec3 volcanicAshSulfur = mix(vec3(0.20, 0.19, 0.21), vec3(0.46, 0.38, 0.16), smoothstep(0.55, 0.90, mossPatch * 0.60 + detailLuma * 0.40));
        crownMossAlbedo = mix(crownMossAlbedo, volcanicAshSulfur, u_rockVolcanicMode);
        // Preserve crisp charcoal basalt relief instead of crushing into pitch-black silhouettes
        rockAlbedo *= mix(vec3(1.0), vec3(0.84, 0.82, 0.85), u_rockVolcanicMode);
    }
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * mix(0.64, 0.36, max(u_rockAridMode, u_rockVolcanicMode)));
    if (u_rockAridMode > 0.5) {
        // Subtle vertical desert-varnish streaks on flanks + pale windblown sand dusting on ledges
        float streak = sin(worldPosition.x * 2.2 + worldPosition.z * 1.9 + sin(worldPosition.y * 0.7) * 0.8) * 0.5 + 0.5;
        float flankMask = (1.0 - abs(worldNormal.y)) * smoothstep(-0.1, 0.4, fragLocalY);
        rockAlbedo = mix(rockAlbedo, rockAlbedo * vec3(0.80, 0.74, 0.68), streak * flankMask * 0.22 * u_rockAridMode);
        float sandDustMask = smoothstep(0.68, 0.92, worldNormal.y) * smoothstep(0.05, 0.35, fragLocalY);
        rockAlbedo = mix(rockAlbedo, vec3(0.72, 0.62, 0.48), sandDustMask * 0.45 * u_rockAridMode);
    }

    // Tight basal soil & root-humus collar grounding (or windblown ochre grus / volcanic ash skirt)
    float skirtMask = (1.0 - smoothstep(-0.42, mix(-0.04, 0.08, max(u_rockAridMode, u_rockVolcanicMode)), fragLocalY)) * (1.0 - isRiverStone * 0.85);
    vec3 skirtLoam = mix(vec3(0.13, 0.11, 0.08), vec3(0.44, 0.33, 0.22), u_rockAridMode);
    skirtLoam = mix(skirtLoam, vec3(0.16, 0.15, 0.16), u_rockVolcanicMode);
    vec3 lushSkirtMoss = mix(skirtLoam, vec3(0.15, 0.24, 0.09), clamp(worldNormal.y * 0.85 + (1.0 - fractureCavity) * 0.35, 0.0, 1.0));
    vec3 aridSkirtSand = mix(skirtLoam, vec3(0.56, 0.44, 0.30), clamp(worldNormal.y * 0.85 + detailLuma * 0.35, 0.0, 1.0));
    vec3 skirtMossAlbedo = mix(lushSkirtMoss, aridSkirtSand, u_rockAridMode);
    skirtMossAlbedo = mix(skirtMossAlbedo, vec3(0.18, 0.17, 0.18), u_rockVolcanicMode);
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.68);
    rockAlbedo *= mix(0.68, 1.0, smoothstep(-0.36, 0.02, fragLocalY));

    // Authentic water-tumbled river boulder biofilm, diatom patina & wet waterline meniscus
    float wetBase = 0.0;
    float splashHalo = 0.0;
    if (isRiverStone > 0.5) {
        float riverWetGate = 1.0 - u_rockAridMode;
        wetBase = (1.0 - smoothstep(0.08, 0.38, fragLocalY)) * riverWetGate;
        splashHalo = smoothstep(0.06, 0.36, fragLocalY) * (1.0 - smoothstep(0.34, 0.52, fragLocalY)) * riverWetGate;

        // Darken porous wet stone by 38%
        rockAlbedo *= mix(1.0, 0.62, wetBase * 0.88);

        // Diatom algae patina: velvety greenish-gold biofilm in the damp splash zone (suppressed in arid mode)
        float diatomMask = smoothstep(0.18, 0.75, worldNormal.y) * (splashHalo * 0.85 + wetBase * 0.35) * riverWetGate;
        vec3 diatomPatina = mix(vec3(0.22, 0.32, 0.14), vec3(0.36, 0.42, 0.18), albedo.g);
        rockAlbedo = mix(rockAlbedo, diatomPatina, diatomMask * 0.68);

        // High gloss wet specular reflection on wet areas
        roughness = mix(roughness, 0.08, wetBase * 0.92);
        roughness = mix(roughness, 0.22, splashHalo * 0.70);
    }

    // Ground & sand contact darkening at the base of every rock (softened in arid mode so cavities & undersides stay lit by sand bounce)
    float skirtContact = 1.0 - smoothstep(-0.62, 0.10, fragLocalY);
    rockAlbedo *= mix(1.0, mix(0.52, 0.78, u_rockAridMode), skirtContact * (isRiverStone > 0.5 ? 0.45 : 0.65));
    float contactAO = clamp(mix(0.42, 0.68, u_rockAridMode) + mix(0.58, 0.32, u_rockAridMode) * smoothstep(-0.60, 0.15, fragLocalY), 0.42, 1.0);

    roughness = mix(roughness, 0.88, max(topMossMask, skirtMask) * 0.82);
    if (isRiverStone <= 0.5) {
        roughness = clamp(roughness, 0.25, 0.95);
    }

    // Soft micro-wrap diffuse (0.15) brings out chiseled granite facets on the shaded side
    vec3 rawModelL = lightDir + vec3(0.32, -0.18, -0.26) * max(worldLightDir.y - 0.50, 0.0);
    vec3 modelLightDir = rawModelL * inversesqrt(max(dot(rawModelL, rawModelL), 1e-8));
    float diff = calcDiffuse(normal, modelLightDir, 0.15);

    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    shadow = mix(0.42, 1.0, shadow);
    if (shadow > 0.001) {
        shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
    }

    float spec = 0.0;
    if (diff > 0.0 && shadow > 0.001) {
        float shininess    = mix(48.0, 6.0, roughness);
        float specStrength = mix(0.04, 0.18, 1.0 - roughness);
        if (isRiverStone > 0.5) {
            specStrength = mix(specStrength, 0.48, wetBase * 0.85 + splashHalo * 0.45);
        }
        spec = calcSpecular(normal, modelLightDir, viewDir, shininess) * specStrength;
    }

    vec3 ambient = Environment_HemisphereIrradiance(worldNormal, actualAmbient);
    // Warm meadow turf bounce (or sun-baked desert sand bounce in arid mode) into lower rock facets
    vec3 bounceTint = mix(vec3(0.20, 0.32, 0.12), vec3(0.62, 0.46, 0.28), u_rockAridMode);
    bounceTint = mix(bounceTint, vec3(1.25, 0.42, 0.12), u_rockVolcanicMode);
    vec3 turfBounce = bounceTint * max(actualAmbient, vec3(0.24)) * max(-worldNormal.y * 0.5 + 0.5, 0.0) * mix(0.45, 0.72, max(u_rockAridMode, u_rockVolcanicMode));
    ambient = (ambient + turfBounce) * contactAO;
    if (isRiverStone > 0.5) {
        vec3 streamBounce = actualAmbient * vec3(0.85, 1.12, 1.20) + vec3(0.04, 0.10, 0.12);
        ambient = mix(ambient, streamBounce, 0.42 * wetBase);
    }
    float groundAO = clamp(worldPosition.y * 0.35 + mix(0.75, 0.88, u_rockAridMode), mix(0.40, 0.72, u_rockAridMode), 1.0);
    float skirtAO = clamp(mix(0.54, 0.76, u_rockAridMode) + mix(0.46, 0.24, u_rockAridMode) * smoothstep(-0.50, 0.12, fragLocalY), 0.54, 1.0);
    float creviceAO = clamp((normalSample.z * 1.30 - 0.22) * mix(mix(0.62, 0.88, u_rockAridMode), 1.0, fractureCavity), mix(0.42, 0.70, u_rockAridMode), 1.0);
    float mossSSS = pow(max(dot(viewDir, -modelLightDir), 0.0), 3.0) * topMossMask * shadow * 0.22 * (1.0 - max(u_rockAridMode, u_rockVolcanicMode));
    vec3 lit = rockAlbedo * (ambient * groundAO * skirtAO * creviceAO + diff * actualLight * shadow)
             + crownMossAlbedo * actualLight * mossSSS
             + spec * actualLight * shadow;
    if (u_rockVolcanicMode > 0.5) {
        // Cool slate-sky Fresnel edge rim so hexagonal basalt & obsidian edges stay crisp even in shadow
        float rimNdotV = max(dot(normal, viewDir), 0.0);
        float slateRim = pow(1.0 - rimNdotV, 3.2) * (0.35 + 0.65 * max(worldNormal.y, 0.0));
        lit += actualAmbient * vec3(0.75, 0.88, 1.15) * (slateRim * 0.42 * u_rockVolcanicMode);
    }

    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    // Atmospheric aerial haze for distant mountain crags (matches ground_splat.fs in arid mode)
    float hazeStart = mix(38.0, 42.0, u_rockAridMode);
    float hazeEnd = mix(115.0, 320.0, u_rockAridMode);
    float haze = smoothstep(hazeStart, hazeEnd, camDist) * mix(0.30, 0.36, u_rockAridMode);
    vec3 hazeColor = mix(actualAmbient * 1.35, actualAmbient * vec3(0.88, 0.84, 0.78) + actualLight * vec3(0.28, 0.22, 0.16), u_rockAridMode);
    lit = mix(lit, hazeColor, haze);

    finalColor = vec4(lit, albedo.a * colDiffuse.a);
}
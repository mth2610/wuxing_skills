#version 330
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_lights.glsl"
#define MAP_SHADOW_NORMAL_OFFSET_TEXELS 2.0
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

// ============================================================
// WUXING — AAA cliff_rock Fragment Shader
// GPU Gems 3 (Ch. 1) World-Space Triplanar Projection with
// Normal Derivatives, 80.lv RTGI Crevice Cavity Occlusion,
// 3D Geological Strata, Ledge Moss & Cloud Up-Bounce.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragTangent;
in vec3 fragBitangent;
in vec4 fragColor;
in float fragLocalY;

uniform sampler2D texture0;   // granite diffuse
uniform sampler2D texture2;   // normal map
uniform sampler2D texture3;   // roughness map

uniform vec4 colDiffuse;      // base tint

uniform vec3 u_lightDir;      // surface -> light, view space
uniform vec3 u_lightColor;    // sun color * intensity
uniform vec3 u_ambientColor;  // ambient sky color
uniform vec3 u_viewPos;       // camera pos in view space
uniform vec3 u_hazeColor;     // atmosphere / cloud haze color
uniform float u_cliffAridMode;// 0.0 = lush moss rim, 1.0 = arid earth/ochre crust rim
uniform float u_seaLevel;     // coastal water Y (world meters)
uniform sampler2D u_cloudNoise;
uniform mat4 u_cloudWorldFromShader;

out vec4 finalColor;

void main() {
    mat3 worldRot = mat3(u_cloudWorldFromShader);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothMeshN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    vec3 smoothWorldN = normalize(worldRot * smoothMeshN);

    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    vec3 viewDir = toCam * invCamDist;

    // ── 1. World-Space Triplanar Projections (GPU Gems 3, Chapter 1) ──
    // Eliminates all UV stretching, seams, and distortion on sheer cliff walls.
    vec3 absWN = abs(smoothWorldN);
    vec3 triWeights = absWN * absWN * absWN * absWN; // Sharp N^4 power blend
    triWeights /= max(triWeights.x + triWeights.y + triWeights.z, 1e-4);

    // Compute continuous world derivatives in uniform control flow
    vec3 wpDx = dFdx(worldPosition);
    vec3 wpDy = dFdy(worldPosition);

    const float kMacroScale = 0.24; // ~4.2m repeating granite bedrock
    vec2 uvX = worldPosition.zy * kMacroScale;
    vec2 uvY = worldPosition.xz * kMacroScale;
    vec2 uvZ = worldPosition.xy * kMacroScale;

    vec2 dxX = wpDx.zy * kMacroScale, dyX = wpDy.zy * kMacroScale;
    vec2 dxY = wpDx.xz * kMacroScale, dyY = wpDy.xz * kMacroScale;
    vec2 dxZ = wpDx.xy * kMacroScale, dyZ = wpDy.xy * kMacroScale;

    // Triplanar albedo fetches
    vec3 albX = (triWeights.x > 0.01) ? textureGrad(texture0, uvX, dxX, dyX).rgb : vec3(0.0);
    vec3 albY = (triWeights.y > 0.01) ? textureGrad(texture0, uvY, dxY, dyY).rgb : vec3(0.0);
    vec3 albZ = (triWeights.z > 0.01) ? textureGrad(texture0, uvZ, dxZ, dyZ).rgb : vec3(0.0);
    vec3 triAlbedo = albX * triWeights.x + albY * triWeights.y + albZ * triWeights.z;

    // Triplanar normal map fetches & world-space swizzle (Ryan Geiss basis)
    vec3 normX = (triWeights.x > 0.01) ? (textureGrad(texture2, uvX, dxX, dyX).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
    vec3 normY = (triWeights.y > 0.01) ? (textureGrad(texture2, uvY, dxY, dyY).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
    vec3 normZ = (triWeights.z > 0.01) ? (textureGrad(texture2, uvZ, dxZ, dyZ).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);

    normX.xy *= 1.65;
    normY.xy *= 1.65;
    normZ.xy *= 1.65;

    // Dual-scale mineral detail at close proximity (< 28m)
    if (camDistSq < 784.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 784.0)) * 0.42;
        const float kMicroScale = 3.6;
        vec2 dUvX = uvX * kMicroScale, dUvY = uvY * kMicroScale, dUvZ = uvZ * kMicroScale;
        vec2 dDxX = dxX * kMicroScale, dDyX = dyX * kMicroScale;
        vec2 dDxY = dxY * kMicroScale, dDyY = dyY * kMicroScale;
        vec2 dDxZ = dxZ * kMicroScale, dDyZ = dyZ * kMicroScale;

        vec3 dAlbX = (triWeights.x > 0.01) ? textureGrad(texture0, dUvX, dDxX, dDyX).rgb : vec3(0.0);
        vec3 dAlbY = (triWeights.y > 0.01) ? textureGrad(texture0, dUvY, dDxY, dDyY).rgb : vec3(0.0);
        vec3 dAlbZ = (triWeights.z > 0.01) ? textureGrad(texture0, dUvZ, dDxZ, dDyZ).rgb : vec3(0.0);
        vec3 detailAlbedo = dAlbX * triWeights.x + dAlbY * triWeights.y + dAlbZ * triWeights.z;

        triAlbedo = mix(triAlbedo, clamp(triAlbedo * detailAlbedo * 1.35 + 0.08, 0.1, 1.25), detailWeight * 0.35);

        vec3 dNormX = (triWeights.x > 0.01) ? (textureGrad(texture2, dUvX, dDxX, dDyX).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
        vec3 dNormY = (triWeights.y > 0.01) ? (textureGrad(texture2, dUvY, dDxY, dDyY).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
        vec3 dNormZ = (triWeights.z > 0.01) ? (textureGrad(texture2, dUvZ, dDxZ, dDyZ).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);

        normX = mix(normX, dNormX * 1.5, detailWeight * 0.45);
        normY = mix(normY, dNormY * 1.5, detailWeight * 0.45);
        normZ = mix(normZ, dNormZ * 1.5, detailWeight * 0.45);
    }

    vec3 sgnN = sign(smoothWorldN);
    vec3 wNormX = vec3(normX.z * sgnN.x, normX.y, normX.x * sgnN.x);
    vec3 wNormY = vec3(normY.x, normY.z * sgnN.y, normY.y);
    vec3 wNormZ = vec3(normZ.x * sgnN.z, normZ.y, normZ.z * sgnN.z);
    vec3 worldNormal = normalize(wNormX * triWeights.x + wNormY * triWeights.y + wNormZ * triWeights.z);

    // Transform world normal into view space for lighting
    vec3 normal = normalize(transpose(worldRot) * worldNormal);

    // Triplanar roughness
    float roughX = (triWeights.x > 0.01) ? textureGrad(texture3, uvX, dxX, dyX).r : 0.82;
    float roughY = (triWeights.y > 0.01) ? textureGrad(texture3, uvY, dxY, dyY).r : 0.82;
    float roughZ = (triWeights.z > 0.01) ? textureGrad(texture3, uvZ, dxZ, dyZ).r : 0.82;
    float roughness = clamp(roughX * triWeights.x + roughY * triWeights.y + roughZ * triWeights.z, 0.18, 0.94);

    // ── 2. Natural 3D Geological Strata & Mineral Suite ──
    // Aligned to tectonic joint strike (32 deg) + dip (14 deg) instead of horizontal scanlines
    const float kStrikeAngle = 32.0 * 3.14159265 / 180.0;
    vec2 strikeDir = vec2(cos(kStrikeAngle), sin(kStrikeAngle));
    float strikeCoord = dot(worldPosition.xz, strikeDir);
    float strataCoord = worldPosition.y * 0.42 + strikeCoord * 0.16;

    // 3D domain-warped turbulence
    float strataWarp = sin(worldPosition.x * 0.14 - worldPosition.z * 0.18) * 1.25;
    float strataMacro = sin(strataCoord * 2.40 + strataWarp) * 0.5 + 0.5;
    float strataFine  = sin(strataCoord * 6.80 + strataWarp * 1.8) * 0.5 + 0.5;
    float combinedStrata = strataMacro * 0.65 + strataFine * 0.35;
    float strataContrast = (strataMacro - 0.5) * 1.55 + (strataFine - 0.5) * 0.78;

    // Authentic mountain slate-granite palette (or warm sun-baked Ninh Thuan granite in arid mode)
    vec3 slateBedrock = mix(vec3(0.46, 0.48, 0.51), vec3(0.68, 0.57, 0.46), u_cliffAridMode);
    vec3 feldsparVein = mix(vec3(0.62, 0.58, 0.52), vec3(0.84, 0.72, 0.57), u_cliffAridMode);
    vec3 bedrockTint = mix(slateBedrock, feldsparVein, combinedStrata * mix(0.42, 0.72, u_cliffAridMode)) * 1.24;

    vec3 rockBase = triAlbedo * bedrockTint * colDiffuse.rgb;

    // ── 3. Ledge Moss, Rim Turf Overhang & Lichen Colonization (or arid ochre crust & caliche rim) ──
    // Organic sod/crust drape over the top cliff lip (fragColor.g = rim moss mask, invariant to rim world Y)
    float sodDropNoise = sin(worldPosition.x * 1.85 + worldPosition.z * 1.45) * 0.14
                       + cos(worldPosition.z * 3.40 - worldPosition.x * 2.70) * 0.08;
    float rimSodDrape = smoothstep(0.32 + sodDropNoise * 0.35, 0.86, fragColor.g) * fragColor.g;
    float ledgeFacing = smoothstep(0.60, 0.86, worldNormal.y);
    float creviceMask = smoothstep(-0.35, 0.45, strataContrast * 0.75 + triAlbedo.g - 0.5);
    float upperRimBoost = smoothstep(0.05, 0.75, fragColor.g) * 0.65 + 0.35;
    float shelfMoss = fragColor.g * ledgeFacing * upperRimBoost * (0.65 + 0.35 * creviceMask);
    float mossMask = clamp(max(shelfMoss, rimSodDrape * 0.94), 0.0, 1.0);
    vec3 lushMossColor = mix(vec3(0.10, 0.19, 0.06), vec3(0.20, 0.32, 0.10), clamp(triAlbedo.g * 0.75 + rimSodDrape * 0.35, 0.0, 1.0));
    vec3 aridRimColor  = mix(vec3(0.72, 0.61, 0.44), vec3(0.82, 0.70, 0.51), clamp(triAlbedo.g * 0.75 + rimSodDrape * 0.35, 0.0, 1.0));
    vec3 mossColor = mix(lushMossColor, aridRimColor, u_cliffAridMode);
    vec3 rockAlbedo = mix(rockBase, mossColor, mossMask * mix(0.88, 0.82, u_cliffAridMode));

    // ── 4. Deep Crevice Cavity Occlusion (80.lv RTGI Principle) ──
    // Intense occlusion in deep fissures and under shelf overhangs gives rock weight and depth
    float microCrevice = clamp(normX.z * triWeights.x + normY.z * triWeights.y + normZ.z * triWeights.z, 0.25, 1.0);
    float cavityAO = clamp(fragColor.r * (0.35 + 0.65 * microCrevice), mix(0.18, 0.68, u_cliffAridMode), 1.0);

    // Coastal tidal wet-rock splash zone + clinging white surf foam + turquoise sea bounce in arid mode
    float coastalWet = (u_cliffAridMode > 0.5)
        ? (1.0 - smoothstep(u_seaLevel - 0.15, u_seaLevel + 0.65, worldPosition.y))
        : 0.0;
    rockAlbedo *= mix(1.0, 0.72, coastalWet * 0.75);
    roughness = mix(roughness, 0.20, coastalWet * 0.82);
    if (u_cliffAridMode > 0.5) {
        // Clinging sea-foam & pale salt-crust collar in wet rock crevices right at the splash band
        float splashBand = 1.0 - smoothstep(0.0, 0.55, abs(worldPosition.y - (u_seaLevel + 0.15)));
        float rockFoam = splashBand * smoothstep(0.48, 0.82, triAlbedo.r * 0.65 + (1.0 - microCrevice) * 0.45);
        rockAlbedo = mix(rockAlbedo, vec3(0.92, 0.94, 0.96), clamp(rockFoam * 0.72, 0.0, 0.78));
    }

    // ── 5. Physically Grounded Lighting ──
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 worldLightDir = worldRot * lightDir;
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // Wrap diffuse lighting & shadow evaluation (view-space fragPosition/normal/lightDir matching u_lightVP)
    float NdotL = max((dot(normal, lightDir) + 0.22) * (1.0 / 1.22), 0.0);
    float directAtten = 0.0;
    if (NdotL > 0.0) {
        directAtten = MapShadowVisibility(fragPosition, normal, lightDir);
        if (directAtten > 0.001) {
            directAtten *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
        }
    }

    // GGX Specular highlight on dense granite quartz crystals
    float spec = 0.0;
    if (directAtten > 0.001) {
        vec3 halfSum = lightDir + viewDir;
        vec3 halfVec = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-8));
        float NdotH = max(dot(normal, halfVec), 0.0);
        float NdotH2 = NdotH * NdotH;
        float a = roughness * roughness;
        float a2 = max(a * a, 0.002);
        float denom = NdotH2 * (a2 - 1.0) + 1.0;
        float ggx = a2 / max(denom * denom * 3.14159265, 1e-4);
        spec = min(ggx * 0.18, 1.6) * (1.0 - roughness) * 0.28 * (1.0 - mossMask * 0.85);
    }

    // ── 6. Cloud Sea Up-Bounce (lush islands) or Turquoise Sea + Sky Bounce (coastal arid cliffs) ──
    float cloudSeaGate = 1.0 - step(0.5, u_cliffAridMode);
    float upwardCloudBounceFactor = max(-worldNormal.y * 0.65 + 0.20, 0.0) * cloudSeaGate;
    vec3 cloudLitColor = actualLight * vec3(0.50, 0.44, 0.36) + actualAmbient * vec3(0.85, 0.88, 0.92);
    vec3 cloudBounceLight = cloudLitColor * (upwardCloudBounceFactor * 1.45);
    vec3 seaBounce = (actualAmbient * vec3(0.55, 0.92, 0.96) + actualLight * vec3(0.24, 0.42, 0.40))
                   * max(-worldNormal.y * 0.45 + 0.42, 0.0) * (step(0.5, u_cliffAridMode) * 0.85);

    // Total Lighting Combination with World-Space Hemisphere Sky Irradiance
    vec3 hemisphereAmbient = Environment_HemisphereIrradiance(worldNormal, actualAmbient);
    vec3 diffuseLight = (hemisphereAmbient + cloudBounceLight + seaBounce) * cavityAO + actualLight * (NdotL * directAtten);
    vec3 finalRgb = rockAlbedo * diffuseLight + actualLight * (spec * directAtten);

    // Dynamic VFX point lights if active
    finalRgb += VFXLights_Accumulate(fragPosition, normal, rockAlbedo);

    // ── 7. Base Dissolving Cloud Haze (cloud-sea islands) or Coastal Aerial Perspective (arid) ──
    float baseHaze = clamp(fragColor.b, 0.0, 1.0) * cloudSeaGate;
    vec3 hazeTarget = (dot(u_hazeColor, u_hazeColor) > 0.001) ? u_hazeColor : vec3(0.82, 0.86, 0.92);
    finalRgb = mix(finalRgb, hazeTarget, baseHaze * 0.90);
    if (u_cliffAridMode > 0.5 && camDistSq > 1600.0) {
        float aerial = smoothstep(1600.0, 129600.0, camDistSq) * 0.42;
        vec3 coastalHaze = actualAmbient * vec3(0.88, 0.84, 0.78) + actualLight * vec3(0.28, 0.22, 0.16);
        finalRgb = mix(finalRgb, coastalHaze, aerial);
    }

    finalColor = vec4(finalRgb, 1.0);
}

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

    // Authentic mountain slate-granite palette (neutral gray with warm feldspar strata)
    vec3 slateBedrock = vec3(0.48, 0.49, 0.52);
    vec3 feldsparVein = vec3(0.62, 0.58, 0.52);
    vec3 bedrockTint = mix(slateBedrock, feldsparVein, combinedStrata * 0.38) * 1.32;

    vec3 rockBase = triAlbedo * bedrockTint * colDiffuse.rgb;

    // ── 3. Ledge Moss & Lichen Colonization (Where Winds Meet Style) ──
    // Upward-facing horizontal shelves receive rain-fed moss and alpine lichen
    float ledgeFacing = clamp(worldNormal.y * 1.45 - 0.18, 0.0, 1.0);
    float upperRimBoost = smoothstep(-8.0, -0.4, fragLocalY) * 0.25 + 0.75;
    float mossMask = fragColor.g * ledgeFacing * upperRimBoost;
    vec3 mossColor = mix(vec3(0.15, 0.25, 0.09), vec3(0.25, 0.38, 0.13), triAlbedo.g);
    vec3 rockAlbedo = mix(rockBase, mossColor, mossMask * 0.85);

    // ── 4. Deep Crevice Cavity Occlusion (80.lv RTGI Principle) ──
    // Intense occlusion in deep fissures and under shelf overhangs gives rock weight and depth
    float microCrevice = clamp(normX.z * triWeights.x + normY.z * triWeights.y + normZ.z * triWeights.z, 0.25, 1.0);
    float cavityAO = clamp(fragColor.r * (0.35 + 0.65 * microCrevice), 0.18, 1.0);

    // ── 5. Physically Grounded Lighting ──
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 worldLightDir = worldRot * lightDir;
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // Wrap diffuse lighting & shadow evaluation
    float NdotL = max((dot(normal, lightDir) + 0.22) * (1.0 / 1.22), 0.0);
    float directAtten = 0.0;
    if (NdotL > 0.0) {
        directAtten = MapShadowVisibility(worldPosition, worldNormal, worldLightDir);
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

    // ── 6. Cloud Sea Up-Bounce ──
    // Warm luminous bounce light radiating upwards from the sunlit cloud sea below onto undercuts
    float upwardCloudBounceFactor = max(-worldNormal.y * 0.65 + 0.20, 0.0);
    vec3 cloudLitColor = actualLight * vec3(0.50, 0.44, 0.36) + actualAmbient * vec3(0.85, 0.88, 0.92);
    vec3 cloudBounceLight = cloudLitColor * (upwardCloudBounceFactor * 1.45);

    // Total Lighting Combination with Hemisphere Sky Irradiance
    vec3 hemisphereAmbient = Environment_HemisphereIrradiance(normal, actualAmbient);
    vec3 diffuseLight = (hemisphereAmbient + cloudBounceLight) * cavityAO + actualLight * (NdotL * directAtten);
    vec3 finalRgb = rockAlbedo * diffuseLight + actualLight * (spec * directAtten);

    // Dynamic VFX point lights if active
    finalRgb += VFXLights_Accumulate(fragPosition, normal, rockAlbedo);

    // ── 7. Base Dissolving Cloud Haze ──
    float baseHaze = clamp(fragColor.b, 0.0, 1.0);
    vec3 hazeTarget = (dot(u_hazeColor, u_hazeColor) > 0.001) ? u_hazeColor : vec3(0.82, 0.86, 0.92);
    finalRgb = mix(finalRgb, hazeTarget, baseHaze * 0.90);

    finalColor = vec4(finalRgb, 1.0);
}

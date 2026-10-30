#version 330
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

// ============================================================
// WUXING — cliff_rock Fragment Shader
// Renders sculpted island cliff skirt with cavity AO,
// ledge moss colonization, cloud up-bounce, and base haze.
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
    // [VULKAN / SPIR-V FIX]: Compute screen derivatives in uniform control flow before branches
    vec2 uvDx = dFdx(fragTexCoord);
    vec2 uvDy = dFdy(fragTexCoord);
    vec3 dPdx = dFdx(fragPosition);
    vec3 dPdy = dFdy(fragPosition);

    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    float camDist = camDistSq * invCamDist;
    vec3 viewDir = toCam * invCamDist;

    vec4 albedo = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
    albedo.rgb *= colDiffuse.rgb;
    vec3 normalSample = textureGrad(texture2, fragTexCoord, uvDx, uvDy).rgb * 2.0 - 1.0;

    // Dual-scale mineral detail at close proximity (< 22m)
    if (camDistSq < 484.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 484.0)) * 0.38;
        vec2 detailUV = fragTexCoord * 3.4;
        vec2 detailDx = uvDx * 3.4;
        vec2 detailDy = uvDy * 3.4;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        vec3 mixedNorm = mix(normalSample, detailNorm, detailWeight);
        normalSample = mixedNorm * inversesqrt(max(dot(mixedNorm, mixedNorm), 1e-8));
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        albedo.rgb *= mix(vec3(1.0), detailAlb * 1.25, detailWeight * 0.32);
    }

    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));

    // Construct robust orthonormal TBN
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
    vec3 B = cross(smoothN, T);
    mat3 TBN = mat3(T, B, smoothN);
    normalSample.xy *= 0.75;
    normalSample *= inversesqrt(max(dot(normalSample, normalSample), 1e-8));
    vec3 rawN = TBN * normalSample;
    vec3 normal = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));
    vec3 rawWorldN = mat3(u_cloudWorldFromShader) * normal;
    vec3 worldNormal = rawWorldN * inversesqrt(max(dot(rawWorldN, rawWorldN), 1e-8));

    float roughness = clamp(textureGrad(texture3, fragTexCoord, uvDx, uvDy).r, 0.12, 0.95);

    // Light direction and colors
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 rawWorldL = mat3(u_cloudWorldFromShader) * lightDir;
    vec3 worldLightDir = rawWorldL * inversesqrt(max(dot(rawWorldL, rawWorldL), 1e-8));
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // 1. Natural Granite & Slate Geological Strata Tone
    float strataCoord = worldPosition.y * 0.28 + sin(worldPosition.x * 0.08 + worldPosition.z * 0.06) * 1.2;
    float strataBand = sin(strataCoord * 3.14159) * 0.5 + 0.5;
    vec3 rockBase = albedo.rgb;
    // Modulate with vertex strata shade
    rockBase *= mix(vec3(0.86, 0.88, 0.92), vec3(1.08, 1.04, 0.98), fragColor.a);
    rockBase *= mix(vec3(0.92, 0.92, 0.90), vec3(1.06, 1.03, 0.97), strataBand * 0.25);

    // 2. Ledge Moss & Lichen Colonization
    // Top-facing shelves near the plateau catch lush green velvet moss
    float ledgeFacing = clamp(worldNormal.y * 1.35 - 0.15, 0.0, 1.0);
    float mossMask = fragColor.g * ledgeFacing;
    vec3 mossColor = mix(vec3(0.14, 0.22, 0.09), vec3(0.24, 0.36, 0.14), albedo.g);
    vec3 rockAlbedo = mix(rockBase, mossColor, mossMask * 0.82);

    // 3. Baked Cavity Ambient Occlusion
    float cavityAO = clamp(fragColor.r, 0.22, 1.0);

    // 4. Wrap diffuse lighting
    float NdotL = max((dot(normal, lightDir) + 0.25) * 0.80, 0.0);
    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    if (shadow > 0.001) {
        shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
    }
    float directAtten = shadow;

    // Specular highlight on dense granite quartz crystals
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specPower = mix(18.0, 72.0, 1.0 - roughness);
    float spec = pow(NdotH, specPower) * (1.0 - roughness) * 0.28 * (1.0 - mossMask * 0.85);

    // 5. Cloud Up-Bounce (Warm luminous bounce light radiating from the cloud sea below!)
    float upwardCloudBounceFactor = max(-worldNormal.y * 0.65 + 0.20, 0.0);
    vec3 cloudBounceLight = vec3(0.88, 0.86, 0.82) * actualAmbient * upwardCloudBounceFactor * 1.65;

    // 6. Total Lighting Combination
    vec3 diffuseLight = actualAmbient * cavityAO + actualLight * (NdotL * directAtten) + cloudBounceLight * cavityAO;
    vec3 finalRgb = rockAlbedo * diffuseLight + actualLight * spec * directAtten;

    // Add dynamic VFX point lights if active
    finalRgb += VFXLights_Accumulate(fragPosition, normal, rockAlbedo);

    // 7. Base Dissolving Cloud Haze (Bottom of cliff seamlessly merges into the cloud sea)
    float baseHaze = clamp(fragColor.b, 0.0, 1.0);
    vec3 hazeTarget = (dot(u_hazeColor, u_hazeColor) > 0.001) ? u_hazeColor : vec3(0.82, 0.86, 0.92);
    finalRgb = mix(finalRgb, hazeTarget, baseHaze * 0.88);

    finalColor = vec4(finalRgb, 1.0);
}

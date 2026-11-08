#version 330
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_lights.glsl"
#define MAP_SHADOW_NORMAL_OFFSET_TEXELS 2.0
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
    // [VULKAN / SPIR-V FIX]: Compute UV derivatives in uniform control flow before branches
    vec2 uvDx = dFdx(fragTexCoord);
    vec2 uvDy = dFdy(fragTexCoord);

    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    vec3 viewDir = toCam * invCamDist;

    vec4 albedo = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
    // Authentic mountain cliff bedrock: neutral slate/granite tone matching Where Winds Meet & Ghost of Tsushima
    vec3 bedrockTint = vec3(0.52, 0.50, 0.48);
    albedo.rgb = albedo.rgb * bedrockTint * 1.35;
    albedo.rgb *= colDiffuse.rgb;
    vec3 normalSample = textureGrad(texture2, fragTexCoord, uvDx, uvDy).rgb * 2.0 - 1.0;

    // Dual-scale mineral detail at close proximity (< 22m)
    if (camDistSq < 484.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 484.0)) * 0.35;
        vec2 detailUV = fragTexCoord * 3.4;
        vec2 detailDx = uvDx * 3.4;
        vec2 detailDy = uvDy * 3.4;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        normalSample = mix(normalSample, detailNorm, detailWeight);
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        albedo.rgb *= mix(vec3(1.0), clamp(detailAlb * 1.1 + 0.12, 0.50, 1.25), detailWeight * 0.22);
    }

    mat3 worldRot = mat3(u_cloudWorldFromShader);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));

    // Construct robust orthonormal TBN preserving bitangent handedness
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

    normalSample.xy *= 1.65;
    normalSample *= inversesqrt(max(dot(normalSample, normalSample), 1e-8));
    // T, B, smoothN are mutually orthonormal and normalSample is unit length
    vec3 normal = T * normalSample.x + B * normalSample.y + smoothN * normalSample.z;
    vec3 worldNormal = worldRot * normal;

    float roughness = clamp(textureGrad(texture3, fragTexCoord, uvDx, uvDy).r, 0.12, 0.95);

    // Light direction and colors
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 worldLightDir = worldRot * lightDir;
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // 1. Natural Granite Geological Strata Tone & Rain Drainage Streaks
    float strataMacro = sin(worldPosition.y * 1.25 + sin(worldPosition.x * 0.15 + worldPosition.z * 0.12) * 1.8) * 0.5 + 0.5;
    float strataShelf = sin(worldPosition.y * 3.40 + normalSample.x * 1.2) * 0.5 + 0.5;
    float strataMicro = sin(worldPosition.y * 8.50 + normalSample.y * 0.8) * 0.5 + 0.5;
    float combinedStrata = strataMacro * 0.50 + strataShelf * 0.35 + strataMicro * 0.15;

    float drainStreak = sin(worldPosition.x * 0.35 + sin(worldPosition.z * 0.25) * 1.4) * 0.5 + 0.5;
    vec3 streakColor = mix(vec3(0.92, 0.90, 0.88), vec3(1.06, 1.02, 0.96), drainStreak);

    vec3 rockBase = albedo.rgb * streakColor;
    rockBase *= mix(vec3(0.88, 0.90, 0.94), vec3(1.06, 1.03, 0.98), fragColor.a);
    rockBase *= mix(vec3(0.85, 0.83, 0.80), vec3(1.15, 1.10, 1.02), combinedStrata * 0.35);

    // 1b. Sub-Soil Regolith Band (warm earthy loam transition directly below the turf lip)
    float cliffArcDepth = fragTexCoord.y * (1.0 / 0.65);
    float soilBand = smoothstep(1.8, 0.3, cliffArcDepth) * (1.0 - smoothstep(0.4, 0.0, cliffArcDepth));
    vec3 subSoilLoam = mix(vec3(0.24, 0.20, 0.15), vec3(0.32, 0.26, 0.19), combinedStrata);
    rockBase = mix(rockBase, subSoilLoam, soilBand * 0.30);

    // Turf overhang fringe: only the topmost 25cm collar tucked under the plateau meadow
    float turfLip = smoothstep(0.35, 0.05, cliffArcDepth);
    vec3 turfColor = mix(vec3(0.14, 0.24, 0.09), vec3(0.22, 0.32, 0.11), drainStreak);
    rockBase = mix(rockBase, turfColor, turfLip * 0.65 * clamp(worldNormal.y * 1.6 + 0.4, 0.0, 1.0));

    // 2. Ledge Moss & Lichen Colonization (enhanced near upper rim via fragLocalY)
    float upperRimBoost = smoothstep(-6.0, -0.5, fragLocalY) * 0.15 + 0.85;
    float ledgeFacing = clamp(worldNormal.y * 1.35 - 0.15, 0.0, 1.0);
    float mossMask = fragColor.g * ledgeFacing * upperRimBoost;
    vec3 mossColor = mix(vec3(0.14, 0.22, 0.09), vec3(0.24, 0.36, 0.14), albedo.g);
    vec3 rockAlbedo = mix(rockBase, mossColor, mossMask * 0.82);

    // 3. Baked Cavity Ambient Occlusion (clamped to prevent crushing shaded facets to black)
    float cavityAO = clamp(fragColor.r, 0.32, 1.0);
    cavityAO = mix(cavityAO * 0.70, cavityAO, clamp(normalSample.z * 0.8 + 0.2, 0.0, 1.0));

    // 4. Wrap diffuse lighting & shadow evaluation
    float NdotL = max((dot(normal, lightDir) + 0.25) * 0.80, 0.0);
    float directAtten = 0.0;
    if (NdotL > 0.0) {
        directAtten = MapShadowVisibility(worldPosition, worldNormal, worldLightDir);
        if (directAtten > 0.001) {
            directAtten *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
        }
    }

    // Specular highlight on dense granite quartz crystals (only evaluated when lit)
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
        spec = min(ggx * 0.18, 1.8) * (1.0 - roughness) * 0.28 * (1.0 - mossMask * 0.85);
    }

    // 5. Cloud Up-Bounce (Warm luminous bounce light radiating from the sunlit cloud sea below)
    float upwardCloudBounceFactor = max(-worldNormal.y * 0.65 + 0.20, 0.0);
    vec3 cloudLitColor = actualLight * vec3(0.50, 0.42, 0.34) + actualAmbient * vec3(0.85, 0.88, 0.92);
    vec3 cloudBounceLight = cloudLitColor * (upwardCloudBounceFactor * 1.45);

    // 6. Total Lighting Combination with Hemisphere Ambient
    vec3 hemisphereAmbient = Environment_HemisphereIrradiance(normal, actualAmbient);
    vec3 diffuseLight = (hemisphereAmbient + cloudBounceLight) * cavityAO + actualLight * (NdotL * directAtten);
    vec3 finalRgb = rockAlbedo * diffuseLight + actualLight * (spec * directAtten);

    // Add dynamic VFX point lights if active
    finalRgb += VFXLights_Accumulate(fragPosition, normal, rockAlbedo);

    // 7. Base Dissolving Cloud Haze (Bottom of cliff seamlessly merges into the cloud sea)
    float baseHaze = clamp(fragColor.b, 0.0, 1.0);
    vec3 hazeTarget = (dot(u_hazeColor, u_hazeColor) > 0.001) ? u_hazeColor : vec3(0.82, 0.86, 0.92);
    finalRgb = mix(finalRgb, hazeTarget, baseHaze * 0.88);

    finalColor = vec4(finalRgb, 1.0);
}
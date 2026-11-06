#version 330
#include "core/shaders/common/lighting.glsl"
// Đợt E / E2 — spell/effect point lights. Same shared block as the ground and
// the character so a fireball agrees with every surface it stands on.
#include "core/shaders/common/vfx_lights.glsl"
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
uniform sampler2D u_cloudNoise;
uniform mat4 u_cloudWorldFromShader;

out vec4 finalColor;

void main() {
    // [VULKAN / SPIR-V FIX]: Tính trước đạo hàm UV trong luồng đồng nhất
    vec2 uvDx = dFdx(fragTexCoord);
    vec2 uvDy = dFdy(fragTexCoord);

    // [TỐI ƯU ALU]: Tính khoảng cách và hướng nhìn camera chỉ với 1 lệnh inversesqrt duy nhất
    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    float camDist = camDistSq * invCamDist;
    vec3 viewDir = toCam * invCamDist;

    vec4 albedo = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
    // Natural mineral lithology calibration: preserve genuine granite, river stone & scree tones
    albedo.rgb = mix(albedo.rgb, vec3(dot(albedo.rgb, vec3(0.3333333))), 0.08) * 0.92;
    albedo.rgb *= colDiffuse.rgb;
    vec3 normalSample = textureGrad(texture2, fragTexCoord, uvDx, uvDy).rgb * 2.0 - 1.0;

    // High-frequency crystalline mineral grain & micro-facet detail on close zoom (< 18m)
    if (camDistSq < 324.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 324.0)) * 0.42;
        vec2 detailUV = fragTexCoord * 2.8;
        vec2 detailDx = uvDx * 2.8;
        vec2 detailDy = uvDy * 2.8;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        normalSample = mix(normalSample, detailNorm, detailWeight);
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        albedo.rgb *= mix(vec3(1.0), detailAlb * 1.32, detailWeight * 0.38);
    }

    mat3 worldRot = mat3(u_cloudWorldFromShader);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float isRiverStone = (colDiffuse.a < 0.985) ? 1.0 : smoothstep(0.01, 0.05, colDiffuse.b - colDiffuse.r);

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

    normalSample.xy *= 1.25;
    normalSample *= inversesqrt(max(dot(normalSample, normalSample), 1e-8));
    vec3 normal = T * normalSample.x + B * normalSample.y + smoothN * normalSample.z;
    vec3 worldNormal = worldRot * normal;

    float roughness = clamp(textureGrad(texture3, fragTexCoord, uvDx, uvDy).r, 0.08, 1.0);

    // Safe fallback if a map forgot to push PropLit_UpdateLighting()
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 worldLightDir = worldRot * lightDir;
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // Warm mineral & geological strata variation while preserving true lithology tones
    float strataNoise = sin(worldPosition.y * 3.8 + worldPosition.x * 0.45 - worldPosition.z * 0.35) * 0.5 + 0.5;
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.95, 0.94, 0.92), vec3(1.08, 1.04, 0.98), strataNoise);

    // Top-surface moss & lichen colonization harmonized with song_quao meadow palette
    float altitudeMoss = (1.0 - smoothstep(9.5, 13.5, worldPosition.y)) * 0.40 + 0.60;
    float topMossMask = smoothstep(0.50, 0.80, worldNormal.y) * smoothstep(-0.18, 0.38, fragLocalY)
                      * smoothstep(0.28, 0.72, albedo.g) * altitudeMoss;
    topMossMask *= (1.0 - isRiverStone * 0.95);
    vec3 crownMossAlbedo = mix(vec3(0.18, 0.32, 0.11), vec3(0.30, 0.46, 0.16), albedo.r);
    vec3 lichenTone = vec3(0.50, 0.48, 0.22);
    crownMossAlbedo = mix(crownMossAlbedo, lichenTone, smoothstep(0.48, 0.86, albedo.r));
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * 0.78);

    // Lower skirt earthy moss & turf grounding blend matching meadow roots
    float skirtMask = (1.0 - smoothstep(-0.42, 0.16, fragLocalY));
    skirtMask *= (1.0 - isRiverStone * 0.90);
    vec3 skirtMossAlbedo = mix(vec3(0.16, 0.28, 0.10), vec3(0.26, 0.42, 0.14), clamp(worldNormal.y, 0.0, 1.0));
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.68);

    // Authentic water-tumbled river cobble biofilm, diatom patina & wet meniscus
    float riverWetBase = 1.0 - smoothstep(-0.48, 0.08, fragLocalY);
    float diatomMask = isRiverStone * smoothstep(0.22, 0.85, worldNormal.y) * smoothstep(-0.16, 0.38, fragLocalY);
    vec3 diatomPatina = mix(vec3(0.38, 0.42, 0.24), vec3(0.48, 0.46, 0.22), albedo.g);
    rockAlbedo = mix(rockAlbedo, diatomPatina, diatomMask * 0.45);
    rockAlbedo *= mix(1.0, 0.72, isRiverStone * riverWetBase * 0.58);

    // Ground & sand contact darkening at the base of every rock
    float skirtContact = 1.0 - smoothstep(-0.52, 0.02, fragLocalY);
    rockAlbedo *= mix(1.0, 0.68, skirtContact * (isRiverStone > 0.5 ? 0.48 : 0.52));

    albedo.rgb = rockAlbedo;
    roughness = mix(roughness, 0.86, max(topMossMask, skirtMask) * 0.78);
    roughness = mix(roughness, 0.28, isRiverStone);
    roughness = mix(roughness, 0.16, isRiverStone * riverWetBase * 0.85);

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
        float shininess    = mix(32.0, 8.0, roughness);
        float specStrength = mix(0.04, 0.14, 1.0 - roughness);
        specStrength = mix(specStrength, 0.26, isRiverStone * riverWetBase * 0.75);
        spec = calcSpecular(normal, modelLightDir, viewDir, shininess) * specStrength;
    }

    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient);
    // Warm meadow turf bounce into lower rock facets
    vec3 turfBounce = vec3(0.20, 0.32, 0.12) * actualAmbient * max(-worldNormal.y * 0.5 + 0.5, 0.0) * 0.45;
    ambient += turfBounce;
    if (isRiverStone > 0.5) {
        vec3 streamBounce = actualAmbient * vec3(0.85, 1.08, 1.15) + vec3(0.04, 0.09, 0.10);
        ambient = mix(ambient, streamBounce, 0.35);
    }
    float groundAO = clamp(0.82 + worldPosition.y * 0.20, 0.78, 1.0);
    float skirtAO = clamp(0.54 + 0.46 * smoothstep(-0.50, 0.12, fragLocalY), 0.54, 1.0);
    float creviceAO = clamp(normalSample.z * 1.30 - 0.22, 0.52, 1.0);
    vec3 lit = albedo.rgb * (ambient * groundAO * skirtAO * creviceAO + diff * actualLight * shadow)
             + spec * actualLight * shadow;

    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    // Atmospheric aerial haze for distant mountain crags
    float haze = smoothstep(38.0, 115.0, camDist) * 0.30;
    vec3 hazeColor = actualAmbient * 1.35;
    lit = mix(lit, hazeColor, haze);

    finalColor = vec4(lit, albedo.a * colDiffuse.a);
}
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
    // [VULKAN / SPIR-V FIX]: Tính trước đạo hàm không gian màn hình trong luồng đồng nhất
    // để dùng cho textureGrad trong nhánh chi tiết cận cảnh (camDistSq < 324.0)
    vec2 uvDx = dFdx(fragTexCoord);
    vec2 uvDy = dFdy(fragTexCoord);
    vec3 dPdx = dFdx(fragPosition);
    vec3 dPdy = dFdy(fragPosition);

    // [TỐI ƯU ALU]: Tính khoảng cách và hướng nhìn camera chỉ với 1 lệnh inversesqrt duy nhất
    // (loại bỏ 2 lần gọi normalize/length dư thừa ở cuối shader)
    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    float camDist = camDistSq * invCamDist;
    vec3 viewDir = toCam * invCamDist;

    vec4 albedo = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
    albedo.rgb *= colDiffuse.rgb;
    vec3 normalSample = textureGrad(texture2, fragTexCoord, uvDx, uvDy).rgb * 2.0 - 1.0;

    // High-frequency crystalline mineral grain & micro-facet detail on close zoom (< 18m)
    if (camDistSq < 324.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 324.0)) * 0.42;
        vec2 detailUV = fragTexCoord * 2.8;
        vec2 detailDx = uvDx * 2.8;
        vec2 detailDy = uvDy * 2.8;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        vec3 mixedNorm = mix(normalSample, detailNorm, detailWeight);
        normalSample = mixedNorm * inversesqrt(max(dot(mixedNorm, mixedNorm), 1e-8));
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        albedo.rgb *= mix(vec3(1.0), detailAlb * 1.32, detailWeight * 0.38);
    }

    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float isRiverStone = (colDiffuse.a < 0.985) ? 1.0 : smoothstep(0.01, 0.05, colDiffuse.b - colDiffuse.r);

    // [BUG FIX]: Bảo vệ chống suy biến vector tiếp tuyến T (NaN) khi smoothN trùng phương với fragTangent
    // hoặc khi mesh procedural quên gọi GenMeshTangents()
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
    normalSample.xy *= 1.35;
    normalSample *= inversesqrt(max(dot(normalSample, normalSample), 1e-8));
    vec3 rawN = TBN * normalSample;
    vec3 normal = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));
    vec3 rawWorldN = mat3(u_cloudWorldFromShader) * normal;
    vec3 worldNormal = rawWorldN * inversesqrt(max(dot(rawWorldN, rawWorldN), 1e-8));

    float roughness = clamp(textureGrad(texture3, fragTexCoord, uvDx, uvDy).r, 0.08, 1.0);

    // Safe fallback if a map forgot to push PropLit_UpdateLighting()
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 rawWorldL = mat3(u_cloudWorldFromShader) * lightDir;
    vec3 worldLightDir = rawWorldL * inversesqrt(max(dot(rawWorldL, rawWorldL), 1e-8));
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    // Warm mineral & geological strata variation while preserving true lithology tones
    float strataNoise = sin(worldPosition.y * 3.8 + worldPosition.x * 0.45 - worldPosition.z * 0.35) * 0.5 + 0.5;
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.92, 0.92, 0.90), vec3(1.05, 1.02, 0.98), strataNoise);

    // Top-surface moss & lichen colonization across meadow plateau
    // Fades smoothly at extreme alpine heights (y > 11m)
    float altitudeMoss = (1.0 - smoothstep(9.5, 13.5, worldPosition.y)) * 0.40 + 0.60;
    float topMossMask = smoothstep(0.55, 0.82, worldNormal.y) * smoothstep(-0.15, 0.40, fragLocalY)
                      * smoothstep(0.32, 0.75, albedo.g) * altitudeMoss;
    topMossMask *= (1.0 - isRiverStone * 0.95);
    vec3 crownMossAlbedo = mix(vec3(0.12, 0.22, 0.07), vec3(0.22, 0.34, 0.10), albedo.r);
    vec3 lichenTone = vec3(0.48, 0.42, 0.18);
    crownMossAlbedo = mix(crownMossAlbedo, lichenTone, smoothstep(0.45, 0.85, albedo.r));
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * 0.75);

    // Lower skirt earthy moss & turf grounding blend
    float skirtMask = (1.0 - smoothstep(-0.45, 0.12, fragLocalY));
    skirtMask *= (1.0 - isRiverStone * 0.90);
    vec3 skirtMossAlbedo = vec3(0.14, 0.20, 0.08);
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.72);

    // Deep soil capillary moisture & contact ambient occlusion at base
    float skirtContact = (1.0 - smoothstep(-0.50, -0.05, fragLocalY)) * (1.0 - isRiverStone * 0.50);
    rockAlbedo *= mix(1.0, 0.62, skirtContact * 0.55);

    // River stone & shoreline wetness darkening at water level
    // [SPIR-V FIX]: Sửa lỗi smoothstep(0.35, -0.05, worldPosition.y) (undefined behavior khi edge0 > edge1)
    // thành (1.0 - smoothstep(-0.05, 0.35, worldPosition.y))
    float waterDamp = (1.0 - smoothstep(-0.05, 0.35, worldPosition.y))
                    * (1.0 - smoothstep(-0.85, -0.30, worldPosition.y));
    rockAlbedo *= mix(1.0, 0.88, waterDamp * 0.35);

    albedo.rgb = rockAlbedo;
    roughness = mix(roughness, 0.86, max(topMossMask, skirtMask) * 0.78);
    // River stones have natural matte velvety freshwater wetness (roughness ~0.52, NOT mirror glass)
    roughness = mix(roughness, 0.52, max(waterDamp * 0.50, isRiverStone * 0.65));

    // Preserve lateral facet contrast even when the sun is near zenith (12:00 Noon)
    vec3 rawModelL = lightDir + vec3(0.32, -0.18, -0.26) * max(worldLightDir.y - 0.50, 0.0);
    vec3 modelLightDir = rawModelL * inversesqrt(max(dot(rawModelL, rawModelL), 1e-8));
    float diff = calcDiffuse(normal, modelLightDir, 0.0);
    float shininess    = mix(24.0, 8.0, roughness);
    float specStrength = mix(0.03, 0.10, 1.0 - roughness);
    specStrength = mix(specStrength, 0.08, max(waterDamp * 0.40, isRiverStone * 0.50));
    float spec = calcSpecular(normal, modelLightDir, viewDir, shininess) * specStrength;

    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    if (shadow > 0.001) {
        shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
    }

    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient);
    float groundAO = clamp(0.76 + worldPosition.y * 0.30, 0.72, 1.0);
    float skirtAO = (isRiverStone > 0.5)
        ? clamp(0.72 + 0.28 * smoothstep(-0.65, -0.15, fragLocalY), 0.70, 1.0)
        : clamp(0.30 + 0.70 * smoothstep(-0.45, 0.18, fragLocalY), 0.28, 1.0);
    vec3 lit = albedo.rgb * (ambient * groundAO * skirtAO + diff * actualLight * shadow)
             + spec * actualLight * shadow;

    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    // Atmospheric aerial haze for distant mountain crags (tái sử dụng camDist đã tính ở đầu hàm)
    float haze = smoothstep(38.0, 115.0, camDist) * 0.30;
    vec3 hazeColor = actualAmbient * 1.35;
    lit = mix(lit, hazeColor, haze);

    finalColor = vec4(lit, albedo.a * colDiffuse.a);
}

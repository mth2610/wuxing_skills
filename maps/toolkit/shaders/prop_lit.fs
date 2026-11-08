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
uniform sampler2D u_cloudNoise;
uniform mat4 u_cloudWorldFromShader;

out vec4 finalColor;

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

    // Biplanar texture projection (Quilez): 2 taps instead of 3, eliminates UV stretching across polyhedral facets
    vec3 absWorldN = abs(worldSmoothN);
    float biplanarTiling = (isRiverStone > 0.5) ? 0.35 : 0.28;
    vec2 uvY = worldPosition.xz * biplanarTiling;
    vec2 uvH = (absWorldN.x > absWorldN.z) ? worldPosition.zy * biplanarTiling : worldPosition.xy * biplanarTiling;
    float wY = clamp((absWorldN.y - 0.35) * (1.0 / 0.50), 0.0, 1.0);
    float wH = 1.0 - wY;

    vec2 uvDxY = dFdx(uvY), uvDyY = dFdy(uvY);
    vec2 uvDxH = dFdx(uvH), uvDyH = dFdy(uvH);

    vec4 texY = textureGrad(texture0, uvY, uvDxY, uvDyY);
    vec4 texH = textureGrad(texture0, uvH, uvDxH, uvDyH);
    vec4 albedo = (texY * wY + texH * wH);

    // Natural mineral lithology calibration: preserve genuine granite, river stone & scree tones
    albedo.rgb = mix(albedo.rgb, vec3(dot(albedo.rgb, vec3(0.3333333))), 0.06) * 0.95;
    albedo.rgb *= colDiffuse.rgb;

    vec3 normY = textureGrad(texture2, uvY, uvDxY, uvDyY).rgb * 2.0 - 1.0;
    vec3 normH = textureGrad(texture2, uvH, uvDxH, uvDyH).rgb * 2.0 - 1.0;
    vec3 normalSample = normY * wY + normH * wH;

    // High-frequency crystalline mineral grain & micro-facet detail on close zoom (< 18m)
    if (camDistSq < 324.0) {
        float detailWeight = (1.0 - camDistSq * (1.0 / 324.0)) * 0.38;
        vec2 detailUV = uvH * 3.2;
        vec2 detailDx = uvDxH * 3.2;
        vec2 detailDy = uvDyH * 3.2;
        vec3 detailNorm = textureGrad(texture2, detailUV, detailDx, detailDy).rgb * 2.0 - 1.0;
        normalSample = mix(normalSample, detailNorm, detailWeight);
        vec3 detailAlb = textureGrad(texture0, detailUV, detailDx, detailDy).rgb;
        albedo.rgb *= mix(vec3(1.0), detailAlb * 1.28, detailWeight * 0.35);
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

    // Coherent world-space geological strata variation matching island bedrock
    float strataNoise = sin(worldPosition.y * 3.8 + sin(worldPosition.x * 0.28 + worldPosition.z * 0.22) * 1.35) * 0.5 + 0.5;
    float fineStrata = sin(worldPosition.y * 12.0 + normalSample.x * 1.8) * 0.5 + 0.5;
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.88, 0.86, 0.84), vec3(1.10, 1.05, 0.98), strataNoise * 0.70 + fineStrata * 0.30);

    // Top-surface moss & lichen colonization harmonized with song_quao meadow palette
    float altitudeMoss = (1.0 - smoothstep(9.5, 13.5, worldPosition.y)) * 0.40 + 0.60;
    float topMossMask = smoothstep(0.48, 0.78, worldNormal.y) * smoothstep(-0.18, 0.38, fragLocalY)
                      * smoothstep(0.22, 0.68, albedo.g) * altitudeMoss;
    topMossMask *= (1.0 - isRiverStone * 0.90);
    vec3 crownMossAlbedo = mix(vec3(0.15, 0.28, 0.09), vec3(0.28, 0.44, 0.14), albedo.r);
    vec3 lichenTone = vec3(0.52, 0.49, 0.22);
    crownMossAlbedo = mix(crownMossAlbedo, lichenTone, smoothstep(0.46, 0.84, albedo.r));
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * 0.84);

    // Lower skirt earthy moss & turf grounding blend matching meadow roots & dark loam
    float skirtMask = (1.0 - smoothstep(-0.46, 0.22, fragLocalY));
    skirtMask *= (1.0 - isRiverStone * 0.85);
    vec3 skirtLoam = vec3(0.15, 0.13, 0.09);
    vec3 skirtMossAlbedo = mix(skirtLoam, vec3(0.22, 0.36, 0.12), clamp(worldNormal.y * 1.15, 0.0, 1.0));
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.78);

    // Authentic water-tumbled river boulder biofilm, diatom patina & wet waterline meniscus
    float wetBase = 0.0;
    float splashHalo = 0.0;
    if (isRiverStone > 0.5) {
        wetBase = 1.0 - smoothstep(0.08, 0.38, fragLocalY);
        splashHalo = smoothstep(0.06, 0.36, fragLocalY) * (1.0 - smoothstep(0.34, 0.52, fragLocalY));

        // Darken porous wet stone by 38%
        rockAlbedo *= mix(1.0, 0.62, wetBase * 0.88);

        // Diatom algae patina: velvety greenish-gold biofilm in the damp splash zone
        float diatomMask = smoothstep(0.18, 0.75, worldNormal.y) * (splashHalo * 0.85 + wetBase * 0.35);
        vec3 diatomPatina = mix(vec3(0.22, 0.32, 0.14), vec3(0.36, 0.42, 0.18), albedo.g);
        rockAlbedo = mix(rockAlbedo, diatomPatina, diatomMask * 0.68);

        // High gloss wet specular reflection on wet areas
        roughness = mix(roughness, 0.08, wetBase * 0.92);
        roughness = mix(roughness, 0.22, splashHalo * 0.70);
    }

    // Ground & sand contact darkening at the base of every rock so rocks sit firmly in the earth
    float skirtContact = 1.0 - smoothstep(-0.62, 0.10, fragLocalY);
    rockAlbedo *= mix(1.0, 0.52, skirtContact * (isRiverStone > 0.5 ? 0.45 : 0.65));
    float contactAO = clamp(0.42 + 0.58 * smoothstep(-0.60, 0.15, fragLocalY), 0.42, 1.0);

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

    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient);
    // Warm meadow turf bounce into lower rock facets
    vec3 turfBounce = vec3(0.20, 0.32, 0.12) * actualAmbient * max(-worldNormal.y * 0.5 + 0.5, 0.0) * 0.45;
    ambient = (ambient + turfBounce) * contactAO;
    if (isRiverStone > 0.5) {
        vec3 streamBounce = actualAmbient * vec3(0.85, 1.12, 1.20) + vec3(0.04, 0.10, 0.12);
        ambient = mix(ambient, streamBounce, 0.42 * wetBase);
    }
    float groundAO = clamp(worldPosition.y * 0.35 + 0.75, 0.40, 1.0);
    float skirtAO = clamp(0.54 + 0.46 * smoothstep(-0.50, 0.12, fragLocalY), 0.54, 1.0);
    float creviceAO = clamp(normalSample.z * 1.30 - 0.22, 0.52, 1.0);
    vec3 lit = rockAlbedo * (ambient * groundAO * skirtAO * creviceAO + diff * actualLight * shadow)
             + spec * actualLight * shadow;

    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    // Atmospheric aerial haze for distant mountain crags
    float haze = smoothstep(38.0, 115.0, camDist) * 0.30;
    vec3 hazeColor = actualAmbient * 1.35;
    lit = mix(lit, hazeColor, haze);

    finalColor = vec4(lit, albedo.a * colDiffuse.a);
}
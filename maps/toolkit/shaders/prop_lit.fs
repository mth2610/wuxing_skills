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
    vec4 albedo = texture(texture0, fragTexCoord);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));

    vec3 normalSample = texture(texture2, fragTexCoord).rgb * 2.0 - 1.0;
    vec3 smoothN = normalize(fragNormal);
    // Blend geometric triangle facet normal so rocks exhibit crisp, chiseled planar cleavage faces
    vec3 dPdx = dFdx(fragPosition);
    vec3 dPdy = dFdy(fragPosition);
    vec3 facetN = cross(dPdx, dPdy);
    float facetLenSq = dot(facetN, facetN);
    if (facetLenSq > 1e-10) {
        facetN *= inversesqrt(facetLenSq);
        if (dot(facetN, smoothN) < 0.0) facetN = -facetN;
        smoothN = normalize(mix(smoothN, facetN, 0.76));
    }
    mat3 TBN = mat3(normalize(fragTangent), normalize(fragBitangent), smoothN);
    vec3 normal = normalize(TBN * normalSample);
    vec3 worldNormal = normalize(mat3(u_cloudWorldFromShader) * normal);

    float roughness = clamp(texture(texture3, fragTexCoord).r, 0.08, 1.0);

    // Safe fallback if a map forgot to push PropLit_UpdateLighting()
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : normalize(vec3(0.25, 0.75, -0.55));
    vec3 worldLightDir = normalize(mat3(u_cloudWorldFromShader) * lightDir);
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    actualLight *= 1.0 / (1.0 + max(worldLightDir.y - 0.36, 0.0) * 0.82);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);
    vec3 viewDir  = normalize(u_viewPos - fragPosition);

    // Warm mineral & geological strata variation so boulders never look like monochrome grey plastic
    float strataNoise = sin(worldPosition.y * 3.8 + worldPosition.x * 0.45 - worldPosition.z * 0.35) * 0.5 + 0.5;
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.76, 0.74, 0.70), vec3(1.04, 0.98, 0.88), strataNoise);

    // Top-surface moss & lichen colonization on upward-facing boulder crowns
    float topMossMask = smoothstep(0.36, 0.70, worldNormal.y) * smoothstep(-0.10, 0.35, fragLocalY)
                      * smoothstep(0.18, 0.65, albedo.g);
    vec3 crownMossAlbedo = mix(vec3(0.14, 0.24, 0.08), vec3(0.25, 0.36, 0.12), albedo.r);
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * 0.78);

    // Lower skirt earthy moss & turf grounding blend
    float skirtMask = (1.0 - smoothstep(-0.35, 0.15, fragLocalY));
    vec3 skirtMossAlbedo = vec3(0.15, 0.22, 0.09);
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.72);

    // River stone & shoreline wetness darkening at water level
    float waterDamp = smoothstep(0.35, -0.05, worldPosition.y) * (1.0 - smoothstep(-0.85, -0.30, worldPosition.y));
    rockAlbedo *= mix(1.0, 0.65, waterDamp * 0.85);

    albedo.rgb = rockAlbedo;
    roughness = mix(roughness, 0.86, max(topMossMask, skirtMask) * 0.78);
    roughness = mix(roughness, 0.16, waterDamp * 0.75);

    // Preserve lateral facet contrast even when the sun is near zenith (12:00 Noon)
    vec3 modelLightDir = normalize(lightDir + vec3(0.32, -0.18, -0.26) * max(worldLightDir.y - 0.50, 0.0));
    float diff = calcDiffuse(normal, modelLightDir, 0.0);
    float shininess    = mix(72.0, 10.0, roughness);
    float specStrength = mix(0.02, 0.20, 1.0 - roughness);
    specStrength = mix(specStrength, 0.38, waterDamp * 0.75);
    float spec = calcSpecular(normal, modelLightDir, viewDir, shininess) * specStrength;

    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);

    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient);
    float groundAO = clamp(0.76 + worldPosition.y * 0.30, 0.72, 1.0);
    float skirtAO  = clamp(0.30 + 0.70 * smoothstep(-0.45, 0.18, fragLocalY), 0.28, 1.0);
    vec3 lit = albedo.rgb * (ambient * groundAO * skirtAO + diff * actualLight * shadow)
             + spec * actualLight * shadow;

    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    // Atmospheric aerial haze for distant mountain crags
    float camDist = length(u_viewPos - fragPosition);
    float haze = smoothstep(38.0, 115.0, camDist) * 0.30;
    vec3 hazeColor = actualAmbient * 1.35;
    lit = mix(lit, hazeColor, haze);

    finalColor = vec4(lit, albedo.a) * colDiffuse;
}

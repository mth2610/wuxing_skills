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
    mat3 TBN = mat3(normalize(fragTangent), normalize(fragBitangent), normalize(fragNormal));
    vec3 normal = normalize(TBN * normalSample);

    float roughness = clamp(texture(texture3, fragTexCoord).r, 0.08, 1.0);

    // Safe fallback if a map forgot to push PropLit_UpdateLighting()
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : normalize(vec3(0.25, 0.75, -0.55));
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);
    vec3 viewDir  = normalize(u_viewPos - fragPosition);

    // Warm mineral & geological strata variation so boulders never look like monochrome grey plastic
    float strataNoise = sin(worldPosition.y * 3.4 + worldPosition.x * 0.45 - worldPosition.z * 0.35) * 0.5 + 0.5;
    vec3 rockAlbedo = albedo.rgb * mix(vec3(0.92, 0.90, 0.86), vec3(1.12, 1.06, 0.96), strataNoise);

    // Top-surface moss & lichen colonization on upward-facing boulder crowns
    float topMossMask = smoothstep(0.48, 0.78, normal.y) * smoothstep(-0.15, 0.45, fragLocalY)
                      * smoothstep(0.28, 0.68, albedo.g);
    vec3 crownMossAlbedo = mix(vec3(0.18, 0.30, 0.11), vec3(0.29, 0.41, 0.15), albedo.r);
    rockAlbedo = mix(rockAlbedo, crownMossAlbedo, topMossMask * 0.72);

    // Lower skirt earthy moss & turf grounding blend
    float skirtMask = (1.0 - smoothstep(-0.75, 0.08, fragLocalY));
    vec3 skirtMossAlbedo = vec3(0.21, 0.31, 0.13);
    rockAlbedo = mix(rockAlbedo, skirtMossAlbedo, skirtMask * 0.62);
    albedo.rgb = rockAlbedo;
    roughness = mix(roughness, 0.85, max(topMossMask, skirtMask) * 0.75);

    float diff = calcDiffuse(normal, lightDir, 0.0);
    float shininess    = mix(72.0, 10.0, roughness);
    float specStrength = mix(0.02, 0.22, 1.0 - roughness);
    float spec = calcSpecular(normal, lightDir, viewDir, shininess) * specStrength;

    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);

    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient);
    float groundAO = clamp(0.76 + worldPosition.y * 0.30, 0.72, 1.0);
    float skirtAO  = clamp(0.52 + 0.48 * smoothstep(-0.85, 0.25, fragLocalY), 0.48, 1.0);
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

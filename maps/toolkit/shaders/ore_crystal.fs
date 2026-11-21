#version 330
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_lights.glsl"
#define MAP_SHADOW_NORMAL_OFFSET_TEXELS 1.6
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragTangent;
in vec3 fragBitangent;
in vec4 fragColor; // .rgb = crystal facet/purity seed, .a = 0.0 host rock, 1.0 metallic ore/crystal
in float fragLocalY;

uniform sampler2D texture0; // host rock diffuse
uniform sampler2D texture2; // host rock normal
uniform sampler2D u_oreVeinTex;
uniform sampler2D u_cloudNoise;

uniform vec4 colDiffuse;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform float u_time;
uniform int u_oreType; // 0=Gold, 1=Silver, 2=Copper/Pyrite, 3=Celestial Meteorite
uniform mat4 u_cloudWorldFromShader;

out vec4 finalColor;

void main()
{
    vec3 toCam = u_viewPos - fragPosition;
    float camDistSq = max(dot(toCam, toCam), 1e-8);
    float invCamDist = inversesqrt(camDistSq);
    vec3 viewDir = toCam * invCamDist;

    mat3 worldRot = mat3(u_cloudWorldFromShader);
    vec3 worldPosition = vec3(u_cloudWorldFromShader * vec4(fragPosition, 1.0));
    vec3 smoothN = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    vec3 worldSmoothN = normalize(worldRot * smoothN);

    vec3 absWN = abs(worldSmoothN);
    bool useX = absWN.x > absWN.z;
    vec2 uvY = worldPosition.xz * 0.78;
    vec2 uvH = (useX ? worldPosition.zy : worldPosition.xy) * 0.78;
    float wY = clamp((absWN.y - 0.35) * 2.0, 0.0, 1.0);

    vec3 rockAlb = texture(texture0, uvY).rgb * wY + texture(texture0, uvH).rgb * (1.0 - wY);
    vec3 rockNormS = (texture(texture2, uvY).rgb * wY + texture(texture2, uvH).rgb * (1.0 - wY)) * 2.0 - 1.0;
    vec4 veinSample = texture(u_oreVeinTex, worldPosition.xz * 0.45 + worldPosition.xy * 0.25);

    vec3 rawT = fragTangent - smoothN * dot(smoothN, fragTangent);
    float tLenSq = dot(rawT, rawT);
    vec3 T = (tLenSq > 1e-8) ? (rawT * inversesqrt(tLenSq)) : normalize(cross(vec3(0.0, 1.0, 0.0), smoothN));
    vec3 B = cross(smoothN, T);
    vec3 normal = normalize(T * rockNormS.x + B * rockNormS.y + smoothN * max(rockNormS.z, 0.25));
    vec3 worldNormal = normalize(worldRot * normal);

    float isCrystal = smoothstep(0.25, 0.85, fragColor.a);
    // Sharp faceted normals on protruding crystal blades
    normal = normalize(mix(normal, smoothN, isCrystal * 0.85));
    worldNormal = normalize(mix(worldNormal, worldSmoothN, isCrystal * 0.85));

    // Ore conductor palette (F0 metallic reflectance + host rock matrix)
    vec3 metalF0 = vec3(1.00, 0.78, 0.24); // Gold
    vec3 hostCol = rockAlb * vec3(0.76, 0.74, 0.70);
    float metalness = isCrystal * 0.92;
    float roughness = mix(0.76, 0.16, isCrystal);
    vec3 innerGlow = vec3(0.0);

    if (u_oreType == 1) {
        // Silver / Argentite lode
        metalF0 = vec3(0.92, 0.95, 0.99);
        hostCol = rockAlb * vec3(0.38, 0.41, 0.46);
        roughness = mix(0.74, 0.14, isCrystal);
    } else if (u_oreType == 2) {
        // Copper Pyrite + Emerald Malachite patina
        metalF0 = mix(vec3(0.92, 0.66, 0.28), vec3(0.18, 0.68, 0.48), smoothstep(0.55, 0.92, veinSample.a) * 0.45);
        hostCol = rockAlb * vec3(0.44, 0.42, 0.38);
        roughness = mix(0.78, 0.22, isCrystal);
    } else if (u_oreType == 3) {
        // Celestial Meteorite (Huyền Thiết) with pulsing golden Sword-Qi veins
        metalF0 = vec3(0.56, 0.62, 0.72);
        hostCol = rockAlb * vec3(0.24, 0.26, 0.30);
        roughness = mix(0.68, 0.20, isCrystal);
        float pulse = 0.75 + 0.25 * sin(u_time * 2.2 + worldPosition.x * 0.6 + worldPosition.z * 0.5);
        innerGlow = vec3(1.00, 0.82, 0.36) * (veinSample.r * isCrystal * 0.42 * pulse);
    }

    // Add fine metallic ore seams bleeding into the host rock collar
    float hostVein = smoothstep(0.45, 0.88, (u_oreType == 1) ? veinSample.g : veinSample.r);
    metalness = max(metalness, hostVein * 0.70);
    roughness = mix(roughness, 0.22, hostVein * 0.65);

    vec3 baseAlbedo = mix(hostCol, metalF0 * (0.85 + 0.30 * fragColor.r), metalness);
    baseAlbedo *= colDiffuse.rgb;

    // Lighting
    float lightLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lightLenSq > 0.001) ? (u_lightDir * inversesqrt(lightLenSq)) : vec3(0.2923, 0.8770, -0.3812);
    vec3 actualLight = (dot(u_lightColor, u_lightColor) > 0.001) ? u_lightColor : vec3(1.0, 0.92, 0.80);
    vec3 actualAmbient = (dot(u_ambientColor, u_ambientColor) > 0.001) ? u_ambientColor : vec3(0.36, 0.42, 0.50);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float diffWrap = max((dot(normal, lightDir) + 0.22) * (1.0 / 1.22), 0.0);
    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    shadow = mix(0.42, 1.0, shadow);
    if (shadow > 0.001) {
        shadow *= Environment_CloudVisibility(u_cloudNoise, worldPosition);
    }

    // Cook-Torrance GGX Metallic Conductor Specular
    vec3 halfSum = lightDir + viewDir;
    vec3 halfVec = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-8));
    float NdotH = max(dot(normal, halfVec), 0.0);
    float NdotV = max(dot(normal, viewDir), 0.001);
    float VdotH = clamp(dot(viewDir, halfVec), 0.0, 1.0);

    float a = roughness * roughness;
    float a2 = max(a * a, 0.0015);
    float denomD = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D_GGX = a2 / max(3.14159265 * denomD * denomD, 1e-4);

    vec3 F0 = mix(vec3(0.04), metalF0, metalness);
    float omvh = 1.0 - VdotH;
    float omvh2 = omvh * omvh;
    vec3 F_Schlick = F0 + (vec3(1.0) - F0) * (omvh2 * omvh2 * omvh);

    float k_dir = ((roughness + 1.0) * (roughness + 1.0)) * 0.125;
    float G_Smith = (NdotV / (NdotV * (1.0 - k_dir) + k_dir)) *
                    (max(NdotL, 0.001) / (max(NdotL, 0.001) * (1.0 - k_dir) + k_dir));
    vec3 specular = (D_GGX * G_Smith * F_Schlick) / (4.0 * NdotV * max(NdotL, 0.001) + 0.001);
    specular = min(specular, vec3(3.2));

    // Sky reflection approximation on metallic facets
    float fresnelRim = pow(1.0 - NdotV, 3.0);
    vec3 skyReflect = mix(actualAmbient * 1.15, actualLight * 0.45 + actualAmbient * 0.85, max(worldNormal.y * 0.5 + 0.5, 0.0));
    vec3 envMetal = skyReflect * F0 * (0.42 + 0.58 * fresnelRim) * metalness;

    float contactAO = clamp(0.48 + 0.52 * smoothstep(-0.35, 0.18, fragLocalY), 0.48, 1.0);
    vec3 hemisphereAmbient = Environment_HemisphereIrradiance(worldNormal, actualAmbient) * contactAO;

    vec3 diffuseTerm = baseAlbedo * (1.0 - metalness * 0.72) * (hemisphereAmbient + actualLight * (diffWrap * shadow));
    vec3 lit = diffuseTerm + specular * actualLight * (NdotL * shadow) + envMetal * contactAO + innerGlow;
    lit += VFXLights_Accumulate(fragPosition, normal, baseAlbedo);

    finalColor = vec4(lit, 1.0);
}

#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is; a private copy of the
// falloff drifts the moment one of them is edited.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface space; see ground_splat.vs
in vec3 fragNormal;
in vec3 fragWorldPos;   // TRUE world space (x, y, z)

uniform vec4 colDiffuse;
uniform sampler2D texture0; // Splatmap (if provided)
uniform sampler2D texGrass; // Meadow substrate detail
uniform sampler2D texPath;  // Soil / dirt detail
uniform sampler2D texGrassMaterial; // packed normal RGB, roughness A
uniform sampler2D texSoilMaterial;  // packed normal RGB, roughness A

uniform vec2 tiling;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;
uniform vec3 viewPos;

#define MAX_PATH_SEGS 16
uniform vec4 u_pathSegs[MAX_PATH_SEGS]; // xy = p0.xz, zw = p1.xz
uniform int u_pathSegCount;
uniform vec4 u_lakeParams; // xy = center.xz, zw = radii.xz

out vec4 finalColor;

void main()
{
    // Dual-scale rotated texture sampling to break tiling
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 colorGrass = texture(texGrass, tiledUV);
    vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
    vec3 broadGrass = texture(texGrass, broadGrassUV).rgb;
    float fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

    // Soil detail from dirt texture
    vec4 colorDirt = texture(texPath, tiledUV * 0.85);
    vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
    vec3 broadDirt = texture(texPath, broadDirtUV).rgb;
    vec3 dirtDetail = mix(colorDirt.rgb, broadDirt, 0.35);
    vec4 grassMaterial = texture(texGrassMaterial, tiledUV);
    vec4 soilMaterial = texture(texSoilMaterial, tiledUV * 0.85);

    // 1. Distance to path
    float distToPath = 1000.0;
    if (u_pathSegCount > 0) {
        for (int i = 0; i < u_pathSegCount && i < MAX_PATH_SEGS; i++) {
            vec2 a = u_pathSegs[i].xy;
            vec2 b = u_pathSegs[i].zw;
            vec2 pa = fragWorldPos.xz - a;
            vec2 ba = b - a;
            float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
            distToPath = min(distToPath, length(pa - ba * h));
        }
    }

    // 2. Shoreline wetness from lake parameters & lake basin cutout
    float shoreFactor = 0.0;
    if (u_lakeParams.z > 0.0) {
        vec2 lakeDelta = (fragWorldPos.xz - u_lakeParams.xy) / u_lakeParams.zw;
        float lakeDist = length(lakeDelta);
        if (lakeDist < 0.98) {
            discard; // Carve out lake hole so 3D bedModel, clear water, and wading character are exposed!
        }
        shoreFactor = (1.0 - smoothstep(1.0, 1.38, lakeDist)) *
                      smoothstep(0.98, 1.04, lakeDist);
    }

    // 3. Slope steepness
    vec3 geomNormal = normalize(fragNormal);
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // 4. Four-layer weights
    float wPath = 1.0 - smoothstep(1.2, 1.9, distToPath);
    float wPathMargin = smoothstep(1.1, 1.85, distToPath) * (1.0 - smoothstep(1.85, 3.4, distToPath));
    float wWetSoil = shoreFactor * 0.55;
    float wSlope = smoothstep(0.14, 0.46, slope);
    float wDrySoil = clamp(wPathMargin * 0.88 + wSlope * 0.92, 0.0, 1.0);
    float wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

    // Height-blended layer modulation (PixelAnt / AAA terrain splatting)
    float hGrass = wGrass + (fineGrassLuma - 0.5) * 0.18;
    float hDry = wDrySoil + (dirtDetail.r - 0.5) * 0.16;
    float hWet = wWetSoil + (1.0 - dirtDetail.g * 0.8) * 0.14;
    float hPath = wPath + (colorDirt.r - 0.5) * 0.16;

    float totalW = max(hGrass, 0.0) + max(hDry, 0.0) + max(hWet, 0.0) + max(hPath, 0.0);
    if (totalW > 0.0001) {
        wGrass = max(hGrass, 0.0) / totalW;
        wDrySoil = max(hDry, 0.0) / totalW;
        wWetSoil = max(hWet, 0.0) / totalW;
        wPath = max(hPath, 0.0) / totalW;
    } else {
        wGrass = 1.0;
    }

    // The authored meadow substrate contains low turf, fine litter, and earth.
    // Keep its large forms while matching the darker blade roots above it.
    vec3 blendedGrass = mix(colorGrass.rgb, broadGrass, 0.32);
    vec3 turfBase = vec3(0.145, 0.205, 0.095);
    vec3 grassAlbedo = mix(turfBase,
                           blendedGrass * vec3(0.82, 0.94, 0.78), 0.68);

    // Multi-scale organic turf variation (deep damp swales vs warm sunny hummocks)
    float turfNoise = sin(fragWorldPos.x * 0.16 + fragWorldPos.z * 0.11) * 0.5
                    + sin(fragWorldPos.x * -0.08 + fragWorldPos.z * 0.24 + 1.7) * 0.35
                    + sin(fragWorldPos.x * 0.45 - fragWorldPos.z * 0.38 + 3.1) * 0.15;
    vec3 warmTurf = grassAlbedo * vec3(1.12, 1.06, 0.84);
    vec3 coolTurf = grassAlbedo * vec3(0.90, 0.98, 0.92);
    grassAlbedo = mix(coolTurf, warmTurf, smoothstep(-0.35, 0.55, turfNoise));

    // Soil & Path PBR Albedos
    vec3 drySoilColor = dirtDetail * vec3(0.86, 0.77, 0.63) * 1.18;
    vec3 wetSoilColor = dirtDetail * vec3(0.62, 0.58, 0.48) * 1.02;
    vec3 pathMarginColor = mix(drySoilColor, colorDirt.rgb * vec3(1.12, 1.06, 0.91), wPath);

    vec3 blendedAlbedo = grassAlbedo * wGrass
                       + drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;

    // Macro landscape modulation (warm golden sun ridges, deep emerald dips)
    float macroA = sin(fragWorldPos.x * 0.042 + fragWorldPos.z * 0.028);
    float macroB = sin(fragWorldPos.x * -0.022 + fragWorldPos.z * 0.048 + 1.35);
    float macroField = 0.50 + 0.32 * macroA + 0.18 * macroB;
    vec3 macroTint = mix(vec3(0.92, 0.96, 0.90), vec3(1.06, 1.03, 0.94), macroField);
    blendedAlbedo *= macroTint;

    // Tangent-space micro normals follow the same UVs as their albedo layers.
    // Keep the strength modest so distant mips stay calm rather than glitter.
    float soilWeight = clamp(wDrySoil + wWetSoil + wPath, 0.0, 1.0);
    vec3 microNormal = normalize(mix(grassMaterial.rgb, soilMaterial.rgb, soilWeight) * 2.0 - 1.0);
    vec3 tangent = normalize(vec3(1.0, -geomNormal.x / max(geomNormal.y, 0.15), 0.0));
    vec3 bitangent = normalize(cross(tangent, geomNormal));
    float microStrength = mix(0.14, 0.30, soilWeight);
    vec3 normal = normalize(geomNormal * (0.70 + 0.30 * microNormal.z)
                          + tangent * microNormal.x * microStrength
                          + bitangent * microNormal.y * microStrength);
    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.48, wWetSoil * 0.65);

    // Micro cavity ambient occlusion from texture relief
    float cavityAO = mix(0.88 + 0.12 * fineGrassLuma,
                         0.75 + 0.25 * colorDirt.r, soilWeight);

    // Lighting
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) {
        light = normalize(-lightDir);
    }
    float NdotL = max(dot(normal, light), 0.0);

    vec4 actualAmbient = ambientColor.a == 0.0 ? vec4(0.4, 0.4, 0.4, 1.0) : ambientColor;
    vec4 actualLight = lightColor.a == 0.0 ? vec4(1.0, 1.0, 1.0, 1.0) : lightColor;

    float shadow = MapShadowVisibility(fragPosition, normal, light);
    float skyWeight = normal.y * 0.5 + 0.5;
    vec3 skyAmbient = actualAmbient.rgb * vec3(1.04, 1.08, 1.16);
    vec3 groundBounce = actualAmbient.rgb * vec3(0.42, 0.38, 0.28);
    vec3 ambient = mix(groundBounce, skyAmbient, skyWeight) * cavityAO;

    float ambientVisibility = mix(0.92, 1.0, shadow);
    vec3 totalLight = ambient * ambientVisibility
                    + actualLight.rgb * NdotL * shadow;

    vec3 groundLit = blendedAlbedo * totalLight;
    vec3 viewDir = normalize(viewPos - fragWorldPos);
    vec3 halfDir = normalize(light + viewDir);
    float specPower = mix(12.0, 72.0, 1.0 - roughness);
    float drySpec = pow(max(dot(normal, halfDir), 0.0), specPower)
                  * (1.0 - roughness) * 0.16;
    groundLit += actualLight.rgb * drySpec * shadow;

    // Capillary wetness mechanics: Albedo darkening & Roughness collapse (PBR Specular Sheen)
    if (wWetSoil > 0.03) {
        float NdotV_wet = max(dot(normal, viewDir), 0.0);
        float fresnelWet = 0.02 + 0.98 * pow(1.0 - NdotV_wet, 5.0);
        float wetSpecSharp = pow(max(dot(normal, halfDir), 0.0), 96.0);
        float wetSpecBroad = pow(max(dot(normal, halfDir), 0.0), 24.0) * 0.25;
        vec3 wetSheen = actualLight.rgb * (wetSpecSharp * 0.75 + wetSpecBroad) * (0.35 + fresnelWet * 0.65);
        groundLit += wetSheen * wWetSoil * shadow;
    }

    // The steep lake cutout faces away from the sun. Sky and water bounce keep
    // that short bank readable instead of leaving a black moat around the lake.
    groundLit += vec3(0.10, 0.13, 0.10) * shoreFactor * (0.75 + 0.25 * dirtDetail.r);

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    finalColor = vec4(groundLit, 1.0);
}

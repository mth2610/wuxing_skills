#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is; a private copy of the
// falloff drifts the moment one of them is edited.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface space; see ground_splat.vs
in vec3 fragNormal;

uniform vec4 colDiffuse;
uniform sampler2D texture0; // Splatmap (if provided)
uniform sampler2D texGrass; // Meadow substrate detail
uniform sampler2D texPath;  // Soil / dirt detail

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
    float broadGrassLuma = dot(broadGrass, vec3(0.2126, 0.7152, 0.0722));
    float grassDetail = clamp(0.93 + (fineGrassLuma - 0.50) * 0.28 + (broadGrassLuma - 0.50) * 0.16, 0.75, 1.15);
    vec3 grassAlbedo = grassDetail * colDiffuse.rgb;

    // Soil detail from dirt texture
    vec4 colorDirt = texture(texPath, tiledUV * 0.85);
    vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
    vec3 broadDirt = texture(texPath, broadDirtUV).rgb;
    vec3 dirtDetail = mix(colorDirt.rgb, broadDirt, 0.35);

    // 1. Distance to path
    float distToPath = 1000.0;
    if (u_pathSegCount > 0) {
        for (int i = 0; i < u_pathSegCount && i < MAX_PATH_SEGS; i++) {
            vec2 a = u_pathSegs[i].xy;
            vec2 b = u_pathSegs[i].zw;
            vec2 pa = fragPosition.xz - a;
            vec2 ba = b - a;
            float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
            distToPath = min(distToPath, length(pa - ba * h));
        }
    }

    // 2. Shoreline wetness from lake parameters
    float shoreFactor = 0.0;
    if (u_lakeParams.z > 0.0) {
        vec2 lakeDelta = (fragPosition.xz - u_lakeParams.xy) / u_lakeParams.zw;
        float lakeDist = length(lakeDelta);
        shoreFactor = smoothstep(1.38, 1.0, lakeDist) * smoothstep(0.82, 1.0, lakeDist);
    }

    // 3. Slope steepness
    vec3 normal = normalize(fragNormal);
    float slope = clamp(1.0 - normal.y, 0.0, 1.0);

    // 4. Four-layer weights
    float wPath = 1.0 - smoothstep(1.2, 1.9, distToPath);
    float wPathMargin = smoothstep(1.1, 1.85, distToPath) * (1.0 - smoothstep(1.85, 3.4, distToPath));
    float wWetSoil = shoreFactor * 0.95;
    float wSlope = smoothstep(0.14, 0.46, slope);
    float wDrySoil = clamp(wPathMargin * 0.88 + wSlope * 0.92, 0.0, 1.0);
    float wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

    // Normalize weights
    float totalW = wGrass + wDrySoil + wWetSoil + wPath;
    if (totalW > 0.0001) {
        wGrass /= totalW;
        wDrySoil /= totalW;
        wWetSoil /= totalW;
        wPath /= totalW;
    } else {
        wGrass = 1.0;
    }

    // Layer albedo synthesis
    vec3 drySoilColor = dirtDetail * vec3(0.85, 0.72, 0.54) * 1.05;
    vec3 wetSoilColor = dirtDetail * vec3(0.30, 0.27, 0.22) * 0.88;
    vec3 pathMarginColor = mix(drySoilColor, colorDirt.rgb * vec3(0.88, 0.84, 0.78), wPath);

    vec3 blendedAlbedo = grassAlbedo * wGrass
                       + drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;

    // Macro landscape noise
    float macroA = sin(fragPosition.x * 0.061 + fragPosition.z * 0.043);
    float macroB = sin(fragPosition.x * -0.033 + fragPosition.z * 0.077 + 1.4);
    blendedAlbedo *= 0.985 + 0.02 * macroA + 0.015 * macroB;

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
    vec3 skyAmbient = actualAmbient.rgb * vec3(1.08, 1.12, 1.20);
    vec3 groundBounce = actualAmbient.rgb * vec3(0.47, 0.40, 0.32);
    vec3 ambient = mix(groundBounce, skyAmbient, skyWeight);

    float ambientVisibility = mix(0.94, 1.0, shadow);
    vec3 totalLight = ambient * ambientVisibility
                    + actualLight.rgb * NdotL * shadow;

    vec3 groundLit = blendedAlbedo * totalLight;

    // Wet soil specular sheen
    if (wWetSoil > 0.05) {
        vec3 viewDir = normalize(viewPos - fragPosition);
        vec3 halfDir = normalize(light + viewDir);
        float wetSpec = pow(max(dot(normal, halfDir), 0.0), 28.0);
        groundLit += actualLight.rgb * wetSpec * wWetSoil * 0.32 * shadow;
    }

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    finalColor = vec4(groundLit, 1.0);
}

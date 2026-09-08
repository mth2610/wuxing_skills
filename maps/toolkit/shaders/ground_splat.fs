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
    vec3 geomNormal = normalize(fragNormal);
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // 4. Four-layer weights
    float wPath = 1.0 - smoothstep(1.2, 1.9, distToPath);
    float wPathMargin = smoothstep(1.1, 1.85, distToPath) * (1.0 - smoothstep(1.85, 3.4, distToPath));
    float wWetSoil = shoreFactor * 0.95;
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

    // Botanical PBR Meadow Grass Albedo (calibrated rich natural green, never neon)
    vec3 blendedGrass = mix(colorGrass.rgb, broadGrass, 0.32);
    vec3 botanicalGreen = vec3(0.18, 0.29, 0.11);
    vec3 sunlitGreen = vec3(0.38, 0.46, 0.20);
    vec3 grassAlbedo = mix(blendedGrass * 0.65 + botanicalGreen * 0.45,
                           blendedGrass * (colDiffuse.rgb * 1.08), 0.60);

    // Multi-scale organic turf variation (deep damp swales vs warm sunny hummocks)
    float turfNoise = sin(fragPosition.x * 0.16 + fragPosition.z * 0.11) * 0.5
                    + sin(fragPosition.x * -0.08 + fragPosition.z * 0.24 + 1.7) * 0.35
                    + sin(fragPosition.x * 0.45 - fragPosition.z * 0.38 + 3.1) * 0.15;
    vec3 warmTurf = grassAlbedo * vec3(1.12, 1.06, 0.84);
    vec3 coolTurf = grassAlbedo * vec3(0.90, 0.98, 0.92);
    grassAlbedo = mix(coolTurf, warmTurf, smoothstep(-0.35, 0.55, turfNoise));

    // Soil & Path PBR Albedos
    vec3 drySoilColor = dirtDetail * vec3(0.48, 0.38, 0.26) * 1.10;
    vec3 wetSoilColor = dirtDetail * vec3(0.24, 0.20, 0.15) * 0.90;
    vec3 pathMarginColor = mix(drySoilColor, colorDirt.rgb * vec3(0.68, 0.64, 0.58), wPath);

    vec3 blendedAlbedo = grassAlbedo * wGrass
                       + drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;

    // Macro landscape modulation (warm golden sun ridges, deep emerald dips)
    float macroA = sin(fragPosition.x * 0.042 + fragPosition.z * 0.028);
    float macroB = sin(fragPosition.x * -0.022 + fragPosition.z * 0.048 + 1.35);
    float macroField = 0.50 + 0.32 * macroA + 0.18 * macroB;
    vec3 macroTint = mix(vec3(0.92, 0.96, 0.90), vec3(1.06, 1.03, 0.94), macroField);
    blendedAlbedo *= macroTint;

    // Surface Gradient Bump Mapping (Morten Mikkelsen's method)
    // Generates true 3D micro-relief from texture height derivatives across the terrain
    vec3 normal = geomNormal;
    float surfaceHeight = mix(fineGrassLuma, colorDirt.r, wDrySoil + wPath * 0.7);
    vec3 dp1 = dFdx(fragPosition);
    vec3 dp2 = dFdy(fragPosition);
    vec3 m = cross(dp1, dp2);
    float det = dot(m, m);
    if (det > 0.000001) {
        float dh_dx = dFdx(surfaceHeight);
        float dh_dy = dFdy(surfaceHeight);
        vec3 surfGrad = (dh_dx * cross(dp2, normal) + dh_dy * cross(normal, dp1)) / sqrt(det);
        normal = normalize(normal - surfGrad * 0.38);
    }

    // Micro cavity ambient occlusion from texture relief
    float cavityAO = clamp(0.35 + 0.65 * surfaceHeight * 1.5, 0.38, 1.0);

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

    // Wet soil specular sheen
    if (wWetSoil > 0.05) {
        vec3 viewDir = normalize(viewPos - fragPosition);
        vec3 halfDir = normalize(light + viewDir);
        float wetSpec = pow(max(dot(normal, halfDir), 0.0), 32.0);
        groundLit += actualLight.rgb * wetSpec * wWetSoil * 0.40 * shadow;
    }

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    finalColor = vec4(groundLit, 1.0);
}

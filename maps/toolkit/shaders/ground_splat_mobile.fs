#version 330

#include "core/shaders/common/vfx_lights.glsl"
#define MAP_DYNAMIC_SHADOW_RADIUS 1.25
#define MAP_SHADOW_FILTER_QUALITY 0

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/meadow_palette.glsl"

#define GS_DISTANT_CANOPY_GAIN 1.02
#define GS_GRASS_CHROMA 0.96
#define GS_FAR_CANOPY_SHADE 0.16

uniform sampler2D u_cloudNoise;

in vec2 fragTexCoord;
in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragWorldPos;
in vec3 fragWorldNormal;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;
in vec2 v_macroPatchAndField;

uniform vec4 colDiffuse;
uniform sampler2D texture0;
uniform sampler2D texGrass;
uniform sampler2D texPath;
uniform sampler2D u_groundRock;
uniform int u_groundRockEnabled;

uniform vec2 tiling;
uniform sampler2D u_ecology;
uniform sampler2D u_ecologyDistance;
uniform vec4 u_ecologyRect;
uniform int u_ecologyEnabled;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;
uniform vec3 viewPos;
uniform vec4 u_islandRect, u_islandShape;
#include "maps/toolkit/shaders/map_island_boundary.glsl"

#define MAX_PATH_SEGS 16
uniform vec4 u_pathSegs[MAX_PATH_SEGS];
uniform int u_pathSegCount;
uniform vec4 u_lakeParams;

out vec4 finalColor;

#define LAYER_EPS 0.005
#define PATH_INFLUENCE 3.55

void main()
{
    vec2 tiledUV = fragTexCoord * tiling;
    vec2 uvDx = dFdx(tiledUV), uvDy = dFdy(tiledUV);
    float mipTiled = max(0.0, 0.5 * log2(max(max(dot(uvDx, uvDx), dot(uvDy, uvDy)) * 262144.0, 1.0)));

    vec3 light = (dot(lightDir, lightDir) > 0.01) ? normalize(-lightDir) : vec3(0.0, 1.0, 0.0);
    vec3 actualAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actualLight   = (lightColor.a == 0.0)   ? vec3(1.0) : lightColor.rgb;
    actualLight *= 1.0 / (1.0 + max(light.y - 0.32, 0.0) * 1.18);

    // Cloud Veil Early Out
    float veil = 0.0;
    vec3 cloudVeilColor = vec3(0.0);
    if (fragWorldPos.y < 0.1 && u_islandRect.z > 0.0 && u_islandRect.w > 0.0) {
        float edge = MapIslandDistance(fragWorldPos.xz, u_islandRect, u_islandShape.x);
        veil = smoothstep(-max(u_islandShape.y, 0.1), 0.0, edge + max(u_islandShape.z, 0.0))
             * (1.0 - smoothstep(-2.0, 0.1, fragWorldPos.y));
        if (veil > 0.001) {
            vec3 cloudLight = clamp(actualAmbient + actualLight * max((light.y + 0.4) * 0.7142857, 0.0) * 0.55, 0.0, 1.05);
            cloudVeilColor = vec3(0.88, 0.91, 0.96) * cloudLight;
            if (veil >= 0.995) {
                finalColor = vec4(cloudVeilColor, 1.0);
                return;
            }
        }
    }

    vec3 viewVec = viewPos - fragWorldPos;
    float camDistSqXZ = dot(viewVec.xz, viewVec.xz);

    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float distToPath = 1000.0;
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    float smoothSignedShore = 1000.0;
    if (u_ecologyEnabled != 0) {
        vec2 ecoUV = clamp((fragWorldPos.xz - u_ecologyRect.xy) / max(u_ecologyRect.zw, vec2(0.0001)), vec2(0.0), vec2(1.0));
        ecology = textureLod(u_ecology, ecoUV, 0.0);
        vec2 decodedDist = textureLod(u_ecologyDistance, ecoUV, 0.0).rg;
        distToPath = decodedDist.r * 5.1 - 0.5;
        smoothSignedShore = decodedDist.g * 5.5 - 3.1;
        if (smoothSignedShore < 0.0) {
            lakeDepthFactor = clamp(-smoothSignedShore * (1.0 / 2.6), 0.0, 1.0);
            shoreFactor = 1.0;
        } else {
            shoreFactor = max(1.0 - smoothstep(0.0, 0.25, smoothSignedShore),
                              (1.0 - smoothstep(0.05, 0.95, smoothSignedShore)) * 0.50);
        }
    } else if (u_pathSegCount > 0) {
        float minDistSq = 1000000.0;
        int count = clamp(u_pathSegCount, 0, MAX_PATH_SEGS);
        for (int i = 0; i < count; i++) {
            vec4 seg = u_pathSegs[i];
            vec2 pa = fragWorldPos.xz - seg.xy;
            vec2 ba = seg.zw - seg.xy;
            float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
            vec2 d = pa - ba * h;
            minDistSq = min(minDistSq, dot(d, d));
        }
        distToPath = sqrt(minDistSq);
    }

    if (u_lakeParams.z > 0.0 && u_lakeParams.w > 0.0) {
        vec2 lakeDelta = (fragWorldPos.xz - u_lakeParams.xy) / max(u_lakeParams.zw, vec2(0.0001));
        float lakeDistSq = dot(lakeDelta, lakeDelta);
        if (lakeDistSq < 1.5625) {
            float lakeDist = sqrt(max(lakeDistSq, 1e-8));
            shoreFactor = max(shoreFactor, 1.0 - smoothstep(0.96, 1.25, lakeDist));
            if (lakeDist < 1.0)
                lakeDepthFactor = max(lakeDepthFactor, 1.0 - lakeDist);
        }
    }

    float waterElevationMask = smoothstep(-0.45, -0.15, fragWorldPos.y);
    lakeDepthFactor *= waterElevationMask;
    shoreFactor *= waterElevationMask;

    vec3 geomNormal = normalize(fragNormal);
    vec3 geomWorldNormal = normalize(fragWorldNormal);
    float slope = clamp(1.0 - geomWorldNormal.y, 0.0, 1.0);
    float outerCliffElev = 1.0 - smoothstep(-1.80, -0.15, fragWorldPos.y);

    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.015
                     || (smoothSignedShore < 1.45 && fragWorldPos.y >= -0.45)
                     || slope > 0.35
                     || outerCliffElev > 0.05
                     || fragWorldPos.y < -0.15;

    float wPath = 0.0;
    float wWetSoil = 0.0;
    float wCliff = 0.0;
    float wDrySoil = 0.0;
    float wGrass = 1.0;
    vec3 dirtDetail = vec3(0.5);

    if (soilPossible) {
        dirtDetail = textureLod(texPath, tiledUV * 0.85, mipTiled).rgb;
        wPath = 1.0 - smoothstep(1.10, 1.95, distToPath);
        float wPathMargin = smoothstep(0.95, 1.65, distToPath) * (1.0 - smoothstep(1.65, 3.1, distToPath));
        wWetSoil = shoreFactor * 0.85;
        if (lakeDepthFactor > 0.0)
            wWetSoil = mix(wWetSoil, 1.0, clamp(lakeDepthFactor * 2.0, 0.0, 1.0));

        float rimMask = outerCliffElev * (1.0 - smoothstep(0.0, 0.45, shoreFactor));
        float wSlope = max(smoothstep(0.50, 0.72, slope), rimMask * smoothstep(0.20, 0.38, slope));
        float outerAbyssMask = outerCliffElev * smoothstep(0.10, 0.28, slope);
        wCliff = clamp(max(wSlope, outerAbyssMask), 0.0, 1.0) * (1.0 - shoreFactor * 0.99);

        if (u_ecologyEnabled != 0) {
            float grassTaper = smoothstep(-0.04, 0.32, smoothSignedShore);
            wGrass *= grassTaper;
            wWetSoil = max(wWetSoil, (1.0 - smoothstep(-0.05, 0.36, smoothSignedShore)) * 0.92);
        }

        float cliffDom = smoothstep(0.08, 0.60, wCliff);
        wGrass *= (1.0 - cliffDom);
        wWetSoil *= (1.0 - cliffDom);

        wDrySoil = clamp(max(wPathMargin * 0.78, wCliff), 0.0, 1.0)
                       * (1.0 - wWetSoil * 0.85) * (1.0 - wPath * 0.85);
        wDrySoil = max(wDrySoil, wCliff);
        wGrass = clamp(wGrass - wPath - wDrySoil, 0.0, 1.0);
        wWetSoil = clamp(1.0 - wGrass - wPath - wDrySoil, 0.0, 1.0);
    }

    vec3 blendedAlbedo = vec3(0.0);
    if (wGrass > LAYER_EPS) {
        vec3 colorGrass = textureLod(texGrass, tiledUV, mipTiled).rgb;
        float turfDetail = dot(colorGrass, vec3(0.2126, 0.7152, 0.0722));
        float detailContrast = clamp((turfDetail - 0.22) * 1.65, -0.26, 0.30);
        vec3 turfBase = vec3(0.155, 0.228, 0.086);
        vec3 litterChroma = clamp(colorGrass / max(turfDetail, 0.06), vec3(0.75), vec3(1.30));
        vec3 grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, 0.45) * (1.0 + detailContrast);

        float habW1 = sin(fragWorldPos.x * 0.085 + fragWorldPos.z * 0.055) * 0.5 + 0.5;
        float habW2 = sin(fragWorldPos.x * -0.065 + fragWorldPos.z * 0.12 + 1.8) * 0.5 + 0.5;
        float analyticHabitat = habW1 * 0.60 + habW2 * 0.40;
        float habitatVal = (u_ecologyEnabled != 0) ? mix(analyticHabitat, ecology.a, 0.25) : analyticHabitat;
        vec3 targetCanopyColor = MeadowCanopyColor(habitatVal);
        vec3 nearCanopy = mix(vec3(0.092, 0.156, 0.054), targetCanopyColor * 0.52, 0.52);
        grassAlbedo = mix(grassAlbedo, nearCanopy * (1.0 + detailContrast * 0.65), 0.45);

        if (camDistSqXZ > 64.0) {
            float camDist = sqrt(camDistSqXZ);
            float distantBlend = smoothstep(12.0, 36.0, camDist) * wGrass * (1.0 - wCliff);
            grassAlbedo = mix(grassAlbedo, targetCanopyColor * GS_DISTANT_CANOPY_GAIN, distantBlend * 0.90);
        }
        float gluma = dot(grassAlbedo, vec3(0.2126, 0.7152, 0.0722));
        grassAlbedo = mix(vec3(gluma), grassAlbedo, GS_GRASS_CHROMA);
        blendedAlbedo += grassAlbedo * wGrass;
    }

    if (wDrySoil + wWetSoil + wPath > LAYER_EPS) {
        vec3 loamColor = mix(vec3(0.10, 0.08, 0.05), vec3(0.18, 0.14, 0.09), smoothstep(0.26, 0.72, dirtDetail.r)) * (0.86 + 0.28 * dirtDetail);
        if (wCliff > 0.01) {
            vec3 rockCol = (u_groundRockEnabled != 0)
                ? textureLod(u_groundRock, fragWorldPos.xz * 0.35, mipTiled).rgb * vec3(0.55, 0.54, 0.50)
                : loamColor * 1.1;
            loamColor = mix(loamColor, rockCol, smoothstep(0.05, 0.55, wCliff));
        }
        vec3 sandColor = mix(vec3(0.54, 0.49, 0.39), vec3(0.32, 0.28, 0.21), clamp(shoreFactor * 0.85, 0.0, 1.0)) * (0.86 + 0.28 * dirtDetail);
        vec3 pathColor = mix(vec3(0.34, 0.28, 0.19), vec3(0.42, 0.36, 0.26), dirtDetail.r);
        blendedAlbedo += loamColor * wDrySoil + sandColor * wWetSoil + pathColor * wPath;
    }

    // Lighting & 1-tap Shadow
    float rawNdotL = dot(geomNormal, light);
    float wrapNdotL = max((rawNdotL + 0.38) * 0.7246, 0.0);
    float horizonEnv = smoothstep(0.02, 0.38, max(light.y, 0.0)) * 0.66 + 0.34;
    float effectiveNdotL = wrapNdotL * horizonEnv;

    float dynShadowCov;
    float rawShadow = MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, geomNormal, light, dynShadowCov);
    float shadow = mix(mix(0.24, 0.32, wGrass), 1.0, rawShadow);
    float sunVisibility = (shadow > 0.001) ? shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos) : 0.0;

    vec3 ambient = Environment_HemisphereIrradiance(geomNormal, actualAmbient) * mix(0.92, 1.0, rawShadow);
    vec3 groundLit = blendedAlbedo * (ambient + actualLight * effectiveNdotL * sunVisibility);

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    if (veil > 0.001)
        groundLit = mix(groundLit, cloudVeilColor, veil);

    if (camDistSqXZ > 1225.0) {
        float aerialFactor = smoothstep(1225.0, 21025.0, camDistSqXZ) * (1.0 - smoothstep(2.5, 11.5, fragWorldPos.y) * 0.35);
        vec3 horizonAerial = actualAmbient * vec3(0.88, 0.84, 0.78) + actualLight * vec3(0.28, 0.22, 0.16);
        groundLit = mix(groundLit, horizonAerial, aerialFactor * 0.28);
    }

    if (fragWorldPos.y < -1.80) {
        float cloudImmersion = 1.0 - smoothstep(-4.80, -1.80, fragWorldPos.y);
        vec3 cloudBaseTone = mix(vec3(0.56, 0.62, 0.72), vec3(0.95, 0.92, 0.86), 0.55);
        vec3 cloudHazeLight = actualAmbient * 0.85 + actualLight * (0.42 * max(light.y + 0.35, 0.25));
        groundLit = mix(groundLit, cloudBaseTone * cloudHazeLight, cloudImmersion * 0.92);
    }

    finalColor = vec4(groundLit, 1.0);
}

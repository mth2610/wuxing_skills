#version 330

// Đợt E / E2 — VFX point lights.
#include "core/shaders/common/vfx_lights.glsl"
// Dynamic PCF penumbra radius & normal offset eliminate riverbed shadow acne stripes and texel dots.
#define MAP_DYNAMIC_SHADOW_RADIUS 1.85
#define MAP_SHADOW_NORMAL_OFFSET_TEXELS 2.0

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/meadow_palette.glsl"

// ---- Look knobs ( tinh chỉnh hài hòa màu cỏ & nền đất cho song_quao ) ----
#define GS_DISTANT_CANOPY_GAIN 1.02
#define GS_CREST_COOL vec3(0.92, 0.99, 0.94)
#define GS_CREST_WARM vec3(1.04, 1.03, 0.95)
#define GS_GRASS_CHROMA 0.96
#define GS_NEAR_DETAIL_BOOST 0.22
#define GS_FAR_CANOPY_SHADE 0.16

uniform sampler2D u_cloudNoise;
uniform sampler2D u_groundRelief;
uniform int u_groundReliefEnabled;
uniform sampler2D u_groundDebris;
uniform int u_groundDebrisEnabled;

in vec2 fragTexCoord;
in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragWorldPos;
in vec3 fragWorldNormal;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;
in vec2 v_macroPatchAndField;

uniform vec4 colDiffuse;
uniform sampler2D texture0;           // Splatmap
uniform sampler2D texGrass;           // Meadow substrate detail
uniform sampler2D texPath;            // Soil / dirt detail
uniform sampler2D texGrassMaterial;   // packed normal RGB, roughness A
uniform sampler2D texSoilMaterial;    // packed normal RGB, roughness A
uniform sampler2D u_groundRock;       // High-res rock/cliff diffuse
uniform sampler2D u_groundRockNormal; // High-res rock/cliff normal map
uniform int u_groundRockEnabled;      // 1 if dedicated rock maps are active

uniform vec2 tiling;
uniform sampler2D u_ecology;
uniform sampler2D u_ecologyDistance;
uniform vec4 u_ecologyRect;
uniform int u_ecologyEnabled;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;
uniform vec3 viewPos;
uniform float u_time;
uniform vec4 u_islandRect, u_islandShape;
#include "maps/toolkit/shaders/map_island_boundary.glsl"

#define MAX_PATH_SEGS 16
uniform vec4 u_pathSegs[MAX_PATH_SEGS];
uniform int u_pathSegCount;
uniform vec4 u_lakeParams;

uniform float u_aridBiomeFactor;
uniform float u_strataStrength;
uniform vec3 u_aridGrusColor;
uniform vec3 u_aridHardpanColor;
uniform vec3 u_aridLichenColor;

out vec4 finalColor;

#define LAYER_EPS 0.003
#define PATH_INFLUENCE 3.55

void main()
{
    // ---- 0. Derivatives (Tính toàn bộ đạo hàm trong luồng đồng nhất trước discard/return) ----
    vec2 baseUvDx = dFdx(fragTexCoord), baseUvDy = dFdy(fragTexCoord);
    vec2 tiledUV = fragTexCoord * tiling;
    vec2 uvDx = baseUvDx * tiling, uvDy = baseUvDy * tiling;
    vec3 wpDx3 = dFdx(fragWorldPos), wpDy3 = dFdy(fragWorldPos);
    vec2 wpDx = wpDx3.xz, wpDy = wpDy3.xz;
    float pixelMetersSq = max(dot(wpDx, wpDx), dot(wpDy, wpDy));
    // Mở rộng phạm vi giữ chi tiết vi mô lên tới ~45m để địa hình trung/hậu cảnh không bị bệt phẳng
    float detailResolve = 1.0 - smoothstep(0.0004, 0.0160, pixelMetersSq);
    bool highDetailGround = (u_shadowFilterQuality >= 1.5);
    float mipTiled = max(0.0, 0.5 * log2(max(max(dot(uvDx, uvDx), dot(uvDy, uvDy)) * 262144.0, 1.0)));
    float mipWorld = max(0.0, 0.5 * log2(max(max(dot(wpDx, wpDx), dot(wpDy, wpDy)) * 65536.0, 1.0)));

    // ---- 1. Splat Fetch ----
    vec4 splatSample = textureLod(texture0, fragTexCoord, 0.0);
    float streamWet = (u_ecologyEnabled != 0) ? 0.0 : splatSample.a;
    float rockScree = splatSample.g;

    float lightLenSq = dot(lightDir, lightDir);
    vec3 light = (lightLenSq > 0.01) ? (-lightDir * inversesqrt(lightLenSq)) : vec3(0.0, 1.0, 0.0);
    vec3 actualAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actualLight   = (lightColor.a == 0.0)   ? vec3(1.0) : lightColor.rgb;
    actualLight *= 1.0 / (1.0 + max(light.y - 0.32, 0.0) * 1.18);

    // ---- 2. Cloud Veil Pre-Evaluation & Early Out ----
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
    bool nearDualScale = highDetailGround && (camDistSqXZ < 225.0);
    float dualScaleFade = 1.0 - smoothstep(144.0, 225.0, camDistSqXZ);

    // ---- 3. Distance to Path & Continuous 16-bit Shoreline ----
    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float distToPath = 1000.0;
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    float smoothSignedShore = 1000.0;
    float variableShore = 1000.0;
    float riparianSandMargin = 0.0;
    if (u_ecologyEnabled != 0) {
        vec2 ecoUV = clamp((fragWorldPos.xz - u_ecologyRect.xy) / max(u_ecologyRect.zw, vec2(0.0001)), vec2(0.0), vec2(1.0));
        ecology = textureLod(u_ecology, ecoUV, 0.0);

        vec2 decodedDist = textureLod(u_ecologyDistance, ecoUV, 0.0).rg;
        distToPath = decodedDist.r * 5.1 - 0.5;
        float signedShore = decodedDist.g * 5.5 - 3.1;

        // Delicate organic shoreline domain warp (keeps soil margin tight to true waterline)
        float shoreWarp = sin(fragWorldPos.x * 0.34 + fragWorldPos.z * 0.27) * 0.10
                        + cos(fragWorldPos.z * 0.76 - fragWorldPos.x * 0.58) * 0.05
                        + sin(fragWorldPos.x * 1.85 + fragWorldPos.z * 1.55) * 0.03;
        smoothSignedShore = signedShore + shoreWarp;
        if (smoothSignedShore < 0.0) {
            lakeDepthFactor = clamp(-smoothSignedShore * (1.0 / 2.6), 0.0, 1.0);
            shoreFactor = 1.0;
        } else {
            float wetCore = 1.0 - smoothstep(0.0, 0.20, smoothSignedShore);
            float dampFringe = 1.0 - smoothstep(0.05, 0.85, smoothSignedShore);
            float riparianInfluence = 1.0 - smoothstep(0.10, 1.45, smoothSignedShore);
            shoreFactor = max(wetCore, max(dampFringe * 0.50, riparianInfluence * 0.20));
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

    // ---- 4. Shoreline Wetness & Lake Basin ----
    if (u_lakeParams.z > 0.0 && u_lakeParams.w > 0.0) {
        vec2 lakeDelta = (fragWorldPos.xz - u_lakeParams.xy) / max(u_lakeParams.zw, vec2(0.0001));
        float lakeDistSq = dot(lakeDelta, lakeDelta);
        if (lakeDistSq < 1.5625) {
            float lakeDist = lakeDistSq * inversesqrt(max(lakeDistSq, 1e-8));
            float distWet = 1.0 - smoothstep(0.96, 1.25, lakeDist);
            shoreFactor = max(shoreFactor, distWet);
            if (lakeDist < 1.0) {
                lakeDepthFactor = max(lakeDepthFactor, 1.0 - lakeDist * mix(1.0, lakeDist, 0.6));
            }
        }
    }

    // Gate river/lake water phenomena to real water elevation (LANDMINES §340):
    // Terrain dropping below -0.45m into the outer cloud abyss is free air, never a submerged lakebed,
    // while deep wet channels inside the shore basin stay active down to -2.20m.
    float inWetChannel = 1.0 - smoothstep(-0.10, 0.35, smoothSignedShore);
    float waterElevationMask = mix(smoothstep(-0.45, -0.15, fragWorldPos.y),
                                   smoothstep(-2.20, -1.60, fragWorldPos.y),
                                   inWetChannel);
    lakeDepthFactor *= waterElevationMask;
    shoreFactor *= waterElevationMask;

    // ---- 5. Geometry Normal & Slope ----
    vec3 geomNormal = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    vec3 geomWorldNormal = fragWorldNormal * inversesqrt(max(dot(fragWorldNormal, fragWorldNormal), 1e-8));
    // True geometric face slope from screen-space world derivatives (uniform control flow):
    // prevents smoothed vertex normals from keeping the top ~1.5m of steep cliff dropoffs green.
    vec3 dpdx = dFdx(fragWorldPos);
    vec3 dpdy = dFdy(fragWorldPos);
    vec3 faceCross = cross(dpdx, dpdy);
    float faceLenSq = dot(faceCross, faceCross);
    float geoSlope = (faceLenSq > 1e-10)
        ? clamp(1.0 - abs(faceCross.y) * inversesqrt(faceLenSq), 0.0, 1.0)
        : 0.0;
    float smoothSlope = clamp(1.0 - geomWorldNormal.y, 0.0, 1.0);
    float slope = max(smoothSlope, geoSlope);

    // Plateau rim drops into the abyss strictly below -0.55m (LANDMINES.md §135, §398)
    // so lower interior stream bends (~ -0.35m) do not falsely trigger outer cliff rock polygons.
    float outerCliffElev = (1.0 - smoothstep(-1.10, -0.18, fragWorldPos.y))
                         * (1.0 - smoothstep(0.02, 0.38, shoreFactor))
                         + (1.0 - smoothstep(-1.60, -0.75, fragWorldPos.y))
                         * (1.0 - smoothstep(0.02, 0.38, shoreFactor));
    outerCliffElev = clamp(outerCliffElev, 0.0, 1.0);
    // Dampen single-quad geometric face slope on interior arid dunes to prevent 1m square rock blotches
    slope = mix(slope, max(smoothSlope, geoSlope * 0.55), u_aridBiomeFactor * (1.0 - outerCliffElev));

    // ---- 6. Soil Detail ----
    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.015
                     || (smoothSignedShore < 1.45 && fragWorldPos.y >= -0.45)
                     || streamWet > 0.80
                     || slope > 0.35
                     || outerCliffElev > 0.05
                     || fragWorldPos.y < -0.15
                     || u_aridBiomeFactor > 0.05;
    vec4 colorDirt = vec4(0.5, 0.5, 0.5, 1.0);
    vec3 dirtDetail = vec3(0.5);
    if (soilPossible) {
        colorDirt = textureLod(texPath, tiledUV * 0.85, mipTiled);
        if (nearDualScale) {
            vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
            vec3 broadDirt = textureLod(texPath, broadDirtUV, mipTiled).rgb;
            vec3 broadMod = clamp(broadDirt / max(dot(broadDirt, vec3(0.3333)), 0.18), vec3(0.80), vec3(1.22));
            float broadWeight = mix(0.45, 0.32, u_aridBiomeFactor);
            dirtDetail = clamp(colorDirt.rgb * mix(vec3(1.0), broadMod, broadWeight * dualScaleFade), vec3(0.0), vec3(1.0));
        } else {
            dirtDetail = colorDirt.rgb;
        }
    }

    float wPath = 0.0;
    float wWetSoil = 0.0;
    float wCliff = 0.0;
    float wDrySoil = 0.0;
    float wGrass = 1.0;

    if (soilPossible) {
        if (distToPath < PATH_INFLUENCE) {
            float pathMeander = sin(fragWorldPos.x * 0.85 + fragWorldPos.z * 1.15) * 0.22
                              + sin(fragWorldPos.x * 2.30 - fragWorldPos.z * 1.70) * 0.11;
            distToPath += pathMeander;
            if (u_ecologyEnabled != 0)
                distToPath += (dirtDetail.r - 0.5) * 0.18;
        }

        // ---- 7. Four-Layer Weights & Height-Based Relief Blending ----
        wPath = 1.0 - smoothstep(1.10, 1.95, distToPath);
        float wPathMargin = smoothstep(0.95, 1.65, distToPath) * (1.0 - smoothstep(1.65, 3.1, distToPath));
        float streamShoreMask = (u_ecologyEnabled != 0) ? smoothstep(0.86, 0.98, streamWet) : smoothstep(0.74, 0.96, streamWet);
        wWetSoil = max(shoreFactor * 0.85, streamShoreMask);
        if (lakeDepthFactor > 0.0) {
            wWetSoil = mix(wWetSoil, 1.0, clamp(lakeDepthFactor * 2.0, 0.0, 1.0));
        }

        // Separate interior grassy terrace embankments and riverbeds from the outer island cliff rim (LANDMINES.md §135)
        float rimMask = outerCliffElev * (1.0 - smoothstep(0.0, 0.45, shoreFactor));
        float cliffSlopeGate = smoothstep(0.36, 0.60, slope) * (1.0 - smoothstep(0.02, 0.40, shoreFactor));
        float wSlope = max(cliffSlopeGate, rimMask * smoothstep(0.18, 0.38, slope));
        float outerAbyssMask = outerCliffElev * smoothstep(0.12, 0.30, slope);
        // In arid desert mode, only steep slopes or the outer cliff rim become bedrock; gentle dunes stay sand
        float rockCliff = mix(max(smoothstep(0.48, 0.82, rockScree), outerAbyssMask),
                              max(smoothstep(0.65, 0.92, rockScree) * smoothstep(0.18, 0.40, slope), outerAbyssMask),
                              u_aridBiomeFactor);
        wCliff = clamp(max(wSlope, max(rockCliff, outerAbyssMask)), 0.0, 1.0) * (1.0 - shoreFactor * 0.99);

        // Organic 3-Zone Riparian & Riverbed System (GPU Gems 3 Continuous Isosurface Blending):
        // Zone 1: Submerged riverbed (variableShore < -0.04m) -> 0% grass, 100% alluvial sand & shingle
        // Zone 2: Bank transition margin (-0.04m .. 0.35m) -> organic turf drift & gravel bars
        // Zone 3: Riparian meadow (variableShore > 0.35m) -> lush meadow turf
        if (u_ecologyEnabled != 0) {
            vec2 warpXZ = fragWorldPos.xz + vec2(
                sin(fragWorldPos.z * 0.41 + fragWorldPos.x * 0.19) * 0.35,
                cos(fragWorldPos.x * 0.37 - fragWorldPos.z * 0.23) * 0.35);
            float bankNoiseMacro = sin(warpXZ.x * 0.31 + warpXZ.y * 0.24) * 0.22
                                 + cos(warpXZ.y * 0.53 - warpXZ.x * 0.39) * 0.14;
            float bankNoiseMed   = sin(warpXZ.x * 1.27 - warpXZ.y * 0.91) * 0.08
                                 + cos(warpXZ.y * 1.73 + warpXZ.x * 1.19) * 0.05;
            float bankNoiseMicro = sin(warpXZ.x * 3.70 + warpXZ.y * 2.50) * 0.03
                                 + cos(warpXZ.y * 4.90 - warpXZ.x * 3.30) * 0.02;
            variableShore = smoothSignedShore + bankNoiseMacro + bankNoiseMed + bankNoiseMicro;

            // Smooth, non-periodic transition across alluvial banks without comb teeth
            float grassTaper = smoothstep(-0.02, 0.42, variableShore);
            float rosetteFringe = smoothstep(0.02, 0.34, variableShore + bankNoiseMicro * 0.75);
            wGrass = wGrass * mix(grassTaper, rosetteFringe, 0.46);

            float riparianFringe = 1.0 - smoothstep(-0.04, 0.44, variableShore);
            wWetSoil = max(wWetSoil, riparianFringe * 0.94);
        }

        // When descending off the meadow lip into the cliff, rock bedrock dominates completely:
        float cliffDomination = smoothstep(0.08, 0.60, wCliff);
        wGrass *= (1.0 - cliffDomination);
        wWetSoil *= (1.0 - cliffDomination);

        wDrySoil = clamp(max(wPathMargin * 0.78, wCliff), 0.0, 1.0)
                       * (1.0 - wWetSoil * 0.85) * (1.0 - wPath * 0.85);
        if (u_aridBiomeFactor > 0.05) {
            float aridCover = (u_ecologyEnabled != 0) ? smoothstep(0.32, 0.78, ecology.r) : 0.12;
            float desertDry = mix(0.95, 0.42, aridCover) * u_aridBiomeFactor;
            wDrySoil = max(wDrySoil, desertDry * (1.0 - wWetSoil * 0.90) * (1.0 - wPath * 0.90));
        }
        wDrySoil = max(wDrySoil, wCliff);
        wGrass = clamp(wGrass - wPath - wDrySoil, 0.0, 1.0) * (1.0 - step(0.5, u_aridBiomeFactor));
        wWetSoil = clamp(1.0 - wGrass - wPath - wDrySoil, 0.0, 1.0);
    }


    vec4 weights = vec4(wGrass, wDrySoil, wWetSoil, wPath);
    vec4 resolved = weights / max(dot(weights, vec4(1.0)), 0.0001);
    vec2 litterClasses = vec2(0.5);
    float reliefShadow = 1.0;

    if (highDetailGround && soilPossible && u_groundReliefEnabled != 0 && detailResolve > 0.10 && camDistSqXZ < 400.0) {
        float combinedSoil = weights.y + weights.w;
        float dominantWeight = max(max(weights.x, combinedSoil), weights.z);
        float boundaryBlend = 1.0 - smoothstep(0.84, 0.97, dominantWeight);
        if (boundaryBlend > 0.005) {
            vec4 substrateRelief = textureLod(u_groundRelief, tiledUV, mipTiled);
            litterClasses = substrateRelief.ba;
            float soilHeight = substrateRelief.g;
            vec3 groupWeights = vec3(weights.x, combinedSoil, weights.z);
            vec3 groupRelief  = vec3(substrateRelief.r, soilHeight, soilHeight);
            vec3 heights = groupWeights + (groupRelief - 0.5) * 0.28;
            float peak = max(max(heights.x, heights.y), heights.z);
            vec3 candidates = max(heights - vec3(peak - 0.34), vec3(0.0)) * groupWeights;
            candidates /= max(dot(candidates, vec3(1.0)), 0.0001);
            float pathRatio = weights.w / max(combinedSoil, 0.0001);
            vec4 groupedResolved = vec4(candidates.x, candidates.y * (1.0 - pathRatio), candidates.z, candidates.y * pathRatio);
            resolved = mix(resolved, groupedResolved, detailResolve * 0.52 * boundaryBlend);

            float heightDelta = (substrateRelief.r - soilHeight) * (resolved.x - (1.0 - resolved.x));
            reliefShadow = clamp(1.0 + heightDelta * 0.28 * detailResolve * boundaryBlend, 0.85, 1.05);
        }
    }
    wGrass = resolved.x; wDrySoil = resolved.y;
    wWetSoil = resolved.z; wPath = resolved.w;

    float soilWeight = clamp(wDrySoil + wWetSoil + wPath, 0.0, 1.0);
    bool hasGrass = wGrass > LAYER_EPS;
    bool hasSoil  = soilWeight > LAYER_EPS;

    // ---- 8. Grass Layer & Distant 3D Faux-Canopy Relief ----
    vec3 grassAlbedo = vec3(0.0);
    float fineGrassLuma = 0.5;
    vec4 grassMaterial = vec4(0.5, 0.5, 1.0, 0.82);
    float distantTuftRelief = 0.0;
    vec2 distantTuftNormal = vec2(0.0);

    if (hasGrass) {
        vec4 colorGrass = textureLod(texGrass, tiledUV, mipTiled);
        vec3 blendedGrass = colorGrass.rgb;
        if (nearDualScale) {
            vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
            vec3 broadGrass = textureLod(texGrass, broadGrassUV, mipTiled).rgb;
            blendedGrass = mix(colorGrass.rgb, broadGrass, 0.28 * dualScaleFade);
        }
        if (highDetailGround && camDistSqXZ < 400.0) {
            float grassMatFade = 1.0 - smoothstep(225.0, 400.0, camDistSqXZ);
            vec4 sampledGrassMat = textureLod(texGrassMaterial, tiledUV, mipTiled);
            grassMaterial = mix(grassMaterial, sampledGrassMat, grassMatFade);
        }
        fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

        // High-frequency botanical moss/fiber micro-grain at close zoom (< 12m) — lush turf only (arid sand uses smooth wind ripples)
        if (highDetailGround && camDistSqXZ < 144.0 && u_aridBiomeFactor < 0.5) {
            float closeWeight = (1.0 - camDistSqXZ * (1.0 / 144.0)) * detailResolve;
            float fiberA = sin(fragWorldPos.x * 26.0 + fragWorldPos.z * 19.0)
                         * cos(fragWorldPos.x * -17.0 + fragWorldPos.z * 29.0);
            float fiberB = sin(fragWorldPos.x * 54.0 - fragWorldPos.z * 48.0);
            float microFiber = (fiberA * 0.65 + fiberB * 0.35) * closeWeight;
            blendedGrass *= 1.0 + microFiber * 0.14;
            fineGrassLuma = clamp(fineGrassLuma + microFiber * 0.08, 0.05, 0.95);
        }

        float turfDetail = dot(blendedGrass, vec3(0.2126, 0.7152, 0.0722));
        float detailContrast = clamp((turfDetail - 0.22) * 1.65, -0.26, 0.30);

        vec3 turfBase = mix(vec3(0.138, 0.212, 0.078), u_aridGrusColor, u_aridBiomeFactor);
        float tintLuma = dot(colDiffuse.rgb, vec3(0.299, 0.587, 0.114));
        if (tintLuma > 0.05) {
            vec3 normalizedTint = clamp(colDiffuse.rgb / tintLuma, vec3(0.78), vec3(1.22));
            turfBase *= mix(vec3(1.0), normalizedTint, 0.30);
        }

        vec3 humusPocketTone = mix(vec3(0.152, 0.168, 0.088), u_aridHardpanColor, u_aridBiomeFactor);
        float humusPocketMask = (1.0 - smoothstep(0.14, 0.28, turfDetail)) * detailResolve * mix(0.28, 0.62, u_aridBiomeFactor);
        turfBase = mix(turfBase, humusPocketTone, humusPocketMask);

        vec3 litterChroma = clamp(blendedGrass / max(turfDetail, 0.06), vec3(0.72), vec3(1.34));
        float chromaStrength = mix(0.34, 0.56, detailResolve) * (1.0 - u_aridBiomeFactor * 0.65);
        grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, chromaStrength) * (1.0 + detailContrast * mix(1.0, 1.85, u_aridBiomeFactor));

        vec3 litterTint = mix(vec3(1.0), vec3(1.06, 1.02, 0.92), litterClasses.x);
        litterTint *= mix(vec3(1.0), mix(vec3(0.93, 1.04, 0.93), vec3(1.04, 0.96, 0.86), u_aridBiomeFactor), litterClasses.y);
        grassAlbedo *= mix(vec3(1.0), litterTint, detailResolve * 0.32);

        float habW1 = sin(fragWorldPos.x * 0.085 + fragWorldPos.z * 0.055) * 0.5 + 0.5;
        float habW2 = sin(fragWorldPos.x * -0.065 + fragWorldPos.z * 0.12 + 1.8) * 0.5 + 0.5;
        float habW3 = sin(fragWorldPos.x * 0.18 - fragWorldPos.z * 0.14 + 0.6) * 0.5 + 0.5;
        float analyticHabitat = habW1 * 0.50 + habW2 * 0.35 + habW3 * 0.15;
        float habitatVal = (u_ecologyEnabled != 0) ? mix(analyticHabitat, ecology.a, 0.25) : analyticHabitat;

        vec3 aridCanopy = mix(u_aridHardpanColor * 1.12, u_aridGrusColor * 1.06, habitatVal);
        vec3 targetCanopyColor = mix(MeadowCanopyColor(habitatVal), aridCanopy, u_aridBiomeFactor);
        // Deep verdant moss-clover undergrowth beneath 3D grass tufts (or dry ochre crust in arid biome)
        vec3 lushSubstrate = mix(vec3(0.086, 0.148, 0.050), targetCanopyColor * 0.50, 0.54);
        vec3 aridSubstrate = mix(u_aridHardpanColor, u_aridGrusColor, 0.45 + 0.25 * detailContrast);
        vec3 nearCanopySubstrate = mix(lushSubstrate, aridSubstrate, u_aridBiomeFactor);
        grassAlbedo = mix(grassAlbedo,
                          nearCanopySubstrate * (1.0 + detailContrast * (0.58 + GS_NEAR_DETAIL_BOOST * detailResolve)),
                          0.56 - GS_NEAR_DETAIL_BOOST * 0.42 * detailResolve);

        if (u_ecologyEnabled != 0)
            grassAlbedo *= mix(vec3(1.0), mix(vec3(0.90, 0.99, 0.92), vec3(1.04, 0.98, 0.90), u_aridBiomeFactor), ecology.b);

        if (camDistSqXZ > 64.0) {
            float camDist = camDistSqXZ * inversesqrt(camDistSqXZ);
            float distantCanopyBlend = smoothstep(12.0, 36.0, camDist) * wGrass;
            if (u_ecologyEnabled != 0)
                distantCanopyBlend *= mix(0.76, 1.0, smoothstep(0.04, 0.48, ecology.r)) * (1.0 - wCliff);
            if (distantCanopyBlend > 0.002) {
                vec3 canopyColor = targetCanopyColor * GS_DISTANT_CANOPY_GAIN;

                // [VISUAL UPGRADE]: Isotropic domain-warped landscape macro-variation (Ghost of Tsushima style)
                // Replaces axis-aligned Cartesian trig waffle grid and planar diagonal sine waves.
                vec2 pMacro = fragWorldPos.xz * 0.048;
                vec2 pWarp = vec2(sin(pMacro.y * 1.6 + pMacro.x * 0.5), cos(pMacro.x * 1.4 - pMacro.y * 0.8)) * 0.42;
                vec2 pRot = mat2(0.80, -0.60, 0.60, 0.80) * (pMacro + pWarp) * 1.65;
                float macroA = sin((pMacro.x + pWarp.x) * 1.9 + (pMacro.y + pWarp.y) * 1.2) * 0.5 + 0.5;
                float macroB = cos(pRot.x * 1.7 - pRot.y * 1.3) * 0.5 + 0.5;
                float hillCrestNoise = macroA * 0.60 + macroB * 0.40;

                // Procedural velvet tuft micro-relief for distant grassy hillsides (prevents bald plastic look)
                float tuftWaveA = sin(fragWorldPos.x * 4.6 + cos(fragWorldPos.z * 3.9));
                float tuftWaveB = cos(fragWorldPos.z * 5.1 - sin(fragWorldPos.x * 4.2));
                float tuftGrain = clamp(mix(hillCrestNoise, 0.5 + (turfDetail - 0.22) * 0.65, 0.45), 0.0, 1.0);
                distantTuftRelief = (tuftWaveA * tuftWaveB) * 0.16 * distantCanopyBlend * (1.0 - u_aridBiomeFactor);
                distantTuftNormal = vec2(tuftWaveA, tuftWaveB) * (0.14 * distantCanopyBlend * (1.0 - u_aridBiomeFactor));

                canopyColor *= mix(0.92, 1.08, tuftGrain);

                float hillCrestMask = smoothstep(0.20, 0.72, hillCrestNoise);
                canopyColor = mix(canopyColor * vec3(0.94, 1.01, 0.96), canopyColor * vec3(1.06, 1.03, 0.90), hillCrestMask * 0.35);

                float macroPatch = v_macroPatchAndField.x;
                float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
                canopyColor *= mix(vec3(0.96, 1.02, 0.94), vec3(1.04, 1.03, 0.92), warmShift * 0.40);

                grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.92);
            }
        }

        float grassAlbedoLuma = dot(grassAlbedo, vec3(0.2126, 0.7152, 0.0722));
        grassAlbedo = mix(vec3(grassAlbedoLuma), grassAlbedo, GS_GRASS_CHROMA);
    }

    // ---- 9. Cliff Strata, Woodland Loam & River Shingle ----
    float pebbleRelief = 0.0;
    vec2 proceduralPebbleNormal = vec2(0.0);
    float isRiparian = clamp(shoreFactor * 1.25 + streamWet * 1.50 + lakeDepthFactor * 2.0, 0.0, 1.0);
    vec3 blendedAlbedo = grassAlbedo * wGrass;
    vec3 cliffTriNormal = geomNormal;
    float cliffOcclusion = 1.0;
    float cliffRoughnessMod = 0.0;

    if (hasSoil) {
        float porousDarkening = smoothstep(0.0, 0.45, wWetSoil) * (1.0 - wWetSoil * 0.35);
        vec3 soilDetailTex = dirtDetail;
        float soilLuma = dot(soilDetailTex, vec3(0.299, 0.587, 0.114));
        pebbleRelief = smoothstep(0.26, 0.72, soilLuma);
        vec3 loamDark  = mix(vec3(0.10, 0.08, 0.05), u_aridHardpanColor * 0.74, u_aridBiomeFactor);
        vec3 loamLight = mix(vec3(0.18, 0.14, 0.09), u_aridGrusColor * 1.12, u_aridBiomeFactor);
        vec3 warmLoamColor = mix(loamDark, loamLight, pebbleRelief) * (0.86 + 0.28 * soilDetailTex) * (1.0 - porousDarkening * 0.32);
        if (u_aridBiomeFactor > 0.05) {
            // Cohesive wind-swept desert sand palette (smooth macro dune variation, no blotchy patches)
            vec2 dP = fragWorldPos.xz * 0.042;
            vec2 dWarp = vec2(sin(dP.y * 1.4 + dP.x * 0.6), cos(dP.x * 1.3 - dP.y * 0.7)) * 0.45;
            float dMacro = sin((dP.x + dWarp.x) * 1.5 + (dP.y + dWarp.y) * 1.0) * 0.5 + 0.5;
            vec3 paleQuartz = u_aridGrusColor * vec3(1.16, 1.12, 1.02);
            vec3 warmBuff   = u_aridGrusColor * vec3(1.06, 0.96, 0.82);
            vec3 desertSand = mix(warmBuff, paleQuartz, dMacro);
            warmLoamColor = mix(warmLoamColor, desertSand * (0.93 + 0.14 * soilDetailTex), u_aridBiomeFactor * 0.88);
        }

        vec3 cliffAlbedo = warmLoamColor;
        if (wCliff > 0.01) {
            vec2 triUVx = fragWorldPos.zy * 0.35;
            vec2 triUVy = fragWorldPos.xz * 0.35;
            vec2 triUVz = fragWorldPos.xy * 0.35;
            vec2 triDxX = wpDx3.zy * 0.35, triDyX = wpDy3.zy * 0.35;
            vec2 triDxY = wpDx * 0.35, triDyY = wpDy * 0.35;
            vec2 triDxZ = wpDx3.xy * 0.35, triDyZ = wpDy3.xy * 0.35;

            vec3 absWN = abs(geomWorldNormal);
            vec3 triWeights = absWN * absWN * absWN * absWN;
            triWeights /= max(triWeights.x + triWeights.y + triWeights.z, 0.0001);

            vec3 rockTexX, rockTexY, rockTexZ;
            vec3 rockNormX = vec3(0.0, 0.0, 1.0);
            vec3 rockNormY = vec3(0.0, 0.0, 1.0);
            vec3 rockNormZ = vec3(0.0, 0.0, 1.0);

            float triThresh = highDetailGround ? 0.02 : 0.14;
            if (u_groundRockEnabled != 0) {
                rockTexX = (triWeights.x > triThresh) ? textureLod(u_groundRock, triUVx, mipWorld).rgb : vec3(0.0);
                rockTexY = (triWeights.y > triThresh) ? textureLod(u_groundRock, triUVy, mipWorld).rgb : vec3(0.0);
                rockTexZ = (triWeights.z > triThresh) ? textureLod(u_groundRock, triUVz, mipWorld).rgb : vec3(0.0);

                rockNormX = (triWeights.x > triThresh) ? (textureLod(u_groundRockNormal, triUVx, mipWorld).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
                rockNormY = (triWeights.y > triThresh) ? (textureLod(u_groundRockNormal, triUVy, mipWorld).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
                rockNormZ = (triWeights.z > triThresh) ? (textureLod(u_groundRockNormal, triUVz, mipWorld).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);

                if (nearDualScale) {
                    vec2 dUVx = triUVx * 3.2;
                    vec2 dUVy = triUVy * 3.2;
                    vec2 dUVz = triUVz * 3.2;

                    if (triWeights.x > 0.02) {
                        vec3 detailX = textureLod(u_groundRock, dUVx, mipWorld).rgb;
                        rockTexX = mix(rockTexX, detailX * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormX = textureLod(u_groundRockNormal, dUVx, mipWorld).rgb * 2.0 - 1.0;
                        rockNormX = mix(rockNormX, detailNormX, 0.42 * dualScaleFade);
                    }
                    if (triWeights.y > 0.02) {
                        vec3 detailY = textureLod(u_groundRock, dUVy, mipWorld).rgb;
                        rockTexY = mix(rockTexY, detailY * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormY = textureLod(u_groundRockNormal, dUVy, mipWorld).rgb * 2.0 - 1.0;
                        rockNormY = mix(rockNormY, detailNormY, 0.42 * dualScaleFade);
                    }
                    if (triWeights.z > 0.02) {
                        vec3 detailZ = textureLod(u_groundRock, dUVz, mipWorld).rgb;
                        rockTexZ = mix(rockTexZ, detailZ * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormZ = textureLod(u_groundRockNormal, dUVz, mipWorld).rgb * 2.0 - 1.0;
                        rockNormZ = mix(rockNormZ, detailNormZ, 0.42 * dualScaleFade);
                    }
                }
            } else {
                rockTexX = (triWeights.x > triThresh) ? textureLod(texPath, triUVx, mipWorld).rgb : vec3(0.0);
                rockTexY = (triWeights.y > triThresh) ? textureLod(texPath, triUVy, mipWorld).rgb : vec3(0.0);
                rockTexZ = (triWeights.z > triThresh) ? textureLod(texPath, triUVz, mipWorld).rgb : vec3(0.0);
                if (nearDualScale) {
                    if (triWeights.x > 0.02) {
                        vec3 detailX = textureLod(texPath, triUVx * 2.8, mipWorld).rgb;
                        rockTexX = mix(rockTexX, detailX, 0.32 * dualScaleFade);
                    }
                    if (triWeights.y > 0.02) {
                        vec3 detailY = textureLod(texPath, triUVy * 2.8, mipWorld).rgb;
                        rockTexY = mix(rockTexY, detailY, 0.32 * dualScaleFade);
                    }
                    if (triWeights.z > 0.02) {
                        vec3 detailZ = textureLod(texPath, triUVz * 2.8, mipWorld).rgb;
                        rockTexZ = mix(rockTexZ, detailZ, 0.32 * dualScaleFade);
                    }
                }
            }
            vec3 triRock = rockTexX * triWeights.x + rockTexY * triWeights.y + rockTexZ * triWeights.z;

            // Authentic mountain slate-granite palette synchronized with cliff_rock.fs
            const float kStrikeAngle = 32.0 * 3.14159265 / 180.0;
            vec2 strikeDir = vec2(cos(kStrikeAngle), sin(kStrikeAngle));
            float strikeCoord = dot(fragWorldPos.xz, strikeDir);
            float strataCoord = fragWorldPos.y * 0.42 + strikeCoord * 0.16;
            float strataWarp = sin(fragWorldPos.x * 0.14 - fragWorldPos.z * 0.18) * 1.25;
            float strataMacro = sin(strataCoord * 2.40 + strataWarp) * 0.5 + 0.5;
            float strataFine  = sin(strataCoord * 6.80 + strataWarp * 1.8) * 0.5 + 0.5;
            float combinedStrata = strataMacro * 0.65 + strataFine * 0.35;

            float fissure = smoothstep(0.0, mix(0.28, 0.65, u_aridBiomeFactor), abs(sin(strikeCoord * 1.45 + strataWarp * 0.90)));
            vec3 slateBedrock = mix(vec3(0.45, 0.46, 0.50), vec3(0.56, 0.47, 0.38), u_aridBiomeFactor);
            vec3 feldsparVein = mix(vec3(0.64, 0.59, 0.52), vec3(0.76, 0.64, 0.50), u_aridBiomeFactor);
            vec3 stoneTint = mix(slateBedrock, feldsparVein, combinedStrata * mix(0.42, 0.72, u_aridBiomeFactor)) * 1.32;
            vec3 rockBase = triRock * stoneTint * mix(mix(0.76, 0.92, u_aridBiomeFactor), 1.04, fissure);

            // Velvet moss restricted strictly to flat horizontal ledges (subtle sand-dusting in arid mode)
            float ledgeFacing = smoothstep(0.76, 0.94, geomWorldNormal.y);
            vec3 lushLedgeMoss = mix(vec3(0.09, 0.14, 0.05), vec3(0.12, 0.18, 0.07), triRock.g);
            vec3 aridLedgeCrust = mix(rockBase * 1.05, u_aridGrusColor * (0.88 + 0.24 * triRock.g), 0.45);
            vec3 ledgeMoss = mix(lushLedgeMoss, aridLedgeCrust, u_aridBiomeFactor);
            cliffAlbedo = mix(rockBase, ledgeMoss, ledgeFacing * mix(0.36, 0.42, u_aridBiomeFactor));

            rockNormX.xy *= 2.6;
            rockNormY.xy *= 2.6;
            rockNormZ.xy *= 2.6;
            vec3 sgnN = sign(geomNormal);
            vec3 worldNx = vec3(rockNormX.z * sgnN.x, rockNormX.y, rockNormX.x * sgnN.x);
            vec3 worldNy = vec3(rockNormY.x, rockNormY.z * sgnN.y, rockNormY.y);
            vec3 worldNz = vec3(rockNormZ.x * sgnN.z, rockNormZ.y, rockNormZ.z * sgnN.z);
            vec3 rawCliffN = worldNx * triWeights.x + worldNy * triWeights.y + worldNz * triWeights.z;

            // GPU Gems 3 Ch. 1 Pre-Warped Stepped Horizontal Fracture Ledges & Exfoliation Shelves
            float shelfPhase = fragWorldPos.y * 2.80 + strikeCoord * 0.25 + strataWarp * 0.65;
            float shelfNormMod = (sin(shelfPhase) * 0.28 + sin(shelfPhase * 2.4) * 0.12 * u_strataStrength) * (1.0 + u_strataStrength * 1.15);
            rawCliffN.y += shelfNormMod * (1.0 - absWN.y);
            cliffTriNormal = rawCliffN * inversesqrt(max(dot(rawCliffN, rawCliffN), 1e-8));

            float normCrevice = clamp(rockNormX.z * triWeights.x + rockNormY.z * triWeights.y + rockNormZ.z * triWeights.z, 0.32, 1.0);
            cliffOcclusion = mix(0.58, 1.0, normCrevice * mix(0.78, 1.0, fissure));
            cliffRoughnessMod = (1.0 - normCrevice) * 0.14;
        }

        float cliffMix = smoothstep(0.02, mix(0.14, 0.55, u_aridBiomeFactor), wCliff);
        vec3 drySoilColor = mix(warmLoamColor, cliffAlbedo, cliffMix);

        // ---- AAA Alluvial Riverbed Sand & Natural Fine River Gravel ----
        vec3 wetSoilColor = vec3(0.0);
        if (isRiparian > 0.01) {
            // [VISUAL FIX]: Xóa sọc sóng cát tuần hoàn bằng nhiễu uốn miền (domain warp) nhẹ
            float warpSand = sin(fragWorldPos.x * 0.75 + fragWorldPos.z * 0.55) * 2.2
                           + cos(fragWorldPos.z * 1.15 - fragWorldPos.x * 0.85) * 1.4;
            float sandPhase1 = (fragWorldPos.z + fragWorldPos.x * 0.65) * 2.1 + warpSand;
            float sandPhase2 = (fragWorldPos.x - fragWorldPos.z * 0.55) * 3.2 - warpSand * 0.8;
            float sinP1 = sin(sandPhase1);
            float sinP2 = sin(sandPhase2);
            float sandRipple = (sinP1 * 0.60 + sinP2 * 0.40) * 0.5 + 0.5;

            // Rich mountain stream alluvial riverbed sand & gravel (Ghost of Tsushima / Where Winds Meet riverbed palette)
            vec3 drySand = mix(vec3(0.42, 0.40, 0.34), vec3(0.50, 0.47, 0.40), sandRipple * 0.09);
            vec3 wetSand = mix(vec3(0.35, 0.33, 0.28), vec3(0.42, 0.39, 0.34), sandRipple * 0.09);
            vec3 submergedSand = mix(vec3(0.36, 0.42, 0.38), vec3(0.44, 0.50, 0.44), sandRipple * 0.09);

            float wetLevel = clamp(shoreFactor * 0.85 + streamWet * 0.50, 0.0, 1.0);
            vec3 sandBase = mix(drySand, wetSand, wetLevel);
            sandBase = mix(sandBase, submergedSand, clamp(lakeDepthFactor * 1.35, 0.0, 1.0));

            // GPU Gems 3 §1.5: Triplanar slope compensation on cutting riverbanks to eliminate 2D stretching
            vec3 microSediment = textureLod(texPath, fragWorldPos.xz * 1.85, mipWorld).rgb;
            float bankCutSlope = clamp((1.0 - geomNormal.y) * 2.8, 0.0, 1.0);
            if (highDetailGround && bankCutSlope > 0.05) {
                vec2 sideUV = (abs(geomNormal.x) > abs(geomNormal.z)) ? fragWorldPos.zy * 1.85 : fragWorldPos.xy * 1.85;
                vec3 sideSediment = textureLod(texPath, sideUV, mipWorld).rgb;
                microSediment = mix(microSediment, sideSediment, bankCutSlope * 0.65);
            }
            sandBase *= (0.84 + 0.36 * microSediment.r) * (0.88 + 0.28 * colorDirt.rgb);

            float beachDryMix = clamp(shoreFactor * 0.35 + riparianSandMargin * 0.70, 0.0, 1.0) * (1.0 - smoothstep(0.04, 0.35, wCliff));
            drySoilColor = mix(drySoilColor, drySand * (0.88 + 0.28 * colorDirt.rgb), beachDryMix);

            vec2 gravelUV = tiledUV * 1.65;
            vec3 fineGravelTex = textureLod(texPath, gravelUV, mipTiled).rgb;

            float barNoise = sin(fragWorldPos.x * 0.65 + fragWorldPos.z * 0.50)
                           * cos(fragWorldPos.z * 0.85 - fragWorldPos.x * 0.40);
            float shingleBar = 0.35 + 0.65 * smoothstep(-0.20, 0.25, barNoise);

            float pebbleLuma = dot(fineGravelTex, vec3(0.299, 0.587, 0.114));
            float gravelHighlight = smoothstep(0.38, 0.70, pebbleLuma);
            float pebbleCrevice = 1.0 - smoothstep(0.24, 0.48, pebbleLuma);

            vec3 pebbleMineral = mix(vec3(0.44, 0.42, 0.37), vec3(0.62, 0.58, 0.50), gravelHighlight);
            pebbleMineral = mix(pebbleMineral, vec3(0.40, 0.45, 0.36), fineGravelTex.g);

            float gravelAO = 1.0 - pebbleCrevice * 0.22 * shingleBar;
            vec3 riverbedBed = mix(sandBase, pebbleMineral, gravelHighlight * 0.38 * shingleBar) * gravelAO;

            if (highDetailGround && camDistSqXZ < 400.0) {
                float cosP1 = cos(sandPhase1);
                float cosP2 = cos(sandPhase2);
                vec2 rippleNorm = clamp(vec2(cosP1 + cosP2 * 0.6, cosP1 - cosP2 * 0.5) * 0.048, vec2(-0.22), vec2(0.22));

                vec4 gravelMat = textureLod(texSoilMaterial, gravelUV, mipTiled);
                vec2 gNorm = (gravelMat.rg * 2.0 - 1.0);
                proceduralPebbleNormal = (gNorm * (0.44 * shingleBar + 0.20) + rippleNorm * (1.0 - shingleBar * 0.45)) * detailResolve;
            }

            // Exposed Riverbank Zonation (Zone 2):
            // 1. Waterline Shingle & Wet Alluvial Bar:
            //    Exposed river gravel, wet sand ripples, and rounded pebbles continuous with riverbed
            // 2. Mid-Bank Sand & Pebble Terrace:
            //    Golden-sand matrix with embedded quartzite pebbles
            vec3 exposedSandBar = mix(sandBase, pebbleMineral, gravelHighlight * 0.68 * shingleBar) * gravelAO;

            // 3. Upper-Bank Moss & Loam Fringe (lush oasis emerald fringe even in arid desert)
            vec3 aridOasisMoss = vec3(0.24, 0.34, 0.14);
            vec3 bankMossTone = mix(mix(vec3(0.15, 0.25, 0.09), vec3(0.23, 0.35, 0.13), dirtDetail.r), aridOasisMoss, u_aridBiomeFactor);
            vec3 bankLoamTone = mix(mix(vec3(0.24, 0.20, 0.15), vec3(0.33, 0.28, 0.21), dirtDetail.g), u_aridGrusColor * 0.96, u_aridBiomeFactor);
            vec3 upperBankFringe = mix(bankLoamTone, bankMossTone, 0.52 + 0.35 * microSediment.g) * (0.90 + 0.25 * colorDirt.rgb);

            float bankDist = max(variableShore, 0.0);
            float bankZoneMid = smoothstep(0.01, 0.22, bankDist);
            float bankZoneUpper = smoothstep(0.12, 0.44, bankDist);

            vec3 exposedBankSoil = mix(riverbedBed, exposedSandBar, bankZoneMid);
            exposedBankSoil = mix(exposedBankSoil, upperBankFringe, bankZoneUpper);

            float submergedBed = clamp(lakeDepthFactor * 2.5, 0.0, 1.0);
            wetSoilColor = mix(exposedBankSoil, riverbedBed, submergedBed);
        } else {
            vec3 mossyBank = vec3(0.32, 0.30, 0.24) * (0.85 + 0.35 * dirtDetail.g);
            wetSoilColor = mossyBank;
        }

        vec3 trailCore = mix(vec3(0.27, 0.20, 0.13), vec3(0.49, 0.38, 0.26), pebbleRelief) * (0.86 + 0.28 * colorDirt.rgb);
        vec3 pathMarginColor = mix(warmLoamColor, trailCore, wPath);

        blendedAlbedo += drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;
    }

    // Physically based capillary darkening: delicate 8% meniscus darkening restricted to narrow contact band
    float wetCapillaryDarken = (1.0 - smoothstep(0.00, 0.06, abs(variableShore))) * 0.08 * (1.0 - wGrass * 0.85);
    blendedAlbedo *= (1.0 - wetCapillaryDarken);

    // Subtle submerged bed depth tint: crystal-clear turquoise mountain water revealing golden riverbed pebbles
    if (lakeDepthFactor > 0.0) {
        vec3 bedWaterTint = mix(vec3(0.20, 0.54, 0.48), vec3(0.06, 0.28, 0.34), smoothstep(0.10, 1.00, lakeDepthFactor));
        blendedAlbedo = mix(blendedAlbedo, bedWaterTint * 0.86, smoothstep(0.10, 1.00, lakeDepthFactor) * 0.34);
    }

    float rockContactAO = 1.0;
    float debrisMaxDistSq = mix(1024.0, 25600.0, u_aridBiomeFactor);
    if (highDetailGround && u_groundDebrisEnabled != 0 && camDistSqXZ < debrisMaxDistSq) {
        float debrisDistFade = 1.0 - smoothstep(debrisMaxDistSq * 0.56, debrisMaxDistSq, camDistSqXZ);
        vec4 debris = textureLod(u_groundDebris, fragTexCoord, 0.0);
        float fineGrain = (soilPossible) ? dirtDetail.r : fineGrassLuma;
        float grassDebrisAtten = 1.0 - wGrass * 0.72;

        if (debris.b > 0.02) {
            float rawRockAO = 1.0 - smoothstep(0.18, 0.90, debris.b) * mix(0.68, 0.36, u_aridBiomeFactor) * debrisDistFade;
            rockContactAO = mix(rawRockAO, 1.0, clamp(lakeDepthFactor * 2.0, 0.0, 1.0));
        }

        float dryWoodlandMask = (1.0 - clamp(shoreFactor + streamWet, 0.0, 1.0)) * debrisDistFade * grassDebrisAtten;
        if (debris.r > 0.04 && dryWoodlandMask > 0.05) {
            vec3 lushLeafCol = mix(vec3(0.25, 0.18, 0.09), vec3(0.42, 0.29, 0.14), fineGrain);
            vec3 redLateriteCol = mix(vec3(0.56, 0.34, 0.20), vec3(0.68, 0.42, 0.26), fineGrain);
            vec3 debrisRCol = mix(lushLeafCol, redLateriteCol, u_aridBiomeFactor);
            float leafMask = smoothstep(0.10, 0.58, debris.r) * (0.40 + 0.60 * fineGrain) * dryWoodlandMask;
            blendedAlbedo = mix(blendedAlbedo, debrisRCol, leafMask * mix(0.36, 0.34, u_aridBiomeFactor));
        }

        // Gate scree gravel by steep slope in lush maps; in arid mode also show rock-apron grus on flats
        float gravelSlopeMin = mix(0.42, 0.24, 1.0 - smoothstep(-1.10, 0.15, fragWorldPos.y));
        float slopeGate = mix(smoothstep(gravelSlopeMin, gravelSlopeMin + 0.16, slope), 1.0, u_aridBiomeFactor);
        if (debris.g > 0.04 && slopeGate > 0.01) {
            vec3 lushGravel = mix(vec3(0.30, 0.28, 0.24), vec3(0.40, 0.38, 0.32), fineGrain);
            vec3 aridGrus   = u_aridGrusColor * (0.88 + 0.22 * fineGrain);
            vec3 gravelColor = mix(lushGravel, aridGrus, u_aridBiomeFactor);
            float gravelMask = smoothstep(0.16, 0.68, debris.g) * slopeGate * grassDebrisAtten * debrisDistFade;
            blendedAlbedo = mix(blendedAlbedo, gravelColor, gravelMask * mix(0.40, 0.38, u_aridBiomeFactor));
        }

        if (debris.a > 0.04) {
            vec3 lushMoss = mix(vec3(0.18, 0.30, 0.10), vec3(0.28, 0.42, 0.14), fineGrain);
            vec3 aridHardpan = u_aridHardpanColor * (1.16 + 0.20 * fineGrain);
            vec3 mossColor = mix(lushMoss, aridHardpan, u_aridBiomeFactor);
            float mossMask = smoothstep(0.12, 0.60, debris.a) * debrisDistFade * grassDebrisAtten;
            blendedAlbedo = mix(blendedAlbedo, mossColor, mossMask * mix(0.28, 0.28, u_aridBiomeFactor));
        }
    }

    float macroField = v_macroPatchAndField.y;
    float heightInfluence = clamp((fragWorldPos.y - 1.2) * 0.08, -0.20, 0.30);
    float crestWarmth = clamp(macroField + heightInfluence, 0.0, 1.0);
    blendedAlbedo *= mix(GS_CREST_COOL, GS_CREST_WARM, crestWarmth);

    if (camDistSqXZ > 1600.0 && fragWorldPos.y > 5.5) {
        float ridgeOutcrop = smoothstep(5.5, 11.5, fragWorldPos.y) * smoothstep(0.22, 0.52, slope);
        vec3 distantGranite = vec3(0.35, 0.34, 0.30);
        blendedAlbedo = mix(blendedAlbedo, distantGranite, ridgeOutcrop * 0.55);
    }

    // ---- 10. Tangent Frame & Specular AA ----
    vec4 soilMaterial = vec4(0.5, 0.5, 1.0, 0.8);
    if (highDetailGround && hasSoil && camDistSqXZ < 400.0) {
        float soilMatFade = 1.0 - smoothstep(225.0, 400.0, camDistSqXZ);
        vec4 sampledSoilMat = textureLod(texSoilMaterial, tiledUV * 0.85, mipTiled);
        soilMaterial = mix(soilMaterial, sampledSoilMat, soilMatFade);
    }

    vec3 normal = geomNormal;
    if (camDistSqXZ < 3600.0 || soilWeight > 0.01 || wCliff > 0.01 || isRiparian > 0.01) {
        vec3 rawMicro = mix(grassMaterial.rgb, soilMaterial.rgb, soilWeight) * 2.0 - 1.0;
        vec3 microNormal = rawMicro * inversesqrt(max(dot(rawMicro, rawMicro), 1e-8));
        vec3 refAxis = (abs(geomNormal.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 tRaw = cross(refAxis, geomNormal);
        vec3 tangent = tRaw * inversesqrt(max(dot(tRaw, tRaw), 1e-8));
        vec3 bitangent = cross(geomNormal, tangent);

        float puddleFlatten = 1.0 - smoothstep(0.60, 0.95, wWetSoil) * 0.45;
        float microStrength = mix(0.28, 0.52, max(soilWeight, wCliff)) * puddleFlatten * (0.45 + 0.55 * detailResolve);
        vec3 nRaw = geomNormal * (0.66 + 0.34 * microNormal.z)
                  + (tangent * microNormal.x + bitangent * microNormal.y) * microStrength;
        if (wGrass > 0.05) {
            nRaw += (tangent * distantTuftNormal.x + bitangent * distantTuftNormal.y) * wGrass;
        }
        if (isRiparian > 0.01) {
            float underwaterAtten = 1.0 - clamp(lakeDepthFactor * 1.4, 0.0, 0.75);
            nRaw += (tangent * proceduralPebbleNormal.x + bitangent * proceduralPebbleNormal.y) * (isRiparian * 0.58 * puddleFlatten * underwaterAtten);
        }
        if (wCliff > 0.01) {
            float cliffBlend = smoothstep(0.04, 0.40, wCliff);
            nRaw = mix(nRaw, cliffTriNormal, cliffBlend);
        }
        // Natural bifurcating asymmetrical aeolian sand ripples (smooth world-space warp, zero mesh-normal phase shear)
        if (u_aridBiomeFactor > 0.05 && camDistSqXZ < 900.0) {
            vec2 wDir = vec2(0.873, 0.487);
            vec2 wPerp = vec2(-0.487, 0.873);
            vec2 wDir2 = vec2(0.715, 0.699); // ~16 deg offset for natural ripple Y-bifurcations
            float along = dot(fragWorldPos.xz, wDir);
            float across = dot(fragWorldPos.xz, wPerp);
            float along2 = dot(fragWorldPos.xz, wDir2);

            float warpA = sin(fragWorldPos.x * 0.21 + fragWorldPos.z * 0.17) * 1.15
                        + cos(fragWorldPos.z * 0.43 - fragWorldPos.x * 0.31) * 0.55;
            float warpB = sin(across * 0.72 + warpA * 0.65) * 0.38;
            float rPhase1 = along * 17.5 + (warpA + warpB) * 1.8;
            float rPhase2 = along2 * 23.0 - warpA * 1.4;

            float fwR = fwidth(rPhase1);
            float rippleAA = 1.0 - smoothstep(1.6, 3.4, fwR);
            float rippleDist = 1.0 - smoothstep(400.0, 900.0, camDistSqXZ);

            // Patchy wind-exposure: ripples form on exposed dune backs and fade into smooth sand in hollows
            float windPatch = sin(fragWorldPos.x * 0.16 - fragWorldPos.z * 0.13 + warpA * 0.4)
                            * cos(fragWorldPos.z * 0.21 + fragWorldPos.x * 0.11);
            float patchMask = 0.25 + 0.75 * smoothstep(-0.20, 0.45, windPatch);
            float crossBlend = smoothstep(-0.35, 0.35, sin(fragWorldPos.x * 0.19 + fragWorldPos.z * 0.23));

            float rippleGate = (1.0 - smoothstep(0.24, 0.50, slope)) * (1.0 - wCliff * 0.80) * (1.0 - wGrass * 0.75) * (1.0 - isRiparian)
                             * rippleAA * rippleDist * patchMask * u_aridBiomeFactor;

            // Asymmetrical windward/leeward profile blended across bifurcating wave trains
            float s1 = cos(rPhase1) * 0.72 + cos(2.0 * rPhase1 - 0.55) * 0.28;
            float s2 = cos(rPhase2) * 0.72 + cos(2.0 * rPhase2 - 0.55) * 0.28;
            float t1 = 0.5 - 0.5 * (sin(rPhase1) * 0.76 + sin(2.0 * rPhase1 - 0.55) * 0.24);
            float t2 = 0.5 - 0.5 * (sin(rPhase2) * 0.76 + sin(2.0 * rPhase2 - 0.55) * 0.24);

            float waveSlope = mix(s1, s2, crossBlend * 0.45);
            float waveTrough = mix(t1, t2, crossBlend * 0.45);
            vec2 gradDir = normalize(mix(wDir, wDir2, crossBlend * 0.45));

            nRaw += vec3(gradDir.x, 0.0, gradDir.y) * (waveSlope * 0.15 * rippleGate);
            blendedAlbedo *= (1.0 - waveTrough * 0.048 * rippleGate);

            // Fine mineral sand grain within 12m (Mali-safe sin-less hash)
            if (camDistSqXZ < 144.0) {
                float grainFade = (1.0 - camDistSqXZ * (1.0 / 144.0)) * detailResolve * u_aridBiomeFactor;
                vec2 gCell = floor(fragWorldPos.xz * 48.0);
                vec2 gH = fract(gCell * vec2(0.1031, 0.1030));
                gH += dot(gH, gH.yx + 33.33);
                float g0 = fract((gH.x + gH.y) * gH.x) - 0.5;
                blendedAlbedo *= (1.0 + g0 * 0.032 * grainFade);
            }
        }
        normal = nRaw * inversesqrt(max(dot(nRaw, nRaw), 1e-8));
    }

    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.78, (wDrySoil + wPath) * 0.80);
    roughness = mix(roughness, clamp(0.72 + cliffRoughnessMod, 0.45, 0.92), wCliff * 0.85);
    float riparianRoughness = mix(0.78, 0.20, smoothstep(0.10, 0.85, max(shoreFactor, wWetSoil)));
    roughness = mix(roughness, riparianRoughness, max(isRiparian, wWetSoil) * 0.85);
    // Wet shoreline meniscus: darkens & polishes the 0..25cm waterline band
    float wetMeniscus = (1.0 - smoothstep(-0.04, 0.25, smoothSignedShore)) * waterElevationMask * (1.0 - wCliff);
    blendedAlbedo *= mix(1.0, 0.66, wetMeniscus * 0.75);
    roughness = mix(roughness, 0.16, wetMeniscus * 0.85);
    roughness = clamp(roughness, 0.16, 0.98);

    float nearCanopyMask = 1.0 - smoothstep(324.0, 1800.0, camDistSqXZ);
    float canopyContactAO = mix(1.0, 0.78 + 0.18 * fineGrassLuma, wGrass * nearCanopyMask);
    float cliffCreviceAO = mix(1.0, cliffOcclusion, smoothstep(0.04, 0.35, wCliff));
    float cavityAO = mix(0.86 + 0.14 * fineGrassLuma + distantTuftRelief * 0.35,
                         0.74 + 0.26 * pebbleRelief, soilWeight) * reliefShadow * cliffCreviceAO * canopyContactAO;
    cavityAO *= rockContactAO;
    cavityAO = clamp(cavityAO, 0.45, 1.08);

    // ---- 11. Physically Based Lighting & Soft Grass/Soil Shadows ----
    float rawNdotL = dot(normal, light);
    float soilWrapNdotL = max((rawNdotL + 0.34) * (1.0 / 1.34), 0.0);
    float grassWrapNdotL = max((rawNdotL + 0.42) * (1.0 / 1.42), 0.0);
    float horizonEnvelope = smoothstep(0.02, 0.38, max(light.y, 0.0)) * 0.66 + 0.34;
    float effectiveNdotL = mix(soilWrapNdotL * horizonEnvelope, grassWrapNdotL * horizonEnvelope, wGrass * 0.76) * (0.85 + 0.15 * cavityAO);

    float dynShadowCoverage;
    float rawShadow = MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, normal, light, dynShadowCoverage);
    float canopyFarField = wGrass * (1.0 - dynShadowCoverage)
                         * (1.0 - smoothstep(900.0, 2025.0, camDistSqXZ))
                         * smoothstep(0.08, 0.60, ecology.r);
    rawShadow *= 1.0 - GS_FAR_CANOPY_SHADE * canopyFarField;

    // [VISUAL FIX]: Làm mềm bóng đổ trên cả thảm cỏ lẫn bãi cát ven sông (xóa vệt bóng răng lược đen)
    // On submerged/wet creek sand, thin grass shadow casters alias into dark dashes; soften toward sky ambient.
    float wetSandMask = clamp(max(lakeDepthFactor * 4.0, isRiparian * 0.92), 0.0, 1.0);
    float softShadowVis = mix(sqrt(max(rawShadow, 0.0)), 1.0, mix(0.16, 0.62, wetSandMask));
    float resolvedShadow = mix(rawShadow, softShadowVis, max(wGrass * (1.0 - u_aridBiomeFactor), wetSandMask));
    float shadowFloor = mix(0.24, 0.48, max(wGrass * (1.0 - u_aridBiomeFactor), wetSandMask));
    float shadow = mix(shadowFloor, 1.0, resolvedShadow);
    float sunVisibility = (shadow > 0.001) ? shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos) : 0.0;

    float ambientShadowMod = mix(mix(0.90, 0.95, wGrass), 1.0, resolvedShadow);
    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient) * (cavityAO * ambientShadowMod);

    vec3 totalLight = ambient + actualLight * effectiveNdotL * sunVisibility;
    vec3 groundLit = blendedAlbedo * totalLight;

    vec3 viewDir = viewVec * inversesqrt(max(dot(viewVec, viewVec), 0.0001));
    vec3 halfSum = light + viewDir;
    vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
    float NdotH = max(dot(normal, halfDir), 0.0);

    if (wGrass > 0.05 && sunVisibility > 0.01) {
        float backScatter = max(dot(viewDir, -light), 0.0);
        float sssFactor = (backScatter * backScatter * backScatter) * wGrass * sunVisibility * horizonEnvelope * 0.16;
        groundLit += grassAlbedo * actualLight * vec3(1.16, 1.06, 0.52) * sssFactor;
    }

    // Cook-Torrance GGX Microfacet Specular for soil, sand, and bedrock
    float alpha = roughness * roughness;
    float alpha2 = max(alpha * alpha, 0.0004);
    float denomD = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D_GGX = alpha2 / (3.14159265 * denomD * denomD);
    float NdotV = max(dot(normal, viewDir), 0.001);
    float NdotL_clamped = max(dot(normal, light), 0.001);
    float k_direct = (roughness + 1.0);
    k_direct = (k_direct * k_direct) * 0.125;
    float G_Smith = (NdotV / (NdotV * (1.0 - k_direct) + k_direct)) *
                    (NdotL_clamped / (NdotL_clamped * (1.0 - k_direct) + k_direct));
    float VdotH = clamp(dot(viewDir, halfDir), 0.0, 1.0);
    float omvh = 1.0 - VdotH;
    float omvh2 = omvh * omvh;
    float F_Schlick = 0.04 + 0.96 * (omvh2 * omvh2 * omvh);
    float specGGX = (D_GGX * G_Smith * F_Schlick) / (4.0 * NdotV * NdotL_clamped + 0.001);
    vec3 drySpec = actualLight * specGGX * (NdotL_clamped * sunVisibility);
    groundLit += drySpec * (0.85 - wGrass * 0.65);

    // Quartz / mica grain sparkle on sunlit desert grus & granite (Mali-safe sin-less hash)
    if (u_aridBiomeFactor > 0.05 && sunVisibility > 0.01 && camDistSqXZ < 400.0 && NdotH > 0.72) {
        vec2 glintUV = fragWorldPos.xz * 26.0;
        vec2 cell = floor(glintUV);
        vec2 cellFrac = fract(glintUV);
        vec3 q3 = fract(cell.xyx * vec3(0.1031, 0.1030, 0.0973));
        q3 += dot(q3, q3.yzx + 33.33);
        vec2 cellRand = fract((q3.xx + q3.yz) * q3.zy);
        vec2 jitterCenter = 0.18 + 0.64 * cellRand;
        float grainRadius = 0.10 + 0.12 * fract(cellRand.x * 17.3 + cellRand.y * 31.7);
        float distToGrain = length(cellFrac - jitterCenter);
        float grainMask = 1.0 - smoothstep(grainRadius * 0.35, grainRadius, distToGrain);
        float quartzHash = fract((q3.x + q3.z) * q3.y);
        vec2 fw = fwidth(glintUV);
        float pixelFootprint = max(fw.x, fw.y);
        float aaFade = 1.0 - smoothstep(0.35, 0.85, pixelFootprint);
        float distFade = 1.0 - smoothstep(100.0, 400.0, camDistSqXZ);
        float quartzGlint = pow(NdotH, 48.0) * smoothstep(0.92, 0.995, quartzHash) * grainMask * aaFade * distFade;
        groundLit += actualLight * (quartzGlint * sunVisibility * u_aridBiomeFactor * 0.55);
    }

    // ---- 12. Capillary Wetness Mechanics (Tight meniscus waterline contact sheen) ----
    if ((wWetSoil > 0.04 || isRiparian > 0.04) && lakeDepthFactor < 0.45) {
        float waterlineBand = (1.0 - smoothstep(0.0, 0.36, abs(smoothSignedShore - 0.04)))
                            * (1.0 - smoothstep(0.05, 0.38, lakeDepthFactor));
        float oneMinusNdotV = 1.0 - max(dot(normal, viewDir), 0.0);
        float om2 = oneMinusNdotV * oneMinusNdotV;
        float fresnelWet = 0.04 + 0.96 * (om2 * om2 * oneMinusNdotV);

        float h2 = NdotH * NdotH;
        float h4 = h2 * h2;
        float h8 = h4 * h4;
        float h16 = h8 * h8;
        float h32 = h16 * h16;
        float wetSpecSharp = (h32 * h32) * 0.82;
        float wetSpecBroad = (h8 * h4) * 0.42;
        vec3 wetSheen = actualLight * (wetSpecSharp + wetSpecBroad * detailResolve) * (0.38 + fresnelWet * 0.62);
        vec3 skySheen = actualAmbient * (fresnelWet * 0.44) * cavityAO;
        groundLit += (wetSheen * sunVisibility + skySheen) * waterlineBand;
    }

    if (lakeDepthFactor > 0.0) {
        groundLit += vec3(0.015, 0.035, 0.028) * actualAmbient * (1.0 - lakeDepthFactor);
    }

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    if (u_aridBiomeFactor < 0.5 && wCliff > 0.02 && fragWorldPos.y < 0.6) {
        float lowerSkirt = 1.0 - smoothstep(-5.50, 0.40, fragWorldPos.y);
        float cloudBounceFactor = max(-normal.y * 0.65 + 0.32, 0.0) * lowerSkirt;
        vec3 cloudBounce = (actualAmbient * vec3(0.94, 0.95, 0.98) + actualLight * vec3(0.42, 0.38, 0.30)) * (cloudBounceFactor * 1.35);
        groundLit += blendedAlbedo * cloudBounce;
    }

    if (veil > 0.001) {
        groundLit = mix(groundLit, cloudVeilColor, veil);
    }

    // Gentle golden-hour aerial perspective over distant hills (35m..320m for MOBA & OW)
    if (camDistSqXZ > 1225.0) {
        float aerialEndSq = mix(21025.0, 102400.0, u_aridBiomeFactor);
        float aerialFactor = smoothstep(1225.0, aerialEndSq, camDistSqXZ) * (1.0 - smoothstep(2.5, 11.5, fragWorldPos.y) * 0.35);
        vec3 horizonAerial = actualAmbient * vec3(0.88, 0.84, 0.78) + actualLight * vec3(0.28, 0.22, 0.16);
        groundLit = mix(groundLit, horizonAerial, aerialFactor * mix(0.28, 0.36, u_aridBiomeFactor));
    }

    if (u_aridBiomeFactor < 0.5 && fragWorldPos.y < -1.80) {
        float cloudImmersion = 1.0 - smoothstep(-4.80, -1.80, fragWorldPos.y);
        vec3 cloudBaseTone = mix(vec3(0.56, 0.62, 0.72), vec3(0.95, 0.92, 0.86), 0.55);
        vec3 cloudHazeLight = actualAmbient * 0.85 + actualLight * (0.42 * max(light.y + 0.35, 0.25));
        vec3 cloudHazeTarget = cloudBaseTone * cloudHazeLight;
        groundLit = mix(groundLit, cloudHazeTarget, cloudImmersion * 0.92);
    }

    finalColor = vec4(groundLit, 1.0);
}
#version 330

// Đợt E / E2 — VFX point lights.
#include "core/shaders/common/vfx_lights.glsl"
// Preserve thin grass silhouettes without adding shadow-map samples.
// Bake tier-1 bilinear PCF into terrain receiver (cuts 9-tap penumbra loop to 4 taps; grass shadows are softened below).
#define MAP_DYNAMIC_SHADOW_RADIUS 1.35
#define MAP_SHADOW_FILTER_QUALITY 1

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/meadow_palette.glsl"

// ---- Look knobs ( tinh chỉnh hài hòa màu cỏ & nền đất cho song_quao ) ----
#define GS_DISTANT_CANOPY_GAIN 0.68
#define GS_CREST_COOL vec3(0.92, 0.99, 0.94)
#define GS_CREST_WARM vec3(1.04, 1.03, 0.95)
#define GS_GRASS_CHROMA 0.96
#define GS_NEAR_DETAIL_BOOST 0.22
#define GS_FAR_CANOPY_SHADE 0.14

uniform sampler2D u_cloudNoise;
uniform sampler2D u_groundRelief;
uniform int u_groundReliefEnabled;
uniform sampler2D u_groundDebris;
uniform int u_groundDebrisEnabled;

in vec2 fragTexCoord;
in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragWorldPos;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;
in vec2 v_macroPatchAndField;

uniform vec4 colDiffuse;
uniform sampler2D texture0;         // Splatmap
uniform sampler2D texGrass;         // Meadow substrate detail
uniform sampler2D texPath;          // Soil / dirt detail
uniform sampler2D texGrassMaterial; // packed normal RGB, roughness A
uniform sampler2D texSoilMaterial;  // packed normal RGB, roughness A

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

out vec4 finalColor;

#define LAYER_EPS 0.003
#define PATH_INFLUENCE 4.0

void main()
{
    // ---- 0. Derivatives (Tính toàn bộ đạo hàm trong luồng đồng nhất trước discard/return) ----
    vec2 baseUvDx = dFdx(fragTexCoord), baseUvDy = dFdy(fragTexCoord);
    vec2 tiledUV = fragTexCoord * tiling;
    vec2 uvDx = baseUvDx * tiling, uvDy = baseUvDy * tiling;
    vec3 wpDx3 = dFdx(fragWorldPos), wpDy3 = dFdy(fragWorldPos);
    vec2 wpDx = wpDx3.xz, wpDy = wpDy3.xz;
    float pixelMetersSq = max(dot(wpDx, wpDx), dot(wpDy, wpDy));
    float detailResolve = 1.0 - smoothstep(0.000225, 0.0049, pixelMetersSq);

    // ---- 1. Splat Fetch & Early Abyss Discard ----
    vec4 splatSample = textureGrad(texture0, fragTexCoord, baseUvDx, baseUvDy);
    float streamWet = splatSample.a;
    float rockScree = splatSample.g;

    // Discard outer abyss floor outside the island perimeter
    if (rockScree > 0.85 && splatSample.r < 0.10 && fragWorldPos.y <= -7.2) {
        discard;
    }

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
    bool nearDualScale = (camDistSqXZ < 289.0); // Trong vòng 17m mới đọc texture xoay lớp thứ 2
    // [VISUAL FIX]: Hệ số chuyển mượt từ 14m -> 17m để xóa sổ đường viền nhảy bậc (LOD pop ring) tại 17m
    float dualScaleFade = 1.0 - smoothstep(196.0, 289.0, camDistSqXZ);

    // ---- 3. Distance to Path ----
    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float distToPath = 1000.0;
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    if (u_ecologyEnabled != 0) {
        vec2 ecoUV = (fragWorldPos.xz - u_ecologyRect.xy) / max(u_ecologyRect.zw, vec2(0.0001));
        ecology = textureLod(u_ecology, ecoUV, 0.0);
        vec4 metric = textureLod(u_ecologyDistance, ecoUV, 0.0);
        float roadU16 = (metric.r * 65280.0 + metric.g * 255.0) * (1.0 / 65535.0);
        distToPath = (roadU16 * 2.0 - 1.0) * 16.0 + 1.15;

        float shoreU16 = (metric.b * 65280.0 + metric.a * 255.0) * (1.0 / 65535.0);
        float signedShore = (shoreU16 * 2.0 - 1.0) * 16.0;
        // Organic continuous domain noise breaks discrete raster distance-transform wavefront scallops
        float shoreJitter = sin(fragWorldPos.x * 2.4 + fragWorldPos.z * 1.9) * 0.22
                          + cos(fragWorldPos.x * 4.8 - fragWorldPos.z * 3.9) * 0.11;
        float smoothSignedShore = signedShore + shoreJitter;
        if (smoothSignedShore < 0.0) {
            lakeDepthFactor = clamp(-smoothSignedShore * (1.0 / 2.6), 0.0, 1.0);
            shoreFactor = 1.0;
        } else {
            shoreFactor = 1.0 - smoothstep(0.0, 1.25, smoothSignedShore);
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
            float lakeDist = sqrt(lakeDistSq);
            float distWet = 1.0 - smoothstep(0.96, 1.25, lakeDist);
            shoreFactor = max(shoreFactor, distWet);
            if (lakeDist < 1.0) {
                lakeDepthFactor = max(lakeDepthFactor, 1.0 - lakeDist * mix(1.0, lakeDist, 0.6));
            }
        }
    }

    // ---- 5. Geometry Normal & Slope ----
    vec3 geomNormal = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // ---- 6. Soil Detail (Tightened gate skips dirt fetches on pure meadow turf) ----
    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.015
                     || streamWet > 0.45
                     || slope > 0.24
                     || rockScree > 0.62
                     || fragWorldPos.y < -1.15;
    vec4 colorDirt = vec4(0.5, 0.5, 0.5, 1.0);
    vec3 dirtDetail = vec3(0.5);
    if (soilPossible) {
        colorDirt = textureGrad(texPath, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85);
        if (nearDualScale) {
            vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
            vec3 broadDirt = textureGrad(texPath, broadDirtUV,
                                         vec2(-uvDx.y, uvDx.x) * 0.31,
                                         vec2(-uvDy.y, uvDy.x) * 0.31).rgb;
            vec3 broadMod = clamp(broadDirt / max(dot(broadDirt, vec3(0.3333)), 0.18), vec3(0.80), vec3(1.22));
            dirtDetail = clamp(colorDirt.rgb * mix(vec3(1.0), broadMod, 0.45 * dualScaleFade), vec3(0.0), vec3(1.0));
        } else {
            dirtDetail = colorDirt.rgb;
        }
    }

    if (distToPath < PATH_INFLUENCE) {
        float pathMeander = sin(fragWorldPos.x * 0.85 + fragWorldPos.z * 1.15) * 0.22
                          + sin(fragWorldPos.x * 2.30 - fragWorldPos.z * 1.70) * 0.11;
        distToPath += pathMeander;
        if (u_ecologyEnabled != 0)
            distToPath += (dirtDetail.r - 0.5) * 0.18;
    }

    // ---- 7. Four-Layer Weights & Height-Based Relief Blending ----
    float wPath = 1.0 - smoothstep(1.10, 1.95, distToPath);
    float wPathMargin = smoothstep(0.95, 1.65, distToPath) * (1.0 - smoothstep(1.65, 3.1, distToPath));
    float wWetSoil = max(shoreFactor * 0.95, smoothstep(0.55, 0.92, streamWet));
    if (lakeDepthFactor > 0.0) {
        wWetSoil = mix(wWetSoil, 1.0, clamp(lakeDepthFactor * 2.0, 0.0, 1.0));
    }

    // Transition steeper terrain banks and outer cliff rims into rock strata & scree
    float wSlope = smoothstep(0.08, 0.22, slope);
    float outerAbyssMask = 1.0 - smoothstep(-0.80, 0.80, fragWorldPos.y);
    float rockCliff = smoothstep(0.20, 0.65, rockScree);
    float wCliff = clamp(max(wSlope, max(rockCliff * 0.85, outerAbyssMask)), 0.0, 1.0);

    float wDrySoil = clamp(max(wPathMargin * 0.78, wCliff), 0.0, 1.0)
                   * (1.0 - wWetSoil * 0.85) * (1.0 - wPath * 0.85);
    float wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

    vec4 weights = vec4(wGrass, wDrySoil, wWetSoil, wPath);
    vec4 resolved = weights / max(dot(weights, vec4(1.0)), 0.0001);
    vec2 litterClasses = vec2(0.5);
    float reliefShadow = 1.0;

    if (u_groundReliefEnabled != 0 && detailResolve > 0.10) {
        float combinedSoil = weights.y + weights.w;
        float dominantWeight = max(max(weights.x, combinedSoil), weights.z);
        bool boundary = dominantWeight < 0.96;
        if (boundary || (weights.x > 0.05 && nearDualScale)) {
            vec4 substrateRelief = textureGrad(u_groundRelief, tiledUV, uvDx, uvDy);
            litterClasses = mix(vec2(0.5), substrateRelief.ba, boundary ? 1.0 : dualScaleFade);
            if (boundary) {
                float soilHeight = substrateRelief.g;
                vec3 groupWeights = vec3(weights.x, combinedSoil, weights.z);
                vec3 groupRelief  = vec3(substrateRelief.r, soilHeight, soilHeight);
                vec3 heights = groupWeights + (groupRelief - 0.5) * 0.28;
                float peak = max(max(heights.x, heights.y), heights.z);
                vec3 candidates = max(heights - vec3(peak - 0.34), vec3(0.0)) * groupWeights;
                candidates /= max(dot(candidates, vec3(1.0)), 0.0001);
                float pathRatio = weights.w / max(combinedSoil, 0.0001);
                vec4 groupedResolved = vec4(candidates.x, candidates.y * (1.0 - pathRatio), candidates.z, candidates.y * pathRatio);
                resolved = mix(resolved, groupedResolved, detailResolve * 0.52);

                float heightDelta = (substrateRelief.r - soilHeight) * (resolved.x - (1.0 - resolved.x));
                reliefShadow = clamp(1.0 + heightDelta * 0.28 * detailResolve, 0.85, 1.05);
            }
        }
    }
    wGrass = resolved.x; wDrySoil = resolved.y;
    wWetSoil = resolved.z; wPath = resolved.w;

    float soilWeight = clamp(wDrySoil + wWetSoil + wPath, 0.0, 1.0);
    bool hasGrass = wGrass > LAYER_EPS;
    bool hasSoil  = soilWeight > LAYER_EPS;

    // ---- 8. Grass Layer (Hài hòa màu nền cỏ & giảm đốm loang ở xa) ----
    vec3 grassAlbedo = vec3(0.0);
    float fineGrassLuma = 0.5;
    vec4 grassMaterial = vec4(0.5, 0.5, 1.0, 0.82);
    float distantTuftRelief = 0.0;

    if (hasGrass) {
        vec4 colorGrass = textureGrad(texGrass, tiledUV, uvDx, uvDy);
        vec3 blendedGrass = colorGrass.rgb;
        if (nearDualScale) {
            vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
            vec3 broadGrass = textureGrad(texGrass, broadGrassUV,
                                          vec2(uvDx.y, -uvDx.x) * 0.38,
                                          vec2(uvDy.y, -uvDy.x) * 0.38).rgb;
            blendedGrass = mix(colorGrass.rgb, broadGrass, 0.28 * dualScaleFade);
        }
        // [TỐI ƯU BĂNG THÔNG & CHỐNG POP]: Chỉ đọc normal/roughness map của cỏ trong bán kính 32m
        // và pha mượt về giá trị phẳng từ 28m -> 32m.
        if (camDistSqXZ < 1024.0) {
            float grassMatFade = 1.0 - smoothstep(784.0, 1024.0, camDistSqXZ);
            vec4 sampledGrassMat = textureGrad(texGrassMaterial, tiledUV, uvDx, uvDy);
            grassMaterial = mix(grassMaterial, sampledGrassMat, grassMatFade);
        }
        fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

        // High-frequency botanical moss/fiber micro-grain at close zoom (< 12m, e.g. Zoom 10.28x)
        if (camDistSqXZ < 144.0) {
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

        // Nền cỏ xanh rêu tự nhiên, tiệp hoàn toàn với gốc và thân lá cỏ 3D
        vec3 turfBase = vec3(0.155, 0.228, 0.086);
        float tintLuma = dot(colDiffuse.rgb, vec3(0.299, 0.587, 0.114));
        if (tintLuma > 0.05) {
            vec3 normalizedTint = clamp(colDiffuse.rgb / tintLuma, vec3(0.78), vec3(1.22));
            turfBase *= mix(vec3(1.0), normalizedTint, 0.30);
        }

        vec3 humusPocketTone = vec3(0.152, 0.168, 0.088);
        float humusPocketMask = (1.0 - smoothstep(0.14, 0.28, turfDetail)) * detailResolve * 0.28;
        turfBase = mix(turfBase, humusPocketTone, humusPocketMask);

        vec3 litterChroma = clamp(blendedGrass / max(turfDetail, 0.06), vec3(0.72), vec3(1.34));
        float chromaStrength = mix(0.34, 0.56, detailResolve);
        grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, chromaStrength) * (1.0 + detailContrast);

        vec3 litterTint = mix(vec3(1.0), vec3(1.06, 1.02, 0.92), litterClasses.x);
        litterTint *= mix(vec3(1.0), vec3(0.93, 1.04, 0.93), litterClasses.y);
        grassAlbedo *= mix(vec3(1.0), litterTint, detailResolve * 0.32);

        float habitatVal;
        if (u_ecologyEnabled != 0) {
            habitatVal = ecology.a;
        } else {
            float turfNoise = sin(fragWorldPos.x * 0.15 + fragWorldPos.z * 0.10) * 0.5
                            + sin(fragWorldPos.x * -0.11 + fragWorldPos.z * 0.20 + 1.8) * 0.5;
            habitatVal = clamp(turfNoise * 0.5 + 0.5, 0.0, 1.0);
        }

        vec3 targetCanopyColor = MeadowCanopyColor(habitatVal);
        vec3 nearCanopySubstrate = targetCanopyColor * 0.58;
        grassAlbedo = mix(grassAlbedo,
                          nearCanopySubstrate * (1.0 + detailContrast * (0.68 + GS_NEAR_DETAIL_BOOST * detailResolve)),
                          0.56 - GS_NEAR_DETAIL_BOOST * 0.42 * detailResolve);

        if (u_ecologyEnabled != 0)
            grassAlbedo *= mix(vec3(1.0), vec3(0.90, 0.99, 0.92), ecology.b);

        if (camDistSqXZ > 64.0) {
            float camDist = sqrt(camDistSqXZ);
            float distantCanopyBlend = smoothstep(10.0, 36.0, camDist) * wGrass;
            if (u_ecologyEnabled != 0)
                distantCanopyBlend *= smoothstep(0.04, 0.48, ecology.r);
            if (distantCanopyBlend > 0.002) {
                vec3 canopyColor = targetCanopyColor * GS_DISTANT_CANOPY_GAIN;
                // Smooth organic turf variation (no periodic sin*cos corduroy grid)
                float tuftGrain = clamp(0.5 + (turfDetail - 0.22) * 0.55, 0.0, 1.0);
                distantTuftRelief = (tuftGrain - 0.5) * distantCanopyBlend * 0.28;
                canopyColor *= mix(0.88, 1.10, tuftGrain);

                // Macro emerald-to-golden tip harmony matching GrassShade (interpolated from vertex stage)
                float macroPatch = v_macroPatchAndField.x;
                float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
                canopyColor *= mix(vec3(0.95, 1.03, 0.92), vec3(1.06, 1.04, 0.88), warmShift * 0.60);

                float windPhase = fract(u_time * 0.31830988) * 6.2831853;
                float windWave = sin(dot(fragWorldPos.xz, vec2(0.26, 0.18)) - windPhase) * 0.5 + 0.5;
                canopyColor *= mix(vec3(0.97, 0.99, 0.95), vec3(1.05, 1.04, 0.96), windWave * distantCanopyBlend);
                grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.92);
            }
        }

        float grassAlbedoLuma = dot(grassAlbedo, vec3(0.2126, 0.7152, 0.0722));
        grassAlbedo = mix(vec3(grassAlbedoLuma), grassAlbedo, GS_GRASS_CHROMA);
    }

    // ---- 9. Cliff Strata, Woodland Loam & River Shingle ----
    float cliffStrataBump = 0.0;
    float pebbleRelief = 0.0;
    float riverPebbleNoise = 0.5;
    vec2 proceduralPebbleNormal = vec2(0.0);
    float isRiparian = clamp(shoreFactor * 1.25 + streamWet * 1.50 + lakeDepthFactor * 2.0, 0.0, 1.0);
    vec3 blendedAlbedo = grassAlbedo * wGrass;
    float vCragJoint = 0.5;
    float hCragJoint = 0.5;

    if (hasSoil) {
        float porousDarkening = smoothstep(0.0, 0.45, wWetSoil) * (1.0 - wWetSoil * 0.35);
        float soilLuma = dot(dirtDetail, vec3(0.299, 0.587, 0.114));
        pebbleRelief = smoothstep(0.26, 0.72, soilLuma);
        vec3 loamDark  = vec3(0.19, 0.148, 0.095);
        vec3 loamLight = vec3(0.38, 0.298, 0.200);
        vec3 warmLoamColor = mix(loamDark, loamLight, pebbleRelief) * (0.86 + 0.28 * dirtDetail) * (1.0 - porousDarkening * 0.18);

        vec3 cliffAlbedo = warmLoamColor;
        if (wCliff > 0.05) {
            vec2 triUVx = fragWorldPos.zy * 0.45;
            vec2 triUVy = fragWorldPos.xz * 0.45;
            vec2 triUVz = fragWorldPos.xy * 0.45;
            vec2 triDxX = wpDx3.zy * 0.45, triDyX = wpDy3.zy * 0.45;
            vec2 triDxY = wpDx * 0.45, triDyY = wpDy * 0.45;
            vec2 triDxZ = wpDx3.xy * 0.45, triDyZ = wpDy3.xy * 0.45;

            vec3 absN = abs(geomNormal);
            vec3 triWeights = absN * absN * absN;
            triWeights /= max(triWeights.x + triWeights.y + triWeights.z, 0.001);

            vec3 rockTexX = textureGrad(texPath, triUVx, triDxX, triDyX).rgb;
            vec3 rockTexY = textureGrad(texPath, triUVy, triDxY, triDyY).rgb;
            vec3 rockTexZ = textureGrad(texPath, triUVz, triDxZ, triDyZ).rgb;
            if (nearDualScale) {
                vec3 rockDetailX = textureGrad(texPath, triUVx * 2.8, triDxX * 2.8, triDyX * 2.8).rgb;
                vec3 rockDetailY = textureGrad(texPath, triUVy * 2.8, triDxY * 2.8, triDyY * 2.8).rgb;
                vec3 rockDetailZ = textureGrad(texPath, triUVz * 2.8, triDxZ * 2.8, triDyZ * 2.8).rgb;
                rockTexX = mix(rockTexX, rockDetailX, 0.32 * dualScaleFade);
                rockTexY = mix(rockTexY, rockDetailY, 0.32 * dualScaleFade);
                rockTexZ = mix(rockTexZ, rockDetailZ, 0.32 * dualScaleFade);
            }
            vec3 triRock = rockTexX * triWeights.x + rockTexY * triWeights.y + rockTexZ * triWeights.z;

            float wallMask = 1.0 - triWeights.y;
            // Columnar basalt and granitic orthogonal cleavage joints
            float jointWarp = sin(fragWorldPos.x * 0.18 + fragWorldPos.z * 0.15) * 1.8;
            vCragJoint = abs(fract((fragWorldPos.x * 0.28 + fragWorldPos.z * 0.24 + jointWarp) * 0.5) - 0.5) * 2.0;
            hCragJoint = abs(fract((fragWorldPos.y + jointWarp * 0.4) * 0.32) - 0.5) * 2.0;
            float cragFacet = smoothstep(0.12, 0.65, vCragJoint * hCragJoint);

            float strataWarp = sin(fragWorldPos.x * 0.14 + fragWorldPos.z * 0.11) * 1.6;
            float strataCoord = (fragWorldPos.y + strataWarp) * 0.42;
            float strataBand = sin(strataCoord * 3.14159) * 0.5 + 0.5;
            float strataLedge = smoothstep(0.35, 0.65, strataBand);
            float jointCrack = smoothstep(0.12, 0.55, triRock.r);
            cliffStrataBump = ((strataLedge - 0.5) * 0.20 + (jointCrack - 0.5) * 0.15 + (cragFacet - 0.5) * 0.25) * wallMask;

            // Authentic sunlit weathered granite corestone tones
            vec3 darkFissure  = vec3(0.16, 0.15, 0.13);
            vec3 coreGranite  = vec3(0.42, 0.39, 0.34);
            vec3 sunlitQuartz = vec3(0.56, 0.51, 0.43);
            vec3 lichenMoss   = vec3(0.18, 0.28, 0.11);

            vec3 rockBase = mix(darkFissure, coreGranite, smoothstep(0.10, 0.60, vCragJoint * hCragJoint));
            rockBase = mix(rockBase, sunlitQuartz, clamp(triRock.r * 0.58 + strataLedge * 0.35, 0.0, 1.0));

            // Velvet moss on upward-facing ledges near the plateau
            float ledgeMoss = clamp(geomNormal.y * 1.35 - 0.25, 0.0, 1.0) * smoothstep(0.30, 0.75, triRock.g);
            cliffAlbedo = mix(rockBase, lichenMoss, ledgeMoss * 0.58);
        }

        float cliffMix = smoothstep(0.04, 0.22, wCliff);
        vec3 drySoilColor = mix(warmLoamColor, cliffAlbedo, cliffMix);

        // ---- AAA Alluvial Riverbed Sand & Natural Fine River Gravel ----
        vec3 wetSoilColor = vec3(0.0);
        if (isRiparian > 0.01) {
            // 1. Alluvial quartz/feldspar river sand with hydrodynamic current ripples
            float sandRipple1 = sin(fragWorldPos.z * 9.5 + sin(fragWorldPos.x * 3.6) * 1.5 + fragWorldPos.x * 1.2) * 0.5 + 0.5;
            float sandRipple2 = sin(fragWorldPos.x * 18.0 - fragWorldPos.z * 7.5) * 0.5 + 0.5;
            float sandRipple = sandRipple1 * 0.68 + sandRipple2 * 0.32;

            // Rich warm golden alluvial sand tones
            vec3 drySand = mix(vec3(0.58, 0.50, 0.38), vec3(0.66, 0.58, 0.44), sandRipple * 0.28);
            vec3 wetSand = mix(vec3(0.44, 0.36, 0.26), vec3(0.50, 0.42, 0.31), sandRipple * 0.28);
            vec3 submergedSand = mix(vec3(0.35, 0.30, 0.23), vec3(0.42, 0.36, 0.28), sandRipple * 0.28);

            vec3 sandBase = mix(drySand, wetSand, clamp(shoreFactor * 0.70 + streamWet * 0.60, 0.0, 1.0));
            sandBase = mix(sandBase, submergedSand, clamp(lakeDepthFactor * 1.25, 0.0, 1.0));

            // Scanned photographic micro-sediment & earth grain from texPath (macro ~0.55m)
            vec3 microSediment = textureGrad(texPath, fragWorldPos.xz * 1.85, wpDx * 1.85, wpDy * 1.85).rgb;
            sandBase *= (0.78 + 0.44 * microSediment.r) * (0.82 + 0.36 * colorDirt.rgb);

            // 2. High-resolution photographic river gravel & pebble shingle (~2-5cm, scanned)
            vec2 gravelUV = tiledUV * 3.4;
            vec2 grDx = uvDx * 3.4, grDy = uvDy * 3.4;
            vec3 fineGravelTex = textureGrad(texPath, gravelUV, grDx, grDy).rgb;

            // Natural gravel shingle bars (banks where water flow deposits stones)
            float barNoise = sin(fragWorldPos.x * 0.65 + fragWorldPos.z * 0.50)
                           * cos(fragWorldPos.z * 0.85 - fragWorldPos.x * 0.40);
            float shingleBar = 0.35 + 0.65 * smoothstep(-0.20, 0.25, barNoise);

            // Pebble grains: photographic mineral flecks (quartzite, granite, slate)
            float pebbleLuma = dot(fineGravelTex, vec3(0.299, 0.587, 0.114));
            float gravelHighlight = smoothstep(0.46, 0.74, pebbleLuma);
            float pebbleCrevice = 1.0 - smoothstep(0.28, 0.46, pebbleLuma);

            vec3 pebbleMineral = mix(vec3(0.36, 0.35, 0.33), vec3(0.56, 0.52, 0.45), gravelHighlight);
            pebbleMineral = mix(pebbleMineral, vec3(0.50, 0.43, 0.34), fineGravelTex.g);

            // Contact shadow/cavity darkening around fine gravel embedded in sand
            float gravelAO = 1.0 - pebbleCrevice * 0.28 * shingleBar;
            vec3 riverbedBed = mix(sandBase, pebbleMineral, gravelHighlight * 0.65 * shingleBar) * gravelAO;

            // Export high-frequency 3D micro-normal for sand ripples and river pebbles
            if (camDistSqXZ < 400.0) {
                vec4 gravelMat = textureGrad(texSoilMaterial, gravelUV, grDx, grDy);
                vec2 gNorm = (gravelMat.rg * 2.0 - 1.0);
                proceduralPebbleNormal = gNorm * (0.35 * shingleBar + 0.20) * detailResolve;
            }

            vec3 mossyBank = vec3(0.24, 0.30, 0.15) * (0.85 + 0.35 * dirtDetail.g);
            float sandDominance = clamp(shoreFactor * 1.15 + streamWet * 1.35 + lakeDepthFactor * 1.8, 0.0, 1.0);
            wetSoilColor = mix(mossyBank, riverbedBed, sandDominance);
        } else {
            vec3 mossyBank = vec3(0.22, 0.28, 0.14) * (0.85 + 0.35 * dirtDetail.g);
            wetSoilColor = mossyBank;
        }

        vec3 trailCore = mix(vec3(0.27, 0.20, 0.13), vec3(0.49, 0.38, 0.26), pebbleRelief) * (0.86 + 0.28 * colorDirt.rgb);
        vec3 pathMarginColor = mix(warmLoamColor, trailCore, wPath);

        blendedAlbedo += drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;
    }

    float wetCapillaryDarken = smoothstep(0.15, 0.85, wWetSoil) * (1.0 - isRiparian * 0.75) * 0.20;
    blendedAlbedo *= (1.0 - wetCapillaryDarken);

    if (u_groundDebrisEnabled != 0 && camDistSqXZ < 1024.0) {
        float debrisDistFade = 1.0 - smoothstep(576.0, 1024.0, camDistSqXZ);
        vec4 debris = textureGrad(u_groundDebris, fragTexCoord, baseUvDx, baseUvDy);
        float fineGrain = (soilPossible) ? dirtDetail.r : fineGrassLuma;
        float grassDebrisAtten = 1.0 - wGrass * 0.72;

        float dryWoodlandMask = (1.0 - clamp(shoreFactor + streamWet, 0.0, 1.0)) * debrisDistFade * grassDebrisAtten;
        if (debris.r > 0.04 && dryWoodlandMask > 0.05) {
            vec3 leafLitterColor = mix(vec3(0.25, 0.18, 0.09), vec3(0.42, 0.29, 0.14), fineGrain);
            float leafMask = smoothstep(0.10, 0.58, debris.r) * (0.40 + 0.60 * fineGrain) * dryWoodlandMask;
            blendedAlbedo = mix(blendedAlbedo, leafLitterColor, leafMask * 0.36);
        }

        if (debris.g > 0.04) {
            vec3 gravelColor = mix(vec3(0.48, 0.44, 0.36), vec3(0.60, 0.56, 0.48), fineGrain);
            float gravelMask = smoothstep(0.14, 0.68, debris.g) * grassDebrisAtten * debrisDistFade;
            blendedAlbedo = mix(blendedAlbedo, gravelColor, gravelMask * 0.45);
        }

        if (debris.a > 0.04) {
            vec3 mossColor = mix(vec3(0.18, 0.30, 0.10), vec3(0.28, 0.42, 0.14), fineGrain);
            float mossMask = smoothstep(0.12, 0.60, debris.a) * debrisDistFade * grassDebrisAtten;
            blendedAlbedo = mix(blendedAlbedo, mossColor, mossMask * 0.28);
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
    if (hasSoil && camDistSqXZ < 1225.0) {
        float soilMatFade = 1.0 - smoothstep(900.0, 1225.0, camDistSqXZ);
        vec4 sampledSoilMat = textureGrad(texSoilMaterial, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85);
        soilMaterial = mix(soilMaterial, sampledSoilMat, soilMatFade);
    }

    vec3 rawMicro = mix(grassMaterial.rgb, soilMaterial.rgb, soilWeight) * 2.0 - 1.0;
    vec3 microNormal = rawMicro * inversesqrt(max(dot(rawMicro, rawMicro), 1e-8));
    if (wCliff > 0.05) {
        microNormal.y += cliffStrataBump * wCliff * 0.40;
        microNormal *= inversesqrt(max(dot(microNormal, microNormal), 1e-8));
    }
    if (abs(distantTuftRelief) > 0.001) {
        microNormal.xz += vec2(distantTuftRelief * 0.36, -distantTuftRelief * 0.30);
        microNormal *= inversesqrt(max(dot(microNormal, microNormal), 1e-8));
    }
    if (wWetSoil > 0.08 && camDistSqXZ < 400.0) {
        microNormal.xy += proceduralPebbleNormal * wWetSoil;
        microNormal *= inversesqrt(max(dot(microNormal, microNormal), 1e-8));
    }

    vec3 refAxis = (abs(geomNormal.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tRaw = cross(refAxis, geomNormal);
    vec3 tangent = tRaw * inversesqrt(max(dot(tRaw, tRaw), 1e-8));
    vec3 bitangent = cross(geomNormal, tangent);

    float puddleFlatten = 1.0 - smoothstep(0.60, 0.95, wWetSoil) * 0.45;
    float microStrength = mix(0.26, 0.52, max(soilWeight, wCliff)) * puddleFlatten * (0.45 + 0.55 * detailResolve);
    vec3 nRaw = geomNormal * (0.66 + 0.34 * microNormal.z)
              + (tangent * microNormal.x + bitangent * microNormal.y) * microStrength;
    if (wCliff > 0.05) {
        float cliffWall = 1.0 - geomNormal.y * geomNormal.y;
        vec3 cragPerturb = (tangent * (vCragJoint - 0.5) + bitangent * (hCragJoint - 0.5)) * 0.55;
        nRaw += cragPerturb * (wCliff * cliffWall);
    }
    vec3 normal = nRaw * inversesqrt(max(dot(nRaw, nRaw), 1e-8));

    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.78, (wDrySoil + wPath) * 0.80);
    roughness = mix(roughness, 0.36, wWetSoil * isRiparian * 0.75);
    roughness = clamp(roughness + (1.0 - detailResolve) * 0.25, 0.20, 0.98);

    // [TỐI ƯU VISUAL]: Nới nhẹ canopyContactAO (0.64 -> 0.76) để mặt đất giữa các bụi cỏ
    // có chiều sâu tự nhiên mà không bị bết đen.
    float nearCanopyMask = 1.0 - smoothstep(324.0, 1800.0, camDistSqXZ);
    float canopyContactAO = mix(1.0, 0.76 + 0.18 * fineGrassLuma, wGrass * nearCanopyMask);
    float cliffCreviceAO = 1.0 - max(0.0, -cliffStrataBump) * wCliff * 0.32;
    float cavityAO = mix(0.86 + 0.14 * fineGrassLuma,
                         0.72 + 0.28 * pebbleRelief, soilWeight) * reliefShadow * cliffCreviceAO * canopyContactAO;

    // ---- 11. Physically Based Lighting & Soft Grass Shadows ----
    float rawNdotL = dot(normal, light);
    float NdotL = max(rawNdotL, 0.0);
    float grassWrapNdotL = max((rawNdotL + 0.38) * (1.0 / 1.38), 0.0);
    float horizonEnvelope = smoothstep(0.02, 0.38, max(light.y, 0.0)) * 0.66 + 0.34;
    float effectiveNdotL = mix(NdotL, grassWrapNdotL * horizonEnvelope, wGrass * 0.72) * (0.85 + 0.15 * cavityAO);

    float dynShadowCoverage;
    float rawShadow = MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, normal, light, dynShadowCoverage);
    float canopyFarField = wGrass * (1.0 - dynShadowCoverage)
                         * (1.0 - smoothstep(900.0, 1764.0, camDistSqXZ))
                         * smoothstep(0.08, 0.60, ecology.r);
    rawShadow *= 1.0 - GS_FAR_CANOPY_SHADE * canopyFarField;

    // Grounded, visible shadows for flowers, grass tufts, and rocks on the meadow floor
    float turfShadowVis = rawShadow * mix(rawShadow, 1.0, 0.32);
    float resolvedShadow = mix(rawShadow, turfShadowVis, wGrass);
    float shadowFloor = mix(0.18, 0.24, wGrass);
    float shadow = mix(shadowFloor, 1.0, resolvedShadow);
    float sunVisibility = (shadow > 0.001) ? shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos) : 0.0;

    float ambientShadowMod = mix(mix(0.68, 0.64, wGrass), 1.0, resolvedShadow);
    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient) * (cavityAO * ambientShadowMod);

    vec3 totalLight = ambient + actualLight * effectiveNdotL * sunVisibility;
    vec3 groundLit = blendedAlbedo * totalLight;

    vec3 viewDir = viewVec * inversesqrt(max(dot(viewVec, viewVec), 0.0001));
    // [SPIR-V FIX]: Chuẩn hóa an toàn cho halfDir khi viewDir ngược hướng hoàn toàn với light
    vec3 halfSum = light + viewDir;
    vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
    float NdotH = max(dot(normal, halfDir), 0.0);

    if (wGrass > 0.05 && sunVisibility > 0.01) {
        float backScatter = max(dot(viewDir, -light), 0.0);
        float sssFactor = (backScatter * backScatter * backScatter) * wGrass * sunVisibility * horizonEnvelope * 0.14;
        groundLit += grassAlbedo * actualLight * vec3(0.94, 1.32, 0.62) * sssFactor;
    }

    float h2 = NdotH * NdotH;
    float h4 = h2 * h2;
    float h8 = h4 * h4;
    float h24 = h8 * h8 * h8;
    float smoothness = 1.0 - roughness;
    float drySpec = mix(h8, h24, smoothness) * smoothness * 0.022;
    groundLit += actualLight * drySpec * sunVisibility;

    // ---- 12. Capillary Wetness Mechanics (Loại bỏ pow(NdotH, 64.0) đắt đỏ) ----
    if (wWetSoil > 0.05 && lakeDepthFactor < 0.35) {
        float waterlineBand = smoothstep(0.05, 0.55, wWetSoil) * (1.0 - smoothstep(0.08, 0.35, lakeDepthFactor));
        waterlineBand = max(waterlineBand, smoothstep(0.12, 0.85, shoreFactor) * 0.90);
        float oneMinusNdotV = 1.0 - max(dot(normal, viewDir), 0.0);
        float om2 = oneMinusNdotV * oneMinusNdotV;
        float fresnelWet = 0.04 + 0.96 * (om2 * om2 * oneMinusNdotV);

        // Tái sử dụng h8 để tính NdotH^64 chỉ với 3 phép nhân thay vì gọi pow()
        float h16 = h8 * h8;
        float h32 = h16 * h16;
        float wetSpecSharp = (h32 * h32) * 0.52;
        float wetSpecBroad = h24 * 0.28;
        vec3 wetSheen = actualLight * (wetSpecSharp + wetSpecBroad * detailResolve) * (0.32 + fresnelWet * 0.68);
        vec3 skySheen = actualAmbient * (fresnelWet * 0.28) * cavityAO;
        groundLit += (wetSheen * sunVisibility + skySheen) * waterlineBand;
    }

    if (lakeDepthFactor > 0.0) {
        groundLit += vec3(0.015, 0.035, 0.028) * actualAmbient * (1.0 - lakeDepthFactor);
    }

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    if (veil > 0.001) {
        groundLit = mix(groundLit, cloudVeilColor, veil);
    }

    // Soft atmospheric cloud sea dissolution at the submerged base of the island
    if (fragWorldPos.y < -2.40) {
        float cloudImmersion = smoothstep(-2.40, -5.20, fragWorldPos.y);
        vec3 cloudBaseTone = mix(vec3(0.62, 0.68, 0.78), vec3(0.96, 0.94, 0.88), max(light.y, 0.0));
        vec3 cloudHazeLight = actualAmbient * 1.45 + actualLight * (0.35 + 0.65 * max(light.y, 0.0));
        vec3 cloudHazeTarget = cloudBaseTone * cloudHazeLight;
        groundLit = mix(groundLit, cloudHazeTarget, cloudImmersion * 0.85);
    }

    finalColor = vec4(groundLit, 1.0);
}

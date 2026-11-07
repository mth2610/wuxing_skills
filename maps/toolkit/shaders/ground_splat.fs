#version 330

// Đợt E / E2 — VFX point lights.
#include "core/shaders/common/vfx_lights.glsl"
// Dynamic PCF penumbra radius for smooth, natural shadow edges on terrain and submerged riverbed.
#define MAP_DYNAMIC_SHADOW_RADIUS 1.55

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/meadow_palette.glsl"

// ---- Look knobs ( tinh chỉnh hài hòa màu cỏ & nền đất cho song_quao ) ----
#define GS_DISTANT_CANOPY_GAIN 0.78
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
    bool nearDualScale = (camDistSqXZ < 289.0);
    float dualScaleFade = 1.0 - smoothstep(196.0, 289.0, camDistSqXZ);

    // ---- 3. Distance to Path & Continuous 16-bit Shoreline ----
    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float distToPath = 1000.0;
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    float smoothSignedShore = 1000.0;
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
            float wetCore = 1.0 - smoothstep(0.0, 0.14, smoothSignedShore);
            float dampFringe = 1.0 - smoothstep(0.02, 0.26, smoothSignedShore);
            shoreFactor = max(wetCore, dampFringe * 0.28);
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

    // ---- 5. Geometry Normal & Slope ----
    vec3 geomNormal = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // ---- 6. Soil Detail ----
    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.015
                     || streamWet > 0.80
                     || slope > 0.42
                     || (fragWorldPos.y < 0.15 && slope > 0.20)
                     || fragWorldPos.y < -0.95;
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

        // Separate interior grassy terrace embankments from the outer island cliff rim (LANDMINES.md §135)
        float rimMask = 1.0 - smoothstep(-1.10, 0.15, fragWorldPos.y);
        float wSlope = max(smoothstep(0.46, 0.64, slope), rimMask * smoothstep(0.22, 0.40, slope));
        float outerAbyssMask = (1.0 - smoothstep(-2.20, -0.45, fragWorldPos.y)) * smoothstep(0.08, 0.26, slope);
        float rockCliff = smoothstep(0.55, 0.88, rockScree)
                        * max(smoothstep(0.44, 0.60, slope), rimMask * smoothstep(0.22, 0.38, slope));
        wCliff = clamp(max(wSlope, max(rockCliff, outerAbyssMask)), 0.0, 1.0);

        // Tight riparian shingle margin (0.0m -> 0.12m) so lush meadow grass reaches right to the riverbank
        if (u_ecologyEnabled != 0 && smoothSignedShore >= -0.10) {
            riparianSandMargin = (1.0 - smoothstep(0.0, 0.12, smoothSignedShore)) * 0.18;
        }

        wDrySoil = clamp(max(max(wPathMargin * 0.78, wCliff), riparianSandMargin), 0.0, 1.0)
                       * (1.0 - wWetSoil * 0.75) * (1.0 - wPath * 0.85);
        wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

        if (u_ecologyEnabled != 0 && smoothSignedShore >= -0.15 && smoothSignedShore < 0.14) {
            float grassGrain = (dirtDetail.r - 0.5) * 0.05;
            float grassTaper = smoothstep(-0.02, 0.10, smoothSignedShore + grassGrain);
            wGrass *= grassTaper;
            wDrySoil = clamp(1.0 - wPath - wWetSoil - wGrass, 0.0, 1.0);
        }
    }

    vec4 weights = vec4(wGrass, wDrySoil, wWetSoil, wPath);
    vec4 resolved = weights / max(dot(weights, vec4(1.0)), 0.0001);
    vec2 litterClasses = vec2(0.5);
    float reliefShadow = 1.0;

    if (soilPossible && u_groundReliefEnabled != 0 && detailResolve > 0.10) {
        float combinedSoil = weights.y + weights.w;
        float dominantWeight = max(max(weights.x, combinedSoil), weights.z);
        bool boundary = dominantWeight < 0.96;
        if (boundary) {
            vec4 substrateRelief = textureGrad(u_groundRelief, tiledUV, uvDx, uvDy);
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
            resolved = mix(resolved, groupedResolved, detailResolve * 0.52);

            float heightDelta = (substrateRelief.r - soilHeight) * (resolved.x - (1.0 - resolved.x));
            reliefShadow = clamp(1.0 + heightDelta * 0.28 * detailResolve, 0.85, 1.05);
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
        vec4 colorGrass = textureGrad(texGrass, tiledUV, uvDx, uvDy);
        vec3 blendedGrass = colorGrass.rgb;
        if (nearDualScale) {
            vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
            vec3 broadGrass = textureGrad(texGrass, broadGrassUV,
                                          vec2(uvDx.y, -uvDx.x) * 0.38,
                                          vec2(uvDy.y, -uvDy.x) * 0.38).rgb;
            blendedGrass = mix(colorGrass.rgb, broadGrass, 0.28 * dualScaleFade);
        }
        if (camDistSqXZ < 1225.0) {
            float grassMatFade = 1.0 - smoothstep(900.0, 1225.0, camDistSqXZ);
            vec4 sampledGrassMat = textureGrad(texGrassMaterial, tiledUV, uvDx, uvDy);
            grassMaterial = mix(grassMaterial, sampledGrassMat, grassMatFade);
        }
        fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

        // High-frequency botanical moss/fiber micro-grain at close zoom (< 12m)
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

        float habW1 = sin(fragWorldPos.x * 0.085 + fragWorldPos.z * 0.055) * 0.5 + 0.5;
        float habW2 = sin(fragWorldPos.x * -0.065 + fragWorldPos.z * 0.12 + 1.8) * 0.5 + 0.5;
        float habW3 = sin(fragWorldPos.x * 0.18 - fragWorldPos.z * 0.14 + 0.6) * 0.5 + 0.5;
        float analyticHabitat = habW1 * 0.50 + habW2 * 0.35 + habW3 * 0.15;
        float habitatVal = (u_ecologyEnabled != 0) ? mix(analyticHabitat, ecology.a, 0.25) : analyticHabitat;

        vec3 targetCanopyColor = MeadowCanopyColor(habitatVal);
        // Deep verdant moss-clover undergrowth beneath 3D grass tufts so grazing angles never flash yellow-tan
        vec3 nearCanopySubstrate = mix(vec3(0.092, 0.156, 0.054), targetCanopyColor * 0.52, 0.52);
        grassAlbedo = mix(grassAlbedo,
                          nearCanopySubstrate * (1.0 + detailContrast * (0.58 + GS_NEAR_DETAIL_BOOST * detailResolve)),
                          0.56 - GS_NEAR_DETAIL_BOOST * 0.42 * detailResolve);

        if (u_ecologyEnabled != 0)
            grassAlbedo *= mix(vec3(1.0), vec3(0.90, 0.99, 0.92), ecology.b);

        if (camDistSqXZ > 64.0) {
            float camDist = camDistSqXZ * inversesqrt(camDistSqXZ);
            float distantCanopyBlend = smoothstep(12.0, 36.0, camDist) * wGrass;
            if (u_ecologyEnabled != 0)
                distantCanopyBlend *= mix(0.76, 1.0, smoothstep(0.04, 0.48, ecology.r)) * (1.0 - wCliff);
            if (distantCanopyBlend > 0.002) {
                vec3 canopyColor = targetCanopyColor * GS_DISTANT_CANOPY_GAIN;

                // [VISUAL UPGRADE]: Giả lập cấu trúc khóm cỏ 3D (Faux-Tuft Canopy Relief) ở cự ly > 18m
                // để mặt đất ngoài tầm vẽ cỏ 3D (38m) vẫn có độ xốp và gợn bóng như thảm cỏ thật!
                vec2 wp = fragWorldPos.xz;
                float tuftWaveA = sin(wp.x * 2.4 + sin(wp.y * 1.7) * 0.8);
                float tuftWaveB = cos(wp.y * 2.8 - sin(wp.x * 1.9) * 0.8);
                float tuftWaveC = sin(wp.x * 5.2 - wp.y * 4.6);
                float fauxTuft = ((tuftWaveA * 0.55 + tuftWaveB * 0.45) * 0.70 + tuftWaveC * 0.30) * 0.5 + 0.5;
                float tuftGrain = clamp(mix(fauxTuft, 0.5 + (turfDetail - 0.22) * 0.85, 0.55), 0.0, 1.0);

                distantTuftRelief = (tuftGrain - 0.48) * distantCanopyBlend * 0.38;
                distantTuftNormal = vec2(cos(wp.x * 2.4) * 0.32 + cos(wp.x * 5.2) * 0.14,
                                         -sin(wp.y * 2.8) * 0.32 - sin(wp.y * 4.6) * 0.14) * distantCanopyBlend;

                canopyColor *= mix(0.84, 1.14, tuftGrain);

                // Multi-scale landscape macro-variation for midground/distant hills (Ghost of Tsushima style)
                float hillRidgeNoise = sin(fragWorldPos.x * 0.085 + fragWorldPos.z * 0.062) * 0.55
                                     + cos(fragWorldPos.z * 0.092 - fragWorldPos.x * 0.048) * 0.45;
                float hillCrestMask = smoothstep(-0.18, 0.48, hillRidgeNoise);
                canopyColor = mix(canopyColor * vec3(0.92, 1.01, 0.94), canopyColor * vec3(1.08, 1.05, 0.86), hillCrestMask * 0.38);

                float macroPatch = v_macroPatchAndField.x;
                float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
                canopyColor *= mix(vec3(0.95, 1.03, 0.92), vec3(1.06, 1.04, 0.88), warmShift * 0.50);

                float windPhase = fract(u_time * 0.31830988) * 6.2831853;
                float windWave = sin(dot(fragWorldPos.xz, vec2(0.26, 0.18)) - windPhase) * 0.5 + 0.5;
                canopyColor *= mix(vec3(0.96, 0.99, 0.94), vec3(1.06, 1.05, 0.95), windWave * distantCanopyBlend);
                grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.94);
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
        float soilLuma = dot(dirtDetail, vec3(0.299, 0.587, 0.114));
        pebbleRelief = smoothstep(0.26, 0.72, soilLuma);
        vec3 loamDark  = vec3(0.19, 0.148, 0.095);
        vec3 loamLight = vec3(0.38, 0.298, 0.200);
        vec3 warmLoamColor = mix(loamDark, loamLight, pebbleRelief) * (0.86 + 0.28 * dirtDetail) * (1.0 - porousDarkening * 0.18);

        vec3 cliffAlbedo = warmLoamColor;
        if (wCliff > 0.01) {
            vec2 triUVx = fragWorldPos.zy * 0.20;
            vec2 triUVy = fragWorldPos.xz * 0.20;
            vec2 triUVz = fragWorldPos.xy * 0.20;
            vec2 triDxX = wpDx3.zy * 0.20, triDyX = wpDy3.zy * 0.20;
            vec2 triDxY = wpDx * 0.20, triDyY = wpDy * 0.20;
            vec2 triDxZ = wpDx3.xy * 0.20, triDyZ = wpDy3.xy * 0.20;

            vec3 absN = abs(geomNormal);
            vec3 triWeights = absN * absN * absN * absN;
            triWeights /= max(triWeights.x + triWeights.y + triWeights.z, 0.0001);

            vec3 rockTexX, rockTexY, rockTexZ;
            vec3 rockNormX = vec3(0.0, 0.0, 1.0);
            vec3 rockNormY = vec3(0.0, 0.0, 1.0);
            vec3 rockNormZ = vec3(0.0, 0.0, 1.0);

            if (u_groundRockEnabled != 0) {
                rockTexX = (triWeights.x > 0.02) ? textureGrad(u_groundRock, triUVx, triDxX, triDyX).rgb : vec3(0.0);
                rockTexY = (triWeights.y > 0.02) ? textureGrad(u_groundRock, triUVy, triDxY, triDyY).rgb : vec3(0.0);
                rockTexZ = (triWeights.z > 0.02) ? textureGrad(u_groundRock, triUVz, triDxZ, triDyZ).rgb : vec3(0.0);

                rockNormX = (triWeights.x > 0.02) ? (textureGrad(u_groundRockNormal, triUVx, triDxX, triDyX).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
                rockNormY = (triWeights.y > 0.02) ? (textureGrad(u_groundRockNormal, triUVy, triDxY, triDyY).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
                rockNormZ = (triWeights.z > 0.02) ? (textureGrad(u_groundRockNormal, triUVz, triDxZ, triDyZ).rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);

                if (nearDualScale) {
                    vec2 dUVx = triUVx * 3.2;
                    vec2 dUVy = triUVy * 3.2;
                    vec2 dUVz = triUVz * 3.2;
                    vec2 dDxX = triDxX * 3.2, dDyX = triDyX * 3.2;
                    vec2 dDxY = triDxY * 3.2, dDyY = triDyY * 3.2;
                    vec2 dDxZ = triDxZ * 3.2, dDyZ = triDyZ * 3.2;

                    if (triWeights.x > 0.02) {
                        vec3 detailX = textureGrad(u_groundRock, dUVx, dDxX, dDyX).rgb;
                        rockTexX = mix(rockTexX, detailX * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormX = textureGrad(u_groundRockNormal, dUVx, dDxX, dDyX).rgb * 2.0 - 1.0;
                        rockNormX = mix(rockNormX, detailNormX, 0.42 * dualScaleFade);
                    }
                    if (triWeights.y > 0.02) {
                        vec3 detailY = textureGrad(u_groundRock, dUVy, dDxY, dDyY).rgb;
                        rockTexY = mix(rockTexY, detailY * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormY = textureGrad(u_groundRockNormal, dUVy, dDxY, dDyY).rgb * 2.0 - 1.0;
                        rockNormY = mix(rockNormY, detailNormY, 0.42 * dualScaleFade);
                    }
                    if (triWeights.z > 0.02) {
                        vec3 detailZ = textureGrad(u_groundRock, dUVz, dDxZ, dDyZ).rgb;
                        rockTexZ = mix(rockTexZ, detailZ * 1.08, 0.35 * dualScaleFade);
                        vec3 detailNormZ = textureGrad(u_groundRockNormal, dUVz, dDxZ, dDyZ).rgb * 2.0 - 1.0;
                        rockNormZ = mix(rockNormZ, detailNormZ, 0.42 * dualScaleFade);
                    }
                }
            } else {
                rockTexX = (triWeights.x > 0.02) ? textureGrad(texPath, triUVx, triDxX, triDyX).rgb : vec3(0.0);
                rockTexY = (triWeights.y > 0.02) ? textureGrad(texPath, triUVy, triDxY, triDyY).rgb : vec3(0.0);
                rockTexZ = (triWeights.z > 0.02) ? textureGrad(texPath, triUVz, triDxZ, triDyZ).rgb : vec3(0.0);
                if (nearDualScale) {
                    if (triWeights.x > 0.02) {
                        vec3 detailX = textureGrad(texPath, triUVx * 2.8, triDxX * 2.8, triDyX * 2.8).rgb;
                        rockTexX = mix(rockTexX, detailX, 0.32 * dualScaleFade);
                    }
                    if (triWeights.y > 0.02) {
                        vec3 detailY = textureGrad(texPath, triUVy * 2.8, triDxY * 2.8, triDyY * 2.8).rgb;
                        rockTexY = mix(rockTexY, detailY, 0.32 * dualScaleFade);
                    }
                    if (triWeights.z > 0.02) {
                        vec3 detailZ = textureGrad(texPath, triUVz * 2.8, triDxZ * 2.8, triDyZ * 2.8).rgb;
                        rockTexZ = mix(rockTexZ, detailZ, 0.32 * dualScaleFade);
                    }
                }
            }
            vec3 triRock = rockTexX * triWeights.x + rockTexY * triWeights.y + rockTexZ * triWeights.z;

            vec3 boulderTint = vec3(0.585, 0.560, 0.525);
            float macroTone = sin(fragWorldPos.x * 0.035 + fragWorldPos.z * 0.026) * 0.5 + 0.5;
            vec3 stoneTint = mix(boulderTint * 0.94, boulderTint * 1.06, macroTone);
            vec3 rockBase = triRock * stoneTint * 1.38;

            // Velvet moss on upward-facing rock ledges near the plateau
            float ledgeFacing = clamp(geomNormal.y * 1.45 - 0.20, 0.0, 1.0);
            vec3 ledgeMoss = mix(vec3(0.14, 0.23, 0.09), vec3(0.24, 0.36, 0.12), triRock.g);
            cliffAlbedo = mix(rockBase, ledgeMoss, ledgeFacing * 0.68);

            rockNormX.xy *= 1.45;
            rockNormY.xy *= 1.45;
            rockNormZ.xy *= 1.45;
            vec3 sgnN = sign(geomNormal);
            vec3 worldNx = vec3(rockNormX.z * sgnN.x, rockNormX.y, rockNormX.x * sgnN.x);
            vec3 worldNy = vec3(rockNormY.x, rockNormY.z * sgnN.y, rockNormY.y);
            vec3 worldNz = vec3(rockNormZ.x * sgnN.z, rockNormZ.y, rockNormZ.z * sgnN.z);
            vec3 rawCliffN = worldNx * triWeights.x + worldNy * triWeights.y + worldNz * triWeights.z;
            cliffTriNormal = rawCliffN * inversesqrt(max(dot(rawCliffN, rawCliffN), 1e-8));

            float normCrevice = clamp(rockNormX.z * triWeights.x + rockNormY.z * triWeights.y + rockNormZ.z * triWeights.z, 0.35, 1.0);
            cliffOcclusion = mix(0.72, 1.0, normCrevice);
            cliffRoughnessMod = (1.0 - normCrevice) * 0.12;
        }

        float cliffMix = smoothstep(0.02, 0.20, wCliff);
        vec3 drySoilColor = mix(warmLoamColor, cliffAlbedo, cliffMix);

        // ---- AAA Alluvial Riverbed Sand & Natural Fine River Gravel ----
        vec3 wetSoilColor = vec3(0.0);
        if (isRiparian > 0.01) {
            // [VISUAL FIX]: Xóa sọc sóng cát tuần hoàn bằng nhiễu uốn miền (domain warp) nhẹ
            float warpSand = sin(fragWorldPos.x * 0.75 + fragWorldPos.z * 0.55) * 2.2
                           + cos(fragWorldPos.z * 1.15 - fragWorldPos.x * 0.85) * 1.4;
            float sandPhase1 = (fragWorldPos.z + fragWorldPos.x * 0.65) * 4.2 + warpSand;
            float sandPhase2 = (fragWorldPos.x - fragWorldPos.z * 0.55) * 6.8 - warpSand * 0.8;
            float sinP1 = sin(sandPhase1);
            float sinP2 = sin(sandPhase2);
            float sandRipple = (sinP1 * 0.60 + sinP2 * 0.40) * 0.5 + 0.5;

            vec3 drySand = mix(vec3(0.35, 0.30, 0.22), vec3(0.40, 0.35, 0.26), sandRipple * 0.12);
            vec3 wetSand = mix(vec3(0.26, 0.23, 0.17), vec3(0.32, 0.28, 0.21), sandRipple * 0.12);
            vec3 submergedSand = mix(vec3(0.46, 0.42, 0.31), vec3(0.55, 0.50, 0.37), sandRipple * 0.15);

            float wetLevel = clamp(shoreFactor * 0.85 + streamWet * 0.50, 0.0, 1.0);
            vec3 sandBase = mix(drySand, wetSand, wetLevel);
            sandBase = mix(sandBase, submergedSand, clamp(lakeDepthFactor * 1.35, 0.0, 1.0));

            vec3 microSediment = textureGrad(texPath, fragWorldPos.xz * 1.85, wpDx * 1.85, wpDy * 1.85).rgb;
            sandBase *= (0.80 + 0.40 * microSediment.r) * (0.84 + 0.32 * colorDirt.rgb);

            float beachDryMix = clamp(shoreFactor * 0.80 + riparianSandMargin * 1.35, 0.0, 1.0);
            drySoilColor = mix(drySoilColor, drySand * (0.84 + 0.32 * colorDirt.rgb), beachDryMix);

            vec2 gravelUV = tiledUV * 3.4;
            vec2 grDx = uvDx * 3.4, grDy = uvDy * 3.4;
            vec3 fineGravelTex = textureGrad(texPath, gravelUV, grDx, grDy).rgb;

            float barNoise = sin(fragWorldPos.x * 0.65 + fragWorldPos.z * 0.50)
                           * cos(fragWorldPos.z * 0.85 - fragWorldPos.x * 0.40);
            float shingleBar = 0.35 + 0.65 * smoothstep(-0.20, 0.25, barNoise);

            float pebbleLuma = dot(fineGravelTex, vec3(0.299, 0.587, 0.114));
            float gravelHighlight = smoothstep(0.38, 0.70, pebbleLuma);
            float pebbleCrevice = 1.0 - smoothstep(0.24, 0.48, pebbleLuma);

            vec3 pebbleMineral = mix(vec3(0.38, 0.36, 0.34), vec3(0.66, 0.61, 0.52), gravelHighlight);
            pebbleMineral = mix(pebbleMineral, vec3(0.52, 0.45, 0.36), fineGravelTex.g);

            float gravelAO = 1.0 - pebbleCrevice * 0.28 * shingleBar;
            vec3 riverbedBed = mix(sandBase, pebbleMineral, gravelHighlight * 0.74 * shingleBar) * gravelAO;

            if (camDistSqXZ < 900.0) {
                float cosP1 = cos(sandPhase1);
                float cosP2 = cos(sandPhase2);
                vec2 rippleNorm = clamp(vec2(cosP1 + cosP2 * 0.6, cosP1 - cosP2 * 0.5) * 0.048, vec2(-0.22), vec2(0.22));

                vec4 gravelMat = textureGrad(texSoilMaterial, gravelUV, grDx, grDy);
                vec2 gNorm = (gravelMat.rg * 2.0 - 1.0);
                proceduralPebbleNormal = (gNorm * (0.44 * shingleBar + 0.20) + rippleNorm * (1.0 - shingleBar * 0.45)) * detailResolve;
            }

            float sandDominance = clamp(shoreFactor * 1.25 + streamWet * 1.35 + lakeDepthFactor * 2.0, 0.0, 1.0);
            vec3 bankFringeHumus = mix(vec3(0.32, 0.29, 0.22), vec3(0.45, 0.40, 0.30), dirtDetail.g) * (0.86 + 0.30 * colorDirt.rgb);
            wetSoilColor = mix(bankFringeHumus, riverbedBed, smoothstep(0.02, 0.28, sandDominance));
        } else {
            vec3 mossyBank = vec3(0.28, 0.26, 0.20) * (0.85 + 0.35 * dirtDetail.g);
            wetSoilColor = mossyBank;
        }

        vec3 trailCore = mix(vec3(0.27, 0.20, 0.13), vec3(0.49, 0.38, 0.26), pebbleRelief) * (0.86 + 0.28 * colorDirt.rgb);
        vec3 pathMarginColor = mix(warmLoamColor, trailCore, wPath);

        blendedAlbedo += drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;
    }

    float riparianSaturation = clamp(shoreFactor * 0.95 + streamWet * 0.70 + lakeDepthFactor * 1.2, 0.0, 1.0);
    // Physically based capillary darkening: porous wet sand and bank mud darken naturally
    float wetCapillaryDarken = smoothstep(0.08, 0.78, riparianSaturation) * 0.34;
    blendedAlbedo *= (1.0 - wetCapillaryDarken);

    // Subtle submerged bed depth tint: crystal-clear turquoise mountain water revealing golden riverbed pebbles
    if (lakeDepthFactor > 0.0) {
        float subDepth = clamp(lakeDepthFactor * 0.35, 0.0, 0.45);
        blendedAlbedo *= exp(-vec3(0.28, 0.09, 0.03) * subDepth);
        vec3 bedWaterTint = mix(vec3(0.20, 0.58, 0.52), vec3(0.08, 0.32, 0.36), smoothstep(0.18, 1.15, lakeDepthFactor));
        blendedAlbedo = mix(blendedAlbedo, bedWaterTint * 0.74, smoothstep(0.18, 0.92, lakeDepthFactor) * 0.28);
    }

    float rockContactAO = 1.0;
    if (u_groundDebrisEnabled != 0 && camDistSqXZ < 1024.0) {
        float debrisDistFade = 1.0 - smoothstep(576.0, 1024.0, camDistSqXZ);
        vec4 debris = textureLod(u_groundDebris, fragTexCoord, 0.0);
        float fineGrain = (soilPossible) ? dirtDetail.r : fineGrassLuma;
        float grassDebrisAtten = 1.0 - wGrass * 0.72;

        if (debris.b > 0.02) {
            rockContactAO = 1.0 - smoothstep(0.04, 0.85, debris.b) * 0.68 * debrisDistFade;
        }

        float dryWoodlandMask = (1.0 - clamp(shoreFactor + streamWet, 0.0, 1.0)) * debrisDistFade * grassDebrisAtten;
        if (debris.r > 0.04 && dryWoodlandMask > 0.05) {
            vec3 leafLitterColor = mix(vec3(0.25, 0.18, 0.09), vec3(0.42, 0.29, 0.14), fineGrain);
            float leafMask = smoothstep(0.10, 0.58, debris.r) * (0.40 + 0.60 * fineGrain) * dryWoodlandMask;
            blendedAlbedo = mix(blendedAlbedo, leafLitterColor, leafMask * 0.36);
        }

        // Gate scree gravel strictly by steep slope on interior hills while preserving outer cliff rim detail
        float gravelSlopeMin = mix(0.42, 0.24, 1.0 - smoothstep(-1.10, 0.15, fragWorldPos.y));
        if (debris.g > 0.04 && slope > gravelSlopeMin) {
            float slopeGate = smoothstep(gravelSlopeMin, gravelSlopeMin + 0.16, slope);
            vec3 gravelColor = mix(vec3(0.30, 0.28, 0.24), vec3(0.40, 0.38, 0.32), fineGrain);
            float gravelMask = smoothstep(0.18, 0.70, debris.g) * slopeGate * grassDebrisAtten * debrisDistFade;
            blendedAlbedo = mix(blendedAlbedo, gravelColor, gravelMask * 0.40);
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
            nRaw += (tangent * proceduralPebbleNormal.x + bitangent * proceduralPebbleNormal.y) * (isRiparian * 0.58 * puddleFlatten);
        }
        if (wCliff > 0.01) {
            float cliffBlend = smoothstep(0.04, 0.40, wCliff);
            nRaw = mix(nRaw, cliffTriNormal, cliffBlend * (0.65 + 0.35 * detailResolve));
        }
        normal = nRaw * inversesqrt(max(dot(nRaw, nRaw), 1e-8));
    }

    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.78, (wDrySoil + wPath) * 0.80);
    roughness = mix(roughness, clamp(0.72 + cliffRoughnessMod, 0.45, 0.92), wCliff * 0.85);
    float riparianRoughness = mix(0.78, 0.24, smoothstep(0.10, 0.85, max(shoreFactor, wWetSoil)));
    roughness = mix(roughness, riparianRoughness, max(isRiparian, wWetSoil) * 0.85);
    roughness = clamp(roughness, 0.22, 0.98);

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
    float softShadowVis = rawShadow * mix(rawShadow, 1.0, 0.58);
    float resolvedShadow = mix(rawShadow, softShadowVis, max(wGrass, isRiparian * 0.85));
    float shadowFloor = mix(0.60, 0.76, max(wGrass, isRiparian * 0.72));
    float shadow = mix(shadowFloor, 1.0, resolvedShadow);
    float sunVisibility = (shadow > 0.001) ? shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos) * rockContactAO : 0.0;

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

    if (wCliff > 0.04 && fragWorldPos.y < -1.0) {
        float lowerSkirt = 1.0 - smoothstep(-5.20, -1.00, fragWorldPos.y);
        float cloudBounceFactor = max(-normal.y * 0.65 + 0.25, 0.0) * lowerSkirt;
        vec3 cloudBounce = vec3(0.92, 0.90, 0.86) * actualAmbient * cloudBounceFactor * 0.95;
        groundLit += blendedAlbedo * cloudBounce;
    }

    if (veil > 0.001) {
        groundLit = mix(groundLit, cloudVeilColor, veil);
    }

    // Gentle golden-hour aerial perspective over distant hills (35m..145m)
    if (camDistSqXZ > 1225.0) {
        float aerialFactor = smoothstep(1225.0, 21025.0, camDistSqXZ) * (1.0 - smoothstep(2.5, 11.5, fragWorldPos.y) * 0.35);
        vec3 horizonAerial = actualAmbient * vec3(0.88, 0.84, 0.78) + actualLight * vec3(0.28, 0.22, 0.16);
        groundLit = mix(groundLit, horizonAerial, aerialFactor * 0.28);
    }

    if (fragWorldPos.y < -1.80) {
        float cloudImmersion = 1.0 - smoothstep(-4.80, -1.80, fragWorldPos.y);
        vec3 cloudBaseTone = mix(vec3(0.56, 0.62, 0.72), vec3(0.95, 0.92, 0.86), 0.55);
        vec3 cloudHazeLight = actualAmbient * 0.85 + actualLight * (0.42 * max(light.y + 0.35, 0.25));
        vec3 cloudHazeTarget = cloudBaseTone * cloudHazeLight;
        groundLit = mix(groundLit, cloudHazeTarget, cloudImmersion * 0.92);
    }

    finalColor = vec4(groundLit, 1.0);
}
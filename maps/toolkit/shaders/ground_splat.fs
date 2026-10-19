#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is.
#include "core/shaders/common/vfx_lights.glsl"
// Preserve thin grass silhouettes without adding shadow-map samples.
#define MAP_DYNAMIC_SHADOW_RADIUS 1.35

#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/meadow_palette.glsl"

uniform sampler2D u_cloudNoise;
uniform sampler2D u_groundRelief; // R/G derived substrate/soil height; B/A litter/moss classes
uniform int u_groundReliefEnabled;
uniform sampler2D u_groundDebris; // R: leaf litter, G: scree gravel, B: twigs, A: moss
uniform int u_groundDebrisEnabled;

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface space; see ground_splat.vs
in vec3 fragNormal;
in vec3 fragWorldPos;   // TRUE world space (x, y, z)

uniform vec4 colDiffuse;
uniform sampler2D texture0;         // Splatmap (if provided)
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
uniform vec4 u_pathSegs[MAX_PATH_SEGS]; // xy = p0.xz, zw = p1.xz
uniform int u_pathSegCount;
uniform vec4 u_lakeParams; // xy = center.xz, zw = radii.xz

out vec4 finalColor;

#define LAYER_EPS 0.003
#define PATH_INFLUENCE 4.0

void main()
{
    // ---- 0. Derivatives (Uniform Control Flow) ----
    // Uses squared length to eliminate two sqrt() calls per pixel.
    vec2 tiledUV = fragTexCoord * tiling;
    vec2 uvDx = dFdx(tiledUV), uvDy = dFdy(tiledUV);
    vec2 wpDx = dFdx(fragWorldPos.xz), wpDy = dFdy(fragWorldPos.xz);
    float pixelMetersSq = max(dot(wpDx, wpDx), dot(wpDy, wpDy));
    // Equivalent to 1.0 - smoothstep(0.015, 0.070, sqrt(pixelMetersSq))
    float detailResolve = 1.0 - smoothstep(0.000225, 0.0049, pixelMetersSq);

    // ---- 1. Splat Fetch & Early Abyss Discard ----
    vec4 splatSample = texture(texture0, fragTexCoord);
    float streamWet = splatSample.a; // Channel A: Streams and lake wetland
    float rockScree = splatSample.g; // Channel G: Exposed cliff rocks and scree

    if (rockScree > 0.85 && splatSample.r < 0.10 && fragWorldPos.y <= -7.2) {
        discard;
    }

    // Pre-validate light & ambient once (used by both cloud veil and surface shading)
    float lightLenSq = dot(lightDir, lightDir);
    vec3 light = (lightLenSq > 0.01) ? (-lightDir * inversesqrt(lightLenSq)) : vec3(0.0, 1.0, 0.0);
    vec3 actualAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actualLight   = (lightColor.a == 0.0)   ? vec3(1.0) : lightColor.rgb;
    // Zenith irradiance compensation: prevents 300% over-exposure at 12:00 Noon when maps use dawn SunIntensity
    actualLight *= 1.0 / (1.0 + max(light.y - 0.36, 0.0) * 0.88);

    // ---- 2. Cloud Veil Pre-Evaluation & Early Out ----
    // If the underside cliff is 100% occluded by the cloud veil, return immediately
    // and skip all expensive terrain textures, shadow maps, and PBR math.
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

    // Camera distance squared (reused for dual-scale LOD and distant canopy blend)
    vec3 viewVec = viewPos - fragWorldPos;
    float camDistSqXZ = dot(viewVec.xz, viewVec.xz);
    bool nearDualScale = (camDistSqXZ < 324.0); // Within 18m: enable secondary rotated texture fetch

    // ---- 3. Distance to Path (Sqrt-free loop) ----
    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float distToPath = 1000.0;
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    if (u_ecologyEnabled != 0) {
        vec2 ecoUV = (fragWorldPos.xz - u_ecologyRect.xy) / u_ecologyRect.zw;
        ecology = texture(u_ecology, ecoUV);
        vec4 metric = texture(u_ecologyDistance, ecoUV);
        float roadU16 = (metric.r * 65280.0 + metric.g * 255.0) / 65535.0;
        distToPath = (roadU16 * 2.0 - 1.0) * 16.0 + 1.15;

        // Metric signed shore distance: < 0 is submerged, > 0 is dry terrain
        float shoreU16 = (metric.b * 65280.0 + metric.a * 255.0) / 65535.0;
        float signedShore = (shoreU16 * 2.0 - 1.0) * 16.0;
        if (signedShore < 0.0) {
            // Submerged ground / riverbed
            lakeDepthFactor = clamp(-signedShore / 3.0, 0.0, 1.0);
            shoreFactor = 1.0;
        } else {
            // Shore dampness extends 2.6m from water edge for a cleaner bank transition
            shoreFactor = max(ecology.b, 1.0 - smoothstep(0.0, 2.6, signedShore));
        }
    } else if (u_pathSegCount > 0) {
        float minDistSq = 1000000.0;
        int count = min(u_pathSegCount, MAX_PATH_SEGS);
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
    if (u_lakeParams.z > 0.0) {
        vec2 lakeDelta = (fragWorldPos.xz - u_lakeParams.xy) / u_lakeParams.zw;
        float lakeDistSq = dot(lakeDelta, lakeDelta);
        if (lakeDistSq < 1.5625) { // 1.25^2
            float lakeDist = sqrt(lakeDistSq);
            float distWet = 1.0 - smoothstep(0.96, 1.25, lakeDist);
            shoreFactor = max(shoreFactor, distWet);
            if (lakeDist < 1.0) {
                lakeDepthFactor = max(lakeDepthFactor, 1.0 - lakeDist * mix(1.0, lakeDist, 0.6));
            }
        }
    }

    // ---- 5. Geometry Normal & Slope ----
    vec3 geomNormal = normalize(fragNormal);
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // ---- 6. Soil Detail (with Crisp Micro-Grain Preservation) ----
    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.002
                     || streamWet > 0.40
                     || slope > 0.18
                     || rockScree > 0.25;
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
            dirtDetail = clamp(colorDirt.rgb * mix(vec3(1.0), broadMod, 0.45), vec3(0.0), vec3(1.0));
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
    float wWetSoil = max(shoreFactor * 0.82, smoothstep(0.68, 0.96, streamWet) * 0.82);

    float wSlope = smoothstep(0.36, 0.70, slope);
    float outerAbyssMask = 1.0 - smoothstep(-2.40, -1.10, fragWorldPos.y);
    float steepOrAbyssMask = max(smoothstep(0.22, 0.48, slope), outerAbyssMask);
    float rockCliff = smoothstep(0.76, 0.96, rockScree) * steepOrAbyssMask;
    float wCliff = clamp(max(wSlope, rockCliff), 0.0, 1.0);

    float wDrySoil = clamp(max(wPathMargin * 0.78, wCliff * 0.92), 0.0, 1.0)
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
            litterClasses = substrateRelief.ba;
            if (boundary) {
                float soilHeight = textureGrad(u_groundRelief, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85).g;
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
                reliefShadow = clamp(1.0 + heightDelta * 0.28 * detailResolve, 0.82, 1.06);
            }
        }
    }
    wGrass = resolved.x; wDrySoil = resolved.y;
    wWetSoil = resolved.z; wPath = resolved.w;

    float soilWeight = clamp(wDrySoil + wWetSoil + wPath, 0.0, 1.0);
    bool hasGrass = wGrass > LAYER_EPS;
    bool hasSoil  = soilWeight > LAYER_EPS;

    // ---- 8. Grass Layer (Rich Earthy Olive-Moss Humus & Tsushima Canopy Harmony) ----
    vec3 grassAlbedo = vec3(0.0);
    float fineGrassLuma = 0.5;
    vec4 grassMaterial = vec4(0.5, 0.5, 1.0, 0.8);
    float distantTuftRelief = 0.0;

    if (hasGrass) {
        vec4 colorGrass = textureGrad(texGrass, tiledUV, uvDx, uvDy);
        vec3 blendedGrass = colorGrass.rgb;
        if (nearDualScale) {
            vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
            vec3 broadGrass = textureGrad(texGrass, broadGrassUV,
                                          vec2(uvDx.y, -uvDx.x) * 0.38,
                                          vec2(uvDy.y, -uvDy.x) * 0.38).rgb;
            blendedGrass = mix(colorGrass.rgb, broadGrass, 0.30);
        }
        grassMaterial = textureGrad(texGrassMaterial, tiledUV, uvDx, uvDy);
        fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

        float turfDetail = dot(blendedGrass, vec3(0.2126, 0.7152, 0.0722));
        // Stronger micro-contrast so close-up ground between grass tufts reads as rich humus & moss, not flat paint
        float detailContrast = clamp((turfDetail - 0.21) * 1.95, -0.32, 0.36);

        // Deep earthy olive-moss forest floor understorey (darker and warmer than 3D grass tips)
        vec3 turfBase = vec3(0.142, 0.205, 0.074);
        float tintLuma = dot(colDiffuse.rgb, vec3(0.299, 0.587, 0.114));
        if (tintLuma > 0.05) {
            vec3 normalizedTint = clamp(colDiffuse.rgb / tintLuma, vec3(0.75), vec3(1.25));
            turfBase *= mix(vec3(1.0), normalizedTint, 0.34);
        }

        // Blend warm woodland soil/humus into low-luma pockets of the grass texture at close range
        vec3 humusPocketTone = vec3(0.165, 0.140, 0.082);
        float humusPocketMask = (1.0 - smoothstep(0.14, 0.28, turfDetail)) * detailResolve * 0.45;
        turfBase = mix(turfBase, humusPocketTone, humusPocketMask);

        vec3 litterChroma = clamp(blendedGrass / max(turfDetail, 0.06), vec3(0.68), vec3(1.42));
        float chromaStrength = mix(0.36, 0.62, detailResolve);
        grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, chromaStrength) * (1.0 + detailContrast);

        vec3 litterTint = mix(vec3(1.0), vec3(1.08, 1.02, 0.90), litterClasses.x);
        litterTint *= mix(vec3(1.0), vec3(0.92, 1.04, 0.92), litterClasses.y);
        grassAlbedo *= mix(vec3(1.0), litterTint, detailResolve * 0.36);

        float habitatVal;
        if (u_ecologyEnabled != 0) {
            habitatVal = ecology.a;
        } else {
            float turfNoise = sin(fragWorldPos.x * 0.15 + fragWorldPos.z * 0.10) * 0.5
                            + sin(fragWorldPos.x * -0.11 + fragWorldPos.z * 0.20 + 1.8) * 0.5;
            habitatVal = clamp(turfNoise * 0.5 + 0.5, 0.0, 1.0);
        }

        // Macro Canopy Palette Harmony across ALL distances (Ghost of Tsushima style):
        // Near ground inherits a grounded version of the 3-band MeadowCanopyColor so the
        // substrate naturally shifts from deep moss in swales to warm olive-gold on sunlit hills.
        vec3 targetCanopyColor = MeadowCanopyColor(habitatVal);
        vec3 nearCanopySubstrate = targetCanopyColor * 0.52;
        grassAlbedo = mix(grassAlbedo, nearCanopySubstrate * (1.0 + detailContrast * 0.75), 0.38);

        if (u_ecologyEnabled != 0)
            grassAlbedo *= mix(vec3(1.0), vec3(0.88, 0.98, 0.90), ecology.b);

        // Seamless Distant Canopy Imposter Blend: matches 3D tuft luminance, golden warmth & wind waves
        if (camDistSqXZ > 121.0) {
            float camDist = sqrt(camDistSqXZ);
            float distantCanopyBlend = smoothstep(11.0, 42.0, camDist) * wGrass;
            if (u_ecologyEnabled != 0)
                distantCanopyBlend *= smoothstep(0.08, 0.60, ecology.r);
            if (distantCanopyBlend > 0.002) {
                vec3 canopyColor = targetCanopyColor * 0.64;
                float clumpNoise1 = sin(fragWorldPos.x * 1.85 + fragWorldPos.z * 1.45)
                                  * cos(fragWorldPos.x * -1.35 + fragWorldPos.z * 2.15);
                float clumpNoise2 = sin(fragWorldPos.x * 4.20 - fragWorldPos.z * 3.80) * 0.5;
                float tuftGrain = clamp(0.5 + 0.35 * clumpNoise1 + 0.20 * clumpNoise2 + (turfDetail - 0.22) * 0.85, 0.0, 1.0);
                distantTuftRelief = (tuftGrain - 0.5) * distantCanopyBlend;
                canopyColor *= mix(0.78, 1.22, tuftGrain);

                // Passing wind waves on distant terrain
                float windWave = sin(dot(fragWorldPos.xz, vec2(0.28, 0.20)) - u_time * 2.2) * 0.5 + 0.5;
                canopyColor *= mix(vec3(0.95, 0.97, 0.94), vec3(1.08, 1.06, 0.96), windWave * distantCanopyBlend);
                grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.86);
            }
        }
    }

    // ---- 9. Full 3-Axis Triplanar Cliff Strata, Woodland Loam & Alluvial Shingle Albedo ----
    float porousDarkening = smoothstep(0.0, 0.45, wWetSoil) * (1.0 - wWetSoil * 0.35);
    float soilLuma = dot(dirtDetail, vec3(0.299, 0.587, 0.114));
    float pebbleRelief = smoothstep(0.26, 0.72, soilLuma);
    vec3 loamDark  = vec3(0.19, 0.148, 0.095);
    vec3 loamLight = vec3(0.38, 0.298, 0.200);
    vec3 warmLoamColor = mix(loamDark, loamLight, pebbleRelief) * (0.86 + 0.28 * dirtDetail) * (1.0 - porousDarkening * 0.18);

    vec3 cliffAlbedo = warmLoamColor;
    float cliffStrataBump = 0.0;
    if (wCliff > 0.10 && (slope > 0.28 || outerAbyssMask > 0.05)) {
        vec2 triUVx = fragWorldPos.zy * 0.28;
        vec2 triUVz = fragWorldPos.xy * 0.28;
        vec3 triWeights = abs(geomNormal);
        triWeights /= max(triWeights.x + triWeights.y + triWeights.z, 0.001);

        vec3 rockTexX = texture(texPath, triUVx).rgb;
        vec3 rockTexZ = texture(texPath, triUVz).rgb;
        vec3 triRock  = rockTexX * triWeights.x + dirtDetail * triWeights.y + rockTexZ * triWeights.z;

        float wallMask = 1.0 - triWeights.y;
        float strataWarp = sin(fragWorldPos.x * 0.14 + fragWorldPos.z * 0.11) * 1.6;
        float strataCoord = (fragWorldPos.y + strataWarp) * 0.42;
        float strataBand = sin(strataCoord * 3.14159) * 0.5 + 0.5;
        float strataLedge = smoothstep(0.35, 0.65, strataBand);
        float jointCrack = smoothstep(0.12, 0.55, triRock.r);
        cliffStrataBump = ((strataLedge - 0.5) * 0.20 + (jointCrack - 0.5) * 0.15) * wallMask;

        vec3 warmSlate   = vec3(0.24, 0.25, 0.22);
        vec3 warmGranite = vec3(0.40, 0.38, 0.32);
        vec3 lichenMoss  = vec3(0.20, 0.28, 0.14);
        cliffAlbedo = mix(warmSlate, warmGranite, clamp(triRock.r * 0.72 + strataLedge * 0.28, 0.0, 1.0));
        float ledgeMoss = clamp(geomNormal.y * 1.15 - 0.30, 0.0, 1.0) * smoothstep(0.38, 0.80, triRock.g);
        cliffAlbedo = mix(cliffAlbedo, lichenMoss, ledgeMoss * 0.55);
    }

    float cliffMix = clamp(wCliff / max(wDrySoil, 0.001), 0.0, 1.0);
    vec3 drySoilColor = mix(warmLoamColor, cliffAlbedo, cliffMix);

    // Natural alluvial river shingle & damp mossy silt (warmer earth-pebble bank transition)
    float pebbleGrain = smoothstep(0.22, 0.78, dirtDetail.r);
    vec3 dampShingle = mix(vec3(0.19, 0.19, 0.15), vec3(0.31, 0.29, 0.22), pebbleGrain);
    vec3 mossyBank   = vec3(0.15, 0.22, 0.10) * (0.80 + 0.40 * dirtDetail.g);
    vec3 wetSoilColor = mix(mossyBank, dampShingle, clamp(shoreFactor * 0.68 + streamWet * 0.32, 0.0, 1.0));
    if (lakeDepthFactor > 0.0) {
        vec3 submergedBed = mix(vec3(0.15, 0.19, 0.15), vec3(0.09, 0.14, 0.13), lakeDepthFactor);
        wetSoilColor = mix(wetSoilColor, submergedBed * (0.84 + 0.32 * pebbleGrain), clamp(lakeDepthFactor * 1.15, 0.0, 1.0));
    }

    vec3 trailCore = mix(vec3(0.27, 0.20, 0.13), vec3(0.49, 0.38, 0.26), pebbleRelief) * (0.86 + 0.28 * colorDirt.rgb);
    vec3 pathMarginColor = mix(warmLoamColor, trailCore, wPath);

    vec3 blendedAlbedo = grassAlbedo * wGrass
                       + drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;

    if (streamWet > 0.50) {
        vec3 streamBedTint = vec3(0.17, 0.23, 0.18) * (0.84 + 0.32 * dirtDetail.r);
        blendedAlbedo = mix(blendedAlbedo, streamBedTint, smoothstep(0.50, 0.92, streamWet) * 0.55);
    }

    // Macro ground debris modulation (organic leaf litter, scree gravel, moss)
    if (u_groundDebrisEnabled != 0) {
        vec4 debris = texture(u_groundDebris, fragTexCoord);
        float fineGrain = (soilPossible) ? dirtDetail.r : fineGrassLuma;

        // Russet/amber fallen leaf litter: adds warm autumn/woodland accents under the meadow
        float dryWoodlandMask = 1.0 - clamp(shoreFactor + streamWet, 0.0, 1.0);
        if (debris.r > 0.04 && dryWoodlandMask > 0.05) {
            vec3 leafLitterColor = mix(vec3(0.25, 0.18, 0.09), vec3(0.42, 0.29, 0.14), fineGrain);
            float leafMask = smoothstep(0.10, 0.58, debris.r) * (0.40 + 0.60 * fineGrain) * dryWoodlandMask;
            blendedAlbedo = mix(blendedAlbedo, leafLitterColor, leafMask * 0.46);
        }

        // Scree gravel: mossy highland pebble grit
        if (debris.g > 0.04) {
            vec3 gravelColor = mix(vec3(0.21, 0.23, 0.16), vec3(0.34, 0.34, 0.26), fineGrain);
            float gravelMask = smoothstep(0.14, 0.68, debris.g) * (1.0 - wGrass * 0.42);
            blendedAlbedo = mix(blendedAlbedo, gravelColor, gravelMask * 0.52);
        }

        // Moss & lichen: velvety jade green in damp hollows
        if (debris.a > 0.04) {
            vec3 mossColor = mix(vec3(0.13, 0.24, 0.08), vec3(0.24, 0.36, 0.12), fineGrain);
            float mossMask = smoothstep(0.08, 0.55, debris.a);
            blendedAlbedo = mix(blendedAlbedo, mossColor, mossMask * 0.78);
        }
    }

    // Macro landscape modulation (warm sunlit ridges vs cool emerald swales)
    float macroA = sin(fragWorldPos.x * 0.042 + fragWorldPos.z * 0.028);
    float macroB = sin(fragWorldPos.x * -0.022 + fragWorldPos.z * 0.048 + 1.35);
    float macroField = 0.50 + 0.32 * macroA + 0.18 * macroB;
    blendedAlbedo *= mix(vec3(0.88, 0.95, 0.88), vec3(1.10, 1.05, 0.92), macroField);

    // Distant ridge granite outcrops on high mountain peaks
    if (camDistSqXZ > 1600.0 && fragWorldPos.y > 5.5) {
        float ridgeOutcrop = smoothstep(5.5, 11.5, fragWorldPos.y) * smoothstep(0.22, 0.52, slope);
        vec3 distantGranite = vec3(0.35, 0.34, 0.30);
        blendedAlbedo = mix(blendedAlbedo, distantGranite, ridgeOutcrop * 0.55);
    }

    // ---- 10. Singularity-Free Tangent Frame, Cliff Relief & Specular AA ----
    vec4 soilMaterial = vec4(0.5, 0.5, 1.0, 0.8);
    if (hasSoil)
        soilMaterial = textureGrad(texSoilMaterial, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85);

    vec3 microNormal = normalize(mix(grassMaterial.rgb, soilMaterial.rgb, soilWeight) * 2.0 - 1.0);
    if (wCliff > 0.05) {
        microNormal.y += cliffStrataBump * wCliff * 0.40;
        microNormal = normalize(microNormal);
    }
    if (abs(distantTuftRelief) > 0.001) {
        microNormal.xz += vec2(distantTuftRelief * 0.42, -distantTuftRelief * 0.35);
        microNormal = normalize(microNormal);
    }

    vec3 refAxis = (abs(geomNormal.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(refAxis, geomNormal));
    vec3 bitangent = cross(geomNormal, tangent);

    float puddleFlatten = 1.0 - smoothstep(0.60, 0.95, wWetSoil) * 0.45;
    float microStrength = mix(0.28, 0.54, max(soilWeight, wCliff)) * puddleFlatten * (0.45 + 0.55 * detailResolve);
    vec3 normal = normalize(geomNormal * (0.64 + 0.36 * microNormal.z)
                          + (tangent * microNormal.x + bitangent * microNormal.y) * microStrength);

    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.78, (wDrySoil + wPath) * 0.80);
    roughness = mix(roughness, 0.44, wWetSoil * 0.70);
    roughness = clamp(roughness + (1.0 - detailResolve) * 0.25, 0.22, 0.98);

    // Deeper analytical grass-canopy root contact occlusion so 3D blades pop over the substrate
    float nearCanopyMask = 1.0 - smoothstep(324.0, 1800.0, camDistSqXZ); // 18m..42m
    float canopyContactAO = mix(1.0, 0.64 + 0.24 * fineGrassLuma, wGrass * nearCanopyMask);
    float cliffCreviceAO = 1.0 - max(0.0, -cliffStrataBump) * wCliff * 0.32;
    float cavityAO = mix(0.82 + 0.18 * fineGrassLuma,
                         0.70 + 0.30 * pebbleRelief, soilWeight) * reliefShadow * cliffCreviceAO * canopyContactAO;

    // ---- 11. Physically Based Lighting & Foliage Subsurface ----
    float rawNdotL = dot(normal, light);
    float NdotL = max(rawNdotL, 0.0);
    float grassWrapNdotL = max((rawNdotL + 0.28) * (1.0 / 1.28), 0.0);
    float effectiveNdotL = mix(NdotL, grassWrapNdotL, wGrass * 0.62) * (0.82 + 0.18 * cavityAO);

    float rawShadow = MapShadowVisibility(fragPosition, normal, light);
    float turfShadowVis = rawShadow * mix(rawShadow, 1.0, 0.55);
    float resolvedShadow = mix(rawShadow, turfShadowVis, wGrass);
    float shadowFloor = mix(0.24, 0.46, wGrass);
    float shadow = mix(shadowFloor, 1.0, resolvedShadow);
    float sunVisibility = shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos);

    float ambientShadowMod = mix(mix(0.74, 0.60, wGrass), 1.0, resolvedShadow);
    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient) * (cavityAO * ambientShadowMod);

    vec3 totalLight = ambient + actualLight * effectiveNdotL * sunVisibility;
    vec3 groundLit = blendedAlbedo * totalLight;

    vec3 viewDir = viewVec * inversesqrt(max(dot(viewVec, viewVec), 0.0001));
    vec3 halfDir = normalize(light + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);

    // Warm golden subsurface back-scattering on sunlit grass hummocks at grazing sun angles
    if (wGrass > 0.05 && sunVisibility > 0.01) {
        float backScatter = max(dot(viewDir, -light), 0.0);
        float sssFactor = (backScatter * backScatter * backScatter) * wGrass * sunVisibility * 0.22;
        groundLit += grassAlbedo * actualLight * vec3(1.32, 1.42, 0.52) * sssFactor;
    }

    float h2 = NdotH * NdotH;
    float h4 = h2 * h2;
    float h8 = h4 * h4;
    float h24 = h8 * h8 * h8;
    float smoothness = 1.0 - roughness;
    float drySpec = mix(h8, h24, smoothness) * smoothness * 0.022;
    groundLit += actualLight * drySpec * sunVisibility;

    // ---- 12. Capillary Wetness Mechanics (Restrained Natural Shingle Sheen) ----
    if (wWetSoil > 0.08 && lakeDepthFactor < 0.25) {
        float waterlineBand = wWetSoil * (1.0 - smoothstep(0.05, 0.25, lakeDepthFactor));
        float oneMinusNdotV = 1.0 - max(dot(normal, viewDir), 0.0);
        float om2 = oneMinusNdotV * oneMinusNdotV;
        float fresnelWet = 0.02 + 0.98 * (om2 * om2 * oneMinusNdotV);

        float wetSpecBroad = h24 * 0.14;
        vec3 wetSheen = actualLight * (wetSpecBroad * detailResolve) * (0.25 + fresnelWet * 0.45);
        vec3 skySheen = actualAmbient * (fresnelWet * 0.12) * cavityAO;
        groundLit += (wetSheen * sunVisibility + skySheen) * waterlineBand;
    }

    if (lakeDepthFactor > 0.0) {
        groundLit += vec3(0.015, 0.035, 0.028) * actualAmbient * (1.0 - lakeDepthFactor);
    }

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    if (veil > 0.001) {
        groundLit = mix(groundLit, cloudVeilColor, veil);
    }

    finalColor = vec4(groundLit, 1.0);
}
#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is.
#include "core/shaders/common/vfx_lights.glsl"
// Preserve thin grass silhouettes without adding shadow-map samples.
#define MAP_DYNAMIC_SHADOW_RADIUS 0.65
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
    bool nearDualScale = (camDistSqXZ < 900.0); // Within 30m: enable secondary rotated texture fetch

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
            // Shore dampness extends 3.0m from water edge
            shoreFactor = max(ecology.b, 1.0 - smoothstep(0.0, 3.0, signedShore));
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
                // Fast & smooth approximation of 1.0 - pow(lakeDist, 1.6) without pow()
                lakeDepthFactor = max(lakeDepthFactor, 1.0 - lakeDist * mix(1.0, lakeDist, 0.6));
            }
        }
    }

    // ---- 5. Geometry Normal & Slope ----
    vec3 geomNormal = normalize(fragNormal);
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // ---- 6. Soil Detail (with Distance Dual-Scale Optimization) ----
    bool soilPossible = distToPath < PATH_INFLUENCE
                     || shoreFactor > 0.002
                     || streamWet > 0.40
                     || slope > 0.14
                     || rockScree > 0.004;
    vec4 colorDirt = vec4(0.5, 0.5, 0.5, 1.0);
    vec3 dirtDetail = vec3(0.5);
    if (soilPossible) {
        colorDirt = textureGrad(texPath, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85);
        if (nearDualScale) {
            vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
            vec3 broadDirt = textureGrad(texPath, broadDirtUV,
                                         vec2(-uvDx.y, uvDx.x) * 0.31,
                                         vec2(-uvDy.y, uvDy.x) * 0.31).rgb;
            dirtDetail = mix(colorDirt.rgb, broadDirt, 0.35);
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
    float wPath = 1.0 - smoothstep(1.15, 1.85, distToPath);
    float wPathMargin = smoothstep(1.05, 1.75, distToPath) * (1.0 - smoothstep(1.75, 3.2, distToPath));
    float wWetSoil = max(shoreFactor * 0.88, smoothstep(0.45, 0.88, streamWet) * 0.85);
    // Keep rolling hills green; only transition to rocky cliff/scree on genuinely steep slopes (>16 deg)
    float wSlope = smoothstep(0.26, 0.62, slope);
    float wCliff = clamp(max(wSlope, rockScree * 0.90), 0.0, 1.0);
    float wDrySoil = clamp(max(wPathMargin * 0.86, wCliff * 0.92), 0.0, 1.0) * (1.0 - wWetSoil * 0.85);
    float wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

    vec4 weights = vec4(wGrass, wDrySoil, wWetSoil, wPath);
    vec4 resolved = weights / max(dot(weights, vec4(1.0)), 0.0001);
    vec2 litterClasses = vec2(0.5);
    float reliefShadow = 1.0; // Visual upgrade: micro-shadowing at height-blend transitions

    if (u_groundReliefEnabled != 0 && detailResolve > 0.03) {
        float dominantWeight = max(max(weights.x, weights.y), max(weights.z, weights.w));
        bool boundary = dominantWeight < 0.98;
        if (boundary || weights.x > 0.02) {
            vec4 substrateRelief = textureGrad(u_groundRelief, tiledUV, uvDx, uvDy);
            litterClasses = substrateRelief.ba;
            if (boundary) {
                float soilHeight = textureGrad(u_groundRelief, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85).g;
                vec4 relief = vec4(substrateRelief.r, soilHeight, soilHeight, soilHeight);
                vec4 heights = weights + (relief - 0.5) * 0.24;
                float peak = max(max(heights.x, heights.y), max(heights.z, heights.w));
                vec4 candidates = max(heights - vec4(peak - 0.36), vec4(0.0)) * weights;
                candidates /= max(dot(candidates, vec4(1.0)), 0.0001);
                resolved = mix(resolved, candidates, detailResolve * 0.40);

                // Visual upgrade: Self-occlusion in recessed pockets between grass turf and path/soil
                float heightDelta = (substrateRelief.r - soilHeight) * (resolved.x - (1.0 - resolved.x));
                reliefShadow = clamp(1.0 + heightDelta * 0.22 * detailResolve, 0.84, 1.06);
            }
        }
    }
    wGrass = resolved.x; wDrySoil = resolved.y;
    wWetSoil = resolved.z; wPath = resolved.w;

    float soilWeight = clamp(wDrySoil + wWetSoil + wPath, 0.0, 1.0);
    bool hasGrass = wGrass > LAYER_EPS;
    bool hasSoil  = soilWeight > LAYER_EPS;

    // ---- 8. Grass Layer (Harmonized with 3D Meadow Turf & Clover Understorey) ----
    vec3 grassAlbedo = vec3(0.0);
    float fineGrassLuma = 0.5;
    vec4 grassMaterial = vec4(0.5, 0.5, 1.0, 0.8);
    if (hasGrass) {
        vec4 colorGrass = textureGrad(texGrass, tiledUV, uvDx, uvDy);
        vec3 blendedGrass = colorGrass.rgb;
        if (nearDualScale) {
            vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
            vec3 broadGrass = textureGrad(texGrass, broadGrassUV,
                                          vec2(uvDx.y, -uvDx.x) * 0.38,
                                          vec2(uvDy.y, -uvDy.x) * 0.38).rgb;
            blendedGrass = mix(colorGrass.rgb, broadGrass, 0.24);
        }
        grassMaterial = textureGrad(texGrassMaterial, tiledUV, uvDx, uvDy);
        fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));

        float turfDetail = dot(blendedGrass, vec3(0.2126, 0.7152, 0.0722));
        float detailContrast = clamp((turfDetail - 0.20) * 1.45, -0.22, 0.24);
        // Lush emerald-olive moss & clover understorey that matches the 3D grass root-to-mid palette
        vec3 turfBase = vec3(0.245, 0.345, 0.148);
        vec3 litterChroma = clamp(blendedGrass / max(turfDetail, 0.06), vec3(0.68), vec3(1.42));
        float chromaStrength = mix(0.32, 0.58, detailResolve);
        grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, chromaStrength) * (1.0 + detailContrast);

        vec3 litterTint = mix(vec3(1.0), vec3(1.04, 1.02, 0.94), litterClasses.x);
        litterTint *= mix(vec3(1.0), vec3(0.95, 1.03, 0.95), litterClasses.y);
        grassAlbedo *= mix(vec3(1.0), litterTint, detailResolve * 0.28);

        float habitatWarmth;
        if (u_ecologyEnabled != 0) {
            habitatWarmth = 1.0 - ecology.a;
        } else {
            float turfNoise = sin(fragWorldPos.x * 0.16 + fragWorldPos.z * 0.11) * 0.5
                            + sin(fragWorldPos.x * -0.08 + fragWorldPos.z * 0.24 + 1.7) * 0.35
                            + sin(fragWorldPos.x * 0.45 - fragWorldPos.z * 0.38 + 3.1) * 0.15;
            habitatWarmth = smoothstep(-0.35, 0.55, turfNoise);
        }
        grassAlbedo *= mix(vec3(0.96, 1.02, 0.96), vec3(1.05, 1.03, 0.94), habitatWarmth);
        if (u_ecologyEnabled != 0)
            grassAlbedo *= mix(vec3(1.0), vec3(0.92, 0.99, 0.93), ecology.b);

        // Seamless Canopy Imposter Blend starting at 12m so inter-clump spaces read as continuous lush meadow
        if (camDistSqXZ > 144.0) {
            float camDist = sqrt(camDistSqXZ);
            float distantCanopyBlend = smoothstep(12.0, 42.0, camDist) * wGrass;
            if (u_ecologyEnabled != 0)
                distantCanopyBlend *= smoothstep(0.08, 0.65, ecology.r);
            if (distantCanopyBlend > 0.002) {
                vec3 canopyColor = MeadowCanopyColor(1.0 - habitatWarmth);
                float distantClumpNoise = sin(fragWorldPos.x * 1.6 + fragWorldPos.z * 1.2) * 0.5
                                        + sin(fragWorldPos.x * -1.1 + fragWorldPos.z * 2.1) * 0.5;
                canopyColor *= mix(0.92, 1.08, distantClumpNoise * 0.5 + 0.5);
                grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.88);
            }
        }
    }

    // ---- 9. Triplanar Cliff Strata, Soil & Alluvial Shingle Albedo ----
    float porousDarkening = smoothstep(0.0, 0.45, wWetSoil) * (1.0 - wWetSoil * 0.35);
    vec3 warmLoamColor = dirtDetail * vec3(0.41, 0.38, 0.30) * (1.0 - porousDarkening * 0.16);

    // Triplanar world-space rock projection for steep slopes & escarpments (prevents UV stretching)
    vec3 cliffAlbedo = warmLoamColor;
    float cliffStrataBump = 0.0;
    if (wCliff > 0.02) {
        vec2 triUVx = fragWorldPos.zy * 0.28;
        vec2 triUVz = fragWorldPos.xy * 0.28;
        vec2 triWeights = abs(geomNormal.xz);
        triWeights /= max(triWeights.x + triWeights.y, 0.001);
        vec3 rockTexX = texture(texPath, triUVx).rgb;
        vec3 rockTexZ = texture(texPath, triUVz).rgb;
        vec3 triRock = rockTexX * triWeights.x + rockTexZ * triWeights.y;

        // Geological horizontal sedimentary strata + vertical joint fractures
        float strataCoord = fragWorldPos.y * 2.6 + sin(fragWorldPos.x * 0.18 + fragWorldPos.z * 0.14) * 0.85;
        float strataBand = sin(strataCoord * 3.14159) * 0.5 + 0.5;
        float strataLedge = smoothstep(0.25, 0.75, strataBand);
        float jointCrack = smoothstep(0.12, 0.55, triRock.r);
        cliffStrataBump = (strataLedge - 0.5) * 0.38 + (jointCrack - 0.5) * 0.28;

        vec3 darkSlate = vec3(0.22, 0.24, 0.25);
        vec3 warmGranite = vec3(0.42, 0.40, 0.36);
        vec3 lichenMoss = vec3(0.24, 0.33, 0.18);
        cliffAlbedo = mix(darkSlate, warmGranite, clamp(triRock.r * 0.75 + strataLedge * 0.35, 0.0, 1.0));
        // Ledge top-moss accumulation on flatter micro-terraces
        float ledgeMoss = clamp(geomNormal.y * 1.1 - 0.35, 0.0, 1.0) * smoothstep(0.45, 0.85, triRock.g);
        cliffAlbedo = mix(cliffAlbedo, lichenMoss, ledgeMoss * 0.55);
    }

    float cliffMix = clamp(wCliff / max(wDrySoil, 0.001), 0.0, 1.0);
    vec3 drySoilColor = mix(warmLoamColor, cliffAlbedo, cliffMix);

    // Natural alluvial river shingle & damp mossy silt (eliminates chocolate-plastic shoreline ring)
    float pebbleGrain = smoothstep(0.22, 0.78, dirtDetail.r);
    vec3 dampShingle = mix(vec3(0.24, 0.26, 0.22), vec3(0.35, 0.34, 0.29), pebbleGrain);
    vec3 mossyBank   = vec3(0.20, 0.28, 0.17) * (0.75 + 0.45 * dirtDetail.g);
    vec3 wetSoilColor = mix(mossyBank, dampShingle, clamp(shoreFactor * 0.65 + streamWet * 0.35, 0.0, 1.0));
    if (lakeDepthFactor > 0.0) {
        vec3 submergedBed = mix(vec3(0.18, 0.23, 0.19), vec3(0.11, 0.16, 0.15), lakeDepthFactor);
        wetSoilColor = mix(wetSoilColor, submergedBed * (0.82 + 0.36 * pebbleGrain), clamp(lakeDepthFactor * 1.15, 0.0, 1.0));
    }
    vec3 pathMarginColor = mix(warmLoamColor, colorDirt.rgb * vec3(0.64, 0.58, 0.45), wPath);

    vec3 blendedAlbedo = grassAlbedo * wGrass
                       + drySoilColor * wDrySoil
                       + wetSoilColor * wWetSoil
                       + pathMarginColor * wPath;

    if (streamWet > 0.50) {
        vec3 streamBedTint = vec3(0.21, 0.27, 0.23) * (0.82 + 0.36 * dirtDetail.r);
        blendedAlbedo = mix(blendedAlbedo, streamBedTint, smoothstep(0.50, 0.92, streamWet) * 0.58);
    }

    // Macro ground debris modulation (organic leaf litter, scree gravel, moss)
    if (u_groundDebrisEnabled != 0) {
        vec4 debris = texture(u_groundDebris, fragTexCoord);
        float fineGrain = dirtDetail.r;

        // Leaf litter: muted woodland humus (suppressed near water so shorelines never turn rusty red)
        float dryWoodlandMask = 1.0 - clamp(shoreFactor + streamWet, 0.0, 1.0);
        if (debris.r > 0.05 && dryWoodlandMask > 0.05) {
            vec3 leafLitterColor = mix(vec3(0.28, 0.24, 0.15), vec3(0.42, 0.34, 0.20), fineGrain);
            float leafMask = smoothstep(0.12, 0.62, debris.r) * (0.35 + 0.65 * fineGrain) * dryWoodlandMask;
            blendedAlbedo = mix(blendedAlbedo, leafLitterColor, leafMask * 0.42);
        }

        // Scree gravel: stony pebble grit
        if (debris.g > 0.04) {
            vec3 gravelColor = mix(vec3(0.26, 0.28, 0.27), vec3(0.42, 0.41, 0.37), fineGrain);
            float gravelMask = smoothstep(0.10, 0.60, debris.g);
            blendedAlbedo = mix(blendedAlbedo, gravelColor, gravelMask * 0.72);
        }

        // Moss & lichen: velvety jade green in damp hollows
        if (debris.a > 0.04) {
            vec3 mossColor = mix(vec3(0.16, 0.28, 0.10), vec3(0.28, 0.42, 0.15), fineGrain);
            float mossMask = smoothstep(0.08, 0.55, debris.a);
            blendedAlbedo = mix(blendedAlbedo, mossColor, mossMask * 0.85);
        }
    }

    // Macro landscape modulation
    float macroA = sin(fragWorldPos.x * 0.042 + fragWorldPos.z * 0.028);
    float macroB = sin(fragWorldPos.x * -0.022 + fragWorldPos.z * 0.048 + 1.35);
    float macroField = 0.50 + 0.32 * macroA + 0.18 * macroB;
    blendedAlbedo *= mix(vec3(0.94, 0.97, 0.92), vec3(1.04, 1.02, 0.96), macroField);

    // ---- 10. Tangent-Space Micro Normals, Cliff Relief & Specular AA ----
    vec4 soilMaterial = vec4(0.5, 0.5, 1.0, 0.8);
    if (hasSoil)
        soilMaterial = textureGrad(texSoilMaterial, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85);

    vec3 microNormal = normalize(mix(grassMaterial.rgb, soilMaterial.rgb, soilWeight) * 2.0 - 1.0);
    if (wCliff > 0.02) {
        microNormal.y += cliffStrataBump * wCliff * 1.35;
        microNormal = normalize(microNormal);
    }
    // Fast Gram-Schmidt tangent frame (avoids division and extra normalize)
    vec3 tangent = normalize(vec3(1.0 - geomNormal.x * geomNormal.x, -geomNormal.x * geomNormal.y, -geomNormal.x * geomNormal.z));
    vec3 bitangent = cross(geomNormal, tangent);

    // Puddle water surface flattens micro-bumps in saturated zones; cliff faces boost relief
    float puddleFlatten = 1.0 - smoothstep(0.60, 0.95, wWetSoil) * 0.45;
    float microStrength = mix(0.16, 0.38, max(soilWeight, wCliff)) * puddleFlatten * (0.40 + 0.60 * detailResolve);
    vec3 normal = normalize(geomNormal * (0.70 + 0.30 * microNormal.z)
                          + (tangent * microNormal.x + bitangent * microNormal.y) * microStrength);

    float roughness = mix(grassMaterial.a, soilMaterial.a, soilWeight);
    roughness = mix(roughness, 0.76, wDrySoil * 0.80);
    // Natural damp shingle roughness (avoids glossy plastic look on beaches/banks)
    roughness = mix(roughness, 0.42, wWetSoil * 0.70);
    // Geometric Specular AA prevents distant wet-surface sparkling
    roughness = clamp(roughness + (1.0 - detailResolve) * 0.25, 0.18, 0.98);

    // Micro cavity AO + Height-blend boundary shadow + Cliff crevice AO
    float cliffCreviceAO = 1.0 - max(0.0, -cliffStrataBump) * wCliff * 0.35;
    float cavityAO = mix(0.88 + 0.12 * fineGrassLuma,
                         0.76 + 0.24 * colorDirt.r, soilWeight) * reliefShadow * cliffCreviceAO;

    // ---- 11. Physically Based Lighting & Foliage Subsurface ----
    float rawNdotL = dot(normal, light);
    float NdotL = max(rawNdotL, 0.0);
    // Soft wrap diffuse for grass/moss substrate (simulates light scattering through roots/moss)
    float grassWrapNdotL = max((rawNdotL + 0.22) * (1.0 / 1.22), 0.0);
    float effectiveNdotL = mix(NdotL, grassWrapNdotL, wGrass * 0.55);

    float shadow = MapShadowVisibility(fragPosition, normal, light);
    float sunVisibility = shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos);
    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient) * cavityAO;

    vec3 totalLight = ambient + actualLight * effectiveNdotL * sunVisibility;
    vec3 groundLit = blendedAlbedo * totalLight;

    vec3 viewDir = viewVec * inversesqrt(max(dot(viewVec, viewVec), 0.0001));
    vec3 halfDir = normalize(light + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);

    // Warm subsurface back-scattering on sunlit grass hummocks at grazing sun angles
    if (wGrass > 0.05 && sunVisibility > 0.01) {
        float backScatter = max(dot(viewDir, -light), 0.0);
        float sssFactor = (backScatter * backScatter) * wGrass * sunVisibility * 0.09;
        groundLit += grassAlbedo * actualLight * vec3(1.15, 1.25, 0.65) * sssFactor;
    }

    float specPower = mix(8.0, 28.0, 1.0 - roughness);
    float drySpec = pow(NdotH, specPower) * (1.0 - roughness) * 0.032;
    groundLit += actualLight * drySpec * sunVisibility;

    // ---- 12. Capillary Wetness Mechanics (Restrained Natural Shingle Sheen) ----
    if (wWetSoil > 0.08 && lakeDepthFactor < 0.25) {
        float waterlineBand = wWetSoil * (1.0 - smoothstep(0.05, 0.25, lakeDepthFactor));
        float oneMinusNdotV = 1.0 - max(dot(normal, viewDir), 0.0);
        float om2 = oneMinusNdotV * oneMinusNdotV;
        float fresnelWet = 0.02 + 0.98 * (om2 * om2 * oneMinusNdotV);

        float h2 = NdotH * NdotH;
        float h4 = h2 * h2;
        float h8 = h4 * h4;
        float h24 = h8 * h8 * h8;
        float wetSpecBroad = h24 * 0.14;

        vec3 wetSheen = actualLight * (wetSpecBroad * detailResolve) * (0.25 + fresnelWet * 0.45);
        vec3 skySheen = actualAmbient * (fresnelWet * 0.12) * cavityAO;
        groundLit += (wetSheen * sunVisibility + skySheen) * waterlineBand;
    }

    // Submerged riverbed ambient tint
    if (lakeDepthFactor > 0.0) {
        groundLit += vec3(0.015, 0.035, 0.028) * actualAmbient * (1.0 - lakeDepthFactor);
    }

    // VFX Point Lights
    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    // Apply Cloud Veil (precomputed in step 2)
    if (veil > 0.001) {
        groundLit = mix(groundLit, cloudVeilColor, veil);
    }

    finalColor = vec4(groundLit, 1.0);
}
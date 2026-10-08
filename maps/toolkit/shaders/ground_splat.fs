#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is; a private copy of the
// falloff drifts the moment one of them is edited.
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

void main()
{
    // Dual-scale rotated texture sampling to break tiling
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 colorGrass = texture(texGrass, tiledUV);
    vec2 broadGrassUV = vec2(tiledUV.y * 0.38 + 17.0, -tiledUV.x * 0.38 + 9.0);
    vec3 broadGrass = texture(texGrass, broadGrassUV).rgb;
    float fineGrassLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec2 uvDx = dFdx(tiledUV), uvDy = dFdy(tiledUV);
    float pixelMeters = max(length(dFdx(fragWorldPos.xz)), length(dFdy(fragWorldPos.xz)));
    float detailResolve = 1.0 - smoothstep(0.015, 0.070, pixelMeters);

    // Soil detail from dirt texture
    vec4 colorDirt = texture(texPath, tiledUV * 0.85);
    vec2 broadDirtUV = vec2(-tiledUV.y * 0.31 + 8.5, tiledUV.x * 0.31 + 14.2);
    vec3 broadDirt = texture(texPath, broadDirtUV).rgb;
    vec3 dirtDetail = mix(colorDirt.rgb, broadDirt, 0.35);
    vec4 grassMaterial = texture(texGrassMaterial, tiledUV);
    vec4 soilMaterial = texture(texSoilMaterial, tiledUV * 0.85);

    // 1. Distance to path
    vec4 ecology = vec4(1.0, 0.6, 0.0, 0.5);
    float roadEdge = 998.85;
    if (u_ecologyEnabled != 0) {
        vec2 ecoUV = (fragWorldPos.xz - u_ecologyRect.xy) / u_ecologyRect.zw;
        ecology = texture(u_ecology, ecoUV);
        vec4 metric = texture(u_ecologyDistance, ecoUV);
        roadEdge = ((metric.r + metric.g / 255.0) * 2.0 - 1.0) * 16.0;
    }
    float distToPath = roadEdge + 1.15;
    if (u_ecologyEnabled == 0 && u_pathSegCount > 0) {
        for (int i = 0; i < u_pathSegCount && i < MAX_PATH_SEGS; i++) {
            vec2 a = u_pathSegs[i].xy;
            vec2 b = u_pathSegs[i].zw;
            vec2 pa = fragWorldPos.xz - a;
            vec2 ba = b - a;
            float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
            distToPath = min(distToPath, length(pa - ba * h));
        }
    }

    // 2. Shoreline wetness & lake basin texturing
    float shoreFactor = 0.0;
    float lakeDepthFactor = 0.0;
    if (u_lakeParams.z > 0.0) {
        vec2 lakeDelta = (fragWorldPos.xz - u_lakeParams.xy) / u_lakeParams.zw;
        float lakeDist = length(lakeDelta);
        // Inside lake basin and along shoreline, transition smoothly from wet silt to dry turf
        float distWet = 1.0 - smoothstep(0.96, 1.25, lakeDist);
        shoreFactor = u_ecologyEnabled != 0 ? max(ecology.b, distWet) : distWet;
        if (lakeDist < 1.0) {
            lakeDepthFactor = 1.0 - pow(lakeDist, 1.6);
        }
    }

    // 3. Slope steepness
    vec3 geomNormal = normalize(fragNormal);
    float slope = clamp(1.0 - geomNormal.y, 0.0, 1.0);

    // Small irregularities follow real detail, never displace the lake cutout.
    float pathMeander = sin(fragWorldPos.x * 0.85 + fragWorldPos.z * 1.15) * 0.22
                      + sin(fragWorldPos.x * 2.30 - fragWorldPos.z * 1.70) * 0.11;
    distToPath += pathMeander;
    if (u_ecologyEnabled != 0)
        distToPath += (dirtDetail.r - 0.5) * 0.18;

    // 4. Four-layer weights
    float wPath = 1.0 - smoothstep(1.15, 1.85, distToPath);
    float wPathMargin = smoothstep(1.05, 1.75, distToPath) * (1.0 - smoothstep(1.75, 3.2, distToPath));
    float wWetSoil = shoreFactor * 0.95;
    float wSlope = smoothstep(0.14, 0.46, slope);
    float wDrySoil = clamp(wPathMargin * 0.88 + wSlope * 0.92, 0.0, 1.0);
    float wGrass = clamp(1.0 - wPath - wWetSoil - wDrySoil, 0.0, 1.0);

    // Separate relief reconstructed offline from normal gradients. The alpha
    // of material maps remains roughness; albedo brightness never drives height.
    vec4 weights = vec4(wGrass, wDrySoil, wWetSoil, wPath);
    vec4 normalizedWeights = weights / max(dot(weights, vec4(1.0)), 0.0001);
    vec4 resolved = normalizedWeights;
    vec2 litterClasses = vec2(0.5);
    if (u_groundReliefEnabled != 0 && detailResolve > 0.03) {
        vec4 substrateRelief = textureGrad(u_groundRelief, tiledUV, uvDx, uvDy);
        litterClasses = substrateRelief.ba;
        // Only material boundaries need the second height read. Far pixels
        // use the shared ecological coverage without additional relief reads.
        float dominantWeight = max(max(weights.x, weights.y), max(weights.z, weights.w));
        if (dominantWeight < 0.98) {
            float soilHeight = textureGrad(u_groundRelief, tiledUV * 0.85, uvDx * 0.85, uvDy * 0.85).g;
            vec4 relief = vec4(substrateRelief.r, soilHeight, soilHeight, soilHeight);
            vec4 heights = weights + (relief - 0.5) * 0.24;
            float peak = max(max(heights.x, heights.y), max(heights.z, heights.w));
            vec4 candidates = max(heights - vec4(peak - 0.36), vec4(0.0)) * weights;
            candidates /= max(dot(candidates, vec4(1.0)), 0.0001);
            resolved = mix(normalizedWeights, candidates, detailResolve * 0.40);
        }
    }
    wGrass = resolved.x; wDrySoil = resolved.y;
    wWetSoil = resolved.z; wPath = resolved.w;

    // Preserve litter detail at low contrast; foliage owns the visible canopy.
    // A restrained olive underlayer matches the roots instead of competing
    // with them as a bright second grass canopy.
    vec3 blendedGrass = mix(colorGrass.rgb, broadGrass, 0.20);
    float turfDetail = dot(blendedGrass, vec3(0.2126, 0.7152, 0.0722));
    // Normalize the authored chroma independently of exposure. Resolved moss
    // and tan roots retain color; minified substrate settles into olive earth.
    float detailContrast = clamp((turfDetail - 0.20) * 1.65, -0.28, 0.25);
    vec3 turfBase = vec3(0.255, 0.290, 0.165);
    vec3 litterChroma = clamp(blendedGrass / max(turfDetail, 0.06),
                              vec3(0.60), vec3(1.50));
    float chromaStrength = mix(0.38, 0.68, detailResolve);
    vec3 grassAlbedo = turfBase * mix(vec3(1.0), litterChroma, chromaStrength)
                               * (1.0 + detailContrast);
    vec3 litterTint = mix(vec3(1.0), vec3(1.08, 1.01, 0.91), litterClasses.x);
    litterTint *= mix(vec3(1.0), vec3(0.97, 1.015, 0.96), litterClasses.y);
    grassAlbedo *= mix(vec3(1.0), litterTint, detailResolve * 0.35);

    // Multi-scale organic turf variation (deep damp swales vs warm sunny hummocks)
    float turfNoise = sin(fragWorldPos.x * 0.16 + fragWorldPos.z * 0.11) * 0.5
                    + sin(fragWorldPos.x * -0.08 + fragWorldPos.z * 0.24 + 1.7) * 0.35
                    + sin(fragWorldPos.x * 0.45 - fragWorldPos.z * 0.38 + 3.1) * 0.15;
    vec3 warmTurf = grassAlbedo * vec3(1.04, 1.02, 0.95);
    vec3 coolTurf = grassAlbedo * vec3(0.97, 1.00, 0.98);
    float habitatWarmth = u_ecologyEnabled != 0 ? 1.0 - ecology.a
                         : smoothstep(-0.35, 0.55, turfNoise);
    grassAlbedo = mix(coolTurf, warmTurf, habitatWarmth);
    if (u_ecologyEnabled != 0)
        grassAlbedo *= mix(vec3(1.0), vec3(0.93, 0.97, 0.94), ecology.b);

    // Distant Grass Canopy Imposter Blend (Seamless infinite horizon):
    // Near the camera, dark root soil is visible between individual blades.
    // Approaching the 3D grass culling horizon (22m -> 50m), smoothly transition
    // ground albedo into the lush illuminated grass canopy color so the cutoff is invisible.
    float camDist = length(viewPos.xz - fragWorldPos.xz);
    float distantCanopyBlend = smoothstep(22.0, 50.0, camDist) * wGrass;
    if (u_ecologyEnabled != 0)
        distantCanopyBlend *= smoothstep(0.10, 0.75, ecology.r);
    vec3 canopyColor = MeadowCanopyColor(1.0 - habitatWarmth);
    float distantClumpNoise = sin(fragWorldPos.x * 1.6 + fragWorldPos.z * 1.2) * 0.5
                            + sin(fragWorldPos.x * -1.1 + fragWorldPos.z * 2.1) * 0.5;
    canopyColor *= mix(0.90, 1.10, distantClumpNoise * 0.5 + 0.5);
    grassAlbedo = mix(grassAlbedo, canopyColor, distantCanopyBlend * 0.86);

    // Soil & Path PBR Albedos
    vec3 drySoilColor = dirtDetail * vec3(0.86, 0.77, 0.63) * 1.18;
    vec3 wetSoilColor = dirtDetail * vec3(0.62, 0.58, 0.48) * 1.02;
    if (lakeDepthFactor > 0.0) {
        wetSoilColor *= mix(1.0, 0.62, lakeDepthFactor);
    }
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
    float sunVisibility = shadow * Environment_CloudVisibility(u_cloudNoise, fragWorldPos);
    vec3 ambient = Environment_HemisphereIrradiance(normal, actualAmbient.rgb) * cavityAO;

    // Physically Based Lighting (PBR daylight law):
    // Direct solar irradiance is modulated by directional shadow visibility.
    // Hemispheric sky ambient and ground bounce illuminate the surface even in shadow,
    // naturally scaled by cavity AO and sky exposure without artificial darkening.
    vec3 totalLight = ambient + actualLight.rgb * NdotL * sunVisibility;

    vec3 groundLit = blendedAlbedo * totalLight;
    vec3 viewDir = normalize(viewPos - fragWorldPos);
    vec3 halfDir = normalize(light + viewDir);
    float specPower = mix(12.0, 72.0, 1.0 - roughness);
    float drySpec = pow(max(dot(normal, halfDir), 0.0), specPower)
                  * (1.0 - roughness) * 0.16;
    groundLit += actualLight.rgb * drySpec * sunVisibility;

    // Capillary wetness mechanics: Albedo darkening & Roughness collapse (PBR Specular Sheen)
    if (wWetSoil > 0.03) {
        float NdotV_wet = max(dot(normal, viewDir), 0.0);
        float fresnelWet = 0.02 + 0.98 * pow(1.0 - NdotV_wet, 5.0);
        float wetSpecSharp = pow(max(dot(normal, halfDir), 0.0), 96.0);
        float wetSpecBroad = pow(max(dot(normal, halfDir), 0.0), 24.0) * 0.25;
        vec3 wetSheen = actualLight.rgb * (wetSpecSharp * 0.75 + wetSpecBroad) * (0.35 + fresnelWet * 0.65);
        groundLit += wetSheen * wWetSoil * sunVisibility;
    }

    // The steep lake cutout faces away from the sun. Sky and water bounce keep
    // that short bank readable instead of leaving a black moat around the lake.
    groundLit += vec3(0.10, 0.13, 0.10) * shoreFactor * (0.75 + 0.25 * dirtDetail.r);

    groundLit += VFXLights_AccumulateFlat(fragPosition, blendedAlbedo);

    if (u_islandRect.z > 0.0 && u_islandRect.w > 0.0) {
        float edge = MapIslandDistance(fragWorldPos.xz, u_islandRect, u_islandShape.x);
        float veil = smoothstep(-max(u_islandShape.y, 0.1), 0.0, edge + max(u_islandShape.z, 0.0))
                   * (1.0 - smoothstep(-2.0, 0.1, fragWorldPos.y));
        vec3 cloudLight = clamp(ambientColor.rgb + lightColor.rgb * max((normalize(-lightDir).y + 0.4) / 1.4, 0.0) * 0.55, 0.0, 1.05);
        groundLit = mix(groundLit, vec3(0.88, 0.91, 0.96) * cloudLight, veil);
    }
    finalColor = vec4(groundLit, 1.0);
}

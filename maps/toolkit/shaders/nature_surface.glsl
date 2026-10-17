#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

// ============================================================================
// FAST MATH HELPERS (ALU Optimized for Foliage Shading)
// ============================================================================
float FastPow2(float x) { return x * x; }
float FastPow4(float x) { float x2 = x * x; return x2 * x2; }
float FastPow8(float x) { float x2 = x * x; float x4 = x2 * x2; return x4 * x4; }
float FastPow16(float x) { float x4 = FastPow4(x); return x4 * x4 * x4 * x4; }
float FastPow32(float x) { float x16 = FastPow16(x); return x16 * x16; }

// Rational approximation for pow(x, 1.4) on [0, 1]: x / (1.4 - 0.4 * x)
float FastPow1_4(float x) { return x / (1.4 - 0.4 * x); }

// Fast linear saturate ramp (compiles to a single MAD_SAT instruction on GPU)
float FastRamp(float edge0, float edge1, float x)
{
    return clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
}

// ============================================================================
// SHADOW MAPPING (Zero Redundant Texture Fetches)
// ============================================================================
float NatureShadowCompare(sampler2D mapTexture, vec2 uv, float depth,
                          float texelSize, float filterWeight)
{
    if (filterWeight <= 0.05)
        return MapShadowCompare(mapTexture, uv, depth);
    float effectiveTexel = texelSize * FastRamp(0.0, 0.35, filterWeight);
    return MapShadowCompareBilinear(mapTexture, uv, depth, effectiveTexel);
}

float FoliageShadowVisibility(vec4 lightSpace, vec4 staticLightSpace,
                              vec3 normal, vec3 lightDir, vec3 worldPosition,
                              float distToCam)
{
    // Shading LOD: Smoothly fade dynamic shadows across 18m..30m to avoid a visible cascade line
    float shadowFade = 1.0 - FastRamp(18.0, 30.0, distToCam);
    if (shadowFade <= 0.0)
        return 1.0;

    float slope = 1.0 - max(dot(normal, lightDir), 0.0);
    float filterWeight = (u_shadowFilterQuality > 1.5)
        ? (1.0 - FastRamp(14.0, 24.0, distToCam)) : 0.0;

    float dynamicShadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        float invW = 1.0 / max(lightSpace.w, 1e-5);
        vec3 projected = (lightSpace.xyz * invW) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float bias = clamp(0.0022 + slope * 0.0038, 0.0018, 0.0060);
            float rawDyn = NatureShadowCompare(shadowMap, projected.xy,
                projected.z - bias, u_shadowTexel, filterWeight);
            // Translucent grass blades transmit ~38% of direct sunlight through the turf canopy,
            // preventing a harsh dark/bright step at the dynamic shadow caster distance boundary.
            rawDyn = mix(0.38, 1.0, rawDyn);
            dynamicShadow = mix(1.0, rawDyn, MapShadowCoverageFadeWidth(projected.xy, 0.18));
        }
    }

    // Early-out optimization: Only sample static shadow map if dynamic shadow isn't already occluded
    float combined = dynamicShadow;
    if (u_staticShadowEnabled > 0.5 && dynamicShadow > 0.01) {
        float invW = 1.0 / max(staticLightSpace.w, 1e-5);
        vec3 projected = (staticLightSpace.xyz * invW) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float bias = clamp(0.0025 + slope * 0.0040, 0.0020, 0.0070);
            float staticShadow = NatureShadowCompare(staticShadowMap, projected.xy,
                projected.z - bias, u_staticShadowTexel, filterWeight);
            staticShadow = mix(1.0, staticShadow, MapShadowCoverageFadeWidth(projected.xy, 0.08));
            combined = min(dynamicShadow, staticShadow);
        }
    }

    return mix(1.0, combined, shadowFade);
}

// ============================================================================
// GENERAL FOLIAGE, STEMS, REEDS & BLOOMS SHADING
// ============================================================================
vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant, float uTransverse,
                 vec4 lightSpace, vec4 staticLightSpace)
{
    vec3 toCam = u_viewPos - worldPosition;
    float distSq = dot(toCam, toCam);
    float invDist = inversesqrt(max(distSq, 1e-6));
    vec3 viewDir = toCam * invDist;
    float distToCam = distSq * invDist;

    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float bloomMask = smoothstep(0.65, 0.90, hNorm);

    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Smooth continuous upper-hemisphere bias
    faceNormal.y = max(faceNormal.y, -0.15) + 0.24;
    faceNormal = normalize(faceNormal);

    // Curved Normals & Cylindrical Volume Illusion
    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.60 * (1.0 - bloomMask)));
    }

    // Distant Normal Flattening (Anti-Shimmering)
    float antiShimmer = FastRamp(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    float flattenFactor = antiShimmer * 0.85;
    float groundBlend = (1.0 - FastRamp(0.0, 0.25, hNorm)) * 0.35;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), totalTerrainBlend));

    // Root-to-ground color blending
    const vec3 soilTone = vec3(0.18, 0.15, 0.09);
    float stemRootBlend = (1.0 - FastRamp(0.0, 0.22, hNorm)) * (1.0 - bloomMask) * 0.65;
    baseColor = mix(baseColor, soilTone, stemRootBlend);

    // Wrapped Diffuse Lighting
    float wrap = mix(0.32, 0.42, bloomMask);
    float wrapped = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    // Two-sided Subsurface Scattering (SSS) Transmission
    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = FastPow1_4(backfaceSun) * 0.70 + FastPow2(viewSunAlign) * 0.60;

    float petalDiffusion = 0.28 + 0.72 * wrapped;
    transmissionAngle = mix(transmissionAngle, max(transmissionAngle, petalDiffusion * 0.75), bloomMask);
    float thinness = mix(0.20, 1.0, FastRamp(0.06, 0.65, hNorm));
    float transmission = transmissionAngle * thinness;

    vec3 foliageSSS = baseColor * vec3(1.65, 1.82, 0.52) + vec3(0.05, 0.08, 0.015) * thinness;
    vec3 petalSSS   = baseColor * vec3(1.48, 1.28, 0.92) + vec3(0.08, 0.04, 0.02);
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    // Skip cloud noise texture fetch when already in full shadow
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float cloudVis = (shadow > 0.001) ? Environment_CloudVisibility(u_cloudNoise, worldPosition) : 0.0;

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 1.2 * invH + 0.72 * invH * invH), 0.58, 1.0);

    // Hemispheric Ambient Lighting
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);

    float cupCavity = mix(0.58, 1.0, FastRamp(0.82, 0.98, hNorm));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    // Specular and Rim only evaluated when sunlit (shadow * cloudVis > 0)
    float sunFactor = canopyExtinction * cloudVis * shadow;
    if (sunFactor > 0.001) {
        vec3 halfDir = normalize(u_lightDir + viewDir);
        float NdotH = max(dot(faceNormal, halfDir), 0.0);
        float specLobeStem = FastPow32(NdotH);
        float specLobeBloom = FastPow16(NdotH);
        float specIntensity = mix(0.75 * (1.0 - antiShimmer * 0.75), 0.45, bloomMask) * canopyExtinction;
        float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

        float oneMinusNV = 1.0 - max(dot(faceNormal, viewDir), 0.0);
        float rim = (oneMinusNV * oneMinusNV * sqrt(oneMinusNV)) * mix(0.22, 0.30, bloomMask);

        vec3 directSunTerms = baseColor * (directDiffuse * 1.10 + rim)
                            + subsurfaceColor * (transmission * mix(0.75, 0.92, bloomMask))
                            + vec3(spec, spec, spec * 0.94);
        lit += directSunTerms * (u_lightColor * sunFactor);
    }

    lit += subsurfaceColor * (transmission * mix(0.18, 0.24, bloomMask)) * skyAmbient;

    float hCurve = hNorm / (0.70 + 0.30 * hNorm);
    float rootAO = clamp(0.74 + 0.26 * hCurve, 0.74, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// ============================================================================
// HIGH-DENSITY BLADE GRASS SHADING (Dual-Lobe Anisotropic + Anti-Aliased)
// ============================================================================
vec3 GrassShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                float heightAlongPlant, float uTransverse,
                vec4 lightSpace, vec4 staticLightSpace)
{
    float h = clamp(heightAlongPlant, 0.0, 1.0);

    vec3 toCam = u_viewPos - worldPosition;
    float distSq = dot(toCam, toCam);
    float invDist = inversesqrt(max(distSq, 1e-6));
    vec3 viewDir = toCam * invDist;
    float distToCam = distSq * invDist;

    // Smooth upward normal bias avoids derivative discontinuities in dFdx/dFdy
    vec3 bladeNormal = normalize(worldNormal);
    vec3 faceNormal = normalize(vec3(bladeNormal.x, max(abs(bladeNormal.y), 0.18), bladeNormal.z));

    float absU = abs(uTransverse);
    // Division-free unresolved blade metric: widthPixels in [1.0, 3.0] <=> fwidth(u) in [0.6667, 2.0]
    float fwU = fwidth(uTransverse);
    float unresolved = FastRamp(0.6667, 2.0, fwU);
    float antiShimmer = max(unresolved, FastRamp(10.0, 32.0, distToCam));

    float groundBlend = (1.0 - FastRamp(0.0, 0.30, h)) * 0.30;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.80) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // Soft central rib & subtle edge relief (filtered away when unresolved)
    float rib = 1.0 - FastRamp(0.0, 0.34, absU);
    baseColor *= 1.0 + (rib - 0.35) * 0.10 * (1.0 - unresolved);

    // Root-to-Ground Color Blending
    const vec3 soilTone = vec3(0.18, 0.15, 0.09);
    float rootBlend = (1.0 - FastRamp(0.0, 0.24, h)) * 0.68;
    baseColor = mix(baseColor, soilTone, rootBlend);

    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);

    // Deep ground contact ambient occlusion at blade base
    float rootAO = mix(0.18, 1.0, smoothstep(0.0, 0.55, h));
    vec3 lit = baseColor * ambient * rootAO;

    // Waxy cuticle grazing sheen (Fresnel reflecting sky)
    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    float heightSheenMask = FastRamp(0.22, 0.88, h);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.34 * heightSheenMask * (1.0 - antiShimmer * 0.5));
    lit += waxSheen * rootAO;

    // Shadow + conditional Cloud visibility (skips cloud texture lookup if shadowed)
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    if (shadow > 0.001) {
        float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
        if (sunVis > 0.001) {
            float wrapped = clamp((dot(n, u_lightDir) + 0.42) * 0.704225, 0.0, 1.0);

            // Beer-Lambert attenuation through the canopy
            float canopyDepth = 0.45 * (1.0 - h);
            float opticalDepth = canopyDepth * 0.9 / max(u_lightDir.y, 0.10);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.48 * opticalDepth * opticalDepth);
            float canopy = 0.12 + 0.88 * expCanopy;

            // Analytical Kajiya-Kay Anisotropic Specular without constructing or normalizing tangent vectors:
            // Since T_raw = up - N.y * N and ||T_raw||^2 = 1 - N.y^2, dot(T, H)^2 = (H.y - N.y * dot(N, H))^2 / (1 - N.y^2)
            vec3 halfDir = normalize(u_lightDir + viewDir);
            float NdotH = dot(faceNormal, halfDir);
            float tDotHUnnorm = halfDir.y - faceNormal.y * NdotH;
            // Subtle cuticular tilt shift along blade height for cohesive Ghost of Tsushima specular waves
            float shiftedTH = tDotHUnnorm - (h - 0.55) * 0.12;
            float invTangentLenSq = 1.0 / max(1.0 - faceNormal.y * faceNormal.y, 1e-4);
            float sinTHSq = clamp(1.0 - (shiftedTH * shiftedTH) * invTangentLenSq, 0.0, 1.0);

            // Normal variance Toksvig-style anti-aliasing
            vec3 normalDx = dFdx(faceNormal);
            vec3 normalDy = dFdy(faceNormal);
            float variance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
            float specAA = (1.0 - antiShimmer) / (1.0 + 8.0 * variance);

            float sinTH4  = sinTHSq * sinTHSq;
            float sinTH8  = sinTH4 * sinTH4;
            float sinTH16 = sinTH8 * sinTH8;
            float sinTH36 = sinTH16 * sinTHSq;

            float tipMask = FastRamp(0.18, 0.85, h);
            vec3 primarySheen = mix(baseColor, vec3(1.0, 1.02, 0.94), 0.68) * (sinTH8 * 0.085);
            vec3 secondaryGlint = vec3(1.0, 1.02, 0.95) * (sinTH36 * 0.24 * tipMask);
            vec3 anisoSpec = (primarySheen + secondaryGlint) * (specAA * max(NdotH + 0.35, 0.0));

            // Chlorophyll translucency: radiant emerald-gold backlight through leaf membrane
            float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, u_lightDir), 0.0);
            float forwardLobe = FastPow2(forwardScatter);
            float backLobe = FastPow1_4(backLight);
            float edgeThinness = 0.72 + 0.56 * absU;
            float transmission = (backLobe * 0.65 + forwardLobe * 0.65)
                                * FastRamp(0.08, 0.75, h) * edgeThinness;
            vec3 translucentColor = baseColor * vec3(1.22, 1.30, 0.70) + vec3(0.04, 0.06, 0.005) * edgeThinness;

            vec3 directSunTerms = baseColor * (wrapped * wrapped * 1.05)
                                + anisoSpec
                                + translucentColor * (transmission * 1.12);
            lit += directSunTerms * u_lightColor * (canopy * sunVis);
        }
    }

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
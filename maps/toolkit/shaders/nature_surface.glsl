#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"
#include "maps/toolkit/shaders/grass_blade_state.glsl"

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
// SHADOW MAPPING (True Early-Out & Exact Texel-Grid Alignment)
// ============================================================================
float NatureShadowCompare(sampler2D mapTexture, vec2 uv, float depth,
                          float texelSize)
{
    return MapShadowCompareBilinear(mapTexture, uv, depth, texelSize);
}

float FoliageShadowVisibility(vec4 lightSpace, vec4 staticLightSpace,
                              vec3 normal, vec3 lightDir, vec3 worldPosition,
                              float distToCam)
{
    float shadowFade = 1.0 - FastRamp(16.0, 28.0, distToCam);
    if (shadowFade <= 0.0)
        return 1.0;

    float slope = 1.0 - max(dot(normal, lightDir), 0.0);

    float rawDyn = 1.0;
    if (u_shadowEnabled > 0.5) {
        float invW = 1.0 / max(lightSpace.w, 1e-5);
        vec3 projected = (lightSpace.xyz * invW) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float edgeFade = MapShadowCoverageFadeWidth(projected.xy, 0.18);
            if (edgeFade > 0.0) {
                float bias = clamp(0.0020 + slope * 0.0036, 0.0016, 0.0058);
                float sampleDyn = NatureShadowCompare(shadowMap, projected.xy,
                    projected.z - bias, u_shadowTexel);
                rawDyn = mix(1.0, sampleDyn, edgeFade);
            }
        }
    }

    float rawCombined = rawDyn;
    if (u_staticShadowEnabled > 0.5 && rawDyn > 0.02) {
        float invW = 1.0 / max(staticLightSpace.w, 1e-5);
        vec3 projected = (staticLightSpace.xyz * invW) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float edgeFade = MapShadowCoverageFadeWidth(projected.xy, 0.08);
            if (edgeFade > 0.0) {
                float bias = clamp(0.0024 + slope * 0.0038, 0.0018, 0.0068);
                float sampleStatic = NatureShadowCompare(staticShadowMap, projected.xy,
                    projected.z - bias, u_staticShadowTexel);
                float rawStatic = mix(1.0, sampleStatic, edgeFade);
                rawCombined = min(rawDyn, rawStatic);
            }
        }
    }

    // Translucent grass blades transmit ~48% of direct sunlight through the turf canopy,
    // preventing harsh black shadow stripes across hillsides.
    float translucentVis = mix(0.48, 1.0, rawCombined);
    return mix(1.0, translucentVis, shadowFade);
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

    faceNormal.y = max(faceNormal.y, -0.15) + 0.24;
    faceNormal = normalize(faceNormal);

    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.60 * (1.0 - bloomMask)));
    }

    float antiShimmer = FastRamp(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    float flattenFactor = antiShimmer * 0.85;
    float groundBlend = (1.0 - FastRamp(0.0, 0.25, hNorm)) * 0.35;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), totalTerrainBlend));

    // Match root of flower stems & basal leaves to moss-green turf instead of dark brown dirt
    const vec3 turfRootTone = vec3(0.18, 0.28, 0.10);
    float stemRootBlend = (1.0 - FastRamp(0.0, 0.24, hNorm)) * (1.0 - bloomMask) * 0.55;
    baseColor = mix(baseColor, turfRootTone, stemRootBlend);

    vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.78));

    vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.30 * u_lightDir.y, u_lightDir.y * 0.78, u_lightDir.z - 0.22 * u_lightDir.y));
    float wrap = mix(0.32, 0.42, bloomMask);
    float wrapped = clamp((dot(faceNormal, modelLightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = FastPow1_4(backfaceSun) * 0.70 + FastPow2(viewSunAlign) * 0.60;

    float petalDiffusion = 0.28 + 0.72 * wrapped;
    transmissionAngle = mix(transmissionAngle, max(transmissionAngle, petalDiffusion * 0.75), bloomMask);
    float thinness = mix(0.20, 1.0, FastRamp(0.06, 0.65, hNorm));
    float transmission = transmissionAngle * thinness;

    vec3 foliageSSS = baseColor * vec3(1.60, 1.78, 0.52) + vec3(0.04, 0.07, 0.015) * thinness;
    vec3 petalSSS   = baseColor * vec3(1.48, 1.28, 0.92) + vec3(0.08, 0.04, 0.02);
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float cloudVis = (shadow > 0.001) ? Environment_CloudVisibility(u_cloudNoise, worldPosition) : 0.0;

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 1.1 * invH + 0.60 * invH * invH), 0.62, 1.0);

    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);

    float cupCavity = mix(0.58, 1.0, FastRamp(0.82, 0.98, hNorm));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    float sunFactor = canopyExtinction * cloudVis * shadow;
    if (sunFactor > 0.001) {
        vec3 halfDir = normalize(u_lightDir + viewDir);
        float NdotH = max(dot(faceNormal, halfDir), 0.0);
        float specLobeStem = FastPow32(NdotH);
        float specLobeBloom = FastPow16(NdotH);
        float specIntensity = mix(0.62 * (1.0 - antiShimmer * 0.75), 0.38, bloomMask) * canopyExtinction;
        float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

        float oneMinusNV = 1.0 - max(dot(faceNormal, viewDir), 0.0);
        float rim = (oneMinusNV * oneMinusNV * sqrt(oneMinusNV)) * mix(0.18, 0.26, bloomMask);

        vec3 directSunTerms = baseColor * (directDiffuse * 1.06 + rim)
                            + subsurfaceColor * (transmission * mix(0.72, 0.88, bloomMask))
                            + vec3(spec, spec, spec * 0.94);
        lit += directSunTerms * (effectiveLightColor * sunFactor);
    }

    lit += subsurfaceColor * (transmission * mix(0.16, 0.22, bloomMask)) * skyAmbient;

    float hCurve = hNorm / (0.70 + 0.30 * hNorm);
    float rootAO = clamp(0.76 + 0.24 * hCurve, 0.76, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// ============================================================================
// HIGH-DENSITY BLADE GRASS SHADING
// Seamless Root-to-Turf Grounding, 3D V-Crease Fold & Luminous Emerald SSS
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

    float u = clamp(uTransverse, -1.0, 1.0);
    float absU = abs(u);

    float fwU = fwidth(uTransverse);
    float fwH = fwidth(h);
    float unresolved = max(FastRamp(0.50, 1.70, fwU), FastRamp(0.15, 0.42, fwH));
    float tipConvergence = FastRamp(0.76, 0.98, h);
    float antiShimmer = max(max(unresolved, FastRamp(10.0, 30.0, distToCam)), tipConvergence * 0.68);

    // Smooth upward normal bias + 3D V-crease lateral fold across the blade width
    vec3 faceNormal = normalize(vec3(worldNormal.x, max(abs(worldNormal.y), 0.28), worldNormal.z));
    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float creaseStrength = 0.26 * (1.0 - antiShimmer * 0.88);
        faceNormal = normalize(faceNormal + bladeSide * (u * creaseStrength));
    }

    float archWindow = FastRamp(0.14, 0.44, h) * (1.0 - FastRamp(0.68, 0.88, h));

    // Align lower blade normal with terrain normal so root lighting matches ground_splat.fs seamlessly
    float groundBlend = (1.0 - FastRamp(0.0, 0.35, h)) * 0.45;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.84) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    float rib = 1.0 - FastRamp(0.0, 0.32, absU);
    baseColor *= 1.0 + (rib - 0.32) * 0.12 * (1.0 - antiShimmer);

    float macroPatch = sin(worldPosition.x * 0.14 + worldPosition.z * 0.09) * 0.55
                     + cos(worldPosition.x * -0.08 + worldPosition.z * 0.19) * 0.45;
    float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
    vec3 tipTone = mix(vec3(0.95, 1.02, 0.97), vec3(1.04, 1.04, 0.90), warmShift * FastRamp(0.35, 0.92, h));
    baseColor *= tipTone;

    // ── GENERALIZED 4-STATE BOTANICAL TRANSITION ─────────────────────────────
    vec2 grassState = GrassEvaluateLocalState(worldPosition);
    float dryness = grassState.x;
    float scorch  = grassState.y;
    float specScale = 1.0;
    float sssScale  = 1.0;
    vec3 emberGlow  = vec3(0.0);
    // Warm sunlit moss-emerald tone matching ground_splat.fs so grass roots NEVER look black!
    vec3 rootSubstrateTone = vec3(0.195, 0.310, 0.105);

    if (dryness > 0.001 || scorch > 0.001) {
        float wiltWeight = clamp(dryness * 1.85, 0.0, 1.0);
        float senescenceMask = clamp(wiltWeight * (0.58 + 0.52 * h + 0.22 * absU), 0.0, 1.0);
        vec3 witheredRoot = vec3(0.22, 0.24, 0.09);
        vec3 witheredTip  = vec3(0.72, 0.56, 0.17);
        vec3 witheredCol  = mix(witheredRoot, witheredTip, smoothstep(0.08, 0.82, h));
        baseColor = mix(baseColor, witheredCol, senescenceMask);
        rootSubstrateTone = mix(rootSubstrateTone, vec3(0.18, 0.19, 0.08), wiltWeight);

        float dryWeight = clamp((dryness - 0.45) * 1.818, 0.0, 1.0);
        if (dryWeight > 0.001) {
            vec3 strawRoot = vec3(0.28, 0.22, 0.13);
            vec3 strawBody = vec3(0.66, 0.55, 0.33);
            vec3 strawTip  = vec3(0.78, 0.68, 0.44);
            vec3 strawCol  = (h < 0.5) ? mix(strawRoot, strawBody, h * 2.0)
                                       : mix(strawBody, strawTip, (h - 0.5) * 2.0);
            baseColor = mix(baseColor, strawCol, dryWeight);
            rootSubstrateTone = mix(rootSubstrateTone, vec3(0.22, 0.17, 0.10), dryWeight);
        }

        specScale = mix(1.0, 0.42, dryness);
        sssScale  = mix(1.0, 0.58, dryness);

        if (scorch > 0.001) {
            float charMask = clamp(scorch * (0.88 + 0.35 * h + 0.15 * absU), 0.0, 1.0);
            vec3 carbonBlack = vec3(0.042, 0.039, 0.038);
            vec3 brittleAsh  = vec3(0.21, 0.20, 0.19);
            float ashTipMask = smoothstep(0.68, 0.96, h) * smoothstep(0.45, 0.95, scorch);
            vec3 charCol     = mix(carbonBlack, brittleAsh, ashTipMask * 0.65);

            baseColor = mix(baseColor, charCol, charMask);
            rootSubstrateTone = mix(rootSubstrateTone, carbonBlack, scorch);
            specScale *= (1.0 - charMask * 0.85);
            sssScale  *= (1.0 - charMask * 0.95);

            float emberStrength = u_grassStateParams.z;
            if (emberStrength > 0.001) {
                float frontierBand = smoothstep(0.12, 0.55, scorch) * (1.0 - smoothstep(0.62, 0.96, scorch));
                float singedTipEmber = smoothstep(0.58, 0.92, h) * smoothstep(0.35, 0.95, scorch);
                float emberMask = max(frontierBand * 1.15, singedTipEmber * 0.75);
                emberGlow = vec3(2.45, 0.52, 0.07) * (emberMask * emberStrength);
            }
        }
    }

    // Seamless Root-to-Ground Turf Color Blending
    float rootBlend = (1.0 - FastRamp(0.0, 0.32, h)) * 0.50;
    baseColor = mix(baseColor, rootSubstrateTone, rootBlend);

    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);

    // Balanced contact AO (0.70..1.0): gives natural depth without black root ink
    float rootAO = mix(0.70, 1.0, smoothstep(0.0, 0.48, h));
    vec3 lit = baseColor * ambient * rootAO;

    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.22 * archWindow * specScale * (1.0 - antiShimmer * 0.78));
    lit += waxSheen * rootAO;

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    if (shadow > 0.001) {
        float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
        if (sunVis > 0.001) {
            vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.82));
            float sunLuma = dot(effectiveLightColor, vec3(0.28, 0.58, 0.14));
            vec3 balancedSun = vec3(sunLuma) * vec3(1.05, 1.02, 0.90);
            effectiveLightColor = mix(effectiveLightColor, balancedSun, 0.38 * (1.0 - dryness));

            vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.36 * u_lightDir.y, u_lightDir.y * 0.72, u_lightDir.z - 0.28 * u_lightDir.y));
            float wrappedSun   = clamp((dot(n, u_lightDir) + 0.42) * 0.704225, 0.0, 1.0);
            float wrappedModel = clamp((dot(faceNormal, modelLightDir) + 0.38) * 0.724638, 0.0, 1.0);
            float zenithBlend  = FastRamp(0.50, 0.90, u_lightDir.y) * (1.0 - antiShimmer * 0.75);
            float wrapped = mix(wrappedSun, wrappedModel, zenithBlend * 0.65);

            // Soft Beer-Lambert attenuation that preserves sunlit warmth down into the tuft base
            float canopyDepth = 0.32 * (1.0 - h);
            float opticalDepth = canopyDepth * 0.75 / max(u_lightDir.y, 0.26);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.35 * opticalDepth * opticalDepth);
            float verticalSelfShadow = mix(0.68, 1.0, smoothstep(0.0, 0.52, h));
            float canopy = (0.42 + 0.58 * expCanopy) * verticalSelfShadow;

            vec3 anisoSpec = vec3(0.0);
            float waxySpec = 0.0;
            if (archWindow > 0.005) {
                vec3 halfDir = normalize(modelLightDir + viewDir);
                float NdotH = max(dot(faceNormal, halfDir), 0.0);
                float tDotHUnnorm = halfDir.y - faceNormal.y * NdotH;
                float shiftedTH = tDotHUnnorm - (h - 0.48) * 0.10;
                float invTangentLenSq = 1.0 / max(1.0 - faceNormal.y * faceNormal.y, 1e-4);
                float sinTHSq = clamp(1.0 - (shiftedTH * shiftedTH) * invTangentLenSq, 0.0, 1.0);

                float specAA = (1.0 - antiShimmer) * archWindow * specScale;
                float sinTH4  = sinTHSq * sinTHSq;
                float sinTH8  = sinTH4 * sinTH4;
                float sinTH16 = sinTH8 * sinTH8;

                vec3 primarySheen   = mix(baseColor, vec3(0.96, 1.0, 0.88), 0.50) * (sinTH8 * 0.062);
                vec3 secondaryGlint = vec3(0.96, 1.0, 0.90) * (sinTH16 * 0.10);
                anisoSpec = (primarySheen + secondaryGlint) * (specAA * max(NdotH + 0.30, 0.0));
                waxySpec = FastPow32(NdotH) * (0.12 * h) * specAA;
            }

            // Subsurface Translucency
            float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, modelLightDir), 0.0);
            float forwardLobe = forwardScatter * forwardScatter * forwardScatter;
            float backLobe = FastPow1_4(backLight);
            float edgeThinness = 0.68 + 0.52 * absU;
            float sssWindow = FastRamp(0.08, 0.50, h) * (1.0 - FastRamp(0.82, 0.99, h) * 0.40);
            float transmission = (backLobe * 0.80 + forwardLobe * 1.30 * (0.5 + 0.5 * h)) * sssWindow * edgeThinness * sssScale;
            vec3 emeraldSSS = baseColor * vec3(1.22, 1.88, 0.56) + vec3(0.025, 0.060, 0.016) * sssScale;

            float diffuseLobe = mix(wrapped * wrapped, wrapped, 0.45);
            vec3 directSunTerms = baseColor * (diffuseLobe * 0.95)
                                + anisoSpec + (effectiveLightColor * waxySpec)
                                + emeraldSSS * (transmission * 1.40);
            lit += directSunTerms * effectiveLightColor * (canopy * sunVis);
        }
    }

    lit += emberGlow;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
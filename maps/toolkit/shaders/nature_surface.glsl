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
    // Shading LOD: Smoothly fade dynamic shadows across 18m..30m to avoid a visible cascade line
    float shadowFade = 1.0 - FastRamp(18.0, 30.0, distToCam);
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

    // True early-out: Only sample static shadow map if dynamic shadow is not already fully occluded
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

    // Translucent grass blades transmit ~42% of direct sunlight through the turf canopy,
    // preventing a harsh dark/bright step at the dynamic shadow caster distance boundary.
    float translucentVis = mix(0.42, 1.0, rawCombined);
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
    const vec3 soilTone = vec3(0.16, 0.15, 0.08);
    float stemRootBlend = (1.0 - FastRamp(0.0, 0.22, hNorm)) * (1.0 - bloomMask) * 0.62;
    baseColor = mix(baseColor, soilTone, stemRootBlend);

    // Zenith irradiance compensation
    vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.78));

    // Wrapped Diffuse Lighting (with azimuthal modeling at Noon)
    vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.30 * u_lightDir.y, u_lightDir.y * 0.78, u_lightDir.z - 0.22 * u_lightDir.y));
    float wrap = mix(0.32, 0.42, bloomMask);
    float wrapped = clamp((dot(faceNormal, modelLightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    // Two-sided Subsurface Scattering (SSS) Transmission
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

    // Skip cloud noise texture fetch when already in full shadow
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float cloudVis = (shadow > 0.001) ? Environment_CloudVisibility(u_cloudNoise, worldPosition) : 0.0;

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 1.2 * invH + 0.72 * invH * invH), 0.56, 1.0);

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
    float rootAO = clamp(0.72 + 0.28 * hCurve, 0.72, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// ============================================================================
// HIGH-DENSITY BLADE GRASS SHADING
// Generalized 4-State Botanical Optics (Lush, Withered, Dry, Scorched + Embers)
// + 3D V-Crease Fold, Macro Botanical Harmony & 5-Layer Anti-Flicker Convergence
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

    // Division-free unresolved blade metric: widthPixels + vertical tip derivative
    float fwU = fwidth(uTransverse);
    float fwH = fwidth(h);
    float unresolved = max(FastRamp(0.50, 1.70, fwU), FastRamp(0.15, 0.42, fwH));
    float tipConvergence = FastRamp(0.76, 0.98, h);
    float antiShimmer = max(max(unresolved, FastRamp(10.0, 30.0, distToCam)), tipConvergence * 0.68);

    // Smooth upward normal bias + 3D V-crease lateral fold across the blade width
    // Restores physical midrib crease & bilateral sunlit/shaded contrast per blade
    vec3 faceNormal = normalize(vec3(worldNormal.x, max(abs(worldNormal.y), 0.26), worldNormal.z));
    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float creaseStrength = 0.18 * (1.0 - antiShimmer * 0.88);
        faceNormal = normalize(faceNormal + bladeSide * (u * creaseStrength));
    }

    // Arch Bell Window: concentrates specular & grazing sheen on the broad curved mid-upper arch
    // (h in [0.16, 0.86]) and tapers strictly to 0.0 at the narrow tip so tips NEVER flicker
    float archWindow = FastRamp(0.14, 0.44, h) * (1.0 - FastRamp(0.68, 0.88, h));

    float groundBlend = (1.0 - FastRamp(0.0, 0.30, h)) * 0.32;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.84) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // Soft central midrib highlight & subtle edge relief (filtered out when unresolved)
    float rib = 1.0 - FastRamp(0.0, 0.32, absU);
    baseColor *= 1.0 + (rib - 0.32) * 0.14 * (1.0 - antiShimmer);

    // Coherent macro botanical hue shift (subtle natural variation between deep viridian & vibrant spring green)
    float macroPatch = sin(worldPosition.x * 0.14 + worldPosition.z * 0.09) * 0.55
                     + cos(worldPosition.x * -0.08 + worldPosition.z * 0.19) * 0.45;
    float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
    vec3 tipTone = mix(vec3(0.94, 1.02, 0.98), vec3(1.01, 1.05, 0.94), warmShift * FastRamp(0.35, 0.92, h));
    baseColor *= tipTone;

    // ── GENERALIZED 4-STATE BOTANICAL TRANSITION ─────────────────────────────
    // Fast-paths when in default Lush Green state (dryness == 0 && scorch == 0)
    vec2 grassState = GrassEvaluateLocalState(worldPosition);
    float dryness = grassState.x;
    float scorch  = grassState.y;
    float specScale = 1.0;
    float sssScale  = 1.0;
    vec3 emberGlow  = vec3(0.0);
    // Matches ground_splat.fs turfBase so grass roots melt seamlessly into the terrain
    vec3 rootSubstrateTone = vec3(0.165, 0.270, 0.088);

    if (dryness > 0.001 || scorch > 0.001) {
        // 1. Lush Green -> Withered Yellow (0.0 .. 0.55):
        float wiltWeight = clamp(dryness * 1.85, 0.0, 1.0);
        float senescenceMask = clamp(wiltWeight * (0.58 + 0.52 * h + 0.22 * absU), 0.0, 1.0);
        vec3 witheredRoot = vec3(0.22, 0.24, 0.09);
        vec3 witheredTip  = vec3(0.72, 0.56, 0.17);
        vec3 witheredCol  = mix(witheredRoot, witheredTip, smoothstep(0.08, 0.82, h));
        baseColor = mix(baseColor, witheredCol, senescenceMask);
        rootSubstrateTone = mix(rootSubstrateTone, vec3(0.18, 0.19, 0.08), wiltWeight);

        // 2. Withered Yellow -> Parched Dry Straw (0.50 .. 1.0):
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

        // Adjust optical sheen & subsurface scattering for yellowing/dry states
        specScale = mix(1.0, 0.42, dryness);
        sssScale  = mix(1.0, 0.58, dryness);

        // 3. Fire-Scorched / Charred Black (scorch > 0.0):
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

            // Smoldering orange-red fire embers along the active burn frontier & singed tips
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
    float rootBlend = (1.0 - FastRamp(0.0, 0.34, h)) * 0.58;
    baseColor = mix(baseColor, rootSubstrateTone, rootBlend);

    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);

    // Grounded contact ambient occlusion: creates lush 3D tuft depth that anchors blades to the turf
    float rootAO = mix(0.55, 1.0, smoothstep(0.0, 0.56, h));
    vec3 lit = baseColor * ambient * rootAO;

    // Waxy cuticle grazing sheen (windowed by archWindow so tips never shimmer)
    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.22 * archWindow * specScale * (1.0 - antiShimmer * 0.78));
    lit += waxSheen * rootAO;

    // Shadow + conditional Cloud visibility (skips cloud texture lookup if shadowed)
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    if (shadow > 0.001) {
        float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
        if (sunVis > 0.001) {
            // Zenith irradiance compensation + warm-sun balance
            vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.82));
            float sunLuma = dot(effectiveLightColor, vec3(0.28, 0.58, 0.14));
            vec3 balancedSun = vec3(sunLuma) * vec3(1.05, 1.02, 0.90);
            effectiveLightColor = mix(effectiveLightColor, balancedSun, 0.38 * (1.0 - dryness));

            // Azimuthal blade modeling: preserves blade-to-blade relief even at 12:00 Noon
            vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.36 * u_lightDir.y, u_lightDir.y * 0.72, u_lightDir.z - 0.28 * u_lightDir.y));
            float wrappedSun   = clamp((dot(n, u_lightDir) + 0.40) * 0.714286, 0.0, 1.0);
            float wrappedModel = clamp((dot(faceNormal, modelLightDir) + 0.36) * 0.735294, 0.0, 1.0);
            float zenithBlend  = FastRamp(0.50, 0.90, u_lightDir.y) * (1.0 - antiShimmer * 0.75);
            float wrapped = mix(wrappedSun, wrappedModel, zenithBlend * 0.65);

            // Beer-Lambert attenuation + balanced tuft interior self-occlusion
            float canopyDepth = 0.44 * (1.0 - h);
            float opticalDepth = canopyDepth * 0.85 / max(u_lightDir.y, 0.22);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.40 * opticalDepth * opticalDepth);
            float verticalSelfShadow = mix(0.50, 1.0, smoothstep(0.0, 0.65, h));
            float canopy = (0.28 + 0.72 * expCanopy) * verticalSelfShadow;

            // Analytical Kajiya-Kay Anisotropic Specular on the curved mid-upper arch (archWindow)
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

            // Subsurface Translucency (fresh, vibrant emerald backlight transmission)
            float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, modelLightDir), 0.0);
            float forwardLobe = forwardScatter * forwardScatter * forwardScatter;
            float backLobe = FastPow1_4(backLight);
            float edgeThinness = 0.68 + 0.52 * absU;
            float sssWindow = FastRamp(0.10, 0.55, h) * (1.0 - FastRamp(0.82, 0.99, h) * 0.45);
            float transmission = (backLobe * 0.80 + forwardLobe * 1.35 * (0.5 + 0.5 * h)) * sssWindow * edgeThinness * sssScale;
            vec3 emeraldSSS = baseColor * vec3(1.15, 1.95, 0.65) + vec3(0.020, 0.065, 0.020) * sssScale;


            float diffuseLobe = mix(wrapped * wrapped, wrapped, 0.40);
            vec3 directSunTerms = baseColor * (diffuseLobe * 0.92)
                                + anisoSpec + (effectiveLightColor * waxySpec)
                                + emeraldSSS * (transmission * 1.45);
            lit += directSunTerms * effectiveLightColor * (canopy * sunVis);
        }
    }

    lit += emberGlow;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
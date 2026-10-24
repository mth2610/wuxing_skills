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
// SHADOW MAPPING (Self-Shadow Acne Free & Bandwidth-Optimized on Foliage)
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
    // Fade out foliage shadow-map reads smoothly by 20m (analytical canopy AO handles far turf)
    float shadowFade = 1.0 - FastRamp(12.5, 20.5, distToCam);
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
                float bias = clamp(0.0055 + slope * 0.0065, 0.0045, 0.0120);
                // Use 4-tap bilinear within 10m and fast 1-tap beyond 10m to cut close-zoom overdraw cost
                float sampleDyn = (distToCam < 10.0)
                    ? MapShadowCompareBilinear(shadowMap, projected.xy, projected.z - bias, u_shadowTexel)
                    : MapShadowCompare(shadowMap, projected.xy, projected.z - bias);
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
                float bias = clamp(0.0032 + slope * 0.0045, 0.0025, 0.0085);
                // Single-tap static shadow lookup on foliage saves 3 textureLod fetches per blade pixel
                float sampleStatic = MapShadowCompare(staticShadowMap, projected.xy, projected.z - bias);
                float rawStatic = mix(1.0, sampleStatic, edgeFade);
                rawCombined = min(rawDyn, rawStatic);
            }
        }
    }

    float translucentVis = mix(0.56, 1.0, rawCombined);
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
    float bloomMask = smoothstep(0.62, 0.88, hNorm);

    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Lift bloom & stem normals upward so 3D petals catch warm sky/sun irradiance evenly
    faceNormal.y = max(faceNormal.y, -0.10) + mix(0.28, 0.45, bloomMask);
    faceNormal = normalize(faceNormal);

    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.48 * (1.0 - bloomMask)));
    }

    float antiShimmer = FastRamp(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    float flattenFactor = antiShimmer * 0.85;
    float groundBlend = (1.0 - FastRamp(0.0, 0.25, hNorm)) * 0.38;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), totalTerrainBlend));

    const vec3 turfRootTone = vec3(0.15, 0.22, 0.08);
    float stemRootBlend = (1.0 - FastRamp(0.0, 0.24, hNorm)) * (1.0 - bloomMask) * 0.55;
    baseColor = mix(baseColor, turfRootTone, stemRootBlend);

    vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.78));

    vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.30 * u_lightDir.y, u_lightDir.y * 0.78, u_lightDir.z - 0.22 * u_lightDir.y));
    float wrap = mix(0.36, 0.58, bloomMask);
    float wrapped = clamp((dot(faceNormal, modelLightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    // Soften petal diffuse roll-off so 3D procedural blooms glow warmly from every viewing angle
    float directDiffuse = mix(wrapped * wrapped, wrapped * (0.45 + 0.55 * wrapped), bloomMask);

    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = FastPow1_4(backfaceSun) * 0.72 + FastPow2(viewSunAlign) * 0.65;

    float petalDiffusion = 0.38 + 0.62 * wrapped;
    transmissionAngle = mix(transmissionAngle, max(transmissionAngle, petalDiffusion * 0.85), bloomMask);
    float thinness = mix(0.24, 1.0, FastRamp(0.06, 0.62, hNorm));
    float transmission = transmissionAngle * thinness;

    vec3 foliageSSS = baseColor * vec3(1.55, 1.72, 0.50) + vec3(0.04, 0.07, 0.015) * thinness;
    vec3 petalSSS   = baseColor * vec3(1.52, 1.36, 1.05) + vec3(0.09, 0.06, 0.03);
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    float cloudVis = (shadow > 0.001) ? Environment_CloudVisibility(u_cloudNoise, worldPosition) : 0.0;

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 0.95 * invH + 0.50 * invH * invH), 0.68, 1.0);

    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);

    // Gentle cup cavity (0.78..1.0) preserves bright, readable flower centers
    float cupCavity = mix(0.78, 1.0, FastRamp(0.76, 0.96, hNorm));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.78 + 0.22 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    float sunFactor = canopyExtinction * cloudVis * shadow;
    if (sunFactor > 0.001) {
        vec3 halfDir = normalize(u_lightDir + viewDir);
        float NdotH = max(dot(faceNormal, halfDir), 0.0);
        float specLobeStem = FastPow32(NdotH);
        float specLobeBloom = FastPow16(NdotH);
        float specIntensity = mix(0.55 * (1.0 - antiShimmer * 0.75), 0.42, bloomMask) * canopyExtinction;
        float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

        float oneMinusNV = 1.0 - max(dot(faceNormal, viewDir), 0.0);
        float rim = (oneMinusNV * oneMinusNV * sqrt(oneMinusNV)) * mix(0.18, 0.30, bloomMask);

        vec3 directSunTerms = baseColor * (directDiffuse * 1.12 + rim)
                            + subsurfaceColor * (transmission * mix(0.74, 0.95, bloomMask))
                            + vec3(spec, spec, spec * 0.94);
        lit += directSunTerms * (effectiveLightColor * sunFactor);
    }

    lit += subsurfaceColor * (transmission * mix(0.18, 0.28, bloomMask)) * skyAmbient;

    float hCurve = hNorm / (0.68 + 0.32 * hNorm);
    float rootAO = clamp(0.78 + 0.22 * hCurve, 0.78, 1.0);
    lit *= mix(rootAO, 1.0, bloomMask);

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// ============================================================================
// AAA BLADE GRASS SHADING (Ghost of Tsushima & Where Winds Meet Inspired)
// Velvety Canopy Normal Cohesion, Sunlit Golden-Chartreuse Tips & Luminous SSS
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

    float fwU = fwidth(u);
    float fwH = fwidth(h);
    float unresolved = max(FastRamp(0.55, 1.80, fwU), FastRamp(0.16, 0.45, fwH));
    float tipConvergence = FastRamp(0.82, 0.99, h);
    float antiShimmer = max(max(unresolved, FastRamp(11.0, 30.0, distToCam)), tipConvergence * 0.75);

    // 1. Soft Cylindrical Blade Normal + Tsushima Canopy Upward Bias:
    // Biases the blade normal upward (0.48 minimum Y) and limits side-to-side twist so
    // neighboring blades in a clump form a cohesive velvety cushion rather than spiky shards.
    vec3 faceNormal = normalize(vec3(worldNormal.x * 0.72, max(abs(worldNormal.y), 0.48), worldNormal.z * 0.72));
    if (distToCam < 18.0) {
        vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
        float sideLenSq = dot(bladeSide, bladeSide);
        if (sideLenSq > 1e-6) {
            bladeSide *= inversesqrt(sideLenSq);
            float curvature = u * 0.19 * (1.0 - antiShimmer * 0.90);
            faceNormal = normalize(faceNormal + bladeSide * curvature);
        }
    }

    // Arch Bell Window: concentrates specular sheen on the curved mid-upper arch (h in [0.16, 0.86])
    float archWindow = FastRamp(0.16, 0.44, h) * (1.0 - FastRamp(0.68, 0.88, h));

    // Cohesive canopy normal `n`: blends at least 46% upward even at close zoom so light glides smoothly across tufts
    float groundBlend = (1.0 - FastRamp(0.0, 0.34, h)) * 0.52;
    float baseCanopySmooth = mix(0.46, 0.88, antiShimmer);
    float terrainBlend = 1.0 - (1.0 - baseCanopySmooth) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // Coherent macro botanical hue shift across the valley (emerald swales -> golden-chartreuse sunlit drifts)
    float macroPatch = sin(worldPosition.x * 0.12 + worldPosition.z * 0.08) * 0.55
                     + cos(worldPosition.x * -0.07 + worldPosition.z * 0.16) * 0.45;
    float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
    float tipHeightMask = FastRamp(0.32, 0.92, h);
    // Hài hòa sắc xanh ngọc bích Kiếm Hiệp (Tsushima Emerald & Morning Gold), tránh chói vàng bết màu
    vec3 coolEmeraldTip = vec3(0.96, 1.02, 0.94);
    vec3 warmGoldenTip  = vec3(1.08, 1.05, 0.88);
    vec3 tipTone = mix(vec3(1.0), mix(coolEmeraldTip, warmGoldenTip, warmShift), tipHeightMask);
    baseColor *= tipTone;

    // ── GENERALIZED 4-STATE BOTANICAL TRANSITION ─────────────────────────────
    vec2 grassState = GrassEvaluateLocalState(worldPosition);
    float dryness = grassState.x;
    float scorch  = grassState.y;
    float specScale = 1.0;
    float sssScale  = 1.0;
    vec3 emberGlow  = vec3(0.0);
    // Grounded olive-moss root tone matching ground_splat.fs turfBase
    vec3 rootSubstrateTone = vec3(0.145, 0.215, 0.076);

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
    float rootBlend = (1.0 - FastRamp(0.0, 0.28, h)) * 0.46;
    baseColor = mix(baseColor, rootSubstrateTone, rootBlend);

    vec3 skyAmbient = Environment_HemisphereIrradiance(vec3(0.0, 1.0, 0.0), u_ambientColor);
    vec3 groundBounce = Environment_HemisphereIrradiance(vec3(0.0, -1.0, 0.0), u_ambientColor);
    // Warm chlorophyll bounce in shadows keeps shadowed valleys lush and painterly
    vec3 greenBounce = baseColor * (u_ambientColor * 1.42 + vec3(0.040, 0.078, 0.020));
    vec3 ambientGround = mix(groundBounce, greenBounce, 0.50);
    vec3 ambient = mix(ambientGround, skyAmbient, n.y * 0.5 + 0.5);

    // [TỐI ƯU VISUAL]: Nới nhẹ rootAO tối thiểu (0.24 -> 0.46) để gốc cỏ bám đất chắc chắn mà không bị đen kịt
    float rootAO = mix(0.46, 1.0, smoothstep(0.0, 0.44, h));
    vec3 lit = baseColor * ambient * rootAO;

    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.07 * archWindow * specScale * (1.0 - antiShimmer * 0.85));
    lit += waxSheen * rootAO;

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir, worldPosition, distToCam);
    if (shadow > 0.001) {
        float sunVis = shadow * Environment_CloudVisibility(u_cloudNoise, worldPosition);
        if (sunVis > 0.001) {
            // Preserve warm morning sunlight chroma instead of desaturating toward gray
            vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(u_lightDir.y - 0.38, 0.0) * 0.80));

            // Use canopy-blended shading normal `shadingN` so blades in a clump share soft, velvety diffuse transitions
            vec3 shadingN = normalize(mix(faceNormal, n, 0.54));
            vec3 modelLightDir = normalize(vec3(u_lightDir.x + 0.32 * u_lightDir.y, u_lightDir.y * 0.76, u_lightDir.z - 0.24 * u_lightDir.y));
            float wrappedFace  = clamp((dot(shadingN, u_lightDir) + 0.42) * 0.704225, 0.0, 1.0);
            float wrappedModel = clamp((dot(shadingN, modelLightDir) + 0.42) * 0.704225, 0.0, 1.0);
            float zenithBlend  = FastRamp(0.50, 0.90, u_lightDir.y);
            float wrapped = mix(wrappedFace, wrappedModel, zenithBlend * 0.60);

            // Smooth Beer-Lambert canopy extinction
            float canopyDepth = 0.34 * (1.0 - h);
            float opticalDepth = canopyDepth * 0.75 / max(u_lightDir.y, 0.26);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.34 * opticalDepth * opticalDepth);
            float verticalSelfShadow = mix(0.72, 1.0, smoothstep(0.0, 0.50, h));
            float canopy = (0.44 + 0.56 * expCanopy) * verticalSelfShadow;

            vec3 anisoSpec = vec3(0.0);
            float waxySpec = 0.0;
            if (archWindow > 0.005) {
                vec3 halfDir = normalize(modelLightDir + viewDir);
                float NdotH = max(dot(faceNormal, halfDir), 0.0);
                float tDotHUnnorm = halfDir.y - faceNormal.y * NdotH;
                float shiftedTH = tDotHUnnorm - (h - 0.48) * 0.10;
                float invTangentLenSq = 1.0 / max(1.0 - faceNormal.y * faceNormal.y, 1e-4);
                float sinTHSq = clamp(1.0 - (shiftedTH * shiftedTH) * invTangentLenSq, 0.0, 1.0);

                float distRoughness = FastRamp(8.0, 22.0, distToCam);
                float specAA = (1.0 - antiShimmer) * archWindow * specScale;
                float sinTH4  = sinTHSq * sinTHSq;
                float sinTH8  = sinTH4 * sinTH4;
                float sinTH16 = sinTH8 * sinTH8;
                float sinTHBroad = mix(sinTH16, sinTH4, distRoughness);

                vec3 primarySheen   = mix(baseColor, vec3(1.00, 0.98, 0.84), 0.52) * (sinTH8 * 0.045);
                vec3 secondaryGlint = mix(baseColor, vec3(1.00, 0.98, 0.88), 0.40) * (sinTHBroad * 0.032 * (1.0 - distRoughness * 0.78));
                anisoSpec = (primarySheen + secondaryGlint) * (specAA * max(NdotH + 0.25, 0.0));
                waxySpec = FastPow32(NdotH) * (0.038 * h) * specAA * (1.0 - distRoughness);
            }

            // Luminous Golden-Jade Subsurface Translucency (Ghost of Tsushima Morning Glow):
            // Thin-leaf isotropic internal scattering + forward specular peak
            float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, modelLightDir), 0.0);
            float backWrap = max(-dot(shadingN, u_lightDir) + 0.38, 0.0) * 0.72;
            float forwardLobe = forwardScatter * forwardScatter * forwardScatter;
            float backLobe = FastPow1_4(backLight);
            float edgeThinness = 0.68 + 0.32 * absU;
            float sssWindow = FastRamp(0.08, 0.44, h) * (1.0 - FastRamp(0.86, 1.0, h) * 0.28);
            float transmission = (backLobe * 0.65 + backWrap * 0.45 + forwardLobe * 1.15 * (0.35 + 0.65 * h)) * sssWindow * edgeThinness * sssScale;
            vec3 goldenJadeSSS = mix(baseColor * vec3(1.72, 1.88, 0.62), vec3(0.98, 1.05, 0.45) * (dot(baseColor, vec3(0.333)) * 1.85), 0.38) + vec3(0.045, 0.058, 0.012) * sssScale;

            float diffuseLobe = wrapped * (0.42 + 0.58 * wrapped);
            vec3 directDiffuse = baseColor * (diffuseLobe * 0.62);
            vec3 jadeTrans = goldenJadeSSS * (transmission * 0.95);
            vec3 specHighlight = anisoSpec + vec3(waxySpec * 0.36) * baseColor;

            vec3 directSun = (directDiffuse + jadeTrans) * (canopy * sunVis) + specHighlight * sunVis;
            lit += directSun * effectiveLightColor;
        }
    }

    lit += emberGlow;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
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
    // Fade out foliage shadow-map reads smoothly by 22m (analytical canopy AO handles far turf)
    float shadowFade = 1.0 - FastRamp(14.0, 22.0, distToCam);
    if (shadowFade <= 0.0)
        return 1.0;

    float slope = 1.0 - max(dot(normal, lightDir), 0.0);

    float rawDyn = 1.0;
    if (u_shadowEnabled > 0.5) {
        float invW = 1.0 / max(lightSpace.w, 1e-5);
        vec3 projected = (lightSpace.xyz * invW) * 0.5 + 0.5;
        if (all(greaterThan(projected, vec3(0.0))) && all(lessThan(projected, vec3(1.0)))) {
            float edgeFade = MapShadowCoverageFadeWidth(projected.xy, 0.16);
            if (edgeFade > 0.0) {
                float bias = clamp(0.0012 + slope * 0.0020, 0.0010, 0.0034);
                // Use 4-tap bilinear within 12m and fast 1-tap beyond 12m to cut close-zoom overdraw cost
                float sampleDyn = (distToCam < 12.0)
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
                float bias = clamp(0.0010 + slope * 0.0018, 0.0008, 0.0028);
                // Single-tap static shadow lookup on foliage saves 3 textureLod fetches per blade pixel
                float sampleStatic = MapShadowCompare(staticShadowMap, projected.xy, projected.z - bias);
                float rawStatic = mix(1.0, sampleStatic, edgeFade);
                rawCombined = min(rawDyn, rawStatic);
            }
        }
    }

    float translucentVis = mix(0.20, 1.0, rawCombined);
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

    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float bloomMask = smoothstep(0.62, 0.88, hNorm);

    vec3 geometricNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-8));
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Preserve sculpted 3D petal cupping normals while gently lifting stem bases
    faceNormal.y = max(faceNormal.y, -0.10) + (1.0 - bloomMask) * 0.28;
    faceNormal *= inversesqrt(max(dot(faceNormal, faceNormal), 1e-8));

    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float u = clamp(uTransverse, -1.0, 1.0);
        vec3 tiltedN = faceNormal + bladeSide * (u * 0.48 * (1.0 - bloomMask));
        faceNormal = tiltedN * inversesqrt(max(dot(tiltedN, tiltedN), 1e-8));
    }

    float antiShimmer = FastRamp(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    float flattenFactor = antiShimmer * 0.85;
    float groundBlend = (1.0 - FastRamp(0.0, 0.25, hNorm)) * 0.38;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 mixedN = mix(faceNormal, vec3(0.0, 1.0, 0.0), totalTerrainBlend);
    vec3 n = mixedN * inversesqrt(max(dot(mixedN, mixedN), 1e-8));

    const vec3 turfRootTone = vec3(0.15, 0.22, 0.08);
    float stemRootBlend = (1.0 - FastRamp(0.0, 0.24, hNorm)) * (1.0 - bloomMask) * 0.55;
    baseColor = mix(baseColor, turfRootTone, stemRootBlend);

    vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(lightDir.y - 0.38, 0.0) * 0.78));

    vec3 rawModelL = vec3(lightDir.x + 0.30 * lightDir.y, lightDir.y * 0.78, lightDir.z - 0.22 * lightDir.y);
    vec3 modelLightDir = rawModelL * inversesqrt(max(dot(rawModelL, rawModelL), 1e-8));
    float wrap = mix(0.36, 0.58, bloomMask);
    float wrapped = clamp((dot(faceNormal, modelLightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    // Soften petal diffuse roll-off so 3D procedural blooms glow warmly from every viewing angle
    float directDiffuse = mix(wrapped * wrapped, wrapped * (0.45 + 0.55 * wrapped), bloomMask);

    float backfaceSun = max(-dot(faceNormal, lightDir), 0.0);
    float viewSunAlign = max(dot(-lightDir, viewDir), 0.0);
    float transmissionAngle = FastPow1_4(backfaceSun) * 0.72 + FastPow2(viewSunAlign) * 0.65;

    float petalDiffusion = 0.38 + 0.62 * wrapped;
    transmissionAngle = mix(transmissionAngle, max(transmissionAngle, petalDiffusion * 0.85), bloomMask);
    float thinness = mix(0.24, 1.0, FastRamp(0.06, 0.62, hNorm));
    float transmission = transmissionAngle * thinness;

    vec3 foliageSSS = baseColor * vec3(1.55, 1.72, 0.50) + vec3(0.04, 0.07, 0.015) * thinness;
    vec3 petalSSS   = baseColor * vec3(1.52, 1.36, 1.05) + vec3(0.09, 0.06, 0.03);
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, lightDir, worldPosition, distToCam);
    float cloudVis = (shadow > 0.001) ? Environment_CloudVisibility(u_cloudNoise, worldPosition) : 0.0;

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 0.95 * invH + 0.50 * invH * invH), 0.68, 1.0);

    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = clamp(u_ambientColor * vec3(1.25, 1.25, 1.35), 0.0, 1.0);
    vec3 groundBounce = clamp(u_ambientColor * vec3(0.55, 0.45, 0.40), 0.0, 1.0);

    // Gentle cup cavity (0.78..1.0) preserves bright, readable flower centers
    float cupCavity = mix(0.78, 1.0, FastRamp(0.76, 0.96, hNorm));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.78 + 0.22 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    float sunFactor = canopyExtinction * cloudVis * shadow;
    if (sunFactor > 0.001) {
        // [SPIR-V FIX]: Chuẩn hóa an toàn cho halfDir tránh lỗi chia 0 (NaN) khi viewDir == -lightDir
        vec3 halfSum = lightDir + viewDir;
        vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
        float NdotH = max(dot(faceNormal, halfDir), 0.0);
        float specLobeStem = FastPow32(NdotH);
        float specLobeBloom = FastPow16(NdotH);
        float specIntensity = mix(0.55 * (1.0 - antiShimmer * 0.75), 0.42, bloomMask) * canopyExtinction;
        float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

        float oneMinusNV = clamp(1.0 - max(dot(faceNormal, viewDir), 0.0), 0.0, 1.0);
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
                vec4 lightSpace, vec4 staticLightSpace, float macroPatch)
{
    float h = clamp(heightAlongPlant, 0.0, 1.0);

    vec3 toCam = u_viewPos - worldPosition;
    float distSq = dot(toCam, toCam);
    float invDist = inversesqrt(max(distSq, 1e-6));
    vec3 viewDir = toCam * invDist;
    float distToCam = distSq * invDist;

    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    float u = clamp(uTransverse, -1.0, 1.0);
    float absU = abs(u);

    // [VULKAN / SPIR-V FIX]: Tính đạo hàm fwidth(u) và fwidth(h) trong luồng đồng nhất (uniform control flow)
    // TRƯỚC nhánh if (distToCam >= 30.0) để tránh lỗi đạo hàm quad không đồng nhất tại ranh giới 30m.
    float fwU = fwidth(u);
    float fwH = fwidth(h);
    float antiShimmer;
    if (distToCam >= 30.0) {
        antiShimmer = 1.0;
    } else {
        float unresolved = max(FastRamp(0.55, 1.80, fwU), FastRamp(0.16, 0.45, fwH));
        float tipConvergence = FastRamp(0.82, 0.99, h);
        antiShimmer = max(max(unresolved, FastRamp(11.0, 30.0, distToCam)), tipConvergence * 0.75);
    }

    // 1. Soft Cylindrical Blade Normal + Tsushima Canopy Upward Bias:
    // Biases the blade normal upward (0.48 minimum Y) and limits side-to-side twist so
    // neighboring blades in a clump form a cohesive velvety cushion rather than spiky shards.
    vec3 rawFaceN = vec3(worldNormal.x * 0.72, max(abs(worldNormal.y), 0.48), worldNormal.z * 0.72);
    vec3 faceNormal = rawFaceN * inversesqrt(max(dot(rawFaceN, rawFaceN), 1e-8));
    if (distToCam < 18.0) {
        vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
        float sideLenSq = dot(bladeSide, bladeSide);
        if (sideLenSq > 1e-6) {
            bladeSide *= inversesqrt(sideLenSq);
            float curvature = u * 0.19 * (1.0 - antiShimmer * 0.90);
            vec3 curvedN = faceNormal + bladeSide * curvature;
            faceNormal = curvedN * inversesqrt(max(dot(curvedN, curvedN), 1e-8));
        }
    }

    // Arch Bell Window: concentrates specular sheen on the curved mid-upper arch (h in [0.16, 0.86])
    float archWindow = FastRamp(0.16, 0.44, h) * (1.0 - FastRamp(0.68, 0.88, h));

    // Cohesive canopy normal `n`: blends at least 46% upward even at close zoom so light glides smoothly across tufts
    float groundBlend = (1.0 - FastRamp(0.0, 0.34, h)) * 0.52;
    float baseCanopySmooth = mix(0.46, 0.88, antiShimmer);
    float terrainBlend = 1.0 - (1.0 - baseCanopySmooth) * (1.0 - groundBlend);
    vec3 rawCanopyN = mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend);
    vec3 n = rawCanopyN * inversesqrt(max(dot(rawCanopyN, rawCanopyN), 1e-8));

    // Coherent macro botanical hue shift across the valley (interpolated from vertex stage)
    float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
    float tipHeightMask = FastRamp(0.32, 0.92, h);
    // Hài hòa sắc xanh ngọc bích Kiếm Hiệp (Tsushima Emerald & Morning Gold), tránh chói vàng bết màu
    vec3 coolEmeraldTip = vec3(0.96, 1.02, 0.94);
    vec3 warmGoldenTip  = vec3(1.08, 1.05, 0.88);
    vec3 tipTone = mix(vec3(1.0), mix(coolEmeraldTip, warmGoldenTip, warmShift), tipHeightMask);
    baseColor *= tipTone;

    // ── GENERALIZED 4-STATE BOTANICAL TRANSITION ─────────────────────────────
    vec2 grassState = ((u_grassStateParams.x + u_grassStateParams.y) > 0.0001 || u_grassBurnZoneCount > 0)
        ? GrassEvaluateLocalState(worldPosition)
        : vec2(0.0);
    float dryness = grassState.x;
    float scorch  = grassState.y;
    float specScale = 1.0;
    float sssScale  = 1.0;
    vec3 emberGlow  = vec3(0.0);
    // Grounded olive-moss root tone matching ground_splat.fs turfBase
    vec3 rootSubstrateTone = vec3(0.155, 0.228, 0.086);

    // Subtle botanical leaf-vein & rounded-edge modulation at close zoom (< 9m)
    if (distSq < 81.0) {
        float closeDetail = (1.0 - distSq * (1.0 / 81.0)) * (1.0 - antiShimmer);
        float vein = 1.0 - 0.065 * abs(sin(u * 9.424778)) * closeDetail;
        float edgeRoll = 1.0 - 0.085 * (u * u) * closeDetail;
        baseColor *= vein * edgeRoll;
    }

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

    // Seamless Root-to-Ground Turf Color Blending + Distant Root Lift (prevents dark root stains without bleaching roots)
    float distRootLift = FastRamp(10.0, 28.0, distToCam);
    float rootBlend = (1.0 - FastRamp(0.0, 0.24, h)) * mix(0.20, 0.04, distRootLift);
    baseColor = mix(baseColor, rootSubstrateTone, rootBlend);
    vec3 distantCanopyTone = mix(vec3(0.24, 0.42, 0.15), vec3(0.34, 0.52, 0.19), warmShift * 0.65);
    baseColor = mix(baseColor, distantCanopyTone, distRootLift * (1.0 - h) * 0.36);

    vec3 skyAmbient = clamp(u_ambientColor * vec3(1.25, 1.25, 1.35), 0.0, 1.0);
    vec3 groundBounce = clamp(u_ambientColor * vec3(0.55, 0.45, 0.40), 0.0, 1.0);
    // Warm chlorophyll bounce in shadows keeps shadowed valleys lush and painterly
    vec3 greenBounce = baseColor * (u_ambientColor * 1.18 + vec3(0.026, 0.052, 0.016));
    vec3 ambientGround = mix(groundBounce, greenBounce, 0.48);
    vec3 ambient = mix(ambientGround, skyAmbient, n.y * 0.5 + 0.5);

    // Natural 3D root-to-tip self-occlusion so blades have lush depth at noon, sunset, and night
    float rootAO = mix(mix(0.74, 0.90, distRootLift), 1.0, smoothstep(0.0, 0.38, h));
    vec3 lit = baseColor * ambient * rootAO;

    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.055 * archWindow * specScale * (1.0 - antiShimmer * 0.85));
    lit += waxSheen * rootAO;

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, lightDir, worldPosition, distToCam);
    // Grounded ambient occlusion inside cast shadows so flower, grass, and rock shadows read clearly
    lit *= mix(0.72, 1.0, shadow);
    if (shadow > 0.001) {
        // [VISUAL FIX]: Pha mượt bóng mây từ 24m -> 30m thay vì cắt bậc đột ngột về 0.92 tại 30m
        float cloudVis = 0.92;
        if (distSq < 900.0) {
            float cloudDistFade = FastRamp(576.0, 900.0, distSq);
            cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, worldPosition), 0.92, cloudDistFade);
        }
        float sunVis = shadow * cloudVis;
        if (sunVis > 0.001) {
            // Noon energy conservation + horizon grazing envelope (keeps 12:00 unbleached and 17:34 coupled to terrain)
            vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(lightDir.y - 0.32, 0.0) * 1.18));
            float horizonEnvelope = smoothstep(0.02, 0.38, max(lightDir.y, 0.0)) * 0.66 + 0.34;

            // Use canopy-blended shading normal `shadingN` so blades in a clump share soft, velvety diffuse transitions
            float lowSunMacroBlend = 1.0 - smoothstep(0.08, 0.36, max(lightDir.y, 0.0));
            float shadingNWeight = mix(0.58, 0.82, lowSunMacroBlend);
            vec3 rawShadingN = mix(faceNormal, n, shadingNWeight);
            vec3 shadingN = rawShadingN * inversesqrt(max(dot(rawShadingN, rawShadingN), 1e-8));
            vec3 rawModelL = vec3(lightDir.x + 0.32 * lightDir.y, lightDir.y * 0.76, lightDir.z - 0.24 * lightDir.y);
            vec3 modelLightDir = rawModelL * inversesqrt(max(dot(rawModelL, rawModelL), 1e-8));
            float wrappedFace  = clamp((dot(shadingN, lightDir) + 0.38) * 0.7246, 0.0, 1.0);
            float wrappedModel = clamp((dot(shadingN, modelLightDir) + 0.38) * 0.7246, 0.0, 1.0);
            float zenithBlend  = FastRamp(0.50, 0.90, lightDir.y) * 0.60;
            float wrapped = mix(wrappedFace, wrappedModel, zenithBlend);

            // Smooth Beer-Lambert canopy extinction harmonized with ground_splat.fs luminance
            float canopyDepth = 0.34 * (1.0 - h) * (1.0 - distRootLift * 0.55);
            float opticalDepth = canopyDepth * 0.75 / max(lightDir.y, 0.28);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.35 * opticalDepth * opticalDepth);
            float verticalSelfShadow = mix(mix(0.72, 0.90, distRootLift), 1.0, smoothstep(0.0, 0.46, h));
            float canopy = (mix(0.52, 0.76, distRootLift) + mix(0.48, 0.24, distRootLift) * expCanopy) * verticalSelfShadow;

            vec3 anisoSpec = vec3(0.0);
            float waxySpec = 0.0;
            if (archWindow > 0.005 && distToCam < 22.0) {
                // [SPIR-V FIX]: Chuẩn hóa an toàn cho halfDir
                vec3 halfSum = modelLightDir + viewDir;
                vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
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

                vec3 primarySheen   = mix(baseColor, vec3(0.96, 0.98, 0.86), 0.46) * (sinTH8 * 0.044);
                vec3 secondaryGlint = mix(baseColor, vec3(0.96, 0.98, 0.88), 0.38) * (sinTHBroad * 0.028 * (1.0 - distRoughness * 0.75));
                anisoSpec = (primarySheen + secondaryGlint) * (specAA * max(NdotH + 0.20, 0.0));
                waxySpec = FastPow32(NdotH) * (0.032 * h) * specAA * (1.0 - distRoughness);
            }

            // Luminous Chlorophyll Emerald-Jade Subsurface Translucency:
            // Keeps G > R even under warm golden-hour / sunset light so blades never turn mustard-olive
            float forwardScatter = max(dot(-lightDir, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, modelLightDir), 0.0);
            float forwardLobe = forwardScatter * forwardScatter;
            float backLobe = FastPow2(backLight) * (0.35 + 0.65 * forwardScatter);
            float edgeThinness = 0.66 + 0.34 * absU;
            float sssWindow = FastRamp(0.18, 0.56, h) * (1.0 - FastRamp(0.86, 1.0, h) * 0.25);
            float transmission = (backLobe * 0.44 + forwardLobe * 0.64 * (0.30 + 0.70 * h)) * sssWindow * edgeThinness * sssScale;
            vec3 goldenJadeSSS = baseColor * vec3(0.94, 1.38, 0.64) + vec3(0.015, 0.034, 0.008) * sssScale;

            float diffuseLobe = wrapped * (0.48 + 0.52 * wrapped);
            vec3 directDiffuse = baseColor * (diffuseLobe * 0.74);
            vec3 jadeTrans = goldenJadeSSS * (transmission * 0.88);
            vec3 specHighlight = anisoSpec + vec3(waxySpec * 0.34) * baseColor;

            vec3 directSun = ((directDiffuse + jadeTrans) * canopy + specHighlight) * (sunVis * horizonEnvelope);
            lit += directSun * effectiveLightColor;
        }
    }

    lit += emberGlow;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

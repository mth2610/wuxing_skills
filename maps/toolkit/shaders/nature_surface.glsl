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
    // [NÂNG CẤP ĐỔ BÓNG HOA & CỎ]: Mở rộng tầm nhìn bóng đổ thực từ 14..22m lên 18..28m
    // để toàn bộ thảm hoa tiền cảnh và trung cảnh đều nhận bóng đổ sắc nét.
    float shadowFade = 1.0 - FastRamp(18.0, 28.0, distToCam);
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
                float bias = clamp(0.0011 + slope * 0.0019, 0.0009, 0.0032);
                float sampleDyn = (distToCam < 14.0)
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
                float sampleStatic = MapShadowCompare(staticShadowMap, projected.xy, projected.z - bias);
                float rawStatic = mix(1.0, sampleStatic, edgeFade);
                rawCombined = min(rawDyn, rawStatic);
            }
        }
    }

    // Tăng độ tương phản bóng đổ (0.12 thay vì 0.20) để bóng trên khóm hoa và thảm cỏ có chiều sâu rõ rệt
    float translucentVis = mix(0.12, 1.0, rawCombined);
    return mix(1.0, translucentVis, shadowFade);
}

// ============================================================================
// GENERAL FOLIAGE, STEMS, REEDS & 3D WILDFLOWER BLOOMS SHADING
// Dual-Sided Botanical SSS, Vein Translucency, Calyx Self-Shadow & Real-Time PCF
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
    float bloomMask = smoothstep(0.58, 0.84, hNorm);
    float u = clamp(uTransverse, -1.0, 1.0);
    float absU = abs(u);

    vec3 geometricNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-8));
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Giữ độ khum 3D của cánh hoa đồng thời nâng nhẹ pháp tuyến cuống hoa
    faceNormal.y = max(faceNormal.y, -0.18) + (1.0 - bloomMask) * 0.26;
    faceNormal *= inversesqrt(max(dot(faceNormal, faceNormal), 1e-8));

    // Uốn cong pháp tuyến ngang cho cả lá/cuống (0.48) và cánh hoa 3D (0.28) để cánh hoa bắt sáng khối tròn 3D
    vec3 bladeSide = vec3(-faceNormal.z, 0.0, faceNormal.x);
    float sideLenSq = dot(bladeSide, bladeSide);
    if (sideLenSq > 1e-6) {
        bladeSide *= inversesqrt(sideLenSq);
        float curveStrength = mix(0.48, 0.28, bloomMask);
        vec3 tiltedN = faceNormal + bladeSide * (u * curveStrength);
        faceNormal = tiltedN * inversesqrt(max(dot(tiltedN, tiltedN), 1e-8));
    }

    // Bản đồ gân cánh hoa & độ mỏng màng sinh học (Vein & Membrane Translucency Map)
    float centerRib = exp(-absU * 5.8) * (1.0 - FastRamp(0.75, 1.0, hNorm) * 0.55);
    float sideVeins = (distSq < 324.0) ? (abs(sin(u * 18.84955 - hNorm * 9.0)) * 0.16 * bloomMask) : 0.0;
    float veinMask  = clamp(centerRib * 0.45 + sideVeins, 0.0, 0.65);

    // Làm đậm nhẹ sắc độ dọc theo gân chính của cánh hoa để thấy rõ cấu trúc cánh khi nhìn gần
    baseColor *= 1.0 - veinMask * mix(0.06, 0.16, bloomMask);

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

    // [ĐỔ BÓNG TỰ THÂN ĐÀI HOA (CALYX & COROLLA SELF-SHADOWING)]:
    // Mặt cánh hoa quay lưng về phía Mặt Trời sẽ tự đổ bóng nhẹ vào lòng đài hoa,
    // tạo độ tương phản rõ rệt giữa mặt đón nắng và mặt khuất nắng của bông hoa 3D!
    float rawNdotL = dot(faceNormal, modelLightDir);
    float wrap = mix(0.32, 0.42, bloomMask);
    float wrapped = clamp((rawNdotL + wrap) / (1.0 + wrap), 0.0, 1.0);
    float calyxSelfShadow = mix(1.0, smoothstep(-0.35, 0.25, rawNdotL) * 0.55 + 0.45, bloomMask);
    float directDiffuse = (wrapped * (0.55 + 0.45 * wrapped)) * calyxSelfShadow;

    // [XUYÊN SÁNG & TÁN XẠ NGƯỢC NẮNG 2 THÀNH PHẦN (BOTANICAL DUAL-LOBE SSS)]:
    // 1. Truyền sáng qua mặt lưng khi nắng chiếu vào phía sau cánh hoa/lá (Back-lit Transmission)
    float backfaceSun = clamp((dot(-faceNormal, lightDir) + 0.22) * 0.8196, 0.0, 1.0);
    // 2. Tán xạ Mie hướng thẳng về mắt khi nhìn ngược về phía Mặt Trời (Forward Phase Translucency)
    vec3 distortedSunDir = normalize(lightDir + faceNormal * 0.36);
    float viewSunAlign = clamp(dot(viewDir, -distortedSunDir), 0.0, 1.0);
    float forwardMie = FastPow4(viewSunAlign) * 1.85 + FastPow2(viewSunAlign) * 0.55;

    // Độ mỏng màng sinh học: Mép cánh (absU -> 1) và đầu cánh (hNorm -> 1) mỏng thấu quang mạnh,
    // trong khi gân hoa (veinMask) và đài hoa dày hơn nên giữ lại đường gân sẫm màu khi ngược nắng!
    float edgeThinness = mix(0.35, 1.0, FastRamp(0.08, 0.72, hNorm)) * (0.72 + 0.28 * absU);
    float membraneThinness = edgeThinness * (1.0 - veinMask * mix(0.25, 0.62, bloomMask));
    float transmission = (backfaceSun * 0.82 + forwardMie * (0.35 + 0.65 * backfaceSun)) * membraneThinness;

    // Sắc độ xuyên sáng rực rỡ: Lá xanh ngọc lục bảo, Cánh hoa bừng sáng sắc huyết ngọc / kim quang ấm
    vec3 foliageSSS = baseColor * vec3(1.48, 1.75, 0.52) + vec3(0.03, 0.06, 0.01) * membraneThinness;
    vec3 petalSSS   = baseColor * vec3(1.68, 1.42, 1.08) + vec3(0.11, 0.06, 0.03) * membraneThinness;
    vec3 subsurfaceColor = mix(foliageSSS, petalSSS, bloomMask);

    // [ĐỔ BÓNG THỜI GIAN THỰC (SHADOW MAP + CLOUD SHADOW)]:
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, lightDir, worldPosition, distToCam);
    float cloudVis = 0.92;
    if (shadow > 0.001 && distSq < 900.0) {
        float cloudDistFade = FastRamp(576.0, 900.0, distSq);
        cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, worldPosition), 0.92, cloudDistFade);
    }

    float invH = 1.0 - hNorm;
    float canopyExtinction = clamp(1.0 / (1.0 + 0.95 * invH + 0.50 * invH * invH), 0.62, 1.0);

    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = clamp(u_ambientColor * vec3(1.25, 1.25, 1.35), 0.0, 1.0);
    vec3 groundBounce = clamp(u_ambientColor * vec3(0.55, 0.45, 0.40), 0.0, 1.0);

    // Tạo độ sâu lòng đài hoa (Cup Cavity Ambient Occlusion)
    float cupCavity = mix(0.66, 1.0, FastRamp(0.64, 0.94, hNorm)) * (1.0 - centerRib * 0.14 * bloomMask);
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.76 + 0.24 * max(n.y, 0.0);
    vec3 lit = baseColor * ambientFloor * horizon;

    // [FIX QUAN TRỌNG]: Hạ độ sáng môi trường khi hoa/lá nằm trong vùng bóng đổ (shadow < 1.0)
    // để bóng của núi đá, cây cối và khóm hoa đổ lên nhau hiện rõ ràng!
    lit *= mix(0.60, 1.0, shadow) * mix(0.82, 1.0, calyxSelfShadow);

    float sunFactor = canopyExtinction * cloudVis * shadow;
    if (sunFactor > 0.001) {
        vec3 halfSum = lightDir + viewDir;
        vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
        float NdotH = max(dot(faceNormal, halfDir), 0.0);
        float specLobeStem = FastPow32(NdotH);
        float specLobeBloom = FastPow16(NdotH);
        float specIntensity = mix(0.50 * (1.0 - antiShimmer * 0.75), 0.38, bloomMask) * canopyExtinction * calyxSelfShadow;
        float spec = mix(specLobeStem, specLobeBloom, bloomMask) * specIntensity;

        // Viền kim quang ngược nắng (Fresnel Backlit Rim Scattering)
        float oneMinusNV = clamp(1.0 - max(dot(faceNormal, viewDir), 0.0), 0.0, 1.0);
        float rimAlign = 0.30 + 0.70 * viewSunAlign;
        float rim = (oneMinusNV * oneMinusNV * sqrt(oneMinusNV)) * mix(0.20, 0.46, bloomMask) * rimAlign;

        vec3 directSunTerms = baseColor * (directDiffuse * 1.10)
                            + subsurfaceColor * (transmission * mix(0.85, 1.28, bloomMask) + rim)
                            + vec3(spec, spec, spec * 0.94);
        lit += directSunTerms * (effectiveLightColor * sunFactor);
    }

    // Xuyên sáng môi trường bầu trời cũng chịu ảnh hưởng của bóng đổ để không làm bệt vùng tối
    lit += subsurfaceColor * (transmission * mix(0.14, 0.22, bloomMask)) * skyAmbient * mix(0.35, 1.0, shadow);

    float hCurve = hNorm / (0.68 + 0.32 * hNorm);
    float rootAO = clamp(0.72 + 0.28 * hCurve, 0.72, 1.0);
    lit *= mix(rootAO, 1.0, bloomMask);

    if (distSq < 1225.0) {
        lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    }
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

    float archWindow = FastRamp(0.16, 0.44, h) * (1.0 - FastRamp(0.68, 0.88, h));

    float groundBlend = (1.0 - FastRamp(0.0, 0.34, h)) * 0.52;
    float baseCanopySmooth = mix(0.46, 0.88, antiShimmer);
    float terrainBlend = 1.0 - (1.0 - baseCanopySmooth) * (1.0 - groundBlend);
    vec3 rawCanopyN = mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend);
    vec3 n = rawCanopyN * inversesqrt(max(dot(rawCanopyN, rawCanopyN), 1e-8));

    float warmShift = clamp(macroPatch * 0.5 + 0.5, 0.0, 1.0);
    float tipHeightMask = FastRamp(0.32, 0.92, h);
    vec3 coolEmeraldTip = vec3(0.96, 1.02, 0.94);
    vec3 warmGoldenTip  = vec3(1.08, 1.05, 0.88);
    vec3 tipTone = mix(vec3(1.0), mix(coolEmeraldTip, warmGoldenTip, warmShift), tipHeightMask);
    baseColor *= tipTone;

    vec2 grassState = ((u_grassStateParams.x + u_grassStateParams.y) > 0.0001 || u_grassBurnZoneCount > 0)
        ? GrassEvaluateLocalState(worldPosition)
        : vec2(0.0);
    float dryness = grassState.x;
    float scorch  = grassState.y;
    float specScale = 1.0;
    float sssScale  = 1.0;
    vec3 emberGlow  = vec3(0.0);
    vec3 rootSubstrateTone = vec3(0.155, 0.228, 0.086);

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

    float distRootLift = FastRamp(10.0, 28.0, distToCam);
    float rootBlend = (1.0 - FastRamp(0.0, 0.24, h)) * mix(0.20, 0.04, distRootLift);
    baseColor = mix(baseColor, rootSubstrateTone, rootBlend);
    vec3 distantCanopyTone = mix(vec3(0.24, 0.42, 0.15), vec3(0.34, 0.52, 0.19), warmShift * 0.65);
    baseColor = mix(baseColor, distantCanopyTone, distRootLift * (1.0 - h) * 0.36);

    vec3 skyAmbient = clamp(u_ambientColor * vec3(1.25, 1.25, 1.35), 0.0, 1.0);
    vec3 groundBounce = clamp(u_ambientColor * vec3(0.55, 0.45, 0.40), 0.0, 1.0);
    vec3 greenBounce = baseColor * (u_ambientColor * 1.18 + vec3(0.026, 0.052, 0.016));
    vec3 ambientGround = mix(groundBounce, greenBounce, 0.48);
    vec3 ambient = mix(ambientGround, skyAmbient, n.y * 0.5 + 0.5);

    float rootAO = mix(mix(0.74, 0.90, distRootLift), 1.0, smoothstep(0.0, 0.38, h));
    vec3 lit = baseColor * ambient * rootAO;

    float nv = max(dot(faceNormal, viewDir), 0.0);
    float waxFresnel = FastPow4(1.0 - nv);
    vec3 waxSheen = skyAmbient * (waxFresnel * 0.055 * archWindow * specScale * (1.0 - antiShimmer * 0.85));
    lit += waxSheen * rootAO;

    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, lightDir, worldPosition, distToCam);
    lit *= mix(0.68, 1.0, shadow);
    if (shadow > 0.001) {
        float cloudVis = 0.92;
        if (distSq < 900.0) {
            float cloudDistFade = FastRamp(576.0, 900.0, distSq);
            cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, worldPosition), 0.92, cloudDistFade);
        }
        float sunVis = shadow * cloudVis;
        if (sunVis > 0.001) {
            vec3 effectiveLightColor = u_lightColor * (1.0 / (1.0 + max(lightDir.y - 0.32, 0.0) * 1.18));
            float horizonEnvelope = smoothstep(0.02, 0.38, max(lightDir.y, 0.0)) * 0.66 + 0.34;

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

            float canopyDepth = 0.34 * (1.0 - h) * (1.0 - distRootLift * 0.55);
            float opticalDepth = canopyDepth * 0.75 / max(lightDir.y, 0.28);
            float expCanopy = 1.0 / (1.0 + opticalDepth + 0.35 * opticalDepth * opticalDepth);
            float verticalSelfShadow = mix(mix(0.72, 0.90, distRootLift), 1.0, smoothstep(0.0, 0.46, h));
            float canopy = (mix(0.52, 0.76, distRootLift) + mix(0.48, 0.24, distRootLift) * expCanopy) * verticalSelfShadow;

            vec3 anisoSpec = vec3(0.0);
            float waxySpec = 0.0;
            if (archWindow > 0.005 && distToCam < 22.0) {
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

            // Tăng cường tán xạ ngược sáng Mie xuyên qua ngọn cỏ mỏng khi nhìn về phía Mặt Trời
            vec3 distortedSun = normalize(lightDir + faceNormal * 0.32);
            float forwardScatter = max(dot(-distortedSun, viewDir), 0.0);
            float backLight = max(-dot(faceNormal, modelLightDir), 0.0);
            float forwardLobe = forwardScatter * forwardScatter * (0.85 + 0.55 * forwardScatter);
            float backLobe = FastPow2(backLight) * (0.38 + 0.62 * forwardScatter);
            float edgeThinness = 0.64 + 0.36 * absU;
            float sssWindow = FastRamp(0.16, 0.54, h) * (1.0 - FastRamp(0.88, 1.0, h) * 0.20);
            float transmission = (backLobe * 0.48 + forwardLobe * 0.72 * (0.28 + 0.72 * h)) * sssWindow * edgeThinness * sssScale;
            vec3 goldenJadeSSS = baseColor * vec3(0.96, 1.42, 0.66) + vec3(0.018, 0.038, 0.010) * sssScale;

            float diffuseLobe = wrapped * (0.48 + 0.52 * wrapped);
            vec3 directDiffuse = baseColor * (diffuseLobe * 0.74);
            vec3 jadeTrans = goldenJadeSSS * (transmission * 0.94);
            vec3 specHighlight = anisoSpec + vec3(waxySpec * 0.34) * baseColor;

            vec3 directSun = ((directDiffuse + jadeTrans) * canopy + specHighlight) * (sunVis * horizonEnvelope);
            lit += directSun * effectiveLightColor;
        }
    }

    lit += emberGlow;
    if (distSq < 1225.0) {
        lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    }
    return lit;
}
// Shared directional-shadow receiver for opaque map and foliage materials.
// The Environment module owns capture/projection; map shaders only sample it.
//
// Optional compile-time switches (all optional, safe defaults):
//   MAP_DYNAMIC_SHADOW_RADIUS  PCF radius scale of the dynamic map (default 1.15).
//   MAP_SHADOW_USE_GATHER      1/0 forces the textureGather path on/off. By default it
//                              is enabled for GLSL ES >= 3.10 and desktop/Vulkan GLSL >= 4.00.
//   MAP_SHADOW_FILTER_QUALITY  0/1/2 bakes the filter tier into the shader. Dead taps are
//                              compiled out, which also lowers register pressure (and thus
//                              raises occupancy on mobile GPUs). If undefined, the tier is
//                              read from u_shadowFilterQuality at runtime as before.
//   MAP_STATIC_SHADOW_FILTER_CAP
//                              Highest filter tier the WORLD-FIXED static layer may use
//                              (default 1 = two bilinear taps).
//   MAP_SHADOW_NORMAL_OFFSET_TEXELS
//                              OPT-IN (undefined = off, identical to legacy output). Pushes the
//                              receiver position along its geometric normal by this many shadow
//                              texels * sin(theta) before the light-space lookup.
#ifndef MAP_DYNAMIC_SHADOW_RADIUS
#define MAP_DYNAMIC_SHADOW_RADIUS 1.15
#endif

#ifndef MAP_STATIC_SHADOW_FILTER_CAP
#define MAP_STATIC_SHADOW_FILTER_CAP 1
#endif

#ifndef MAP_SHADOW_USE_GATHER
  #if defined(GL_ES)
    #if __VERSION__ >= 310
      #define MAP_SHADOW_USE_GATHER 1
    #else
      #define MAP_SHADOW_USE_GATHER 0
    #endif
  #elif __VERSION__ >= 400
    #define MAP_SHADOW_USE_GATHER 1
  #else
    #define MAP_SHADOW_USE_GATHER 0
  #endif
#endif

#ifdef MAP_SHADOW_FILTER_QUALITY
  #define MAP_SHADOW_QUALITY float(MAP_SHADOW_FILTER_QUALITY)
#else
  #define MAP_SHADOW_QUALITY u_shadowFilterQuality
#endif

// Samplers are explicitly highp: in GLSL ES the implicit sampler2D precision is lowp,
// which is not acceptable for R32F depth compares.
uniform highp sampler2D shadowMap;
uniform mat4 u_lightVP;
uniform float u_shadowEnabled;
uniform float u_shadowTexel;
uniform float u_shadowFilterQuality;
uniform float u_shadowThinFeatureBoost;
uniform highp sampler2D staticShadowMap;
uniform mat4 u_staticLightVP;
uniform float u_staticShadowEnabled;
uniform float u_staticShadowTexel;

float MapShadowCompare(highp sampler2D mapTexture, vec2 uv, float compareDepth)
{
    return step(compareDepth, textureLod(mapTexture, uv, 0.0).r);
}

#if MAP_SHADOW_USE_GATHER
// Fast 2x2 box average using a single hardware textureGather (no floor/mix ALU overhead)
float MapShadowGatherBox4(highp sampler2D mapTexture, vec2 uv, float compareDepth)
{
    vec4 s = step(vec4(compareDepth), textureGather(mapTexture, uv));
    return dot(s, vec4(0.25));
}
#endif

float MapShadowCompareBilinearTexel(highp sampler2D mapTexture, vec2 texelPos,
                                    float compareDepth, float texelSize)
{
    vec2 base = floor(texelPos);
    vec2 fraction = texelPos - base;
#if MAP_SHADOW_USE_GATHER
    vec4 s = step(vec4(compareDepth),
                  textureGather(mapTexture, (base + 1.0) * texelSize));
    return mix(mix(s.w, s.z, fraction.x),
               mix(s.x, s.y, fraction.x), fraction.y);
#else
    vec2 uv = (base + 0.5) * texelSize;
    float s00 = MapShadowCompare(mapTexture, uv, compareDepth);
    float s10 = MapShadowCompare(mapTexture, uv + vec2(texelSize, 0.0), compareDepth);
    float s01 = MapShadowCompare(mapTexture, uv + vec2(0.0, texelSize), compareDepth);
    float s11 = MapShadowCompare(mapTexture, uv + vec2(texelSize), compareDepth);
    return mix(mix(s00, s10, fraction.x),
               mix(s01, s11, fraction.x), fraction.y);
#endif
}

float MapShadowCompareBilinear(highp sampler2D mapTexture, vec2 uv,
                               float compareDepth, float texelSize)
{
    float resolution = 1.0 / max(texelSize, 0.0000001);
    return MapShadowCompareBilinearTexel(
        mapTexture, uv * resolution - 0.5, compareDepth, texelSize);
}

float MapShadowFilteredVisibilityTier(highp sampler2D mapTexture, vec2 uv,
                                      float compareDepth, float texelSize,
                                      float radiusScale, float thinFeatureBoost,
                                      float quality)
{
    if (quality < 0.5)
        return MapShadowCompareBilinear(mapTexture, uv, compareDepth, texelSize);

    float resolution = 1.0 / max(texelSize, 0.0000001);
    vec2 centerPos = uv * resolution - 0.5;

    if (quality < 1.5) {
#if MAP_SHADOW_USE_GATHER
        // Single bilinear gather + 1 offset box gather (2 gathers total, half ALU)
        vec2 base = floor(centerPos);
        vec2 fraction = centerPos - base;
        vec4 sc = step(vec4(compareDepth), textureGather(mapTexture, (base + 1.0) * texelSize));
        float cSum = sc.x + sc.y + sc.z + sc.w;
        if (cSum <= 0.001) return 0.0;
        if (cSum >= 3.999) return 1.0;
        float s0 = mix(mix(sc.w, sc.z, fraction.x), mix(sc.x, sc.y, fraction.x), fraction.y);
        float s1 = MapShadowGatherBox4(mapTexture, uv + vec2(radiusScale * 0.45 * texelSize), compareDepth);
        return (s0 * 0.65 + s1 * 0.35);
#else
        return MapShadowCompareBilinearTexel(mapTexture, centerPos, compareDepth, texelSize);
#endif
    }

    float diagonal = radiusScale * 0.707;   // diagonal tap offset, in texels

#if MAP_SHADOW_USE_GATHER
    // HIGH with textureGather:
    // Stage 1: Fetch center 2x2 neighborhood + 2 opposing rotated diagonal gathers first (3 gathers).
    // If all 12 texels agree (fully lit or deep umbra, >80% of screen pixels), early-out immediately
    // and skip the remaining 2 gathers + bilinear ALU!
    vec2 base = floor(centerPos);
    vec2 fraction = centerPos - base;
    vec4 sc = step(vec4(compareDepth), textureGather(mapTexture, (base + 1.0) * texelSize));
    float center = mix(mix(sc.w, sc.z, fraction.x),
                       mix(sc.x, sc.y, fraction.x), fraction.y);

    // Subtle rotated disk offsets (26.5 deg golden-ratio tilt) break up blocky axis-aligned staircase edges
    vec2 offA = vec2(diagonal * 0.894, diagonal * 0.447) * texelSize;
    vec2 offB = vec2(-diagonal * 0.447, diagonal * 0.894) * texelSize;

    float downLeft = MapShadowGatherBox4(mapTexture, uv - offA, compareDepth);
    float upRight  = MapShadowGatherBox4(mapTexture, uv + offA, compareDepth);

    float earlySum = center + downLeft + upRight;
    if (earlySum <= 0.001)
        return 0.0;
    if (earlySum >= 2.999 && thinFeatureBoost < 0.01)
        return 1.0;

    // Stage 2: Penumbra boundary or thin-feature candidate — fetch remaining 2 orthogonal gathers
    float downRight = MapShadowGatherBox4(mapTexture, uv + offB, compareDepth);
    float upLeft    = MapShadowGatherBox4(mapTexture, uv - offB, compareDepth);
#else
    // HIGH without textureGather (e.g. Desktop GLSL 330):
    // 2-Stage 5-Tap Cross Early-Out + 4-Corner Penumbra Refine
    vec2 nearest = floor(centerPos + 0.5);
    vec2 d = centerPos - nearest; // fractional offset in [-0.5, 0.5)
    vec2 uvCenter = (nearest + 0.5) * texelSize;

    float s10 = MapShadowCompare(mapTexture, uvCenter + vec2(       0.0, -texelSize), compareDepth);
    float s01 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize,        0.0), compareDepth);
    float s11 = MapShadowCompare(mapTexture, uvCenter,                                compareDepth);
    float s21 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize,        0.0), compareDepth);
    float s12 = MapShadowCompare(mapTexture, uvCenter + vec2(       0.0,  texelSize), compareDepth);

    float crossSum = s10 + s01 + s11 + s21 + s12;
    if (crossSum <= 0.001)
        return 0.0;
    if (crossSum >= 4.999)
        return 1.0;

    // Penumbra edge pixel: fetch the 4 diagonal corners to complete the exact 3x3 resolve
    float s00 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize, -texelSize), compareDepth);
    float s20 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize, -texelSize), compareDepth);
    float s02 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize,  texelSize), compareDepth);
    float s22 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize,  texelSize), compareDepth);

    vec2 w0 = max(-d, 0.0);
    vec2 w1 = 1.0 - abs(d);
    vec2 w2 = max(d, 0.0);
    float row0 = s00 * w0.x + s10 * w1.x + s20 * w2.x;
    float row1 = s01 * w0.x + s11 * w1.x + s21 * w2.x;
    float row2 = s02 * w0.x + s12 * w1.x + s22 * w2.x;
    float center = row0 * w0.y + row1 * w1.y + row2 * w2.y;

    float effDiag = min(diagonal, 0.5);
    vec2 fDL = clamp(d + vec2(1.0 - effDiag), 0.0, 1.0);
    vec2 fDR = clamp(vec2(d.x + effDiag, d.y + 1.0 - effDiag), 0.0, 1.0);
    vec2 fUL = clamp(vec2(d.x + 1.0 - effDiag, d.y + effDiag), 0.0, 1.0);
    vec2 fUR = clamp(d + vec2(effDiag), 0.0, 1.0);

    float downLeft  = mix(mix(s00, s10, fDL.x), mix(s01, s11, fDL.x), fDL.y);
    float downRight = mix(mix(s10, s20, fDR.x), mix(s11, s21, fDR.x), fDR.y);
    float upLeft    = mix(mix(s01, s11, fUL.x), mix(s02, s12, fUL.x), fUL.y);
    float upRight   = mix(mix(s11, s21, fUR.x), mix(s12, s22, fUR.x), fUR.y);
#endif

    float smoothVisibility = center * 0.34
                           + (downLeft + downRight + upLeft + upRight) * 0.165;
    float darkestVisibility = min(center,
        min(min(downLeft, downRight), min(upLeft, upRight)));
    return mix(smoothVisibility, darkestVisibility, thinFeatureBoost * 0.18);
}

float MapShadowFilteredVisibility(highp sampler2D mapTexture, vec2 uv,
                                  float compareDepth, float texelSize,
                                  float radiusScale, float thinFeatureBoost)
{
    return MapShadowFilteredVisibilityTier(mapTexture, uv, compareDepth, texelSize,
                                           radiusScale, thinFeatureBoost,
                                           MAP_SHADOW_QUALITY);
}

float MapShadowCoverageFadeWidth(vec2 uv, float width)
{
    float edgeDistance = min(min(uv.x, 1.0 - uv.x),
                             min(uv.y, 1.0 - uv.y));
    return smoothstep(0.0, width, edgeDistance);
}

float MapShadowCoverageFade(vec2 uv)
{
    return MapShadowCoverageFadeWidth(uv, 0.035);
}

float MapShadowContrast(float visibility)
{
    visibility = clamp(visibility, 0.0, 1.0);
    // Natural penumbra rolloff: preserves delicate mid-tone shadow softness while grounding core umbra
    float sCurve = visibility * visibility * (3.0 - 2.0 * visibility);
    return mix(visibility, sCurve, 0.18);
}

float MapDynamicShadowVisibilityCovLS(vec4 lightSpace, float slope, out float coverage)
{
    coverage = 0.0;
    if (u_shadowEnabled < 0.5)
        return 1.0;

    vec3 projected = lightSpace.xyz * (0.5 / max(lightSpace.w, 0.00001)) + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0)
        return 1.0;

    float edgeFade = MapShadowCoverageFadeWidth(projected.xy, 0.12);
    if (edgeFade <= 0.0)
        return 1.0;
    coverage = edgeFade;

    float compareDepth = projected.z - mix(0.00026, 0.00080, slope);
    float visibility = MapShadowFilteredVisibility(
        shadowMap, projected.xy, compareDepth, u_shadowTexel, MAP_DYNAMIC_SHADOW_RADIUS,
        u_shadowThinFeatureBoost);
    return MapShadowContrast(mix(1.0, visibility, edgeFade));
}

float MapDynamicShadowVisibilityCov(vec3 worldPos, float slope, out float coverage)
{
    return MapDynamicShadowVisibilityCovLS(u_lightVP * vec4(worldPos, 1.0), slope, coverage);
}

float MapDynamicShadowVisibility(vec3 worldPos, float slope)
{
    float coverage;
    return MapDynamicShadowVisibilityCov(worldPos, slope, coverage);
}

float MapStaticShadowVisibilityLS(vec4 lightSpace, float slope)
{
    if (u_staticShadowEnabled < 0.5)
        return 1.0;
    vec3 projected = lightSpace.xyz * (0.5 / max(lightSpace.w, 0.00001)) + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0)
        return 1.0;
    float fade = MapShadowCoverageFade(projected.xy);
    if (fade <= 0.0)
        return 1.0;
    float compareDepth = projected.z - mix(0.0012, 0.0032, slope);
    float staticQuality = min(MAP_SHADOW_QUALITY, float(MAP_STATIC_SHADOW_FILTER_CAP));
    float visibility = MapShadowFilteredVisibilityTier(
        staticShadowMap, projected.xy, compareDepth, u_staticShadowTexel, 1.10, 0.0,
        staticQuality);
    return mix(1.0, visibility, fade);
}

float MapStaticShadowVisibility(vec3 worldPos, float slope)
{
    return MapStaticShadowVisibilityLS(u_staticLightVP * vec4(worldPos, 1.0), slope);
}

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
float MapShadowWorldTexelSize(mat4 lightVP, float texelSize)
{
    vec3 row0 = vec3(lightVP[0][0], lightVP[1][0], lightVP[2][0]);
    return texelSize / max(0.5 * length(row0), 0.000001);
}
#endif

float MapShadowVisibilityCovLS(vec4 lightSpace, vec4 staticLightSpace, vec3 normal, vec3 lightDir, out float dynamicCoverage)
{
    dynamicCoverage = 0.0;
    if (u_shadowEnabled < 0.5 && u_staticShadowEnabled < 0.5)
        return 1.0;

    float normLenSq = dot(normal, normal) * dot(lightDir, lightDir);
    float cosTheta = dot(normal, lightDir) * inversesqrt(max(normLenSq, 1e-12));
    float slope = 1.0 - clamp(cosTheta, 0.0, 1.0);

    float dynamicShadow = MapDynamicShadowVisibilityCovLS(lightSpace, slope, dynamicCoverage);
    if (dynamicShadow <= 0.001)
        return 0.0;

    return min(dynamicShadow, MapStaticShadowVisibilityLS(staticLightSpace, slope));
}

float MapShadowVisibilityCov(vec3 worldPos, vec3 normal, vec3 lightDir, out float dynamicCoverage)
{
    dynamicCoverage = 0.0;
    if (u_shadowEnabled < 0.5 && u_staticShadowEnabled < 0.5)
        return 1.0;

    float normLenSq = dot(normal, normal) * dot(lightDir, lightDir);
    float cosTheta = dot(normal, lightDir) * inversesqrt(max(normLenSq, 1e-12));
    float slope = 1.0 - clamp(cosTheta, 0.0, 1.0);

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 pushDir = normal * (inversesqrt(max(dot(normal, normal), 1e-12)) * sinTheta
                             * float(MAP_SHADOW_NORMAL_OFFSET_TEXELS));
    vec3 dynamicPos = worldPos + pushDir * MapShadowWorldTexelSize(u_lightVP, u_shadowTexel);
#else
    vec3 dynamicPos = worldPos;
#endif

    float dynamicShadow = MapDynamicShadowVisibilityCov(dynamicPos, slope, dynamicCoverage);
    if (dynamicShadow <= 0.001)
        return 0.0;

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
    vec3 staticPos = worldPos + pushDir * MapShadowWorldTexelSize(u_staticLightVP, u_staticShadowTexel);
#else
    vec3 staticPos = worldPos;
#endif
    return min(dynamicShadow, MapStaticShadowVisibility(staticPos, slope));
}

float MapShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    float dynamicCoverage;
    return MapShadowVisibilityCov(worldPos, normal, lightDir, dynamicCoverage);
}
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
#ifndef MAP_DYNAMIC_SHADOW_RADIUS
#define MAP_DYNAMIC_SHADOW_RADIUS 1.15
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
    return step(compareDepth, texture(mapTexture, uv).r);
}

// R32F shadow targets intentionally use POINT filtering because linear
// filtering is not portable across Vulkan/GLES devices. Interpolate the four
// comparison results ourselves: interpolating depth before comparison causes
// light leaks, while interpolating binary coverage produces a genuinely soft,
// sub-texel edge without exposing the shadow-map grid.
//
// texelPos is the continuous texel coordinate (uv * textureSize - 0.5), i.e. texel
// centres sit on integers. Working in texel space lets the HIGH kernel offset its
// taps with a single add and share one reciprocal.
float MapShadowCompareBilinearTexel(highp sampler2D mapTexture, vec2 texelPos,
                                    float compareDepth, float texelSize)
{
    vec2 base = floor(texelPos);
    vec2 fraction = texelPos - base;
#if MAP_SHADOW_USE_GATHER
    // One textureGather returns the whole 2x2 footprint (1 fetch instead of 4).
    // It is aimed at the shared corner of the four texels, which is 0.5 texel away
    // from every boundary, so the hardware footprint always agrees with floor() above.
    // Gather order: x=(i0,j1) y=(i1,j1) z=(i1,j0) w=(i0,j0). Gather is a point fetch
    // and does not require the format to be linearly filterable, so it is safe on R32F.
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

// Quality-scaled deterministic PCF. HIGH anchors the resolve with a weighted
// center tap, then uses four diagonals only for penumbra. Omitting the center
// made sub-texel stems fall between all four taps when the radius was reduced.
// Lower tiers retain cheaper kernels for mobile fill-rate budgets.
float MapShadowFilteredVisibility(highp sampler2D mapTexture, vec2 uv,
                                  float compareDepth, float texelSize,
                                  float radiusScale, float thinFeatureBoost)
{
    float quality = MAP_SHADOW_QUALITY;
    if (quality < 0.5)
        return MapShadowCompare(mapTexture, uv, compareDepth);

    if (quality < 1.5) {
        vec2 tap = vec2(texelSize * radiusScale);
        float visibility = 0.0;
        visibility += MapShadowCompare(mapTexture, uv + vec2(-tap.x, -tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2( tap.x, -tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2(-tap.x,  tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2( tap.x,  tap.y), compareDepth);
        return visibility * 0.25;
    }

    // HIGH: five bilinear taps = 5 fetches with gather (was 20 point fetches).
    float resolution = 1.0 / max(texelSize, 0.0000001);
    vec2 centerPos = uv * resolution - 0.5;
    float diagonal = radiusScale * 0.707;   // diagonal tap offset, in texels

    float center = MapShadowCompareBilinearTexel(
        mapTexture, centerPos, compareDepth, texelSize);
    float downLeft = MapShadowCompareBilinearTexel(
        mapTexture, centerPos + vec2(-diagonal, -diagonal), compareDepth, texelSize);
    float downRight = MapShadowCompareBilinearTexel(
        mapTexture, centerPos + vec2( diagonal, -diagonal), compareDepth, texelSize);
    float upLeft = MapShadowCompareBilinearTexel(
        mapTexture, centerPos + vec2(-diagonal,  diagonal), compareDepth, texelSize);
    float upRight = MapShadowCompareBilinearTexel(
        mapTexture, centerPos + vec2( diagonal,  diagonal), compareDepth, texelSize);

    float smoothVisibility = center * 0.36
                           + (downLeft + downRight + upLeft + upRight) * 0.16;
    float darkestVisibility = min(center,
        min(min(downLeft, downRight), min(upLeft, upRight)));
    return mix(smoothVisibility, darkestVisibility, thinFeatureBoost * 0.20);
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

// A modest contrast resolve makes sub-texel blades and petals readable after PCF
// without dilating or replacing their captured silhouette. This is the original
// pow(v, 1.03) as a multiply-add: max error 0.24% of visibility (< 1 level in 8 bit)
// and it avoids a log2+exp2 pair per fragment.
float MapShadowContrast(float visibility)
{
    visibility = clamp(visibility, 0.0, 1.0);
    return visibility * (1.0 - 0.0471 * (1.0 - visibility));
}

float MapDynamicShadowVisibility(vec3 worldPos, float slope)
{
    if (u_shadowEnabled < 0.5)
        return 1.0;

    vec4 lightSpace = u_lightVP * vec4(worldPos, 1.0);
    vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001);
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;

    // Wide chase views expose the moving shadow box. Blend its outer rim
    // gradually while preserving full shadow contrast in the interior.
    // edgeFade is exactly 0 on and outside the xy border, so it doubles as the xy
    // bounds test, and is evaluated BEFORE the expensive filter so that fragments
    // outside the box never pay for it.
    float edgeFade = MapShadowCoverageFadeWidth(projected.xy, 0.12);
    if (edgeFade <= 0.0)
        return 1.0;

    // Smooth slope-aware bias eliminating stair-stepping acne bands
    float compareDepth = projected.z - mix(0.00028, 0.00085, slope);
    float visibility = MapShadowFilteredVisibility(
        shadowMap, projected.xy, compareDepth, u_shadowTexel, MAP_DYNAMIC_SHADOW_RADIUS,
        u_shadowThinFeatureBoost);
    return MapShadowContrast(mix(1.0, visibility, edgeFade));
}

float MapStaticShadowVisibility(vec3 worldPos, float slope)
{
    if (u_staticShadowEnabled < 0.5)
        return 1.0;
    vec4 lightSpace = u_staticLightVP * vec4(worldPos, 1.0);
    vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001);
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;
    float fade = MapShadowCoverageFade(projected.xy);   // 0 outside xy -> early out
    if (fade <= 0.0)
        return 1.0;
    float compareDepth = projected.z - mix(0.0012, 0.0032, slope);
    float visibility = MapShadowFilteredVisibility(
        staticShadowMap, projected.xy, compareDepth, u_staticShadowTexel, 1.10, 0.0);
    return mix(1.0, visibility, fade);
}

float MapShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    if (u_shadowEnabled < 0.5 && u_staticShadowEnabled < 0.5)
        return 1.0;

    // cos(theta) with a single inversesqrt instead of two normalize() calls.
    float cosTheta = dot(normal, lightDir)
                   * inversesqrt(dot(normal, normal) * dot(lightDir, lightDir));
    float slope = 1.0 - max(cosTheta, 0.0);

    float dynamicShadow = MapDynamicShadowVisibility(worldPos, slope);
    if (dynamicShadow <= 0.0)
        return 0.0;
    return min(dynamicShadow, MapStaticShadowVisibility(worldPos, slope));
}

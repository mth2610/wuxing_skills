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
//                              (default 1 = two bilinear taps). The static layer only carries
//                              distant terrain/rock occlusion at a coarser texel size and has
//                              no thin features to protect, so the 5-tap HIGH kernel bought
//                              almost nothing there while costing 3 extra gathers per pixel.
//                              Define as 2 to restore the previous behaviour exactly.
//   MAP_SHADOW_NORMAL_OFFSET_TEXELS
//                              OPT-IN (undefined = off, identical to legacy output). Pushes the
//                              receiver position along its geometric normal by this many shadow
//                              texels * sin(theta) before the light-space lookup. This is the
//                              standard cure for terrain self-shadow acne under a low sun and
//                              lets rolling ground receive its own shadows without raising the
//                              constant depth bias (which erases thin blades). Assumes an
//                              orthographic light matrix and a rigid (no-scale) view transform.
//                              Typical values 1.0 - 2.5. Do not enable for thin foliage
//                              receivers; use it from ground/rock shaders.
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

// Explicit LOD 0: the shadow targets have no mip chain, and every call site sits behind
// per-pixel early-outs (non-uniform control flow), where implicit-derivative texture()
// has undefined LOD selection and forces the GPU to keep helper lanes alive for the
// derivative quad. textureLod needs neither.
float MapShadowCompare(highp sampler2D mapTexture, vec2 uv, float compareDepth)
{
    return step(compareDepth, textureLod(mapTexture, uv, 0.0).r);
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
//
// `quality` selects the tier explicitly so the static layer can run a cheaper tier than
// the dynamic one; MapShadowFilteredVisibility() below keeps the original signature.
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
        float diag = radiusScale * 0.55;
        float s0 = MapShadowCompareBilinearTexel(
            mapTexture, centerPos + vec2(-diag, -diag), compareDepth, texelSize);
        float s1 = MapShadowCompareBilinearTexel(
            mapTexture, centerPos + vec2( diag,  diag), compareDepth, texelSize);
        return (s0 + s1) * 0.5;
    }

    float diagonal = radiusScale * 0.707;   // diagonal tap offset, in texels

#if MAP_SHADOW_USE_GATHER
    // HIGH with textureGather: 5 hardware gather instructions.
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
#else
    // HIGH without textureGather (e.g. Desktop GLSL 330):
    // Deduplicate overlapping 2x2 taps into a single shared 3x3 neighborhood fetch
    // (9 point fetches total instead of 20), reconstructing the exact bilinear
    // center tap and all four quadrant bilinear penumbra taps in registers.
    vec2 nearest = floor(centerPos + 0.5);
    vec2 d = centerPos - nearest; // fractional offset in [-0.5, 0.5)
    vec2 uvCenter = (nearest + 0.5) * texelSize;

    float s00 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize, -texelSize), compareDepth);
    float s10 = MapShadowCompare(mapTexture, uvCenter + vec2(       0.0, -texelSize), compareDepth);
    float s20 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize, -texelSize), compareDepth);
    float s01 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize,        0.0), compareDepth);
    float s11 = MapShadowCompare(mapTexture, uvCenter,                                compareDepth);
    float s21 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize,        0.0), compareDepth);
    float s02 = MapShadowCompare(mapTexture, uvCenter + vec2(-texelSize,  texelSize), compareDepth);
    float s12 = MapShadowCompare(mapTexture, uvCenter + vec2(       0.0,  texelSize), compareDepth);
    float s22 = MapShadowCompare(mapTexture, uvCenter + vec2( texelSize,  texelSize), compareDepth);

    // Mathematically exact bilinear resolve for centerPos
    vec2 w0 = max(-d, 0.0);
    vec2 w1 = 1.0 - abs(d);
    vec2 w2 = max(d, 0.0);
    float row0 = s00 * w0.x + s10 * w1.x + s20 * w2.x;
    float row1 = s01 * w0.x + s11 * w1.x + s21 * w2.x;
    float row2 = s02 * w0.x + s12 * w1.x + s22 * w2.x;
    float center = row0 * w0.y + row1 * w1.y + row2 * w2.y;

    // Continuous C0-seamless quadrant bilinear taps across the 4 sub-quads of the 3x3 grid.
    // NOTE: a 3x3 neighbourhood can only reach 0.5 texel beyond the nearest texel without
    // another row/column of fetches, so the diagonal offset is clamped to 0.5 here. With the
    // default radius (1.15 -> 0.81 texel) this path therefore resolves a slightly NARROWER
    // penumbra than the textureGather path. Radii <= 0.707 (e.g. the ground's 0.65) are exact.
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

    float smoothVisibility = center * 0.36
                           + (downLeft + downRight + upLeft + upRight) * 0.16;
    float darkestVisibility = min(center,
        min(min(downLeft, downRight), min(upLeft, upRight)));
    return mix(smoothVisibility, darkestVisibility, thinFeatureBoost * 0.20);
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
    // The static layer never carries thin features (thinFeatureBoost 0), so it is capped to
    // a cheaper tier than the dynamic one (see MAP_STATIC_SHADOW_FILTER_CAP).
    float staticQuality = min(MAP_SHADOW_QUALITY, float(MAP_STATIC_SHADOW_FILTER_CAP));
    float visibility = MapShadowFilteredVisibilityTier(
        staticShadowMap, projected.xy, compareDepth, u_staticShadowTexel, 1.10, 0.0,
        staticQuality);
    return mix(1.0, visibility, fade);
}

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
// World-space size of one shadow texel for an orthographic light matrix. Row 0 of the
// light view-projection maps world -> clip x; for an orthographic projection its xyz length
// is 2 / (world width covered), so one uv unit (0.5 clip units) spans 1 / (0.5 * |row0|).
// u_lightVP / u_staticLightVP already carry the inverse view, which is a pure rotation +
// translation, so the length is unchanged.
float MapShadowWorldTexelSize(mat4 lightVP, float texelSize)
{
    vec3 row0 = vec3(lightVP[0][0], lightVP[1][0], lightVP[2][0]);
    return texelSize / max(0.5 * length(row0), 0.000001);
}
#endif

float MapShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    if (u_shadowEnabled < 0.5 && u_staticShadowEnabled < 0.5)
        return 1.0;

    // Guard against degenerate zero-length normals/light vectors to prevent NaN propagation
    float normLenSq = dot(normal, normal) * dot(lightDir, lightDir);
    float cosTheta = dot(normal, lightDir) * inversesqrt(max(normLenSq, 1e-12));
    float slope = 1.0 - clamp(cosTheta, 0.0, 1.0);

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
    // sin(theta) scaled unit-normal push: zero when facing the light, full at grazing angles.
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 pushDir = normal * (inversesqrt(max(dot(normal, normal), 1e-12)) * sinTheta
                             * float(MAP_SHADOW_NORMAL_OFFSET_TEXELS));
    vec3 dynamicPos = worldPos + pushDir * MapShadowWorldTexelSize(u_lightVP, u_shadowTexel);
#else
    vec3 dynamicPos = worldPos;
#endif

    float dynamicShadow = MapDynamicShadowVisibility(dynamicPos, slope);
    if (dynamicShadow <= 0.001)
        return 0.0;

#ifdef MAP_SHADOW_NORMAL_OFFSET_TEXELS
    vec3 staticPos = worldPos + pushDir * MapShadowWorldTexelSize(u_staticLightVP, u_staticShadowTexel);
#else
    vec3 staticPos = worldPos;
#endif
    return min(dynamicShadow, MapStaticShadowVisibility(staticPos, slope));
}

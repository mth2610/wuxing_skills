// Shared directional-shadow receiver for opaque map and foliage materials.
// The Environment module owns capture/projection; map shaders only sample it.
uniform sampler2D shadowMap;
uniform mat4 u_lightVP;
uniform float u_shadowEnabled;
uniform float u_shadowTexel;
uniform float u_shadowFilterQuality;
uniform float u_shadowThinFeatureBoost;
uniform sampler2D staticShadowMap;
uniform mat4 u_staticLightVP;
uniform float u_staticShadowEnabled;
uniform float u_staticShadowTexel;

float MapShadowCompare(sampler2D mapTexture, vec2 uv, float compareDepth)
{
    return step(compareDepth, texture(mapTexture, uv).r);
}

// R32F shadow targets intentionally use POINT filtering because linear
// filtering is not portable across Vulkan/GLES devices. Interpolate the four
// comparison results ourselves: interpolating depth before comparison causes
// light leaks, while interpolating binary coverage produces a genuinely soft,
// sub-texel edge without exposing the shadow-map grid.
float MapShadowCompareBilinear(sampler2D mapTexture, vec2 uv,
                               float compareDepth, float texelSize)
{
    float resolution = 1.0 / max(texelSize, 0.0000001);
    vec2 texelPosition = uv * resolution - 0.5;
    vec2 fraction = fract(texelPosition);
    vec2 base = (floor(texelPosition) + 0.5) * texelSize;
    float s00 = MapShadowCompare(mapTexture, base, compareDepth);
    float s10 = MapShadowCompare(mapTexture, base + vec2(texelSize, 0.0), compareDepth);
    float s01 = MapShadowCompare(mapTexture, base + vec2(0.0, texelSize), compareDepth);
    float s11 = MapShadowCompare(mapTexture, base + vec2(texelSize), compareDepth);
    return mix(mix(s00, s10, fraction.x),
               mix(s01, s11, fraction.x), fraction.y);
}

// Quality-scaled deterministic PCF. HIGH anchors the resolve with a weighted
// center tap, then uses four diagonals only for penumbra. Omitting the center
// made sub-texel stems fall between all four taps when the radius was reduced.
// Lower tiers retain cheaper kernels for mobile fill-rate budgets.
float MapShadowFilteredVisibility(sampler2D mapTexture, vec2 uv,
                                  float compareDepth, float texelSize,
                                  float radiusScale, float thinFeatureBoost)
{
    if (u_shadowFilterQuality < 0.5)
        return MapShadowCompare(mapTexture, uv, compareDepth);

    vec2 tap = vec2(texelSize * radiusScale);
    if (u_shadowFilterQuality < 1.5) {
        float visibility = 0.0;
        visibility += MapShadowCompare(mapTexture, uv + vec2(-tap.x, -tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2( tap.x, -tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2(-tap.x,  tap.y), compareDepth);
        visibility += MapShadowCompare(mapTexture, uv + vec2( tap.x,  tap.y), compareDepth);
        return visibility * 0.25;
    }

    float center = MapShadowCompareBilinear(
        mapTexture, uv, compareDepth, texelSize);
    float downLeft = MapShadowCompareBilinear(
        mapTexture, uv + vec2(-tap.x, -tap.y), compareDepth, texelSize);
    float downRight = MapShadowCompareBilinear(
        mapTexture, uv + vec2(tap.x, -tap.y), compareDepth, texelSize);
    float upLeft = MapShadowCompareBilinear(
        mapTexture, uv + vec2(-tap.x, tap.y), compareDepth, texelSize);
    float upRight = MapShadowCompareBilinear(
        mapTexture, uv + vec2(tap.x, tap.y), compareDepth, texelSize);
    float smoothVisibility = center * 0.50
                           + (downLeft + downRight + upLeft + upRight) * 0.125;
    // A small darkest-sample contribution retains narrow blades without the
    // old 84% binary minimum that expanded every covered texel into black,
    // comb-shaped blocks.
    float darkestVisibility = min(center,
        min(min(downLeft, downRight), min(upLeft, upRight)));
    return mix(smoothVisibility, darkestVisibility, thinFeatureBoost);
}

float MapShadowCoverageFade(vec2 uv)
{
    float edgeDistance = min(min(uv.x, 1.0 - uv.x),
                             min(uv.y, 1.0 - uv.y));
    return smoothstep(0.0, 0.035, edgeDistance);
}

float MapDynamicShadowVisibility(vec3 worldPos, float slope)
{
    if (u_shadowEnabled < 0.5)
        return 1.0;

    vec4 lightSpace = u_lightVP * vec4(worldPos, 1.0);
    vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001);
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0)
        return 1.0;

    // Dynamic capture contains vegetation/characters but no terrain receiver,
    // so it does not need the large static-terrain acne bias. The old bias was
    // several centimetres in light depth and erased short flower shadows.
    float compareDepth = projected.z - mix(0.00018, 0.00055, slope);
    float visibility = MapShadowFilteredVisibility(
        shadowMap, projected.xy, compareDepth, u_shadowTexel, 0.95,
        u_shadowThinFeatureBoost);
    float edgeFade = MapShadowCoverageFade(projected.xy);
    float resolved = mix(1.0, visibility, edgeFade);
    // A modest contrast resolve makes sub-texel blades and petals readable
    // after PCF without dilating or replacing their captured silhouette.
    return pow(clamp(resolved, 0.0, 1.0), 1.03);
}

float MapStaticShadowVisibility(vec3 worldPos, float slope)
{
    if (u_staticShadowEnabled < 0.5)
        return 1.0;
    vec4 lightSpace = u_staticLightVP * vec4(worldPos, 1.0);
    vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001);
    projected = projected * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        projected.x <= 0.0 || projected.x >= 1.0 ||
        projected.y <= 0.0 || projected.y >= 1.0)
        return 1.0;
    float compareDepth = projected.z - mix(0.0012, 0.0032, slope);
    float visibility = MapShadowFilteredVisibility(
        staticShadowMap, projected.xy, compareDepth, u_staticShadowTexel, 1.10, 0.0);
    return mix(1.0, visibility, MapShadowCoverageFade(projected.xy));
}

float MapShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    float slope = 1.0 - max(dot(normalize(normal), normalize(lightDir)), 0.0);
    float dynamicShadow = MapDynamicShadowVisibility(worldPos, slope);
    float staticShadow = MapStaticShadowVisibility(worldPos, slope);
    return min(dynamicShadow, staticShadow);
}

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

// Quality-scaled deterministic PCF. HIGH uses a compact 3x3 tent kernel so
// fine foliage silhouettes stay stable without the checker pattern produced
// by four equally weighted diagonal taps. Lower tiers retain cheaper kernels
// for mobile fill-rate budgets.
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

    float center = MapShadowCompare(mapTexture, uv, compareDepth);
    float left = MapShadowCompare(mapTexture, uv + vec2(-tap.x, 0.0), compareDepth);
    float right = MapShadowCompare(mapTexture, uv + vec2(tap.x, 0.0), compareDepth);
    float down = MapShadowCompare(mapTexture, uv + vec2(0.0, -tap.y), compareDepth);
    float up = MapShadowCompare(mapTexture, uv + vec2(0.0, tap.y), compareDepth);
    float downLeft = MapShadowCompare(mapTexture, uv + vec2(-tap.x, -tap.y), compareDepth);
    float downRight = MapShadowCompare(mapTexture, uv + vec2(tap.x, -tap.y), compareDepth);
    float upLeft = MapShadowCompare(mapTexture, uv + vec2(-tap.x, tap.y), compareDepth);
    float upRight = MapShadowCompare(mapTexture, uv + vec2(tap.x, tap.y), compareDepth);
    float tentVisibility = (center * 4.0
                          + (left + right + down + up) * 2.0
                          + downLeft + downRight + upLeft + upRight) * 0.0625;
    // Tent PCF alone can average a one-texel grass blade into near invisibility.
    // Preserve a restrained amount of the darkest covered sample on HIGH;
    // this behaves like a stable contact-preserving resolve, not a larger caster.
    float darkestVisibility = min(center, min(min(left, right), min(down, up)));
    darkestVisibility = min(darkestVisibility,
                            min(min(downLeft, downRight), min(upLeft, upRight)));
    return mix(tentVisibility, darkestVisibility, thinFeatureBoost);
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
        shadowMap, projected.xy, compareDepth, u_shadowTexel, 1.55,
        u_shadowThinFeatureBoost);
    float edgeFade = MapShadowCoverageFade(projected.xy);
    float resolved = mix(1.0, visibility, edgeFade);
    // A modest contrast resolve makes sub-texel blades and petals readable
    // after PCF without dilating or replacing their captured silhouette.
    return pow(clamp(resolved, 0.0, 1.0), 1.28);
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

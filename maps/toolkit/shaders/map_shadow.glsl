// Shared directional-shadow receiver for opaque map and foliage materials.
// The Environment module owns capture/projection; map shaders only sample it.
uniform sampler2D shadowMap;
uniform mat4 u_lightVP;
uniform float u_shadowEnabled;
uniform float u_shadowTexel;

float MapShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
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

    float slope = 1.0 - max(dot(normalize(normal), normalize(lightDir)), 0.0);
    float compareDepth = projected.z - mix(0.00065, 0.0018, slope);
    vec2 tap = vec2(u_shadowTexel * 1.35);
    float visibility = 0.0;
    visibility += step(compareDepth, texture(shadowMap, projected.xy + vec2(-tap.x, -tap.y)).r);
    visibility += step(compareDepth, texture(shadowMap, projected.xy + vec2( tap.x, -tap.y)).r);
    visibility += step(compareDepth, texture(shadowMap, projected.xy + vec2(-tap.x,  tap.y)).r);
    visibility += step(compareDepth, texture(shadowMap, projected.xy + vec2( tap.x,  tap.y)).r);
    return visibility * 0.25;
}

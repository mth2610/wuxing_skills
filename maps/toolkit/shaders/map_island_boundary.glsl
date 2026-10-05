float MapIslandDistance(vec2 worldXZ, vec4 rect, float radius)
{
    radius = clamp(radius, 0.0, min(rect.z, rect.w) * 0.5);
    vec2 q = abs(worldXZ - rect.xy - rect.zw * 0.5) - rect.zw * 0.5 + radius;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

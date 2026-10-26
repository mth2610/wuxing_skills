float MapIslandDistance(vec2 worldXZ, vec4 rect, float radius)
{
    vec2 halfSize = max(rect.zw * 0.5, vec2(0.0));
    float r = clamp(radius, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(worldXZ - (rect.xy + halfSize)) - (halfSize - r);

    // Fast path: inside the inner box, max(q, 0.0) is zero -> skip sqrt() completely
    vec2 qOut = max(q, vec2(0.0));
    float outsideSq = dot(qOut, qOut);
    float outsideDist = (outsideSq > 1e-12) ? (outsideSq * inversesqrt(outsideSq)) : 0.0;

    return outsideDist + min(max(q.x, q.y), 0.0) - r;
}

float MapIslandDistance(vec2 worldXZ, vec4 rect, float radius)
{
    vec2 halfSize = rect.zw * 0.5;
    float r = clamp(radius, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(worldXZ - (rect.xy + halfSize)) - (halfSize - r);
    
    // Fast path: inside the inner box, max(q, 0.0) is zero -> skip sqrt() completely
    float outsideSq = dot(max(q, vec2(0.0)), max(q, vec2(0.0)));
    float outsideDist = (outsideSq > 0.0) ? sqrt(outsideSq) : 0.0;
    
    return outsideDist + min(max(q.x, q.y), 0.0) - r;
}
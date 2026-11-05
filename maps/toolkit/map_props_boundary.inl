static bool IslandBoundaryValid(const MapIslandBoundary *b)
{
    return b && isfinite(b->rect.x) && isfinite(b->rect.y) &&
           isfinite(b->rect.z) && isfinite(b->rect.w) && b->rect.z > 0.0f && b->rect.w > 0.0f &&
           isfinite(b->cornerRadius) && isfinite(b->mistWidth) && isfinite(b->groundInset) &&
           isfinite(b->cloudLift) && isfinite(b->cloudBankWidth);
}

// Visual-only rounded perimeter shared by terrain and cloud banks.
float MapProp_IslandBoundaryDistance(const MapIslandBoundary *b, Vector2 p)
{
    if (!IslandBoundaryValid(b))
        return -1e6f;
    float halfW = b->rect.z * 0.5f;
    float halfD = b->rect.w * 0.5f;
    float radius = fmaxf(0.0f, fminf(b->cornerRadius, fminf(halfW, halfD)));
    float x = fabsf(p.x - (b->rect.x + halfW)) - (halfW - radius);
    float z = fabsf(p.y - (b->rect.y + halfD)) - (halfD - radius);

    // Fast path: inside the inner box, both x and z are <= 0 -> skip sqrtf() completely
    if (x <= 0.0f && z <= 0.0f)
        return fmaxf(x, z) - radius;

    float ox = fmaxf(x, 0.0f);
    float oz = fmaxf(z, 0.0f);
    return sqrtf(ox * ox + oz * oz) - radius;
}

void MapProp_SetGroundIslandBoundary(MapGroundSurface *ground, const MapIslandBoundary *b)
{
    if (ground)
        ground->boundary = IslandBoundaryValid(b) ? *b : (MapIslandBoundary){0};
}

// Compatibility rectangle descriptor. Cloud geometry remains a single flat quad.
void MapProp_SetCloudSeaIslandBoundary(MapCloudSea *cloud, const MapIslandBoundary *b)
{
    if (cloud && cloud->ready && IslandBoundaryValid(b))
        cloud->boundary = *b;
}
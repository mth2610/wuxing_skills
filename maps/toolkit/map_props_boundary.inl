static bool IslandBoundaryValid(const MapIslandBoundary *b)
{
    return b && isfinite(b->rect.x) && isfinite(b->rect.y) &&
        isfinite(b->rect.z) && isfinite(b->rect.w) && b->rect.z > 0 && b->rect.w > 0 &&
        isfinite(b->cornerRadius) && isfinite(b->mistWidth) && isfinite(b->groundInset) &&
        isfinite(b->cloudLift) && isfinite(b->cloudBankWidth);
}

// Visual-only rounded perimeter shared by terrain and cloud banks.
float MapProp_IslandBoundaryDistance(const MapIslandBoundary *b, Vector2 p)
{
    if (!IslandBoundaryValid(b)) return -1e6f;
    float radius = fmaxf(0.0f, fminf(b->cornerRadius, fminf(b->rect.z, b->rect.w) * 0.5f));
    float x = fabsf(p.x - b->rect.x - b->rect.z * 0.5f) - b->rect.z * 0.5f + radius;
    float z = fabsf(p.y - b->rect.y - b->rect.w * 0.5f) - b->rect.w * 0.5f + radius;
    return sqrtf(fmaxf(x, 0.0f) * fmaxf(x, 0.0f) + fmaxf(z, 0.0f) * fmaxf(z, 0.0f))
         + fminf(fmaxf(x, z), 0.0f) - radius;
}

void MapProp_SetGroundIslandBoundary(MapGroundSurface *ground, const MapIslandBoundary *b)
{
    if (ground) ground->boundary = IslandBoundaryValid(b) ? *b : (MapIslandBoundary){0};
}

// Compatibility rectangle descriptor. Cloud geometry remains a single flat quad.
void MapProp_SetCloudSeaIslandBoundary(MapCloudSea *cloud, const MapIslandBoundary *b)
{
    if (cloud && cloud->ready && IslandBoundaryValid(b)) cloud->boundary = *b;
}

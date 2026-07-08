// Rock props (one mesh/texture set, many placements) — plain-textured or
// prop_lit (maps/toolkit/prop_lit.h) depending on whether normal/roughness
// paths are given. #include'd once from map_props.c — not a standalone
// translation unit.

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};

    Mesh mesh = GenMeshSphere(1.0f, 6, 8); // low ring/slice count -> faceted low-poly look

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);
        Texture2D normal = ResourceManager_LoadTexture(normalPath);
        Texture2D roughness = ResourceManager_LoadTexture(roughnessPath);

        GenTextureMipmaps(&diffuse);
        GenTextureMipmaps(&normal);
        GenTextureMipmaps(&roughness);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(normal, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(roughness, TEXTURE_FILTER_ANISOTROPIC_16X);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);

        GenTextureMipmaps(&diffuse);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count)
{
    if (!rocks->ready)
        return;

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];

        Vector3 pos = {p->position.x, -0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        // API Môi trường - Đổ bóng giả tự động tính góc sáng
        Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                    p->radiusScale * 2.0f, p->heightScale * 2.0f);

        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
}

void MapProp_UnloadRocks(MapRockSet *rocks)
{
    if (!rocks->ready)
        return;
    UnloadModel(rocks->model);
    rocks->ready = false;
}

// --- Mountain ring (border rocks for the floating-island motif) ---------

int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                 float mapWidth, float mapDepth,
                                 float minRadiusScale, float maxRadiusScale,
                                 float minHeightScale, float maxHeightScale,
                                 unsigned int seed)
{
    if (maxCount <= 0)
        return 0;

    SetRandomSeed(seed);

    float perimeter = 2.0f * (mapWidth + mapDepth);
    float centerX = mapWidth * 0.5f;
    float centerZ = mapDepth * 0.5f;

    for (int i = 0; i < maxCount; i++)
    {
        // Walk clockwise around the rectangle's border: +X along Z=0, +Z
        // along X=width, -X along Z=depth, -Z back along X=0.
        float t = ((float)i / (float)maxCount) * perimeter;
        float x, z;
        if (t < mapWidth) { x = t; z = 0.0f; }
        else if (t < mapWidth + mapDepth) { x = mapWidth; z = t - mapWidth; }
        else if (t < 2.0f * mapWidth + mapDepth) { x = mapWidth - (t - mapWidth - mapDepth); z = mapDepth; }
        else { x = 0.0f; z = mapDepth - (t - 2.0f * mapWidth - mapDepth); }

        // Jitter outward (away from map center) so peaks don't form a
        // perfectly straight wall, plus a little jitter along the border so
        // spacing doesn't look like an evenly-spaced pearl necklace. Kept
        // small — mapWidth/mapDepth is expected to already be sized a bit
        // smaller than the actual ground extent (see MapProp_CreateGroundHeightmap's
        // flat plateau boundary) so the whole ring — jitter included — stays
        // on FLAT ground; these rocks don't follow terrain height, so if a
        // placement lands on the sloped cliff band the rock visibly floats
        // above (or sinks below) the actual ground surface there.
        float outward = (float)GetRandomValue(-100, 200) / 100.0f;  // -1m..+2m
        float tangent = (float)GetRandomValue(-150, 150) / 100.0f;  // +/-1.5m
        float dx = x - centerX, dz = z - centerZ;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.001f) { dx /= len; dz /= len; }
        x += dx * outward + (-dz) * tangent;
        z += dz * outward + (dx) * tangent;

        outPlacements[i].position = (Vector3){x, 0.0f, z};
        outPlacements[i].radiusScale = (float)GetRandomValue((int)(minRadiusScale * 100.0f), (int)(maxRadiusScale * 100.0f)) / 100.0f;
        outPlacements[i].heightScale = (float)GetRandomValue((int)(minHeightScale * 100.0f), (int)(maxHeightScale * 100.0f)) / 100.0f;
        outPlacements[i].rotationDeg = (float)GetRandomValue(0, 359);
    }

    return maxCount;
}

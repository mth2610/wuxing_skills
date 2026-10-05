// Rock props (one mesh/texture set, many placements) — plain-textured or
// prop_lit (maps/toolkit/prop_lit.h) depending on whether normal/roughness
// paths are given. #include'd once from map_props.c — not a standalone
// translation unit.

static Mesh Nature_BuildProceduralRockMesh(bool isMountainCrag)
{
    int rings = isMountainCrag ? 14 : 10;
    int slices = isMountainCrag ? 18 : 12;
    Mesh mesh = GenMeshSphere(1.0f, rings, slices);

    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3 + 0];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];

        float horizR = sqrtf(x * x + z * z);
        float angle = atan2f(z, x);

        if (isMountainCrag) {
            // Jagged mountain cliff / crag:
            // 1. Taper upward with prominent sharp ridge lines (3-ridge karst/granite peak)
            float ridge = 1.0f + 0.32f * cosf(3.0f * angle + 0.5f) + 0.16f * cosf(5.0f * angle - 1.1f);
            // 2. Horizontal rock strata & stepped terraces
            float terrace = 0.08f * sinf(y * 12.0f + angle * 1.5f);
            // 3. Peak height factor: stretch upward into sharp spire
            float heightFactor = (y + 1.0f) * 0.5f;
            float taper = 1.12f - 0.72f * powf(heightFactor, 1.35f);
            // 4. Flattened and expanded base for solid grounding
            float baseWiden = (y < -0.1f) ? (1.0f + (-0.1f - y) * 0.75f) : 1.0f;

            float newR = horizR * ridge * taper * baseWiden;
            mesh.vertices[i * 3 + 0] = cosf(angle) * newR;
            mesh.vertices[i * 3 + 1] = (y + 0.15f) * 2.2f + terrace;
            mesh.vertices[i * 3 + 2] = sinf(angle) * newR;
        } else {
            // Natural weathered boulder:
            // 1. Asymmetric faceted perturbation
            float facet = 1.0f + 0.14f * cosf(3.0f * angle + y * 4.0f)
                               + 0.09f * sinf(5.0f * angle - y * 3.0f)
                               + 0.05f * cosf(7.0f * angle);
            // 2. Flatten base so it sits solidly on the ground
            float flatY = (y < -0.15f) ? (-0.15f + (y + 0.15f) * 0.35f) : y;
            float baseSpread = (y < -0.15f) ? (1.0f + (-0.15f - y) * 0.45f) : 1.0f;

            float newR = horizR * facet * baseSpread;
            mesh.vertices[i * 3 + 0] = cosf(angle) * newR;
            mesh.vertices[i * 3 + 1] = flatY;
            mesh.vertices[i * 3 + 2] = sinf(angle) * newR;
        }
    }

    // Recompute smooth normals from triangles
    if (mesh.indices != NULL) {
        for (int i = 0; i < mesh.vertexCount * 3; i++) mesh.normals[i] = 0.0f;
        for (int t = 0; t < mesh.triangleCount; t++) {
            unsigned short i0 = mesh.indices[t * 3 + 0];
            unsigned short i1 = mesh.indices[t * 3 + 1];
            unsigned short i2 = mesh.indices[t * 3 + 2];

            Vector3 v0 = {mesh.vertices[i0 * 3], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]};
            Vector3 v1 = {mesh.vertices[i1 * 3], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]};
            Vector3 v2 = {mesh.vertices[i2 * 3], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]};

            Vector3 e1 = Vector3Subtract(v1, v0);
            Vector3 e2 = Vector3Subtract(v2, v0);
            Vector3 n = Vector3CrossProduct(e1, e2);

            mesh.normals[i0 * 3 + 0] += n.x; mesh.normals[i0 * 3 + 1] += n.y; mesh.normals[i0 * 3 + 2] += n.z;
            mesh.normals[i1 * 3 + 0] += n.x; mesh.normals[i1 * 3 + 1] += n.y; mesh.normals[i1 * 3 + 2] += n.z;
            mesh.normals[i2 * 3 + 0] += n.x; mesh.normals[i2 * 3 + 1] += n.y; mesh.normals[i2 * 3 + 2] += n.z;
        }
        for (int i = 0; i < mesh.vertexCount; i++) {
            Vector3 n = {mesh.normals[i * 3], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]};
            float len = Vector3Length(n);
            if (len > 0.0001f) {
                mesh.normals[i * 3 + 0] = n.x / len;
                mesh.normals[i * 3 + 1] = n.y / len;
                mesh.normals[i * 3 + 2] = n.z / len;
            } else {
                mesh.normals[i * 3 + 1] = 1.0f;
            }
        }
    }

    return mesh;
}

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(false);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

MapRockSet MapProp_CreateMountainCrags(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(true);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow)
{
    if (!rocks->ready)
        return;

    // Tried DrawMeshInstanced() here to collapse the no-shadow case (the
    // mountain ring) into one draw call instead of `count` — reverted.
    // raylib's plain default material/shader (what the NULL,NULL branch of
    // MapProp_CreateRocks uses) isn't compiled with instancing support
    // (no `instanceTransform` attribute), so instances silently failed to
    // place/render correctly. Doing this properly needs a small dedicated
    // instanced-unlit shader — worth adding later if draw-call count (not
    // fill-rate/shadow overdraw, both already fixed) turns out to still
    // matter; not done speculatively.

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];

        Vector3 pos = {p->position.x, p->position.y - 0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        // API Môi trường - Đổ bóng giả tự động tính góc sáng. Bỏ qua cho
        // dàn đá dùng làm vách núi/viền — hàng chục cái bóng lớn xếp sát
        // nhau đè lên nhau (alpha overdraw) tốn fill-rate mà không thấy rõ
        // tác dụng.
        if (drawShadow) {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater) {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
}

void MapProp_DrawRockShadowCasters(MapRockSet *rocks,
                                   const MapRockPlacement *placements, int count,
                                   Shader depthShader)
{
    if (!rocks || !rocks->ready || !placements || count <= 0 ||
        rocks->model.materialCount < 1)
        return;
    Shader previous = rocks->model.materials[0].shader;
    rocks->model.materials[0].shader = depthShader;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++) {
        const MapRockPlacement *p = &placements[i];
        Vector3 pos = {p->position.x, p->position.y - 0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
    rlDrawRenderBatchActive();
    rocks->model.materials[0].shader = previous;
}

void MapProp_UnloadRocks(MapRockSet *rocks)
{
    if (!rocks->ready)
        return;
    // The environment owns the cloud field; this material only borrows it.
    for (int i = 0; i < rocks->model.materialCount; i++)
        rocks->model.materials[i].maps[MATERIAL_MAP_METALNESS].texture = (Texture2D){0};
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

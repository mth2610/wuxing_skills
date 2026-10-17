// Rock props (one mesh/texture set, many placements) — plain-textured or
// prop_lit (maps/toolkit/prop_lit.h) depending on whether normal/roughness
// paths are given. #include'd once from map_props.c — not a standalone
// translation unit.

static inline float Rock_Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static Mesh Nature_BuildProceduralRockMesh(int rockType)
{
    int rings = (rockType == 1) ? 30 : 24;
    int slices = (rockType == 1) ? 38 : 32;
    Mesh mesh = GenMeshSphere(1.0f, rings, slices);

    // Planar cleavage normals for sharp, chiseled granite / karst facets
    static const float kBoulderPlanes[7][4] = {
        { 0.78f,  0.48f,  0.39f, 0.76f},
        {-0.72f,  0.52f,  0.46f, 0.74f},
        { 0.18f,  0.44f, -0.88f, 0.75f},
        {-0.32f,  0.65f, -0.69f, 0.72f},
        { 0.55f,  0.74f, -0.38f, 0.68f},
        {-0.64f,  0.28f, -0.71f, 0.78f},
        { 0.12f,  0.92f,  0.37f, 0.64f},
    };

    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3 + 0];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];

        float horizR = sqrtf(x * x + z * z);
        float angle = atan2f(z, x);

        if (rockType == 1) {
            // Jagged mountain cliff / karst crag with sharp planar facets and stepped strata
            float c1 = cosf(3.0f * angle + 0.45f);
            float c2 = sinf(5.0f * angle - 0.85f);
            float c3 = cosf(7.0f * angle + 1.10f);
            float ridge = 1.0f + 0.34f * (c1 >= 0.0f ? sqrtf(c1) : -sqrtf(-c1))
                              + 0.18f * c2 + 0.09f * fabsf(c3);

            // Sharp stepped sedimentary ledges
            float strataPhase = y * 11.0f + angle * 1.5f;
            float terrace = 0.09f * (sinf(strataPhase) + 0.35f * sinf(strataPhase * 2.0f));

            float heightFactor = (y + 1.0f) * 0.5f;
            float taper = 1.28f - 0.82f * powf(heightFactor, 1.15f);
            float summitPinch = Rock_Smoothstep(0.30f, 1.0f, y);
            float ridgeStrike = cosf(angle - 0.4f);
            float ridgeChisel = 1.0f - summitPinch * 0.68f * (1.0f - fabsf(ridgeStrike));

            float baseWiden = (y < -0.05f) ? (1.0f + (-0.05f - y) * 0.92f) : 1.0f;
            float leanX = 0.26f * powf(heightFactor, 2.0f);
            float leanZ = -0.17f * powf(heightFactor, 2.0f);

            float newR = horizR * ridge * taper * ridgeChisel * baseWiden;
            float vx = cosf(angle) * newR + leanX;
            float vy = (y + 0.15f) * 2.35f + terrace;
            float vz = sinf(angle) * newR + leanZ;

            mesh.vertices[i * 3 + 0] = vx;
            mesh.vertices[i * 3 + 1] = vy;
            mesh.vertices[i * 3 + 2] = vz;

            mesh.texcoords[i * 2 + 0] *= 2.8f;
            mesh.texcoords[i * 2 + 1] *= 3.8f;
        } else if (rockType == 2) {
            // River stone / cobblestone: water-worn flattened alluvial boulder
            float facet = 1.0f + 0.10f * cosf(2.0f * angle + y * 1.8f)
                               + 0.06f * sinf(3.0f * angle - 0.6f)
                               - 0.04f * fabsf(cosf(5.0f * angle));
            float flatY = (y < -0.08f) ? (-0.08f + (y + 0.08f) * 0.35f) : y * 0.42f;
            float newR = horizR * facet * 1.18f;
            mesh.vertices[i * 3 + 0] = cosf(angle) * newR;
            mesh.vertices[i * 3 + 1] = flatY;
            mesh.vertices[i * 3 + 2] = sinf(angle) * newR;

            mesh.texcoords[i * 2 + 0] *= 1.6f;
            mesh.texcoords[i * 2 + 1] *= 1.4f;
        } else {
            // Chiseled granite boulder: planar cleavage facets + grounded skirt
            float c1 = cosf(3.0f * angle + 0.35f);
            float c2 = sinf(4.0f * angle - 0.65f);
            float c3 = cosf(6.0f * angle + 1.2f);
            float cleavage = 0.24f * c1 + 0.14f * c2 - 0.09f * fabsf(c3);

            float vx = cosf(angle) * horizR * (1.0f + cleavage);
            float vy = y * 0.88f;
            float vz = sinf(angle) * horizR * (1.0f + cleavage);

            // Clip against tilted geological fracture planes for crisp angular rock faces
            for (int p = 0; p < 7; p++) {
                float nx = kBoulderPlanes[p][0];
                float ny = kBoulderPlanes[p][1];
                float nz = kBoulderPlanes[p][2];
                float d  = kBoulderPlanes[p][3];
                float dist = vx * nx + vy * ny + vz * nz - d;
                if (dist > 0.0f) {
                    float clipStrength = 0.72f;
                    vx -= nx * dist * clipStrength;
                    vy -= ny * dist * clipStrength;
                    vz -= nz * dist * clipStrength;
                }
            }

            // Horizontal bedding micro-terraces
            float terrace = 0.045f * sinf(vy * 10.5f + angle * 2.0f);
            vy += terrace;

            // Grounded flared skirt at the base
            if (vy < -0.10f) {
                float skirt = (-0.10f - vy);
                vy = -0.10f - skirt * 0.32f;
                vx *= 1.0f + skirt * 0.55f;
                vz *= 1.0f + skirt * 0.55f;
            }

            mesh.vertices[i * 3 + 0] = vx;
            mesh.vertices[i * 3 + 1] = vy;
            mesh.vertices[i * 3 + 2] = vz;

            mesh.texcoords[i * 2 + 0] *= 2.6f;
            mesh.texcoords[i * 2 + 1] *= 2.0f;
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
    Mesh mesh = Nature_BuildProceduralRockMesh(0);

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
    Mesh mesh = Nature_BuildProceduralRockMesh(1);

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

MapRockSet MapProp_CreateRiverStones(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(2);

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

// --- Unified Rock & Crag System -----------------------------------------

MapRockSystem MapProp_CreateRockSystem(const char *diffusePath, const char *normalPath, const char *roughnessPath, int capacity)
{
    MapRockSystem sys = {0};
    sys.boulders = MapProp_CreateRocks(diffusePath, normalPath, roughnessPath);
    sys.crags = MapProp_CreateMountainCrags(diffusePath, normalPath, roughnessPath);
    sys.riverStones = MapProp_CreateRiverStones(diffusePath, normalPath, roughnessPath);
    if (capacity > 0) {
        sys.items = (MapRockItem *)RL_MALLOC(capacity * sizeof(MapRockItem));
        sys.capacity = capacity;
    }
    sys.count = 0;
    sys.ready = (sys.boulders.ready || sys.crags.ready || sys.riverStones.ready);
    return sys;
}

bool MapProp_AddRockItem(MapRockSystem *sys, Vector3 pos, float rScale, float hScale, float rotDeg, MapRockArchetype archetype)
{
    if (!sys || !sys->ready || sys->count >= sys->capacity) return false;
    sys->items[sys->count++] = (MapRockItem){
        .position = pos,
        .radiusScale = rScale,
        .heightScale = hScale,
        .rotationDeg = rotDeg,
        .archetype = archetype
    };
    return true;
}

int MapProp_AddRockCluster(MapRockSystem *sys, Vector3 center, MapRockArchetype archetype,
                           int count, float clusterRadius, float baseScale, unsigned int seed)
{
    if (!sys || count <= 0) return 0;
    unsigned int rng = seed ? seed : 0x7a3b19u;
    int added = 0;

    // Primary anchor stone
    float anchorRot = Nature_Random01(&rng) * 360.0f;
    float anchorH = (archetype == MAP_ROCK_CRAG) ? (baseScale * (1.6f + 0.6f * Nature_Random01(&rng)))
                                                 : (baseScale * (0.65f + 0.25f * Nature_Random01(&rng)));
    if (MapProp_AddRockItem(sys, center, baseScale, anchorH, anchorRot, archetype)) added++;

    // Companion stones orbiting anchor
    for (int i = 1; i < count; i++) {
        float angle = Nature_Random01(&rng) * 2.0f * PI;
        float dist = clusterRadius * (0.35f + 0.65f * Nature_Random01(&rng));
        Vector3 compPos = {center.x + cosf(angle) * dist, center.y, center.z + sinf(angle) * dist};
        float companionScale = baseScale * (0.35f + 0.45f * Nature_Random01(&rng));
        float compRot = anchorRot + (Nature_Random01(&rng) - 0.5f) * 60.0f;
        float compH = (archetype == MAP_ROCK_CRAG) ? (companionScale * (1.4f + 0.5f * Nature_Random01(&rng)))
                                                   : (companionScale * (0.55f + 0.30f * Nature_Random01(&rng)));
        if (MapProp_AddRockItem(sys, compPos, companionScale, compH, compRot, archetype)) added++;
    }
    return added;
}

void MapProp_DrawRockSystem(const MapRockSystem *sys, bool drawShadows)
{
    if (!sys || !sys->ready || sys->count <= 0) return;

    PropLit_UpdateLighting();
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};

    for (int i = 0; i < sys->count; i++) {
        const MapRockItem *p = &sys->items[i];
        Model model;
        if (p->archetype == MAP_ROCK_CRAG) {
            model = sys->crags.model;
        } else if (p->archetype == MAP_ROCK_RIVER_STONE) {
            model = sys->riverStones.model;
        } else {
            model = sys->boulders.model;
        }

        Vector3 pos = {p->position.x, p->position.y - 0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        if (drawShadows && p->archetype != MAP_ROCK_CRAG) {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater) {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        Color rockTint;
        if (p->archetype == MAP_ROCK_CRAG) rockTint = (Color){152, 148, 142, 255};
        else if (p->archetype == MAP_ROCK_RIVER_STONE) rockTint = (Color){138, 142, 144, 255};
        else rockTint = (Color){156, 150, 142, 255};

        DrawModelEx(model, pos, rotAxis, p->rotationDeg, scale, rockTint);
    }
}

void MapProp_DrawRockSystemShadowCasters(MapRockSystem *sys, Shader depthShader)
{
    if (!sys || !sys->ready || sys->count <= 0) return;

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++) {
        const MapRockItem *p = &sys->items[i];
        Model model;
        if (p->archetype == MAP_ROCK_CRAG) model = sys->crags.model;
        else if (p->archetype == MAP_ROCK_RIVER_STONE) model = sys->riverStones.model;
        else model = sys->boulders.model;

        if (model.materialCount < 1) continue;
        Shader prev = model.materials[0].shader;
        model.materials[0].shader = depthShader;

        Vector3 pos = {p->position.x, p->position.y - 0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(model, pos, rotAxis, p->rotationDeg, scale, WHITE);

        rlDrawRenderBatchActive();
        model.materials[0].shader = prev;
    }
}

void MapProp_UnloadRockSystem(MapRockSystem *sys)
{
    if (!sys || !sys->ready) return;
    MapProp_UnloadRocks(&sys->boulders);
    MapProp_UnloadRocks(&sys->crags);
    MapProp_UnloadRocks(&sys->riverStones);
    if (sys->items) {
        RL_FREE(sys->items);
        sys->items = NULL;
    }
    sys->count = 0;
    sys->capacity = 0;
    sys->ready = false;
}


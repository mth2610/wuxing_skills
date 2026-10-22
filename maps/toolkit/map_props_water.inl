// Water Surface System (Terrain water, lake, strip, spline, wave field solver, dynamic ripples, and caustics).
// Split from map_props_nature.inl for clean modular domain separation.

static int s_bedLocBankPass = -2;

static void Water_SetVertex(Mesh *mesh, int index, Vector3 p, Vector2 uv, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->texcoords[index * 2 + 0] = uv.x;
    mesh->texcoords[index * 2 + 1] = uv.y;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

static void Water_SetBedVertex(Mesh *mesh, int index, Vector3 p, Vector3 n, Vector2 uv, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->normals[index * 3 + 0] = n.x;
    mesh->normals[index * 3 + 1] = n.y;
    mesh->normals[index * 3 + 2] = n.z;
    mesh->texcoords[index * 2 + 0] = uv.x;
    mesh->texcoords[index * 2 + 1] = uv.y;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = color.a;
}

static float Water_EdgeScale(float angle, float radial, unsigned int seed)
{
    float seedPhase = (float)(seed & 1023u) * 0.0173f;
    float shorelineNoise = sinf(angle * 2.0f + seedPhase) * 0.095f + cosf(angle * 3.0f - seedPhase * 0.70f) * 0.065f + sinf(angle * 5.0f + 1.2f) * 0.038f + sinf(angle * 11.0f - seedPhase * 0.40f) * 0.018f;
    float edgeWeight = radial * radial;
    edgeWeight *= edgeWeight;
    return 1.0f + shorelineNoise * edgeWeight;
}

MapWaterConfig MapProp_DefaultWaterConfig(MapWaterEcosystem eco)
{
    MapWaterConfig c = {0};
    c.shape = WATER_SHAPE_RADIAL;
    c.ecosystem = eco;
    c.maxDepth = 1.25f;
    c.waveHeight = 0.038f;
    c.waveScale = 0.95f;
    c.waveSpeed = 0.72f;
    c.detailScale = 0.10f;
    c.detailStrength = 0.16f;
    c.causticsStrength = 0.65f;
    c.causticsScale = 1.20f;
    c.foamThreshold = 0.12f;
    c.refractionStrength = 0.04f;
    c.scatterCoeff = 0.45f;
    c.foamColor = (Color){215, 230, 222, 255};
    c.bankInnerColor = (Color){52, 58, 43, 255};
    c.bankOuterColor = (Color){65, 84, 51, 255};

    switch (eco)
    {
    case WATER_ECO_ALPINE_STREAM:
        c.absorption = (Vector3){0.80f, 0.20f, 0.05f};
        c.scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
        c.deepColor = (Color){10, 38, 42, 255};
        c.shallowColor = (Color){48, 92, 85, 255};
        c.causticsStrength = 0.72f;
        break;
    case WATER_ECO_FOREST_SWAMP:
        c.absorption = (Vector3){0.30f, 0.70f, 1.30f};
        c.scatterColor = (Vector3){0.80f, 0.55f, 0.22f};
        c.deepColor = (Color){28, 20, 12, 255};
        c.shallowColor = (Color){68, 52, 32, 255};
        c.causticsStrength = 0.35f;
        c.maxDepth = 0.65f;
        break;
    case WATER_ECO_TROPICAL_SHALLOW:
        c.absorption = (Vector3){0.45f, 0.15f, 0.08f};
        c.scatterColor = (Vector3){0.15f, 0.92f, 0.88f};
        c.deepColor = (Color){12, 52, 68, 255};
        c.shallowColor = (Color){58, 122, 118, 255};
        c.causticsStrength = 0.80f;
        break;
    case WATER_ECO_STAGNANT_POND:
        c.absorption = (Vector3){0.90f, 0.60f, 0.90f};
        c.scatterColor = (Vector3){0.42f, 0.68f, 0.28f};
        c.deepColor = (Color){20, 32, 16, 255};
        c.shallowColor = (Color){50, 72, 40, 255};
        c.causticsStrength = 0.40f;
        c.maxDepth = 0.55f;
        break;
    case WATER_ECO_CUSTOM:
    default:
        c.absorption = (Vector3){0.80f, 0.20f, 0.05f};
        c.scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
        c.deepColor = (Color){10, 38, 42, 255};
        c.shallowColor = (Color){48, 92, 85, 255};
        break;
    }
    return c;
}

static void Water_ApplyConfigDefaults(MapWaterConfig *config)
{
    if (config->maxDepth <= 0.0f)
        config->maxDepth = 1.25f;
    if (config->maxDepth > 2.50f)
        config->maxDepth = 2.50f;

    if (config->absorption.x <= 0.0f && config->absorption.y <= 0.0f && config->absorption.z <= 0.0f)
    {
        MapWaterConfig def = MapProp_DefaultWaterConfig(config->ecosystem);
        config->absorption = def.absorption;
        if (config->scatterColor.x <= 0.0f && config->scatterColor.y <= 0.0f)
        {
            config->scatterColor = def.scatterColor;
        }
        if (config->scatterCoeff <= 0.0f)
            config->scatterCoeff = def.scatterCoeff;
        if (config->deepColor.a == 0)
            config->deepColor = def.deepColor;
        if (config->shallowColor.a == 0)
            config->shallowColor = def.shallowColor;
        if (config->foamColor.a == 0)
            config->foamColor = def.foamColor;
        if (config->causticsStrength <= 0.0f)
            config->causticsStrength = def.causticsStrength;
        if (config->causticsScale <= 0.0f)
            config->causticsScale = def.causticsScale;
    }
    if (config->scatterColor.x <= 0.0f && config->scatterColor.y <= 0.0f && config->scatterColor.z <= 0.0f)
    {
        config->scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
    }
    if (config->scatterCoeff <= 0.0f)
        config->scatterCoeff = 0.45f;
    if (config->causticsStrength < 0.0f)
        config->causticsStrength = 0.0f;
    if (config->causticsStrength == 0.0f)
        config->causticsStrength = 0.65f;
    if (config->causticsScale <= 0.0f)
        config->causticsScale = 1.20f;
    if (config->foamThreshold <= 0.0f)
        config->foamThreshold = 0.12f;
    if (config->waveHeight <= 0.0f)
        config->waveHeight = 0.038f;
    if (config->waveScale <= 0.0f)
        config->waveScale = 0.95f;
    if (config->waveSpeed <= 0.0f)
        config->waveSpeed = 0.72f;
    if (config->detailScale <= 0.0f)
        config->detailScale = 0.10f;
    if (config->detailStrength <= 0.0f)
        config->detailStrength = 0.16f;
}

static void Water_InitializeWaveUploadCache(MapWaterSurface *water)
{
    if (!water->waveFieldTex.id || !water->wavePixels)
        return;
    const unsigned int bytes = WATER_FIELD_SIZE * WATER_FIELD_SIZE * 4u;
    water->waveUploadedPixels = MemAlloc(bytes);
    if (!water->waveUploadedPixels)
        return;
    memcpy(water->waveUploadedPixels, water->wavePixels, bytes);
    water->waveUploadedTextureId = water->waveFieldTex.id;
}

static void Water_UploadWaveField(MapWaterSurface *water)
{
    if (!water->waveFieldTex.id || !water->wavePixels)
        return;
    const unsigned int bytes = WATER_FIELD_SIZE * WATER_FIELD_SIZE * 4u;
    if (water->waveUploadedPixels &&
        water->waveUploadedTextureId == water->waveFieldTex.id &&
        memcmp(water->waveUploadedPixels, water->wavePixels, bytes) == 0)
        return;
    UpdateTexture(water->waveFieldTex, water->wavePixels);
    if (water->waveUploadedPixels)
    {
        memcpy(water->waveUploadedPixels, water->wavePixels, bytes);
        water->waveUploadedTextureId = water->waveFieldTex.id;
    }
}

MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (config.radiusX <= 0.0f || config.radiusZ <= 0.0f)
        return water;
    if (config.segments < 24)
        config.segments = 24;
    if (config.segments > 192)
        config.segments = 192;
    if (config.rings < 2)
        config.rings = 2;
    if (config.rings > 32)
        config.rings = 32;

    Water_ApplyConfigDefaults(&config);
    config.shape = WATER_SHAPE_RADIAL;
    water.config = config;

    Mesh lakeMesh = {0};
    int lakeVertices = config.segments * config.rings * 6;
    lakeMesh.vertexCount = lakeVertices;
    lakeMesh.triangleCount = lakeVertices / 3;
    lakeMesh.vertices = MemAlloc((unsigned int)lakeVertices * 3u * sizeof(float));
    lakeMesh.texcoords = MemAlloc((unsigned int)lakeVertices * 2u * sizeof(float));
    lakeMesh.colors = MemAlloc((unsigned int)lakeVertices * 4u * sizeof(unsigned char));
    int cursor = 0;
    for (int ring = 0; ring < config.rings; ring++)
    {
        float r0 = (float)ring / config.rings;
        float r1 = (float)(ring + 1) / config.rings;
        for (int segment = 0; segment < config.segments; segment++)
        {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float e00 = Water_EdgeScale(a0, r0, config.seed);
            float e01 = Water_EdgeScale(a1, r0, config.seed);
            float e10 = Water_EdgeScale(a0, r1, config.seed);
            float e11 = Water_EdgeScale(a1, r1, config.seed);
            Vector3 p00 = {cosf(a0) * config.radiusX * r0 * e00, 0.0f, sinf(a0) * config.radiusZ * r0 * e00};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0 * e01, 0.0f, sinf(a1) * config.radiusZ * r0 * e01};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1 * e10, 0.0f, sinf(a0) * config.radiusZ * r1 * e10};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1 * e11, 0.0f, sinf(a1) * config.radiusZ * r1 * e11};
            Vector2 uv00 = {cosf(a0) * r0 * 0.5f + 0.5f, sinf(a0) * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {cosf(a1) * r0 * 0.5f + 0.5f, sinf(a1) * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {cosf(a0) * r1 * 0.5f + 0.5f, sinf(a0) * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {cosf(a1) * r1 * 0.5f + 0.5f, sinf(a1) * r1 * 0.5f + 0.5f};
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p10, uv10, WHITE);
        }
    }
    water.waterModel = Nature_ModelFromMesh(lakeMesh, Water_GetShader());
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    // ── Build 3D Concave Lake Bed ──────────────────────────────────────────
    Mesh bedMesh = {0};
    int bedVertices = config.segments * config.rings * 6;
    bedMesh.vertexCount = bedVertices;
    bedMesh.triangleCount = bedVertices / 3;
    bedMesh.vertices = MemAlloc((unsigned int)bedVertices * 3u * sizeof(float));
    bedMesh.normals = MemAlloc((unsigned int)bedVertices * 3u * sizeof(float));
    bedMesh.texcoords = MemAlloc((unsigned int)bedVertices * 2u * sizeof(float));
    bedMesh.colors = MemAlloc((unsigned int)bedVertices * 4u * sizeof(unsigned char));
    int bedCursor = 0;
    for (int ring = 0; ring < config.rings; ring++)
    {
        float r0 = (float)ring / config.rings;
        float r1 = (float)(ring + 1) / config.rings;
        float d0 = config.maxDepth * (1.0f - powf(r0, 1.6f)) + 0.015f;
        float d1 = config.maxDepth * (1.0f - powf(r1, 1.6f)) + 0.015f;
        float y0 = -d0;
        float y1 = -d1;
        float sand0 = r0 * r0 * (3.0f - 2.0f * r0);
        float sand1 = r1 * r1 * (3.0f - 2.0f * r1);
        Color c0 = Nature_LerpColor((Color){119, 101, 73, 255}, config.bankInnerColor, sand0);
        Color c1 = Nature_LerpColor((Color){119, 101, 73, 255}, config.bankInnerColor, sand1);
        for (int segment = 0; segment < config.segments; segment++)
        {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float e00 = Water_EdgeScale(a0, r0, config.seed);
            float e01 = Water_EdgeScale(a1, r0, config.seed);
            float e10 = Water_EdgeScale(a0, r1, config.seed);
            float e11 = Water_EdgeScale(a1, r1, config.seed);
            Vector3 p00 = {cosf(a0) * config.radiusX * r0 * e00, y0, sinf(a0) * config.radiusZ * r0 * e00};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0 * e01, y0, sinf(a1) * config.radiusZ * r0 * e01};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1 * e10, y1, sinf(a0) * config.radiusZ * r1 * e10};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1 * e11, y1, sinf(a1) * config.radiusZ * r1 * e11};
            Vector2 uv00 = {cosf(a0) * r0 * 0.5f + 0.5f, sinf(a0) * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {cosf(a1) * r0 * 0.5f + 0.5f, sinf(a1) * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {cosf(a0) * r1 * 0.5f + 0.5f, sinf(a0) * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {cosf(a1) * r1 * 0.5f + 0.5f, sinf(a1) * r1 * 0.5f + 0.5f};

            float slope0 = 1.6f * powf(fmaxf(0.01f, r0), 0.6f) * (config.maxDepth / fmaxf(config.radiusX, config.radiusZ));
            float slope1 = 1.6f * powf(fmaxf(0.01f, r1), 0.6f) * (config.maxDepth / fmaxf(config.radiusX, config.radiusZ));
            Vector3 n00 = Vector3Normalize((Vector3){cosf(a0) * slope0, 1.0f, sinf(a0) * slope0});
            Vector3 n01 = Vector3Normalize((Vector3){cosf(a1) * slope0, 1.0f, sinf(a1) * slope0});
            Vector3 n10 = Vector3Normalize((Vector3){cosf(a0) * slope1, 1.0f, sinf(a0) * slope1});
            Vector3 n11 = Vector3Normalize((Vector3){cosf(a1) * slope1, 1.0f, sinf(a1) * slope1});

            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p01, n01, uv01, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, c1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, c1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p10, n10, uv10, c1);
        }
    }
    water.bedModel = Nature_ModelFromMesh(bedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/dirt_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    const int bankRings = 6;
    Mesh bankMesh = Nature_AllocMesh(config.segments * bankRings * 6);
    cursor = 0;
    float bankOuterY = config.bankGroundY - config.center.y;
    for (int ring = 0; ring < bankRings; ring++)
    {
        float t0 = (float)ring / bankRings;
        float t1 = (float)(ring + 1) / bankRings;
        float s0 = t0 * t0 * (3.0f - 2.0f * t0);
        float s1 = t1 * t1 * (3.0f - 2.0f * t1);
        Color c0 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s0);
        Color c1 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s1);
        c0.a = (unsigned char)(255.0f * (1.0f - s0));
        c1.a = (unsigned char)(255.0f * (1.0f - s1));
        for (int segment = 0; segment < config.segments; segment++)
        {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float inner0 = Water_EdgeScale(a0, 1.0f, config.seed);
            float inner1 = Water_EdgeScale(a1, 1.0f, config.seed);
            float seedPhase = (float)(config.seed & 1023u) * 0.0173f;
            float habitat0 = 0.5f + 0.34f * sinf(a0 * 4.0f + seedPhase) + 0.16f * sinf(a0 * 9.0f - seedPhase * 0.61f);
            float habitat1 = 0.5f + 0.34f * sinf(a1 * 4.0f + seedPhase) + 0.16f * sinf(a1 * 9.0f - seedPhase * 0.61f);
            habitat0 = fmaxf(0.0f, fminf(1.0f, habitat0));
            habitat1 = fmaxf(0.0f, fminf(1.0f, habitat1));
            float width0 = config.bankWidth * (0.10f + 1.04f * habitat0 * habitat0);
            float width1 = config.bankWidth * (0.10f + 1.04f * habitat1 * habitat1);
            float rx00 = config.radiusX * 1.0f * inner0 + width0 * s0;
            float rz00 = config.radiusZ * 1.0f * inner0 + width0 * 0.72f * s0;
            float rx01 = config.radiusX * 1.0f * inner1 + width1 * s0;
            float rz01 = config.radiusZ * 1.0f * inner1 + width1 * 0.72f * s0;
            float rx10 = config.radiusX * 1.0f * inner0 + width0 * s1;
            float rz10 = config.radiusZ * 1.0f * inner0 + width0 * 0.72f * s1;
            float rx11 = config.radiusX * 1.0f * inner1 + width1 * s1;
            float rz11 = config.radiusZ * 1.0f * inner1 + width1 * 0.72f * s1;
            float y0 = -0.015f + (bankOuterY + 0.015f) * s0;
            float y1 = -0.015f + (bankOuterY + 0.015f) * s1;
            Vector3 p00 = {cosf(a0) * rx00, y0, sinf(a0) * rz00};
            Vector3 p01 = {cosf(a1) * rx01, y0, sinf(a1) * rz01};
            Vector3 p10 = {cosf(a0) * rx10, y1, sinf(a0) * rz10};
            Vector3 p11 = {cosf(a1) * rx11, y1, sinf(a1) * rz11};
            float shade0 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a0 * 7.0f - seedPhase * 0.7f));
            float shade1 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a1 * 7.0f - seedPhase * 0.7f));
            Color bank00 = Nature_ScaleColor(c0, 1.0f + (shade0 - 1.0f) * s0);
            Color bank01 = Nature_ScaleColor(c0, 1.0f + (shade1 - 1.0f) * s0);
            Color bank10 = Nature_ScaleColor(c1, 1.0f + (shade0 - 1.0f) * s1);
            Color bank11 = Nature_ScaleColor(c1, 1.0f + (shade1 - 1.0f) * s1);
            bank00.a = bank01.a = c0.a;
            bank10.a = bank11.a = c1.a;
            Nature_AddQuad4(&bankMesh, &cursor, p00, p01, p11, p10,
                            (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
                            bank00, bank01, bank11, bank10);
        }
    }
    water.bankModel = Nature_ModelFromMesh(bankMesh, Water_GetBedShader());
    water.bankModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;
    const int cellCount = WATER_FIELD_SIZE * WATER_FIELD_SIZE;
    water.waveHeightField = calloc((size_t)cellCount, sizeof(float));
    water.waveNextField = calloc((size_t)cellCount, sizeof(float));
    water.waveVelocityField = calloc((size_t)cellCount, sizeof(float));
    water.wavePixels = malloc((size_t)cellCount * 4u);
    if (water.waveHeightField && water.waveNextField && water.waveVelocityField && water.wavePixels)
    {
        for (int i = 0; i < cellCount; i++)
        {
            water.wavePixels[i * 4 + 0] = 128;
            water.wavePixels[i * 4 + 1] = 128;
            water.wavePixels[i * 4 + 2] = 128;
            water.wavePixels[i * 4 + 3] = 128;
        }
        Image waveImage = {
            .data = water.wavePixels,
            .width = WATER_FIELD_SIZE,
            .height = WATER_FIELD_SIZE,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        water.waveFieldTex = LoadTextureFromImage(waveImage);
        if (water.waveFieldTex.id > 0)
        {
            SetTextureFilter(water.waveFieldTex, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(water.waveFieldTex, TEXTURE_WRAP_CLAMP);
            Water_InitializeWaveUploadCache(&water);
        }
    }
    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateWaterPatch(Vector3 center, float width, float depth, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (width <= 0.0f || depth <= 0.0f)
        return water;

    Water_ApplyConfigDefaults(&config);
    config.center = center;
    config.radiusX = width * 0.5f;
    config.radiusZ = depth * 0.5f;
    config.shape = WATER_SHAPE_RECT;
    water.config = config;

    int segX = config.segments >= 8 ? config.segments : 16;
    int segZ = config.rings >= 8 ? config.rings : 16;
    int totalVerts = segX * segZ * 6;

    Mesh patchMesh = {0};
    patchMesh.vertexCount = totalVerts;
    patchMesh.triangleCount = totalVerts / 3;
    patchMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    patchMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));

    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;
    int cursor = 0;

    for (int iz = 0; iz < segZ; iz++)
    {
        float z0 = -halfD + depth * ((float)iz / (float)segZ);
        float z1 = -halfD + depth * ((float)(iz + 1) / (float)segZ);
        float v0 = (float)iz / (float)segZ;
        float v1 = (float)(iz + 1) / (float)segZ;

        for (int ix = 0; ix < segX; ix++)
        {
            float x0 = -halfW + width * ((float)ix / (float)segX);
            float x1 = -halfW + width * ((float)(ix + 1) / (float)segX);
            float u0 = (float)ix / (float)segX;
            float u1 = (float)(ix + 1) / (float)segX;

            Vector3 p00 = {x0, 0.0f, z0};
            Vector3 p10 = {x1, 0.0f, z0};
            Vector3 p11 = {x1, 0.0f, z1};
            Vector3 p01 = {x0, 0.0f, z1};

            Vector2 uv00 = {u0, v0};
            Vector2 uv10 = {u1, v0};
            Vector2 uv11 = {u1, v1};
            Vector2 uv01 = {u0, v1};

            Water_SetVertex(&patchMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p10, uv10, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p01, uv01, WHITE);
        }
    }

    water.waterModel = Nature_ModelFromMesh(patchMesh, Water_GetShader());
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    Mesh patchBedMesh = {0};
    patchBedMesh.vertexCount = totalVerts;
    patchBedMesh.triangleCount = totalVerts / 3;
    patchBedMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchBedMesh.normals = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchBedMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    patchBedMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));
    int patchBedCursor = 0;
    for (int iz = 0; iz < segZ; iz++)
    {
        float z0 = -halfD + depth * ((float)iz / (float)segZ);
        float z1 = -halfD + depth * ((float)(iz + 1) / (float)segZ);
        float v0 = (float)iz / (float)segZ;
        float v1 = (float)(iz + 1) / (float)segZ;

        for (int ix = 0; ix < segX; ix++)
        {
            float x0 = -halfW + width * ((float)ix / (float)segX);
            float x1 = -halfW + width * ((float)(ix + 1) / (float)segX);
            float u0 = (float)ix / (float)segX;
            float u1 = (float)(ix + 1) / (float)segX;

            float y00 = -config.maxDepth * sinf(PI * u0) * sinf(PI * v0) - 0.015f;
            float y10 = -config.maxDepth * sinf(PI * u1) * sinf(PI * v0) - 0.015f;
            float y11 = -config.maxDepth * sinf(PI * u1) * sinf(PI * v1) - 0.015f;
            float y01 = -config.maxDepth * sinf(PI * u0) * sinf(PI * v1) - 0.015f;

            Vector3 p00 = {x0, y00, z0};
            Vector3 p10 = {x1, y10, z0};
            Vector3 p11 = {x1, y11, z1};
            Vector3 p01 = {x0, y01, z1};
            Vector3 n = {0.0f, 1.0f, 0.0f};

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};

            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p10, n, uv10, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p01, n, uv01, WHITE);
        }
    }
    water.bedModel = Nature_ModelFromMesh(patchBedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/stone_path_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateWaterStrip(Vector3 start, Vector3 end, float width, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    Vector3 diff = Vector3Subtract(end, start);
    float length = Vector3Length(diff);
    if (length < 0.1f || width <= 0.0f)
        return water;

    Vector3 forward = Vector3Scale(diff, 1.0f / length);
    Vector3 right = (Vector3){-forward.z, 0.0f, forward.x};

    Water_ApplyConfigDefaults(&config);
    config.center = start;
    config.radiusX = length * 0.5f;
    config.radiusZ = width * 0.5f;
    config.shape = WATER_SHAPE_STRIP;

    if (config.flowVelocity.x == 0.0f && config.flowVelocity.y == 0.0f)
    {
        config.flowVelocity = (Vector2){forward.x * 0.85f, forward.z * 0.85f};
    }
    water.config = config;

    int segL = config.segments >= 4 ? config.segments : (int)(length * 2.0f);
    if (segL < 4)
        segL = 4;
    if (segL > 128)
        segL = 128;
    int segW = config.rings >= 2 ? config.rings : 4;
    int totalVerts = segL * segW * 6;

    Mesh stripMesh = {0};
    stripMesh.vertexCount = totalVerts;
    stripMesh.triangleCount = totalVerts / 3;
    stripMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    stripMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));

    float halfWidth = width * 0.5f;
    int cursor = 0;

    for (int il = 0; il < segL; il++)
    {
        float f0 = (float)il / (float)segL;
        float f1 = (float)(il + 1) / (float)segL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++)
        {
            float w0 = -halfWidth + width * ((float)iw / (float)segW);
            float w1 = -halfWidth + width * ((float)(iw + 1) / (float)segW);

            Vector3 p00 = Vector3Add(c0, Vector3Scale(right, w0));
            Vector3 p01 = Vector3Add(c0, Vector3Scale(right, w1));
            Vector3 p11 = Vector3Add(c1, Vector3Scale(right, w1));
            Vector3 p10 = Vector3Add(c1, Vector3Scale(right, w0));

            Vector2 uv00 = {f0, (float)iw / (float)segW};
            Vector2 uv01 = {f0, (float)(iw + 1) / (float)segW};
            Vector2 uv11 = {f1, (float)(iw + 1) / (float)segW};
            Vector2 uv10 = {f1, (float)iw / (float)segW};

            Water_SetVertex(&stripMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p10, uv10, WHITE);
        }
    }

    water.waterModel = Nature_ModelFromMesh(stripMesh, Water_GetShader());
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    Mesh stripBedMesh = {0};
    stripBedMesh.vertexCount = totalVerts;
    stripBedMesh.triangleCount = totalVerts / 3;
    stripBedMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripBedMesh.normals = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripBedMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    stripBedMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));
    int stripBedCursor = 0;
    for (int il = 0; il < segL; il++)
    {
        float f0 = (float)il / (float)segL;
        float f1 = (float)(il + 1) / (float)segL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++)
        {
            float w0 = -halfWidth + width * ((float)iw / (float)segW);
            float w1 = -halfWidth + width * ((float)(iw + 1) / (float)segW);
            float nw0 = w0 / halfWidth;
            float nw1 = w1 / halfWidth;
            float y0 = -config.maxDepth * (1.0f - nw0 * nw0) - 0.015f;
            float y1 = -config.maxDepth * (1.0f - nw1 * nw1) - 0.015f;

            Vector3 p00 = Vector3Add(c0, Vector3Scale(right, w0));
            p00.y = y0;
            Vector3 p01 = Vector3Add(c0, Vector3Scale(right, w1));
            p01.y = y1;
            Vector3 p11 = Vector3Add(c1, Vector3Scale(right, w1));
            p11.y = y1;
            Vector3 p10 = Vector3Add(c1, Vector3Scale(right, w0));
            p10.y = y0;
            Vector3 n = {0.0f, 1.0f, 0.0f};

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};

            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p01, n, uv01, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p10, n, uv10, WHITE);
        }
    }
    water.bedModel = Nature_ModelFromMesh(stripBedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/stone_path_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateTerrainWater(const char *meshPath, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (!meshPath || !FileExists(meshPath))
        return water;
    Water_ApplyConfigDefaults(&config);
    config.shape = WATER_SHAPE_TERRAIN;
    config.center = (Vector3){0};
    water.config = config;
    water.waterModel = LoadModel(meshPath);
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    if (water.waterModel.meshCount < 1 || water.waterModel.materialCount < 1)
    {
        UnloadModel(water.waterModel);
        return (MapWaterSurface){0};
    }
    Texture2D detail = ResourceManager_LoadTextureVariant("assets/textures/noise.png",
                                                          true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    water.causticTex = ResourceManager_LoadTextureVariant("assets/textures/water_caustics.png",
                                                          true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    for (int i = 0; i < water.waterModel.materialCount; i++)
    {
        water.waterModel.materials[i].shader = Water_GetShader();
        water.waterModel.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = detail;
    }
    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateWaterSpline(const Vector3 *points, const float *widths, int pointCount, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (!points || pointCount < 2)
        return water;

    Water_ApplyConfigDefaults(&config);
    config.center = (Vector3){0.0f, 0.0f, 0.0f};
    config.radiusX = 100.0f;
    config.radiusZ = 100.0f;
    config.shape = WATER_SHAPE_STRIP;
    water.config = config;

    int totalSegments = pointCount - 1;
    int rings = 4;
    int totalVerts = totalSegments * rings * 6;

    Mesh splineMesh = {0};
    splineMesh.vertexCount = totalVerts;
    splineMesh.triangleCount = totalVerts / 3;
    splineMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    splineMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    splineMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));

    Vector3 *tangents = MemAlloc((unsigned int)pointCount * sizeof(Vector3));
    Vector3 *normals = MemAlloc((unsigned int)pointCount * sizeof(Vector3));
    for (int i = 0; i < pointCount; i++)
    {
        Vector3 t;
        if (i == 0)
        {
            t = Vector3Subtract(points[1], points[0]);
        }
        else if (i == pointCount - 1)
        {
            t = Vector3Subtract(points[pointCount - 1], points[pointCount - 2]);
        }
        else
        {
            Vector3 t1 = Vector3Subtract(points[i], points[i - 1]);
            Vector3 t2 = Vector3Subtract(points[i + 1], points[i]);
            t = Vector3Add(Vector3Normalize(t1), Vector3Normalize(t2));
        }
        t.y = 0.0f;
        float tLen = Vector3Length(t);
        if (tLen > 1e-4f)
        {
            t = Vector3Scale(t, 1.0f / tLen);
        }
        else
        {
            t = (Vector3){0.0f, 0.0f, 1.0f};
        }
        tangents[i] = t;
        normals[i] = (Vector3){-t.z, 0.0f, t.x};
    }

    int cursor = 0;
    float cumulativeDist = 0.0f;

    for (int i = 0; i < totalSegments; i++)
    {
        Vector3 p0 = points[i];
        Vector3 p1 = points[i + 1];
        float w0 = widths ? widths[i] : 2.5f;
        float w1 = widths ? widths[i + 1] : 2.5f;
        float halfW0 = w0 * 0.5f;
        float halfW1 = w1 * 0.5f;

        Vector3 n0 = normals[i];
        Vector3 n1 = normals[i + 1];

        float segDist = Vector3Distance(p0, p1);
        float v0 = cumulativeDist * 0.25f;
        float v1 = (cumulativeDist + segDist) * 0.25f;
        cumulativeDist += segDist;

        for (int r = 0; r < rings; r++)
        {
            float fr0 = (float)r / (float)rings;
            float fr1 = (float)(r + 1) / (float)rings;
            float off0_a = -halfW0 + w0 * fr0;
            float off0_b = -halfW0 + w0 * fr1;
            float off1_a = -halfW1 + w1 * fr0;
            float off1_b = -halfW1 + w1 * fr1;

            Vector3 v00 = Vector3Add(p0, Vector3Scale(n0, off0_a));
            Vector3 v01 = Vector3Add(p0, Vector3Scale(n0, off0_b));
            Vector3 v10 = Vector3Add(p1, Vector3Scale(n1, off1_a));
            Vector3 v11 = Vector3Add(p1, Vector3Scale(n1, off1_b));

            Vector2 uv00 = {v0, fr0};
            Vector2 uv01 = {v0, fr1};
            Vector2 uv10 = {v1, fr0};
            Vector2 uv11 = {v1, fr1};

            Water_SetVertex(&splineMesh, cursor++, v00, uv00, WHITE);
            Water_SetVertex(&splineMesh, cursor++, v01, uv01, WHITE);
            Water_SetVertex(&splineMesh, cursor++, v11, uv11, WHITE);
            Water_SetVertex(&splineMesh, cursor++, v00, uv00, WHITE);
            Water_SetVertex(&splineMesh, cursor++, v11, uv11, WHITE);
            Water_SetVertex(&splineMesh, cursor++, v10, uv10, WHITE);
        }
    }

    MemFree(tangents);
    MemFree(normals);

    water.waterModel = Nature_ModelFromMesh(splineMesh, Water_GetShader());
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    water.ready = true;
    return water;
}

Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale)
{
    if (!water)
        return (Vector3){0};
    float edge = Water_EdgeScale(angleRad, 1.0f, water->config.seed);
    return (Vector3){
        water->config.center.x + cosf(angleRad) * water->config.radiusX * radialScale * edge,
        water->config.center.y,
        water->config.center.z + sinf(angleRad) * water->config.radiusZ * radialScale * edge,
    };
}

bool MapProp_SampleWaterBed(const MapWaterSurface *water, float x, float z,
                            float *outBedHeight, Vector3 *outNormal)
{
    if (!water || !water->ready)
        return false;

    if (water->config.shape == WATER_SHAPE_RADIAL)
    {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        float angle = atan2f(dz, dx);
        float edge = Water_EdgeScale(angle, 1.0f, water->config.seed);
        float rx = water->config.radiusX * edge;
        float rz = water->config.radiusZ * edge;
        if (rx <= 0.001f || rz <= 0.001f)
            return false;

        float nx = dx / rx;
        float nz = dz / rz;
        float r2 = nx * nx + nz * nz;
        if (r2 >= 1.0f)
            return false;

        float r = sqrtf(r2);
        float depth = water->config.maxDepth * (1.0f - powf(r, 1.6f));
        if (outBedHeight)
        {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal)
        {
            float slope = 1.6f * powf(fmaxf(0.01f, r), 0.6f) * (water->config.maxDepth / fmaxf(rx, rz));
            Vector3 n = {nx * slope, 1.0f, nz * slope};
            *outNormal = Vector3Normalize(n);
        }
        return true;
    }
    else if (water->config.shape == WATER_SHAPE_RECT)
    {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        float halfW = water->config.radiusX;
        float halfD = water->config.radiusZ;
        if (fabsf(dx) >= halfW || fabsf(dz) >= halfD)
            return false;

        float u = (dx + halfW) / (2.0f * halfW);
        float v = (dz + halfD) / (2.0f * halfD);
        float depth = water->config.maxDepth * sinf(PI * u) * sinf(PI * v);
        if (outBedHeight)
        {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal)
        {
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        }
        return true;
    }
    else if (water->config.shape == WATER_SHAPE_STRIP)
    {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        Vector2 flowDir = {water->config.flowVelocity.x, water->config.flowVelocity.y};
        float flowLen = Vector2Length(flowDir);
        Vector3 forward = (flowLen > 0.001f) ? (Vector3){flowDir.x / flowLen, 0.0f, flowDir.y / flowLen} : (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 right = {-forward.z, 0.0f, forward.x};

        float distLong = dx * forward.x + dz * forward.z;
        float distCross = dx * right.x + dz * right.z;
        float halfLen = water->config.radiusX;
        float halfWid = water->config.radiusZ;
        if (distLong < 0.0f || distLong > 2.0f * halfLen || fabsf(distCross) >= halfWid)
            return false;

        float w = distCross / halfWid;
        float depth = water->config.maxDepth * (1.0f - w * w);
        if (outBedHeight)
        {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal)
        {
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        }
        return true;
    }

    return false;
}

void MapProp_DrawWaterBed(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready)
        return;
    Water_UploadWaveField((MapWaterSurface *)water);
    Vector3 position = water->config.center;

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector3 sunRgb = Vector3Scale((Vector3){sun.x, sun.y, sun.z}, Environment_GetSunIntensity());
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};

    if (water->bedModel.meshCount > 0)
    {
        Shader bedShader = Water_GetBedShader();
        if (s_bedLocBankPass == -2)
            s_bedLocBankPass = GetShaderLocation(bedShader, "u_bankPass");

        BeginShaderMode(bedShader);
        SetShaderValue(bedShader, s_bedLocTime, &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocWaterHeight, &water->config.center.y, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocLightDir, &lightDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocLightColor, &sunRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocAmbientColor, &ambientRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocCausticsStrength, &water->config.causticsStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocCausticsScale, &water->config.causticsScale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocAbsorption, &water->config.absorption, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocDeepColor, &deepRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocShallowColor, &shallowRgb, SHADER_UNIFORM_VEC3);
        if (s_bedLocModelPos >= 0)
            SetShaderValue(bedShader, s_bedLocModelPos, &position, SHADER_UNIFORM_VEC3);

        Texture2D cTex = (water->causticTex.id > 0) ? water->causticTex : s_defaultCausticTex;
        rlActiveTextureSlot(1);
        rlEnableTexture(cTex.id);
        rlActiveTextureSlot(0);

        int bankPass = 0;
        if (s_bedLocBankPass >= 0)
            SetShaderValue(bedShader, s_bedLocBankPass, &bankPass, SHADER_UNIFORM_INT);
        DrawModel(water->bedModel, position, 1.0f, WHITE);
        if (water->bankModel.meshCount > 0)
        {
            rlDrawRenderBatchActive();
            bankPass = 1;
            if (s_bedLocBankPass >= 0)
                SetShaderValue(bedShader, s_bedLocBankPass, &bankPass, SHADER_UNIFORM_INT);
            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            DrawModel(water->bankModel, position, 1.0f, WHITE);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();
            bankPass = 0;
            if (s_bedLocBankPass >= 0)
                SetShaderValue(bedShader, s_bedLocBankPass, &bankPass, SHADER_UNIFORM_INT);
        }

        rlActiveTextureSlot(1);
        rlDisableTexture();
        rlActiveTextureSlot(0);
        EndShaderMode();
    }
}

void MapProp_DrawWaterOverlay(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready)
        return;
    Vector3 position = water->config.center;

    // The whole surface is one draw call, so skip it entirely when its bounding
    // sphere is outside the camera frustum (map_props_cull.inl). Conservative:
    // a surface that is even partly on screen is always drawn.
    if (water->boundsRadius > 0.0f)
    {
        MapPropViewFrustum frustum;
        if (MapProp_BuildViewFrustum(&frustum))
        {
            Vector3 boundsPos = Vector3Add(position, water->boundsCenter);
            if (MapProp_FrustumCullsSphere(&frustum, boundsPos, water->boundsRadius))
                return;
        }
    }

    Water_UploadWaveField((MapWaterSurface *)water);

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector4 foam = ColorNormalize(water->config.foamColor);
    Vector3 sunRgb = Vector3Scale((Vector3){sun.x, sun.y, sun.z}, Environment_GetSunIntensity());
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};
    Vector3 foamRgb = {foam.x, foam.y, foam.z};

    Shader shader = Water_GetShader();
    BeginShaderMode(shader);
    SetShaderValue(shader, s_waterLocTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveHeight, &water->config.waveHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveScale, &water->config.waveScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveSpeed, &water->config.waveSpeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocDetailScale, &water->config.detailScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocDetailStrength, &water->config.detailStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocFlowVelocity, &water->config.flowVelocity, SHADER_UNIFORM_VEC2);
    int shapeInt = (int)water->config.shape;
    SetShaderValue(shader, s_waterLocWaterShape, &shapeInt, SHADER_UNIFORM_INT);

    SetShaderValue(shader, s_waterLocLightDir, &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocLightColor, &sunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocAmbientColor, &ambientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_waterLocModelPos >= 0)
        SetShaderValue(shader, s_waterLocModelPos, &position, SHADER_UNIFORM_VEC3);
    Vector2 lakeRadii = {water->config.radiusX, water->config.radiusZ};
    int lakeSeed = (int)water->config.seed;
    if (s_waterLocLakeRadii >= 0)
        SetShaderValue(shader, s_waterLocLakeRadii, &lakeRadii, SHADER_UNIFORM_VEC2);
    if (s_waterLocLakeSeed >= 0)
        SetShaderValue(shader, s_waterLocLakeSeed, &lakeSeed, SHADER_UNIFORM_INT);

    SetShaderValue(shader, s_waterLocMaxDepth, &water->config.maxDepth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocAbsorption, &water->config.absorption, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocDeepColor, &deepRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocShallowColor, &shallowRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocFoamColor, &foamRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocScatterColor, &water->config.scatterColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocScatterCoeff, &water->config.scatterCoeff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocCausticsStrength, &water->config.causticsStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocCausticsScale, &water->config.causticsScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocFoamThreshold, &water->config.foamThreshold, SHADER_UNIFORM_FLOAT);
    int waveFieldEnabled = water->waveFieldTex.id > 0 ? 1 : 0;
    if (s_waterLocWaveFieldEnabled >= 0)
        SetShaderValue(shader, s_waterLocWaveFieldEnabled, &waveFieldEnabled, SHADER_UNIFORM_INT);

    ((MapWaterSurface *)water)->lastTime = time;
    Vector3 interaction = {water->interactorPos.x, water->interactorSubmerged, water->interactorPos.z};
    if (s_waterLocInteractor >= 0)
        SetShaderValue(shader, s_waterLocInteractor, &interaction, SHADER_UNIFORM_VEC3);
    if (s_waterLocVelocity >= 0)
        SetShaderValue(shader, s_waterLocVelocity, &water->interactorVel, SHADER_UNIFORM_VEC3);
    if (s_waterLocRadius >= 0)
        SetShaderValue(shader, s_waterLocRadius, &water->interactorRadius, SHADER_UNIFORM_FLOAT);

    // Pre-evaluate current expanding ring radius & decayed amplitude on CPU once per frame
    // to match water_surface.fs contract (rings.xy=pos, rings.z=maxRad, rings.w=curRad; params.x=amp, params.y=wavelength)
    Vector4 rings[MAX_WATER_RIPPLES];
    Vector4 params[MAX_WATER_RIPPLES];
    for (int i = 0; i < MAX_WATER_RIPPLES; i++)
    {
        if (water->ripples[i].active)
        {
            float age = fmaxf(0.0f, time - water->ripples[i].spawnTime);
            float maxRad = fmaxf(water->ripples[i].maxRadius, 0.1f);
            float curRad = age * water->ripples[i].speed;
            if (curRad < maxRad)
            {
                float radialFade = 1.0f - (curRad / maxRad);
                float decayedAmp = water->ripples[i].amplitude * expf(-water->ripples[i].decay * age) * radialFade;
                rings[i] = (Vector4){
                    water->ripples[i].position.x,
                    water->ripples[i].position.z,
                    maxRad,
                    curRad};
                params[i] = (Vector4){
                    decayedAmp,
                    water->ripples[i].wavelength,
                    water->ripples[i].speed,
                    water->ripples[i].decay};
            }
            else
            {
                ((MapWaterSurface *)water)->ripples[i].active = false;
                rings[i] = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
                params[i] = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
        else
        {
            rings[i] = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
            params[i] = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
    for (int i = 0; i < MAX_WATER_RIPPLES; i++)
    {
        if (s_waterLocRippleRings[i] >= 0)
            SetShaderValue(shader, s_waterLocRippleRings[i], &rings[i], SHADER_UNIFORM_VEC4);
        if (s_waterLocRippleParams[i] >= 0)
            SetShaderValue(shader, s_waterLocRippleParams[i], &params[i], SHADER_UNIFORM_VEC4);
    }
    for (int i = 0; i < MAX_WATER_OBSTACLES; i++)
    {
        Vector4 rock = i < water->obstacleCount ? water->obstacles[i] : (Vector4){0};
        if (s_waterLocObstacles[i] >= 0)
            SetShaderValue(shader, s_waterLocObstacles[i], &rock, SHADER_UNIFORM_VEC4);
    }

    Texture2D cTex = (water->causticTex.id > 0) ? water->causticTex : s_defaultCausticTex;
    rlActiveTextureSlot(1);
    rlEnableTexture(cTex.id);
    if (waveFieldEnabled)
    {
        rlActiveTextureSlot(3);
        rlEnableTexture(water->waveFieldTex.id);
    }

    Vector2 screenRes = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    if (s_waterLocResolution >= 0)
        SetShaderValue(shader, s_waterLocResolution, &screenRes, SHADER_UNIFORM_VEC2);

    bool needsSceneDepth = (water->config.shape != WATER_SHAPE_RADIAL &&
                            water->config.shape != WATER_SHAPE_TERRAIN);
    if (needsSceneDepth)
        SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, screenRes.x, screenRes.y});
    Texture2D depthTex = needsSceneDepth
                             ? SceneTargets_GetDepthTexture()
                             : (Texture2D){0};
    int hasDepth = (depthTex.id > 0) ? 1 : 0;
    SetShaderValue(shader, s_waterLocHasDepthTex, &hasDepth, SHADER_UNIFORM_INT);
    if (hasDepth)
    {
        rlActiveTextureSlot(2);
        rlEnableTexture(depthTex.id);
    }

    rlActiveTextureSlot(0);
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    DrawModel(water->waterModel, position, 1.0f, WHITE);

    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();

    if (hasDepth)
    {
        rlActiveTextureSlot(2);
        rlDisableTexture();
    }
    if (waveFieldEnabled)
    {
        rlActiveTextureSlot(3);
        rlDisableTexture();
    }
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    EndShaderMode();
}

void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time)
{
    MapProp_DrawWaterBed(water, time);
    MapProp_DrawWaterOverlay(water, time);
}

void MapProp_SetWaterInteractor(MapWaterSurface *water, Vector3 position, Vector3 velocity,
                                float radius, float submerged)
{
    if (!water || !water->ready)
        return;
    water->interactorPos = position;
    water->interactorVel = velocity;
    water->interactorRadius = radius;
    water->interactorSubmerged = submerged;
}

void MapProp_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity)
{
    if (!water || !water->ready)
        return;
    bool isStepEvent = intensity > 0.0f && intensity <= 0.65f;
    if (isStepEvent && water->lastTime - water->waveLastImpulseTime < 0.12f)
        return;
    if (isStepEvent)
        water->waveLastImpulseTime = water->lastTime;
    int idx = water->nextRipple;

    float planarSpeed = hypotf(water->interactorVel.x, water->interactorVel.z);
    bool footstep = intensity > 0.0f && intensity <= 0.65f && planarSpeed > 0.2f;
    if (footstep)
    {
        float side = (idx & 1) ? 1.0f : -1.0f;
        float footOffset = fmaxf(0.12f, water->interactorRadius * 0.48f);
        position.x += -water->interactorVel.z / planarSpeed * footOffset * side;
        position.z += water->interactorVel.x / planarSpeed * footOffset * side;
    }
    water->ripples[idx].position = position;
    water->ripples[idx].spawnTime = water->lastTime;
    water->ripples[idx].maxRadius = footstep ? fminf(radius, 1.8f) : ((radius > 0.0f) ? radius : 6.0f);
    water->ripples[idx].amplitude = (intensity > 0.0f) ? intensity : 1.0f;
    water->ripples[idx].speed = footstep ? (2.25f + 0.16f * (float)(idx & 1)) : 3.2f;
    water->ripples[idx].wavelength = footstep ? (0.42f + 0.05f * (float)(idx & 1)) : 0.52f;
    water->ripples[idx].decay = footstep ? 2.2f : 1.8f;
    water->ripples[idx].active = true;
    water->nextRipple = (water->nextRipple + 1) % MAX_WATER_RIPPLES;

    if (water->waveVelocityField)
    {
        const int n = WATER_FIELD_SIZE;
        float dx = 2.0f * water->config.radiusX / (float)(n - 1);
        float dz = 2.0f * water->config.radiusZ / (float)(n - 1);
        float sigma = footstep ? 0.22f : 0.30f;
        float impulse = footstep ? fminf(1.2f, intensity * 2.35f)
                                 : fminf(1.1f, fmaxf(0.0f, intensity) * 0.9f);
        int cx = (int)roundf((position.x - water->config.center.x + water->config.radiusX) / dx);
        int cz = (int)roundf((position.z - water->config.center.z + water->config.radiusZ) / dz);
        int reachX = (int)ceilf(3.0f * sigma / dx);
        int reachZ = (int)ceilf(3.0f * sigma / dz);
        for (int z = fmaxf(1, cz - reachZ); z <= fminf(n - 2, cz + reachZ); z++)
        {
            float wz = water->config.center.z - water->config.radiusZ + z * dz;
            for (int x = fmaxf(1, cx - reachX); x <= fminf(n - 2, cx + reachX); x++)
            {
                float wx = water->config.center.x - water->config.radiusX + x * dx;
                float nx = (wx - water->config.center.x) / water->config.radiusX;
                float nz = (wz - water->config.center.z) / water->config.radiusZ;
                if (nx * nx + nz * nz >= 0.94f)
                    continue;
                float dist2 = (wx - position.x) * (wx - position.x) +
                              (wz - position.z) * (wz - position.z);
                float q = dist2 / (2.0f * sigma * sigma);
                water->waveVelocityField[z * n + x] += impulse * expf(-q) * (1.0f - q);
            }
        }
    }
}

void MapProp_AddWaterObstacle(MapWaterSurface *water, Vector3 position, float radius, float strength)
{
    if (!water || !water->ready || water->obstacleCount >= MAX_WATER_OBSTACLES || radius <= 0.0f)
        return;
    water->obstacles[water->obstacleCount++] = (Vector4){position.x, position.z, radius, strength};
}

void MapProp_UpdateWaterSurface(MapWaterSurface *water, float dt)
{
    if (!water || !water->ready || dt <= 0.0f)
        return;

    // 1. Update wading footsteps for all water types (including WATER_SHAPE_TERRAIN)
    float motionSpeed = hypotf(water->interactorVel.x, water->interactorVel.z);
    if (water->interactorSubmerged > 0.05f && motionSpeed > 0.35f)
    {
        water->waveWakeTimer += dt;
        if (water->waveWakeTimer >= 0.24f)
        {
            water->waveWakeTimer -= 0.24f;
            MapProp_AddWaterRipple(water, water->interactorPos, 1.8f, 0.48f);
        }
    }
    else
    {
        water->waveWakeTimer = 0.12f;
    }

    // 2. 128x128 2D Wave Equation Solver (only for lakes with waveFieldTex)
    if (!water->waveFieldTex.id || !water->waveHeightField)
        return;

    const int n = WATER_FIELD_SIZE;
    const float step = 1.0f / 60.0f;
    const float dx = 2.0f * water->config.radiusX / (float)(n - 1);
    const float dz = 2.0f * water->config.radiusZ / (float)(n - 1);
    const float invDx2 = 1.0f / (dx * dx);
    const float invDz2 = 1.0f / (dz * dz);

    water->waveStepRemainder = fminf(water->waveStepRemainder + dt, 3.0f * step);
    bool stepped = false;
    float maxEnergy = 0.0f;

    while (water->waveStepRemainder >= step)
    {
        water->waveStepRemainder -= step;
        stepped = true;
        maxEnergy = 0.0f;
        for (int z = 1; z < n - 1; z++)
        {
            float nz = 2.0f * (float)z / (float)(n - 1) - 1.0f;
            for (int x = 1; x < n - 1; x++)
            {
                int i = z * n + x;
                float nx = 2.0f * (float)x / (float)(n - 1) - 1.0f;
                float radial2 = nx * nx + nz * nz;
                bool blocked = radial2 >= 0.94f;
                if (!blocked && water->obstacleCount > 0)
                {
                    float wx = water->config.center.x + nx * water->config.radiusX;
                    float wz = water->config.center.z + nz * water->config.radiusZ;
                    for (int k = 0; k < water->obstacleCount; k++)
                    {
                        float ox = wx - water->obstacles[k].x;
                        float oz = wz - water->obstacles[k].y;
                        if (ox * ox + oz * oz < water->obstacles[k].z * water->obstacles[k].z)
                        {
                            blocked = true;
                            break;
                        }
                    }
                }
                if (blocked)
                {
                    water->waveNextField[i] = 0.0f;
                    water->waveVelocityField[i] = 0.0f;
                    continue;
                }
                float h = water->waveHeightField[i];
                float lap = (water->waveHeightField[i - 1] + water->waveHeightField[i + 1] - 2.0f * h) * invDx2 +
                            (water->waveHeightField[i - n] + water->waveHeightField[i + n] - 2.0f * h) * invDz2;
                float speedSquared = (2.35f * 2.35f) * fmaxf(0.08f, 1.0f - radial2);
                float shoreDamping = 1.4f + 4.0f * fmaxf(0.0f, radial2 - 0.70f);
                float velocity = (water->waveVelocityField[i] + speedSquared * lap * step) *
                                 fmaxf(0.0f, 1.0f - shoreDamping * step);
                float nextH = fmaxf(-0.12f, fminf(0.12f, h + velocity * step));
                water->waveVelocityField[i] = velocity;
                water->waveNextField[i] = nextH;
                float cellEnergy = fabsf(nextH) + fabsf(velocity);
                if (cellEnergy > maxEnergy)
                    maxEnergy = cellEnergy;
            }
        }
        float *old = water->waveHeightField;
        water->waveHeightField = water->waveNextField;
        water->waveNextField = old;
        if (maxEnergy < 1e-5f)
            break;
    }

    // Only repack 128x128 RGBA8 pixels if a 60Hz PDE step ran and the field has active energy
    if (!stepped || maxEnergy < 1e-6f)
        return;

    float inv2Dx = 1.0f / (2.0f * dx);
    float inv2Dz = 1.0f / (2.0f * dz);
    for (int z = 0; z < n; z++)
    {
        for (int x = 0; x < n; x++)
        {
            int i = z * n + x;
            int xl = x > 0 ? i - 1 : i;
            int xr = x < n - 1 ? i + 1 : i;
            int zb = z > 0 ? i - n : i;
            int zf = z < n - 1 ? i + n : i;
            float sx = (water->waveHeightField[xr] - water->waveHeightField[xl]) * inv2Dx;
            float sz = (water->waveHeightField[zf] - water->waveHeightField[zb]) * inv2Dz;
            float h = water->waveHeightField[i];
            water->wavePixels[i * 4 + 0] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + sx)));
            water->wavePixels[i * 4 + 1] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + sz)));
            water->wavePixels[i * 4 + 2] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + h * 4.0f)));
            float lap = (water->waveHeightField[xl] + water->waveHeightField[xr] - 2.0f * h) * invDx2 +
                        (water->waveHeightField[zb] + water->waveHeightField[zf] - 2.0f * h) * invDz2;
            water->wavePixels[i * 4 + 3] = (unsigned char)(255.0f *
                                                           fminf(1.0f, fmaxf(0.0f, 0.5f - lap * 0.035f)));
        }
    }
}

void MapProp_UnloadWaterSurface(MapWaterSurface *water)
{
    if (!water || !water->ready)
        return;
    UnloadModel(water->waterModel);
    if (water->bankModel.meshCount > 0)
    {
        UnloadModel(water->bankModel);
    }
    if (water->bedModel.meshCount > 0)
    {
        UnloadModel(water->bedModel);
    }
    if (water->waveFieldTex.id > 0)
        UnloadTexture(water->waveFieldTex);
    free(water->waveHeightField);
    free(water->waveNextField);
    free(water->waveVelocityField);
    free(water->wavePixels);
    MemFree(water->waveUploadedPixels);
    water->waveUploadedPixels = NULL;
    water->waveUploadedTextureId = 0;
    water->ready = false;
}
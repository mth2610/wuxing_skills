// Water Surface System (Terrain water, lake, strip, spline, wave field solver, dynamic ripples, and caustics).
// Split from map_props_nature.inl for clean modular domain separation.

static int s_bedLocBankPass = -2;

// Cache các uniform môi trường dùng chung trong cùng 1 frame khi có nhiều mặt nước/suối
static unsigned int s_waterEnvFrameId = 0;
static Vector3 s_waterCachedLightDir = {0};
static Vector3 s_waterCachedSunRgb = {0};
static Vector3 s_waterCachedAmbientRgb = {0};

static inline void Water_RefreshFrameLightingCache(void)
{
    if (s_waterEnvFrameId == s_natureRenderFrameId)
        return;
    s_waterEnvFrameId = s_natureRenderFrameId;
    s_waterCachedLightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    float sunIntensity = Environment_GetSunIntensity();
    s_waterCachedSunRgb = (Vector3){sun.x * sunIntensity, sun.y * sunIntensity, sun.z * sunIntensity};
    s_waterCachedAmbientRgb = (Vector3){ambient.x, ambient.y, ambient.z};
}

static inline unsigned char Water_EncodeUnorm8(float v)
{
    v = (v > 0.0f) ? ((v < 1.0f) ? v : 1.0f) : 0.0f;
    return (unsigned char)(v * 255.0f + 0.5f);
}

static inline void Water_SetVertex(Mesh *mesh, int index, Vector3 p, Vector2 uv, Color color)
{
    int v3 = index * 3;
    int v2 = index * 2;
    int v4 = index * 4;
    mesh->vertices[v3 + 0] = p.x;
    mesh->vertices[v3 + 1] = p.y;
    mesh->vertices[v3 + 2] = p.z;
    mesh->texcoords[v2 + 0] = uv.x;
    mesh->texcoords[v2 + 1] = uv.y;
    mesh->colors[v4 + 0] = color.r;
    mesh->colors[v4 + 1] = color.g;
    mesh->colors[v4 + 2] = color.b;
    mesh->colors[v4 + 3] = 255;
}

static inline void Water_SetBedVertex(Mesh *mesh, int index, Vector3 p, Vector3 n, Vector2 uv, Color color)
{
    int v3 = index * 3;
    int v2 = index * 2;
    int v4 = index * 4;
    mesh->vertices[v3 + 0] = p.x;
    mesh->vertices[v3 + 1] = p.y;
    mesh->vertices[v3 + 2] = p.z;
    mesh->normals[v3 + 0] = n.x;
    mesh->normals[v3 + 1] = n.y;
    mesh->normals[v3 + 2] = n.z;
    mesh->texcoords[v2 + 0] = uv.x;
    mesh->texcoords[v2 + 1] = uv.y;
    mesh->colors[v4 + 0] = color.r;
    mesh->colors[v4 + 1] = color.g;
    mesh->colors[v4 + 2] = color.b;
    mesh->colors[v4 + 3] = color.a;
}

static inline float Water_EdgeBonus(float angle, unsigned int seed)
{
    float seedPhase = (float)(seed & 1023u) * 0.0173f;
    float s2, c2, s3, c3, s5, c5, s11, c11;
    Nature_FastSinCos(angle * 2.0f + seedPhase, &s2, &c2);
    Nature_FastSinCos(angle * 3.0f - seedPhase * 0.70f, &s3, &c3);
    Nature_FastSinCos(angle * 5.0f + 1.2f, &s5, &c5);
    Nature_FastSinCos(angle * 11.0f - seedPhase * 0.40f, &s11, &c11);
    return s2 * 0.095f +
           c3 * 0.065f +
           s5 * 0.038f +
           s11 * 0.018f;
}

static float Water_EdgeScale(float angle, float radial, unsigned int seed)
{
    float edgeWeight = radial * radial;
    edgeWeight *= edgeWeight;
    return 1.0f + Water_EdgeBonus(angle, seed) * edgeWeight;
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

// Sử dụng bộ đệm waveNextField (vốn chỉ dùng làm scratch khi dịch chuyển lưới)
// để lưu cache mặt nạ bờ & vật cản tĩnh, loại bỏ hoàn toàn vòng lặp kiểm tra vật cản mỗi frame.
static void Water_RebuildBlockedMask(MapWaterSurface *water)
{
    if (!water || !water->waveNextField)
        return;
    const int n = WATER_FIELD_SIZE;
    float radX = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusX;
    float radZ = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusZ;
    Vector3 center = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridCenter : water->config.center;
    const float invNMinus1 = 2.0f / (float)(n - 1);
    const bool radialShape = (water->config.shape == WATER_SHAPE_RADIAL);

    int obsCount = water->obstacleCount;
    if (obsCount > MAX_WATER_OBSTACLES)
        obsCount = MAX_WATER_OBSTACLES;
    float obsX[MAX_WATER_OBSTACLES];
    float obsZ[MAX_WATER_OBSTACLES];
    float obsRadSq[MAX_WATER_OBSTACLES];
    for (int k = 0; k < obsCount; k++)
    {
        obsX[k] = water->obstacles[k].x;
        obsZ[k] = water->obstacles[k].y;
        obsRadSq[k] = water->obstacles[k].z * water->obstacles[k].z;
    }

    for (int z = 0; z < n; z++)
    {
        float nz = (float)z * invNMinus1 - 1.0f;
        float nz2 = nz * nz;
        float wz = center.z + nz * radZ;
        int row = z * n;
        for (int x = 0; x < n; x++)
        {
            float nx = (float)x * invNMinus1 - 1.0f;
            float nx2 = nx * nx;
            float distMetric = radialShape ? (nx2 + nz2) : fmaxf(fabsf(nx), fabsf(nz));
            bool blocked = (distMetric >= 0.94f);
            if (!blocked && obsCount > 0)
            {
                float wx = center.x + nx * radX;
                for (int k = 0; k < obsCount; k++)
                {
                    float ox = wx - obsX[k];
                    float oz = wz - obsZ[k];
                    if (ox * ox + oz * oz < obsRadSq[k])
                    {
                        blocked = true;
                        break;
                    }
                }
            }
            water->waveNextField[row + x] = blocked ? 1.0f : 0.0f;
        }
    }
}

static void Water_InitializeWaveField(MapWaterSurface *water)
{
    const int cellCount = WATER_FIELD_SIZE * WATER_FIELD_SIZE;
    water->waveHeightField = calloc((size_t)cellCount, sizeof(float));
    water->waveNextField = calloc((size_t)cellCount, sizeof(float));
    water->waveVelocityField = calloc((size_t)cellCount, sizeof(float));
    water->bedHeight = calloc((size_t)cellCount, sizeof(float));
    water->waterDepth = calloc((size_t)cellCount, sizeof(float));
    water->smoothEta = calloc((size_t)cellCount, sizeof(float));
    water->fluxL = calloc((size_t)cellCount, sizeof(float));
    water->fluxR = calloc((size_t)cellCount, sizeof(float));
    water->fluxT = calloc((size_t)cellCount, sizeof(float));
    water->fluxB = calloc((size_t)cellCount, sizeof(float));
    water->velU = calloc((size_t)cellCount, sizeof(float));
    water->velV = calloc((size_t)cellCount, sizeof(float));
    water->wavePixels = malloc((size_t)cellCount * 4u);
    if (water->waveHeightField && water->waveNextField && water->waveVelocityField &&
        water->bedHeight && water->waterDepth && water->smoothEta &&
        water->fluxL && water->fluxR && water->fluxT && water->fluxB &&
        water->velU && water->velV && water->wavePixels)
    {
        float baseDepth = (water->config.maxDepth > 0.05f) ? water->config.maxDepth : 0.85f;

        for (int idx = 0; idx < cellCount; idx++)
        {
            water->bedHeight[idx] = -baseDepth;
            water->waterDepth[idx] = baseDepth;
            water->smoothEta[idx] = 0.0f;
            water->velU[idx] = 0.0f;
            water->velV[idx] = 0.0f;
        }
        water->waveNextField[0] = -1.0f; // Đánh dấu cần khởi tạo mặt nạ bờ/vật cản

        memset(water->wavePixels, 128, (size_t)cellCount * 4u);
        for (int p = 0; p < cellCount; p++)
            water->wavePixels[p * 4 + 3] = 0;
        Image waveImage = {
            .data = water->wavePixels,
            .width = WATER_FIELD_SIZE,
            .height = WATER_FIELD_SIZE,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        water->waveFieldTex = LoadTextureFromImage(waveImage);
        if (water->waveFieldTex.id > 0)
        {
            SetTextureFilter(water->waveFieldTex, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(water->waveFieldTex, TEXTURE_WRAP_CLAMP);
            Water_InitializeWaveUploadCache(water);
        }
        water->waveStepRemainder = -1.0f;
    }
    else
    {
        free(water->waveHeightField);
        free(water->waveNextField);
        free(water->waveVelocityField);
        free(water->bedHeight);
        free(water->waterDepth);
        free(water->smoothEta);
        free(water->fluxL);
        free(water->fluxR);
        free(water->fluxT);
        free(water->fluxB);
        free(water->velU);
        free(water->velV);
        free(water->wavePixels);
        water->waveHeightField = NULL;
        water->waveNextField = NULL;
        water->waveVelocityField = NULL;
        water->bedHeight = NULL;
        water->waterDepth = NULL;
        water->smoothEta = NULL;
        water->fluxL = NULL;
        water->fluxR = NULL;
        water->fluxT = NULL;
        water->fluxB = NULL;
        water->velU = NULL;
        water->velV = NULL;
        water->wavePixels = NULL;
    }
}

static void Water_UploadWaveField(MapWaterSurface *water)
{
    if (!water->waveFieldTex.id || !water->wavePixels)
        return;
    if (water->waveUploadedPixels && water->waveUploadedTextureId == water->waveFieldTex.id)
        return;

    const unsigned int bytes = WATER_FIELD_SIZE * WATER_FIELD_SIZE * 4u;
    if (water->waveUploadedPixels && water->waveUploadedTextureId == 0 &&
        memcmp(water->waveUploadedPixels, water->wavePixels, bytes) == 0)
    {
        water->waveUploadedTextureId = water->waveFieldTex.id;
        return;
    }
    UpdateTexture(water->waveFieldTex, water->wavePixels);
    if (water->waveUploadedPixels)
    {
        memcpy(water->waveUploadedPixels, water->wavePixels, bytes);
        water->waveUploadedTextureId = water->waveFieldTex.id;
    }
}

// ---------------------------------------------------------------------------
// Terrain hydrology (SWE on the real DEM bed + rain/runoff)
// Units: metres / seconds, but rain is time-compressed so a shower is visible in game time:
//   intensity 1.0 (~65 mm/h real) -> 5.4 mm/s of depth on every cell.
// ---------------------------------------------------------------------------
#define WATER_RAIN_GAIN_MPS 0.0054f // depth added per second at rain intensity 1.0
#define WATER_INFIL_MPS 0.0036f     // dry-soil infiltration; falls to 25% as the ground saturates
#define WATER_STORM_LIFT_M 0.16f    // max stream swell at runoff 1.5 (mesh + equilibrium depth)
#define WATER_FLOW_ENC 0.25f        // flow texture: 0.5 + v * 0.25  => +-2 m/s range

static inline float Water_StormLift(void)
{
    return WATER_STORM_LIFT_M * fminf(MapProp_GetRunoff(), 1.5f);
}

static void Water_TerrainRefreshCells(MapWaterSurface *w, int i0, int i1, int j0, int j1, bool resetDepth)
{
    const int n = WATER_FIELD_SIZE;
    const float dx = 2.0f * w->waveGridRadius / (float)(n - 1);
    const float x0 = w->waveGridCenter.x - w->waveGridRadius;
    const float z0 = w->waveGridCenter.z - w->waveGridRadius;
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    if (i1 > n) i1 = n;
    if (j1 > n) j1 = n;
    for (int j = j0; j < j1; j++)
    {
        const float wz = z0 + (float)j * dx;
        for (int i = i0; i < i1; i++)
        {
            const float wx = x0 + (float)i * dx;
            const int idx = j * n + i;
            const float bed = w->sampleBed(wx, wz, w->terrainCtx);
            const float surf = w->sampleSurface ? w->sampleSurface(wx, wz, w->terrainCtx) : -1e9f;
            const float eq = (surf > -1e5f && surf > bed + 0.005f) ? (surf - bed) : 0.0f;
            w->bedHeight[idx] = bed;
            w->eqDepth[idx] = eq;
            if (resetDepth)
            {
                w->waterDepth[idx] = eq;
                w->fluxL[idx] = w->fluxR[idx] = w->fluxT[idx] = w->fluxB[idx] = 0.0f;
                w->velU[idx] = w->velV[idx] = 0.0f;
            }
        }
    }
}

static void Water_TerrainRecountWater(MapWaterSurface *w)
{
    const int cells = WATER_FIELD_SIZE * WATER_FIELD_SIZE;
    bool any = false;
    for (int i = 0; i < cells && !any; i++)
        any = (w->eqDepth[i] > 0.02f);
    w->windowHasWater = any;
}

// Pipe driving head between two cells. Inside the baked river both cells are measured against
// their steady depth, so the river at rest exerts no force (its natural slope is carried by the
// analytic base current instead). Any pair involving land uses the true surface bed + depth, so
// rain runoff still flows downhill and spills into / out of the channel.
static inline float Water_DeltaEta(const MapWaterSurface *w, int a, int b, bool hydro)
{
    const float ha = w->waterDepth[a], hb = w->waterDepth[b];
    if (hydro)
    {
        const float ea = w->eqDepth[a], eb = w->eqDepth[b];
        if (ea > 0.0f && eb > 0.0f)
            return (ha - ea) - (hb - eb);
        // Channel -> bank: the coarse water mask leaves some bank cells lower than the river surface.
        // Clamp the bank's effective bed to the river's steady surface so resting water never pours
        // onto dry land as a waterfall, while genuine swells (ha > ea) still spill naturally.
        if (ea > 0.0f && eb <= 0.0f)
        {
            const float bankBed = fmaxf(w->bedHeight[b], w->bedHeight[a] + ea);
            return (w->bedHeight[a] + ha) - (bankBed + hb);
        }
    }
    return (w->bedHeight[a] + ha) - (w->bedHeight[b] + hb);
}

// Baked steady water surface at a cell (falls back to the centre cell's when the neighbour is land)
static inline float Water_BakedSurf(const MapWaterSurface *w, int k, float fallback)
{
    return (w->eqDepth[k] > 0.0f) ? (w->bedHeight[k] + w->eqDepth[k]) : fallback;
}

void MapProp_SetWaterTerrainSource(MapWaterSurface *water,
                                   float (*sampleBed)(float x, float z, void *ctx),
                                   float (*sampleSurface)(float x, float z, void *ctx),
                                   void *ctx)
{
    if (!water || !water->ready || water->config.shape != WATER_SHAPE_TERRAIN ||
        !sampleBed || !water->waterDepth)
        return;
    const int cells = WATER_FIELD_SIZE * WATER_FIELD_SIZE;
    if (!water->eqDepth)
        water->eqDepth = calloc((size_t)cells, sizeof(float));
    if (!water->flowPixels)
    {
        water->flowPixels = malloc((size_t)cells * 4u);
        if (water->flowPixels)
        {
            memset(water->flowPixels, 128, (size_t)cells * 4u);
            for (int p = 0; p < cells; p++)
                water->flowPixels[p * 4 + 3] = 0;
            Image img = {.data = water->flowPixels,
                         .width = WATER_FIELD_SIZE,
                         .height = WATER_FIELD_SIZE,
                         .mipmaps = 1,
                         .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            water->flowFieldTex = LoadTextureFromImage(img);
            if (water->flowFieldTex.id > 0)
            {
                SetTextureFilter(water->flowFieldTex, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(water->flowFieldTex, TEXTURE_WRAP_CLAMP);
            }
        }
    }
    if (!water->eqDepth || !water->flowPixels)
        return;
    water->sampleBed = sampleBed;
    water->sampleSurface = sampleSurface;
    water->terrainCtx = ctx;
    if (water->manning <= 0.0f)
        water->manning = 0.045f;
    if (water->rngState == 0u)
        water->rngState = 0x9E3779B9u;
    water->hydrologyReady = true;
    water->hydroNeedsFullRefresh = true;
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

    float cosTable[193];
    float sinTable[193];
    float edgeBonusTable[193];
    const float invSegments = 1.0f / (float)config.segments;
    for (int s = 0; s <= config.segments; s++)
    {
        float a = (float)s * (2.0f * PI) * invSegments;
        Nature_FastSinCos(a, &sinTable[s], &cosTable[s]);
        edgeBonusTable[s] = Water_EdgeBonus(a, config.seed);
    }

    Mesh lakeMesh = {0};
    int lakeVertices = config.segments * config.rings * 6;
    lakeMesh.vertexCount = lakeVertices;
    lakeMesh.triangleCount = lakeVertices / 3;
    lakeMesh.vertices = MemAlloc((unsigned int)lakeVertices * 3u * sizeof(float));
    lakeMesh.texcoords = MemAlloc((unsigned int)lakeVertices * 2u * sizeof(float));
    lakeMesh.colors = MemAlloc((unsigned int)lakeVertices * 4u * sizeof(unsigned char));
    int cursor = 0;
    const float invRings = 1.0f / (float)config.rings;
    for (int ring = 0; ring < config.rings; ring++)
    {
        float r0 = (float)ring * invRings;
        float r1 = (float)(ring + 1) * invRings;
        float w0 = r0 * r0;
        w0 *= w0;
        float w1 = r1 * r1;
        w1 *= w1;
        for (int segment = 0; segment < config.segments; segment++)
        {
            float c0 = cosTable[segment], s0 = sinTable[segment];
            float c1 = cosTable[segment + 1], s1 = sinTable[segment + 1];
            float eb0 = edgeBonusTable[segment], eb1 = edgeBonusTable[segment + 1];
            float e00 = 1.0f + eb0 * w0;
            float e01 = 1.0f + eb1 * w0;
            float e10 = 1.0f + eb0 * w1;
            float e11 = 1.0f + eb1 * w1;
            Vector3 p00 = {c0 * config.radiusX * r0 * e00, 0.0f, s0 * config.radiusZ * r0 * e00};
            Vector3 p01 = {c1 * config.radiusX * r0 * e01, 0.0f, s1 * config.radiusZ * r0 * e01};
            Vector3 p10 = {c0 * config.radiusX * r1 * e10, 0.0f, s0 * config.radiusZ * r1 * e10};
            Vector3 p11 = {c1 * config.radiusX * r1 * e11, 0.0f, s1 * config.radiusZ * r1 * e11};
            Vector2 uv00 = {c0 * r0 * 0.5f + 0.5f, s0 * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {c1 * r0 * 0.5f + 0.5f, s1 * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {c0 * r1 * 0.5f + 0.5f, s0 * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {c1 * r1 * 0.5f + 0.5f, s1 * r1 * 0.5f + 0.5f};
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
    const float invMaxRad = config.maxDepth / fmaxf(config.radiusX, config.radiusZ);
    for (int ring = 0; ring < config.rings; ring++)
    {
        float r0 = (float)ring * invRings;
        float r1 = (float)(ring + 1) * invRings;
        float w0 = r0 * r0;
        w0 *= w0;
        float w1 = r1 * r1;
        w1 *= w1;
        float d0 = config.maxDepth * (1.0f - powf(r0, 1.6f)) + 0.015f;
        float d1 = config.maxDepth * (1.0f - powf(r1, 1.6f)) + 0.015f;
        float y0 = -d0;
        float y1 = -d1;
        float sand0 = r0 * r0 * (3.0f - 2.0f * r0);
        float sand1 = r1 * r1 * (3.0f - 2.0f * r1);
        Color col0 = Nature_LerpColor((Color){119, 101, 73, 255}, config.bankInnerColor, sand0);
        Color col1 = Nature_LerpColor((Color){119, 101, 73, 255}, config.bankInnerColor, sand1);

        float slope0 = 1.6f * powf(fmaxf(0.01f, r0), 0.6f) * invMaxRad;
        float slope1 = 1.6f * powf(fmaxf(0.01f, r1), 0.6f) * invMaxRad;
        float invNorm0 = Nature_FastInvSqrt(slope0 * slope0 + 1.0f);
        float invNorm1 = Nature_FastInvSqrt(slope1 * slope1 + 1.0f);

        for (int segment = 0; segment < config.segments; segment++)
        {
            float c0 = cosTable[segment], s0 = sinTable[segment];
            float c1 = cosTable[segment + 1], s1 = sinTable[segment + 1];
            float eb0 = edgeBonusTable[segment], eb1 = edgeBonusTable[segment + 1];
            float e00 = 1.0f + eb0 * w0;
            float e01 = 1.0f + eb1 * w0;
            float e10 = 1.0f + eb0 * w1;
            float e11 = 1.0f + eb1 * w1;
            Vector3 p00 = {c0 * config.radiusX * r0 * e00, y0, s0 * config.radiusZ * r0 * e00};
            Vector3 p01 = {c1 * config.radiusX * r0 * e01, y0, s1 * config.radiusZ * r0 * e01};
            Vector3 p10 = {c0 * config.radiusX * r1 * e10, y1, s0 * config.radiusZ * r1 * e10};
            Vector3 p11 = {c1 * config.radiusX * r1 * e11, y1, s1 * config.radiusZ * r1 * e11};
            Vector2 uv00 = {c0 * r0 * 0.5f + 0.5f, s0 * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {c1 * r0 * 0.5f + 0.5f, s1 * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {c0 * r1 * 0.5f + 0.5f, s0 * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {c1 * r1 * 0.5f + 0.5f, s1 * r1 * 0.5f + 0.5f};

            Vector3 n00 = {c0 * slope0 * invNorm0, invNorm0, s0 * slope0 * invNorm0};
            Vector3 n01 = {c1 * slope0 * invNorm0, invNorm0, s1 * slope0 * invNorm0};
            Vector3 n10 = {c0 * slope1 * invNorm1, invNorm1, s0 * slope1 * invNorm1};
            Vector3 n11 = {c1 * slope1 * invNorm1, invNorm1, s1 * slope1 * invNorm1};

            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, col0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p01, n01, uv01, col0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, col1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, col0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, col1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p10, n10, uv10, col1);
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
    float seedPhase = (float)(config.seed & 1023u) * 0.0173f;
    for (int ring = 0; ring < bankRings; ring++)
    {
        float t0 = (float)ring / (float)bankRings;
        float t1 = (float)(ring + 1) / (float)bankRings;
        float s0 = t0 * t0 * (3.0f - 2.0f * t0);
        float s1 = t1 * t1 * (3.0f - 2.0f * t1);
        Color col0 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s0);
        Color col1 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s1);
        col0.a = (unsigned char)(255.0f * (1.0f - s0));
        col1.a = (unsigned char)(255.0f * (1.0f - s1));
        for (int segment = 0; segment < config.segments; segment++)
        {
            float a0 = (float)segment * (2.0f * PI) * invSegments;
            float a1 = (float)(segment + 1) * (2.0f * PI) * invSegments;
            float c0 = cosTable[segment], sin0 = sinTable[segment];
            float c1 = cosTable[segment + 1], sin1 = sinTable[segment + 1];
            float inner0 = 1.0f + edgeBonusTable[segment];
            float inner1 = 1.0f + edgeBonusTable[segment + 1];
            float sh0_4, ch0_4, sh0_9, ch0_9, sh1_4, ch1_4, sh1_9, ch1_9;
            Nature_FastSinCos(a0 * 4.0f + seedPhase, &sh0_4, &ch0_4);
            Nature_FastSinCos(a0 * 9.0f - seedPhase * 0.61f, &sh0_9, &ch0_9);
            Nature_FastSinCos(a1 * 4.0f + seedPhase, &sh1_4, &ch1_4);
            Nature_FastSinCos(a1 * 9.0f - seedPhase * 0.61f, &sh1_9, &ch1_9);
            float habitat0 = fmaxf(0.0f, fminf(1.0f, 0.5f + 0.34f * sh0_4 + 0.16f * sh0_9));
            float habitat1 = fmaxf(0.0f, fminf(1.0f, 0.5f + 0.34f * sh1_4 + 0.16f * sh1_9));
            float width0 = config.bankWidth * (0.10f + 1.04f * habitat0 * habitat0);
            float width1 = config.bankWidth * (0.10f + 1.04f * habitat1 * habitat1);
            float rx00 = config.radiusX * inner0 + width0 * s0;
            float rz00 = config.radiusZ * inner0 + width0 * 0.72f * s0;
            float rx01 = config.radiusX * inner1 + width1 * s0;
            float rz01 = config.radiusZ * inner1 + width1 * 0.72f * s0;
            float rx10 = config.radiusX * inner0 + width0 * s1;
            float rz10 = config.radiusZ * inner0 + width0 * 0.72f * s1;
            float rx11 = config.radiusX * inner1 + width1 * s1;
            float rz11 = config.radiusZ * inner1 + width1 * 0.72f * s1;
            float y0 = -0.015f + (bankOuterY + 0.015f) * s0;
            float y1 = -0.015f + (bankOuterY + 0.015f) * s1;
            Vector3 p00 = {c0 * rx00, y0, sin0 * rz00};
            Vector3 p01 = {c1 * rx01, y0, sin1 * rz01};
            Vector3 p10 = {c0 * rx10, y1, sin0 * rz10};
            Vector3 p11 = {c1 * rx11, y1, sin1 * rz11};
            float ss0, cs0, ss1, cs1;
            Nature_FastSinCos(a0 * 7.0f - seedPhase * 0.7f, &ss0, &cs0);
            Nature_FastSinCos(a1 * 7.0f - seedPhase * 0.7f, &ss1, &cs1);
            float shade0 = 0.88f + 0.16f * (0.5f + 0.5f * ss0);
            float shade1 = 0.88f + 0.16f * (0.5f + 0.5f * ss1);
            Color bank00 = Nature_ScaleColor(col0, 1.0f + (shade0 - 1.0f) * s0);
            Color bank01 = Nature_ScaleColor(col0, 1.0f + (shade1 - 1.0f) * s0);
            Color bank10 = Nature_ScaleColor(col1, 1.0f + (shade0 - 1.0f) * s1);
            Color bank11 = Nature_ScaleColor(col1, 1.0f + (shade1 - 1.0f) * s1);
            bank00.a = bank01.a = col0.a;
            bank10.a = bank11.a = col1.a;
            Nature_AddQuad4(&bankMesh, &cursor, p00, p01, p11, p10,
                            (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
                            bank00, bank01, bank11, bank10);
        }
    }
    water.bankModel = Nature_ModelFromMesh(bankMesh, Water_GetBedShader());
    water.bankModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;
    water.waveGridCenter = config.center;
    water.waveGridRadius = fmaxf(config.radiusX, config.radiusZ);
    Water_InitializeWaveField(&water);
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
    float invSegX = 1.0f / (float)segX;
    float invSegZ = 1.0f / (float)segZ;
    int cursor = 0;

    for (int iz = 0; iz < segZ; iz++)
    {
        float v0 = (float)iz * invSegZ;
        float v1 = (float)(iz + 1) * invSegZ;
        float z0 = -halfD + depth * v0;
        float z1 = -halfD + depth * v1;

        for (int ix = 0; ix < segX; ix++)
        {
            float u0 = (float)ix * invSegX;
            float u1 = (float)(ix + 1) * invSegX;
            float x0 = -halfW + width * u0;
            float x1 = -halfW + width * u1;

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
    const float slopeScaleX = config.maxDepth * PI / width;
    const float slopeScaleZ = config.maxDepth * PI / depth;

    for (int iz = 0; iz < segZ; iz++)
    {
        float v0 = (float)iz * invSegZ;
        float v1 = (float)(iz + 1) * invSegZ;
        float z0 = -halfD + depth * v0;
        float z1 = -halfD + depth * v1;
        float sinV0, cosV0, sinV1, cosV1;
        Nature_FastSinCos(PI * v0, &sinV0, &cosV0);
        Nature_FastSinCos(PI * v1, &sinV1, &cosV1);

        for (int ix = 0; ix < segX; ix++)
        {
            float u0 = (float)ix * invSegX;
            float u1 = (float)(ix + 1) * invSegX;
            float x0 = -halfW + width * u0;
            float x1 = -halfW + width * u1;
            float sinU0, cosU0, sinU1, cosU1;
            Nature_FastSinCos(PI * u0, &sinU0, &cosU0);
            Nature_FastSinCos(PI * u1, &sinU1, &cosU1);

            float y00 = -config.maxDepth * sinU0 * sinV0 - 0.015f;
            float y10 = -config.maxDepth * sinU1 * sinV0 - 0.015f;
            float y11 = -config.maxDepth * sinU1 * sinV1 - 0.015f;
            float y01 = -config.maxDepth * sinU0 * sinV1 - 0.015f;

            Vector3 p00 = {x0, y00, z0};
            Vector3 p10 = {x1, y10, z0};
            Vector3 p11 = {x1, y11, z1};
            Vector3 p01 = {x0, y01, z1};

            Vector3 n00 = Nature_FastNormalize((Vector3){cosU0 * sinV0 * slopeScaleX, 1.0f, sinU0 * cosV0 * slopeScaleZ});
            Vector3 n10 = Nature_FastNormalize((Vector3){cosU1 * sinV0 * slopeScaleX, 1.0f, sinU1 * cosV0 * slopeScaleZ});
            Vector3 n11 = Nature_FastNormalize((Vector3){cosU1 * sinV1 * slopeScaleX, 1.0f, sinU1 * cosV1 * slopeScaleZ});
            Vector3 n01 = Nature_FastNormalize((Vector3){cosU0 * sinV1 * slopeScaleX, 1.0f, sinU0 * cosV1 * slopeScaleZ});

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};

            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n00, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p10, n10, uv10, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n11, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n00, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n11, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p01, n01, uv01, WHITE);
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
    float invSegL = 1.0f / (float)segL;
    float invSegW = 1.0f / (float)segW;
    int cursor = 0;

    for (int il = 0; il < segL; il++)
    {
        float f0 = (float)il * invSegL;
        float f1 = (float)(il + 1) * invSegL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++)
        {
            float v0 = (float)iw * invSegW;
            float v1 = (float)(iw + 1) * invSegW;
            float w0 = -halfWidth + width * v0;
            float w1 = -halfWidth + width * v1;

            Vector3 p00 = Vector3Add(c0, Vector3Scale(right, w0));
            Vector3 p01 = Vector3Add(c0, Vector3Scale(right, w1));
            Vector3 p11 = Vector3Add(c1, Vector3Scale(right, w1));
            Vector3 p10 = Vector3Add(c1, Vector3Scale(right, w0));

            Vector2 uv00 = {f0, v0};
            Vector2 uv01 = {f0, v1};
            Vector2 uv11 = {f1, v1};
            Vector2 uv10 = {f1, v0};

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
    const float invHalfWidth = 1.0f / halfWidth;
    const float slopeScale = 2.0f * config.maxDepth * invHalfWidth;

    for (int il = 0; il < segL; il++)
    {
        float f0 = (float)il * invSegL;
        float f1 = (float)(il + 1) * invSegL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++)
        {
            float w0 = -halfWidth + width * ((float)iw * invSegW);
            float w1 = -halfWidth + width * ((float)(iw + 1) * invSegW);
            float nw0 = w0 * invHalfWidth;
            float nw1 = w1 * invHalfWidth;
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

            float g0 = -nw0 * slopeScale;
            float g1 = -nw1 * slopeScale;
            Vector3 n0 = Nature_FastNormalize((Vector3){right.x * g0, 1.0f, right.z * g0});
            Vector3 n1 = Nature_FastNormalize((Vector3){right.x * g1, 1.0f, right.z * g1});

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};

            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n0, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p01, n1, uv01, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n1, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n0, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n1, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p10, n0, uv10, WHITE);
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
    water.waterModel = LoadModel(meshPath);
    MapProp_ModelBounds(&water.waterModel, &water.boundsCenter, &water.boundsRadius);
    if (water.waterModel.meshCount < 1 || water.waterModel.materialCount < 1)
    {
        UnloadModel(water.waterModel);
        return (MapWaterSurface){0};
    }
    if (config.center.x == 0.0f && config.center.z == 0.0f)
        config.center = water.boundsCenter;
    if (config.radiusX <= 0.0f || config.radiusZ <= 0.0f)
    {
        config.radiusX = (water.boundsRadius > 0.0f) ? water.boundsRadius : 100.0f;
        config.radiusZ = config.radiusX;
    }
    water.config = config;
    Texture2D detail = ResourceManager_LoadTextureVariant("assets/textures/noise.png",
                                                          true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    water.causticTex = ResourceManager_LoadTextureVariant("assets/textures/water_caustics.png",
                                                          true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    for (int i = 0; i < water.waterModel.materialCount; i++)
    {
        water.waterModel.materials[i].shader = Water_GetShader();
        water.waterModel.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = detail;
    }
    water.waveGridRadius = 16.0f;
    water.waveGridCenter = (Vector3){0};
    Water_InitializeWaveField(&water);
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

    Vector3 *normals = MemAlloc((unsigned int)pointCount * sizeof(Vector3));
    if (!splineMesh.vertices || !splineMesh.texcoords || !splineMesh.colors || !normals)
    {
        if (splineMesh.vertices)
            MemFree(splineMesh.vertices);
        if (splineMesh.texcoords)
            MemFree(splineMesh.texcoords);
        if (splineMesh.colors)
            MemFree(splineMesh.colors);
        if (normals)
            MemFree(normals);
        return water;
    }

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
            t = Vector3Add(Nature_FastNormalize(t1), Nature_FastNormalize(t2));
        }
        t.y = 0.0f;
        float tLenSq = t.x * t.x + t.z * t.z;
        if (tLenSq > 1e-8f)
        {
            float invLen = Nature_FastInvSqrt(tLenSq);
            t.x *= invLen;
            t.z *= invLen;
        }
        else
        {
            t = (Vector3){0.0f, 0.0f, 1.0f};
        }
        normals[i] = (Vector3){-t.z, 0.0f, t.x};
    }

    int cursor = 0;
    float cumulativeDist = 0.0f;
    const float invRings = 1.0f / (float)rings;

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
            float fr0 = (float)r * invRings;
            float fr1 = (float)(r + 1) * invRings;
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
    float sinA, cosA;
    Nature_FastSinCos(angleRad, &sinA, &cosA);
    return (Vector3){
        water->config.center.x + cosA * water->config.radiusX * radialScale * edge,
        water->config.center.y,
        water->config.center.z + sinA * water->config.radiusZ * radialScale * edge,
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
            *outNormal = Nature_FastNormalize(n);
        }
        return true;
    }
    else if (water->config.shape == WATER_SHAPE_RECT)
    {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        float halfW = water->config.radiusX;
        float halfD = water->config.radiusZ;
        if (halfW <= 0.001f || halfD <= 0.001f || fabsf(dx) >= halfW || fabsf(dz) >= halfD)
            return false;

        float u = (dx + halfW) / (2.0f * halfW);
        float v = (dz + halfD) / (2.0f * halfD);
        float sinU, cosU, sinV, cosV;
        Nature_FastSinCos(PI * u, &sinU, &cosU);
        Nature_FastSinCos(PI * v, &sinV, &cosV);
        float depth = water->config.maxDepth * sinU * sinV;
        if (outBedHeight)
        {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal)
        {
            float gx = cosU * sinV * (water->config.maxDepth * PI / (2.0f * halfW));
            float gz = sinU * cosV * (water->config.maxDepth * PI / (2.0f * halfD));
            *outNormal = Nature_FastNormalize((Vector3){gx, 1.0f, gz});
        }
        return true;
    }
    else if (water->config.shape == WATER_SHAPE_STRIP)
    {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        Vector2 flowDir = {water->config.flowVelocity.x, water->config.flowVelocity.y};
        float flowLenSq = flowDir.x * flowDir.x + flowDir.y * flowDir.y;
        Vector3 forward = (flowLenSq > 1e-6f)
                              ? (Vector3){flowDir.x * Nature_FastInvSqrt(flowLenSq), 0.0f, flowDir.y * Nature_FastInvSqrt(flowLenSq)}
                              : (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 right = {-forward.z, 0.0f, forward.x};

        float distLong = dx * forward.x + dz * forward.z;
        float distCross = dx * right.x + dz * right.z;
        float halfLen = water->config.radiusX;
        float halfWid = water->config.radiusZ;
        if (halfWid <= 0.001f || distLong < 0.0f || distLong > 2.0f * halfLen || fabsf(distCross) >= halfWid)
            return false;

        float w = distCross / halfWid;
        float depth = water->config.maxDepth * (1.0f - w * w);
        if (outBedHeight)
        {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal)
        {
            float g = -w * (2.0f * water->config.maxDepth / halfWid);
            *outNormal = Nature_FastNormalize((Vector3){right.x * g, 1.0f, right.z * g});
        }
        return true;
    }

    return false;
}

void MapProp_DrawWaterBed(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready || water->bedModel.meshCount <= 0)
        return;

    Vector3 position = water->config.center;
    if (water->boundsRadius > 0.0f)
    {
        Nature_RefreshFrustumCache();
        Vector3 boundsPos = Vector3Add(position, water->boundsCenter);
        if (!Nature_IsChunkVisibleFast(boundsPos, water->boundsRadius + water->config.bankWidth))
            return;
    }

    Water_RefreshFrameLightingCache();

    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};

    Shader bedShader = Water_GetBedShader();
    if (s_bedLocBankPass == -2)
        s_bedLocBankPass = GetShaderLocation(bedShader, "u_bankPass");

    BeginShaderMode(bedShader);
    SetShaderValue(bedShader, s_bedLocTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(bedShader, s_bedLocWaterHeight, &water->config.center.y, SHADER_UNIFORM_FLOAT);
    SetShaderValue(bedShader, s_bedLocLightDir, &s_waterCachedLightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocLightColor, &s_waterCachedSunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocAmbientColor, &s_waterCachedAmbientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocCausticsStrength, &water->config.causticsStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(bedShader, s_bedLocCausticsScale, &water->config.causticsScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(bedShader, s_bedLocAbsorption, &water->config.absorption, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocDeepColor, &deepRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(bedShader, s_bedLocShallowColor, &shallowRgb, SHADER_UNIFORM_VEC3);
    if (s_bedLocModelPos >= 0)
        SetShaderValue(bedShader, s_bedLocModelPos, &position, SHADER_UNIFORM_VEC3);

    unsigned int causticId = (water->causticTex.id > 0)
                                 ? water->causticTex.id
                                 : (s_defaultCausticTex.id > 0 ? s_defaultCausticTex.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(1);
    rlEnableTexture(causticId);
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

void MapProp_DrawWaterOverlay(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready)
        return;
    Vector3 position = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridCenter : water->config.center;
    Vector3 drawPos = (water->config.shape == WATER_SHAPE_TERRAIN) ? (Vector3){0} : position;

    if (water->boundsRadius > 0.0f)
    {
        Nature_RefreshFrustumCache();
        Vector3 boundsPos = Vector3Add(drawPos, water->boundsCenter);
        if (!Nature_IsChunkVisibleFast(boundsPos, water->boundsRadius))
            return;
    }

    Water_UploadWaveField((MapWaterSurface *)water);
    Water_RefreshFrameLightingCache();

    GfxQuality quality = GfxQuality_Get();
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector4 foam = ColorNormalize(water->config.foamColor);
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
    // Storm swell speeds the current up; rain drives splash rings + stream rise in the shader.
    const float rainVisible = MapProp_GetRain();
    const float runoffNow = MapProp_GetRunoff();
    Vector2 drawFlow = Vector2Scale(water->config.flowVelocity, 1.0f + 0.5f * fminf(runoffNow, 1.5f));
    SetShaderValue(shader, s_waterLocFlowVelocity, &drawFlow, SHADER_UNIFORM_VEC2);
    if (s_waterLocRain >= 0)
        SetShaderValue(shader, s_waterLocRain, &rainVisible, SHADER_UNIFORM_FLOAT);
    if (s_waterLocRainRise >= 0)
    {
        float rise = (water->config.shape == WATER_SHAPE_TERRAIN) ? Water_StormLift() : 0.0f;
        SetShaderValue(shader, s_waterLocRainRise, &rise, SHADER_UNIFORM_FLOAT);
    }
    int flowEnabled = (water->hydrologyReady && water->flowFieldTex.id > 0) ? 1 : 0;
    if (flowEnabled && water->flowDirty)
    {
        UpdateTexture(water->flowFieldTex, water->flowPixels);
        ((MapWaterSurface *)water)->flowDirty = false;
    }
    if (s_waterLocFlowFieldEnabled >= 0)
        SetShaderValue(shader, s_waterLocFlowFieldEnabled, &flowEnabled, SHADER_UNIFORM_INT);
    int shapeInt = (int)water->config.shape;
    SetShaderValue(shader, s_waterLocWaterShape, &shapeInt, SHADER_UNIFORM_INT);

    SetShaderValue(shader, s_waterLocLightDir, &s_waterCachedLightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocLightColor, &s_waterCachedSunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocAmbientColor, &s_waterCachedAmbientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_waterLocModelPos >= 0)
        SetShaderValue(shader, s_waterLocModelPos, &position, SHADER_UNIFORM_VEC3);
    Vector2 lakeRadii = (water->config.shape == WATER_SHAPE_TERRAIN)
                            ? (Vector2){water->waveGridRadius, water->waveGridRadius}
                            : (Vector2){water->config.radiusX, water->config.radiusZ};
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

    Vector4 rings[MAX_WATER_RIPPLES];
    Vector4 params[MAX_WATER_RIPPLES];
    for (int i = 0; i < MAX_WATER_RIPPLES; i++)
    {
        if (water->ripples[i].active)
        {
            float age = time - water->ripples[i].spawnTime;
            if (age < 0.0f)
                age += 3600.0f;
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

    unsigned int defaultTexId = rlGetTextureIdDefault();
    unsigned int causticId = (water->causticTex.id > 0)
                                 ? water->causticTex.id
                                 : (s_defaultCausticTex.id > 0 ? s_defaultCausticTex.id : defaultTexId);
    rlActiveTextureSlot(1);
    rlEnableTexture(causticId);
    rlActiveTextureSlot(3);
    rlEnableTexture(waveFieldEnabled ? water->waveFieldTex.id : defaultTexId);
    rlActiveTextureSlot(4);
    rlEnableTexture(flowEnabled ? water->flowFieldTex.id : defaultTexId);

    Vector2 screenRes = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    if (s_waterLocResolution >= 0)
        SetShaderValue(shader, s_waterLocResolution, &screenRes, SHADER_UNIFORM_VEC2);

    // Mobile TBDR: bỏ qua depth resolve toàn màn hình trên GFX_LOW để tránh tốn băng thông bộ nhớ tile
    bool needsSceneDepth = (quality >= GFX_MED) &&
                           (water->config.shape != WATER_SHAPE_RADIAL &&
                            water->config.shape != WATER_SHAPE_TERRAIN);
    if (needsSceneDepth)
        SceneTargets_RequestSoftDepthRegion((Rectangle){0, 0, screenRes.x, screenRes.y});
    Texture2D depthTex = needsSceneDepth
                             ? SceneTargets_GetDepthTexture()
                             : (Texture2D){0};
    int hasDepth = (depthTex.id > 0) ? 1 : 0;
    SetShaderValue(shader, s_waterLocHasDepthTex, &hasDepth, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(2);
    rlEnableTexture(hasDepth ? depthTex.id : defaultTexId);

    rlActiveTextureSlot(0);
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    DrawModel(water->waterModel, drawPos, 1.0f, WHITE);

    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();

    rlActiveTextureSlot(2);
    rlDisableTexture();
    rlActiveTextureSlot(4);
    rlDisableTexture();
    rlActiveTextureSlot(3);
    rlDisableTexture();
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
    if (isStepEvent && fabsf(water->lastTime - water->waveLastImpulseTime) < 0.12f)
        return;
    if (isStepEvent)
        water->waveLastImpulseTime = water->lastTime;
    int idx = water->nextRipple;

    float planarSpeed = sqrtf(water->interactorVel.x * water->interactorVel.x +
                              water->interactorVel.z * water->interactorVel.z);
    bool footstep = isStepEvent && planarSpeed > 0.2f;
    if (footstep)
    {
        float side = (idx & 1) ? 1.0f : -1.0f;
        float footOffset = fmaxf(0.12f, water->interactorRadius * 0.48f) * side / planarSpeed;
        position.x += -water->interactorVel.z * footOffset;
        position.z += water->interactorVel.x * footOffset;
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

    if (!footstep && water->waterDepth && water->fluxL)
    {
        const int n = WATER_FIELD_SIZE;
        float radX = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusX;
        float radZ = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusZ;
        if (radX > 1e-4f && radZ > 1e-4f)
        {
            Vector3 center = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridCenter : water->config.center;
            float dx = 2.0f * radX / (float)(n - 1);
            float dz = 2.0f * radZ / (float)(n - 1);
            float invRadX = 1.0f / radX;
            float invRadZ = 1.0f / radZ;
            float cellMin = fmaxf(dx * 0.95f, dz * 0.95f);
            float sigma = fmaxf(cellMin, 0.55f);
            float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);
            float impulse = fminf(1.8f, fmaxf(0.0f, intensity * 1.5f));
            int cx = (int)roundf((position.x - center.x + radX) / dx);
            int cz = (int)roundf((position.z - center.z + radZ) / dz);
            int reachX = (int)ceilf(3.2f * sigma / dx);
            int reachZ = (int)ceilf(3.2f * sigma / dz);
            int minZ = (cz - reachZ > 1) ? (cz - reachZ) : 1;
            int maxZ = (cz + reachZ < n - 2) ? (cz + reachZ) : (n - 2);
            int minX = (cx - reachX > 1) ? (cx - reachX) : 1;
            int maxX = (cx + reachX < n - 2) ? (cx + reachX) : (n - 2);
            bool radialShape = (water->config.shape == WATER_SHAPE_RADIAL);

            for (int z = minZ; z <= maxZ; z++)
            {
                float wz = center.z - radZ + (float)z * dz;
                float nz = (wz - center.z) * invRadZ;
                float nz2 = nz * nz;
                float dzPos = wz - position.z;
                float dzPosSq = dzPos * dzPos;
                int row = z * n;
                for (int x = minX; x <= maxX; x++)
                {
                    float wx = center.x - radX + (float)x * dx;
                    float nx = (wx - center.x) * invRadX;
                    float nx2 = nx * nx;
                    float distMetric = radialShape ? (nx2 + nz2) : fmaxf(fabsf(nx), fabsf(nz));
                    if (distMetric >= 0.95f)
                        continue;
                    float dxPos = wx - position.x;
                    float dist2 = dxPos * dxPos + dzPosSq;
                    float q = dist2 * invTwoSigmaSq;
                    if (q > 4.5f)
                        continue;
                    float falloff = expf(-q);
                    int idx = row + x;

                    float shock = impulse * 0.45f * falloff;
                    if (dxPos < 0.0f)
                        water->fluxL[idx] += shock;
                    if (dxPos > 0.0f)
                        water->fluxR[idx] += shock;
                    if (dzPos < 0.0f)
                        water->fluxT[idx] += shock;
                    if (dzPos > 0.0f)
                        water->fluxB[idx] += shock;

                    float waveRing = sinf(sqrtf(dist2) * 10.5f) * 0.026f * impulse * falloff;
                    water->waterDepth[idx] = fmaxf(0.01f, water->waterDepth[idx] + waveRing);
                    if (water->waveVelocityField)
                        water->waveVelocityField[idx] += impulse * falloff * (1.0f - q);
                }
            }
            if (water->waveStepRemainder < 0.0f)
                water->waveStepRemainder = 0.0f;
        }
    }
}

void MapProp_AddWaterObstacle(MapWaterSurface *water, Vector3 position, float radius, float strength)
{
    if (!water || !water->ready || water->obstacleCount >= MAX_WATER_OBSTACLES || radius <= 0.0f)
        return;
    water->obstacles[water->obstacleCount++] = (Vector4){position.x, position.z, radius, strength};
    if (water->waveNextField)
        water->waveNextField[0] = -1.0f;
}

static void Water_ShiftField(float *field, float *temp, int n, int shiftX, int shiftZ, float defaultVal)
{
    if (!field || !temp)
        return;
    for (int z = 0; z < n; z++)
    {
        int sz = z + shiftZ;
        bool validZ = (sz >= 0 && sz < n);
        int dstRow = z * n;
        int srcRow = sz * n;
        for (int x = 0; x < n; x++)
        {
            int sx = x + shiftX;
            temp[dstRow + x] = (validZ && sx >= 0 && sx < n) ? field[srcRow + sx] : defaultVal;
        }
    }
    memcpy(field, temp, (size_t)(n * n) * sizeof(float));
}

void MapProp_UpdateWaterSurface(MapWaterSurface *water, float dt)
{
    if (!water || !water->ready || dt <= 0.0f)
        return;

    // 1. Seamlessly shift wave grid to track player on large terrain water maps
    bool hydroStrip = false;
    int hydroShiftX = 0, hydroShiftZ = 0;
    if (water->config.shape == WATER_SHAPE_TERRAIN && water->waterDepth)
    {
        const int n = WATER_FIELD_SIZE;
        const float dx = 2.0f * water->waveGridRadius / (float)(n - 1);
        const float invDx = 1.0f / dx;

        if (water->waveGridCenter.x == 0.0f && water->waveGridCenter.z == 0.0f)
        {
            water->waveGridCenter = water->interactorPos;
            water->config.center = water->waveGridCenter;
            water->hydroNeedsFullRefresh = true;
            if (water->waveNextField)
                water->waveNextField[0] = -1.0f;
        }

        if (water->interactorPos.x != 0.0f || water->interactorPos.z != 0.0f)
        {
            int shiftX = (int)roundf((water->interactorPos.x - water->waveGridCenter.x) * invDx);
            int shiftZ = (int)roundf((water->interactorPos.z - water->waveGridCenter.z) * invDx);

            if (shiftX != 0 || shiftZ != 0)
            {
                const float baseDepth = (water->config.maxDepth > 0.05f) ? water->config.maxDepth : 0.85f;
                if (abs(shiftX) >= n || abs(shiftZ) >= n)
                {
                    for (int idx = 0; idx < n * n; idx++)
                    {
                        water->bedHeight[idx] = -baseDepth;
                        water->waterDepth[idx] = baseDepth;
                        water->smoothEta[idx] = 0.0f;
                        water->fluxL[idx] = 0.0f;
                        water->fluxR[idx] = 0.0f;
                        water->fluxT[idx] = 0.0f;
                        water->fluxB[idx] = 0.0f;
                        water->velU[idx] = 0.0f;
                        water->velV[idx] = 0.0f;
                        if (water->waveHeightField)
                            water->waveHeightField[idx] = 0.0f;
                        if (water->waveVelocityField)
                            water->waveVelocityField[idx] = 0.0f;
                    }
                    if (water->wavePixels)
                    {
                        memset(water->wavePixels, 128, (size_t)(n * n) * 4u);
                        for (int p = 0; p < n * n; p++)
                            water->wavePixels[p * 4 + 3] = 0;
                        water->waveUploadedTextureId = 0;
                    }
                    water->waveStepRemainder = -1.0f;
                    water->hydroNeedsFullRefresh = true;
                }
                else
                {
                    hydroStrip = true;
                    hydroShiftX = shiftX;
                    hydroShiftZ = shiftZ;
                    Water_ShiftField(water->waterDepth, water->waveNextField, n, shiftX, shiftZ, baseDepth);
                    Water_ShiftField(water->bedHeight, water->waveNextField, n, shiftX, shiftZ, -baseDepth);
                    if (water->eqDepth)
                        Water_ShiftField(water->eqDepth, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->smoothEta, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->fluxL, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->fluxR, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->fluxT, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->fluxB, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->velU, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    Water_ShiftField(water->velV, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    if (water->waveHeightField)
                        Water_ShiftField(water->waveHeightField, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                    if (water->waveVelocityField)
                        Water_ShiftField(water->waveVelocityField, water->waveNextField, n, shiftX, shiftZ, 0.0f);
                }
                water->waveGridCenter.x += (float)shiftX * dx;
                water->waveGridCenter.z += (float)shiftZ * dx;
                water->config.center = water->waveGridCenter;
                if (water->waveNextField)
                    water->waveNextField[0] = -1.0f;
            }
        }
    }

    // 1b. Re-sample the DEM bed + steady river depth for cells that scrolled into the window
    if (water->hydrologyReady && water->config.shape == WATER_SHAPE_TERRAIN &&
        (water->waveGridCenter.x != 0.0f || water->waveGridCenter.z != 0.0f))
    {
        const int hn = WATER_FIELD_SIZE;
        if (water->hydroNeedsFullRefresh)
        {
            Water_TerrainRefreshCells(water, 0, hn, 0, hn, true);
            Water_TerrainRecountWater(water);
            water->hydroNeedsFullRefresh = false;
            water->waveStepRemainder = 0.0f;
        }
        else if (hydroStrip)
        {
            if (hydroShiftX > 0)
                Water_TerrainRefreshCells(water, hn - hydroShiftX, hn, 0, hn, true);
            else if (hydroShiftX < 0)
                Water_TerrainRefreshCells(water, 0, -hydroShiftX, 0, hn, true);
            if (hydroShiftZ > 0)
                Water_TerrainRefreshCells(water, 0, hn, hn - hydroShiftZ, hn, true);
            else if (hydroShiftZ < 0)
                Water_TerrainRefreshCells(water, 0, hn, 0, -hydroShiftZ, true);
            Water_TerrainRecountWater(water);
            if (water->waveStepRemainder < 0.0f)
                water->waveStepRemainder = 0.0f;
        }
    }

    // 2. Continuous interactor physics & momentum drag
    const int n = WATER_FIELD_SIZE;
    float radX = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusX;
    float radZ = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridRadius : water->config.radiusZ;
    if (radX <= 1e-4f || radZ <= 1e-4f)
        return;
    Vector3 center = (water->config.shape == WATER_SHAPE_TERRAIN) ? water->waveGridCenter : water->config.center;
    const float dx = 2.0f * radX / (float)(n - 1);
    const float dz = 2.0f * radZ / (float)(n - 1);
    const float flowU = water->config.flowVelocity.x;
    const float flowV = water->config.flowVelocity.y;
    const float flowSpeedSq = flowU * flowU + flowV * flowV;
    const bool hasStreamFlow = (flowSpeedSq > 0.0001f);
    const bool interactorActive = (water->interactorSubmerged > 0.01f);

    if (interactorActive)
    {
        float moveDx = water->hasInteractorPrev ? (water->interactorPos.x - water->interactorPrevPos.x) : (water->interactorVel.x * dt);
        float moveDz = water->hasInteractorPrev ? (water->interactorPos.z - water->interactorPrevPos.z) : (water->interactorVel.z * dt);
        water->interactorPrevPos = water->interactorPos;
        water->hasInteractorPrev = true;

        float dragLenSq = moveDx * moveDx + moveDz * moveDz;
        if (dragLenSq > 0.000025f)
        {
            float dragLen = sqrtf(dragLenSq);
            float invDragLen = 1.0f / dragLen;
            float dirX = moveDx * invDragLen;
            float dirZ = moveDz * invDragLen;
            float dragStrength = fminf(1.0f, dragLen * 3.5f + 0.15f) * fminf(1.0f, water->interactorSubmerged);
            float brushRad = fmaxf(0.8f, water->interactorRadius * 2.0f);
            float brushRadSq = brushRad * brushRad;
            int rCells = (int)ceilf(brushRad / dx);
            float invRSq = 1.0f / brushRadSq;

            int ci = (int)roundf((water->interactorPos.x - center.x + radX) / dx);
            int cj = (int)roundf((water->interactorPos.z - center.z + radZ) / dz);

            for (int dj = -rCells; dj <= rCells; dj++)
            {
                int nj = cj + dj;
                if (nj < 1 || nj >= n - 1)
                    continue;
                float dzOffset = (float)dj * dz;
                float dzSq = dzOffset * dzOffset;
                int row = nj * n;
                for (int di = -rCells; di <= rCells; di++)
                {
                    int ni = ci + di;
                    if (ni < 1 || ni >= n - 1)
                        continue;
                    float dxOffset = (float)di * dx;
                    float distSq = dxOffset * dxOffset + dzSq;
                    if (distSq > brushRadSq)
                        continue;

                    int idx = row + ni;
                    if (water->waterDepth[idx] > 0.02f)
                    {
                        float falloff = expf(-distSq * invRSq * 2.5f) * dragStrength;
                        float push = 0.06f * falloff;
                        if (dirX > 0.0f)
                            water->fluxR[idx] += push * dirX;
                        else
                            water->fluxL[idx] += push * (-dirX);

                        if (dirZ > 0.0f)
                            water->fluxB[idx] += push * dirZ;
                        else
                            water->fluxT[idx] += push * (-dirZ);
                    }
                }
            }
        }
    }
    else
    {
        water->hasInteractorPrev = false;
    }

    float motionSpeedSq = water->interactorVel.x * water->interactorVel.x +
                          water->interactorVel.z * water->interactorVel.z;
    if (interactorActive && motionSpeedSq > 0.0484f)
    {
        water->waveFootstepTimer += dt;
        if (water->waveFootstepTimer >= 0.20f)
        {
            water->waveFootstepTimer -= 0.20f;
            MapProp_AddWaterRipple(water, water->interactorPos, 2.2f, 0.65f);
        }
    }
    else
    {
        water->waveFootstepTimer = 0.0f;
    }

    // 3. Saint-Venant Shallow Water Equations (SWE) Virtual Pipe Solver
    if (!water->waveFieldTex.id || !water->waterDepth || !water->fluxL)
        return;

    GfxQuality quality = GfxQuality_Get();
    const float step = (quality >= GFX_HIGH) ? (1.0f / 60.0f)
                                             : (quality == GFX_MED ? (1.0f / 45.0f) : (1.0f / 30.0f));
    const float maxAccum = (quality >= GFX_HIGH) ? (3.0f * step)
                                                 : (quality == GFX_MED ? (2.0f * step) : (1.5f * step));
    const float cellArea = dx * dz;
    const float invCellArea = step / cellArea;
    const float pipeFactor = (step * 9.81f) / dx;
    const float dampBase = 0.994f;
    const float invNMinus1 = 2.0f / (float)(n - 1);
    const bool radialShape = (water->config.shape == WATER_SHAPE_RADIAL);
    const float baseDepth = (water->config.maxDepth > 0.05f) ? water->config.maxDepth : 0.85f;

    // Hydrology (DEM-bound SWE): rain source, infiltration, Manning friction, open boundaries
    const bool hydro = water->hydrologyReady && water->eqDepth && water->flowPixels &&
                       (water->waveGridCenter.x != 0.0f || water->waveGridCenter.z != 0.0f);
    const float rainVis = MapProp_GetRain();
    const float runoff = MapProp_GetRunoff();
    const float rainAdd = rainVis * WATER_RAIN_GAIN_MPS * step;
    const float infilStep = WATER_INFIL_MPS * (1.0f - 0.75f * MapProp_GetWetness()) * step;
    const float stormLift = Water_StormLift();
    const float stepFric = step * 9.81f * water->manning * water->manning;
    const bool hydroActive = hydro && (rainVis > 0.01f || runoff > 0.01f);

    if (interactorActive || hydroActive)
    {
        if (water->waveStepRemainder < 0.0f)
            water->waveStepRemainder = 0.0f;
    }

    if (water->waveStepRemainder < 0.0f)
        return;

    if (water->waveNextField && water->waveNextField[0] < 0.0f)
        Water_RebuildBlockedMask(water);

    const float *restrict blockedMask = water->waveNextField;
    water->waveStepRemainder = fminf(water->waveStepRemainder + dt, maxAccum);
    float maxEnergy = 0.0f;

    while (water->waveStepRemainder >= step)
    {
        water->waveStepRemainder -= step;
        maxEnergy = 0.0f;
        bool isFinalSubstep = (water->waveStepRemainder < step);

        // Pass 1: Áp dụng mặt nạ bờ & vật cản đã tiền tính toán O(1) mỗi ô
        if (blockedMask)
        {
            for (int idx = 0; idx < n * n; idx++)
            {
                if (blockedMask[idx] > 0.5f)
                {
                    water->fluxL[idx] = 0.0f;
                    water->fluxR[idx] = 0.0f;
                    water->fluxT[idx] = 0.0f;
                    water->fluxB[idx] = 0.0f;
                    water->velU[idx] = 0.0f;
                    water->velV[idx] = 0.0f;
                }
            }
        }

        // Pass 2: Virtual Pipe Flux Update
        for (int j = 0; j < n; j++)
        {
            int row = j * n;
            for (int i = 0; i < n; i++)
            {
                int idx = row + i;
                float h0 = water->waterDepth[idx];
                if (h0 <= 0.0002f)
                {
                    water->fluxL[idx] = 0.0f;
                    water->fluxR[idx] = 0.0f;
                    water->fluxT[idx] = 0.0f;
                    water->fluxB[idx] = 0.0f;
                    continue;
                }
                
                // Manning friction (semi-implicit): fast sqrtf instead of cbrtf, skipped when still
                float damping = dampBase;
                if (hydro)
                {
                    float spSq = water->velU[idx] * water->velU[idx] + water->velV[idx] * water->velV[idx];
                    if (spSq > 1e-6f)
                    {
                        float hh = fmaxf(h0, 0.02f);
                        damping = 0.998f / (1.0f + stepFric * sqrtf(spSq) / (hh * sqrtf(hh)));
                    }
                    else
                    {
                        damping = 0.998f;
                    }
                }

                float fL = 0.0f, fR = 0.0f, fT = 0.0f, fB = 0.0f;
                if (i > 0)
                {
                    fL = water->fluxL[idx] * damping + pipeFactor * Water_DeltaEta(water, idx, idx - 1, hydro);
                    if (fL < 0.0f)
                        fL = 0.0f;
                }
                if (i < n - 1)
                {
                    fR = water->fluxR[idx] * damping + pipeFactor * Water_DeltaEta(water, idx, idx + 1, hydro);
                    if (fR < 0.0f)
                        fR = 0.0f;
                }
                if (j > 0)
                {
                    fT = water->fluxT[idx] * damping + pipeFactor * Water_DeltaEta(water, idx, idx - n, hydro);
                    if (fT < 0.0f)
                        fT = 0.0f;
                }
                if (j < n - 1)
                {
                    fB = water->fluxB[idx] * damping + pipeFactor * Water_DeltaEta(water, idx, idx + n, hydro);
                    if (fB < 0.0f)
                        fB = 0.0f;
                }
                else if (!hydro && hasStreamFlow && flowV > 0.0f)
                {
                    fB = h0 * 0.40f;
                }

                float sumOut = fL + fR + fT + fB;
                if (sumOut * step > h0 * cellArea && sumOut > 1e-6f)
                {
                    float K = (h0 * cellArea) / (sumOut * step);
                    fL *= K;
                    fR *= K;
                    fT *= K;
                    fB *= K;
                }
                water->fluxL[idx] = fL;
                water->fluxR[idx] = fR;
                water->fluxT[idx] = fT;
                water->fluxB[idx] = fB;
            }
        }

        // Pass 3: Depth & Velocity Integration
        for (int j = 0; j < n; j++)
        {
            int row = j * n;
            const float nzm = (float)j * invNMinus1 - 1.0f;
            for (int i = 0; i < n; i++)
            {
                int idx = row + i;
                float inL = (i > 0) ? water->fluxR[idx - 1] : 0.0f;
                float inR = (i < n - 1) ? water->fluxL[idx + 1] : 0.0f;
                float inT = (j > 0) ? water->fluxB[idx - n] : 0.0f;
                float inB = (j < n - 1) ? water->fluxT[idx + n] : 0.0f;

                float netRate = (inL + inR + inT + inB) -
                                (water->fluxL[idx] + water->fluxR[idx] + water->fluxT[idx] + water->fluxB[idx]);
                float hPrev = water->waterDepth[idx];
                float hNew = hPrev + netRate * invCellArea;
                if (hNew < 0.0f)
                    hNew = 0.0f;
                if (hydro)
                {
                    // Open boundary + sources: the river relaxes to its baked steady depth
                    // (upstream feed / downstream drain); everything else loses water to
                    // infiltration and to a sink band near the window edge.
                    const float nxm = (float)i * invNMinus1 - 1.0f;
                    float edge = (fmaxf(fabsf(nxm), fabsf(nzm)) - 0.70f) * (1.0f / 0.24f);
                    edge = edge < 0.0f ? 0.0f : (edge > 1.0f ? 1.0f : edge);
                    const float eq = water->eqDepth[idx];
                    if (eq > 0.0f)
                    {
                        float k = step * (0.35f + 5.0f * edge);
                        if (k > 1.0f)
                            k = 1.0f;
                        hNew += (eq + stormLift - hNew) * k;
                    }
                    else
                    {
                        hNew -= fminf(hNew, infilStep);
                        if (edge > 0.0f)
                            hNew *= 1.0f - fminf(1.0f, step * 4.0f * edge);
                        // Rain on land only: the river's own rain is already inside its steady
                        // depth, adding it again biased the whole channel above equilibrium.
                        hNew += rainAdd;
                    }
                }
                water->waterDepth[idx] = hNew;

                if (hNew > 0.008f)
                {
                    float invDenom = 0.5f / (dx * fmaxf(0.04f, 0.5f * (hPrev + hNew)));
                    float u = (inL - water->fluxL[idx] + water->fluxR[idx] - inR) * invDenom;
                    float v = (inT - water->fluxT[idx] + water->fluxB[idx] - inB) * invDenom;
                    if (u > 3.5f)
                        u = 3.5f;
                    else if (u < -3.5f)
                        u = -3.5f;
                    if (v > 3.5f)
                        v = 3.5f;
                    else if (v < -3.5f)
                        v = -3.5f;
                    water->velU[idx] = water->velU[idx] * 0.65f + u * 0.35f;
                    water->velV[idx] = water->velV[idx] * 0.65f + v * 0.35f;
                }
                else
                {
                    water->velU[idx] *= 0.5f;
                    water->velV[idx] *= 0.5f;
                }

                float cellEnergy = fabsf(hNew - hPrev) + fabsf(water->velU[idx]) + fabsf(water->velV[idx]);
                if (cellEnergy > maxEnergy)
                    maxEnergy = cellEnergy;
            }
        }

        // Pass 4: Chỉ mã hóa texture pháp tuyến & bọt ở bước con cuối cùng của frame
        if (isFinalSubstep)
        {
            const float inv2Dx = 1.0f / (2.0f * dx);
            const float inv2Dz = 1.0f / (2.0f * dz);
            for (int j = 0; j < n; j++)
            {
                int row = j * n;
                int rowT = (j > 0 ? j - 1 : 0) * n;
                int rowB = (j < n - 1 ? j + 1 : n - 1) * n;
                float nzMetric = (float)j * invNMinus1 - 1.0f;
                float nzMetricSq = nzMetric * nzMetric;
                for (int i = 0; i < n; i++)
                {
                    int idx = row + i;
                    float b0 = water->bedHeight[idx], h0 = water->waterDepth[idx];
                    int idxL = row + (i > 0 ? i - 1 : 0);
                    int idxR = row + (i < n - 1 ? i + 1 : n - 1);
                    int idxT = rowT + i;
                    int idxB = rowB + i;

                    // Hydrology: shade from the DISTURBANCE (depth - steady depth), not from the
                    // absolute surface. The steady river already slopes and flows; treating that
                    // slope/speed as "turbulence" painted the whole simulated window white.
                    float eta0, etaL, etaR, etaT, etaB;
                    if (hydro)
                    {
                        const int ids[5] = {idx, idxL, idxR, idxT, idxB};
                        float dv[5];
                        for (int q = 0; q < 5; q++)
                        {
                            float hq = water->waterDepth[ids[q]];
                            float eq = water->eqDepth[ids[q]];
                            dv[q] = (hq > 0.008f) ? (hq - ((eq > 0.0f) ? (eq + stormLift) : 0.0f)) : 0.0f;
                        }
                        eta0 = dv[0]; etaL = dv[1]; etaR = dv[2]; etaT = dv[3]; etaB = dv[4];
                    }
                    else
                    {
                        eta0 = (h0 > 0.006f) ? (b0 + h0) : (b0 - 0.025f);
                        etaL = (water->waterDepth[idxL] > 0.008f) ? (water->bedHeight[idxL] + water->waterDepth[idxL]) : eta0;
                        etaR = (water->waterDepth[idxR] > 0.008f) ? (water->bedHeight[idxR] + water->waterDepth[idxR]) : eta0;
                        etaT = (water->waterDepth[idxT] > 0.008f) ? (water->bedHeight[idxT] + water->waterDepth[idxT]) : eta0;
                        etaB = (water->waterDepth[idxB] > 0.008f) ? (water->bedHeight[idxB] + water->waterDepth[idxB]) : eta0;
                    }

                    float sEta = eta0 * 0.5f + 0.125f * (etaL + etaR + etaT + etaB);
                    water->smoothEta[idx] = sEta;
                    if (water->waveHeightField)
                        water->waveHeightField[idx] = hydro ? 0.0f : (sEta - (b0 + h0));

                    float nx = -(etaR - etaL) * inv2Dx;
                    float nz = -(etaB - etaT) * inv2Dz;
                    if (nx > 1.2f)
                        nx = 1.2f;
                    else if (nx < -1.2f)
                        nx = -1.2f;
                    if (nz > 1.2f)
                        nz = 1.2f;
                    else if (nz < -1.2f)
                        nz = -1.2f;

                    float u = water->velU[idx], v = water->velV[idx];
                    float speedSq = u * u + v * v;
                    float slopeSq = nx * nx + nz * nz;
                    float foam = (slopeSq - 0.03f) * 2.5f + (speedSq - 0.25f) * 0.35f;
                    if (hydro) // only genuinely fast water (>1.5 m/s) whitens; slopes come from disturbance only
                        foam = (slopeSq - 0.06f) * 3.0f + (speedSq - 2.25f) * 0.25f;
                    if (foam < 0.0f)
                        foam = 0.0f;
                    else if (foam > 1.0f)
                        foam = 1.0f;

                    float nxMetric = (float)i * invNMinus1 - 1.0f;
                    float bndMetric = radialShape ? (nxMetric * nxMetric + nzMetricSq) : fmaxf(fabsf(nxMetric), fabsf(nzMetric));
                    if (bndMetric >= 0.91f)
                    {
                        nx = 0.0f;
                        nz = 0.0f;
                        foam = 0.0f;
                    }

                    int p4 = idx * 4;
                    float dispRef = baseDepth;
                    if (hydro)
                    {
                        float eqBase = water->eqDepth[idx];
                        dispRef = (eqBase > 0.0f) ? (eqBase + stormLift) : 0.0f;
                    }
                    float disp = (h0 > 0.005f) ? (h0 - dispRef) : 0.0f;
                    if (hydro) // dead-zone: leave the field neutral unless the surface is truly disturbed
                    {
                        float ad = fabsf(disp) - 0.012f;
                        disp = (ad > 0.0f) ? copysignf(ad, disp) : 0.0f;
                    }
                    water->wavePixels[p4 + 0] = Water_EncodeUnorm8(0.5f + nx * 0.45f);
                    water->wavePixels[p4 + 1] = Water_EncodeUnorm8(0.5f + nz * 0.45f);
                    water->wavePixels[p4 + 2] = Water_EncodeUnorm8(0.5f + disp * 4.0f);
                    water->wavePixels[p4 + 3] = Water_EncodeUnorm8(foam);
                    if (hydro)
                    {
                        const bool wet = (h0 > 0.03f) && (bndMetric < 0.91f);
                        // Base current: direction/speed from the baked surface slope (5-cell stencil
                        // for smoothness), added to the simulated disturbance velocity.
                        float bu = 0.0f, bv = 0.0f;
                        if (water->eqDepth[idx] > 0.0f)
                        {
                            const float sC = water->bedHeight[idx] + water->eqDepth[idx];
                            const int iL = row + (i > 1 ? i - 2 : 0), iR = row + (i < n - 2 ? i + 2 : n - 1);
                            const int iT = (j > 1 ? j - 2 : 0) * n + i, iB = (j < n - 2 ? j + 2 : n - 1) * n + i;
                            float gx = (Water_BakedSurf(water, iR, sC) - Water_BakedSurf(water, iL, sC)) / (4.0f * dx);
                            float gz = (Water_BakedSurf(water, iB, sC) - Water_BakedSurf(water, iT, sC)) / (4.0f * dz);
                            float g = sqrtf(gx * gx + gz * gz);
                            if (g > 1e-5f)
                            {
                                float sp = sqrtf(g) * 2.0f;
                                sp = sp < 0.08f ? 0.08f : (sp > 0.5f ? 0.5f : sp);
                                bu = -gx / g * sp;
                                bv = -gz / g * sp;
                            }
                        }
                        float fu = u * 0.5f + bu, fv = v * 0.5f + bv;
                        water->flowPixels[p4 + 0] = Water_EncodeUnorm8(0.5f + fu * WATER_FLOW_ENC);
                        water->flowPixels[p4 + 1] = Water_EncodeUnorm8(0.5f + fv * WATER_FLOW_ENC);
                        water->flowPixels[p4 + 2] = 128;
                        water->flowPixels[p4 + 3] = wet ? 255 : 0;
                    }
                }
            }
            water->waveUploadedTextureId = 0;
            if (hydro && (hydroStrip || maxEnergy > 5e-4f || !water->flowFieldTex.id))
                water->flowDirty = true;
        }

        if (!interactorActive && !hydroActive && maxEnergy < 1e-5f)

        {
            memset(water->wavePixels, 128, (size_t)(n * n) * 4u);
            for (int p = 0; p < n * n; p++)
                water->wavePixels[p * 4 + 3] = 0;
            water->waveUploadedTextureId = 0;
            water->waveStepRemainder = -1.0f;
            return;
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
    free(water->bedHeight);
    free(water->waterDepth);
    free(water->smoothEta);
    free(water->fluxL);
    free(water->fluxR);
    free(water->fluxT);
    free(water->fluxB);
    free(water->velU);
    free(water->velV);
    free(water->wavePixels);
    if (water->flowFieldTex.id > 0)
        UnloadTexture(water->flowFieldTex);
    free(water->eqDepth);
    free(water->flowPixels);
    if (water->waveUploadedPixels)
        MemFree(water->waveUploadedPixels);
    *water = (MapWaterSurface){0};
}
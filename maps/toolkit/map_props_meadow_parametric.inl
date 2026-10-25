// Shared indexed tuft templates and immutable per-blade authoring data.
// Included after Nature_DescribeMeadowBlade; only compact IDs upload per frame.
#define NATURE_PARAMETRIC_LODS 4
#define NATURE_PARAMETER_COLUMNS 7
#define NATURE_PARAMETER_ROW_BLADES 256
#define NATURE_PARAMETER_MAX_ROWS 8192
#define NATURE_TEMPLATE_MAX_VERTICES 230
#define NATURE_TEMPLATE_MAX_INDICES 330
#define NATURE_LOD_NEAR_BLEND_HALF_WIDTH 1.8f
#define NATURE_LOD_FAR_BLEND_HALF_WIDTH 4.2f
#define NATURE_VISIBLE_ID_ROW_TEXELS 256
// [TỐI ƯU HIỆU NĂNG]: Nới rộng dung sai camera epsilon (3cm -> 15cm) để đóng băng texture upload
// khi đứng im hoặc rung nhẹ camera, triệt tiêu hoàn toàn hiện tượng rớt FPS xuống 30 do bus stall.
#define NATURE_CAMERA_EPSILON_SQ 0.0225f

typedef struct
{
    unsigned int vao, vertices, indices;
    int indexCount, vertexCount, blades, segments;
} NatureTuftTemplate;

typedef struct
{
    int offset[NATURE_PARAMETRIC_LODS];
    int count;
    int tuftOffset;
    int visibleOffset[3], visibleCount[3];
    Vector2 minimum;
} NatureTuftRange;

typedef struct
{
    Texture2D parameters;
    Texture2D visibleIds;
    NatureTuftTemplate templates[NATURE_PARAMETRIC_LODS];
    NatureTuftRange *ranges;
    int rangeCapacity;
    bool shadow;
    bool canonical, compact;
    bool nearFirst;
    bool traceEnabled;
    int *drawOrder;
    float *drawDepth;
    float *chunkDistCache;
    int drawCount;
    // Compact submissions pack ids lod-major in front-to-back chunk drawOrder
    int bandBase[3], bandCount[3];
    int canonicalBlades, tuftCount, idPixelCount;
    Vector3 *roots;
    float *ranks, *idPixels;
    unsigned char *visibleLods;
    unsigned int visibleFrames;
    bool prepared;
    Camera3D preparedCamera;
    Vector3 preparedOffset;
    Vector4 preparedBands;
    int preparedWidth, preparedHeight, preparedQuality;
    float preparedDrawDistance;
} NatureParametricMeadow;

typedef struct
{
    float lodScale, zoomFactor, lodDistance, drawDistance;
    Vector4 bands;
} NatureMeadowView;

static NatureMeadowView NatureParametric_View(const MapMeadowSurface *meadow)
{
    int quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH  ? 1.0f
                       : quality == GFX_MED ? 0.92f
                       : quality == GFX_LOW ? 0.85f
                                            : 0.72f;
    float lodScale = quality >= GFX_HIGH ? 1.0f : quality == GFX_MED ? 0.88f
                                                                     : 0.78f;

    float dx = camera.position.x - camera.target.x;
    float dz = camera.position.z - camera.target.z;
    float focalDistanceXZ = sqrtf(dx * dx + dz * dz);

    float fovy = fmaxf(camera.fovy, 15.0f) * DEG2RAD;
    float zoom = 0.41421356f / tanf(fovy * 0.5f);
    zoom = fminf(fmaxf(zoom, 0.8f), 2.5f);

    float lodZoom = (zoom > 1.0f) ? sqrtf(zoom) : zoom;
    float scale = lodScale * lodZoom;

    // Horizontal camera-to-target focal offset (GEMINI.md §2.3):
    // Ensures the meadow around camera.target stays in Near/Mid LOD when the camera orbits back,
    // without ever shrinking drawDistance or LOD radii when zoomed in close!
    float focalOffset = fminf(focalDistanceXZ * 0.80f, 12.5f);

    NatureMeadowView view;
    view.lodScale = lodScale;
    view.zoomFactor = zoom;
    view.lodDistance = focalOffset + meadow->lodDistance * scale;
    view.drawDistance = meadow->drawDistance > 0.0f
                            ? (focalOffset + meadow->drawDistance * rangeScale * fminf(zoom, 1.20f))
                            : 0.0f;

    float rawMidDist = meadow->midLodDistance > 0.0f
                           ? meadow->midLodDistance
                           : (meadow->lodDistance > 12.0f ? meadow->lodDistance * 0.48f : 0.0f);
    float midDist = (rawMidDist > 0.0f) ? (focalOffset + rawMidDist * scale) : 0.0f;
    float nearBlend = NATURE_LOD_NEAR_BLEND_HALF_WIDTH * scale;
    float farBlend = NATURE_LOD_FAR_BLEND_HALF_WIDTH * scale;
    if (midDist > 0.0f && view.lodDistance > midDist)
    {
        float maxHalfSpan = (view.lodDistance - midDist) * 0.45f;
        nearBlend = fminf(nearBlend, fminf(midDist * 0.45f, maxHalfSpan));
        farBlend = fminf(farBlend, maxHalfSpan);
    }

    view.bands = (Vector4){midDist, view.lodDistance, nearBlend, farBlend};
    return view;
}

static inline bool NatureParametric_SameVector(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return (dx * dx + dy * dy + dz * dz) <= NATURE_CAMERA_EPSILON_SQ;
}

static bool NatureParametric_IsPrepared(const MapMeadowSurface *meadow, Vector3 offset, Vector4 bands)
{
    const NatureParametricMeadow *data = meadow->parametric;
    return data->prepared &&
           NatureParametric_SameVector(data->preparedCamera.position, camera.position) &&
           NatureParametric_SameVector(data->preparedCamera.target, camera.target) &&
           NatureParametric_SameVector(data->preparedCamera.up, camera.up) &&
           fabsf(data->preparedCamera.fovy - camera.fovy) <= 0.02f &&
           data->preparedCamera.projection == camera.projection &&
           NatureParametric_SameVector(data->preparedOffset, offset) &&
           fabsf(data->preparedBands.x - bands.x) <= 0.02f &&
           fabsf(data->preparedBands.y - bands.y) <= 0.02f &&
           fabsf(data->preparedBands.z - bands.z) <= 0.02f &&
           fabsf(data->preparedBands.w - bands.w) <= 0.02f &&
           data->preparedQuality == GfxQuality_Get() &&
           fabsf(data->preparedDrawDistance - meadow->drawDistance) <= 0.02f &&
           data->preparedWidth == GetScreenWidth() &&
           data->preparedHeight == GetScreenHeight();
}

static Shader s_natureParametricShader = {0};
static Shader s_natureParametricShadowShader = {0};
static int s_natureParameterLoc[2], s_natureBladeOffsetLoc[2], s_natureBladeCountLoc[2];
static int s_natureReceiverSamplerLoc[2][4];
static int s_natureTuftLodBandsLoc = -1, s_natureTuftLodLevelLoc = -1;
static int s_natureTuftLodCameraLoc = -1;
static int s_natureTuftFadeRangeLoc = -1;
static int s_natureCanonicalBladesLoc[2], s_natureCanonicalLoc[2], s_natureGeometryLodLoc[2];
static int s_natureTuftOffsetLoc[2], s_natureCompactLoc[2], s_natureVisibleIdsLoc;
static int s_natureVisibleOffsetLoc;
static int s_natureWorldOffsetLoc[2];

static float NatureParametric_Rank(Vector3 root)
{
    unsigned int x, z;
    memcpy(&x, &root.x, sizeof(x));
    memcpy(&z, &root.z, sizeof(z));
    unsigned int h = (x * 0x8da6b343u) ^ (z * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    float hashRank = (float)(h & 0x00ffffffu) * (1.0f / 16777216.0f);
    float ign = fmodf(52.9829189f * fmodf(0.06711056f * root.x + 0.00583715f * root.z, 1.0f), 1.0f);
    if (ign < 0.0f)
        ign += 1.0f;
    float combined = hashRank * 0.65f + ign * 0.35f;
    return combined - floorf(combined);
}

static inline float NatureParametric_Smoothstep(float low, float high, float value)
{
    float span = high - low;
    if (span <= 1e-5f)
        return value >= high ? 1.0f : 0.0f;
    float t = (value - low) / span;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static inline int NatureParametric_SelectLod(Vector3 root, float rank, Vector3 cameraPos, Vector4 bands)
{
    float dx = root.x - cameraPos.x, dy = root.y - cameraPos.y, dz = root.z - cameraPos.z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (bands.y <= 0.0f)
        return 0;
    float farWeight = NatureParametric_Smoothstep(bands.y - bands.w, bands.y + bands.w, d);
    if (rank < farWeight)
        return 2;
    float midWeight = bands.x > 0.0f
                          ? NatureParametric_Smoothstep(bands.x - bands.z, bands.x + bands.z, d)
                          : 0.0f;
    return rank < midWeight ? 1 : 0;
}

static inline int NatureParametric_SelectLodFast(float d2, float rank, Vector4 bands,
                                                 float nearMinSq, float farMaxSq,
                                                 float midInnerMinSq, float midInnerMaxSq)
{
    if (bands.y <= 0.0f || d2 <= nearMinSq)
        return 0;
    if (d2 >= farMaxSq)
        return 2;
    if (midInnerMaxSq > midInnerMinSq && d2 >= midInnerMinSq && d2 <= midInnerMaxSq)
        return 1;

    float d = sqrtf(d2);
    float farWeight = NatureParametric_Smoothstep(bands.y - bands.w, bands.y + bands.w, d);
    if (rank < farWeight)
        return 2;
    float midWeight = bands.x > 0.0f
                          ? NatureParametric_Smoothstep(bands.x - bands.z, bands.x + bands.z, d)
                          : 0.0f;
    return rank < midWeight ? 1 : 0;
}

static bool NatureParametric_LodIntersectsSphere(int lod, float nearest, float farthest, Vector4 bands)
{
    if (bands.y <= 0.0f)
        return lod == 0;
    if (lod == 0)
        return nearest <= (bands.x > 0.0f ? bands.x + bands.z : bands.y + bands.w);
    if (lod == 1)
        return bands.x > 0.0f && farthest >= bands.x - bands.z && nearest <= bands.y + bands.w;
    return lod == 2 && farthest >= bands.y - bands.w;
}

static Shader NatureParametric_Shader(bool shadow)
{
    Shader *shader = shadow ? &s_natureParametricShadowShader : &s_natureParametricShader;
    if (shader->id == 0)
    {
        *shader = ResourceManager_LoadShader(
            shadow ? "maps/toolkit/shaders/nature_shadow_parametric.vs"
                   : "maps/toolkit/shaders/nature_lit_parametric.vs",
            shadow ? "maps/toolkit/shaders/nature_shadow.fs"
                   : "maps/toolkit/shaders/nature_opaque.fs");
        int pass = shadow ? 1 : 0;
        s_natureParameterLoc[pass] = GetShaderLocation(*shader, "u_bladeParameters");
        s_natureBladeOffsetLoc[pass] = GetShaderLocation(*shader, "u_bladeOffset");
        s_natureBladeCountLoc[pass] = GetShaderLocation(*shader, "u_bladesPerTuft");
        s_natureCanonicalLoc[pass] = GetShaderLocation(*shader, "u_canonicalBladeData");
        s_natureCanonicalBladesLoc[pass] = GetShaderLocation(*shader, "u_canonicalBlades");
        s_natureGeometryLodLoc[pass] = GetShaderLocation(*shader, "u_geometryLod");
        s_natureTuftOffsetLoc[pass] = GetShaderLocation(*shader, "u_chunkTuftOffset");
        s_natureWorldOffsetLoc[pass] = GetShaderLocation(*shader, "u_worldOffset");
        s_natureCompactLoc[pass] = GetShaderLocation(*shader, "u_compactTuftSubmission");
        const char *samplers[4] = {"u_windCacheMap", "u_interactionMap", "shadowMap", "staticShadowMap"};
        for (int i = 0; i < 4; i++)
            s_natureReceiverSamplerLoc[pass][i] = GetShaderLocation(*shader, samplers[i]);
        shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
        shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
        shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
        shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
        if (!shadow)
        {
            s_natureTuftLodBandsLoc = GetShaderLocation(*shader, "u_tuftLodBands");
            s_natureTuftLodLevelLoc = GetShaderLocation(*shader, "u_tuftLodLevel");
            s_natureTuftLodCameraLoc = GetShaderLocation(*shader, "u_tuftLodCamera");
            s_natureTuftFadeRangeLoc = GetShaderLocation(*shader, "u_tuftFadeRange");
            s_natureVisibleIdsLoc = GetShaderLocation(*shader, "u_visibleTuftIds");
            s_natureVisibleOffsetLoc = GetShaderLocation(*shader, "u_visibleTuftOffset");
            if (s_natureTuftLodBandsLoc < 0 || s_natureTuftLodLevelLoc < 0 ||
                s_natureTuftLodCameraLoc < 0 || s_natureTuftFadeRangeLoc < 0)
                TraceLog(LOG_WARNING, "MEADOW_PARAMETRIC: missing LOD uniforms; using expanded mesh fallback");
            MapShadow_ConfigureShader(*shader);
            VFXLight_RegisterShader(*shader);
        }
        (void)Nature_GetCachedLocs(*shader);
    }
    return *shader;
}

static bool NatureParametric_BuildTemplate(NatureTuftTemplate *mesh, int blades, int segments)
{
    Vector3 vertices[NATURE_TEMPLATE_MAX_VERTICES];
    unsigned short indices[NATURE_TEMPLATE_MAX_INDICES];
    int vertexCount = 0, indexCount = 0;
    float tipStart = (segments == 2) ? 0.64f : (segments > 2 ? 0.78f : 0.0f);

    for (int blade = 0; blade < blades; blade++)
    {
        unsigned short bladeBase = (unsigned short)vertexCount;
        int bodySegments = segments - 1;

        for (int ring = 0; ring <= bodySegments; ring++)
        {
            float frac = bodySegments > 0 ? (float)ring / (float)bodySegments : 0.0f;
            float t = tipStart * powf(frac, 0.96f);
            float zTag = (float)blade + (ring == 0 ? 0.25f : 0.50f);
            vertices[vertexCount++] = (Vector3){-1.0f, t, zTag};
            vertices[vertexCount++] = (Vector3){1.0f, t, zTag};
        }
        vertices[vertexCount++] = (Vector3){0.0f, 1.0f, (float)blade + 0.75f};

        for (int segment = 0; segment < bodySegments; segment++)
        {
            unsigned short b0 = bladeBase + (unsigned short)(segment * 2);
            indices[indexCount++] = b0;
            indices[indexCount++] = b0 + 1;
            indices[indexCount++] = b0 + 3;
            indices[indexCount++] = b0;
            indices[indexCount++] = b0 + 3;
            indices[indexCount++] = b0 + 2;
        }
        unsigned short tipBase = bladeBase + (unsigned short)(bodySegments * 2);
        indices[indexCount++] = tipBase;
        indices[indexCount++] = tipBase + 1;
        indices[indexCount++] = tipBase + 2;
    }

    mesh->vao = rlLoadVertexArray();
    if (!mesh->vao || !rlEnableVertexArray(mesh->vao))
        return false;
    mesh->vertices = rlLoadVertexBuffer(vertices, vertexCount * sizeof(Vector3), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, sizeof(Vector3), 0);
    rlEnableVertexAttribute(0);
    mesh->indices = rlLoadVertexBufferElement(indices, indexCount * sizeof(unsigned short), false);
    rlDisableVertexArray();
    mesh->indexCount = indexCount;
    mesh->vertexCount = vertexCount;
    mesh->blades = blades;
    mesh->segments = segments;
    return mesh->vao != 0 && mesh->vertices != 0 && mesh->indices != 0;
}

static void NatureParametric_Destroy(MapMeadowSurface *meadow)
{
    NatureParametricMeadow *data = meadow->parametric;
    if (!data)
        return;
    if (data->parameters.id)
        UnloadTexture(data->parameters);
    if (data->visibleIds.id)
        UnloadTexture(data->visibleIds);
    for (int lod = 0; lod < NATURE_PARAMETRIC_LODS; lod++)
    {
        if (data->templates[lod].vao)
            rlUnloadVertexArray(data->templates[lod].vao);
        if (data->templates[lod].vertices)
            rlUnloadVertexBuffer(data->templates[lod].vertices);
        if (data->templates[lod].indices)
            rlUnloadVertexBuffer(data->templates[lod].indices);
    }
    MemFree(data->ranges);
    MemFree(data->roots);
    MemFree(data->ranks);
    MemFree(data->idPixels);
    MemFree(data->visibleLods);
    MemFree(data->drawOrder);
    MemFree(data->drawDepth);
    MemFree(data->chunkDistCache);
    MemFree(data);
    meadow->parametric = NULL;
}

static bool NatureParametric_Create(MapMeadowSurface *meadow,
                                    const MapMeadowPlacement *placements, int count, MapMeadowStyle style)
{
    if (count <= 0 || style.chunkSize <= 0.0f)
        return false;
    const char *renderer = getenv("WUXING_MEADOW_RENDERER");
    if (style.texturePath || style.hasPlumes ||
        (renderer && strcmp(renderer, "legacy") == 0))
        return false;
    Shader visible = NatureParametric_Shader(false);
    if (visible.id == rlGetShaderIdDefault() || visible.locs[SHADER_LOC_MATRIX_MVP] < 0 ||
        GetShaderLocation(visible, "u_bladeParameters") < 0 ||
        s_natureTuftLodBandsLoc < 0 || s_natureTuftLodLevelLoc < 0 ||
        s_natureTuftLodCameraLoc < 0 || s_natureTuftFadeRangeLoc < 0)
        return false;
    bool shadows = style.shadowDistance > 0.0f;
    if (shadows && GetShaderLocation(NatureParametric_Shader(true), "u_bladeParameters") < 0)
        return false;
    for (int pass = 0; pass < (shadows ? 2 : 1); pass++)
    {
        if (s_natureCanonicalLoc[pass] < 0 || s_natureCanonicalBladesLoc[pass] < 0 ||
            s_natureGeometryLodLoc[pass] < 0 || s_natureTuftOffsetLoc[pass] < 0 ||
            s_natureWorldOffsetLoc[pass] < 0)
            return false;
    }
    if (s_natureCompactLoc[0] < 0 || s_natureVisibleIdsLoc < 0 || s_natureVisibleOffsetLoc < 0)
        return false;

    int *cellCounts = NULL, *cellToChunk = NULL, *chunkWriteCursor = NULL, *sortedIndices = NULL;
    float *pixels = NULL;

    float minX = placements[0].position.x, maxX = minX;
    float minZ = placements[0].position.z, maxZ = minZ;
    for (int i = 1; i < count; i++)
    {
        minX = fminf(minX, placements[i].position.x);
        maxX = fmaxf(maxX, placements[i].position.x);
        minZ = fminf(minZ, placements[i].position.z);
        maxZ = fmaxf(maxZ, placements[i].position.z);
    }
    float invChunkSize = 1.0f / style.chunkSize;
    int columns = (int)ceilf((maxX - minX + 0.001f) * invChunkSize);
    int rows = (int)ceilf((maxZ - minZ + 0.001f) * invChunkSize);
    if (columns < 1)
        columns = 1;
    if (rows < 1)
        rows = 1;
    int capacity = columns * rows;

    NatureParametricMeadow *data = MemAlloc(sizeof(*data));
    if (!data)
        return false;
    memset(data, 0, sizeof(*data));
    meadow->parametric = data;

    cellCounts = MemAlloc(capacity * sizeof(int));
    cellToChunk = MemAlloc(capacity * sizeof(int));
    sortedIndices = MemAlloc(count * sizeof(int));
    if (!cellCounts || !cellToChunk || !sortedIndices)
        goto failed;
    memset(cellCounts, 0, capacity * sizeof(int));

    for (int i = 0; i < count; i++)
    {
        int col = (int)((placements[i].position.x - minX) * invChunkSize);
        int row = (int)((placements[i].position.z - minZ) * invChunkSize);
        col = col < 0 ? 0 : (col >= columns ? columns - 1 : col);
        row = row < 0 ? 0 : (row >= rows ? rows - 1 : row);
        cellCounts[row * columns + col]++;
    }

    int activeChunks = 0;
    for (int cell = 0; cell < capacity; cell++)
    {
        cellToChunk[cell] = cellCounts[cell] > 0 ? activeChunks++ : -1;
    }
    if (activeChunks == 0)
        goto failed;

    data->ranges = MemAlloc(activeChunks * sizeof(*data->ranges));
    meadow->chunks = MemAlloc(activeChunks * sizeof(*meadow->chunks));
    data->drawOrder = MemAlloc(activeChunks * sizeof(*data->drawOrder));
    data->drawDepth = MemAlloc(activeChunks * sizeof(*data->drawDepth));
    data->chunkDistCache = MemAlloc(activeChunks * sizeof(*data->chunkDistCache));
    chunkWriteCursor = MemAlloc(activeChunks * sizeof(int));
    if (!data->ranges || !meadow->chunks || !data->drawOrder || !data->drawDepth ||
        !data->chunkDistCache || !chunkWriteCursor)
        goto failed;
    memset(data->ranges, 0, activeChunks * sizeof(*data->ranges));
    memset(meadow->chunks, 0, activeChunks * sizeof(*meadow->chunks));

    meadow->chunkCount = activeChunks;
    data->rangeCapacity = activeChunks;

    int runningTuftOffset = 0;
    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < columns; col++)
        {
            int cell = row * columns + col;
            int chunk = cellToChunk[cell];
            if (chunk < 0)
                continue;
            float x0 = minX + col * style.chunkSize;
            float z0 = minZ + row * style.chunkSize;
            data->ranges[chunk].count = cellCounts[cell];
            data->ranges[chunk].tuftOffset = runningTuftOffset;
            data->ranges[chunk].minimum = (Vector2){x0, z0};
            chunkWriteCursor[chunk] = runningTuftOffset;
            runningTuftOffset += cellCounts[cell];
        }
    }

    for (int i = 0; i < count; i++)
    {
        int col = (int)((placements[i].position.x - minX) * invChunkSize);
        int row = (int)((placements[i].position.z - minZ) * invChunkSize);
        col = col < 0 ? 0 : (col >= columns ? columns - 1 : col);
        row = row < 0 ? 0 : (row >= rows ? rows - 1 : row);
        int chunk = cellToChunk[row * columns + col];
        sortedIndices[chunkWriteCursor[chunk]++] = i;
    }

    data->shadow = shadows;
    data->tuftCount = count;
    data->canonicalBlades = style.bladesPerClump;
    data->canonical = style.bladesPerClump >= 3 && style.bladeSegments >= 2 &&
                      style.growthForm != MAP_MEADOW_GROWTH_REED &&
                      style.growthForm != MAP_MEADOW_GROWTH_PAMPAS;
    for (int i = 0; i < count; i++)
    {
        if (style.growthForm == MAP_MEADOW_GROWTH_AUTO && placements[i].height > 0.95f)
        {
            data->canonical = false;
            break;
        }
    }

    const char *submission = getenv("WUXING_MEADOW_SUBMISSION");
    data->compact = !submission || strcmp(submission, "legacy") != 0;
    const char *order = getenv("WUXING_MEADOW_ORDER");
    data->nearFirst = !order || (strcmp(order, "none") != 0 && strcmp(order, "unsorted") != 0);
    const char *trace = getenv("WUXING_MEADOW_SUBMISSION_TRACE");
    data->traceEnabled = trace && *trace && *trace != '0';

    data->roots = MemAlloc(count * sizeof(*data->roots));
    data->ranks = MemAlloc(count * sizeof(*data->ranks));
    data->visibleLods = MemAlloc(count * sizeof(*data->visibleLods));
    int idRows = (count + NATURE_VISIBLE_ID_ROW_TEXELS * 4 - 1) / (NATURE_VISIBLE_ID_ROW_TEXELS * 4);
    data->idPixelCount = idRows * NATURE_VISIBLE_ID_ROW_TEXELS * 4;
    data->idPixels = MemAlloc(data->idPixelCount * sizeof(float));
    if (!data->roots || !data->ranks || !data->visibleLods || !data->idPixels)
        goto failed;
    memset(data->idPixels, 0, data->idPixelCount * sizeof(float));

    Image idImage = {.data = data->idPixels, .width = NATURE_VISIBLE_ID_ROW_TEXELS, .height = idRows, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    data->visibleIds = LoadTextureFromImage(idImage);
    if (!data->visibleIds.id)
        goto failed;
    SetTextureFilter(data->visibleIds, TEXTURE_FILTER_POINT);
    SetTextureWrap(data->visibleIds, TEXTURE_WRAP_CLAMP);

    for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
    {
        int start = data->ranges[chunk].tuftOffset;
        int end = start + data->ranges[chunk].count;
        Vector3 pFirst = placements[sortedIndices[start]].position;
        float cMinX = pFirst.x, cMaxX = pFirst.x;
        float cMinY = pFirst.y, cMaxY = pFirst.y;
        float cMinZ = pFirst.z, cMaxZ = pFirst.z;
        float maxTuftReach = 0.85f;

        for (int t = start; t < end; t++)
        {
            const MapMeadowPlacement *pl = &placements[sortedIndices[t]];
            Vector3 p = pl->position;
            data->roots[t] = p;
            data->ranks[t] = NatureParametric_Rank(p);
            if (p.x < cMinX)
                cMinX = p.x;
            if (p.x > cMaxX)
                cMaxX = p.x;
            if (p.y < cMinY)
                cMinY = p.y;
            if (p.y > cMaxY)
                cMaxY = p.y;
            if (p.z < cMinZ)
                cMinZ = p.z;
            if (p.z > cMaxZ)
                cMaxZ = p.z;
            float reach = pl->radius + pl->height * 1.15f;
            if (reach > maxTuftReach)
                maxTuftReach = reach;
        }

        Vector3 center = {
            (cMinX + cMaxX) * 0.5f,
            (cMinY + cMaxY) * 0.5f + 0.25f,
            (cMinZ + cMaxZ) * 0.5f};
        float maxDistSq = 0.0f;
        for (int t = start; t < end; t++)
        {
            Vector3 d = Vector3Subtract(data->roots[t], center);
            float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
            if (d2 > maxDistSq)
                maxDistSq = d2;
        }

        meadow->chunks[chunk] = (MapMeadowChunk){
            .center = center,
            .radius = sqrtf(maxDistSq) + maxTuftReach,
            .midReady = true,
            .realShadowReady = shadows,
            .ready = true};
    }

    // Ghost of Tsushima High-to-Low Density Blade Templates:
    // LOD 1 and LOD 2 keep 2 Bezier segments (7 vertices / 3 tris per blade) so distant blades
    // preserve their graceful curved Bezier arch instead of collapsing into flat 1-segment spikes.
    int blades[4] = {
        style.bladesPerClump,
        style.bladesPerClump >= 4 ? 3 : style.bladesPerClump,
        style.bladesPerClump >= 3 ? 2 : style.bladesPerClump,
        style.bladesPerClump >= 3 ? 2 : style.bladesPerClump};
    int segments[4] = {
        style.bladeSegments,
        style.bladeSegments >= 2 ? 2 : 1,
        style.bladeSegments >= 2 ? 2 : 1,
        2};

    float baseBlades = (float)(blades[0] > 0 ? blades[0] : 1);
    float widths[4] = {
        1.0f,
        powf(baseBlades / (float)blades[1], 0.46f),
        powf(baseBlades / (float)blades[2], 0.48f),
        powf(baseBlades / (float)blades[3], 0.45f)};

    int geometryLods = shadows ? 4 : 3;
    int atlasLods = data->canonical ? 1 : geometryLods;
    int total = 0;
    for (int lod = 0; lod < atlasLods; lod++)
        total += count * blades[lod];
    int height = (total + NATURE_PARAMETER_ROW_BLADES - 1) / NATURE_PARAMETER_ROW_BLADES;
    if (height > NATURE_PARAMETER_MAX_ROWS)
        goto failed;
    int width = NATURE_PARAMETER_COLUMNS * NATURE_PARAMETER_ROW_BLADES;
    unsigned int bytes = (unsigned int)width * height * 4 * sizeof(float);
    pixels = MemAlloc(bytes);
    if (!pixels)
        goto failed;
    memset(pixels, 0, bytes);

    int cursor = 0;
    for (int lod = 0; lod < atlasLods; lod++)
    {
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
        {
            data->ranges[chunk].offset[lod] = cursor;
            int start = data->ranges[chunk].tuftOffset;
            int end = start + data->ranges[chunk].count;
            for (int t = start; t < end; t++)
            {
                int i = sortedIndices[t];
                for (int blade = 0; blade < blades[lod]; blade++)
                {
                    NatureBladeDescriptor d = Nature_DescribeMeadowBlade(
                        &placements[i], i, blade, style, blades[lod], segments[lod], widths[lod]);
                    float *v = pixels + (size_t)cursor++ * 28;
                    const Vector4 values[7] = {
                        {d.p0.x, d.p0.y, d.p0.z, d.phase},
                        {d.p1.x, d.p1.y, d.p1.z, d.width},
                        {d.p2.x, d.p2.y, d.p2.z, d.isReed ? 1.0f : 0.0f},
                        {d.p3.x, d.p3.y, d.p3.z, d.isSeedhead ? 1.0f : 0.0f},
                        {d.clump.x, d.clump.y, d.clump.z, d.leanAngle},
                        {d.rootColor.r, d.rootColor.g, d.rootColor.b, 255.0f},
                        {d.tipColor.r, d.tipColor.g, d.tipColor.b, 255.0f}};
                    memcpy(v, values, sizeof(values));
                }
            }
        }
    }
    if (data->canonical)
    {
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
            for (int lod = 1; lod < geometryLods; lod++)
                data->ranges[chunk].offset[lod] = data->ranges[chunk].offset[0];
    }

    Image image = {.data = pixels, .width = width, .height = height, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    data->parameters = LoadTextureFromImage(image);
    MemFree(pixels);
    pixels = NULL;
    if (!data->parameters.id)
        goto failed;
    SetTextureFilter(data->parameters, TEXTURE_FILTER_POINT);
    SetTextureWrap(data->parameters, TEXTURE_WRAP_CLAMP);

    for (int lod = 0; lod < geometryLods; lod++)
        if (!NatureParametric_BuildTemplate(&data->templates[lod], blades[lod], segments[lod]))
            goto failed;

    MemFree(cellCounts);
    MemFree(cellToChunk);
    MemFree(chunkWriteCursor);
    MemFree(sortedIndices);

    long long parameterBytes = bytes;
    meadow->lodDistance = style.lodDistance;
    meadow->midLodDistance = style.midLodDistance > 0.0f
                                 ? style.midLodDistance
                                 : (style.lodDistance > 12.0f ? style.lodDistance * 0.48f : 0.0f);
    meadow->drawDistance = style.drawDistance;
    meadow->shadowDistance = style.shadowDistance;
    meadow->alphaCutoff = style.alphaCutoff;
    meadow->ready = true;
    TraceLog(LOG_INFO, "MEADOW_PARAMETRIC: chunks=%d tufts=%d parameter_bytes=%lld shared_atlas=1 canonical=%d compact=%d immutable_templates=4",
             meadow->chunkCount, count, parameterBytes, data->canonical ? 1 : 0, data->compact ? 1 : 0);
    return true;

failed:
    if (pixels)
        MemFree(pixels);
    if (cellCounts)
        MemFree(cellCounts);
    if (cellToChunk)
        MemFree(cellToChunk);
    if (chunkWriteCursor)
        MemFree(chunkWriteCursor);
    if (sortedIndices)
        MemFree(sortedIndices);
    NatureParametric_Destroy(meadow);
    if (meadow->chunks)
        MemFree(meadow->chunks);
    memset(meadow, 0, sizeof(*meadow));
    TraceLog(LOG_WARNING, "MEADOW_PARAMETRIC: initialization failed; preserving expanded mesh fallback");
    return false;
}

static void NatureParametric_BindReceivers(Shader shader, bool shadow)
{
    int pass = shadow ? 1 : 0;
    unsigned int textures[4] = {s_natureMacroTexture.id, s_natureInteractionTexture.id,
                                EnvShadow_GetShadowMap().id, EnvShadow_GetStaticShadowMap().id};
    const int units[4] = {1, 2, 4, 5};
    for (int i = 0; i < (shadow ? 2 : 4); i++)
    {
        if (s_natureReceiverSamplerLoc[pass][i] < 0)
            continue;
        rlActiveTextureSlot(units[i]);
        rlEnableTexture(textures[i] ? textures[i] : rlGetTextureIdDefault());
        SetShaderValue(shader, s_natureReceiverSamplerLoc[pass][i], &units[i], SHADER_UNIFORM_INT);
    }
    rlActiveTextureSlot(0);
}

static void NatureParametric_EndReceivers(void)
{
    for (int unit = 0; unit <= 6; unit++)
    {
        rlActiveTextureSlot(unit);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
}

static void NatureParametric_PrepareVisible(MapMeadowSurface *meadow, Vector3 worldOffset, Vector4 bands)
{
    NatureParametricMeadow *data = meadow->parametric;
    if (!data->compact && !data->nearFirst)
        return;
    if (NatureParametric_IsPrepared(meadow, worldOffset, bands))
        return;

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 localEye = Vector3Subtract(camera.position, worldOffset);

    data->drawCount = 0;
    for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
    {
        NatureTuftRange *range = &data->ranges[chunk];
        range->visibleCount[0] = 0;
        range->visibleCount[1] = 0;
        range->visibleCount[2] = 0;

        if (!meadow->chunks[chunk].visibleThisFrame)
            continue;

        Vector3 localRel = Vector3Subtract(meadow->chunks[chunk].center, localEye);
        float dist = sqrtf(localRel.x * localRel.x + localRel.y * localRel.y + localRel.z * localRel.z);
        data->chunkDistCache[chunk] = dist;

        float depth = localRel.x * forward.x + localRel.y * forward.y + localRel.z * forward.z;
        int slot = data->drawCount++;
        if (data->nearFirst)
        {
            while (slot > 0 && data->drawDepth[slot - 1] > depth)
            {
                data->drawOrder[slot] = data->drawOrder[slot - 1];
                data->drawDepth[slot] = data->drawDepth[slot - 1];
                slot--;
            }
        }
        data->drawOrder[slot] = chunk;
        data->drawDepth[slot] = depth;
    }

    data->prepared = true;
    data->preparedCamera = camera;
    data->preparedOffset = worldOffset;
    data->preparedBands = bands;
    data->preparedWidth = GetScreenWidth();
    data->preparedHeight = GetScreenHeight();
    data->preparedQuality = GfxQuality_Get();
    data->preparedDrawDistance = meadow->drawDistance;
    if (!data->compact)
        return;

    NatureMeadowView view = NatureParametric_View(meadow);
    float fadeEnd = view.drawDistance;
    float fadeEndSq = fadeEnd * fadeEnd;
    float lod0MaxD = bands.x > 0.0f ? (bands.x - bands.z) : (bands.y - bands.w);
    float lod1MinD = bands.x + bands.z;
    float lod1MaxD = bands.y - bands.w;
    float lod2MinD = bands.y + bands.w;

    float nearMinSq = (lod0MaxD > 0.0f) ? (lod0MaxD * lod0MaxD) : 0.0f;
    float farMax = bands.y + bands.w;
    float farMaxSq = farMax * farMax;
    float midInnerMinSq = (bands.x > 0.0f && lod1MinD > 0.0f) ? (lod1MinD * lod1MinD) : 0.0f;
    float midInnerMaxSq = (bands.x > 0.0f && lod1MaxD > lod1MinD) ? (lod1MaxD * lod1MaxD) : -1.0f;

    int totalLodCounts[3] = {0, 0, 0};

    // PASS 1: Phân loại LOD cho các chunk hiển thị (sử dụng khoảng cách chunk đã cache)
    for (int idx = 0; idx < data->drawCount; idx++)
    {
        int chunk = data->drawOrder[idx];
        NatureTuftRange *range = &data->ranges[chunk];
        float chunkDist = data->chunkDistCache[chunk];
        float nearD = chunkDist - meadow->chunks[chunk].radius;
        float farD = chunkDist + meadow->chunks[chunk].radius;

        if (nearD >= fadeEnd)
            continue;

        if ((bands.y <= 0.0f || farD <= lod0MaxD) && farD <= fadeEnd)
        {
            range->visibleCount[0] = range->count;
            totalLodCounts[0] += range->count;
            memset(data->visibleLods + range->tuftOffset, 0, (size_t)range->count);
        }
        else if (bands.x > 0.0f && nearD >= lod1MinD && farD <= lod1MaxD && farD <= fadeEnd)
        {
            range->visibleCount[1] = range->count;
            totalLodCounts[1] += range->count;
            memset(data->visibleLods + range->tuftOffset, 1, (size_t)range->count);
        }
        else if (nearD >= lod2MinD && farD <= fadeEnd)
        {
            range->visibleCount[2] = range->count;
            totalLodCounts[2] += range->count;
            memset(data->visibleLods + range->tuftOffset, 2, (size_t)range->count);
        }
        else
        {
            int start = range->tuftOffset;
            int end = start + range->count;
            int c0 = 0, c1 = 0, c2 = 0;
            for (int id = start; id < end; id++)
            {
                Vector3 root = data->roots[id];
                float dx = root.x - localEye.x, dy = root.y - localEye.y, dz = root.z - localEye.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 >= fadeEndSq)
                {
                    data->visibleLods[id] = 3;
                    continue;
                }
                int lod = NatureParametric_SelectLodFast(d2, data->ranks[id], bands,
                                                         nearMinSq, farMaxSq,
                                                         midInnerMinSq, midInnerMaxSq);
                data->visibleLods[id] = (unsigned char)lod;
                if (lod == 0)
                    c0++;
                else if (lod == 1)
                    c1++;
                else
                    c2++;
            }
            range->visibleCount[0] = c0;
            range->visibleCount[1] = c1;
            range->visibleCount[2] = c2;
            totalLodCounts[0] += c0;
            totalLodCounts[1] += c1;
            totalLodCounts[2] += c2;
        }
    }

    // [TỐI ƯU EARLY-Z & SINGLE-PASS PACKING]:
    // Đóng gói idPixels theo đúng thứ tự gần-ra-xa (data->drawOrder[idx]) trong 1 lượt duyệt duy nhất!
    data->bandBase[0] = 0;
    data->bandCount[0] = totalLodCounts[0];
    data->bandBase[1] = totalLodCounts[0];
    data->bandCount[1] = totalLodCounts[1];
    data->bandBase[2] = totalLodCounts[0] + totalLodCounts[1];
    data->bandCount[2] = totalLodCounts[2];

    int writeHead[3] = {data->bandBase[0], data->bandBase[1], data->bandBase[2]};

    for (int idx = 0; idx < data->drawCount; idx++)
    {
        int chunk = data->drawOrder[idx];
        NatureTuftRange *range = &data->ranges[chunk];
        range->visibleOffset[0] = writeHead[0];
        range->visibleOffset[1] = writeHead[1];
        range->visibleOffset[2] = writeHead[2];

        int n0 = range->visibleCount[0];
        int n1 = range->visibleCount[1];
        int n2 = range->visibleCount[2];
        if ((n0 | n1 | n2) == 0)
            continue;

        int start = range->tuftOffset;
        int end = start + range->count;

        if (n0 == range->count)
        {
            int w0 = writeHead[0];
            for (int id = start; id < end; id++)
                data->idPixels[w0++] = (float)id;
            writeHead[0] = w0;
        }
        else if (n1 == range->count)
        {
            int w1 = writeHead[1];
            for (int id = start; id < end; id++)
                data->idPixels[w1++] = (float)id;
            writeHead[1] = w1;
        }
        else if (n2 == range->count)
        {
            int w2 = writeHead[2];
            for (int id = start; id < end; id++)
                data->idPixels[w2++] = (float)id;
            writeHead[2] = w2;
        }
        else
        {
            for (int id = start; id < end; id++)
            {
                unsigned char lod = data->visibleLods[id];
                if (lod < 3)
                    data->idPixels[writeHead[lod]++] = (float)id;
            }
        }
    }

    int totalSubmitted = totalLodCounts[0] + totalLodCounts[1] + totalLodCounts[2];
    if (totalSubmitted > 0)
    {
        int activeRows = (totalSubmitted + NATURE_VISIBLE_ID_ROW_TEXELS * 4 - 1) / (NATURE_VISIBLE_ID_ROW_TEXELS * 4);
        Rectangle updateRec = {0.0f, 0.0f, (float)NATURE_VISIBLE_ID_ROW_TEXELS, (float)activeRows};
        UpdateTextureRec(data->visibleIds, updateRec, data->idPixels);
    }

    if (data->traceEnabled && data->visibleFrames++ % 120 == 0)
    {
        int candidateInstances = 0, compactVertices = 0, candidateVertices = 0;
        for (int idx = 0; idx < data->drawCount; idx++)
        {
            int chunk = data->drawOrder[idx];
            float d = data->chunkDistCache[chunk];
            float nearD = fmaxf(0.0f, d - meadow->chunks[chunk].radius);
            float farD = d + meadow->chunks[chunk].radius;
            for (int lod = 0; lod < 3; lod++)
            {
                compactVertices += data->ranges[chunk].visibleCount[lod] * data->templates[lod].vertexCount;
                if (!NatureParametric_LodIntersectsSphere(lod, nearD, farD, bands))
                    continue;
                candidateInstances += data->ranges[chunk].count;
                candidateVertices += data->ranges[chunk].count * data->templates[lod].vertexCount;
            }
        }
        int activeRows = (totalSubmitted + NATURE_VISIBLE_ID_ROW_TEXELS * 4 - 1) / (NATURE_VISIBLE_ID_ROW_TEXELS * 4);
        int uploadedBytes = totalSubmitted ? activeRows * NATURE_VISIBLE_ID_ROW_TEXELS * 4 * (int)sizeof(float) : 0;
        TraceLog(LOG_INFO, "MEADOW_SUBMISSION: tufts=%d submitted_visible=%d legacy_candidates=%d vertices=%d legacy_vertices=%d id_upload_bytes=%d",
                 data->tuftCount, totalSubmitted, candidateInstances, compactVertices, candidateVertices, uploadedBytes);
    }
}

static void NatureParametric_DrawChunk(MapMeadowSurface *meadow, int chunk, int lod,
                                       Shader shader, Vector3 worldOffset, bool compactNow)
{
    NatureParametricMeadow *data = meadow->parametric;
    int pass = lod == 3 ? 1 : 0;
    bool compact = pass == 0 && compactNow;
    int instanceCount = compact ? data->ranges[chunk].visibleCount[lod] : data->ranges[chunk].count;
    if (instanceCount <= 0)
        return;

    NatureTuftTemplate *mesh = &data->templates[lod];
    Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
    Matrix mvp;
    if (lod == 3)
    {
        mvp = MatrixMultiply(model, EnvShadow_GetLightVP());
    }
    else
    {
        model = MatrixMultiply(model, rlGetMatrixTransform());
        mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
    }
    SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MVP], mvp);

    const Vector4 white = {1.0f, 1.0f, 1.0f, 1.0f};
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] >= 0)
        SetShaderValue(shader, shader.locs[SHADER_LOC_COLOR_DIFFUSE], &white, SHADER_UNIFORM_VEC4);

    SetShaderValue(shader, s_natureWorldOffsetLoc[pass], &worldOffset, SHADER_UNIFORM_VEC3);
    if (pass == 0)
        SetShaderValue(shader, s_natureTuftLodLevelLoc, &lod, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureBladeCountLoc[pass], &mesh->blades, SHADER_UNIFORM_INT);

    int canonical = data->canonical ? 1 : 0, compactValue = compact ? 1 : 0;
    SetShaderValue(shader, s_natureCanonicalLoc[pass], &canonical, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureCanonicalBladesLoc[pass], &data->canonicalBlades, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureGeometryLodLoc[pass], &lod, SHADER_UNIFORM_INT);

    if (!(data->canonical && compact))
    {
        SetShaderValue(shader, s_natureBladeOffsetLoc[pass], &data->ranges[chunk].offset[lod], SHADER_UNIFORM_INT);
        SetShaderValue(shader, s_natureTuftOffsetLoc[pass], &data->ranges[chunk].tuftOffset, SHADER_UNIFORM_INT);
    }
    if (s_natureCompactLoc[pass] >= 0)
        SetShaderValue(shader, s_natureCompactLoc[pass], &compactValue, SHADER_UNIFORM_INT);

    if (compact)
    {
        SetShaderValue(shader, s_natureVisibleOffsetLoc, &data->ranges[chunk].visibleOffset[lod], SHADER_UNIFORM_INT);
        int idUnit = 6;
        rlActiveTextureSlot(idUnit);
        rlEnableTexture(data->visibleIds.id);
        SetShaderValue(shader, s_natureVisibleIdsLoc, &idUnit, SHADER_UNIFORM_INT);
    }

    rlActiveTextureSlot(0);
    rlEnableTexture(data->parameters.id);
    int unit = 0;
    SetShaderValue(shader, s_natureParameterLoc[pass], &unit, SHADER_UNIFORM_INT);

    rlEnableVertexArray(mesh->vao);
    rlDrawVertexArrayElementsInstanced(0, mesh->indexCount, NULL, instanceCount);
    rlDisableVertexArray();
}

static void NatureParametric_DrawBand(MapMeadowSurface *meadow, int lod, Shader shader,
                                      Vector3 worldOffset)
{
    NatureParametricMeadow *data = meadow->parametric;
    int instanceCount = data->bandCount[lod];
    if (instanceCount <= 0)
        return;

    NatureTuftTemplate *mesh = &data->templates[lod];
    Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
    model = MatrixMultiply(model, rlGetMatrixTransform());
    Matrix mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
    SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MVP], mvp);

    const Vector4 white = {1.0f, 1.0f, 1.0f, 1.0f};
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] >= 0)
        SetShaderValue(shader, shader.locs[SHADER_LOC_COLOR_DIFFUSE], &white, SHADER_UNIFORM_VEC4);

    SetShaderValue(shader, s_natureWorldOffsetLoc[0], &worldOffset, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_natureTuftLodLevelLoc, &lod, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureBladeCountLoc[0], &mesh->blades, SHADER_UNIFORM_INT);
    int canonical = 1, compactValue = 1;
    SetShaderValue(shader, s_natureCanonicalLoc[0], &canonical, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureCanonicalBladesLoc[0], &data->canonicalBlades, SHADER_UNIFORM_INT);
    SetShaderValue(shader, s_natureGeometryLodLoc[0], &lod, SHADER_UNIFORM_INT);
    if (s_natureCompactLoc[0] >= 0)
        SetShaderValue(shader, s_natureCompactLoc[0], &compactValue, SHADER_UNIFORM_INT);

    int bandBase = data->bandBase[lod];
    SetShaderValue(shader, s_natureVisibleOffsetLoc, &bandBase, SHADER_UNIFORM_INT);
    int idUnit = 6;
    rlActiveTextureSlot(idUnit);
    rlEnableTexture(data->visibleIds.id);
    SetShaderValue(shader, s_natureVisibleIdsLoc, &idUnit, SHADER_UNIFORM_INT);

    rlActiveTextureSlot(0);
    rlEnableTexture(data->parameters.id);
    int unit = 0;
    SetShaderValue(shader, s_natureParameterLoc[0], &unit, SHADER_UNIFORM_INT);

    rlEnableVertexArray(mesh->vao);
    rlDrawVertexArrayElementsInstanced(0, mesh->indexCount, NULL, instanceCount);
    rlDisableVertexArray();
}
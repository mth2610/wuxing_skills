// Shared indexed tuft templates and immutable per-blade authoring data.
// Included after Nature_DescribeMeadowBlade; only compact IDs upload per frame.
#define NATURE_PARAMETRIC_LODS 4
#define NATURE_PARAMETER_COLUMNS 7
#define NATURE_PARAMETER_ROW_BLADES 256
#define NATURE_PARAMETER_MAX_ROWS 2048
#define NATURE_TEMPLATE_MAX_VERTICES 230
#define NATURE_TEMPLATE_MAX_INDICES 330
#define NATURE_LOD_NEAR_BLEND_HALF_WIDTH 2.0f
#define NATURE_LOD_FAR_BLEND_HALF_WIDTH 5.0f
#define NATURE_VISIBLE_ID_ROW_TEXELS 256

typedef struct {
    unsigned int vao, vertices, indices;
    int indexCount, vertexCount, blades, segments;
} NatureTuftTemplate;

typedef struct {
    int offset[NATURE_PARAMETRIC_LODS];
    int count;
    int tuftOffset;
    int visibleOffset[3], visibleCount[3];
    Vector2 minimum; // Exact authoring bounds; reconstructing from center can round outward.
} NatureTuftRange;

typedef struct {
    Texture2D parameters;
    Texture2D visibleIds;
    NatureTuftTemplate templates[NATURE_PARAMETRIC_LODS];
    NatureTuftRange *ranges;
    int rangeCapacity;
    bool shadow;
    bool canonical, compact;
    bool nearFirst;
    int *drawOrder;
    float *drawDepth;
    int drawCount;
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

typedef struct {
    float lodScale, zoomFactor, lodDistance, drawDistance;
    Vector4 bands;
} NatureMeadowView;

static NatureMeadowView NatureParametric_View(const MapMeadowSurface *meadow)
{
    int quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH ? 1.0f
                     : quality == GFX_MED ? 0.84f : quality == GFX_LOW ? 0.68f : 0.55f;
    float lodScale = quality >= GFX_HIGH ? 1.0f : quality == GFX_MED ? 0.84f : 0.68f;
    float dx = camera.position.x-camera.target.x, dz = camera.position.z-camera.target.z;
    float focalDistance = sqrtf(dx*dx+dz*dz);
    float fovy = fmaxf(camera.fovy,15.0f)*DEG2RAD;
    float zoom = tanf(45.0f*0.5f*DEG2RAD)/tanf(fovy*0.5f);
    zoom = fminf(fmaxf(zoom,0.8f),2.5f);
    NatureMeadowView view;
    view.lodScale = lodScale;
    view.zoomFactor = zoom;
    view.lodDistance = meadow->lodDistance*lodScale*zoom;
    view.drawDistance = meadow->drawDistance > 0.0f ? focalDistance+meadow->drawDistance*rangeScale*zoom : 0.0f;
    view.bands = (Vector4){meadow->midLodDistance > 0.0f ? meadow->midLodDistance*lodScale*zoom : 0.0f,
        view.lodDistance,NATURE_LOD_NEAR_BLEND_HALF_WIDTH,NATURE_LOD_FAR_BLEND_HALF_WIDTH};
    return view;
}

static bool NatureParametric_SameVector(Vector3 a, Vector3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool NatureParametric_IsPrepared(const MapMeadowSurface *meadow, Vector3 offset, Vector4 bands)
{
    const NatureParametricMeadow *data = meadow->parametric;
    return data->prepared && NatureParametric_SameVector(data->preparedCamera.position,camera.position) &&
        NatureParametric_SameVector(data->preparedCamera.target,camera.target) &&
        NatureParametric_SameVector(data->preparedCamera.up,camera.up) &&
        data->preparedCamera.fovy == camera.fovy && data->preparedCamera.projection == camera.projection &&
        NatureParametric_SameVector(data->preparedOffset,offset) &&
        data->preparedBands.x == bands.x && data->preparedBands.y == bands.y &&
        data->preparedBands.z == bands.z && data->preparedBands.w == bands.w &&
        data->preparedWidth == GetScreenWidth() && data->preparedHeight == GetScreenHeight() &&
        data->preparedQuality == GfxQuality_Get() && data->preparedDrawDistance == meadow->drawDistance;
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
    memcpy(&x,&root.x,sizeof(x)); memcpy(&z,&root.z,sizeof(z));
    unsigned int h = x ^ (z * 0x9e3779b9u);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    h *= 0x846ca68bu; h ^= h >> 16;
    return (float)(h & 0x00ffffffu) * (1.0f / 16777216.0f);
}

static float NatureParametric_Smoothstep(float low, float high, float value)
{
    float t = fmaxf(0.0f,fminf(1.0f,(value-low)/(high-low)));
    return t*t*(3.0f-2.0f*t);
}

static int NatureParametric_SelectLod(Vector3 root, float rank, Vector3 eye, Vector4 bands)
{
    if (bands.y <= 0.0f) return 0;
    float d = Vector3Distance(root,eye);
    float farWeight = NatureParametric_Smoothstep(bands.y-bands.w,bands.y+bands.w,d);
    float midWeight = bands.x > 0.0f
        ? NatureParametric_Smoothstep(bands.x-bands.z,bands.x+bands.z,d) : 0.0f;
    return rank < farWeight ? 2 : (rank < midWeight ? 1 : 0);
}

static bool NatureParametric_LodIntersectsSphere(int lod, float nearest, float farthest, Vector4 bands)
{
    if (bands.y <= 0.0f) return lod == 0;
    if (lod == 0) return nearest <= (bands.x > 0.0f ? bands.x + bands.z : bands.y + bands.w);
    if (lod == 1) return bands.x > 0.0f && farthest >= bands.x - bands.z && nearest <= bands.y + bands.w;
    return lod == 2 && farthest >= bands.y - bands.w;
}

static Shader NatureParametric_Shader(bool shadow)
{
    Shader *shader = shadow ? &s_natureParametricShadowShader : &s_natureParametricShader;
    if (shader->id == 0) {
        *shader = ResourceManager_LoadShader(
            shadow ? "maps/toolkit/shaders/nature_shadow_parametric.vs"
                   : "maps/toolkit/shaders/nature_lit_parametric.vs",
            shadow ? "maps/toolkit/shaders/nature_shadow.fs"
                   : "maps/toolkit/shaders/nature_opaque.fs");
        int pass = shadow ? 1 : 0;
        s_natureParameterLoc[pass] = GetShaderLocation(*shader,"u_bladeParameters");
        s_natureBladeOffsetLoc[pass] = GetShaderLocation(*shader,"u_bladeOffset");
        s_natureBladeCountLoc[pass] = GetShaderLocation(*shader,"u_bladesPerTuft");
        s_natureCanonicalLoc[pass] = GetShaderLocation(*shader,"u_canonicalBladeData");
        s_natureCanonicalBladesLoc[pass] = GetShaderLocation(*shader,"u_canonicalBlades");
        s_natureGeometryLodLoc[pass] = GetShaderLocation(*shader,"u_geometryLod");
        s_natureTuftOffsetLoc[pass] = GetShaderLocation(*shader,"u_chunkTuftOffset");
        s_natureWorldOffsetLoc[pass] = GetShaderLocation(*shader,"u_worldOffset");
        s_natureCompactLoc[pass] = GetShaderLocation(*shader,"u_compactTuftSubmission");
        const char *samplers[4] = {"u_windCacheMap","u_interactionMap","shadowMap","staticShadowMap"};
        for (int i = 0; i < 4; i++) s_natureReceiverSamplerLoc[pass][i] = GetShaderLocation(*shader,samplers[i]);
        shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
        shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
        shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
        shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
        if (!shadow) {
            s_natureTuftLodBandsLoc = GetShaderLocation(*shader,"u_tuftLodBands");
            s_natureTuftLodLevelLoc = GetShaderLocation(*shader,"u_tuftLodLevel");
            s_natureTuftLodCameraLoc = GetShaderLocation(*shader,"u_tuftLodCamera");
            s_natureTuftFadeRangeLoc = GetShaderLocation(*shader,"u_tuftFadeRange");
            s_natureVisibleIdsLoc = GetShaderLocation(*shader,"u_visibleTuftIds");
            s_natureVisibleOffsetLoc = GetShaderLocation(*shader,"u_visibleTuftOffset");
            if (s_natureTuftLodBandsLoc < 0 || s_natureTuftLodLevelLoc < 0 || s_natureTuftLodCameraLoc < 0)
                TraceLog(LOG_WARNING,"MEADOW_PARAMETRIC: missing LOD uniforms; using expanded mesh fallback");
            MapShadow_ConfigureShader(*shader);
            VFXLight_RegisterShader(*shader);
        }
    }
    return *shader;
}

static bool NatureParametric_BuildTemplate(NatureTuftTemplate *mesh, int blades, int segments)
{
    Vector3 vertices[NATURE_TEMPLATE_MAX_VERTICES];
    unsigned short indices[NATURE_TEMPLATE_MAX_INDICES];
    int vertexCount = 0, indexCount = 0;
    for (int blade = 0; blade < blades; blade++) {
        for (int segment = 0; segment < segments; segment++) {
            float tipStart = segments > 1 ? 0.80f : 0.0f;
            float t0 = segment == segments-1 ? tipStart : tipStart*segment/(segments-1);
            float t1 = segment == segments-1 ? 1.0f : tipStart*(segment+1)/(segments-1);
            unsigned short base = (unsigned short)vertexCount;
            vertices[vertexCount++] = (Vector3){-1.0f,t0,blade+0.25f};
            vertices[vertexCount++] = (Vector3){ 1.0f,t0,blade+0.25f};
            if (segment == segments-1) {
                vertices[vertexCount++] = (Vector3){0.0f,1.0f,blade+0.75f};
                indices[indexCount++] = base;
                indices[indexCount++] = base+1;
                indices[indexCount++] = base+2;
            } else {
                vertices[vertexCount++] = (Vector3){ 1.0f,t1,blade+0.50f};
                vertices[vertexCount++] = (Vector3){-1.0f,t1,blade+0.50f};
                const unsigned short quad[6] = {base,base+1,base+2,base,base+2,base+3};
                for (int j = 0; j < 6; j++) indices[indexCount++] = quad[j];
            }
        }
    }
    mesh->vao = rlLoadVertexArray();
    if (!mesh->vao || !rlEnableVertexArray(mesh->vao)) return false;
    mesh->vertices = rlLoadVertexBuffer(vertices, vertexCount*sizeof(Vector3), false);
    rlSetVertexAttribute(0,3,RL_FLOAT,false,sizeof(Vector3),0);
    rlEnableVertexAttribute(0);
    mesh->indices = rlLoadVertexBufferElement(indices,indexCount*sizeof(unsigned short),false);
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
    if (!data) return;
    if (data->parameters.id) UnloadTexture(data->parameters);
    if (data->visibleIds.id) UnloadTexture(data->visibleIds);
    for (int lod = 0; lod < NATURE_PARAMETRIC_LODS; lod++) {
        if (data->templates[lod].vao) rlUnloadVertexArray(data->templates[lod].vao);
        if (data->templates[lod].vertices) rlUnloadVertexBuffer(data->templates[lod].vertices);
        if (data->templates[lod].indices) rlUnloadVertexBuffer(data->templates[lod].indices);
    }
    MemFree(data->ranges);
    MemFree(data->roots); MemFree(data->ranks); MemFree(data->idPixels);
    MemFree(data->visibleLods);
    MemFree(data->drawOrder); MemFree(data->drawDepth);
    MemFree(data);
    meadow->parametric = NULL;
}

static bool NatureParametric_Create(MapMeadowSurface *meadow,
    const MapMeadowPlacement *placements, int count, MapMeadowStyle style)
{
    const char *renderer = getenv("WUXING_MEADOW_RENDERER");
    if (style.texturePath || style.hasPlumes ||
        (renderer && strcmp(renderer,"legacy") == 0)) return false;
    Shader visible = NatureParametric_Shader(false);
    if (visible.id == rlGetShaderIdDefault() || visible.locs[SHADER_LOC_MATRIX_MVP] < 0 ||
        GetShaderLocation(visible,"u_bladeParameters") < 0 ||
        s_natureTuftLodBandsLoc < 0 || s_natureTuftLodLevelLoc < 0 || s_natureTuftLodCameraLoc < 0) return false;
    bool shadows = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_HIGH;
    if (shadows && GetShaderLocation(NatureParametric_Shader(true),"u_bladeParameters") < 0)
        return false;
    for (int pass = 0; pass < (shadows ? 2 : 1); pass++) {
        if (s_natureCanonicalLoc[pass] < 0 || s_natureCanonicalBladesLoc[pass] < 0 ||
            s_natureGeometryLodLoc[pass] < 0 || s_natureTuftOffsetLoc[pass] < 0 ||
            s_natureWorldOffsetLoc[pass] < 0) return false;
    }
    if (s_natureCompactLoc[0] < 0 || s_natureVisibleIdsLoc < 0 || s_natureVisibleOffsetLoc < 0) return false;
    float minX = placements[0].position.x, maxX = minX;
    float minZ = placements[0].position.z, maxZ = minZ;
    for (int i = 1; i < count; i++) {
        minX = fminf(minX,placements[i].position.x); maxX = fmaxf(maxX,placements[i].position.x);
        minZ = fminf(minZ,placements[i].position.z); maxZ = fmaxf(maxZ,placements[i].position.z);
    }
    int columns = (int)ceilf((maxX-minX+0.001f)/style.chunkSize);
    int rows = (int)ceilf((maxZ-minZ+0.001f)/style.chunkSize);
    int capacity = columns*rows;
    NatureParametricMeadow *data = MemAlloc(sizeof(*data));
    if (!data) return false;
    memset(data,0,sizeof(*data));
    meadow->parametric = data;
    data->ranges = MemAlloc(capacity*sizeof(*data->ranges));
    meadow->chunks = MemAlloc(capacity*sizeof(*meadow->chunks));
    if (!data->ranges || !meadow->chunks) goto failed;
    memset(data->ranges,0,capacity*sizeof(*data->ranges));
    memset(meadow->chunks,0,capacity*sizeof(*meadow->chunks));
    data->rangeCapacity = capacity;
    data->drawOrder = MemAlloc(capacity*sizeof(*data->drawOrder));
    data->drawDepth = MemAlloc(capacity*sizeof(*data->drawDepth));
    if (!data->drawOrder || !data->drawDepth) goto failed;
    data->shadow = shadows;
    data->tuftCount = count;
    data->canonicalBlades = style.bladesPerClump;
    data->canonical = style.bladesPerClump >= 3 && style.bladeSegments >= 2 &&
        style.growthForm != MAP_MEADOW_GROWTH_REED;
    for (int i = 0; i < count; i++)
        if (style.growthForm == MAP_MEADOW_GROWTH_AUTO && placements[i].height > 0.95f)
            data->canonical = false;
    const char *submission = getenv("WUXING_MEADOW_SUBMISSION");
    data->compact = submission && strcmp(submission,"compact") == 0;
    const char *order = getenv("WUXING_MEADOW_ORDER");
    data->nearFirst = order && strcmp(order,"sorted") == 0;
    data->roots = MemAlloc(count*sizeof(*data->roots));
    data->ranks = MemAlloc(count*sizeof(*data->ranks));
    data->visibleLods = MemAlloc(count*sizeof(*data->visibleLods));
    int idRows = (count+NATURE_VISIBLE_ID_ROW_TEXELS*4-1)/(NATURE_VISIBLE_ID_ROW_TEXELS*4);
    data->idPixelCount = idRows*NATURE_VISIBLE_ID_ROW_TEXELS*4;
    data->idPixels = MemAlloc(data->idPixelCount*sizeof(float));
    if (!data->roots || !data->ranks || !data->visibleLods || !data->idPixels) goto failed;
    memset(data->idPixels,0,data->idPixelCount*sizeof(float));
    Image idImage = {.data=data->idPixels,.width=NATURE_VISIBLE_ID_ROW_TEXELS,.height=idRows,
        .mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    data->visibleIds = LoadTextureFromImage(idImage);
    if (!data->visibleIds.id) goto failed;
    SetTextureFilter(data->visibleIds,TEXTURE_FILTER_POINT);
    SetTextureWrap(data->visibleIds,TEXTURE_WRAP_CLAMP);
    for (int row = 0; row < rows; row++) for (int column = 0; column < columns; column++) {
        float x0 = minX+column*style.chunkSize, z0 = minZ+row*style.chunkSize;
        int selected = 0;
        for (int i = 0; i < count; i++) {
            Vector3 p = placements[i].position;
            if (p.x >= x0 && p.x < x0+style.chunkSize && p.z >= z0 && p.z < z0+style.chunkSize) selected++;
        }
        if (!selected) continue;
        int chunk = meadow->chunkCount++;
        data->ranges[chunk].count = selected;
        data->ranges[chunk].minimum = (Vector2){x0,z0};
        meadow->chunks[chunk] = (MapMeadowChunk){
            .center = {x0+style.chunkSize*0.5f,0.0f,z0+style.chunkSize*0.5f},
            .radius = style.chunkSize*0.72f+1.5f,
            .midReady = true, .realShadowReady = shadows, .ready = true};
    }
    int blades[4] = {style.bladesPerClump,
        style.bladesPerClump >= 5 ? 4 : (style.bladesPerClump >= 3 ? 3 : style.bladesPerClump),3,3};
    int segments[4] = {style.bladeSegments,style.bladeSegments >= 3 ? 2 : 1,1,2};
    const float widths[4] = {1.0f,1.22f,1.65f,1.30f};
    int geometryLods = shadows ? 4 : 3;
    int atlasLods = data->canonical ? 1 : geometryLods;
    int total = 0;
    for (int lod = 0; lod < atlasLods; lod++) total += count*blades[lod];
    int height = (total+NATURE_PARAMETER_ROW_BLADES-1)/NATURE_PARAMETER_ROW_BLADES;
    if (height > NATURE_PARAMETER_MAX_ROWS) goto failed;
    int width = NATURE_PARAMETER_COLUMNS*NATURE_PARAMETER_ROW_BLADES;
    unsigned int bytes = (unsigned int)width*height*4*sizeof(float);
    float *pixels = MemAlloc(bytes);
    if (!pixels) goto failed;
    memset(pixels,0,bytes);
    int cursor = 0;
    for (int lod = 0; lod < atlasLods; lod++) {
        int tuft = 0;
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
            float x0 = data->ranges[chunk].minimum.x, z0 = data->ranges[chunk].minimum.y;
            data->ranges[chunk].offset[lod] = cursor;
            data->ranges[chunk].tuftOffset = tuft;
            for (int i = 0; i < count; i++) {
                Vector3 p = placements[i].position;
                if (p.x < x0 || p.x >= x0+style.chunkSize || p.z < z0 || p.z >= z0+style.chunkSize) continue;
                data->roots[tuft] = placements[i].position;
                data->ranks[tuft] = NatureParametric_Rank(placements[i].position);
                tuft++;
                for (int blade = 0; blade < blades[lod]; blade++) {
                    NatureBladeDescriptor d = Nature_DescribeMeadowBlade(&placements[i],i,blade,style,blades[lod],segments[lod],widths[lod]);
                    float *v = pixels+(size_t)cursor++*28;
                    const Vector4 values[7] = {
                        {d.p0.x,d.p0.y,d.p0.z,d.phase}, {d.p1.x,d.p1.y,d.p1.z,d.width},
                        {d.p2.x,d.p2.y,d.p2.z,d.isReed ? 1.0f : 0.0f}, {d.p3.x,d.p3.y,d.p3.z,0.0f},
                        {d.clump.x,d.clump.y,d.clump.z,d.leanAngle},
                        {d.rootColor.r,d.rootColor.g,d.rootColor.b,255.0f},
                        {d.tipColor.r,d.tipColor.g,d.tipColor.b,255.0f}};
                    memcpy(v,values,sizeof(values));
                }
            }
        }
        if (tuft != count) { MemFree(pixels); goto failed; }
    }
    if (data->canonical) {
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
            for (int lod = 1; lod < geometryLods; lod++)
                data->ranges[chunk].offset[lod] = data->ranges[chunk].offset[0];
    }
    Image image = {.data=pixels,.width=width,.height=height,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    data->parameters = LoadTextureFromImage(image);
    MemFree(pixels);
    if (!data->parameters.id) goto failed;
    SetTextureFilter(data->parameters,TEXTURE_FILTER_POINT);
    SetTextureWrap(data->parameters,TEXTURE_WRAP_CLAMP);
    for (int lod = 0; lod < geometryLods; lod++)
        if (!NatureParametric_BuildTemplate(&data->templates[lod],blades[lod],segments[lod])) goto failed;
    long long parameterBytes = bytes;
    meadow->lodDistance = style.lodDistance;
    meadow->midLodDistance = style.midLodDistance;
    meadow->drawDistance = style.drawDistance;
    meadow->shadowDistance = style.shadowDistance;
    meadow->alphaCutoff = style.alphaCutoff;
    meadow->ready = true;
    TraceLog(LOG_INFO,"MEADOW_PARAMETRIC: chunks=%d tufts=%d parameter_bytes=%lld shared_atlas=1 immutable_templates=4",meadow->chunkCount,count,parameterBytes);
    return true;
failed:
    NatureParametric_Destroy(meadow);
    if (meadow->chunks) MemFree(meadow->chunks);
    memset(meadow,0,sizeof(*meadow));
    TraceLog(LOG_WARNING,"MEADOW_PARAMETRIC: initialization failed; preserving expanded mesh fallback");
    return false;
}

// SetShaderValueTexture queues bindings for rlgl's immediate-mode batch.
// Raw VAO draws must bind receivers explicitly; never inherit a prior mesh.
static void NatureParametric_BindReceivers(Shader shader, bool shadow)
{
    int pass = shadow ? 1 : 0;
    unsigned int textures[4] = {s_natureMacroTexture.id,s_natureInteractionTexture.id,
        EnvShadow_GetShadowMap().id,EnvShadow_GetStaticShadowMap().id};
    const int units[4] = {1,2,4,5}; // Cloud field occupies unit three.
    for (int i = 0; i < (shadow ? 2 : 4); i++) {
        if (s_natureReceiverSamplerLoc[pass][i] < 0) continue;
        rlActiveTextureSlot(units[i]);
        rlEnableTexture(textures[i] ? textures[i] : rlGetTextureIdDefault());
        SetShaderValue(shader,s_natureReceiverSamplerLoc[pass][i],&units[i],SHADER_UNIFORM_INT);
    }
    rlActiveTextureSlot(0);
}

static void NatureParametric_EndReceivers(void)
{
    for (int unit = 0; unit <= 6; unit++) {
        rlActiveTextureSlot(unit);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
}

// Stable nested detail selection matches the previous shader rank/thresholds.
// Upload once before any visible draw; shadows never read this mutable ID list.
static void NatureParametric_PrepareVisible(MapMeadowSurface *meadow, Vector3 worldOffset, Vector4 bands)
{
    NatureParametricMeadow *data = meadow->parametric;
    if (!data->compact && !data->nearFirst) return;
    if (NatureParametric_IsPrepared(meadow,worldOffset,bands)) return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target,camera.position));
    data->drawCount = 0;
    for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
        if (!meadow->chunks[chunk].visibleThisFrame) continue;
        Vector3 relative = Vector3Subtract(Vector3Add(meadow->chunks[chunk].center,worldOffset),camera.position);
        float depth = Vector3DotProduct(relative,forward);
        int slot = data->drawCount++;
        // Equal-depth chunks retain source order. View depth works for both
        // perspective and orthographic cameras; shadows keep their own order.
        if (data->nearFirst) while (slot > 0 && data->drawDepth[slot-1] > depth) {
            data->drawOrder[slot] = data->drawOrder[slot-1];
            data->drawDepth[slot] = data->drawDepth[slot-1];
            slot--;
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
    if (!data->compact) return;
    for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
        NatureTuftRange *range = &data->ranges[chunk];
        for (int lod = 0; lod < 3; lod++) range->visibleCount[lod] = 0;
        if (!meadow->chunks[chunk].visibleThisFrame) continue;
        for (int local = 0; local < range->count; local++) {
            int id = range->tuftOffset+local;
            Vector3 world = Vector3Add(data->roots[id],worldOffset);
            int lod = NatureParametric_SelectLod(world,data->ranks[id],camera.position,bands);
            data->visibleLods[id] = (unsigned char)lod;
            range->visibleCount[lod]++;
        }
    }
    int cursor = 0;
    for (int lod = 0; lod < 3; lod++) for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
        NatureTuftRange *range = &data->ranges[chunk];
        range->visibleOffset[lod] = cursor;
        if (!meadow->chunks[chunk].visibleThisFrame) continue;
        for (int local = 0; local < range->count; local++) {
            int id = range->tuftOffset+local;
            if (data->visibleLods[id] == lod) data->idPixels[cursor++] = (float)id;
        }
    }
    if (cursor > 0) UpdateTexture(data->visibleIds,data->idPixels);
    const char *trace = getenv("WUXING_MEADOW_SUBMISSION_TRACE");
    if (trace && *trace && *trace != '0' && data->visibleFrames++ % 120 == 0) {
        int candidateInstances = 0, compactVertices = 0, candidateVertices = 0;
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
            if (!meadow->chunks[chunk].visibleThisFrame) continue;
            float d = Vector3Distance(camera.position,Vector3Add(meadow->chunks[chunk].center,worldOffset));
            float nearD = fmaxf(0.0f,d-meadow->chunks[chunk].radius), farD = d+meadow->chunks[chunk].radius;
            for (int lod = 0; lod < 3; lod++) {
                compactVertices += data->ranges[chunk].visibleCount[lod]*data->templates[lod].vertexCount;
                if (!NatureParametric_LodIntersectsSphere(lod,nearD,farD,bands)) continue;
                candidateInstances += data->ranges[chunk].count;
                candidateVertices += data->ranges[chunk].count*data->templates[lod].vertexCount;
            }
        }
        TraceLog(LOG_INFO,"MEADOW_SUBMISSION: tufts=%d submitted_visible=%d legacy_candidates=%d vertices=%d legacy_vertices=%d id_upload_bytes=%d",data->tuftCount,cursor,candidateInstances,compactVertices,candidateVertices,cursor ? data->idPixelCount*(int)sizeof(float) : 0);
    }
}

static void NatureParametric_DrawChunk(MapMeadowSurface *meadow, int chunk, int lod,
                                      Shader shader, Vector3 worldOffset)
{
    NatureParametricMeadow *data = meadow->parametric;
    NatureTuftTemplate *mesh = &data->templates[lod];
    Matrix model = MatrixTranslate(worldOffset.x,worldOffset.y,worldOffset.z);
    Matrix mvp;
    if (lod == 3) {
        mvp = MatrixMultiply(model, EnvShadow_GetLightVP());
    } else {
        model = MatrixMultiply(model, rlGetMatrixTransform());
        mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
    }
    SetShaderValueMatrix(shader,shader.locs[SHADER_LOC_MATRIX_MVP],mvp);
    const Vector4 white = {1.0f,1.0f,1.0f,1.0f};
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] >= 0)
        SetShaderValue(shader,shader.locs[SHADER_LOC_COLOR_DIFFUSE],&white,SHADER_UNIFORM_VEC4);
    int pass = lod == 3 ? 1 : 0;
    bool compact = pass == 0 && data->compact;
    int instanceCount = compact ? data->ranges[chunk].visibleCount[lod] : data->ranges[chunk].count;
    if (instanceCount <= 0) return;
    // This path supports translation only. Keep the exact offset rather than
    // cancelling the camera transform separately for every template vertex.
    SetShaderValue(shader,s_natureWorldOffsetLoc[pass],&worldOffset,SHADER_UNIFORM_VEC3);
    if (pass == 0)
        SetShaderValue(shader,s_natureTuftLodLevelLoc,&lod,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureBladeCountLoc[pass],&mesh->blades,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureBladeOffsetLoc[pass],&data->ranges[chunk].offset[lod],SHADER_UNIFORM_INT);
    int canonical = data->canonical ? 1 : 0, compactValue = compact ? 1 : 0;
    SetShaderValue(shader,s_natureCanonicalLoc[pass],&canonical,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureCanonicalBladesLoc[pass],&data->canonicalBlades,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureGeometryLodLoc[pass],&lod,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureTuftOffsetLoc[pass],&data->ranges[chunk].tuftOffset,SHADER_UNIFORM_INT);
    if (s_natureCompactLoc[pass] >= 0)
        SetShaderValue(shader,s_natureCompactLoc[pass],&compactValue,SHADER_UNIFORM_INT);
    if (compact) {
        SetShaderValue(shader,s_natureVisibleOffsetLoc,&data->ranges[chunk].visibleOffset[lod],SHADER_UNIFORM_INT);
        int idUnit = 6;
        rlActiveTextureSlot(idUnit); rlEnableTexture(data->visibleIds.id);
        SetShaderValue(shader,s_natureVisibleIdsLoc,&idUnit,SHADER_UNIFORM_INT);
    }
    // Canonical unit zero also distinguishes atlas changes in rlvk's mesh
    // binding cache. Opaque grass has no diffuse sampler to consume this unit.
    rlActiveTextureSlot(0);
    rlEnableTexture(data->parameters.id);
    int unit = 0;
    SetShaderValue(shader,s_natureParameterLoc[pass],&unit,SHADER_UNIFORM_INT);
    rlEnableVertexArray(mesh->vao);
    rlDrawVertexArrayElementsInstanced(0,mesh->indexCount,NULL,instanceCount);
    rlDisableVertexArray();
}

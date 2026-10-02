// Shared indexed tuft templates and immutable per-blade authoring data.
// Included after Nature_DescribeMeadowBlade; no per-frame allocation/upload.
#define NATURE_PARAMETRIC_LODS 4
#define NATURE_PARAMETER_COLUMNS 7
#define NATURE_PARAMETER_ROW_BLADES 256
#define NATURE_PARAMETER_MAX_ROWS 2048
#define NATURE_TEMPLATE_MAX_VERTICES 230
#define NATURE_TEMPLATE_MAX_INDICES 330
#define NATURE_LOD_NEAR_BLEND_HALF_WIDTH 2.0f
#define NATURE_LOD_FAR_BLEND_HALF_WIDTH 5.0f

typedef struct {
    unsigned int vao, vertices, indices;
    int indexCount, blades, segments;
} NatureTuftTemplate;

typedef struct {
    int offset[NATURE_PARAMETRIC_LODS];
    int count;
    Vector2 minimum; // Exact authoring bounds; reconstructing from center can round outward.
} NatureTuftRange;

typedef struct {
    Texture2D parameters[NATURE_PARAMETRIC_LODS];
    NatureTuftTemplate templates[NATURE_PARAMETRIC_LODS];
    NatureTuftRange *ranges;
    int rangeCapacity;
    bool shadow;
} NatureParametricMeadow;

static Shader s_natureParametricShader = {0};
static Shader s_natureParametricShadowShader = {0};
static int s_natureParameterLoc[2], s_natureBladeOffsetLoc[2], s_natureBladeCountLoc[2];
static int s_natureReceiverSamplerLoc[2][4];
static int s_natureTuftLodBandsLoc = -1, s_natureTuftLodLevelLoc = -1;
static int s_natureTuftLodCameraLoc = -1;

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
    mesh->blades = blades;
    mesh->segments = segments;
    return mesh->vao != 0 && mesh->vertices != 0 && mesh->indices != 0;
}

static void NatureParametric_Destroy(MapMeadowSurface *meadow)
{
    NatureParametricMeadow *data = meadow->parametric;
    if (!data) return;
    for (int lod = 0; lod < NATURE_PARAMETRIC_LODS; lod++) {
        if (data->parameters[lod].id) UnloadTexture(data->parameters[lod]);
        if (data->templates[lod].vao) rlUnloadVertexArray(data->templates[lod].vao);
        if (data->templates[lod].vertices) rlUnloadVertexBuffer(data->templates[lod].vertices);
        if (data->templates[lod].indices) rlUnloadVertexBuffer(data->templates[lod].indices);
    }
    MemFree(data->ranges);
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
    data->shadow = shadows;
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
    long long parameterBytes = 0;
    for (int lod = 0; lod < (shadows ? 4 : 3); lod++) {
        int total = 0;
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++)
            total += data->ranges[chunk].count*blades[lod];
        int height = (total+NATURE_PARAMETER_ROW_BLADES-1)/NATURE_PARAMETER_ROW_BLADES;
        if (height > NATURE_PARAMETER_MAX_ROWS) goto failed;
        int width = NATURE_PARAMETER_COLUMNS*NATURE_PARAMETER_ROW_BLADES;
        unsigned int bytes = (unsigned int)width*height*4*sizeof(float);
        float *pixels = MemAlloc(bytes);
        if (!pixels) goto failed;
        memset(pixels,0,bytes);
        int cursor = 0;
        for (int chunk = 0; chunk < meadow->chunkCount; chunk++) {
            float x0 = data->ranges[chunk].minimum.x, z0 = data->ranges[chunk].minimum.y;
            data->ranges[chunk].offset[lod] = cursor;
            for (int i = 0; i < count; i++) {
                Vector3 p = placements[i].position;
                if (p.x < x0 || p.x >= x0+style.chunkSize || p.z < z0 || p.z >= z0+style.chunkSize) continue;
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
        Image image = {.data=pixels,.width=width,.height=height,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
        data->parameters[lod] = LoadTextureFromImage(image);
        MemFree(pixels);
        if (!data->parameters[lod].id || !NatureParametric_BuildTemplate(&data->templates[lod],blades[lod],segments[lod])) goto failed;
        SetTextureFilter(data->parameters[lod],TEXTURE_FILTER_POINT);
        SetTextureWrap(data->parameters[lod],TEXTURE_WRAP_CLAMP);
        parameterBytes += bytes;
    }
    meadow->lodDistance = style.lodDistance;
    meadow->midLodDistance = style.midLodDistance > 0.0f ? style.midLodDistance : style.lodDistance*0.45f;
    meadow->drawDistance = style.drawDistance;
    meadow->shadowDistance = style.shadowDistance;
    meadow->alphaCutoff = style.alphaCutoff;
    meadow->ready = true;
    TraceLog(LOG_INFO,"MEADOW_PARAMETRIC: chunks=%d tufts=%d parameter_bytes=%lld immutable_templates=4",meadow->chunkCount,count,parameterBytes);
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
    for (int unit = 0; unit <= 5; unit++) {
        rlActiveTextureSlot(unit);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
}

static void NatureParametric_DrawChunk(MapMeadowSurface *meadow, int chunk, int lod,
                                      Shader shader, Vector3 worldOffset)
{
    NatureParametricMeadow *data = meadow->parametric;
    NatureTuftTemplate *mesh = &data->templates[lod];
    Matrix model = MatrixMultiply(MatrixTranslate(worldOffset.x,worldOffset.y,worldOffset.z),rlGetMatrixTransform());
    Matrix mvp = MatrixMultiply(MatrixMultiply(model,rlGetMatrixModelview()),rlGetMatrixProjection());
    SetShaderValueMatrix(shader,shader.locs[SHADER_LOC_MATRIX_MODEL],model);
    SetShaderValueMatrix(shader,shader.locs[SHADER_LOC_MATRIX_MVP],mvp);
    const Vector4 white = {1.0f,1.0f,1.0f,1.0f};
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] >= 0)
        SetShaderValue(shader,shader.locs[SHADER_LOC_COLOR_DIFFUSE],&white,SHADER_UNIFORM_VEC4);
    int pass = lod == 3 ? 1 : 0;
    if (pass == 0)
        SetShaderValue(shader,s_natureTuftLodLevelLoc,&lod,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureBladeCountLoc[pass],&mesh->blades,SHADER_UNIFORM_INT);
    SetShaderValue(shader,s_natureBladeOffsetLoc[pass],&data->ranges[chunk].offset[lod],SHADER_UNIFORM_INT);
    // Canonical unit zero also distinguishes atlas changes in rlvk's mesh
    // binding cache. Opaque grass has no diffuse sampler to consume this unit.
    rlActiveTextureSlot(0);
    rlEnableTexture(data->parameters[lod].id);
    int unit = 0;
    SetShaderValue(shader,s_natureParameterLoc[pass],&unit,SHADER_UNIFORM_INT);
    rlEnableVertexArray(mesh->vao);
    rlDrawVertexArrayElementsInstanced(0,mesh->indexCount,NULL,data->ranges[chunk].count);
    rlDisableVertexArray();
}

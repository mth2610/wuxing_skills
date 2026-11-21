#ifndef MAP_TOOLKIT_MAP_PROPS_LAVA_INL
#define MAP_TOOLKIT_MAP_PROPS_LAVA_INL

// Molten Lava & Magma Surface Renderer (#include'd in map_props.c)

static int s_lavaLocTime = -1;
static int s_lavaLocWaveHeight = -1;
static int s_lavaLocFlowDir = -1;
static int s_lavaLocFlowSpeed = -1;
static int s_lavaLocCrustCov = -1;
static int s_lavaLocGlow = -1;
static int s_lavaLocPulse = -1;
static int s_lavaLocCoreCol = -1;
static int s_lavaLocMagmaCol = -1;
static int s_lavaLocCrustCol = -1;
static int s_lavaLocViewPos = -1;
static int s_lavaLocAmbientCol = -1;
static int s_lavaLocCrackTex = -1;
static int s_lavaLocFlowTex = -1;
static int s_lavaLocFlowEnabled = -1;
static int s_lavaLocBounds = -1;
static int s_lavaLocLightDir = -1;
static int s_lavaLocLightCol = -1;
static unsigned int s_lavaShaderId = 0;

static void Lava_EnsureShaderLocs(Shader shader)
{
    if (shader.id == 0 || s_lavaShaderId == shader.id)
        return;
    shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");

    s_lavaLocTime = GetShaderLocation(shader, "u_time");
    s_lavaLocWaveHeight = GetShaderLocation(shader, "u_waveHeight");
    s_lavaLocFlowDir = GetShaderLocation(shader, "u_flowDirection");
    s_lavaLocFlowSpeed = GetShaderLocation(shader, "u_flowSpeed");
    s_lavaLocCrustCov = GetShaderLocation(shader, "u_crustCoverage");
    s_lavaLocGlow = GetShaderLocation(shader, "u_glowIntensity");
    s_lavaLocPulse = GetShaderLocation(shader, "u_pulseSpeed");
    s_lavaLocCoreCol = GetShaderLocation(shader, "u_coreColor");
    s_lavaLocMagmaCol = GetShaderLocation(shader, "u_magmaColor");
    s_lavaLocCrustCol = GetShaderLocation(shader, "u_crustColor");
    s_lavaLocViewPos = GetShaderLocation(shader, "u_viewPos");
    s_lavaLocAmbientCol = GetShaderLocation(shader, "u_ambientColor");
    s_lavaLocCrackTex = GetShaderLocation(shader, "u_crackTex");
    s_lavaLocFlowTex = GetShaderLocation(shader, "u_lavaFlowTex");
    s_lavaLocFlowEnabled = GetShaderLocation(shader, "u_lavaFlowEnabled");
    s_lavaLocBounds = GetShaderLocation(shader, "u_lavaBounds");
    s_lavaLocLightDir = GetShaderLocation(shader, "u_lightDir");
    s_lavaLocLightCol = GetShaderLocation(shader, "u_lightColor");
    s_lavaShaderId = shader.id;
}

MapLavaSurface MapProp_CreateTerrainLava(const char *meshPath, MapLavaConfig config)
{
    MapLavaSurface lava = {0};
    if (!meshPath || !FileExists(meshPath))
        return lava;

    if (config.flowSpeed <= 0.0f) config.flowSpeed = 0.28f;
    if (config.crustCoverage <= 0.0f) config.crustCoverage = 0.56f;
    if (config.glowIntensity <= 0.0f) config.glowIntensity = 2.40f;
    if (config.pulseSpeed <= 0.0f) config.pulseSpeed = 0.85f;
    if (config.waveHeight <= 0.0f) config.waveHeight = 0.025f;
    if (config.coreColor.a == 0) config.coreColor = (Color){255, 225, 115, 255};
    if (config.magmaColor.a == 0) config.magmaColor = (Color){255, 82, 16, 255};
    if (config.crustColor.a == 0) config.crustColor = (Color){24, 20, 22, 255};

    lava.model = LoadModel(meshPath);
    if (lava.model.meshCount <= 0 || lava.model.materialCount <= 0)
        return lava;

    lava.shader = ResourceManager_LoadShader("maps/toolkit/shaders/lava_surface.vs",
                                             "maps/toolkit/shaders/lava_surface.fs");
    Lava_EnsureShaderLocs(lava.shader);

    lava.noiseTex = MapLoadMippedTexture("assets/textures/cloud_noise.png", TEXTURE_WRAP_REPEAT);
    if (FileExists("assets/maps/ly_son/textures/lava_voronoi_plates.png"))
        lava.crackTex = MapLoadMippedTexture("assets/maps/ly_son/textures/lava_voronoi_plates.png", TEXTURE_WRAP_REPEAT);
    else
        lava.crackTex = MapLoadMippedTexture("assets/textures/tex_crack_mask.png", TEXTURE_WRAP_REPEAT);
    if (config.flowMapPath && FileExists(config.flowMapPath))
        lava.flowTex = ResourceManager_LoadTextureVariant(config.flowMapPath, false, TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_CLAMP);
    if (config.mapWidth <= 0.0f) config.mapWidth = 300.0f;
    if (config.mapDepth <= 0.0f) config.mapDepth = 211.0f;

    lava.model.materials[0].shader = lava.shader;
    if (lava.noiseTex.id != 0)
        lava.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lava.noiseTex;

    BoundingBox bb = GetModelBoundingBox(lava.model);
    lava.boundsCenter = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
    lava.boundsRadius = Vector3Distance(bb.min, bb.max) * 0.5f + 2.0f;
    lava.config = config;
    lava.ready = (lava.shader.id != 0);
    return lava;
}

void MapProp_DrawLavaSurface(const MapLavaSurface *lava, float time)
{
    if (!lava || !lava->ready || lava->shader.id == 0)
        return;

    MapPropViewFrustum frustum;
    if (MapProp_BuildViewFrustum(&frustum) &&
        MapProp_FrustumCullsSphere(&frustum, lava->boundsCenter, lava->boundsRadius))
    {
        return;
    }

    Lava_EnsureShaderLocs(lava->shader);
    BeginShaderMode(lava->shader);

    if (s_lavaLocTime >= 0)
        SetShaderValue(lava->shader, s_lavaLocTime, &time, SHADER_UNIFORM_FLOAT);
    if (s_lavaLocWaveHeight >= 0)
        SetShaderValue(lava->shader, s_lavaLocWaveHeight, &lava->config.waveHeight, SHADER_UNIFORM_FLOAT);
    if (s_lavaLocFlowDir >= 0)
        SetShaderValue(lava->shader, s_lavaLocFlowDir, &lava->config.flowDirection, SHADER_UNIFORM_VEC2);
    if (s_lavaLocFlowSpeed >= 0)
        SetShaderValue(lava->shader, s_lavaLocFlowSpeed, &lava->config.flowSpeed, SHADER_UNIFORM_FLOAT);
    if (s_lavaLocCrustCov >= 0)
        SetShaderValue(lava->shader, s_lavaLocCrustCov, &lava->config.crustCoverage, SHADER_UNIFORM_FLOAT);
    if (s_lavaLocGlow >= 0)
        SetShaderValue(lava->shader, s_lavaLocGlow, &lava->config.glowIntensity, SHADER_UNIFORM_FLOAT);
    if (s_lavaLocPulse >= 0)
        SetShaderValue(lava->shader, s_lavaLocPulse, &lava->config.pulseSpeed, SHADER_UNIFORM_FLOAT);

    Vector3 coreRgb = {lava->config.coreColor.r / 255.0f, lava->config.coreColor.g / 255.0f, lava->config.coreColor.b / 255.0f};
    Vector3 magmaRgb = {lava->config.magmaColor.r / 255.0f, lava->config.magmaColor.g / 255.0f, lava->config.magmaColor.b / 255.0f};
    Vector3 crustRgb = {lava->config.crustColor.r / 255.0f, lava->config.crustColor.g / 255.0f, lava->config.crustColor.b / 255.0f};
    if (s_lavaLocCoreCol >= 0) SetShaderValue(lava->shader, s_lavaLocCoreCol, &coreRgb, SHADER_UNIFORM_VEC3);
    if (s_lavaLocMagmaCol >= 0) SetShaderValue(lava->shader, s_lavaLocMagmaCol, &magmaRgb, SHADER_UNIFORM_VEC3);
    if (s_lavaLocCrustCol >= 0) SetShaderValue(lava->shader, s_lavaLocCrustCol, &crustRgb, SHADER_UNIFORM_VEC3);

    if (s_lavaLocViewPos >= 0)
        SetShaderValue(lava->shader, s_lavaLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);

    Color amb = Environment_GetAmbientColor();
    Vector3 ambRgb = {amb.r / 255.0f, amb.g / 255.0f, amb.b / 255.0f};
    if (s_lavaLocAmbientCol >= 0)
        SetShaderValue(lava->shader, s_lavaLocAmbientCol, &ambRgb, SHADER_UNIFORM_VEC3);

    Vector3 lDir = Vector3Negate(Environment_GetSunDirection());
    if (s_lavaLocLightDir >= 0)
        SetShaderValue(lava->shader, s_lavaLocLightDir, &lDir, SHADER_UNIFORM_VEC3);
    Color sunC = Environment_GetSunColor();
    float sunI = Environment_GetSunIntensity();
    Vector3 sunRgb = {sunC.r / 255.0f * sunI, sunC.g / 255.0f * sunI, sunC.b / 255.0f * sunI};
    if (s_lavaLocLightCol >= 0)
        SetShaderValue(lava->shader, s_lavaLocLightCol, &sunRgb, SHADER_UNIFORM_VEC3);

    Vector2 bounds = {lava->config.mapWidth > 0.0f ? lava->config.mapWidth : 300.0f,
                      lava->config.mapDepth > 0.0f ? lava->config.mapDepth : 211.0f};
    if (s_lavaLocBounds >= 0)
        SetShaderValue(lava->shader, s_lavaLocBounds, &bounds, SHADER_UNIFORM_VEC2);
    int flowEnabled = (lava->flowTex.id != 0) ? 1 : 0;
    if (s_lavaLocFlowEnabled >= 0)
        SetShaderValue(lava->shader, s_lavaLocFlowEnabled, &flowEnabled, SHADER_UNIFORM_INT);

    if (s_lavaLocCrackTex >= 0 && lava->crackTex.id != 0)
    {
        int slot = 1;
        rlActiveTextureSlot(slot);
        rlEnableTexture(lava->crackTex.id);
        SetShaderValue(lava->shader, s_lavaLocCrackTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }
    if (s_lavaLocFlowTex >= 0 && lava->flowTex.id != 0)
    {
        int slot = 2;
        rlActiveTextureSlot(slot);
        rlEnableTexture(lava->flowTex.id);
        SetShaderValue(lava->shader, s_lavaLocFlowTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }

    DrawModel(lava->model, Vector3Zero(), 1.0f, WHITE);
    rlDrawRenderBatchActive();

    if (s_lavaLocFlowTex >= 0 && lava->flowTex.id != 0)
    {
        rlActiveTextureSlot(2);
        rlDisableTexture();
    }
    if (s_lavaLocCrackTex >= 0 && lava->crackTex.id != 0)
    {
        rlActiveTextureSlot(1);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    EndShaderMode();
}

void MapProp_UnloadLavaSurface(MapLavaSurface *lava)
{
    if (!lava || !lava->ready)
        return;
    if (lava->model.materialCount > 0)
        lava->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){0};
    UnloadModel(lava->model);
    lava->noiseTex = (Texture2D){0};
    lava->crackTex = (Texture2D){0};
    lava->flowTex = (Texture2D){0};
    lava->ready = false;
}

#endif // MAP_TOOLKIT_MAP_PROPS_LAVA_INL

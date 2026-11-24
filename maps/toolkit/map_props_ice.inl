#ifndef MAP_TOOLKIT_MAP_PROPS_ICE_INL
#define MAP_TOOLKIT_MAP_PROPS_ICE_INL

// Conformal Glacial Ice Shelf & Floating Voronoi Ice Floe Renderer (included in map_props.c)

static int s_iceLocTime = -1;
static int s_iceLocDriftDir = -1;
static int s_iceLocDriftSpeed = -1;
static int s_iceLocFloeCov = -1;
static int s_iceLocCrackInt = -1;
static int s_iceLocSnowCrust = -1;
static int s_iceLocDeepCol = -1;
static int s_iceLocShallowCol = -1;
static int s_iceLocSnowCol = -1;
static int s_iceLocViewPos = -1;
static int s_iceLocAmbientCol = -1;
static int s_iceLocPlateTex = -1;
static int s_iceLocFlowTex = -1;
static int s_iceLocFlowEnabled = -1;
static int s_iceLocBounds = -1;
static int s_iceLocLightDir = -1;
static int s_iceLocLightCol = -1;
static unsigned int s_iceShaderId = 0;

static void Ice_EnsureShaderLocs(Shader shader)
{
    if (shader.id == 0 || s_iceShaderId == shader.id)
        return;
    shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");

    s_iceLocTime = GetShaderLocation(shader, "u_time");
    s_iceLocDriftDir = GetShaderLocation(shader, "u_driftDirection");
    s_iceLocDriftSpeed = GetShaderLocation(shader, "u_driftSpeed");
    s_iceLocFloeCov = GetShaderLocation(shader, "u_floeCoverage");
    s_iceLocCrackInt = GetShaderLocation(shader, "u_crackIntensity");
    s_iceLocSnowCrust = GetShaderLocation(shader, "u_snowCrustAmount");
    s_iceLocDeepCol = GetShaderLocation(shader, "u_deepIceColor");
    s_iceLocShallowCol = GetShaderLocation(shader, "u_shallowIceColor");
    s_iceLocSnowCol = GetShaderLocation(shader, "u_snowRimeColor");
    s_iceLocViewPos = GetShaderLocation(shader, "u_viewPos");
    s_iceLocAmbientCol = GetShaderLocation(shader, "u_ambientColor");
    s_iceLocPlateTex = GetShaderLocation(shader, "u_plateTex");
    s_iceLocFlowTex = GetShaderLocation(shader, "u_iceFlowTex");
    s_iceLocFlowEnabled = GetShaderLocation(shader, "u_iceFlowEnabled");
    s_iceLocBounds = GetShaderLocation(shader, "u_iceBounds");
    s_iceLocLightDir = GetShaderLocation(shader, "u_lightDir");
    s_iceLocLightCol = GetShaderLocation(shader, "u_lightColor");
    s_iceShaderId = shader.id;
}

MapIceSurface MapProp_CreateTerrainIce(const char *meshPath, MapIceConfig config)
{
    MapIceSurface ice = {0};
    if (!meshPath || !FileExists(meshPath))
        return ice;

    if (config.driftSpeed <= 0.0f) config.driftSpeed = 0.08f;
    if (config.floeCoverage <= 0.0f) config.floeCoverage = 0.48f;
    if (config.crackIntensity <= 0.0f) config.crackIntensity = 1.35f;
    if (config.snowCrustAmount <= 0.0f) config.snowCrustAmount = 0.65f;
    if (config.deepIceColor.a == 0) config.deepIceColor = (Color){18, 72, 118, 255};
    if (config.shallowIceColor.a == 0) config.shallowIceColor = (Color){126, 204, 228, 255};
    if (config.snowRimeColor.a == 0) config.snowRimeColor = (Color){236, 244, 252, 255};

    ice.model = LoadModel(meshPath);
    if (ice.model.meshCount <= 0 || ice.model.materialCount <= 0)
        return ice;

    ice.shader = ResourceManager_LoadShader("maps/toolkit/shaders/ice_surface.vs",
                                            "maps/toolkit/shaders/ice_surface.fs");
    Ice_EnsureShaderLocs(ice.shader);
    MapShadow_ConfigureShader(ice.shader);

    ice.noiseTex = MapLoadMippedTexture("assets/textures/cloud_noise.png", TEXTURE_WRAP_REPEAT);
    if (FileExists("assets/maps/mau_son/textures/glacial_ice_plates.png"))
        ice.plateTex = MapLoadMippedTexture("assets/maps/mau_son/textures/glacial_ice_plates.png", TEXTURE_WRAP_REPEAT);
    else
        ice.plateTex = ice.noiseTex;
    if (config.flowMapPath && FileExists(config.flowMapPath))
        ice.flowTex = ResourceManager_LoadTextureVariant(config.flowMapPath, false, TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_CLAMP);
    if (config.mapWidth <= 0.0f) config.mapWidth = 300.0f;
    if (config.mapDepth <= 0.0f) config.mapDepth = 225.0f;

    ice.model.materials[0].shader = ice.shader;
    if (ice.noiseTex.id != 0)
        ice.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = ice.noiseTex;

    BoundingBox bb = GetModelBoundingBox(ice.model);
    ice.boundsCenter = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
    ice.boundsRadius = Vector3Distance(bb.min, bb.max) * 0.5f + 2.0f;
    ice.config = config;
    ice.ready = (ice.shader.id != 0);
    return ice;
}

void MapProp_DrawIceSurface(const MapIceSurface *ice, float time)
{
    if (!ice || !ice->ready || ice->shader.id == 0)
        return;

    MapPropViewFrustum frustum;
    if (MapProp_BuildViewFrustum(&frustum) &&
        MapProp_FrustumCullsSphere(&frustum, ice->boundsCenter, ice->boundsRadius))
    {
        return;
    }

    Ice_EnsureShaderLocs(ice->shader);
    MapShadow_AttachMaterial((Material *)&ice->model.materials[0]);
    BeginShaderMode(ice->shader);
    MapShadow_UpdateShader(ice->shader);
    Environment_BindCloudShadowShader(ice->shader);

    if (s_iceLocTime >= 0)
        SetShaderValue(ice->shader, s_iceLocTime, &time, SHADER_UNIFORM_FLOAT);
    if (s_iceLocDriftDir >= 0)
        SetShaderValue(ice->shader, s_iceLocDriftDir, &ice->config.driftDirection, SHADER_UNIFORM_VEC2);
    if (s_iceLocDriftSpeed >= 0)
        SetShaderValue(ice->shader, s_iceLocDriftSpeed, &ice->config.driftSpeed, SHADER_UNIFORM_FLOAT);
    if (s_iceLocFloeCov >= 0)
        SetShaderValue(ice->shader, s_iceLocFloeCov, &ice->config.floeCoverage, SHADER_UNIFORM_FLOAT);
    if (s_iceLocCrackInt >= 0)
        SetShaderValue(ice->shader, s_iceLocCrackInt, &ice->config.crackIntensity, SHADER_UNIFORM_FLOAT);
    if (s_iceLocSnowCrust >= 0)
        SetShaderValue(ice->shader, s_iceLocSnowCrust, &ice->config.snowCrustAmount, SHADER_UNIFORM_FLOAT);

    Vector3 deepRgb = {ice->config.deepIceColor.r / 255.0f, ice->config.deepIceColor.g / 255.0f, ice->config.deepIceColor.b / 255.0f};
    Vector3 shallowRgb = {ice->config.shallowIceColor.r / 255.0f, ice->config.shallowIceColor.g / 255.0f, ice->config.shallowIceColor.b / 255.0f};
    Vector3 snowRgb = {ice->config.snowRimeColor.r / 255.0f, ice->config.snowRimeColor.g / 255.0f, ice->config.snowRimeColor.b / 255.0f};
    if (s_iceLocDeepCol >= 0) SetShaderValue(ice->shader, s_iceLocDeepCol, &deepRgb, SHADER_UNIFORM_VEC3);
    if (s_iceLocShallowCol >= 0) SetShaderValue(ice->shader, s_iceLocShallowCol, &shallowRgb, SHADER_UNIFORM_VEC3);
    if (s_iceLocSnowCol >= 0) SetShaderValue(ice->shader, s_iceLocSnowCol, &snowRgb, SHADER_UNIFORM_VEC3);

    if (s_iceLocViewPos >= 0)
        SetShaderValue(ice->shader, s_iceLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);

    Color amb = Environment_GetAmbientColor();
    Vector3 ambRgb = {amb.r / 255.0f, amb.g / 255.0f, amb.b / 255.0f};
    if (s_iceLocAmbientCol >= 0)
        SetShaderValue(ice->shader, s_iceLocAmbientCol, &ambRgb, SHADER_UNIFORM_VEC3);

    Vector3 lDir = Vector3Negate(Environment_GetSunDirection());
    if (s_iceLocLightDir >= 0)
        SetShaderValue(ice->shader, s_iceLocLightDir, &lDir, SHADER_UNIFORM_VEC3);
    Color sunC = Environment_GetSunColor();
    float sunI = Environment_GetSunIntensity();
    Vector3 sunRgb = {sunC.r / 255.0f * sunI, sunC.g / 255.0f * sunI, sunC.b / 255.0f * sunI};
    if (s_iceLocLightCol >= 0)
        SetShaderValue(ice->shader, s_iceLocLightCol, &sunRgb, SHADER_UNIFORM_VEC3);

    Vector2 bounds = {ice->config.mapWidth > 0.0f ? ice->config.mapWidth : 300.0f,
                      ice->config.mapDepth > 0.0f ? ice->config.mapDepth : 225.0f};
    if (s_iceLocBounds >= 0)
        SetShaderValue(ice->shader, s_iceLocBounds, &bounds, SHADER_UNIFORM_VEC2);
    int flowEnabled = (ice->flowTex.id != 0) ? 1 : 0;
    if (s_iceLocFlowEnabled >= 0)
        SetShaderValue(ice->shader, s_iceLocFlowEnabled, &flowEnabled, SHADER_UNIFORM_INT);

    if (s_iceLocPlateTex >= 0 && ice->plateTex.id != 0)
    {
        int slot = 1;
        rlActiveTextureSlot(slot);
        rlEnableTexture(ice->plateTex.id);
        SetShaderValue(ice->shader, s_iceLocPlateTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }
    if (s_iceLocFlowTex >= 0 && ice->flowTex.id != 0)
    {
        int slot = 2;
        rlActiveTextureSlot(slot);
        rlEnableTexture(ice->flowTex.id);
        SetShaderValue(ice->shader, s_iceLocFlowTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }

    // Offset +0.014m above liquid water surface to eliminate Z-fighting with MapWaterSurface
    DrawModel(ice->model, (Vector3){0.0f, 0.014f, 0.0f}, 1.0f, WHITE);
    rlDrawRenderBatchActive();

    if (s_iceLocFlowTex >= 0 && ice->flowTex.id != 0)
    {
        rlActiveTextureSlot(2);
        rlDisableTexture();
    }
    if (s_iceLocPlateTex >= 0 && ice->plateTex.id != 0)
    {
        rlActiveTextureSlot(1);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    EndShaderMode();
}

void MapProp_UnloadIceSurface(MapIceSurface *ice)
{
    if (!ice || !ice->ready)
        return;
    if (ice->model.materialCount > 0)
        ice->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){0};
    UnloadModel(ice->model);
    ice->noiseTex = (Texture2D){0};
    ice->plateTex = (Texture2D){0};
    ice->flowTex = (Texture2D){0};
    ice->ready = false;
}

#endif // MAP_TOOLKIT_MAP_PROPS_ICE_INL

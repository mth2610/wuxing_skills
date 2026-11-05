// Sea-of-clouds plane (maps/toolkit/shaders/cloud_sea.fs) — drawn far below
// the playable ground of a floating plain suspended above clouds.
// #include'd once from map_props.c — not a standalone translation unit.

static int locCloudOffset = -1;
static int locCloudBank = -1, locCloudView = -1, locCloudSky = -1, locCloudHaze = -1;
static Shader cloudShader = {0};
static unsigned int s_cloudShaderId = 0;
static int locCloudLightDir = -1, locCloudLightCol = -1, locCloudAmbCol = -1, locCloudTime = -1, locCloudTiling = -1;

MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize)
{
    MapCloudSea cloud = {0};
    if (width <= 0.0f || depth <= 0.0f)
        return cloud;

    // Flat 1x1 plane is enough — the shader does all the work per-fragment,
    // no need for extra mesh subdivision.
    Mesh mesh = GenMeshPlane(width, depth, 1, 1);
    cloud.model = LoadModelFromMesh(mesh);

    cloudShader = ResourceManager_LoadShader("maps/toolkit/shaders/cloud_sea.vs",
                                             "maps/toolkit/shaders/cloud_sea.fs");
    if (cloudShader.id != 0 && s_cloudShaderId != cloudShader.id)
    {
        locCloudOffset = GetShaderLocation(cloudShader, "u_cloudOffset");
        if (cloudShader.locs != NULL)
            cloudShader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(cloudShader, "vertexNormal");
        locCloudLightDir = GetShaderLocation(cloudShader, "lightDir");
        locCloudLightCol = GetShaderLocation(cloudShader, "lightColor");
        locCloudAmbCol = GetShaderLocation(cloudShader, "ambientColor");
        locCloudTime = GetShaderLocation(cloudShader, "u_time");
        locCloudTiling = GetShaderLocation(cloudShader, "tiling");
        locCloudBank = GetShaderLocation(cloudShader, "u_cloudBankEnabled");
        locCloudView = GetShaderLocation(cloudShader, "u_viewPos");
        locCloudSky = GetShaderLocation(cloudShader, "u_skyAmbient");
        locCloudHaze = GetShaderLocation(cloudShader, "u_hazeColor");
        s_cloudShaderId = cloudShader.id;
    }

    cloud.model.materials[0].shader = cloudShader;

    Texture2D noiseTex = ResourceManager_LoadTextureVariant(
        "assets/textures/cloud_noise.png",
        true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    if (!noiseTex.id)
    {
        noiseTex = ResourceManager_LoadTexture("assets/textures/cloud_noise.png");
        SetTextureWrap(noiseTex, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(noiseTex, TEXTURE_FILTER_BILINEAR);
    }
    TraceLog(LOG_INFO, "CloudSea: noise texture %u, mip levels %d", noiseTex.id, noiseTex.mipmaps);
    cloud.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = noiseTex;

    float invTile = (tileSize > 1e-6f) ? (1.0f / tileSize) : 1.0f;
    cloud.tiling = (Vector2){width * invTile, depth * invTile};
    cloud.size = (Vector2){width, depth};

    BeginShaderMode(cloudShader);
    if (locCloudTiling >= 0)
        SetShaderValue(cloudShader, locCloudTiling, &cloud.tiling, SHADER_UNIFORM_VEC2);
    EndShaderMode();

    cloud.mistTexture = ResourceManager_LoadTextureVariant(
        "maps/toolkit/textures/island_mist_strip.png",
        true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    cloud.ready = true;
    return cloud;
}

void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset)
{
    if (!cloud || !cloud->ready || cloudShader.id == 0)
        return;

    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunScale = Environment_GetSunIntensity() * (1.0f / 255.0f);
    float sunColArr[4] = {
        (float)sunCol.r * sunScale,
        (float)sunCol.g * sunScale,
        (float)sunCol.b * sunScale,
        (float)sunCol.a * (1.0f / 255.0f)};
    float ambColArr[4] = {
        (float)ambCol.r * (1.0f / 255.0f),
        (float)ambCol.g * (1.0f / 255.0f),
        (float)ambCol.b * (1.0f / 255.0f),
        (float)ambCol.a * (1.0f / 255.0f)};

    // Chu kỳ 1000.0s là bội số chung chính xác của các vận tốc trôi UV (0.02, 0.008, -0.015, 0.01)
    float t = TimeFX_IsDeterministic() ? TimeFX_Elapsed() : (float)fmod(GetTime(), 1000.0);

    BeginShaderMode(cloudShader); // Opaque sea surface: native depth covers submerged cliffs.
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    if (locCloudOffset >= 0)
        SetShaderValue(cloudShader, locCloudOffset, &pos, SHADER_UNIFORM_VEC3);
    if (locCloudTiling >= 0)
        SetShaderValue(cloudShader, locCloudTiling, &cloud->tiling, SHADER_UNIFORM_VEC2);
    if (locCloudLightDir >= 0)
        SetShaderValue(cloudShader, locCloudLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    if (locCloudLightCol >= 0)
        SetShaderValue(cloudShader, locCloudLightCol, sunColArr, SHADER_UNIFORM_VEC4);
    if (locCloudAmbCol >= 0)
        SetShaderValue(cloudShader, locCloudAmbCol, ambColArr, SHADER_UNIFORM_VEC4);
    if (locCloudTime >= 0)
        SetShaderValue(cloudShader, locCloudTime, &t, SHADER_UNIFORM_FLOAT);

    int bankEnabled = (cloud->mistReady || cloud->boundary.rect.z > 0.0f) ? 1 : 0;
    if (locCloudBank >= 0)
        SetShaderValue(cloudShader, locCloudBank, &bankEnabled, SHADER_UNIFORM_INT);

    EnvFrameLighting frame = Environment_GetFrameLighting();
    Vector4 sky = ColorNormalize(frame.skyAmbient);
    Vector4 haze = ColorNormalize(frame.atmosphere.color);
    Vector3 skyRgb = {sky.x, sky.y, sky.z};
    Vector3 hazeRgb = {haze.x, haze.y, haze.z};
    if (locCloudView >= 0)
        SetShaderValue(cloudShader, locCloudView, &camera.position, SHADER_UNIFORM_VEC3);
    if (locCloudSky >= 0)
        SetShaderValue(cloudShader, locCloudSky, &skyRgb, SHADER_UNIFORM_VEC3);
    if (locCloudHaze >= 0)
        SetShaderValue(cloudShader, locCloudHaze, &hazeRgb, SHADER_UNIFORM_VEC3);

    DrawModel(cloud->model, pos, 1.0f, WHITE);
    rlDrawRenderBatchActive();
    EndShaderMode();
}

void MapProp_DrawIslandMist(const MapCloudSea *cloud, Vector3 worldCenter)
{
    if (!cloud || !cloud->ready || !cloud->mistReady)
        return;
    Color sun = Environment_GetSunColor(), ambient = Environment_GetAmbientColor();
    Color tint = {
        (unsigned char)fminf(255.0f, (float)ambient.r + (float)sun.r * 0.55f),
        (unsigned char)fminf(255.0f, (float)ambient.g + (float)sun.g * 0.55f),
        (unsigned char)fminf(255.0f, (float)ambient.b + (float)sun.b * 0.55f),
        255};
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling(); // Concave and hole contours may face either direction.
    DrawModel(cloud->mistModel, worldCenter, 1.0f, tint);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void MapProp_UnloadCloudSea(MapCloudSea *cloud)
{
    if (!cloud || !cloud->ready)
        return;
    if (cloud->mistReady)
        UnloadModel(cloud->mistModel);
    cloud->mistModel = (Model){0};
    cloud->mistReady = false;
    UnloadModel(cloud->model);
    cloud->ready = false;
}
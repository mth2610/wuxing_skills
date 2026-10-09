// Sea-of-clouds plane (maps/toolkit/shaders/cloud_sea.fs) — drawn far below
// the playable ground of a floating plain suspended above clouds.
// #include'd once from map_props.c — not a standalone translation unit.

static int locCloudOffset = -1;
static int locCloudBank = -1, locCloudView = -1, locCloudSky = -1, locCloudHaze = -1;
static Shader cloudShader = {0};
static bool cloudShaderLoaded = false;
static int locCloudLightDir = -1, locCloudLightCol = -1, locCloudAmbCol = -1, locCloudTime = -1, locCloudTiling = -1;

MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize)
{
    MapCloudSea cloud = {0};

    // Flat 1x1 plane is enough — the shader does all the work per-fragment,
    // no need for extra mesh subdivision.
    Mesh mesh = GenMeshPlane(width, depth, 1, 1);
    cloud.model = LoadModelFromMesh(mesh);

    if (!cloudShaderLoaded)
    {
        cloudShader = ResourceManager_LoadShader("maps/toolkit/shaders/cloud_sea.vs", "maps/toolkit/shaders/cloud_sea.fs");
        locCloudOffset = GetShaderLocation(cloudShader, "u_cloudOffset");
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
        cloudShaderLoaded = true;
    }

    cloud.model.materials[0].shader = cloudShader;

    // Tileable noise texture (scripts/generate_cloud_noise.py) — replaces
    // per-pixel sin()-based FBM math in the shader, much cheaper for a plane
    // that often covers most of the screen.
    Texture2D noiseTex = {0};
    noiseTex = ResourceManager_LoadTextureVariant("assets/textures/cloud_noise.png",
            true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    if (!noiseTex.id) {
        // Preserve the existing path if the optional variant cannot be cached.
        noiseTex = ResourceManager_LoadTexture("assets/textures/cloud_noise.png");
        SetTextureWrap(noiseTex, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(noiseTex, TEXTURE_FILTER_BILINEAR);
    }
    TraceLog(LOG_INFO, "CloudSea: noise texture %u, mip levels %d", noiseTex.id, noiseTex.mipmaps);
    cloud.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = noiseTex;

    cloud.tiling = (Vector2){width / tileSize, depth / tileSize};
    cloud.size = (Vector2){width, depth};

    cloud.mistTexture = ResourceManager_LoadTextureVariant("maps/toolkit/textures/island_mist_strip.png",
        true, TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_REPEAT);
    cloud.ready = true;
    return cloud;
}

void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset)
{
    if (!cloud->ready)
        return;

    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunColArr[4] = {sunCol.r / 255.0f, sunCol.g / 255.0f, sunCol.b / 255.0f, sunCol.a / 255.0f};
    float ambColArr[4] = {ambCol.r / 255.0f, ambCol.g / 255.0f, ambCol.b / 255.0f, ambCol.a / 255.0f};
    float sunIntensity = Environment_GetSunIntensity();
    for (int channel = 0; channel < 3; channel++) sunColArr[channel] *= sunIntensity;
    float t = TimeFX_IsDeterministic() ? TimeFX_Elapsed() : (float)GetTime();

    BeginShaderMode(cloudShader); // Opaque sea surface: native depth covers submerged cliffs.
    Vector3 offset = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    SetShaderValue(cloudShader, locCloudOffset, &offset, SHADER_UNIFORM_VEC3);
    SetShaderValue(cloudShader, locCloudTiling, &cloud->tiling, SHADER_UNIFORM_VEC2);
    SetShaderValue(cloudShader, locCloudLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    SetShaderValue(cloudShader, locCloudLightCol, sunColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(cloudShader, locCloudAmbCol, ambColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(cloudShader, locCloudTime, &t, SHADER_UNIFORM_FLOAT);
    int bankEnabled = cloud->mistReady || cloud->boundary.rect.z > 0.0f;
    SetShaderValue(cloudShader, locCloudBank, &bankEnabled, SHADER_UNIFORM_INT);
    EnvFrameLighting frame = Environment_GetFrameLighting();
    Vector4 sky = ColorNormalize(frame.skyAmbient), haze = ColorNormalize(frame.atmosphere.color);
    SetShaderValue(cloudShader, locCloudView, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(cloudShader, locCloudSky, &sky, SHADER_UNIFORM_VEC3);
    SetShaderValue(cloudShader, locCloudHaze, &haze, SHADER_UNIFORM_VEC3);

    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModel(cloud->model, pos, 1.0f, WHITE);
    rlDrawRenderBatchActive();
    EndShaderMode();
}

void MapProp_DrawIslandMist(const MapCloudSea *cloud, Vector3 worldCenter)
{
    if (!cloud || !cloud->ready || !cloud->mistReady) return;
    Color sun = Environment_GetSunColor(), ambient = Environment_GetAmbientColor();
    Color tint = {
        (unsigned char)fminf(255, ambient.r + sun.r * 0.55f),
        (unsigned char)fminf(255, ambient.g + sun.g * 0.55f),
        (unsigned char)fminf(255, ambient.b + sun.b * 0.55f), 255};
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
    if (!cloud->ready)
        return;
    if (cloud->mistReady) UnloadModel(cloud->mistModel);
    cloud->mistModel = (Model){0}; cloud->mistReady = false;
    UnloadModel(cloud->model);
    cloud->ready = false;
}

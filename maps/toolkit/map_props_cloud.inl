// Sea-of-clouds plane (maps/toolkit/shaders/cloud_sea.fs) — drawn far below
// the playable ground for the "floating island ringed by mountains" motif.
// #include'd once from map_props.c — not a standalone translation unit.

static Shader cloudShader = {0};
static bool cloudShaderLoaded = false;
static int locCloudLightDir = -1, locCloudLightCol = -1, locCloudAmbCol = -1, locCloudTime = -1;

MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize)
{
    MapCloudSea cloud = {0};

    // Flat 1x1 plane is enough — the shader does all the work per-fragment,
    // no need for extra mesh subdivision.
    Mesh mesh = GenMeshPlane(width, depth, 1, 1);
    cloud.model = LoadModelFromMesh(mesh);

    if (!cloudShaderLoaded)
    {
        cloudShader = LoadShader(0, "maps/toolkit/shaders/cloud_sea.fs");
        locCloudLightDir = GetShaderLocation(cloudShader, "lightDir");
        locCloudLightCol = GetShaderLocation(cloudShader, "lightColor");
        locCloudAmbCol = GetShaderLocation(cloudShader, "ambientColor");
        locCloudTime = GetShaderLocation(cloudShader, "u_time");
        cloudShaderLoaded = true;
    }

    cloud.model.materials[0].shader = cloudShader;

    float tiling[2] = {width / tileSize, depth / tileSize};
    int tilingLoc = GetShaderLocation(cloudShader, "tiling");
    SetShaderValue(cloudShader, tilingLoc, tiling, SHADER_UNIFORM_VEC2);

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
    float t = (float)GetTime();

    SetShaderValue(cloudShader, locCloudLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    SetShaderValue(cloudShader, locCloudLightCol, sunColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(cloudShader, locCloudAmbCol, ambColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(cloudShader, locCloudTime, &t, SHADER_UNIFORM_FLOAT);

    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModel(cloud->model, pos, 1.0f, WHITE);
}

void MapProp_UnloadCloudSea(MapCloudSea *cloud)
{
    if (!cloud->ready)
        return;
    UnloadModel(cloud->model);
    cloud->ready = false;
}

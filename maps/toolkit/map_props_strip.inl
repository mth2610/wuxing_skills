// Flat strip (stone path, road, bridge deck, ...) — noise-feathered edge blend
// (maps/toolkit/shaders/path_blend.fs). #include'd once from map_props.c —
// not a standalone translation unit.

static Shader pathShader = {0};
static unsigned int s_pathShaderId = 0;
static int locPathLightDir = -1, locPathLightCol = -1, locPathAmbCol = -1;
static int locPathViewPos = -1, locPathTiling = -1, locPathSurfaceMaps = -1;

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapStripSurface strip = {0};
    if (length <= 0.0f || width <= 0.0f)
        return strip;

    // 1. Tạo Mesh (KHÔNG GỌI TilePlaneUVs để giữ hệ trục tọa độ viền)
    Mesh mesh = GenMeshPlane(length, width, 16, 4);
    strip.model = LoadModelFromMesh(mesh);

    // 2. Load Shader Con đường (hỗ trợ tự động re-bind nếu shader được nạp lại)
    pathShader = ResourceManager_LoadShader("maps/toolkit/shaders/path_blend.vs",
                                            "maps/toolkit/shaders/path_blend.fs");
    if (pathShader.id != 0 && s_pathShaderId != pathShader.id)
    {
        pathShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(pathShader, "vertexPosition");
        pathShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(pathShader, "vertexTexCoord");
        pathShader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(pathShader, "vertexNormal");
        pathShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(pathShader, "mvp");
        pathShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(pathShader, "matModel");
        pathShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(pathShader, "colDiffuse");
        pathShader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(pathShader, "texture0");
        pathShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(pathShader, "texture2");
        pathShader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(pathShader, "texture3");
        MapShadow_ConfigureShader(pathShader);

        VFXLight_RegisterShader(pathShader);
        locPathLightDir = GetShaderLocation(pathShader, "lightDir");
        locPathLightCol = GetShaderLocation(pathShader, "lightColor");
        locPathAmbCol = GetShaderLocation(pathShader, "ambientColor");
        locPathViewPos = GetShaderLocation(pathShader, "viewPos");
        locPathTiling = GetShaderLocation(pathShader, "tiling");
        locPathSurfaceMaps = GetShaderLocation(pathShader, "u_hasSurfaceMaps");
        s_pathShaderId = pathShader.id;
    }

    // 3. Load Texture
    Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

    strip.model.materials[0].shader = pathShader;
    strip.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;

    strip.useSurfaceMaps = (normalPath != NULL && roughnessPath != NULL);
    if (strip.useSurfaceMaps)
    {
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        strip.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = normal;
        strip.model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = roughness;
    }
    MapShadow_AttachMaterial(&strip.model.materials[0]);

    // 4. Truyền Tiling xuống shader để lặp texture (chống chia cho 0)
    float invTile = (tileSize > 1e-6f) ? (1.0f / tileSize) : 1.0f;
    strip.tiling = (Vector2){length * invTile, width * invTile};

    strip.ready = true;
    return strip;
}

static void MapProp_UpdateStripLighting(const MapStripSurface *strip)
{
    Shader shader = strip->model.materials[0].shader;
    if (shader.id == 0)
        return;

    MapShadow_AttachMaterial(&strip->model.materials[0]);

    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    const float inv255 = 1.0f / 255.0f;
    const float sunScale = Environment_GetSunIntensity() * inv255;
    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunColArr[4] = {
        (float)sunCol.r * sunScale,
        (float)sunCol.g * sunScale,
        (float)sunCol.b * sunScale,
        (float)sunCol.a * inv255};
    float ambColArr[4] = {
        (float)ambCol.r * inv255,
        (float)ambCol.g * inv255,
        (float)ambCol.b * inv255,
        (float)ambCol.a * inv255};
    int hasSurfaceMaps = strip->useSurfaceMaps ? 1 : 0;

    BeginShaderMode(shader);
    if (locPathLightDir >= 0)
        SetShaderValue(shader, locPathLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    if (locPathLightCol >= 0)
        SetShaderValue(shader, locPathLightCol, sunColArr, SHADER_UNIFORM_VEC4);
    if (locPathAmbCol >= 0)
        SetShaderValue(shader, locPathAmbCol, ambColArr, SHADER_UNIFORM_VEC4);
    if (locPathViewPos >= 0)
        SetShaderValue(shader, locPathViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (locPathTiling >= 0)
        SetShaderValue(shader, locPathTiling, &strip->tiling, SHADER_UNIFORM_VEC2);
    if (locPathSurfaceMaps >= 0)
        SetShaderValue(shader, locPathSurfaceMaps, &hasSurfaceMaps, SHADER_UNIFORM_INT);
    MapShadow_UpdateShader(shader);
    EndShaderMode();
}

void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset)
{
    if (!strip || !strip->ready)
        return;

    MapProp_UpdateStripLighting(strip);
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModel(strip->model, pos, 1.0f, WHITE);
}

void MapProp_DrawStripEx(const MapStripSurface *strip, Vector3 worldCenter, float yOffset,
                         float rotationDeg, Vector3 scale)
{
    if (!strip || !strip->ready)
        return;

    MapProp_UpdateStripLighting(strip);
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModelEx(strip->model, pos, (Vector3){0.0f, 1.0f, 0.0f}, rotationDeg, scale, WHITE);
}

void MapProp_UnloadStrip(MapStripSurface *strip)
{
    if (!strip || !strip->ready)
        return;
    for (int i = 0; i < strip->model.materialCount; i++)
    {
        strip->model.materials[i].maps[MATERIAL_MAP_OCCLUSION].texture = (Texture2D){0};
        strip->model.materials[i].maps[MATERIAL_MAP_HEIGHT].texture = (Texture2D){0};
    }
    UnloadModel(strip->model);
    strip->ready = false;
}
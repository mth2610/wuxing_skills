// Flat strip (stone path, road, bridge deck, ...) — noise-feathered edge blend
// (maps/toolkit/shaders/path_blend.fs). #include'd once from map_props.c —
// not a standalone translation unit.

static Shader pathShader = {0};
static bool pathShaderLoaded = false;
static int locPathLightDir = -1, locPathLightCol = -1, locPathAmbCol = -1;
static int locPathViewPos = -1, locPathTiling = -1, locPathSurfaceMaps = -1;

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapStripSurface strip = {0};
    // 1. Tạo Mesh (KHÔNG GỌI TilePlaneUVs để giữ hệ trục tọa độ viền)
    Mesh mesh = GenMeshPlane(length, width, 16, 4); // Tăng chia lưới để bóp méo viền đẹp hơn
    strip.model = LoadModelFromMesh(mesh);

    // 2. Load Shader Con đường
    if (!pathShaderLoaded)
    {
        // ResourceManager_LoadShader — only it runs the preprocessor that resolves
        // the #include this shader now carries (core/docs/LANDMINES.md).
        pathShader = ResourceManager_LoadShader("maps/toolkit/shaders/path_blend.vs",
                                                "maps/toolkit/shaders/path_blend.fs");
        // Required for path_blend.vs's world-space fragPosition — see the long
        // note in map_props_ground.inl. Without it the strip thinks it is at
        // the origin and receives no point light.
        pathShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(pathShader, "vertexPosition");
        pathShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(pathShader, "vertexTexCoord");
        pathShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(pathShader, "mvp");
        pathShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(pathShader, "matModel");
        pathShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(pathShader, "colDiffuse");
        pathShader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(pathShader, "texture0");
        pathShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(pathShader, "texture2");
        pathShader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(pathShader, "texture3");

        VFXLight_RegisterShader(pathShader);   // main.c binds it each frame
        locPathLightDir = GetShaderLocation(pathShader, "lightDir");
        locPathLightCol = GetShaderLocation(pathShader, "lightColor");
        locPathAmbCol = GetShaderLocation(pathShader, "ambientColor");
        locPathViewPos = GetShaderLocation(pathShader, "viewPos");
        locPathTiling = GetShaderLocation(pathShader, "tiling");
        locPathSurfaceMaps = GetShaderLocation(pathShader, "u_hasSurfaceMaps");
        pathShaderLoaded = true;
    }

    // 3. Load Texture
    Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);
    GenTextureMipmaps(&diffuse);
    SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(diffuse, TEXTURE_WRAP_REPEAT);

    // Tạm thời bỏ qua normal/roughness đối với đường mờ lề để tối ưu hiệu năng
    strip.model.materials[0].shader = pathShader;
    strip.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;

    strip.useSurfaceMaps = normalPath && roughnessPath;
    if (strip.useSurfaceMaps)
    {
        Texture2D normal = ResourceManager_LoadTexture(normalPath);
        Texture2D roughness = ResourceManager_LoadTexture(roughnessPath);
        GenTextureMipmaps(&normal);
        GenTextureMipmaps(&roughness);
        SetTextureFilter(normal, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(roughness, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(normal, TEXTURE_WRAP_REPEAT);
        SetTextureWrap(roughness, TEXTURE_WRAP_REPEAT);
        strip.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = normal;
        strip.model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = roughness;
    }

    // 4. Truyền Tiling xuống shader để lặp texture
    strip.tiling = (Vector2){length / tileSize, width / tileSize};

    strip.ready = true;
    return strip;
}

static void MapProp_UpdateStripLighting(const MapStripSurface *strip)
{
    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunColArr[4] = {sunCol.r / 255.0f, sunCol.g / 255.0f, sunCol.b / 255.0f, sunCol.a / 255.0f};
    float ambColArr[4] = {ambCol.r / 255.0f, ambCol.g / 255.0f, ambCol.b / 255.0f, ambCol.a / 255.0f};
    int hasSurfaceMaps = strip->useSurfaceMaps ? 1 : 0;

    SetShaderValue(strip->model.materials[0].shader, locPathLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    SetShaderValue(strip->model.materials[0].shader, locPathLightCol, sunColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(strip->model.materials[0].shader, locPathAmbCol, ambColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(strip->model.materials[0].shader, locPathViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(strip->model.materials[0].shader, locPathTiling, &strip->tiling, SHADER_UNIFORM_VEC2);
    SetShaderValue(strip->model.materials[0].shader, locPathSurfaceMaps, &hasSurfaceMaps, SHADER_UNIFORM_INT);
}

void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset)
{
    if (!strip->ready)
        return;

    MapProp_UpdateStripLighting(strip);
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModel(strip->model, pos, 1.0f, WHITE);
}

void MapProp_DrawStripEx(const MapStripSurface *strip, Vector3 worldCenter, float yOffset,
                         float rotationDeg, Vector3 scale)
{
    if (!strip->ready)
        return;

    MapProp_UpdateStripLighting(strip);
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModelEx(strip->model, pos, (Vector3){0.0f, 1.0f, 0.0f}, rotationDeg, scale, WHITE);
}

void MapProp_UnloadStrip(MapStripSurface *strip)
{
    if (!strip->ready)
        return;
    UnloadModel(strip->model);
    strip->ready = false;
}

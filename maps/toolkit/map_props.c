#include "maps/toolkit/map_props.h"
#include "core/resource_manager.h"
#include "maps/toolkit/prop_lit.h"
#include "environment/environment_system.h"
#include "rlgl.h"

// Biến toàn cục (static) để lưu trữ Shader và vị trí các biến (Uniform Locations)
// Giúp tối ưu hiệu năng, tránh việc phải tìm location mỗi frame.
static Shader groundShader = {0};
static bool shaderLoaded = false;
static int locLightDir = -1;
static int locLightColor = -1;
static int locAmbientColor = -1;

// raylib's GenMeshPlane UVs span a flat 0..1 across the whole plane; scale
// them so the texture repeats every `tileSize` meters instead of stretching
// once across the mesh.
static void TilePlaneUVs(Mesh *mesh, float worldWidth, float worldLength, float tileSize)
{
    float repeatU = worldWidth / tileSize;
    float repeatV = worldLength / tileSize;
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        mesh->texcoords[i * 2 + 0] *= repeatU;
        mesh->texcoords[i * 2 + 1] *= repeatV;
    }
}

// --- Ground plane --------------------------------------------------------

MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize,
                                      const char *splatMapPath,
                                      const char *grassTexPath,
                                      const char *pathTexPath)
{
    MapGroundSurface ground = {0};

    // Tạo Mesh (Giữ nguyên UV 0..1 để Splatmap bao phủ toàn map)
    Mesh mesh = GenMeshPlane(width, depth, 20, 20);
    ground.model = LoadModelFromMesh(mesh);

    // 1. Load Textures
    Texture2D texSplat = ResourceManager_LoadTexture(splatMapPath);
    Texture2D texGrass = ResourceManager_LoadTexture(grassTexPath);
    Texture2D texPath = ResourceManager_LoadTexture(pathTexPath);

    // Tối ưu Texture Lặp (Grass, Path)
    GenTextureMipmaps(&texGrass);
    GenTextureMipmaps(&texPath);
    SetTextureFilter(texGrass, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureFilter(texPath, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(texGrass, TEXTURE_WRAP_REPEAT);
    SetTextureWrap(texPath, TEXTURE_WRAP_REPEAT);

    // Tối ưu Splatmap (Không lặp)
    SetTextureFilter(texSplat, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(texSplat, TEXTURE_WRAP_REPEAT);

    // 2. Khởi tạo Shader một lần duy nhất
    if (!shaderLoaded)
    {
        groundShader = LoadShader(0, "maps/toolkit/shaders/ground_splat.fs");

        // Cache lại các vị trí uniform ánh sáng để dùng trong hàm Draw
        locLightDir = GetShaderLocation(groundShader, "lightDir");
        locLightColor = GetShaderLocation(groundShader, "lightColor");
        locAmbientColor = GetShaderLocation(groundShader, "ambientColor");

        shaderLoaded = true;
    }

    // 3. Đăng ký Shader vào vật liệu
    ground.model.materials[0].shader = groundShader;

    // Gán Texture vào các khe chứa (slots) của Material để Raylib tự động bind lên GPU
    ground.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texSplat;  // Slot 0
    ground.model.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = texGrass; // Slot 1
    ground.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = texPath;    // Slot 2

    // 4. Khai báo khe cắm (slot) cho Shader
    int grassTexSlot = 1;
    int pathTexSlot = 2;
    int grassLoc = GetShaderLocation(groundShader, "texGrass");
    int pathLoc = GetShaderLocation(groundShader, "texPath");
    SetShaderValue(groundShader, grassLoc, &grassTexSlot, SHADER_UNIFORM_INT);
    SetShaderValue(groundShader, pathLoc, &pathTexSlot, SHADER_UNIFORM_INT);

    // 5. Truyền thông số độ lặp (Tiling)
    float tiling[2] = {width / tileSize, depth / tileSize};
    int tilingLoc = GetShaderLocation(groundShader, "tiling");
    SetShaderValue(groundShader, tilingLoc, tiling, SHADER_UNIFORM_VEC2);

    ground.ready = true;
    return ground;
}

void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter)
{
    if (!ground->ready)
        return;

    // --- CẬP NHẬT ÁNH SÁNG THEO THỜI GIAN THỰC ---
    // Lấy thông số từ API Môi trường
    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    // Chuyển đổi Color (0-255) sang dạng mảng float (0.0 - 1.0) cho GLSL vec4
    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunColArr[4] = {sunCol.r / 255.0f, sunCol.g / 255.0f, sunCol.b / 255.0f, sunCol.a / 255.0f};
    float ambColArr[4] = {ambCol.r / 255.0f, ambCol.g / 255.0f, ambCol.b / 255.0f, ambCol.a / 255.0f};

    // Đẩy dữ liệu ánh sáng mới nhất xuống Shader
    SetShaderValue(groundShader, locLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    SetShaderValue(groundShader, locLightColor, sunColArr, SHADER_UNIFORM_VEC4);
    SetShaderValue(groundShader, locAmbientColor, ambColArr, SHADER_UNIFORM_VEC4);

    // Vẽ mặt đất
    DrawModel(ground->model, worldCenter, 1.0f, WHITE);
}

void MapProp_UnloadGround(MapGroundSurface *ground)
{
    if (!ground->ready)
        return;
    UnloadModel(ground->model);
    ground->ready = false;
}

// --- Flat strip ------------------------------------------------------------

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapStripSurface strip = {0};

    Mesh mesh = GenMeshPlane(length, width, 8, 2);
    TilePlaneUVs(&mesh, length, width, tileSize);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        strip.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);
        Texture2D normal = ResourceManager_LoadTexture(normalPath);
        Texture2D roughness = ResourceManager_LoadTexture(roughnessPath);

        GenTextureMipmaps(&diffuse);
        GenTextureMipmaps(&normal);
        GenTextureMipmaps(&roughness);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(normal, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(roughness, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(diffuse, TEXTURE_WRAP_REPEAT);
        SetTextureWrap(normal, TEXTURE_WRAP_REPEAT);
        SetTextureWrap(roughness, TEXTURE_WRAP_REPEAT);

        strip.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        strip.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);

        GenTextureMipmaps(&diffuse);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(diffuse, TEXTURE_WRAP_REPEAT);

        strip.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    strip.ready = true;
    return strip;
}

void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset)
{
    if (!strip->ready)
        return;
    Vector3 pos = {worldCenter.x, worldCenter.y + yOffset, worldCenter.z};
    DrawModel(strip->model, pos, 1.0f, WHITE);
}

void MapProp_UnloadStrip(MapStripSurface *strip)
{
    if (!strip->ready)
        return;
    UnloadModel(strip->model);
    strip->ready = false;
}

// --- Rock props --------------------------------------------------------

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};

    Mesh mesh = GenMeshSphere(1.0f, 6, 8); // low ring/slice count -> faceted low-poly look

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);
        Texture2D normal = ResourceManager_LoadTexture(normalPath);
        Texture2D roughness = ResourceManager_LoadTexture(roughnessPath);

        GenTextureMipmaps(&diffuse);
        GenTextureMipmaps(&normal);
        GenTextureMipmaps(&roughness);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(normal, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureFilter(roughness, TEXTURE_FILTER_ANISOTROPIC_16X);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = ResourceManager_LoadTexture(diffusePath);

        GenTextureMipmaps(&diffuse);
        SetTextureFilter(diffuse, TEXTURE_FILTER_ANISOTROPIC_16X);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count)
{
    if (!rocks->ready)
        return;

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];

        Vector3 pos = {p->position.x, -0.3f * p->heightScale, p->position.z};
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        // API Môi trường - Đổ bóng giả tự động tính góc sáng
        Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                    p->radiusScale * 2.0f, p->heightScale * 2.0f);

        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
}

void MapProp_UnloadRocks(MapRockSet *rocks)
{
    if (!rocks->ready)
        return;
    UnloadModel(rocks->model);
    rocks->ready = false;
}
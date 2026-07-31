// Ground plane — splatmap blend of grass/path textures (maps/toolkit/shaders/ground_splat.fs).
// #include'd once from map_props.c — not a standalone translation unit.

// Biến toàn cục (static) để lưu trữ Shader và vị trí các biến (Uniform Locations)
// Giúp tối ưu hiệu năng, tránh việc phải tìm location mỗi frame.
static Shader groundShader = {0};
static bool shaderLoaded = false;
static int locLightDir = -1;
static int locLightColor = -1;
static int locAmbientColor = -1;

// Shared by MapProp_CreateGround (flat) and MapProp_CreateGroundHeightmap
// (sloped island) — both just build a different Mesh, then hand it here for
// the splatmap texture/shader setup so that part isn't duplicated.
static MapGroundSurface SetupGroundMaterial(Mesh mesh, float width, float depth, float tileSize,
                                            const char *splatMapPath,
                                            const char *grassTexPath,
                                            const char *pathTexPath)
{
    MapGroundSurface ground = {0};
    ground.model = LoadModelFromMesh(mesh);

    // 1. Load Textures
    Texture2D texSplat = ResourceManager_LoadTexture(splatMapPath);
    Texture2D texGrass = ResourceManager_LoadTexture(grassTexPath);
    Texture2D texPath = ResourceManager_LoadTexture(pathTexPath);

    // Splatmap filter FIRST. When splatMapPath == grassTexPath (a common
    // placeholder setup), ResourceManager returns the SAME GPU texture for both
    // texSplat and texGrass — so this bilinear (no-mipmap) filter would clobber
    // the anisotropic+mipmap filter below if set last, leaving the tiled grass
    // sampled without mipmaps → severe minification aliasing (the "speckle").
    // Set the grass/path aniso+mipmap filters LAST so they win on a shared tex.
    SetTextureFilter(texSplat, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(texSplat, TEXTURE_WRAP_REPEAT);

    // Tối ưu Texture Lặp (Grass, Path) — mipmaps + anisotropic để khử aliasing
    // khi tile dày và nhìn xa/nghiêng.
    GenTextureMipmaps(&texGrass);
    GenTextureMipmaps(&texPath);
    SetTextureFilter(texGrass, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureFilter(texPath, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(texGrass, TEXTURE_WRAP_REPEAT);
    SetTextureWrap(texPath, TEXTURE_WRAP_REPEAT);

    // 2. Khởi tạo Shader một lần duy nhất
    if (!shaderLoaded)
    {
        // ResourceManager_LoadShader, not raylib's LoadShader: only the former
        // runs core/shader_preprocessor.c, which is what resolves the
        // `#include "core/shaders/common/vfx_lights.glsl"` these shaders now
        // carry. Raw LoadShader hands the #include line straight to GLSL, which
        // has no such directive, and the shader fails to compile.
        // (It also caches by path — never UnloadShader the result.)
        groundShader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_splat.vs",
                                                  "maps/toolkit/shaders/ground_splat.fs");

        // matModel is NOT auto-uploaded to a custom shader unless this loc is
        // set — raylib only pushes it when shader.locs[SHADER_LOC_MATRIX_MODEL]
        // is valid. Left unset, the uniform is never written, GLSL reads it as
        // the ZERO matrix, and ground_splat.vs's
        //     fragPosition = matModel * vertexPosition
        // collapses to (0,0,0) for every fragment: the whole ground believes it
        // is at the world origin. Sun/ambient still look right (they need no
        // position), so nothing appears wrong until something POSITIONAL — like
        // a point light — silently receives nothing. See ENGINE_LANDMINES.md.
        groundShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(groundShader, "matModel");

        // Cache lại các vị trí uniform ánh sáng để dùng trong hàm Draw
        locLightDir = GetShaderLocation(groundShader, "lightDir");
        locLightColor = GetShaderLocation(groundShader, "lightColor");
        locAmbientColor = GetShaderLocation(groundShader, "ambientColor");
        VFXLight_RegisterShader(groundShader);   // main.c binds it each frame

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

MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize,
                                      const char *splatMapPath,
                                      const char *grassTexPath,
                                      const char *pathTexPath)
{
    // Tạo Mesh (Giữ nguyên UV 0..1 để Splatmap bao phủ toàn map)
    Mesh mesh = GenMeshPlane(width, depth, 20, 20);
    return SetupGroundMaterial(mesh, width, depth, tileSize, splatMapPath, grassTexPath, pathTexPath);
}

MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                               float cliffDepth, float tileSize,
                                               const char *splatMapPath,
                                               const char *grassTexPath,
                                               const char *pathTexPath)
{
    // Heightmap: WHITE = plateau (top, walkable), BLACK = cliff edge (bottom).
    // GenMeshHeightmap spans local X/Z [0, width]/[0, depth] (NOT centered
    // like GenMeshPlane) and Y [0, cliffDepth] (black=0, white=cliffDepth).
    // Deliberately does NOT mutate mesh.vertices to re-center it (that was
    // tried first and produced a mangled mesh — only ~1/4 of it actually
    // rendered, root cause not identified, not worth chasing further) —
    // instead the correction ships as drawOffset, applied at DrawModel time
    // in MapProp_DrawGround, so the mesh itself stays exactly as
    // GenMeshHeightmap produced it.
    Image heightmapImg = LoadImage(heightmapPath);
    Mesh mesh = GenMeshHeightmap(heightmapImg, (Vector3){width, cliffDepth, depth});
    UnloadImage(heightmapImg);

    MapGroundSurface result = SetupGroundMaterial(mesh, width, depth, tileSize, splatMapPath, grassTexPath, pathTexPath);
    result.drawOffset = (Vector3){-width * 0.5f, -cliffDepth, -depth * 0.5f};
    return result;
}

float MapProp_SampleGroundHeight(const MapGroundSurface *ground, Vector3 worldCenter, float x, float z)
{
    if (!ground->ready || ground->model.meshCount < 1)
        return worldCenter.y;

    // Raycast straight down through the ACTUAL rendered mesh — see the
    // header comment on why this replaced a hand-derived pixel formula.
    // Same transform MapProp_DrawGround passes to DrawModel.
    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };
    Matrix transform = MatrixTranslate(pos.x, pos.y, pos.z);

    Ray ray = { (Vector3){x, worldCenter.y + 1000.0f, z}, (Vector3){0.0f, -1.0f, 0.0f} };
    RayCollision hit = GetRayCollisionMesh(ray, ground->model.meshes[0], transform);
    if (hit.hit)
        return hit.point.y;

    return worldCenter.y; // (x,z) outside the mesh's footprint
}

bool MapProp_SampleGroundSurface(const MapGroundSurface *ground, Vector3 worldCenter,
                                 float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (outPosition) *outPosition = (Vector3){x, worldCenter.y, z};
    if (outNormal) *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
    if (!ground->ready || ground->model.meshCount < 1) return false;
    Vector3 pos = {worldCenter.x + ground->drawOffset.x, worldCenter.y + ground->drawOffset.y,
                   worldCenter.z + ground->drawOffset.z};
    Ray ray = {(Vector3){x, worldCenter.y + 1000.0f, z}, (Vector3){0.0f, -1.0f, 0.0f}};
    RayCollision hit = GetRayCollisionMesh(ray, ground->model.meshes[0],
                                            MatrixTranslate(pos.x, pos.y, pos.z));
    if (!hit.hit) return false;
    if (outPosition) *outPosition = hit.point;
    if (outNormal) *outNormal = Vector3Normalize(hit.normal);
    return true;
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



    // Vẽ mặt đất — drawOffset (0,0,0) for the flat plane, non-zero for the
    // heightmap variant (see MapProp_CreateGroundHeightmap).
    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };
    DrawModel(ground->model, pos, 1.0f, WHITE);
}

void MapProp_UnloadGround(MapGroundSurface *ground)
{
    if (!ground->ready)
        return;
    UnloadModel(ground->model);
    ground->ready = false;
}

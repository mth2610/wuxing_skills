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

// ── MapGroundLookup — an XZ bin grid over the ground mesh's triangles ────────
//
// WHAT THIS IS NOT. It is not a re-derivation of height from the heightmap
// image. That was tried once (see MapProp_SampleGroundHeight's header comment)
// and it buried ground-hugging effects metres underground, because the mapping
// from pixel to rendered vertex has several independent ways to be wrong and
// all of them look plausible. This indexes the SAME triangle array the raycast
// walks and interpolates within the SAME triangle the raycast would hit; the
// only thing it changes is how many triangles get tested. Verified against the
// raycast at load time under WUXING_GROUND_LOOKUP_VERIFY=1.
//
// WHY IT WAS NEEDED. GetRayCollisionMesh is O(every triangle). On VERDANT_PATH
// (7,938 triangles) that measured 232-292 us per sample against this grid's
// ~0.59 us — 400-500x — which is why every consumer of the query had been forced
// to ration itself: vc_ground_wave.inl down from 455 samples/frame to 48,
// vc_rune_circle.inl behind a round-robin cache. Fixing it here removes the
// reason for all of that.

static void GroundLookup_Free(MapGroundLookup *g)
{
    if (g->cellStart) { RL_FREE(g->cellStart); g->cellStart = NULL; }
    if (g->cellItems) { RL_FREE(g->cellItems); g->cellItems = NULL; }
    g->built = false;
}

static void GroundLookup_Tri(const MapGroundLookup *g, int t,
                             Vector3 *a, Vector3 *b, Vector3 *c)
{
    int i0, i1, i2;
    if (g->indices) {
        i0 = g->indices[t * 3 + 0];
        i1 = g->indices[t * 3 + 1];
        i2 = g->indices[t * 3 + 2];
    } else {
        i0 = t * 3 + 0; i1 = t * 3 + 1; i2 = t * 3 + 2;
    }
    *a = (Vector3){g->verts[i0 * 3], g->verts[i0 * 3 + 1], g->verts[i0 * 3 + 2]};
    *b = (Vector3){g->verts[i1 * 3], g->verts[i1 * 3 + 1], g->verts[i1 * 3 + 2]};
    *c = (Vector3){g->verts[i2 * 3], g->verts[i2 * 3 + 1], g->verts[i2 * 3 + 2]};
}

static bool GroundLookup_Build(MapGroundLookup *g, const Mesh *mesh)
{
    GroundLookup_Free(g);
    if (mesh == NULL || mesh->vertices == NULL || mesh->triangleCount < 1)
        return false;

    g->verts = mesh->vertices;
    g->indices = mesh->indices;
    g->triCount = mesh->triangleCount;

    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (int t = 0; t < g->triCount; t++) {
        Vector3 a, b, c; GroundLookup_Tri(g, t, &a, &b, &c);
        float lo = fminf(a.x, fminf(b.x, c.x)), hi = fmaxf(a.x, fmaxf(b.x, c.x));
        if (lo < minX) minX = lo; if (hi > maxX) maxX = hi;
        lo = fminf(a.z, fminf(b.z, c.z)); hi = fmaxf(a.z, fmaxf(b.z, c.z));
        if (lo < minZ) minZ = lo; if (hi > maxZ) maxZ = hi;
    }
    float spanX = maxX - minX, spanZ = maxZ - minZ;
    if (!(spanX > 1e-4f) || !(spanZ > 1e-4f)) return false;

    // Aim for roughly two triangles per bin. Fewer and the CSR arrays cost more
    // than the search they save; more and the inner loop stops being constant.
    int n = (int)sqrtf((float)g->triCount * 0.5f);
    if (n < 4) n = 4;
    if (n > 512) n = 512;
    g->nx = n; g->nz = n;
    g->minX = minX; g->minZ = minZ;
    g->invCellX = (float)g->nx / spanX;
    g->invCellZ = (float)g->nz / spanZ;

    int cells = g->nx * g->nz;
    g->cellStart = (int *)RL_CALLOC((size_t)cells + 1u, sizeof(int));
    if (g->cellStart == NULL) return false;

    // Two passes: count per bin, prefix-sum, then fill. A triangle goes into
    // every bin its XZ bounding box touches, so a query never has to look at a
    // neighbouring bin — including for a point sitting exactly on an edge.
    for (int pass = 0; pass < 2; pass++) {
        for (int t = 0; t < g->triCount; t++) {
            Vector3 a, b, c; GroundLookup_Tri(g, t, &a, &b, &c);
            int x0 = (int)((fminf(a.x, fminf(b.x, c.x)) - minX) * g->invCellX);
            int x1 = (int)((fmaxf(a.x, fmaxf(b.x, c.x)) - minX) * g->invCellX);
            int z0 = (int)((fminf(a.z, fminf(b.z, c.z)) - minZ) * g->invCellZ);
            int z1 = (int)((fmaxf(a.z, fmaxf(b.z, c.z)) - minZ) * g->invCellZ);
            if (x0 < 0) x0 = 0; if (x1 > g->nx - 1) x1 = g->nx - 1;
            if (z0 < 0) z0 = 0; if (z1 > g->nz - 1) z1 = g->nz - 1;
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++) {
                    int cell = z * g->nx + x;
                    if (pass == 0) g->cellStart[cell + 1]++;
                    else g->cellItems[g->cellStart[cell]++] = t;
                }
        }
        if (pass == 0) {
            for (int i = 0; i < cells; i++)
                g->cellStart[i + 1] += g->cellStart[i];
            g->cellItems = (int *)RL_MALLOC((size_t)g->cellStart[cells] * sizeof(int));
            if (g->cellItems == NULL) { GroundLookup_Free(g); return false; }
        }
    }
    // The fill pass advanced every cellStart by its own count, which turns the
    // array into the EXCLUSIVE end offsets. Shift it back down to starts.
    for (int i = cells; i > 0; i--) g->cellStart[i] = g->cellStart[i - 1];
    g->cellStart[0] = 0;

    g->built = true;
    return true;
}

// Local-space (x, z) -> local Y and face normal. Returns false outside the
// mesh footprint, matching GetRayCollisionMesh's miss.
static bool GroundLookup_Sample(const MapGroundLookup *g, float lx, float lz,
                                float *outY, Vector3 *outNormal)
{
    if (!g->built) return false;
    int cx = (int)((lx - g->minX) * g->invCellX);
    int cz = (int)((lz - g->minZ) * g->invCellZ);
    if (cx < 0 || cz < 0 || cx >= g->nx || cz >= g->nz) return false;

    const int *from = &g->cellItems[g->cellStart[cz * g->nx + cx]];
    const int *to   = &g->cellItems[g->cellStart[cz * g->nx + cx + 1]];

    bool hit = false;
    float bestY = -1e30f;
    Vector3 bestN = {0.0f, 1.0f, 0.0f};

    for (const int *it = from; it != to; it++) {
        Vector3 a, b, c; GroundLookup_Tri(g, *it, &a, &b, &c);

        // Barycentric containment in the XZ plane. The straight-down ray of the
        // raycast this replaces is exactly a 2D point-in-triangle test, so this
        // selects the same triangle; the height then comes from the same plane
        // the ray would have intersected.
        float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (fabsf(d) < 1e-12f) continue;   // degenerate, edge-on to the query
        float w0 = ((b.z - c.z) * (lx - c.x) + (c.x - b.x) * (lz - c.z)) / d;
        float w1 = ((c.z - a.z) * (lx - c.x) + (a.x - c.x) * (lz - c.z)) / d;
        float w2 = 1.0f - w0 - w1;
        // A hair of tolerance so a point landing exactly on a shared edge is
        // claimed by both triangles rather than by neither.
        const float eps = -1e-5f;
        if (w0 < eps || w1 < eps || w2 < eps) continue;

        float y = w0 * a.y + w1 * b.y + w2 * c.y;
        if (!hit || y > bestY) {
            // Highest wins, which is what a ray fired from far above finds
            // first, and is well defined if the mesh ever folds over itself.
            bestY = y;
            Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a),
                                            Vector3Subtract(c, a));
            if (n.y < 0.0f) n = Vector3Negate(n);
            bestN = Vector3Normalize(n);
            hit = true;
        }
    }
    if (!hit) return false;
    if (outY) *outY = bestY;
    if (outNormal) *outNormal = bestN;
    return true;
}

// Prove the accelerator agrees with the thing it accelerates, on the real mesh,
// at load time. Off unless WUXING_GROUND_LOOKUP_VERIFY=1 — the point is that it
// CAN be re-run on demand whenever this file or raylib's mesh generation
// changes, not that it runs every boot.
static void GroundLookup_Verify(const MapGroundSurface *ground)
{
    const char *v = getenv("WUXING_GROUND_LOOKUP_VERIFY");
    if (v == NULL || *v == '\0' || *v == '0') return;
    if (!ground->lookup.built || ground->model.meshCount < 1) {
        TraceLog(LOG_WARNING, "GROUND LOOKUP VERIFY: no grid to verify");
        return;
    }
    const MapGroundLookup *g = &ground->lookup;
    Mesh mesh = ground->model.meshes[0];
    float spanX = (float)g->nx / g->invCellX, spanZ = (float)g->nz / g->invCellZ;

    const int N = 64;
    int probes = 0, agree = 0, missBoth = 0, disagreeHit = 0;
    double maxDelta = 0.0, tFast = 0.0, tRay = 0.0;
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            float lx = g->minX + spanX * ((float)i + 0.5f) / (float)N;
            float lz = g->minZ + spanZ * ((float)j + 0.5f) / (float)N;
            probes++;

            double t0 = GetTime();
            float fy; Vector3 fn;
            bool fHit = GroundLookup_Sample(g, lx, lz, &fy, &fn);
            tFast += GetTime() - t0;

            t0 = GetTime();
            Ray ray = {(Vector3){lx, 1000.0f, lz}, (Vector3){0.0f, -1.0f, 0.0f}};
            RayCollision rc = GetRayCollisionMesh(ray, mesh, MatrixIdentity());
            tRay += GetTime() - t0;

            if (!fHit && !rc.hit) { missBoth++; agree++; continue; }
            if (fHit != rc.hit) { disagreeHit++; continue; }
            double d = fabs((double)fy - (double)rc.point.y);
            if (d > maxDelta) maxDelta = d;
            if (d < 1e-3) agree++;
        }
    }
    TraceLog(LOG_INFO, "GROUND LOOKUP VERIFY: %d probes, %d agree, %d hit-mismatch, "
                       "%d missed by both, max |dY| = %.6f m",
             probes, agree, disagreeHit, missBoth, maxDelta);
    TraceLog(LOG_INFO, "GROUND LOOKUP VERIFY: grid %.3f us/sample, raycast %.3f us/sample "
                       "(%d triangles, %dx%d bins)",
             tFast * 1e6 / probes, tRay * 1e6 / probes, g->triCount, g->nx, g->nz);
}

static MapGroundSurface SetupGroundMaterial(Mesh mesh, float width, float depth, float tileSize,
                                            const char *splatMapPath,
                                            const char *grassTexPath,
                                            const char *pathTexPath)
{
    MapGroundSurface ground = {0};
    ground.model = LoadModelFromMesh(mesh);
    // Built from the model's own mesh, not from `mesh`: LoadModelFromMesh takes
    // the struct by value, and the grid borrows pointers into the copy the
    // Model now owns and will free.
    if (ground.model.meshCount > 0)
        GroundLookup_Build(&ground.lookup, &ground.model.meshes[0]);

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
        groundShader.locs[SHADER_LOC_VERTEX_NORMAL] =
            GetShaderLocationAttrib(groundShader, "vertexNormal");
        MapShadow_ConfigureShader(groundShader);

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
    MapShadow_AttachMaterial(&ground.model.materials[0]);

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
    GroundLookup_Verify(&ground);
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

    // The model's transform is a pure translation (MapProp_DrawGround passes
    // exactly this to DrawModel), so world <-> local is a subtraction and the
    // grid can be queried directly in local space.
    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };

    float ly;
    if (GroundLookup_Sample(&ground->lookup, x - pos.x, z - pos.z, &ly, NULL))
        return ly + pos.y;
    if (ground->lookup.built)
        return worldCenter.y; // inside the grid's world, outside the mesh

    // Fallback: raycast straight down through the ACTUAL rendered mesh — see
    // the header comment on why this replaced a hand-derived pixel formula.
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

    float ly; Vector3 ln;
    if (GroundLookup_Sample(&ground->lookup, x - pos.x, z - pos.z, &ly, &ln)) {
        if (outPosition) *outPosition = (Vector3){x, ly + pos.y, z};
        if (outNormal) *outNormal = ln;
        return true;
    }
    if (ground->lookup.built) return false;

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
    MapShadow_UpdateShader(groundShader);



    // Vẽ mặt đất — drawOffset (0,0,0) for the flat plane, non-zero for the
    // heightmap variant (see MapProp_CreateGroundHeightmap).
    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };
    DrawModel(ground->model, pos, 1.0f, WHITE);
}

void MapProp_SetGroundTint(MapGroundSurface *ground, Color tint)
{
    if (!ground || !ground->ready)
        return;
    tint.a = 255;
    ground->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = tint;
}

void MapProp_UnloadGround(MapGroundSurface *ground)
{
    if (!ground->ready)
        return;
    GroundLookup_Free(&ground->lookup);
    UnloadModel(ground->model);
    ground->ready = false;
}

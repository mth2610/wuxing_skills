// Ground plane — splatmap blend of grass/path textures (maps/toolkit/shaders/ground_splat.fs).
// #include'd once from map_props.c — not a standalone translation unit.

// Biến toàn cục (static) để lưu trữ Shader và vị trí các biến (Uniform Locations)
// Giúp tối ưu hiệu năng, tránh việc phải tìm location mỗi frame.
static int locIslandRect = -1, locIslandShape = -1;
static Shader groundShader = {0};
static bool shaderLoaded = false;
static int locLightDir = -1;
static int locLightColor = -1;
static int locAmbientColor = -1;
static int locViewPos = -1;
static int locTime = -1;
static int locPathSegs = -1;
static int locPathSegCount = -1;
static int locLakeParams = -1;
static int locGroundOffset = -1;
static int locGroundTiling = -1, locGroundCloudNoise = -1;
static int locGroundReliefSampler = -1, locGroundReliefEnabled = -1;
static int locGroundDebrisSampler = -1, locGroundDebrisEnabled = -1;
static int locEcologyRect = -1;
static int locEcologyEnabled = -1;
static int locEcologySampler = -1, locEcologyDistanceSampler = -1;
static int locGroundCloudUV = -1, locGroundCloudShape = -1, locGroundCloudProjection = -1;
static int locThinShadowBoost = -1;
static int locTexGrass = -1, locTexPath = -1;
static int locTexGrassMaterial = -1, locTexSoilMaterial = -1;

#define MAX_GROUND_PATH_SEGS 16
#define GROUND_CLOUD_TEXTURE_SLOT 7         // Raw 2D binding; not a cubemap material map.
#define GROUND_RELIEF_TEXTURE_SLOT 8        // Separate 2D resource, never material normals/roughness.
#define GROUND_DEBRIS_TEXTURE_SLOT 9        // Macro debris distribution map (R=litter, G=scree, B=twigs, A=moss).
#define GROUND_ECOLOGY_TEXTURE_SLOT 10      // Macro habitat / vegetation suitability.
#define GROUND_ECOLOGY_DIST_TEXTURE_SLOT 11 // Metric path & shore signed distance.
static Vector4 s_groundPathSegs[MAX_GROUND_PATH_SEGS];
static int s_groundPathSegCount = 0;
static Vector4 s_groundLakeParams = {0};

// ── MapGroundLookup — an XZ bin grid over the ground mesh's triangles ────────

static void GroundLookup_Free(MapGroundLookup *g)
{
    if (g->cellStart)
    {
        RL_FREE(g->cellStart);
        g->cellStart = NULL;
    }
    if (g->cellItems)
    {
        RL_FREE(g->cellItems);
        g->cellItems = NULL;
    }
    g->built = false;
}

static inline void GroundLookup_Tri(const MapGroundLookup *g, int t,
                                    Vector3 *a, Vector3 *b, Vector3 *c)
{
    int i0, i1, i2;
    if (g->indices)
    {
        i0 = g->indices[t * 3 + 0];
        i1 = g->indices[t * 3 + 1];
        i2 = g->indices[t * 3 + 2];
    }
    else
    {
        i0 = t * 3 + 0;
        i1 = t * 3 + 1;
        i2 = t * 3 + 2;
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
    for (int t = 0; t < g->triCount; t++)
    {
        Vector3 a, b, c;
        GroundLookup_Tri(g, t, &a, &b, &c);
        float lo = fminf(a.x, fminf(b.x, c.x)), hi = fmaxf(a.x, fmaxf(b.x, c.x));
        if (lo < minX)
            minX = lo;
        if (hi > maxX)
            maxX = hi;
        lo = fminf(a.z, fminf(b.z, c.z));
        hi = fmaxf(a.z, fmaxf(b.z, c.z));
        if (lo < minZ)
            minZ = lo;
        if (hi > maxZ)
            maxZ = hi;
    }
    float spanX = maxX - minX, spanZ = maxZ - minZ;
    if (!(spanX > 1e-4f) || !(spanZ > 1e-4f))
        return false;

    int n = (int)sqrtf((float)g->triCount * 0.5f);
    if (n < 4)
        n = 4;
    if (n > 512)
        n = 512;
    g->nx = n;
    g->nz = n;
    g->minX = minX;
    g->minZ = minZ;
    g->invCellX = (float)g->nx / spanX;
    g->invCellZ = (float)g->nz / spanZ;

    int cells = g->nx * g->nz;
    g->cellStart = (int *)RL_CALLOC((size_t)cells + 1u, sizeof(int));
    if (g->cellStart == NULL)
        return false;

    for (int pass = 0; pass < 2; pass++)
    {
        for (int t = 0; t < g->triCount; t++)
        {
            Vector3 a, b, c;
            GroundLookup_Tri(g, t, &a, &b, &c);
            int x0 = (int)((fminf(a.x, fminf(b.x, c.x)) - minX) * g->invCellX);
            int x1 = (int)((fmaxf(a.x, fmaxf(b.x, c.x)) - minX) * g->invCellX);
            int z0 = (int)((fminf(a.z, fminf(b.z, c.z)) - minZ) * g->invCellZ);
            int z1 = (int)((fmaxf(a.z, fmaxf(b.z, c.z)) - minZ) * g->invCellZ);
            if (x0 < 0)
                x0 = 0;
            if (x1 > g->nx - 1)
                x1 = g->nx - 1;
            if (z0 < 0)
                z0 = 0;
            if (z1 > g->nz - 1)
                z1 = g->nz - 1;
            for (int z = z0; z <= z1; z++)
            {
                for (int x = x0; x <= x1; x++)
                {
                    int cell = z * g->nx + x;
                    if (pass == 0)
                        g->cellStart[cell + 1]++;
                    else
                        g->cellItems[g->cellStart[cell]++] = t;
                }
            }
        }
        if (pass == 0)
        {
            for (int i = 0; i < cells; i++)
                g->cellStart[i + 1] += g->cellStart[i];
            g->cellItems = (int *)RL_MALLOC((size_t)g->cellStart[cells] * sizeof(int));
            if (g->cellItems == NULL)
            {
                GroundLookup_Free(g);
                return false;
            }
        }
    }
    for (int i = cells; i > 0; i--)
        g->cellStart[i] = g->cellStart[i - 1];
    g->cellStart[0] = 0;

    g->built = true;
    return true;
}

// Local-space (x, z) -> local Y and face normal.
// Defers cross-product & sqrtf normalization until after the loop and only when outNormal != NULL.
static bool GroundLookup_Sample(const MapGroundLookup *g, float lx, float lz,
                                float *outY, Vector3 *outNormal)
{
    if (!g->built)
        return false;
    int cx = (int)((lx - g->minX) * g->invCellX);
    int cz = (int)((lz - g->minZ) * g->invCellZ);
    if (cx < 0 || cz < 0 || cx >= g->nx || cz >= g->nz)
        return false;

    const int *from = &g->cellItems[g->cellStart[cz * g->nx + cx]];
    const int *to = &g->cellItems[g->cellStart[cz * g->nx + cx + 1]];

    int bestTri = -1;
    float bestY = -1e30f;

    for (const int *it = from; it != to; it++)
    {
        Vector3 a, b, c;
        GroundLookup_Tri(g, *it, &a, &b, &c);

        float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (fabsf(d) < 1e-12f)
            continue;
        float invD = 1.0f / d;
        float w0 = ((b.z - c.z) * (lx - c.x) + (c.x - b.x) * (lz - c.z)) * invD;
        float w1 = ((c.z - a.z) * (lx - c.x) + (a.x - c.x) * (lz - c.z)) * invD;
        float w2 = 1.0f - w0 - w1;
        const float eps = -1e-5f;
        if (w0 < eps || w1 < eps || w2 < eps)
            continue;

        float y = w0 * a.y + w1 * b.y + w2 * c.y;
        if (bestTri < 0 || y > bestY)
        {
            bestY = y;
            bestTri = *it;
        }
    }
    if (bestTri < 0)
        return false;
    if (outY)
        *outY = bestY;
    if (outNormal)
    {
        Vector3 a, b, c;
        GroundLookup_Tri(g, bestTri, &a, &b, &c);
        Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (n.y < 0.0f)
            n = Vector3Negate(n);
        *outNormal = Vector3Normalize(n);
    }
    return true;
}

static void GroundLookup_Verify(const MapGroundSurface *ground)
{
    const char *v = getenv("WUXING_GROUND_LOOKUP_VERIFY");
    if (v == NULL || *v == '\0' || *v == '0')
        return;
    if (!ground->lookup.built || ground->model.meshCount < 1)
    {
        TraceLog(LOG_WARNING, "GROUND LOOKUP VERIFY: no grid to verify");
        return;
    }
    const MapGroundLookup *g = &ground->lookup;
    Mesh mesh = ground->model.meshes[0];
    float spanX = (float)g->nx / g->invCellX, spanZ = (float)g->nz / g->invCellZ;

    const int N = 64;
    int probes = 0, agree = 0, missBoth = 0, disagreeHit = 0;
    double maxDelta = 0.0, tFast = 0.0, tRay = 0.0;
    for (int j = 0; j < N; j++)
    {
        for (int i = 0; i < N; i++)
        {
            float lx = g->minX + spanX * ((float)i + 0.5f) / (float)N;
            float lz = g->minZ + spanZ * ((float)j + 0.5f) / (float)N;
            probes++;

            double t0 = GetTime();
            float fy;
            Vector3 fn;
            bool fHit = GroundLookup_Sample(g, lx, lz, &fy, &fn);
            tFast += GetTime() - t0;

            t0 = GetTime();
            Ray ray = {(Vector3){lx, 1000.0f, lz}, (Vector3){0.0f, -1.0f, 0.0f}};
            RayCollision rc = GetRayCollisionMesh(ray, mesh, MatrixIdentity());
            tRay += GetTime() - t0;

            if (!fHit && !rc.hit)
            {
                missBoth++;
                agree++;
                continue;
            }
            if (fHit != rc.hit)
            {
                disagreeHit++;
                continue;
            }
            double d = fabs((double)fy - (double)rc.point.y);
            if (d > maxDelta)
                maxDelta = d;
            if (d < 1e-3)
                agree++;
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
    if (ground.model.meshCount > 0)
        GroundLookup_Build(&ground.lookup, &ground.model.meshes[0]);

    // 1. Load Textures
    Texture2D texSplat = ResourceManager_LoadTexture(splatMapPath);
    Texture2D texGrass = MapLoadMippedTexture(grassTexPath, TEXTURE_WRAP_REPEAT);
    Texture2D texPath = MapLoadMippedTexture(pathTexPath, TEXTURE_WRAP_REPEAT);

    SetTextureFilter(texSplat, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(texSplat, TEXTURE_WRAP_REPEAT);

    // 2. Initialize Shader & cache all uniform locations once
    if (!shaderLoaded)
    {
        groundShader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_splat.vs",
                                                  "maps/toolkit/shaders/ground_splat.fs");

        groundShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(groundShader, "matModel");
        groundShader.locs[SHADER_LOC_VERTEX_NORMAL] =
            GetShaderLocationAttrib(groundShader, "vertexNormal");
        groundShader.locs[SHADER_LOC_MAP_ROUGHNESS] =
            GetShaderLocation(groundShader, "texGrassMaterial");
        groundShader.locs[SHADER_LOC_MAP_EMISSION] =
            GetShaderLocation(groundShader, "texSoilMaterial");
        groundShader.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(groundShader, "texGrass");
        groundShader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(groundShader, "texPath");
        MapShadow_ConfigureShader(groundShader);

        locIslandRect = GetShaderLocation(groundShader, "u_islandRect");
        locIslandShape = GetShaderLocation(groundShader, "u_islandShape");
        locLightDir = GetShaderLocation(groundShader, "lightDir");
        locLightColor = GetShaderLocation(groundShader, "lightColor");
        locAmbientColor = GetShaderLocation(groundShader, "ambientColor");
        locViewPos = GetShaderLocation(groundShader, "viewPos");
        locTime = GetShaderLocation(groundShader, "u_time");
        locPathSegs = GetShaderLocation(groundShader, "u_pathSegs");
        locPathSegCount = GetShaderLocation(groundShader, "u_pathSegCount");
        locLakeParams = GetShaderLocation(groundShader, "u_lakeParams");
        locGroundOffset = GetShaderLocation(groundShader, "u_groundOffset");
        locGroundTiling = GetShaderLocation(groundShader, "tiling");
        locGroundCloudNoise = GetShaderLocation(groundShader, "u_cloudNoise");
        locGroundReliefSampler = GetShaderLocation(groundShader, "u_groundRelief");
        locGroundReliefEnabled = GetShaderLocation(groundShader, "u_groundReliefEnabled");
        locGroundDebrisSampler = GetShaderLocation(groundShader, "u_groundDebris");
        locGroundDebrisEnabled = GetShaderLocation(groundShader, "u_groundDebrisEnabled");
        locEcologyRect = GetShaderLocation(groundShader, "u_ecologyRect");
        locEcologyEnabled = GetShaderLocation(groundShader, "u_ecologyEnabled");
        locGroundCloudUV = GetShaderLocation(groundShader, "u_cloudUV");
        locGroundCloudShape = GetShaderLocation(groundShader, "u_cloudShape");
        locGroundCloudProjection = GetShaderLocation(groundShader, "u_cloudProjection");
        locEcologySampler = GetShaderLocation(groundShader, "u_ecology");
        locEcologyDistanceSampler = GetShaderLocation(groundShader, "u_ecologyDistance");
        locThinShadowBoost = GetShaderLocation(groundShader, "u_shadowThinFeatureBoost");
        locTexGrass = GetShaderLocation(groundShader, "texGrass");
        locTexPath = GetShaderLocation(groundShader, "texPath");
        locTexGrassMaterial = GetShaderLocation(groundShader, "texGrassMaterial");
        locTexSoilMaterial = GetShaderLocation(groundShader, "texSoilMaterial");
        VFXLight_RegisterShader(groundShader);

        shaderLoaded = true;
    }

    // 3. Assign Shader and Textures to Material
    ground.model.materials[0].shader = groundShader;
    ground.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texSplat;  // Slot 0
    ground.model.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = texGrass; // Slot 1
    ground.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = texPath;    // Slot 2
    MapShadow_AttachMaterial(&ground.model.materials[0]);

    // 4. Upload static slot & tiling uniforms inside explicit BeginShaderMode scope for rlvk
    float tiling[2] = {width / tileSize, depth / tileSize};
    ground.tiling = (Vector2){tiling[0], tiling[1]};

    BeginShaderMode(groundShader);
    int grassTexSlot = 1;
    int pathTexSlot = 2;
    int grassMaterialSlot = MATERIAL_MAP_ROUGHNESS;
    int soilMaterialSlot = MATERIAL_MAP_EMISSION;
    if (locTexGrass >= 0)
        SetShaderValue(groundShader, locTexGrass, &grassTexSlot, SHADER_UNIFORM_INT);
    if (locTexPath >= 0)
        SetShaderValue(groundShader, locTexPath, &pathTexSlot, SHADER_UNIFORM_INT);
    if (locTexGrassMaterial >= 0)
        SetShaderValue(groundShader, locTexGrassMaterial, &grassMaterialSlot, SHADER_UNIFORM_INT);
    if (locTexSoilMaterial >= 0)
        SetShaderValue(groundShader, locTexSoilMaterial, &soilMaterialSlot, SHADER_UNIFORM_INT);
    if (locGroundTiling >= 0)
        SetShaderValue(groundShader, locGroundTiling, tiling, SHADER_UNIFORM_VEC2);
    EndShaderMode();

    ground.ready = true;
    GroundLookup_Verify(&ground);
    return ground;
}

MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize,
                                      const char *splatMapPath,
                                      const char *grassTexPath,
                                      const char *pathTexPath)
{
    Mesh mesh = GenMeshPlane(width, depth, 20, 20);
    return SetupGroundMaterial(mesh, width, depth, tileSize, splatMapPath, grassTexPath, pathTexPath);
}

#include "maps/toolkit/map_props_heightfield.inl"

MapGroundSurface MapProp_CreateGroundHeightfield16(const char *heightmapPath, float width, float depth,
                                                   float totalHeight, float cliffDepth, float tileSize,
                                                   const char *splatMapPath, const char *grassTexPath,
                                                   const char *pathTexPath)
{
    Image image = LoadImage(heightmapPath);
    const char *legacy = getenv("WUXING_GROUND_MESH");
    Mesh mesh = Ground_CreateIndexedHeightfield(image, (Vector3){width, totalHeight, depth},
                                                true, !legacy || strcmp(legacy, "legacy") != 0);
    UnloadImage(image);
    if (!mesh.vertices)
    {
        TraceLog(LOG_WARNING, "Packed heightfield load failed: %s", heightmapPath);
        return (MapGroundSurface){0};
    }
    TraceLog(LOG_INFO, "GROUND_MESH: vertices=%d triangles=%d indexed=%d", mesh.vertexCount,
             mesh.triangleCount, mesh.indices != NULL);
    MapGroundSurface ground = SetupGroundMaterial(mesh, width, depth, tileSize, splatMapPath, grassTexPath, pathTexPath);
    ground.drawOffset = (Vector3){-width * 0.5f, -cliffDepth, -depth * 0.5f};
    return ground;
}

MapGroundSurface MapProp_CreateGroundHeightmapEx(const char *heightmapPath, float width, float depth,
                                                 float totalHeight, float cliffDepth, float tileSize,
                                                 const char *splatMapPath,
                                                 const char *grassTexPath,
                                                 const char *pathTexPath)
{
    Image heightmapImg = LoadImage(heightmapPath);
    Mesh mesh = GenMeshHeightmap(heightmapImg, (Vector3){width, totalHeight, depth});

    if (mesh.normals != NULL && heightmapImg.width > 1 && heightmapImg.height > 1 && mesh.triangleCount > 0)
    {
        int mapX = heightmapImg.width;
        int mapZ = heightmapImg.height;
        Vector3 *gridNormals = (Vector3 *)MemAlloc((unsigned int)(mapX * mapZ) * sizeof(Vector3));
        if (gridNormals != NULL)
        {
            memset(gridNormals, 0, (unsigned int)(mapX * mapZ) * sizeof(Vector3));
            for (int t = 0; t < mesh.triangleCount; t++)
            {
                int b = t * 9;
                Vector3 vA = {mesh.vertices[b + 0], mesh.vertices[b + 1], mesh.vertices[b + 2]};
                Vector3 vB = {mesh.vertices[b + 3], mesh.vertices[b + 4], mesh.vertices[b + 5]};
                Vector3 vC = {mesh.vertices[b + 6], mesh.vertices[b + 7], mesh.vertices[b + 8]};
                Vector3 fn = Vector3CrossProduct(Vector3Subtract(vB, vA), Vector3Subtract(vC, vA));
                if (fn.y < 0.0f)
                    fn = Vector3Negate(fn);

                for (int k = 0; k < 3; k++)
                {
                    float vx = mesh.vertices[b + k * 3 + 0];
                    float vz = mesh.vertices[b + k * 3 + 2];
                    int ix = (int)roundf((vx / width) * (float)(mapX - 1));
                    int iz = (int)roundf((vz / depth) * (float)(mapZ - 1));
                    if (ix >= 0 && ix < mapX && iz >= 0 && iz < mapZ)
                    {
                        int gIdx = iz * mapX + ix;
                        gridNormals[gIdx] = Vector3Add(gridNormals[gIdx], fn);
                    }
                }
            }

            for (int i = 0; i < mesh.vertexCount; i++)
            {
                float vx = mesh.vertices[i * 3 + 0];
                float vz = mesh.vertices[i * 3 + 2];
                int ix = (int)roundf((vx / width) * (float)(mapX - 1));
                int iz = (int)roundf((vz / depth) * (float)(mapZ - 1));
                if (ix >= 0 && ix < mapX && iz >= 0 && iz < mapZ)
                {
                    int gIdx = iz * mapX + ix;
                    Vector3 n = Vector3Normalize(gridNormals[gIdx]);
                    if (Vector3Length(n) > 0.001f)
                    {
                        mesh.normals[i * 3 + 0] = n.x;
                        mesh.normals[i * 3 + 1] = n.y;
                        mesh.normals[i * 3 + 2] = n.z;
                    }
                }
            }
            MemFree(gridNormals);
            UpdateMeshBuffer(mesh, 2, mesh.normals, mesh.vertexCount * 3 * (int)sizeof(float), 0);
        }
    }

    UnloadImage(heightmapImg);

    MapGroundSurface result = SetupGroundMaterial(mesh, width, depth, tileSize, splatMapPath, grassTexPath, pathTexPath);
    result.drawOffset = (Vector3){-width * 0.5f, -cliffDepth, -depth * 0.5f};
    return result;
}

MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                               float cliffDepth, float tileSize,
                                               const char *splatMapPath,
                                               const char *grassTexPath,
                                               const char *pathTexPath)
{
    return MapProp_CreateGroundHeightmapEx(heightmapPath, width, depth, cliffDepth, cliffDepth,
                                           tileSize, splatMapPath, grassTexPath, pathTexPath);
}

float MapProp_SampleGroundHeight(const MapGroundSurface *ground, Vector3 worldCenter, float x, float z)
{
    if (!ground->ready || ground->model.meshCount < 1)
        return worldCenter.y;

    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };

    float ly;
    if (GroundLookup_Sample(&ground->lookup, x - pos.x, z - pos.z, &ly, NULL))
        return ly + pos.y;
    if (ground->lookup.built)
        return worldCenter.y;

    Matrix transform = MatrixTranslate(pos.x, pos.y, pos.z);
    Ray ray = {(Vector3){x, worldCenter.y + 1000.0f, z}, (Vector3){0.0f, -1.0f, 0.0f}};
    RayCollision hit = GetRayCollisionMesh(ray, ground->model.meshes[0], transform);
    if (hit.hit)
        return hit.point.y;

    return worldCenter.y;
}

bool MapProp_SampleGroundSurface(const MapGroundSurface *ground, Vector3 worldCenter,
                                 float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (outPosition)
        *outPosition = (Vector3){x, worldCenter.y, z};
    if (outNormal)
        *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
    if (!ground->ready || ground->model.meshCount < 1)
        return false;
    Vector3 pos = {worldCenter.x + ground->drawOffset.x, worldCenter.y + ground->drawOffset.y,
                   worldCenter.z + ground->drawOffset.z};

    float ly;
    Vector3 ln;
    if (GroundLookup_Sample(&ground->lookup, x - pos.x, z - pos.z, &ly, outNormal ? &ln : NULL))
    {
        if (outPosition)
            *outPosition = (Vector3){x, ly + pos.y, z};
        if (outNormal)
            *outNormal = ln;
        return true;
    }
    if (ground->lookup.built)
        return false;

    Ray ray = {(Vector3){x, worldCenter.y + 1000.0f, z}, (Vector3){0.0f, -1.0f, 0.0f}};
    RayCollision hit = GetRayCollisionMesh(ray, ground->model.meshes[0],
                                           MatrixTranslate(pos.x, pos.y, pos.z));
    if (!hit.hit)
        return false;
    if (outPosition)
        *outPosition = hit.point;
    if (outNormal)
        *outNormal = Vector3Normalize(hit.normal);
    return true;
}

void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter)
{
    if (!ground->ready)
        return;

    BeginShaderMode(groundShader);
    MapShadow_AttachMaterial(&ground->model.materials[0]);
    Vector4 islandShape = {ground->boundary.cornerRadius, ground->boundary.mistWidth, ground->boundary.groundInset, 0};
    if (locIslandRect >= 0)
        SetShaderValue(groundShader, locIslandRect, &ground->boundary.rect, SHADER_UNIFORM_VEC4);
    if (locIslandShape >= 0)
        SetShaderValue(groundShader, locIslandShape, &islandShape, SHADER_UNIFORM_VEC4);

    EnvCloudShadowFrame cloud = Environment_GetCloudShadowFrame();
    rlActiveTextureSlot(GROUND_CLOUD_TEXTURE_SLOT);
    rlEnableTexture(cloud.noiseTexture.id ? cloud.noiseTexture.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(GROUND_RELIEF_TEXTURE_SLOT);
    rlEnableTexture(ground->reliefTexture.id ? ground->reliefTexture.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(GROUND_DEBRIS_TEXTURE_SLOT);
    rlEnableTexture(ground->debrisTexture.id ? ground->debrisTexture.id : rlGetTextureIdDefault());

    int ecologyEnabled = ground->ecology && ground->ecology->ready;
    rlActiveTextureSlot(GROUND_ECOLOGY_TEXTURE_SLOT);
    rlEnableTexture((ecologyEnabled && ground->ecology->habitatTexture.id)
                        ? ground->ecology->habitatTexture.id
                        : rlGetTextureIdDefault());
    rlActiveTextureSlot(GROUND_ECOLOGY_DIST_TEXTURE_SLOT);
    rlEnableTexture((ecologyEnabled && ground->ecology->distanceTexture.id)
                        ? ground->ecology->distanceTexture.id
                        : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);

    int reliefSlot = GROUND_RELIEF_TEXTURE_SLOT;
    int reliefEnabled = ground->reliefTexture.id != 0;
    if (locGroundReliefSampler >= 0)
        SetShaderValue(groundShader, locGroundReliefSampler, &reliefSlot, SHADER_UNIFORM_INT);
    if (locGroundReliefEnabled >= 0)
        SetShaderValue(groundShader, locGroundReliefEnabled, &reliefEnabled, SHADER_UNIFORM_INT);
    int debrisSlot = GROUND_DEBRIS_TEXTURE_SLOT;
    int debrisEnabled = ground->debrisTexture.id != 0;
    if (locGroundDebrisSampler >= 0)
        SetShaderValue(groundShader, locGroundDebrisSampler, &debrisSlot, SHADER_UNIFORM_INT);
    if (locGroundDebrisEnabled >= 0)
        SetShaderValue(groundShader, locGroundDebrisEnabled, &debrisEnabled, SHADER_UNIFORM_INT);
    int ecoSlot = GROUND_ECOLOGY_TEXTURE_SLOT;
    if (locEcologySampler >= 0)
        SetShaderValue(groundShader, locEcologySampler, &ecoSlot, SHADER_UNIFORM_INT);
    int ecoDistSlot = GROUND_ECOLOGY_DIST_TEXTURE_SLOT;
    if (locEcologyDistanceSampler >= 0)
        SetShaderValue(groundShader, locEcologyDistanceSampler, &ecoDistSlot, SHADER_UNIFORM_INT);
    int cloudSlot = GROUND_CLOUD_TEXTURE_SLOT;
    if (locGroundCloudNoise >= 0)
        SetShaderValue(groundShader, locGroundCloudNoise, &cloudSlot, SHADER_UNIFORM_INT);
    if (locGroundTiling >= 0)
        SetShaderValue(groundShader, locGroundTiling, &ground->tiling, SHADER_UNIFORM_VEC2);
    if (locGroundCloudUV >= 0)
        SetShaderValue(groundShader, locGroundCloudUV, &cloud.uvTransform, SHADER_UNIFORM_VEC4);
    if (locGroundCloudShape >= 0)
        SetShaderValue(groundShader, locGroundCloudShape, &cloud.shape, SHADER_UNIFORM_VEC4);
    if (locGroundCloudProjection >= 0)
        SetShaderValue(groundShader, locGroundCloudProjection, &cloud.projection, SHADER_UNIFORM_VEC2);
    if (locEcologyEnabled >= 0)
        SetShaderValue(groundShader, locEcologyEnabled, &ecologyEnabled, SHADER_UNIFORM_INT);
    if (ecologyEnabled && locEcologyRect >= 0)
        SetShaderValue(groundShader, locEcologyRect, &ground->ecology->rect, SHADER_UNIFORM_VEC4);

    Vector3 lightDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Color ambCol = Environment_GetAmbientColor();

    float lightDirArr[3] = {lightDir.x, lightDir.y, lightDir.z};
    float sunIntensity = Environment_GetSunIntensity();
    float sunColArr[4] = {
        (sunCol.r / 255.0f) * sunIntensity,
        (sunCol.g / 255.0f) * sunIntensity,
        (sunCol.b / 255.0f) * sunIntensity,
        sunCol.a / 255.0f};
    float ambColArr[4] = {ambCol.r / 255.0f, ambCol.g / 255.0f, ambCol.b / 255.0f, ambCol.a / 255.0f};

    if (locLightDir >= 0)
        SetShaderValue(groundShader, locLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    if (locLightColor >= 0)
        SetShaderValue(groundShader, locLightColor, sunColArr, SHADER_UNIFORM_VEC4);
    if (locAmbientColor >= 0)
        SetShaderValue(groundShader, locAmbientColor, ambColArr, SHADER_UNIFORM_VEC4);
    if (locViewPos >= 0)
        SetShaderValue(groundShader, locViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (locTime >= 0)
    {
        float timeVal = (float)GetTime();
        SetShaderValue(groundShader, locTime, &timeVal, SHADER_UNIFORM_FLOAT);
    }
    if (locPathSegCount >= 0)
        SetShaderValue(groundShader, locPathSegCount, &s_groundPathSegCount, SHADER_UNIFORM_INT);
    if (locPathSegs >= 0 && s_groundPathSegCount > 0)
        SetShaderValueV(groundShader, locPathSegs, s_groundPathSegs, SHADER_UNIFORM_VEC4, s_groundPathSegCount);
    if (locLakeParams >= 0)
        SetShaderValue(groundShader, locLakeParams, &s_groundLakeParams, SHADER_UNIFORM_VEC4);

    MapShadow_UpdateShader(groundShader);
    // Restrained thinShadowBoost (0.30) keeps grass shadow silhouettes soft and natural
    // rather than dilating them into harsh dark bars across hillsides.
    if (locThinShadowBoost >= 0)
    {
        float thinShadowBoost = (GfxQuality_Get() >= GFX_HIGH) ? 0.30f : 0.0f;
        SetShaderValue(groundShader, locThinShadowBoost, &thinShadowBoost, SHADER_UNIFORM_FLOAT);
    }

    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };
    if (locGroundOffset >= 0)
        SetShaderValue(groundShader, locGroundOffset, &pos, SHADER_UNIFORM_VEC3);
    DrawModel(ground->model, pos, 1.0f, WHITE);

    rlActiveTextureSlot(GROUND_ECOLOGY_DIST_TEXTURE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GROUND_ECOLOGY_TEXTURE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GROUND_RELIEF_TEXTURE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GROUND_DEBRIS_TEXTURE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GROUND_CLOUD_TEXTURE_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    EndShaderMode();
}

void MapProp_SetGroundHabitat(MapGroundSurface *ground,
                              const Vector4 *pathSegments, int segmentCount,
                              Vector4 lakeParams)
{
    (void)ground;
    if (segmentCount > MAX_GROUND_PATH_SEGS)
        segmentCount = MAX_GROUND_PATH_SEGS;
    s_groundPathSegCount = segmentCount;
    for (int i = 0; i < segmentCount; i++)
    {
        s_groundPathSegs[i] = pathSegments[i];
    }
    s_groundLakeParams = lakeParams;
}

void MapProp_DrawGroundShadowCaster(MapGroundSurface *ground, Vector3 worldCenter,
                                    Shader depthShader)
{
    if (!ground || !ground->ready || ground->model.materialCount < 1)
        return;
    Shader previous = ground->model.materials[0].shader;
    ground->model.materials[0].shader = depthShader;
    Vector3 pos = Vector3Add(worldCenter, ground->drawOffset);
    DrawModel(ground->model, pos, 1.0f, WHITE);
    rlDrawRenderBatchActive();
    ground->model.materials[0].shader = previous;
}

void MapProp_SetGroundTint(MapGroundSurface *ground, Color tint)
{
    if (!ground || !ground->ready)
        return;
    tint.a = 255;
    ground->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = tint;
}

void MapProp_SetGroundSurfaceMaps(MapGroundSurface *ground,
                                  const char *grassMaterialPath,
                                  const char *soilMaterialPath)
{
    if (!ground || !ground->ready || !grassMaterialPath || !soilMaterialPath)
        return;
    Texture2D grassMaterial = MapLoadMippedTexture(grassMaterialPath, TEXTURE_WRAP_REPEAT);
    Texture2D soilMaterial = MapLoadMippedTexture(soilMaterialPath, TEXTURE_WRAP_REPEAT);

    ground->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = grassMaterial;
    ground->model.materials[0].maps[MATERIAL_MAP_EMISSION].texture = soilMaterial;
}

void MapProp_UnloadGround(MapGroundSurface *ground)
{
    if (!ground || !ground->ready)
        return;
    GroundLookup_Free(&ground->lookup);
    if (ground->ecology)
        MapProp_SetGroundEcology(ground, NULL);
    // Leave ResourceManager-cached textures intact; only clear borrowed references
    ground->reliefTexture = (Texture2D){0};
    ground->debrisTexture = (Texture2D){0};
    UnloadModel(ground->model);
    ground->ready = false;
}

void MapProp_SetGroundEcology(MapGroundSurface *ground, const MapEcology *ecology)
{
    if (!ground || !ground->ready)
        return;
    ground->ecology = ecology;
}

void MapProp_SetGroundReliefMap(MapGroundSurface *ground, const char *reliefPath)
{
    if (!ground || !ground->ready)
        return;
    ground->reliefTexture = reliefPath ? MapLoadMippedTexture(reliefPath, TEXTURE_WRAP_REPEAT) : (Texture2D){0};
}

void MapProp_SetGroundDebrisMap(MapGroundSurface *ground, const char *debrisMapPath)
{
    if (!ground || !ground->ready)
        return;
    ground->debrisTexture = debrisMapPath ? MapLoadMippedTexture(debrisMapPath, TEXTURE_WRAP_REPEAT) : (Texture2D){0};
}
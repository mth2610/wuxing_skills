// Ground plane — splatmap blend of grass/path textures (maps/toolkit/shaders/ground_splat.fs).
// #include'd once from map_props.c — not a standalone translation unit.

// Biến toàn cục (static) để lưu trữ Shader và vị trí các biến (Uniform Locations)
// Giúp tối ưu hiệu năng, tránh việc phải tìm location mỗi frame.
static int locIslandRect = -1, locIslandShape = -1;
static Shader groundShader = {0};
static unsigned int s_groundShaderId = 0;
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
static int locGroundRockSampler = -1, locGroundRockNormalSampler = -1, locGroundRockEnabled = -1;
static int locEcologyRect = -1;
static int locEcologyEnabled = -1;
static int locEcologySampler = -1, locEcologyDistanceSampler = -1;
static int locGroundCloudUV = -1, locGroundCloudShape = -1, locGroundCloudProjection = -1;
static int locThinShadowBoost = -1;
static int locTexGrass = -1, locTexPath = -1;
static int locTexGrassMaterial = -1, locTexSoilMaterial = -1;
static int locSeismicOriginDir = -1, locSeismicParams = -1, locSeismicProfile = -1;

#define MAX_GROUND_PATH_SEGS 16
#define GROUND_CLOUD_TEXTURE_SLOT 7         // Raw 2D binding; not a cubemap material map.
#define GROUND_RELIEF_TEXTURE_SLOT 8        // Separate 2D resource, never material normals/roughness.
#define GROUND_DEBRIS_TEXTURE_SLOT 9        // Macro debris distribution map (R=litter, G=scree, B=twigs, A=moss).
#define GROUND_ECOLOGY_TEXTURE_SLOT 10      // Macro habitat / vegetation suitability.
#define GROUND_ECOLOGY_DIST_TEXTURE_SLOT 11 // Metric path & shore signed distance.
#define GROUND_ROCK_TEXTURE_SLOT 12         // Dedicated high-resolution cliff/crag rock diffuse.
#define GROUND_ROCK_NORMAL_SLOT 13          // Packed normal RGB + roughness for triplanar cliffs.
static Vector4 s_groundPathSegs[MAX_GROUND_PATH_SEGS];
static int s_groundPathSegCount = 0;
static Vector4 s_groundLakeParams = {0};
static bool s_groundHabitatDirty = true;

// Cache trạng thái đã upload lên groundShader để loại bỏ các lần gọi SetShaderValue / SetShaderValueV dư thừa
static unsigned int s_groundStateShaderId = 0;
static Vector4 s_groundCachedIslandRect = {0};
static Vector4 s_groundCachedIslandShape = {0};
static Vector2 s_groundCachedTiling = {0};
static Vector4 s_groundCachedEcologyRect = {0};
static int s_groundCachedReliefEnabled = -1;
static int s_groundCachedDebrisEnabled = -1;
static int s_groundCachedRockEnabled = -1;
static int s_groundCachedEcologyEnabled = -1;
static float s_groundCachedThinBoost = -1.0f;

static inline int Ground_GetTerrainFilterMode(void)
{
    GfxQuality q = GfxQuality_Get();
    if (q >= GFX_HIGH)
        return TEXTURE_FILTER_ANISOTROPIC_8X;
    if (q == GFX_MED)
        return TEXTURE_FILTER_ANISOTROPIC_4X;
    return TEXTURE_FILTER_TRILINEAR;
}

// ── MapGroundLookup — an XZ bin grid over the ground mesh's triangles ────────

static void GroundLookup_Free(MapGroundLookup *g)
{
    if (!g)
        return;
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
    const float *restrict v = g->verts;
    *a = (Vector3){v[i0 * 3], v[i0 * 3 + 1], v[i0 * 3 + 2]};
    *b = (Vector3){v[i1 * 3], v[i1 * 3 + 1], v[i1 * 3 + 2]};
    *c = (Vector3){v[i2 * 3], v[i2 * 3 + 1], v[i2 * 3 + 2]};
}

static bool GroundLookup_Build(MapGroundLookup *g, const Mesh *mesh)
{
    GroundLookup_Free(g);
    if (g == NULL || mesh == NULL || mesh->vertices == NULL || mesh->triangleCount < 1)
        return false;

    g->verts = mesh->vertices;
    g->indices = mesh->indices;
    g->normals = mesh->normals;
    g->triCount = mesh->triangleCount;

    const float *restrict verts = g->verts;
    const unsigned short *restrict indices = g->indices;
    const int triCount = g->triCount;

    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (int t = 0; t < triCount; t++)
    {
        int i0 = indices ? indices[t * 3 + 0] : (t * 3 + 0);
        int i1 = indices ? indices[t * 3 + 1] : (t * 3 + 1);
        int i2 = indices ? indices[t * 3 + 2] : (t * 3 + 2);
        float ax = verts[i0 * 3 + 0], az = verts[i0 * 3 + 2];
        float bx = verts[i1 * 3 + 0], bz = verts[i1 * 3 + 2];
        float cx = verts[i2 * 3 + 0], cz = verts[i2 * 3 + 2];
        float loX = fminf(ax, fminf(bx, cx)), hiX = fmaxf(ax, fmaxf(bx, cx));
        float loZ = fminf(az, fminf(bz, cz)), hiZ = fmaxf(az, fmaxf(bz, cz));
        if (loX < minX)
            minX = loX;
        if (hiX > maxX)
            maxX = hiX;
        if (loZ < minZ)
            minZ = loZ;
        if (hiZ > maxZ)
            maxZ = hiZ;
    }
    float spanX = maxX - minX, spanZ = maxZ - minZ;
    if (!(spanX > 1e-4f) || !(spanZ > 1e-4f))
        return false;

    // Pad upper span slightly so points lying exactly on maxX / maxZ map into cell (n - 1)
    spanX *= 1.00001f;
    spanZ *= 1.00001f;

    int n = (int)sqrtf((float)triCount * 0.5f);
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

    const float invCellX = g->invCellX;
    const float invCellZ = g->invCellZ;
    const int maxCellX = g->nx - 1;
    const int maxCellZ = g->nz - 1;

    for (int pass = 0; pass < 2; pass++)
    {
        for (int t = 0; t < triCount; t++)
        {
            int i0 = indices ? indices[t * 3 + 0] : (t * 3 + 0);
            int i1 = indices ? indices[t * 3 + 1] : (t * 3 + 1);
            int i2 = indices ? indices[t * 3 + 2] : (t * 3 + 2);
            float ax = verts[i0 * 3 + 0], az = verts[i0 * 3 + 2];
            float bx = verts[i1 * 3 + 0], bz = verts[i1 * 3 + 2];
            float cx = verts[i2 * 3 + 0], cz = verts[i2 * 3 + 2];

            int x0 = (int)((fminf(ax, fminf(bx, cx)) - minX) * invCellX);
            int x1 = (int)((fmaxf(ax, fmaxf(bx, cx)) - minX) * invCellX);
            int z0 = (int)((fminf(az, fminf(bz, cz)) - minZ) * invCellZ);
            int z1 = (int)((fmaxf(az, fmaxf(bz, cz)) - minZ) * invCellZ);
            if (x0 < 0)
                x0 = 0;
            if (x1 > maxCellX)
                x1 = maxCellX;
            if (z0 < 0)
                z0 = 0;
            if (z1 > maxCellZ)
                z1 = maxCellZ;
            for (int z = z0; z <= z1; z++)
            {
                int rowOffset = z * n;
                for (int x = x0; x <= x1; x++)
                {
                    int cell = rowOffset + x;
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
// Rejects non-containing triangles via unnormalized barycentric signs before dividing,
// eliminating ~85% of hardware divisions per ground sample.
static bool GroundLookup_Sample(const MapGroundLookup *g, float lx, float lz,
                                float *outY, Vector3 *outNormal)
{
    if (!g || !g->built)
        return false;

    float relX = lx - g->minX;
    float relZ = lz - g->minZ;
    if (relX < 0.0f || relZ < 0.0f)
        return false;

    int cx = (int)(relX * g->invCellX);
    int cz = (int)(relZ * g->invCellZ);
    if (cx >= g->nx || cz >= g->nz)
        return false;

    const int cell = cz * g->nx + cx;
    const int *restrict from = &g->cellItems[g->cellStart[cell]];
    const int *restrict to = &g->cellItems[g->cellStart[cell + 1]];

    int bestTri = -1;
    float bestY = -1e30f;
    float bestW0 = 0.0f, bestW1 = 0.0f, bestW2 = 0.0f;
    int bestI0 = 0, bestI1 = 0, bestI2 = 0;
    const float *restrict verts = g->verts;
    const unsigned short *restrict indices = g->indices;
    const float eps = 1e-5f;

    for (const int *it = from; it != to; it++)
    {
        int t = *it;
        int i0, i1, i2;
        if (indices)
        {
            i0 = indices[t * 3 + 0];
            i1 = indices[t * 3 + 1];
            i2 = indices[t * 3 + 2];
        }
        else
        {
            i0 = t * 3 + 0;
            i1 = t * 3 + 1;
            i2 = t * 3 + 2;
        }

        float ax = verts[i0 * 3 + 0], az = verts[i0 * 3 + 2];
        float bx = verts[i1 * 3 + 0], bz = verts[i1 * 3 + 2];
        float cx_v = verts[i2 * 3 + 0], cz_v = verts[i2 * 3 + 2];

        float d = (bz - cz_v) * (ax - cx_v) + (cx_v - bx) * (az - cz_v);
        if (fabsf(d) < 1e-12f)
            continue;

        float dx = lx - cx_v;
        float dz = lz - cz_v;
        float num0 = (bz - cz_v) * dx + (cx_v - bx) * dz;
        float num1 = (cz_v - az) * dx + (ax - cx_v) * dz;
        float num2 = d - num0 - num1;

        if (d > 0.0f)
        {
            float negEpsD = -eps * d;
            if (num0 < negEpsD || num1 < negEpsD || num2 < negEpsD)
                continue;
        }
        else
        {
            float posEpsD = -eps * d; // positive since d < 0
            if (num0 > posEpsD || num1 > posEpsD || num2 > posEpsD)
                continue;
        }

        float invD = 1.0f / d;
        float w0 = num0 * invD;
        float w1 = num1 * invD;
        float w2 = 1.0f - w0 - w1;

        float y = w0 * verts[i0 * 3 + 1] + w1 * verts[i1 * 3 + 1] + w2 * verts[i2 * 3 + 1];
        if (bestTri < 0 || y > bestY)
        {
            bestY = y;
            bestTri = t;
            bestW0 = w0;
            bestW1 = w1;
            bestW2 = w2;
            bestI0 = i0;
            bestI1 = i1;
            bestI2 = i2;
        }
    }
    if (bestTri < 0)
        return false;
    if (outY)
        *outY = bestY;
    if (outNormal)
    {
        if (g->normals)
        {
            const float *restrict norms = g->normals;
            Vector3 n = {
                bestW0 * norms[bestI0 * 3 + 0] + bestW1 * norms[bestI1 * 3 + 0] + bestW2 * norms[bestI2 * 3 + 0],
                bestW0 * norms[bestI0 * 3 + 1] + bestW1 * norms[bestI1 * 3 + 1] + bestW2 * norms[bestI2 * 3 + 1],
                bestW0 * norms[bestI0 * 3 + 2] + bestW1 * norms[bestI1 * 3 + 2] + bestW2 * norms[bestI2 * 3 + 2]};
            float lenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            if (lenSq > 1e-6f)
            {
                float invLen = 1.0f / sqrtf(lenSq);
                *outNormal = (Vector3){n.x * invLen, n.y * invLen, n.z * invLen};
            }
            else
            {
                *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
            }
        }
        else
        {
            Vector3 a, b, c;
            GroundLookup_Tri(g, bestTri, &a, &b, &c);
            Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            if (n.y < 0.0f)
                n = Vector3Negate(n);
            *outNormal = Vector3Normalize(n);
        }
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

    // 1. Load Textures with quality-adaptive Anisotropic filtering to keep mobile bandwidth lean
    Texture2D texSplat = ResourceManager_LoadTexture(splatMapPath);
    Texture2D texGrass = MapLoadMippedTexture(grassTexPath, TEXTURE_WRAP_REPEAT);
    Texture2D texPath = MapLoadMippedTexture(pathTexPath, TEXTURE_WRAP_REPEAT);
    int filterMode = Ground_GetTerrainFilterMode();

    SetTextureFilter(texSplat, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(texSplat, TEXTURE_WRAP_CLAMP);
    if (texGrass.id != 0)
        SetTextureFilter(texGrass, filterMode);
    if (texPath.id != 0)
        SetTextureFilter(texPath, filterMode);

    // 2. Initialize Shader & cache all uniform locations once (re-bind if shader is reloaded)
    groundShader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_splat.vs",
                                              "maps/toolkit/shaders/ground_splat.fs");
    if (groundShader.id != 0 && s_groundShaderId != groundShader.id)
    {
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
        locGroundRockSampler = GetShaderLocation(groundShader, "u_groundRock");
        locGroundRockNormalSampler = GetShaderLocation(groundShader, "u_groundRockNormal");
        locGroundRockEnabled = GetShaderLocation(groundShader, "u_groundRockEnabled");
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
        locSeismicOriginDir = GetShaderLocation(groundShader, "u_seismicOriginDir");
        locSeismicParams = GetShaderLocation(groundShader, "u_seismicParams");
        locSeismicProfile = GetShaderLocation(groundShader, "u_seismicProfile");
        VFXLight_RegisterShader(groundShader);

        s_groundShaderId = groundShader.id;
        s_groundStateShaderId = 0;
        s_groundHabitatDirty = true;
    }

    // 3. Assign Shader and Textures to Material
    ground.model.materials[0].shader = groundShader;
    ground.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texSplat;  // Slot 0
    ground.model.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = texGrass; // Slot 1
    ground.model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = texPath;    // Slot 2
    MapShadow_AttachMaterial(&ground.model.materials[0]);

    // 4. Upload static slot & tiling uniforms inside explicit BeginShaderMode scope for rlvk
    float invTile = (tileSize > 1e-6f) ? (1.0f / tileSize) : 1.0f;
    float tiling[2] = {width * invTile, depth * invTile};
    ground.tiling = (Vector2){tiling[0], tiling[1]};

    BeginShaderMode(groundShader);
    int grassTexSlot = 1;
    int pathTexSlot = 2;
    int grassMaterialSlot = MATERIAL_MAP_ROUGHNESS;
    int soilMaterialSlot = MATERIAL_MAP_EMISSION;
    int cloudSlot = GROUND_CLOUD_TEXTURE_SLOT;
    int reliefSlot = GROUND_RELIEF_TEXTURE_SLOT;
    int debrisSlot = GROUND_DEBRIS_TEXTURE_SLOT;
    int ecoSlot = GROUND_ECOLOGY_TEXTURE_SLOT;
    int ecoDistSlot = GROUND_ECOLOGY_DIST_TEXTURE_SLOT;
    int rockSlot = GROUND_ROCK_TEXTURE_SLOT;
    int rockNormSlot = GROUND_ROCK_NORMAL_SLOT;
    if (locTexGrass >= 0)
        SetShaderValue(groundShader, locTexGrass, &grassTexSlot, SHADER_UNIFORM_INT);
    if (locTexPath >= 0)
        SetShaderValue(groundShader, locTexPath, &pathTexSlot, SHADER_UNIFORM_INT);
    if (locTexGrassMaterial >= 0)
        SetShaderValue(groundShader, locTexGrassMaterial, &grassMaterialSlot, SHADER_UNIFORM_INT);
    if (locTexSoilMaterial >= 0)
        SetShaderValue(groundShader, locTexSoilMaterial, &soilMaterialSlot, SHADER_UNIFORM_INT);
    if (locGroundCloudNoise >= 0)
        SetShaderValue(groundShader, locGroundCloudNoise, &cloudSlot, SHADER_UNIFORM_INT);
    if (locGroundReliefSampler >= 0)
        SetShaderValue(groundShader, locGroundReliefSampler, &reliefSlot, SHADER_UNIFORM_INT);
    if (locGroundDebrisSampler >= 0)
        SetShaderValue(groundShader, locGroundDebrisSampler, &debrisSlot, SHADER_UNIFORM_INT);
    if (locEcologySampler >= 0)
        SetShaderValue(groundShader, locEcologySampler, &ecoSlot, SHADER_UNIFORM_INT);
    if (locEcologyDistanceSampler >= 0)
        SetShaderValue(groundShader, locEcologyDistanceSampler, &ecoDistSlot, SHADER_UNIFORM_INT);
    if (locGroundRockSampler >= 0)
        SetShaderValue(groundShader, locGroundRockSampler, &rockSlot, SHADER_UNIFORM_INT);
    if (locGroundRockNormalSampler >= 0)
        SetShaderValue(groundShader, locGroundRockNormalSampler, &rockNormSlot, SHADER_UNIFORM_INT);
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

static void Ground_BuildChunks(MapGroundSurface *ground, float width, float depth)
{
    ground->chunkCount = 0;
    if (!ground->model.meshes || ground->model.meshCount < 1)
        return;
    const Mesh *src = &ground->model.meshes[0];
    if (!src->vertices || !src->normals || !src->texcoords || !src->indices || src->triangleCount <= 2000)
        return;

    const int G = MAP_GROUND_CHUNK_GRID;
    int triCounts[MAP_GROUND_MAX_CHUNKS] = {0};
    float invW = (width > 1e-4f) ? ((float)G / width) : 1.0f;
    float invD = (depth > 1e-4f) ? ((float)G / depth) : 1.0f;

    for (int t = 0; t < src->triangleCount; t++)
    {
        const unsigned short *ids = &src->indices[t * 3];
        float cx = (src->vertices[ids[0] * 3 + 0] + src->vertices[ids[1] * 3 + 0] + src->vertices[ids[2] * 3 + 0]) * (1.0f / 3.0f);
        float cz = (src->vertices[ids[0] * 3 + 2] + src->vertices[ids[1] * 3 + 2] + src->vertices[ids[2] * 3 + 2]) * (1.0f / 3.0f);
        int gx = (int)(cx * invW);
        int gz = (int)(cz * invD);
        if (gx < 0) gx = 0;
        if (gx >= G) gx = G - 1;
        if (gz < 0) gz = 0;
        if (gz >= G) gz = G - 1;
        triCounts[gz * G + gx]++;
    }

    int *vRemap = (int *)RL_MALLOC((size_t)src->vertexCount * sizeof(int));
    if (!vRemap)
        return;

    for (int c = 0; c < MAP_GROUND_MAX_CHUNKS; c++)
    {
        if (triCounts[c] <= 0)
            continue;
        memset(vRemap, -1, (size_t)src->vertexCount * sizeof(int));

        int usedVerts = 0;
        for (int t = 0; t < src->triangleCount; t++)
        {
            const unsigned short *ids = &src->indices[t * 3];
            float cx = (src->vertices[ids[0] * 3 + 0] + src->vertices[ids[1] * 3 + 0] + src->vertices[ids[2] * 3 + 0]) * (1.0f / 3.0f);
            float cz = (src->vertices[ids[0] * 3 + 2] + src->vertices[ids[1] * 3 + 2] + src->vertices[ids[2] * 3 + 2]) * (1.0f / 3.0f);
            int gx = (int)(cx * invW);
            int gz = (int)(cz * invD);
            if (gx < 0) gx = 0;
            if (gx >= G) gx = G - 1;
            if (gz < 0) gz = 0;
            if (gz >= G) gz = G - 1;
            if (gz * G + gx != c)
                continue;
            for (int k = 0; k < 3; k++)
            {
                if (vRemap[ids[k]] < 0)
                    vRemap[ids[k]] = usedVerts++;
            }
        }

        Mesh cm = {0};
        cm.vertexCount = usedVerts;
        cm.triangleCount = triCounts[c];
        cm.vertices = (float *)MemAlloc((unsigned int)usedVerts * 3u * sizeof(float));
        cm.normals = (float *)MemAlloc((unsigned int)usedVerts * 3u * sizeof(float));
        cm.texcoords = (float *)MemAlloc((unsigned int)usedVerts * 2u * sizeof(float));
        cm.indices = (unsigned short *)MemAlloc((unsigned int)cm.triangleCount * 3u * sizeof(unsigned short));
        if (!cm.vertices || !cm.normals || !cm.texcoords || !cm.indices)
        {
            MemFree(cm.vertices);
            MemFree(cm.normals);
            MemFree(cm.texcoords);
            MemFree(cm.indices);
            continue;
        }

        Vector3 bmin = {1e9f, 1e9f, 1e9f};
        Vector3 bmax = {-1e9f, -1e9f, -1e9f};
        for (int vi = 0; vi < src->vertexCount; vi++)
        {
            int dst = vRemap[vi];
            if (dst < 0)
                continue;
            float vx = src->vertices[vi * 3 + 0];
            float vy = src->vertices[vi * 3 + 1];
            float vz = src->vertices[vi * 3 + 2];
            cm.vertices[dst * 3 + 0] = vx;
            cm.vertices[dst * 3 + 1] = vy;
            cm.vertices[dst * 3 + 2] = vz;
            cm.normals[dst * 3 + 0] = src->normals[vi * 3 + 0];
            cm.normals[dst * 3 + 1] = src->normals[vi * 3 + 1];
            cm.normals[dst * 3 + 2] = src->normals[vi * 3 + 2];
            cm.texcoords[dst * 2 + 0] = src->texcoords[vi * 2 + 0];
            cm.texcoords[dst * 2 + 1] = src->texcoords[vi * 2 + 1];
            if (vx < bmin.x) bmin.x = vx;
            if (vy < bmin.y) bmin.y = vy;
            if (vz < bmin.z) bmin.z = vz;
            if (vx > bmax.x) bmax.x = vx;
            if (vy > bmax.y) bmax.y = vy;
            if (vz > bmax.z) bmax.z = vz;
        }

        int iCursor = 0;
        for (int t = 0; t < src->triangleCount; t++)
        {
            const unsigned short *ids = &src->indices[t * 3];
            float cx = (src->vertices[ids[0] * 3 + 0] + src->vertices[ids[1] * 3 + 0] + src->vertices[ids[2] * 3 + 0]) * (1.0f / 3.0f);
            float cz = (src->vertices[ids[0] * 3 + 2] + src->vertices[ids[1] * 3 + 2] + src->vertices[ids[2] * 3 + 2]) * (1.0f / 3.0f);
            int gx = (int)(cx * invW);
            int gz = (int)(cz * invD);
            if (gx < 0) gx = 0;
            if (gx >= G) gx = G - 1;
            if (gz < 0) gz = 0;
            if (gz >= G) gz = G - 1;
            if (gz * G + gx != c)
                continue;
            cm.indices[iCursor++] = (unsigned short)vRemap[ids[0]];
            cm.indices[iCursor++] = (unsigned short)vRemap[ids[1]];
            cm.indices[iCursor++] = (unsigned short)vRemap[ids[2]];
        }

        UploadMesh(&cm, false);
        Vector3 center = {(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f};
        Vector3 half = {(bmax.x - bmin.x) * 0.5f, (bmax.y - bmin.y) * 0.5f, (bmax.z - bmin.z) * 0.5f};
        float radius = sqrtf(half.x * half.x + half.y * half.y + half.z * half.z) + 0.5f;
        ground->chunks[ground->chunkCount++] = (MapGroundChunk){
            .mesh = cm,
            .center = center,
            .radius = radius,
        };
    }

    RL_FREE(vRemap);
}

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
    Ground_BuildChunks(&ground, width, depth);
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

    if (mesh.normals != NULL && heightmapImg.width > 1 && heightmapImg.height > 1 &&
        mesh.triangleCount > 0 && width > 1e-4f && depth > 1e-4f)
    {
        int mapX = heightmapImg.width;
        int mapZ = heightmapImg.height;
        Vector3 *gridNormals = (Vector3 *)MemAlloc((unsigned int)(mapX * mapZ) * sizeof(Vector3));
        if (gridNormals != NULL)
        {
            memset(gridNormals, 0, (size_t)(mapX * mapZ) * sizeof(Vector3));
            const float scaleX = (float)(mapX - 1) / width;
            const float scaleZ = (float)(mapZ - 1) / depth;
            const float *restrict verts = mesh.vertices;
            float *restrict norms = mesh.normals;

            for (int t = 0; t < mesh.triangleCount; t++)
            {
                int b = t * 9;
                float ax = verts[b + 0], ay = verts[b + 1], az = verts[b + 2];
                float bx = verts[b + 3], by = verts[b + 4], bz = verts[b + 5];
                float cx = verts[b + 6], cy = verts[b + 7], cz = verts[b + 8];
                float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
                float e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
                float fnx = e1y * e2z - e1z * e2y;
                float fny = e1z * e2x - e1x * e2z;
                float fnz = e1x * e2y - e1y * e2x;
                if (fny < 0.0f)
                {
                    fnx = -fnx;
                    fny = -fny;
                    fnz = -fnz;
                }

                for (int k = 0; k < 3; k++)
                {
                    float vx = verts[b + k * 3 + 0];
                    float vz = verts[b + k * 3 + 2];
                    int ix = (int)(vx * scaleX + 0.5f);
                    int iz = (int)(vz * scaleZ + 0.5f);
                    if (ix >= 0 && ix < mapX && iz >= 0 && iz < mapZ)
                    {
                        int gIdx = iz * mapX + ix;
                        gridNormals[gIdx].x += fnx;
                        gridNormals[gIdx].y += fny;
                        gridNormals[gIdx].z += fnz;
                    }
                }
            }

            int totalGrid = mapX * mapZ;
            for (int gIdx = 0; gIdx < totalGrid; gIdx++)
            {
                Vector3 n = gridNormals[gIdx];
                float lenSq = n.x * n.x + n.y * n.y + n.z * n.z;
                if (lenSq > 1e-6f)
                {
                    float invLen = 1.0f / sqrtf(lenSq);
                    gridNormals[gIdx] = (Vector3){n.x * invLen, n.y * invLen, n.z * invLen};
                }
                else
                {
                    gridNormals[gIdx] = (Vector3){0.0f, 1.0f, 0.0f};
                }
            }

            for (int i = 0; i < mesh.vertexCount; i++)
            {
                float vx = verts[i * 3 + 0];
                float vz = verts[i * 3 + 2];
                int ix = (int)(vx * scaleX + 0.5f);
                int iz = (int)(vz * scaleZ + 0.5f);
                if (ix >= 0 && ix < mapX && iz >= 0 && iz < mapZ)
                {
                    Vector3 n = gridNormals[iz * mapX + ix];
                    norms[i * 3 + 0] = n.x;
                    norms[i * 3 + 1] = n.y;
                    norms[i * 3 + 2] = n.z;
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
    if (!ground || !ground->ready || ground->model.meshCount < 1)
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
    if (!ground || !ground->ready || ground->model.meshCount < 1)
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
    if (!ground || !ground->ready)
        return;

    BeginShaderMode(groundShader);
    bool firstShaderBind = (s_groundStateShaderId != groundShader.id);
    if (firstShaderBind)
    {
        s_groundStateShaderId = groundShader.id;
        s_groundCachedReliefEnabled = -1;
        s_groundCachedDebrisEnabled = -1;
        s_groundCachedRockEnabled = -1;
        s_groundCachedEcologyEnabled = -1;
        s_groundCachedThinBoost = -1.0f;
        s_groundHabitatDirty = true;
    }

    Vector4 islandShape = {ground->boundary.cornerRadius, ground->boundary.mistWidth, ground->boundary.groundInset, 0};
    if (firstShaderBind ||
        ground->boundary.rect.x != s_groundCachedIslandRect.x ||
        ground->boundary.rect.y != s_groundCachedIslandRect.y ||
        ground->boundary.rect.z != s_groundCachedIslandRect.z ||
        ground->boundary.rect.w != s_groundCachedIslandRect.w)
    {
        if (locIslandRect >= 0)
            SetShaderValue(groundShader, locIslandRect, &ground->boundary.rect, SHADER_UNIFORM_VEC4);
        s_groundCachedIslandRect = ground->boundary.rect;
    }
    if (firstShaderBind ||
        islandShape.x != s_groundCachedIslandShape.x ||
        islandShape.y != s_groundCachedIslandShape.y ||
        islandShape.z != s_groundCachedIslandShape.z)
    {
        if (locIslandShape >= 0)
            SetShaderValue(groundShader, locIslandShape, &islandShape, SHADER_UNIFORM_VEC4);
        s_groundCachedIslandShape = islandShape;
    }

    unsigned int defaultTexId = rlGetTextureIdDefault();
    EnvCloudShadowFrame cloud = Environment_GetCloudShadowFrame();
    rlActiveTextureSlot(GROUND_CLOUD_TEXTURE_SLOT);
    rlEnableTexture(cloud.noiseTexture.id ? cloud.noiseTexture.id : defaultTexId);
    rlActiveTextureSlot(GROUND_RELIEF_TEXTURE_SLOT);
    rlEnableTexture(ground->reliefTexture.id ? ground->reliefTexture.id : defaultTexId);
    rlActiveTextureSlot(GROUND_DEBRIS_TEXTURE_SLOT);
    rlEnableTexture(ground->debrisTexture.id ? ground->debrisTexture.id : defaultTexId);

    int ecologyEnabled = (ground->ecology && ground->ecology->ready) ? 1 : 0;
    rlActiveTextureSlot(GROUND_ECOLOGY_TEXTURE_SLOT);
    rlEnableTexture((ecologyEnabled && ground->ecology->habitatTexture.id)
                        ? ground->ecology->habitatTexture.id
                        : defaultTexId);
    rlActiveTextureSlot(GROUND_ECOLOGY_DIST_TEXTURE_SLOT);
    rlEnableTexture((ecologyEnabled && ground->ecology->distanceTexture.id)
                        ? ground->ecology->distanceTexture.id
                        : defaultTexId);

    int rockEnabled = (ground->rockTexture.id != 0) ? 1 : 0;
    rlActiveTextureSlot(GROUND_ROCK_TEXTURE_SLOT);
    rlEnableTexture(rockEnabled ? ground->rockTexture.id : defaultTexId);
    rlActiveTextureSlot(GROUND_ROCK_NORMAL_SLOT);
    rlEnableTexture((rockEnabled && ground->rockNormalTexture.id) ? ground->rockNormalTexture.id : defaultTexId);
    rlActiveTextureSlot(0);

    int reliefEnabled = (ground->reliefTexture.id != 0) ? 1 : 0;
    if (s_groundCachedReliefEnabled != reliefEnabled)
    {
        if (locGroundReliefEnabled >= 0)
            SetShaderValue(groundShader, locGroundReliefEnabled, &reliefEnabled, SHADER_UNIFORM_INT);
        s_groundCachedReliefEnabled = reliefEnabled;
    }
    int debrisEnabled = (ground->debrisTexture.id != 0) ? 1 : 0;
    if (s_groundCachedDebrisEnabled != debrisEnabled)
    {
        if (locGroundDebrisEnabled >= 0)
            SetShaderValue(groundShader, locGroundDebrisEnabled, &debrisEnabled, SHADER_UNIFORM_INT);
        s_groundCachedDebrisEnabled = debrisEnabled;
    }
    if (s_groundCachedRockEnabled != rockEnabled)
    {
        if (locGroundRockEnabled >= 0)
            SetShaderValue(groundShader, locGroundRockEnabled, &rockEnabled, SHADER_UNIFORM_INT);
        s_groundCachedRockEnabled = rockEnabled;
    }
    if (firstShaderBind ||
        ground->tiling.x != s_groundCachedTiling.x ||
        ground->tiling.y != s_groundCachedTiling.y)
    {
        if (locGroundTiling >= 0)
            SetShaderValue(groundShader, locGroundTiling, &ground->tiling, SHADER_UNIFORM_VEC2);
        s_groundCachedTiling = ground->tiling;
    }
    if (locGroundCloudUV >= 0)
        SetShaderValue(groundShader, locGroundCloudUV, &cloud.uvTransform, SHADER_UNIFORM_VEC4);
    if (locGroundCloudShape >= 0)
        SetShaderValue(groundShader, locGroundCloudShape, &cloud.shape, SHADER_UNIFORM_VEC4);
    if (locGroundCloudProjection >= 0)
        SetShaderValue(groundShader, locGroundCloudProjection, &cloud.projection, SHADER_UNIFORM_VEC2);

    if (s_groundCachedEcologyEnabled != ecologyEnabled)
    {
        if (locEcologyEnabled >= 0)
            SetShaderValue(groundShader, locEcologyEnabled, &ecologyEnabled, SHADER_UNIFORM_INT);
        s_groundCachedEcologyEnabled = ecologyEnabled;
    }
    if (ecologyEnabled && locEcologyRect >= 0 &&
        (firstShaderBind ||
         ground->ecology->rect.x != s_groundCachedEcologyRect.x ||
         ground->ecology->rect.y != s_groundCachedEcologyRect.y ||
         ground->ecology->rect.z != s_groundCachedEcologyRect.z ||
         ground->ecology->rect.w != s_groundCachedEcologyRect.w))
    {
        SetShaderValue(groundShader, locEcologyRect, &ground->ecology->rect, SHADER_UNIFORM_VEC4);
        s_groundCachedEcologyRect = ground->ecology->rect;
    }

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
        float timeVal = fmodf((float)GetTime(), 3600.0f);
        SetShaderValue(groundShader, locTime, &timeVal, SHADER_UNIFORM_FLOAT);
    }

    if (s_groundHabitatDirty)
    {
        if (locPathSegCount >= 0)
            SetShaderValue(groundShader, locPathSegCount, &s_groundPathSegCount, SHADER_UNIFORM_INT);
        if (locPathSegs >= 0 && s_groundPathSegCount > 0)
            SetShaderValueV(groundShader, locPathSegs, s_groundPathSegs, SHADER_UNIFORM_VEC4, s_groundPathSegCount);
        if (locLakeParams >= 0)
            SetShaderValue(groundShader, locLakeParams, &s_groundLakeParams, SHADER_UNIFORM_VEC4);
        s_groundHabitatDirty = false;
    }

    MapShadow_UpdateShader(groundShader);

    Vector4 seismicOriginDir = {0}, seismicParams = {0}, seismicProfile = {0};
    MapManager_GetGroundWaveParams(&seismicOriginDir, &seismicParams, &seismicProfile);
    if (locSeismicOriginDir >= 0)
        SetShaderValue(groundShader, locSeismicOriginDir, &seismicOriginDir, SHADER_UNIFORM_VEC4);
    if (locSeismicParams >= 0)
        SetShaderValue(groundShader, locSeismicParams, &seismicParams, SHADER_UNIFORM_VEC4);
    if (locSeismicProfile >= 0)
        SetShaderValue(groundShader, locSeismicProfile, &seismicProfile, SHADER_UNIFORM_VEC4);

    if (locThinShadowBoost >= 0)
    {
        GfxQuality q = GfxQuality_Get();
        float thinShadowBoost = (q >= GFX_HIGH) ? 0.04f : ((q >= GFX_MED) ? 0.02f : 0.0f);
        if (thinShadowBoost != s_groundCachedThinBoost)
        {
            SetShaderValue(groundShader, locThinShadowBoost, &thinShadowBoost, SHADER_UNIFORM_FLOAT);
            s_groundCachedThinBoost = thinShadowBoost;
        }
    }

    Vector3 pos = {
        worldCenter.x + ground->drawOffset.x,
        worldCenter.y + ground->drawOffset.y,
        worldCenter.z + ground->drawOffset.z,
    };
    if (locGroundOffset >= 0)
        SetShaderValue(groundShader, locGroundOffset, &pos, SHADER_UNIFORM_VEC3);
    if (ground->chunkCount > 0)
    {
        MapPropViewFrustum frustum;
        bool cull = MapProp_BuildViewFrustum(&frustum);
        Matrix transform = MatrixTranslate(pos.x, pos.y, pos.z);
        for (int c = 0; c < ground->chunkCount; c++)
        {
            const MapGroundChunk *chunk = &ground->chunks[c];
            if (cull)
            {
                Vector3 wc = Vector3Add(chunk->center, pos);
                if (MapProp_FrustumCullsSphere(&frustum, wc, chunk->radius))
                    continue;
            }
            DrawMesh(chunk->mesh, ground->model.materials[0], transform);
        }
    }
    else
    {
        DrawModel(ground->model, pos, 1.0f, WHITE);
    }

    rlActiveTextureSlot(GROUND_ROCK_NORMAL_SLOT);
    rlDisableTexture();
    rlActiveTextureSlot(GROUND_ROCK_TEXTURE_SLOT);
    rlDisableTexture();
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
    if (!pathSegments || segmentCount <= 0)
    {
        s_groundPathSegCount = 0;
        s_groundLakeParams = lakeParams;
        s_groundHabitatDirty = true;
        return;
    }
    if (segmentCount > MAX_GROUND_PATH_SEGS)
        segmentCount = MAX_GROUND_PATH_SEGS;
    s_groundPathSegCount = segmentCount;
    for (int i = 0; i < segmentCount; i++)
    {
        s_groundPathSegs[i] = pathSegments[i];
    }
    s_groundLakeParams = lakeParams;
    s_groundHabitatDirty = true;
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
    int filterMode = Ground_GetTerrainFilterMode();
    Texture2D grassMaterial = MapLoadMippedTexture(grassMaterialPath, TEXTURE_WRAP_REPEAT);
    Texture2D soilMaterial = MapLoadMippedTexture(soilMaterialPath, TEXTURE_WRAP_REPEAT);
    if (grassMaterial.id != 0)
        SetTextureFilter(grassMaterial, filterMode);
    if (soilMaterial.id != 0)
        SetTextureFilter(soilMaterial, filterMode);

    ground->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = grassMaterial;
    ground->model.materials[0].maps[MATERIAL_MAP_EMISSION].texture = soilMaterial;
}

void MapProp_SetGroundRockMaps(MapGroundSurface *ground,
                               const char *diffusePath,
                               const char *normalPath)
{
    if (!ground || !ground->ready)
        return;
    int filterMode = Ground_GetTerrainFilterMode();
    ground->rockTexture = diffusePath ? MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT) : (Texture2D){0};
    ground->rockNormalTexture = normalPath ? MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT) : (Texture2D){0};
    if (ground->rockTexture.id != 0)
        SetTextureFilter(ground->rockTexture, filterMode);
    if (ground->rockNormalTexture.id != 0)
        SetTextureFilter(ground->rockNormalTexture, filterMode);
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
    ground->rockTexture = (Texture2D){0};
    ground->rockNormalTexture = (Texture2D){0};
    for (int c = 0; c < ground->chunkCount; c++)
        UnloadMesh(ground->chunks[c].mesh);
    ground->chunkCount = 0;
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
    if (ground->reliefTexture.id != 0)
        SetTextureFilter(ground->reliefTexture, Ground_GetTerrainFilterMode());
}

void MapProp_SetGroundDebrisMap(MapGroundSurface *ground, const char *debrisMapPath)
{
    if (!ground || !ground->ready)
        return;
    ground->debrisTexture = debrisMapPath ? MapLoadMippedTexture(debrisMapPath, TEXTURE_WRAP_REPEAT) : (Texture2D){0};
}
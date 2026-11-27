#ifndef MAP_TOOLKIT_MAP_PROPS_ORE_INL
#define MAP_TOOLKIT_MAP_PROPS_ORE_INL

// 3D Procedural Mineral Ore & Crystal Cluster System (included in map_props.c)
// Uses canonical Raylib Mesh buffers (vertices, texcoords, normals, tangents, colors)
// for full Vulkan (rlvk) and OpenGL 3.3 compatibility (LANDMINES.md §375).

static int s_oreLocTime = -1;
static int s_oreLocType = -1;
static int s_oreLocLightDir = -1;
static int s_oreLocLightCol = -1;
static int s_oreLocAmbientCol = -1;
static int s_oreLocViewPos = -1;
static int s_oreLocVeinTex = -1;
static int s_oreLocCloudNoise = -1;
static int s_oreLocCloudWorldFromShader = -1;
static unsigned int s_oreShaderId = 0;

static void Ore_EnsureShaderLocs(Shader shader)
{
    if (shader.id == 0 || s_oreShaderId == shader.id)
        return;

    shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(shader, "vertexNormal");
    shader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocationAttrib(shader, "vertexTangent");
    shader.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(shader, "vertexColor");

    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(shader, "texture2");

    s_oreLocTime = GetShaderLocation(shader, "u_time");
    s_oreLocType = GetShaderLocation(shader, "u_oreType");
    s_oreLocLightDir = GetShaderLocation(shader, "u_lightDir");
    s_oreLocLightCol = GetShaderLocation(shader, "u_lightColor");
    s_oreLocAmbientCol = GetShaderLocation(shader, "u_ambientColor");
    s_oreLocViewPos = GetShaderLocation(shader, "u_viewPos");
    s_oreLocVeinTex = GetShaderLocation(shader, "u_oreVeinTex");
    s_oreLocCloudNoise = GetShaderLocation(shader, "u_cloudNoise");
    s_oreLocCloudWorldFromShader = GetShaderLocation(shader, "u_cloudWorldFromShader");

    MapShadow_ConfigureShader(shader);
    VFXLight_RegisterShader(shader);
    s_oreShaderId = shader.id;
}

typedef struct
{
    float *verts;
    float *uvs;
    float *norms;
    float *tangs;
    unsigned char *cols;
    int vCount;
    int vCap;
} OreMeshBuilder;

static void Ore_PushTri(OreMeshBuilder *b, Vector3 a, Vector3 bPt, Vector3 c,
                        unsigned char purity, unsigned char isCrystal)
{
    if (b->vCount + 3 > b->vCap)
        return;

    Vector3 e1 = Vector3Subtract(bPt, a);
    Vector3 e2 = Vector3Subtract(c, a);
    Vector3 n = Vector3CrossProduct(e1, e2);
    float nLen = Vector3Length(n);
    n = (nLen > 1e-6f) ? Vector3Scale(n, 1.0f / nLen) : (Vector3){0.0f, 1.0f, 0.0f};

    Vector3 ref = (fabsf(n.y) < 0.95f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 t = Vector3Normalize(Vector3CrossProduct(ref, n));

    Vector3 pts[3] = {a, bPt, c};
    for (int k = 0; k < 3; k++)
    {
        int vi = b->vCount++;
        b->verts[vi * 3 + 0] = pts[k].x;
        b->verts[vi * 3 + 1] = pts[k].y;
        b->verts[vi * 3 + 2] = pts[k].z;
        b->uvs[vi * 2 + 0] = pts[k].x * 0.5f + 0.5f;
        b->uvs[vi * 2 + 1] = pts[k].z * 0.5f + 0.5f;
        b->norms[vi * 3 + 0] = n.x;
        b->norms[vi * 3 + 1] = n.y;
        b->norms[vi * 3 + 2] = n.z;
        b->tangs[vi * 4 + 0] = t.x;
        b->tangs[vi * 4 + 1] = t.y;
        b->tangs[vi * 4 + 2] = t.z;
        b->tangs[vi * 4 + 3] = 1.0f;
        b->cols[vi * 4 + 0] = purity;
        b->cols[vi * 4 + 1] = purity;
        b->cols[vi * 4 + 2] = purity;
        b->cols[vi * 4 + 3] = isCrystal;
    }
}

static Vector3 Ore_RotateVec(Vector3 v, float yaw, float pitch, float roll)
{
    Matrix m = MatrixRotateXYZ((Vector3){pitch, yaw, roll});
    return Vector3Transform(v, m);
}

static void Ore_AppendCrystalPrism(OreMeshBuilder *b, Vector3 baseOffset,
                                   float radius, float height, int sides,
                                   float yaw, float pitch, float roll,
                                   unsigned char purity)
{
    if (sides < 4) sides = 6;
    if (sides > 8) sides = 8;

    Vector3 botRing[8];
    Vector3 topRing[8];
    float shoulderY = height * 0.72f;
    float topRadius = radius * 0.78f;

    for (int i = 0; i < sides; i++)
    {
        float ang = (float)i * (2.0f * PI / (float)sides);
        float ca = cosf(ang);
        float sa = sinf(ang);
        Vector3 vb = Ore_RotateVec((Vector3){ca * radius, -0.08f, sa * radius}, yaw, pitch, roll);
        Vector3 vt = Ore_RotateVec((Vector3){ca * topRadius, shoulderY, sa * topRadius}, yaw, pitch, roll);
        botRing[i] = Vector3Add(vb, baseOffset);
        topRing[i] = Vector3Add(vt, baseOffset);
    }

    Vector3 apex = Vector3Add(Ore_RotateVec((Vector3){0.0f, height, 0.0f}, yaw, pitch, roll), baseOffset);

    for (int i = 0; i < sides; i++)
    {
        int next = (i + 1) % sides;
        Ore_PushTri(b, botRing[i], topRing[i], topRing[next], purity, 255);
        Ore_PushTri(b, botRing[i], topRing[next], botRing[next], purity, 255);
        Ore_PushTri(b, topRing[i], apex, topRing[next], purity, 255);
    }
}

static Mesh Ore_BuildProceduralClusterMesh(MapOreType type)
{
    Mesh mesh = {0};
    unsigned int rng = 0x91A4C2u + (unsigned int)type * 0x4F1BBCDCu;

    // 1. Basal host-rock collar (icosphere subdivision 1 = 80 tris = 240 verts)
    Mesh baseRock = Nature_GenMeshIcoSphere(0.76f, 1, false);
    int rockVerts = baseRock.vertexCount;
    for (int i = 0; i < rockVerts; i++)
    {
        float x = baseRock.vertices[i * 3 + 0];
        float y = baseRock.vertices[i * 3 + 1];
        float z = baseRock.vertices[i * 3 + 2];
        float ang = atan2f(z, x);
        float warp = 1.0f + 0.22f * cosf(ang * 3.0f + (float)type) + 0.14f * sinf(y * 4.0f);
        baseRock.vertices[i * 3 + 0] = x * warp * 1.15f;
        baseRock.vertices[i * 3 + 1] = y * 0.46f + 0.06f;
        baseRock.vertices[i * 3 + 2] = z * warp * 1.05f;
    }

    // 2. Protruding crystal spires (up to 11 prisms * 8 sides * 3 tris * 3 verts = 792 verts)
    int crystalCount = (type == MAP_ORE_CELESTIAL_METEORITE) ? 7 : 10;
    int maxCrystalVerts = crystalCount * 8 * 3 * 3;
    int totalCap = rockVerts + maxCrystalVerts;

    OreMeshBuilder b = {0};
    b.vCap = totalCap;
    b.verts = (float *)RL_MALLOC((size_t)totalCap * 3u * sizeof(float));
    b.uvs = (float *)RL_MALLOC((size_t)totalCap * 2u * sizeof(float));
    b.norms = (float *)RL_MALLOC((size_t)totalCap * 3u * sizeof(float));
    b.tangs = (float *)RL_MALLOC((size_t)totalCap * 4u * sizeof(float));
    b.cols = (unsigned char *)RL_MALLOC((size_t)totalCap * 4u * sizeof(unsigned char));

    if (!b.verts || !b.uvs || !b.norms || !b.tangs || !b.cols)
    {
        UnloadMesh(baseRock);
        if (b.verts) RL_FREE(b.verts);
        if (b.uvs) RL_FREE(b.uvs);
        if (b.norms) RL_FREE(b.norms);
        if (b.tangs) RL_FREE(b.tangs);
        if (b.cols) RL_FREE(b.cols);
        return mesh;
    }

    for (int t = 0; t < baseRock.triangleCount; t++)
    {
        int i0 = t * 3 + 0;
        int i1 = t * 3 + 1;
        int i2 = t * 3 + 2;
        Vector3 v0 = {baseRock.vertices[i0 * 3 + 0], baseRock.vertices[i0 * 3 + 1], baseRock.vertices[i0 * 3 + 2]};
        Vector3 v1 = {baseRock.vertices[i1 * 3 + 0], baseRock.vertices[i1 * 3 + 1], baseRock.vertices[i1 * 3 + 2]};
        Vector3 v2 = {baseRock.vertices[i2 * 3 + 0], baseRock.vertices[i2 * 3 + 1], baseRock.vertices[i2 * 3 + 2]};
        Ore_PushTri(&b, v0, v1, v2, 160, 0);
    }
    UnloadMesh(baseRock);

    int sides = (type == MAP_ORE_COPPER_PYRITE) ? 4 : ((type == MAP_ORE_SILVER_LODE) ? 5 : 6);
    for (int c = 0; c < crystalCount; c++)
    {
        bool isHero = (c == 0);
        float ang = (float)c * (2.0f * PI / (float)(crystalCount > 1 ? crystalCount - 1 : 1)) + Nature_Random01(&rng) * 0.45f;
        float dist = isHero ? 0.05f : (0.18f + 0.36f * Nature_Random01(&rng));
        Vector3 off = {cosf(ang) * dist, 0.06f, sinf(ang) * dist};

        float rad = isHero ? 0.28f : (0.13f + 0.12f * Nature_Random01(&rng));
        float ht = isHero ? (type == MAP_ORE_CELESTIAL_METEORITE ? 1.48f : 1.18f) : (0.52f + 0.56f * Nature_Random01(&rng));
        float tilt = isHero ? (0.08f * (Nature_Random01(&rng) - 0.5f)) : (0.28f + 0.42f * Nature_Random01(&rng));
        float pitch = cosf(ang) * tilt;
        float roll = -sinf(ang) * tilt;
        float yaw = Nature_Random01(&rng) * 2.0f * PI;
        unsigned char purity = (unsigned char)(180 + (int)(Nature_Random01(&rng) * 75.0f));

        Ore_AppendCrystalPrism(&b, off, rad, ht, sides, yaw, pitch, roll, purity);
    }

    mesh.vertexCount = b.vCount;
    mesh.triangleCount = b.vCount / 3;
    mesh.vertices = b.verts;
    mesh.texcoords = b.uvs;
    mesh.normals = b.norms;
    mesh.tangents = b.tangs;
    mesh.colors = b.cols;
    UploadMesh(&mesh, false);
    return mesh;
}

MapOreSystem MapProp_CreateOreSystem(const MapOrePlacement *items, int count,
                                     const MapGroundSurface *ground, Vector3 groundCenter)
{
    MapOreSystem sys = {0};
    if (!items || count <= 0)
        return sys;

    sys.shader = ResourceManager_LoadShader("maps/toolkit/shaders/ore_crystal.vs",
                                            "maps/toolkit/shaders/ore_crystal.fs");
    if (sys.shader.id == 0)
        return sys;
    Ore_EnsureShaderLocs(sys.shader);

    sys.rockTex = MapLoadMippedTexture("assets/textures/rocks/slate_diffuse.png", TEXTURE_WRAP_REPEAT);
    sys.rockNormalTex = MapLoadMippedTexture("assets/textures/rocks/slate_normal.png", TEXTURE_WRAP_REPEAT);
    sys.veinTex = FileExists("assets/maps/phu_quoc/textures/mineral_ore_veins.png")
                      ? MapLoadMippedTexture("assets/maps/phu_quoc/textures/mineral_ore_veins.png", TEXTURE_WRAP_REPEAT)
                      : MapLoadMippedTexture("assets/textures/cloud_noise.png", TEXTURE_WRAP_REPEAT);

    Texture2D cloudNoise = Environment_GetCloudShadowFrame().noiseTexture;

    for (int t = 0; t < MAP_ORE_TYPE_COUNT; t++)
    {
        Mesh m = Ore_BuildProceduralClusterMesh((MapOreType)t);
        MapProp_MeshBounds(&m, &sys.boundsCenter[t], &sys.boundsRadius[t]);
        sys.models[t] = LoadModelFromMesh(m);
        if (sys.models[t].materialCount > 0)
        {
            sys.models[t].materials[0].shader = sys.shader;
            if (sys.rockTex.id != 0)
                sys.models[t].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sys.rockTex;
            if (sys.rockNormalTex.id != 0)
                sys.models[t].materials[0].maps[MATERIAL_MAP_NORMAL].texture = sys.rockNormalTex;
            if (cloudNoise.id != 0)
                sys.models[t].materials[0].maps[MATERIAL_MAP_METALNESS].texture = cloudNoise;
            MapShadow_AttachMaterial(&sys.models[t].materials[0]);
        }
    }

    sys.items = (MapOrePlacement *)RL_MALLOC((size_t)count * sizeof(MapOrePlacement));
    if (!sys.items)
    {
        MapProp_UnloadOreSystem(&sys);
        return (MapOreSystem){0};
    }

    sys.capacity = count;
    sys.count = count;
    for (int i = 0; i < count; i++)
    {
        sys.items[i] = items[i];
        if (ground && ground->ready)
        {
            Vector3 gp = items[i].position;
            Vector3 gn = {0.0f, 1.0f, 0.0f};
            if (MapProp_SampleGroundSurface(ground, groundCenter, items[i].position.x, items[i].position.z, &gp, &gn))
            {
                sys.items[i].position.y = gp.y - items[i].heightScale * 0.10f;
            }
        }
    }

    sys.ready = true;
    return sys;
}

void MapProp_DrawOreSystem(const MapOreSystem *sys, float time)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0 || sys->shader.id == 0)
        return;

    Ore_EnsureShaderLocs(sys->shader);
    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    Vector3 camPos = camera.position;

    BeginShaderMode(sys->shader);
    Environment_BindCloudShadowShader(sys->shader);

    Matrix shaderFromWorld = rlGetMatrixTransform();
    if (s_oreLocCloudWorldFromShader >= 0)
        SetShaderValueMatrix(sys->shader, s_oreLocCloudWorldFromShader, MatrixInvert(shaderFromWorld));

    if (s_oreLocTime >= 0)
        SetShaderValue(sys->shader, s_oreLocTime, &time, SHADER_UNIFORM_FLOAT);

    if (s_oreLocLightDir >= 0)
    {
        Vector3 lDir = Vector3Negate(Environment_GetSunDirection());
        Vector3 vLightDir = {
            shaderFromWorld.m0 * lDir.x + shaderFromWorld.m4 * lDir.y + shaderFromWorld.m8 * lDir.z,
            shaderFromWorld.m1 * lDir.x + shaderFromWorld.m5 * lDir.y + shaderFromWorld.m9 * lDir.z,
            shaderFromWorld.m2 * lDir.x + shaderFromWorld.m6 * lDir.y + shaderFromWorld.m10 * lDir.z};
        vLightDir = Vector3Normalize(vLightDir);
        SetShaderValue(sys->shader, s_oreLocLightDir, &vLightDir, SHADER_UNIFORM_VEC3);
    }
    if (s_oreLocLightCol >= 0)
    {
        Vector4 c = ColorNormalize(Environment_GetSunColor());
        Vector3 rgb = Vector3Scale((Vector3){c.x, c.y, c.z}, Environment_GetSunIntensity());
        SetShaderValue(sys->shader, s_oreLocLightCol, &rgb, SHADER_UNIFORM_VEC3);
    }
    if (s_oreLocAmbientCol >= 0)
    {
        Vector4 c = ColorNormalize(Environment_GetAmbientColor());
        Vector3 rgb = {c.x, c.y, c.z};
        SetShaderValue(sys->shader, s_oreLocAmbientCol, &rgb, SHADER_UNIFORM_VEC3);
    }
    if (s_oreLocViewPos >= 0)
    {
        Vector3 vPos = Vector3Transform(camPos, shaderFromWorld);
        SetShaderValue(sys->shader, s_oreLocViewPos, &vPos, SHADER_UNIFORM_VEC3);
    }
    if (s_oreLocVeinTex >= 0 && sys->veinTex.id != 0)
    {
        int slot = 4;
        rlActiveTextureSlot(slot);
        rlEnableTexture(sys->veinTex.id);
        SetShaderValue(sys->shader, s_oreLocVeinTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }
    MapShadow_UpdateShader(sys->shader);

    for (int t = 0; t < MAP_ORE_TYPE_COUNT; t++)
    {
        if (s_oreLocType >= 0)
            SetShaderValue(sys->shader, s_oreLocType, &t, SHADER_UNIFORM_INT);

        for (int i = 0; i < sys->count; i++)
        {
            const MapOrePlacement *p = &sys->items[i];
            if ((int)p->type != t)
                continue;

            float dx = p->position.x - camPos.x;
            float dz = p->position.z - camPos.z;
            if (dx * dx + dz * dz > 135.0f * 135.0f)
                continue;

            float maxS = fmaxf(p->radiusScale, p->heightScale);
            if (cull && MapProp_FrustumCullsSphere(&frustum, p->position, sys->boundsRadius[t] * maxS))
                continue;

            Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
            DrawModelEx(sys->models[t], p->position, rotAxis, p->rotationDeg, scale, WHITE);
        }
    }

    rlDrawRenderBatchActive();
    if (s_oreLocVeinTex >= 0 && sys->veinTex.id != 0)
    {
        rlActiveTextureSlot(4);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    EndShaderMode();
}

void MapProp_DrawOreShadowCasters(MapOreSystem *sys, Shader depthShader)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0)
        return;

    Shader prevShaders[MAP_ORE_TYPE_COUNT] = {0};
    for (int t = 0; t < MAP_ORE_TYPE_COUNT; t++)
    {
        if (sys->models[t].materialCount > 0)
        {
            prevShaders[t] = sys->models[t].materials[0].shader;
            sys->models[t].materials[0].shader = depthShader;
        }
    }

    Vector3 focus = {0};
    float halfExtent = 0.0f;
    EnvShadow_GetActiveCaptureBounds(&focus, &halfExtent);
    float limitBase = (halfExtent > 1.0f) ? halfExtent : 28.0f;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};

    for (int i = 0; i < sys->count; i++)
    {
        const MapOrePlacement *p = &sys->items[i];
        int t = (int)p->type;
        if (t < 0 || t >= MAP_ORE_TYPE_COUNT)
            continue;
        float dx = p->position.x - focus.x;
        float dz = p->position.z - focus.z;
        float limit = limitBase + p->radiusScale * 2.0f;
        if (dx * dx + dz * dz > limit * limit)
            continue;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(sys->models[t], p->position, rotAxis, p->rotationDeg, scale, WHITE);
    }
    rlDrawRenderBatchActive();

    for (int t = 0; t < MAP_ORE_TYPE_COUNT; t++)
    {
        if (sys->models[t].materialCount > 0)
            sys->models[t].materials[0].shader = prevShaders[t];
    }
}

void MapProp_UnloadOreSystem(MapOreSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    for (int t = 0; t < MAP_ORE_TYPE_COUNT; t++)
    {
        if (sys->models[t].materialCount > 0)
            sys->models[t].materials[0].maps[MATERIAL_MAP_METALNESS].texture = (Texture2D){0};
        UnloadModel(sys->models[t]);
    }
    if (sys->items)
    {
        RL_FREE(sys->items);
        sys->items = NULL;
    }
    *sys = (MapOreSystem){0};
}

#endif // MAP_TOOLKIT_MAP_PROPS_ORE_INL

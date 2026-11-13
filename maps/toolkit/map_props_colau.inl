// ============================================================================
// 3D Cỏ Lau (Pampas Plume / Silvergrass) GPU Instanced Flora Pipeline
// Dual-LOD Template Architecture:
// - LOD 0: 100 strands (516 vertices, 324 triangles) for close-up hero fidelity
// - LOD 1: 44 strands (229 vertices, 144 triangles) for mid/far & mobile TBDR
// Features: Camera-facing strand ribbon expansion, backscattering SSS,
// Kajiya-Kay fiber specular, Frustum + Early-Z sorting, and lightweight shadow pass.
// ============================================================================

#define CO_LAU_HIGH_STRANDS 100
#define CO_LAU_LOW_STRANDS 44
#define CO_LAU_SEGMENTS 2
#define CO_LAU_HIGH_CYL_FACETS 4
#define CO_LAU_HIGH_CYL_RINGS 3
#define CO_LAU_LOW_CYL_FACETS 3
#define CO_LAU_LOW_CYL_RINGS 2
#define CO_LAU_BASE_HEIGHT 1.65f
#define CO_LAU_PLUME_Y0 0.95f
#define CO_LAU_PLUME_LEN 0.70f

static Mesh s_coLauMesh = {0};
static Mesh s_coLauLodMesh = {0};
static bool s_coLauTemplateReady = false;
static Shader s_coLauShader = {0};
static Shader s_coLauShadowShader = {0};
static unsigned int s_coLauCachedShaderId[2] = {0, 0};
static int s_coLauInstanceTexLoc[2] = {-1, -1};
static int s_coLauLodStrideLoc[2] = {-1, -1};
static int s_coLauWorldOffsetLoc[2] = {-1, -1};

static inline float CoLau_Mulberry32(unsigned int *state)
{
    unsigned int a = *state;
    a += 0x6D2B79F5u;
    *state = a;
    unsigned int t = a ^ (a >> 15);
    t = t * (1u | a);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (float)((t ^ (t >> 14)) & 0xFFFFFFFFu) * (1.0f / 4294967296.0f);
}

static inline float CoLau_StemCurveX(float y, float invH)
{
    float normY = y * invH;
    if (normY < 0.0f)
        normY = 0.0f;
    else if (normY > 1.0f)
        normY = 1.0f;
    return 0.16f * normY * normY;
}

static inline float CoLau_StemCurveZ(float y)
{
    float s, c;
    Nature_FastSinCos(y * 1.6f, &s, &c);
    return 0.04f * s;
}

static inline float CoLau_Envelope(float u)
{
    float clamped = (u > 0.0f) ? ((u < 1.0f) ? u : 1.0f) : 0.0f;
    float uShaped = powf(clamped, 0.75f);
    float s, c;
    Nature_FastSinCos(3.14159265f * uShaped, &s, &c);
    return 0.012f + 0.095f * powf(fmaxf(0.0f, s), 0.85f);
}

static void CoLau_UnloadTemplate(void)
{
    if (s_coLauMesh.vaoId != 0)
    {
        UnloadMesh(s_coLauMesh);
        s_coLauMesh = (Mesh){0};
    }
    if (s_coLauLodMesh.vaoId != 0)
    {
        UnloadMesh(s_coLauLodMesh);
        s_coLauLodMesh = (Mesh){0};
    }
    s_coLauTemplateReady = false;
}

static inline void CoLau_SetVertex(
    Mesh *mesh, int idx,
    Vector3 pos, Vector2 uv, Vector3 normal, Vector3 tangent, float isStrand, float rd, float rad)
{
    mesh->vertices[idx * 3 + 0] = pos.x;
    mesh->vertices[idx * 3 + 1] = pos.y;
    mesh->vertices[idx * 3 + 2] = pos.z;

    mesh->texcoords[idx * 2 + 0] = uv.x;
    mesh->texcoords[idx * 2 + 1] = uv.y;

    mesh->normals[idx * 3 + 0] = normal.x;
    mesh->normals[idx * 3 + 1] = normal.y;
    mesh->normals[idx * 3 + 2] = normal.z;

    mesh->tangents[idx * 4 + 0] = tangent.x;
    mesh->tangents[idx * 4 + 1] = tangent.y;
    mesh->tangents[idx * 4 + 2] = tangent.z;
    mesh->tangents[idx * 4 + 3] = isStrand;

    mesh->texcoords2[idx * 2 + 0] = rd;
    mesh->texcoords2[idx * 2 + 1] = rad;
}

static bool CoLau_BuildTemplateMesh(Mesh *outMesh, int strands, int cylFacets, int cylRings, float radiusBoost)
{
    int maxVerts = (cylRings + 1) * cylFacets + strands * 5;
    int maxIndices = cylRings * cylFacets * 6 + strands * 9;

    Mesh mesh = {0};
    mesh.vertices = (float *)RL_MALLOC((size_t)maxVerts * 3 * sizeof(float));
    mesh.texcoords = (float *)RL_MALLOC((size_t)maxVerts * 2 * sizeof(float));
    mesh.normals = (float *)RL_MALLOC((size_t)maxVerts * 3 * sizeof(float));
    mesh.tangents = (float *)RL_MALLOC((size_t)maxVerts * 4 * sizeof(float));
    mesh.texcoords2 = (float *)RL_MALLOC((size_t)maxVerts * 2 * sizeof(float));
    mesh.indices = (unsigned short *)RL_MALLOC((size_t)maxIndices * sizeof(unsigned short));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals ||
        !mesh.tangents || !mesh.texcoords2 || !mesh.indices)
    {
        if (mesh.vertices)
            RL_FREE(mesh.vertices);
        if (mesh.texcoords)
            RL_FREE(mesh.texcoords);
        if (mesh.normals)
            RL_FREE(mesh.normals);
        if (mesh.tangents)
            RL_FREE(mesh.tangents);
        if (mesh.texcoords2)
            RL_FREE(mesh.texcoords2);
        if (mesh.indices)
            RL_FREE(mesh.indices);
        return false;
    }

    int vCount = 0;
    int iCount = 0;
    const float H = CO_LAU_BASE_HEIGHT;
    const float invH = 1.0f / H;
    const float invRings = 1.0f / (float)cylRings;
    const float invFacets = 6.2831853f / (float)cylFacets;

    // 1. Stem Cylinder
    for (int r = 0; r <= cylRings; r++)
    {
        float y = H * ((float)r * invRings);
        float rad = 0.009f * (1.0f - 0.65f * (y * invH));
        float axX = CoLau_StemCurveX(y, invH);
        float axZ = CoLau_StemCurveZ(y);

        for (int j = 0; j < cylFacets; j++)
        {
            float a = (float)j * invFacets;
            float sa, ca;
            Nature_FastSinCos(a, &sa, &ca);

            CoLau_SetVertex(
                &mesh, vCount++,
                (Vector3){axX + rad * ca, y, axZ + rad * sa},
                (Vector2){0.0f, 0.0f},
                (Vector3){ca, 0.0f, sa},
                (Vector3){0.0f, 1.0f, 0.0f},
                0.0f, 0.0f, 1.0f);
        }
    }

    for (int r = 0; r < cylRings; r++)
    {
        for (int j = 0; j < cylFacets; j++)
        {
            unsigned short a = (unsigned short)(r * cylFacets + j);
            unsigned short b = (unsigned short)(r * cylFacets + (j + 1) % cylFacets);
            unsigned short c = a + (unsigned short)cylFacets;
            unsigned short d = b + (unsigned short)cylFacets;

            mesh.indices[iCount++] = a;
            mesh.indices[iCount++] = b;
            mesh.indices[iCount++] = c;
            mesh.indices[iCount++] = b;
            mesh.indices[iCount++] = d;
            mesh.indices[iCount++] = c;
        }
    }

    // 2. Plume Strands
    unsigned int rng = 11u;
    const float invSegs = 1.0f / (float)CO_LAU_SEGMENTS;
    for (int i = 0; i < strands; i++)
    {
        float t0 = CoLau_Mulberry32(&rng) * 0.86f;
        float ph = (float)i * 2.39996f + CoLau_Mulberry32(&rng) * 0.6f;
        float tw = (CoLau_Mulberry32(&rng) - 0.5f) * 0.9f;
        float u1 = fminf(1.0f, t0 + (0.3f + 0.55f * CoLau_Mulberry32(&rng)) * (1.0f - t0) + 0.1f);
        float y0 = CO_LAU_PLUME_Y0 + CO_LAU_PLUME_LEN * t0;
        float y1 = CO_LAU_PLUME_Y0 + CO_LAU_PLUME_LEN * u1;
        float re = (CoLau_Envelope(u1) * (0.78f + 0.22f * CoLau_Mulberry32(&rng)) + 0.012f) * radiusBoost;
        float rd = CoLau_Mulberry32(&rng);

        Vector4 pts[3];
        for (int j = 0; j <= CO_LAU_SEGMENTS; j++)
        {
            float u = (float)j * invSegs;
            float m = 1.0f - u;
            float r = 2.0f * m * u * re * 0.55f + u * u * re;
            float y = m * m * y0 + 2.0f * m * u * (y0 + (y1 - y0) * 0.38f) + u * u * y1 - 0.06f * u * u * u * (0.5f + rd);
            float sWave, cWave;
            Nature_FastSinCos(u * 6.0f + rd * 40.0f, &sWave, &cWave);
            float a = ph + tw * u + 0.14f * u * sWave;
            float sa, ca;
            Nature_FastSinCos(a, &sa, &ca);
            float axX = CoLau_StemCurveX(y, invH);
            float axZ = CoLau_StemCurveZ(y);
            pts[j] = (Vector4){axX + r * ca, y, axZ + r * sa, r};
        }

        unsigned short vb = (unsigned short)vCount;
        for (int j = 0; j <= CO_LAU_SEGMENTS; j++)
        {
            Vector4 aPt = pts[j > 0 ? j - 1 : 0];
            Vector4 bPt = pts[j < CO_LAU_SEGMENTS ? j + 1 : CO_LAU_SEGMENTS];
            Vector4 p = pts[j];

            float tx = bPt.x - aPt.x;
            float ty = bPt.y - aPt.y;
            float tz = bPt.z - aPt.z;
            float tLenSq = tx * tx + ty * ty + tz * tz;
            float invTl = (tLenSq > 1e-8f) ? Nature_FastInvSqrt(tLenSq) : 1.0f;
            Vector3 T = {tx * invTl, ty * invTl, tz * invTl};

            float axX = CoLau_StemCurveX(p.y, invH);
            float axZ = CoLau_StemCurveZ(p.y);
            float ox = p.x - axX;
            float oz = p.z - axZ;
            float oLenSq = ox * ox + oz * oz;
            float invOl = (oLenSq > 1e-8f) ? Nature_FastInvSqrt(oLenSq) : 1.0f;
            ox = (ox * invOl) * 0.90f;
            oz = (oz * invOl) * 0.90f;
            float oy = 0.45f;
            float nLenSq = ox * ox + oy * oy + oz * oz;
            float invNl = (nLenSq > 1e-8f) ? Nature_FastInvSqrt(nLenSq) : 1.0f;
            float radVal = fmaxf(0.06f, fminf(1.0f, p.w * (1.0f / 0.11f)));
            Vector3 normal = {ox * invNl, oy * invNl, oz * invNl};

            float u = (float)j * invSegs;
            Vector3 pos = {p.x, p.y, p.z};

            if (j < CO_LAU_SEGMENTS)
            {
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){u, -1.0f}, normal, T, 1.0f, rd, radVal);
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){u, +1.0f}, normal, T, 1.0f, rd, radVal);
            }
            else
            {
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){1.0f, 0.0f}, normal, T, 1.0f, rd, radVal);
            }
        }

        unsigned short a = vb;
        mesh.indices[iCount++] = a;
        mesh.indices[iCount++] = a + 1;
        mesh.indices[iCount++] = a + 2;
        mesh.indices[iCount++] = a + 1;
        mesh.indices[iCount++] = a + 3;
        mesh.indices[iCount++] = a + 2;

        unsigned short e = vb + 2;
        mesh.indices[iCount++] = e;
        mesh.indices[iCount++] = e + 1;
        mesh.indices[iCount++] = vb + 4;
    }

    mesh.vertexCount = vCount;
    mesh.triangleCount = iCount / 3;

    UploadMesh(&mesh, false);

    RL_FREE(mesh.vertices);
    RL_FREE(mesh.texcoords);
    RL_FREE(mesh.normals);
    RL_FREE(mesh.tangents);
    RL_FREE(mesh.texcoords2);
    RL_FREE(mesh.indices);
    mesh.vertices = NULL;
    mesh.texcoords = NULL;
    mesh.normals = NULL;
    mesh.tangents = NULL;
    mesh.texcoords2 = NULL;
    mesh.indices = NULL;

    *outMesh = mesh;
    return (mesh.vaoId != 0);
}

static bool CoLau_BuildTemplate(void)
{
    CoLau_UnloadTemplate();

    if (!CoLau_BuildTemplateMesh(&s_coLauMesh, CO_LAU_HIGH_STRANDS, CO_LAU_HIGH_CYL_FACETS, CO_LAU_HIGH_CYL_RINGS, 1.0f))
        return false;

    if (!CoLau_BuildTemplateMesh(&s_coLauLodMesh, CO_LAU_LOW_STRANDS, CO_LAU_LOW_CYL_FACETS, CO_LAU_LOW_CYL_RINGS, 1.12f))
    {
        CoLau_UnloadTemplate();
        return false;
    }

    s_coLauTemplateReady = true;
    return true;
}

static Shader CoLau_GetShader(bool shadow)
{
    Shader *shader = shadow ? &s_coLauShadowShader : &s_coLauShader;
    int pass = shadow ? 1 : 0;
    if (shader->id == 0 || s_coLauCachedShaderId[pass] != shader->id)
    {
        if (shader->id == 0)
        {
            *shader = ResourceManager_LoadShader(
                shadow ? "maps/toolkit/shaders/colau_plume_shadow.vs"
                       : "maps/toolkit/shaders/colau_plume.vs",
                shadow ? "maps/toolkit/shaders/nature_shadow.fs"
                       : "maps/toolkit/shaders/colau_plume.fs");
        }
        if (shader->id != 0 && shader->locs != NULL)
        {
            shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
            shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
            shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
            shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
            s_coLauInstanceTexLoc[pass] = GetShaderLocation(*shader, "u_coLauInstances");
            s_coLauLodStrideLoc[pass] = GetShaderLocation(*shader, "u_lodStride");
            s_coLauWorldOffsetLoc[pass] = GetShaderLocation(*shader, "u_worldOffset");
            if (!shadow)
            {
                MapShadow_ConfigureShader(*shader);
                VFXLight_RegisterShader(*shader);
            }
            (void)Nature_GetCachedLocs(*shader);
            s_coLauCachedShaderId[pass] = shader->id;
        }
    }
    return *shader;
}

static bool CoLau_Init(void)
{
    if (s_coLauTemplateReady)
        return true;

    Shader vis = CoLau_GetShader(false);
    Shader shd = CoLau_GetShader(true);
    if (vis.id == 0 || vis.id == rlGetShaderIdDefault() ||
        shd.id == 0 || shd.id == rlGetShaderIdDefault() ||
        s_coLauInstanceTexLoc[0] < 0)
        return false;

    return CoLau_BuildTemplate();
}

MapCoLauField MapProp_CreateCoLauField(const MapCoLauPlacement *placements, int count)
{
    MapCoLauField field = {0};
    if (!placements || count <= 0)
        return field;

    if (!CoLau_Init())
        return field;

    int rows = (count + 255) / 256;
    float *pixels = (float *)MemAlloc((size_t)(rows * 512 * 4) * sizeof(float));
    if (!pixels)
        return field;

    Vector3 boundsMin = {1e9f, 1e9f, 1e9f};
    Vector3 boundsMax = {-1e9f, -1e9f, -1e9f};

    for (int i = 0; i < count; i++)
    {
        const MapCoLauPlacement *p = &placements[i];
        float h = (p->height > 0.5f) ? p->height : 2.80f;
        float sc = (p->scale > 0.1f) ? p->scale : 1.0f;
        float rPad = 0.45f * sc;

        if (p->position.x - rPad < boundsMin.x)
            boundsMin.x = p->position.x - rPad;
        if (p->position.y < boundsMin.y)
            boundsMin.y = p->position.y;
        if (p->position.z - rPad < boundsMin.z)
            boundsMin.z = p->position.z - rPad;
        if (p->position.x + rPad > boundsMax.x)
            boundsMax.x = p->position.x + rPad;
        if (p->position.y + h > boundsMax.y)
            boundsMax.y = p->position.y + h;
        if (p->position.z + rPad > boundsMax.z)
            boundsMax.z = p->position.z + rPad;

        int base = i * 8;
        Color tint = (p->tint.a > 0) ? p->tint : (Color){228, 230, 223, 255};
        int packedRGB = ((int)tint.r << 16) | ((int)tint.g << 8) | (int)tint.b;

        pixels[base + 0] = p->position.x;
        pixels[base + 1] = p->position.y;
        pixels[base + 2] = p->position.z;
        pixels[base + 3] = p->phase;

        pixels[base + 4] = h;
        pixels[base + 5] = sc;
        pixels[base + 6] = p->rotationDeg * DEG2RAD;
        pixels[base + 7] = (float)packedRGB;
    }

    Image instImg = {
        .data = pixels,
        .width = 512,
        .height = rows,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
    };
    field.instanceTexture = LoadTextureFromImage(instImg);
    MemFree(pixels);

    if (field.instanceTexture.id != 0)
    {
        SetTextureFilter(field.instanceTexture, TEXTURE_FILTER_POINT);
        SetTextureWrap(field.instanceTexture, TEXTURE_WRAP_CLAMP);
        field.instanceCount = count;
        field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
        field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f + 0.5f;
        field.drawDistance = 85.0f;
        field.lodDistance = 34.0f;
        field.shadowDistance = 28.0f;
        field.ready = true;
    }
    return field;
}

static void CoLau_SortFieldsFrontToBack(int *order, float *depth, int count)
{
    if (count <= 1)
        return;
    if (count <= 24)
    {
        for (int i = 1; i < count; i++)
        {
            int keyIdx = order[i];
            float keyDepth = depth[i];
            int j = i - 1;
            while (j >= 0 && depth[j] > keyDepth)
            {
                order[j + 1] = order[j];
                depth[j + 1] = depth[j];
                j--;
            }
            order[j + 1] = keyIdx;
            depth[j + 1] = keyDepth;
        }
        return;
    }
    static const int gaps[5] = {57, 23, 10, 4, 1};
    for (int g = 0; g < 5; g++)
    {
        int gap = gaps[g];
        for (int i = gap; i < count; i++)
        {
            int keyIdx = order[i];
            float keyDepth = depth[i];
            int j = i;
            while (j >= gap && depth[j - gap] > keyDepth)
            {
                order[j] = order[j - gap];
                depth[j] = depth[j - gap];
                j -= gap;
            }
            order[j] = keyIdx;
            depth[j] = keyDepth;
        }
    }
}

void MapProp_DrawCoLauFieldBatch(MapCoLauField *fields, int fieldCount, Vector3 worldOffset,
                                 float time, Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0 || !s_coLauTemplateReady)
        return;

    Shader shader = CoLau_GetShader(false);
    if (shader.id == 0 || shader.id == rlGetShaderIdDefault())
        return;

    Nature_RefreshFrustumCache();

    GfxQuality quality = GfxQuality_Get();
    float rangeScale = (quality >= GFX_HIGH) ? 1.0f
                                             : (quality == GFX_MED ? 0.84f : 0.68f);
    float lodScale = (quality >= GFX_HIGH) ? 1.0f
                                           : (quality == GFX_MED ? 0.78f : 0.58f);

    static int *s_coLauOrder = NULL;
    static float *s_coLauDepth = NULL;
    static int s_coLauCapacity = 0;

    int localOrder[128];
    float localDepth[128];
    int *drawOrder = localOrder;
    float *drawDepth = localDepth;

    if (fieldCount > 128)
    {
        if (fieldCount > s_coLauCapacity)
        {
            int *grownOrder = (int *)RL_REALLOC(s_coLauOrder, (size_t)fieldCount * sizeof(int));
            float *grownDepth = (float *)RL_REALLOC(s_coLauDepth, (size_t)fieldCount * sizeof(float));
            if (!grownOrder || !grownDepth)
                return;
            s_coLauOrder = grownOrder;
            s_coLauDepth = grownDepth;
            s_coLauCapacity = fieldCount;
        }
        drawOrder = s_coLauOrder;
        drawDepth = s_coLauDepth;
    }

    int drawCount = 0;
    Vector3 camPos = camera.position;
    for (int i = 0; i < fieldCount; i++)
    {
        MapCoLauField *field = &fields[i];
        if (!field->ready || field->instanceCount <= 0)
            continue;

        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
        float dx = camPos.x - center.x;
        float dy = camPos.y - center.y;
        float dz = camPos.z - center.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        float maxDraw = field->drawDistance * rangeScale + field->boundsRadius;
        if (distSq > maxDraw * maxDraw)
            continue;

        if (!Nature_IsChunkVisibleFast(center, field->boundsRadius))
            continue;

        drawOrder[drawCount] = i;
        drawDepth[drawCount] = distSq;
        drawCount++;
    }

    if (drawCount <= 0)
        return;

    if (drawCount > 1)
        CoLau_SortFieldsFrontToBack(drawOrder, drawDepth, drawCount);

    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
    rlDisableBackfaceCulling();

    Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
    model = MatrixMultiply(model, rlGetMatrixTransform());
    Matrix mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
    SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MVP], mvp);

    if (s_coLauWorldOffsetLoc[0] >= 0)
        SetShaderValue(shader, s_coLauWorldOffsetLoc[0], &worldOffset, SHADER_UNIFORM_VEC3);
    if (s_coLauInstanceTexLoc[0] >= 0)
    {
        int unit0 = 0;
        SetShaderValue(shader, s_coLauInstanceTexLoc[0], &unit0, SHADER_UNIFORM_INT);
    }

    unsigned int activeVao = 0;
    unsigned int activeTexId = 0;
    int activeStride = -1;

    for (int d = 0; d < drawCount; d++)
    {
        int i = drawOrder[d];
        MapCoLauField *field = &fields[i];
        float distSq = drawDepth[d];
        float lodDist = field->lodDistance * lodScale + field->boundsRadius;
        float lodDistSq = lodDist * lodDist;
        bool isFar = (distSq > lodDistSq);

        // Mobile GFX_LOW dùng trực tiếp LOD 1 (144 tris) để tiết kiệm vertex/fragment TBDR
        bool useLowMesh = (quality == GFX_LOW) || isFar;
        const Mesh *targetMesh = useLowMesh ? &s_coLauLodMesh : &s_coLauMesh;

        int stride = 1;
        if (isFar)
        {
            float veryFarDist = lodDist * 1.85f;
            stride = (quality == GFX_LOW && distSq > veryFarDist * veryFarDist) ? 3 : 2;
        }

        int instances = (stride > 1) ? ((field->instanceCount + stride - 1) / stride) : field->instanceCount;
        if (instances <= 0)
            continue;

        if (activeStride != stride && s_coLauLodStrideLoc[0] >= 0)
        {
            SetShaderValue(shader, s_coLauLodStrideLoc[0], &stride, SHADER_UNIFORM_INT);
            activeStride = stride;
        }

        if (activeTexId != field->instanceTexture.id)
        {
            rlActiveTextureSlot(0);
            rlEnableTexture(field->instanceTexture.id);
            activeTexId = field->instanceTexture.id;
        }

        if (activeVao != targetMesh->vaoId)
        {
            rlEnableVertexArray(targetMesh->vaoId);
            activeVao = targetMesh->vaoId;
        }

        rlDrawVertexArrayElementsInstanced(0, targetMesh->triangleCount * 3, NULL, instances);
    }

    if (activeVao != 0)
        rlDisableVertexArray();
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_DrawCoLauField(MapCoLauField *field, Vector3 worldOffset,
                            float time, Vector2 windDirection, float windStrength)
{
    MapProp_DrawCoLauFieldBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

void MapProp_DrawCoLauFieldShadowCasterBatch(MapCoLauField *fields, int fieldCount, Vector3 worldOffset,
                                             float time, Vector2 windDirection, float windStrength)
{
    GfxQuality quality = GfxQuality_Get();
    if (!fields || fieldCount <= 0 || !s_coLauTemplateReady ||
        quality < GFX_MED ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(false))
        return;

    Vector3 sunDir = Nature_FastNormalize(Environment_GetSunDirection());
    if (fabsf(sunDir.y) > 0.98f)
        return;

    Shader shd = CoLau_GetShader(true);
    if (shd.id == 0 || shd.id == rlGetShaderIdDefault())
        return;

    Nature_RefreshFrustumCache();

    bool filterActive = Nature_ShadowCasterFilterActive();
    Vector3 shadowFocus = EnvShadow_GetFocus();
    float halfExtent = EnvShadow_GetHalfExtent();
    float groundRadius = fmaxf(halfExtent, 8.0f) / fmaxf(fabsf(sunDir.y), 0.32f);
    float qualityDistScale = (quality >= GFX_HIGH) ? 1.0f : 0.65f;
    Vector3 shadowSweepOffset = {sunDir.x * 1.8f, 0.0f, sunDir.z * 1.8f};
    int stride = (quality >= GFX_HIGH) ? 2 : 3;

    bool bound = false;
    unsigned int activeTexId = 0;

    for (int i = 0; i < fieldCount; i++)
    {
        MapCoLauField *field = &fields[i];
        if (!field->ready || field->instanceCount <= 0)
            continue;

        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
        if (!filterActive)
        {
            float dx = center.x - shadowFocus.x;
            float dz = center.z - shadowFocus.z;
            float distSq = dx * dx + dz * dz;
            if (field->shadowDistance > 0.0f)
            {
                float maxDist = field->shadowDistance * qualityDistScale + field->boundsRadius + 2.0f;
                if (distSq > maxDist * maxDist)
                    continue;
            }
            float covLimit = groundRadius + field->boundsRadius * 1.12f + 2.0f;
            if (distSq > covLimit * covLimit)
                continue;
            Vector3 sweptCenter = Vector3Add(center, shadowSweepOffset);
            if (!Nature_IsChunkVisibleFast(sweptCenter, field->boundsRadius + 4.0f))
                continue;
        }

        if (!bound)
        {
            Nature_BeginWindReceiverShader(shd);
            Nature_UpdateShadowShader(shd, time, windDirection, windStrength,
                                      false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);

            Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
            Matrix mvp = MatrixMultiply(model, EnvShadow_GetLightVP());
            SetShaderValueMatrix(shd, shd.locs[SHADER_LOC_MATRIX_MVP], mvp);

            if (s_coLauWorldOffsetLoc[1] >= 0)
                SetShaderValue(shd, s_coLauWorldOffsetLoc[1], &worldOffset, SHADER_UNIFORM_VEC3);
            if (s_coLauInstanceTexLoc[1] >= 0)
            {
                int unit0 = 0;
                SetShaderValue(shd, s_coLauInstanceTexLoc[1], &unit0, SHADER_UNIFORM_INT);
            }
            if (s_coLauLodStrideLoc[1] >= 0)
                SetShaderValue(shd, s_coLauLodStrideLoc[1], &stride, SHADER_UNIFORM_INT);

            rlDisableBackfaceCulling();
            rlEnableVertexArray(s_coLauLodMesh.vaoId);
            bound = true;
        }

        int instances = (field->instanceCount + stride - 1) / stride;
        if (instances <= 0)
            continue;

        if (activeTexId != field->instanceTexture.id)
        {
            rlActiveTextureSlot(0);
            rlEnableTexture(field->instanceTexture.id);
            activeTexId = field->instanceTexture.id;
        }
        rlDrawVertexArrayElementsInstanced(0, s_coLauLodMesh.triangleCount * 3, NULL, instances);
    }

    if (bound)
    {
        rlDisableVertexArray();
        Nature_EndWindReceiverShader();
        rlEnableBackfaceCulling();
    }
}

void MapProp_DrawCoLauFieldShadowCaster(MapCoLauField *field, Vector3 worldOffset,
                                        float time, Vector2 windDirection, float windStrength)
{
    MapProp_DrawCoLauFieldShadowCasterBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

void MapProp_UnloadCoLauField(MapCoLauField *field)
{
    if (!field || !field->ready)
        return;

    if (field->instanceTexture.id != 0)
    {
        UnloadTexture(field->instanceTexture);
        field->instanceTexture = (Texture2D){0};
    }

    field->instanceCount = 0;
    field->boundsRadius = 0.0f;
    field->ready = false;
}
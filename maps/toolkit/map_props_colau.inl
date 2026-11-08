// ============================================================================
// 3D Cỏ Lau (Pampas Plume / Silvergrass) GPU Instanced Flora Pipeline
// WebGL2 Low-Preset Implementation (516 vertices, 324 triangles per plume)
// Features: Camera-facing strand ribbon expansion, backscattering SSS,
// Kajiya-Kay fiber specular, volumetric AO, and early-Z alpha-test cutout.
// ============================================================================

#define CO_LAU_LOW_STRANDS 100
#define CO_LAU_LOW_SEGMENTS 2
#define CO_LAU_LOW_CYL_FACETS 4
#define CO_LAU_LOW_CYL_RINGS 3
#define CO_LAU_BASE_HEIGHT 1.65f
#define CO_LAU_PLUME_Y0 0.95f
#define CO_LAU_PLUME_LEN 0.70f

static Mesh s_coLauMesh = {0};
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

static inline float CoLau_StemCurveX(float y, float H)
{
    float normY = fmaxf(0.0f, fminf(1.0f, y / H));
    return 0.16f * normY * normY;
}

static inline float CoLau_StemCurveZ(float y)
{
    return 0.04f * sinf(y * 1.6f);
}

static inline float CoLau_Envelope(float u)
{
    float uShaped = powf(fmaxf(0.0f, fminf(1.0f, u)), 0.75f);
    return 0.012f + 0.095f * powf(fmaxf(0.0f, sinf(3.14159265f * uShaped)), 0.85f);
}

static void CoLau_UnloadTemplate(void)
{
    if (s_coLauMesh.vaoId != 0)
    {
        UnloadMesh(s_coLauMesh);
        s_coLauMesh = (Mesh){0};
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

static bool CoLau_BuildTemplate(void)
{
    CoLau_UnloadTemplate();

    int maxVerts = 16 + CO_LAU_LOW_STRANDS * 5; // 516 vertices
    int maxIndices = 72 + CO_LAU_LOW_STRANDS * 9; // 972 indices (324 triangles)

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
        if (mesh.vertices) RL_FREE(mesh.vertices);
        if (mesh.texcoords) RL_FREE(mesh.texcoords);
        if (mesh.normals) RL_FREE(mesh.normals);
        if (mesh.tangents) RL_FREE(mesh.tangents);
        if (mesh.texcoords2) RL_FREE(mesh.texcoords2);
        if (mesh.indices) RL_FREE(mesh.indices);
        return false;
    }

    int vCount = 0;
    int iCount = 0;
    float H = CO_LAU_BASE_HEIGHT;

    // 1. Stem Cylinder: 4 facets, 3 vertical rings
    for (int r = 0; r <= CO_LAU_LOW_CYL_RINGS; r++)
    {
        float y = H * (float)r / (float)CO_LAU_LOW_CYL_RINGS;
        float rad = 0.009f * (1.0f - 0.65f * (y / H));
        float axX = CoLau_StemCurveX(y, H);
        float axZ = CoLau_StemCurveZ(y);

        for (int j = 0; j < CO_LAU_LOW_CYL_FACETS; j++)
        {
            float a = (float)j / (float)CO_LAU_LOW_CYL_FACETS * 6.2831853f;
            float ca = cosf(a);
            float sa = sinf(a);

            CoLau_SetVertex(
                &mesh, vCount++,
                (Vector3){axX + rad * ca, y, axZ + rad * sa},
                (Vector2){0.0f, 0.0f},
                (Vector3){ca, 0.0f, sa},
                (Vector3){0.0f, 1.0f, 0.0f},
                0.0f, 0.0f, 1.0f);
        }
    }

    for (int r = 0; r < CO_LAU_LOW_CYL_RINGS; r++)
    {
        for (int j = 0; j < CO_LAU_LOW_CYL_FACETS; j++)
        {
            unsigned short a = (unsigned short)(r * CO_LAU_LOW_CYL_FACETS + j);
            unsigned short b = (unsigned short)(r * CO_LAU_LOW_CYL_FACETS + (j + 1) % CO_LAU_LOW_CYL_FACETS);
            unsigned short c = a + CO_LAU_LOW_CYL_FACETS;
            unsigned short d = b + CO_LAU_LOW_CYL_FACETS;

            mesh.indices[iCount++] = a;
            mesh.indices[iCount++] = b;
            mesh.indices[iCount++] = c;
            mesh.indices[iCount++] = b;
            mesh.indices[iCount++] = d;
            mesh.indices[iCount++] = c;
        }
    }

    // 2. Strands (100 strands along plume length)
    unsigned int rng = 11u;
    for (int i = 0; i < CO_LAU_LOW_STRANDS; i++)
    {
        float t0 = CoLau_Mulberry32(&rng) * 0.86f;
        float ph = (float)i * 2.39996f + CoLau_Mulberry32(&rng) * 0.6f;
        float tw = (CoLau_Mulberry32(&rng) - 0.5f) * 0.9f;
        float u1 = fminf(1.0f, t0 + (0.3f + 0.55f * CoLau_Mulberry32(&rng)) * (1.0f - t0) + 0.1f);
        float y0 = CO_LAU_PLUME_Y0 + CO_LAU_PLUME_LEN * t0;
        float y1 = CO_LAU_PLUME_Y0 + CO_LAU_PLUME_LEN * u1;
        float re = CoLau_Envelope(u1) * (0.78f + 0.22f * CoLau_Mulberry32(&rng)) + 0.012f;
        float rd = CoLau_Mulberry32(&rng);

        Vector4 pts[3];
        for (int j = 0; j <= CO_LAU_LOW_SEGMENTS; j++)
        {
            float u = (float)j / (float)CO_LAU_LOW_SEGMENTS;
            float m = 1.0f - u;
            float r = 2.0f * m * u * re * 0.55f + u * u * re;
            float y = m * m * y0 + 2.0f * m * u * (y0 + (y1 - y0) * 0.38f) + u * u * y1 - 0.06f * u * u * u * (0.5f + rd);
            float a = ph + tw * u + 0.14f * u * sinf(u * 6.0f + rd * 40.0f);
            float axX = CoLau_StemCurveX(y, H);
            float axZ = CoLau_StemCurveZ(y);
            pts[j] = (Vector4){axX + r * cosf(a), y, axZ + r * sinf(a), r};
        }

        unsigned short vb = (unsigned short)vCount;
        for (int j = 0; j <= CO_LAU_LOW_SEGMENTS; j++)
        {
            Vector4 aPt = pts[j > 0 ? j - 1 : 0];
            Vector4 bPt = pts[j < CO_LAU_LOW_SEGMENTS ? j + 1 : CO_LAU_LOW_SEGMENTS];
            Vector4 p = pts[j];

            float tx = bPt.x - aPt.x;
            float ty = bPt.y - aPt.y;
            float tz = bPt.z - aPt.z;
            float tl = sqrtf(tx * tx + ty * ty + tz * tz);
            if (tl < 1e-4f) tl = 1.0f;
            Vector3 T = {tx / tl, ty / tl, tz / tl};

            float axX = CoLau_StemCurveX(p.y, H);
            float axZ = CoLau_StemCurveZ(p.y);
            float ox = p.x - axX;
            float oz = p.z - axZ;
            float ol = sqrtf(ox * ox + oz * oz);
            if (ol < 1e-4f) ol = 1.0f;
            ox = (ox / ol) * 0.90f;
            oz = (oz / ol) * 0.90f;
            float oy = 0.45f;
            float nl = sqrtf(ox * ox + oy * oy + oz * oz);
            if (nl < 1e-4f) nl = 1.0f;
            float radVal = fmaxf(0.06f, fminf(1.0f, p.w / 0.11f));
            Vector3 normal = {ox / nl, oy / nl, oz / nl};

            float u = (float)j / (float)CO_LAU_LOW_SEGMENTS;
            Vector3 pos = {p.x, p.y, p.z};

            if (j < CO_LAU_LOW_SEGMENTS)
            {
                // Left ribbon vertex (v = -1.0)
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){u, -1.0f}, normal, T, 1.0f, rd, radVal);
                // Right ribbon vertex (v = +1.0)
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){u, +1.0f}, normal, T, 1.0f, rd, radVal);
            }
            else
            {
                // Tip ribbon vertex (v = 0.0)
                CoLau_SetVertex(&mesh, vCount++, pos, (Vector2){1.0f, 0.0f}, normal, T, 1.0f, rd, radVal);
            }
        }

        // Quad for segment 0
        unsigned short a = vb;
        mesh.indices[iCount++] = a;
        mesh.indices[iCount++] = a + 1;
        mesh.indices[iCount++] = a + 2;
        mesh.indices[iCount++] = a + 1;
        mesh.indices[iCount++] = a + 3;
        mesh.indices[iCount++] = a + 2;

        // Triangle to tip for segment 1
        unsigned short e = vb + 2;
        mesh.indices[iCount++] = e;
        mesh.indices[iCount++] = e + 1;
        mesh.indices[iCount++] = vb + 4;
    }

    mesh.vertexCount = vCount;
    mesh.triangleCount = iCount / 3;

    // Upload to GPU using Raylib canonical mesh pipeline (100% compatible with rlvk Vulkan backend)
    UploadMesh(&mesh, false);

    // Free CPU duplicate arrays (now held in GPU VBOs)
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

    s_coLauMesh = mesh;
    s_coLauTemplateReady = (mesh.vaoId != 0);
    return s_coLauTemplateReady;
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
        if (p->position.x < boundsMin.x) boundsMin.x = p->position.x;
        if (p->position.y < boundsMin.y) boundsMin.y = p->position.y;
        if (p->position.z < boundsMin.z) boundsMin.z = p->position.z;
        if (p->position.x > boundsMax.x) boundsMax.x = p->position.x;
        if (p->position.y + p->height > boundsMax.y) boundsMax.y = p->position.y + p->height;
        if (p->position.z > boundsMax.z) boundsMax.z = p->position.z;

        int base = i * 8;
        Color tint = (p->tint.a > 0) ? p->tint : (Color){228, 230, 223, 255};
        int packedRGB = ((int)tint.r << 16) | ((int)tint.g << 8) | (int)tint.b;

        // Texel 0: pos.xyz + phase
        pixels[base + 0] = p->position.x;
        pixels[base + 1] = p->position.y;
        pixels[base + 2] = p->position.z;
        pixels[base + 3] = p->phase;

        // Texel 1: height, scale, yaw (rad), packedRGB
        pixels[base + 4] = (p->height > 0.5f) ? p->height : 2.80f;
        pixels[base + 5] = (p->scale > 0.1f) ? p->scale : 1.0f;
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
        field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f + 3.0f;
        field.drawDistance = 85.0f;
        field.lodDistance = 34.0f;
        field.shadowDistance = 28.0f;
        field.ready = true;
    }
    return field;
}

void MapProp_DrawCoLauFieldBatch(MapCoLauField *fields, int fieldCount, Vector3 worldOffset,
                                 float time, Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0 || !s_coLauTemplateReady)
        return;

    Shader shader = CoLau_GetShader(false);
    if (shader.id == 0 || shader.id == rlGetShaderIdDefault())
        return;

    Vector3 camPos = camera.position;

    bool bound = false;

    for (int i = 0; i < fieldCount; i++)
    {
        MapCoLauField *field = &fields[i];
        if (!field->ready || field->instanceCount <= 0)
            continue;

        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
        float distToCam = Vector3Distance(camPos, center);
        if (distToCam > field->drawDistance + field->boundsRadius)
            continue;

        if (!bound)
        {
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

            rlEnableVertexArray(s_coLauMesh.vaoId);
            bound = true;
        }

        int stride = 1;
        if (distToCam > field->lodDistance + field->boundsRadius)
            stride = 2; // Distance-based subsampling for 60+ FPS efficiency

        if (s_coLauLodStrideLoc[0] >= 0)
            SetShaderValue(shader, s_coLauLodStrideLoc[0], &stride, SHADER_UNIFORM_INT);

        int instances = (stride > 1) ? ((field->instanceCount + stride - 1) / stride) : field->instanceCount;
        if (instances <= 0)
            continue;

        rlActiveTextureSlot(0);
        rlEnableTexture(field->instanceTexture.id);
        rlDrawVertexArrayElementsInstanced(0, s_coLauMesh.triangleCount * 3, NULL, instances);
    }

    if (bound)
    {
        rlDisableVertexArray();
        Nature_EndWindReceiverShader();
        rlEnableBackfaceCulling();
    }
}

void MapProp_DrawCoLauField(MapCoLauField *field, Vector3 worldOffset,
                            float time, Vector2 windDirection, float windStrength)
{
    MapProp_DrawCoLauFieldBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

void MapProp_DrawCoLauFieldShadowCasterBatch(MapCoLauField *fields, int fieldCount, Vector3 worldOffset,
                                             float time, Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0 || !s_coLauTemplateReady)
        return;

    Shader shd = CoLau_GetShader(true);
    if (shd.id == 0 || shd.id == rlGetShaderIdDefault())
        return;

    bool bound = false;

    for (int i = 0; i < fieldCount; i++)
    {
        MapCoLauField *field = &fields[i];
        if (!field->ready || field->instanceCount <= 0)
            continue;

        if (!bound)
        {
            Nature_BeginWindReceiverShader(shd);
            Nature_UpdateShader(shd, time, windDirection, windStrength,
                                false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);

            Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
            model = MatrixMultiply(model, rlGetMatrixTransform());
            Matrix mvp = MatrixMultiply(model, EnvShadow_GetLightVP());
            SetShaderValueMatrix(shd, shd.locs[SHADER_LOC_MATRIX_MVP], mvp);

            if (s_coLauWorldOffsetLoc[1] >= 0)
                SetShaderValue(shd, s_coLauWorldOffsetLoc[1], &worldOffset, SHADER_UNIFORM_VEC3);
            if (s_coLauInstanceTexLoc[1] >= 0)
            {
                int unit0 = 0;
                SetShaderValue(shd, s_coLauInstanceTexLoc[1], &unit0, SHADER_UNIFORM_INT);
            }

            int stride = 2;
            if (s_coLauLodStrideLoc[1] >= 0)
                SetShaderValue(shd, s_coLauLodStrideLoc[1], &stride, SHADER_UNIFORM_INT);

            rlDisableBackfaceCulling();
            rlEnableVertexArray(s_coLauMesh.vaoId);
            bound = true;
        }

        int instances = (field->instanceCount + 1) / 2;
        rlActiveTextureSlot(0);
        rlEnableTexture(field->instanceTexture.id);
        rlDrawVertexArrayElementsInstanced(0, s_coLauMesh.triangleCount * 3, NULL, instances);
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

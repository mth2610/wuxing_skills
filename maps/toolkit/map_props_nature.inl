// Batched, shader-driven meadow/flower/water surfaces. All geometry is built
// once at map init and submitted as a small number of opaque draw calls.

static Shader s_natureShader = {0};
static Shader s_waterShader = {0};
static bool s_natureShaderReady = false;
static bool s_waterShaderReady = false;

static Color Nature_LerpColor(Color a, Color b, float t)
{
    Color result = {
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
    return result;
}

static Shader Nature_GetShader(void)
{
    if (!s_natureShaderReady) {
        s_natureShader = ResourceManager_LoadShader("maps/toolkit/shaders/nature_lit.vs",
                                                    "maps/toolkit/shaders/nature_lit.fs");
        s_natureShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s_natureShader, "vertexPosition");
        s_natureShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s_natureShader, "vertexTexCoord");
        s_natureShader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(s_natureShader, "vertexNormal");
        s_natureShader.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(s_natureShader, "vertexColor");
        s_natureShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s_natureShader, "mvp");
        s_natureShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s_natureShader, "matModel");
        s_natureShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_natureShader, "colDiffuse");
        VFXLight_RegisterShader(s_natureShader);
        s_natureShaderReady = true;
    }
    return s_natureShader;
}

static Shader Water_GetShader(void)
{
    if (!s_waterShaderReady) {
        s_waterShader = ResourceManager_LoadShader("maps/toolkit/shaders/water_surface.vs",
                                                   "maps/toolkit/shaders/water_surface.fs");
        s_waterShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s_waterShader, "vertexPosition");
        s_waterShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s_waterShader, "vertexTexCoord");
        s_waterShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s_waterShader, "mvp");
        s_waterShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s_waterShader, "matModel");
        s_waterShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_waterShader, "colDiffuse");
        VFXLight_RegisterShader(s_waterShader);
        s_waterShaderReady = true;
    }
    return s_waterShader;
}

static void Nature_SetVertex(Mesh *mesh, int index, Vector3 p, Vector3 n,
                             float phase, float heightMask, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->normals[index * 3 + 0] = n.x;
    mesh->normals[index * 3 + 1] = n.y;
    mesh->normals[index * 3 + 2] = n.z;
    mesh->texcoords[index * 2 + 0] = phase;
    mesh->texcoords[index * 2 + 1] = heightMask;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

static void Nature_AddQuad(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                           Vector3 p2, Vector3 p3, Vector3 normal,
                           float phase, float h0, float h1, Color c0, Color c1)
{
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p3, normal, phase, h1, c1);
}

static Mesh Nature_AllocMesh(int vertexCount)
{
    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = vertexCount / 3;
    mesh.vertices = MemAlloc((unsigned int)vertexCount * 3u * sizeof(float));
    mesh.normals = MemAlloc((unsigned int)vertexCount * 3u * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)vertexCount * 2u * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)vertexCount * 4u * sizeof(unsigned char));
    return mesh;
}

static Model Nature_ModelFromMesh(Mesh mesh, Shader shader)
{
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;
    return model;
}

static void Nature_UpdateShader(float time, Vector2 windDirection, float windStrength)
{
    Shader shader = Nature_GetShader();
    float windLength = sqrtf(windDirection.x * windDirection.x + windDirection.y * windDirection.y);
    if (windLength > 0.0001f) {
        windDirection.x /= windLength;
        windDirection.y /= windLength;
    }
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector3 sunRgb = {sun.x, sun.y, sun.z};
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windDirection"), &windDirection, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windStrength"), &windStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightColor"), &sunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_ambientColor"), &ambientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
}

static unsigned int Nature_NextRandom(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float Nature_Random01(unsigned int *state)
{
    return (float)(Nature_NextRandom(state) & 0x00ffffffu) / 16777215.0f;
}

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData)
{
    if (!outPlacements || maxCount <= 0 || distribution.spacing <= 0.0f)
        return 0;
    if (distribution.jitter < 0.0f) distribution.jitter = 0.0f;
    if (distribution.jitter > 0.95f) distribution.jitter = 0.95f;

    int columns = (int)ceilf((distribution.maxBounds.x - distribution.minBounds.x) /
                             distribution.spacing);
    int rows = (int)ceilf((distribution.maxBounds.y - distribution.minBounds.y) /
                          distribution.spacing);
    unsigned int rng = distribution.seed ? distribution.seed : 1u;
    int count = 0;
    for (int row = 0; row < rows && count < maxCount; row++) {
        for (int column = 0; column < columns && count < maxCount; column++) {
            float jx = (Nature_Random01(&rng) - 0.5f) * distribution.spacing * distribution.jitter;
            float jz = (Nature_Random01(&rng) - 0.5f) * distribution.spacing * distribution.jitter;
            float x = distribution.minBounds.x + (column + 0.5f) * distribution.spacing + jx;
            float z = distribution.minBounds.y + (row + 0.5f) * distribution.spacing + jz;
            float density = densityFn ? densityFn(x, z, userData) : 1.0f;
            if (density < 0.0f) density = 0.0f;
            if (density > 1.0f) density = 1.0f;
            if (Nature_Random01(&rng) > density)
                continue;

            float y = groundCenter.y;
            if (ground && ground->ready)
                y = MapProp_SampleGroundHeight(ground, groundCenter, x, z);
            MapMeadowPlacement *placement = &outPlacements[count++];
            placement->position = (Vector3){x, y + distribution.yOffset, z};
            placement->radius = distribution.minRadius
                + (distribution.maxRadius - distribution.minRadius) * Nature_Random01(&rng);
            placement->height = distribution.minHeight
                + (distribution.maxHeight - distribution.minHeight) * Nature_Random01(&rng);
            placement->rotationDeg = Nature_Random01(&rng) * 360.0f;
            placement->phase = Nature_Random01(&rng);
        }
    }
    return count;
}

MapMeadowSurface MapProp_CreateMeadow(const MapMeadowPlacement *placements, int count,
                                      MapMeadowStyle style)
{
    MapMeadowSurface meadow = {0};
    if (!placements || count <= 0) return meadow;
    if (style.bladesPerClump < 1) style.bladesPerClump = 1;
    if (style.bladesPerClump > 10) style.bladesPerClump = 10;
    if (style.bladeSegments < 1) style.bladeSegments = 1;
    if (style.bladeSegments > 4) style.bladeSegments = 4;
    if (style.bladeWidthScale <= 0.0f) style.bladeWidthScale = 0.24f;

    int vertexCount = count * style.bladesPerClump * style.bladeSegments * 6;
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    const float golden = 2.39996323f;
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        for (int blade = 0; blade < style.bladesPerClump; blade++) {
            float angle = clump->rotationDeg * DEG2RAD + blade * golden;
            float radial = clump->radius * (0.12f + 0.72f * (float)(blade % 3) / 2.0f);
            float bx = clump->position.x + cosf(angle * 1.73f) * radial;
            float bz = clump->position.z + sinf(angle * 1.73f) * radial;
            float height = clump->height * (0.74f + 0.26f * sinf((float)(i * 13 + blade * 7)) * 0.5f + 0.13f);
            // Slightly widen alternating blades. Sub-pixel-thin vegetation
            // aliases into black needles at gameplay camera distance.
            float width = clump->radius * style.bladeWidthScale
                        * (0.88f + 0.20f * (float)(blade & 1));
            Vector3 side = {-sinf(angle), 0.0f, cosf(angle)};
            Vector3 facing = {cosf(angle), 0.12f, sinf(angle)};
            for (int segment = 0; segment < style.bladeSegments; segment++) {
                float t0 = (float)segment / style.bladeSegments;
                float t1 = (float)(segment + 1) / style.bladeSegments;
                float bend0 = t0 * t0 * height * 0.16f;
                float bend1 = t1 * t1 * height * 0.16f;
                float w0 = width * (1.0f - t0 * 0.82f);
                float w1 = width * (1.0f - t1 * 0.82f);
                Vector3 center0 = {bx + cosf(angle) * bend0, clump->position.y + height * t0, bz + sinf(angle) * bend0};
                Vector3 center1 = {bx + cosf(angle) * bend1, clump->position.y + height * t1, bz + sinf(angle) * bend1};
                Vector3 p0 = {center0.x - side.x * w0, center0.y, center0.z - side.z * w0};
                Vector3 p1 = {center0.x + side.x * w0, center0.y, center0.z + side.z * w0};
                Vector3 p2 = {center1.x + side.x * w1, center1.y, center1.z + side.z * w1};
                Vector3 p3 = {center1.x - side.x * w1, center1.y, center1.z - side.z * w1};
                Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, facing,
                               clump->phase, t0, t1,
                               Nature_LerpColor(style.rootColor, style.tipColor, t0),
                               Nature_LerpColor(style.rootColor, style.tipColor, t1));
            }
        }
    }
    meadow.model = Nature_ModelFromMesh(mesh, Nature_GetShader());
    meadow.ready = true;
    return meadow;
}

void MapProp_DrawMeadow(const MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready) return;
    Nature_UpdateShader(time, windDirection, windStrength);
    rlDisableBackfaceCulling();
    DrawModel(meadow->model, worldOffset, 1.0f, WHITE);
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready) return;
    UnloadModel(meadow->model);
    meadow->ready = false;
}

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor)
{
    MapFlowerField field = {0};
    if (!placements || count <= 0) return field;
    const int verticesPerFlower = 54;
    Mesh mesh = Nature_AllocMesh(count * verticesPerFlower);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        float stemWidth = fmaxf(0.018f, flower->bloomRadius * 0.22f);
        Vector3 base = flower->position;
        Vector3 head = {base.x, base.y + h, base.z};
        for (int cross = 0; cross < 2; cross++) {
            float angle = flower->rotationDeg * DEG2RAD + cross * PI * 0.5f;
            Vector3 side = {cosf(angle) * stemWidth, 0.0f, sinf(angle) * stemWidth};
            Vector3 normal = {-sinf(angle), 0.05f, cosf(angle)};
            Vector3 p0 = {base.x - side.x, base.y, base.z - side.z};
            Vector3 p1 = {base.x + side.x, base.y, base.z + side.z};
            Vector3 p2 = {head.x + side.x * 0.45f, head.y, head.z + side.z * 0.45f};
            Vector3 p3 = {head.x - side.x * 0.45f, head.y, head.z - side.z * 0.45f};
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, normal, phase, 0.0f, 1.0f, stemColor, stemColor);
        }
        for (int petal = 0; petal < 5; petal++) {
            float angle = flower->rotationDeg * DEG2RAD + (float)petal * 2.0f * PI / 5.0f;
            Vector3 radial = {cosf(angle), 0.0f, sinf(angle)};
            Vector3 side = {-radial.z, 0.0f, radial.x};
            float r = flower->bloomRadius;
            Vector3 p0 = {head.x + radial.x * r * 0.10f, head.y + 0.018f, head.z + radial.z * r * 0.10f};
            Vector3 p1 = {head.x + radial.x * r * 0.48f + side.x * r * 0.34f, head.y + r * 0.10f, head.z + radial.z * r * 0.48f + side.z * r * 0.34f};
            Vector3 p2 = {head.x + radial.x * r, head.y - r * 0.08f, head.z + radial.z * r};
            Vector3 p3 = {head.x + radial.x * r * 0.48f - side.x * r * 0.34f, head.y + r * 0.10f, head.z + radial.z * r * 0.48f - side.z * r * 0.34f};
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, (Vector3){0.0f, 1.0f, 0.0f},
                           phase, 1.0f, 1.0f, centerColor, flower->petalColor);
        }
        float c = flower->bloomRadius * 0.22f;
        Vector3 top = {head.x, head.y + c * 0.75f, head.z};
        Vector3 corners[4] = {
            {head.x - c, head.y, head.z - c}, {head.x + c, head.y, head.z - c},
            {head.x + c, head.y, head.z + c}, {head.x - c, head.y, head.z + c},
        };
        for (int face = 0; face < 4; face++) {
            Nature_SetVertex(&mesh, cursor++, top, (Vector3){0.0f, 1.0f, 0.0f}, phase, 1.0f, centerColor);
            Nature_SetVertex(&mesh, cursor++, corners[face], (Vector3){0.0f, 1.0f, 0.0f}, phase, 1.0f, centerColor);
            Nature_SetVertex(&mesh, cursor++, corners[(face + 1) & 3], (Vector3){0.0f, 1.0f, 0.0f}, phase, 1.0f, centerColor);
        }
    }
    field.model = Nature_ModelFromMesh(mesh, Nature_GetShader());
    field.ready = true;
    return field;
}

void MapProp_DrawFlowerField(const MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready) return;
    Nature_UpdateShader(time, windDirection, windStrength);
    rlDisableBackfaceCulling();
    DrawModel(field->model, worldOffset, 1.0f, WHITE);
    rlEnableBackfaceCulling();
}

void MapProp_UnloadFlowerField(MapFlowerField *field)
{
    if (!field || !field->ready) return;
    UnloadModel(field->model);
    field->ready = false;
}

static void Water_SetVertex(Mesh *mesh, int index, Vector3 p, Vector2 uv, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->texcoords[index * 2 + 0] = uv.x;
    mesh->texcoords[index * 2 + 1] = uv.y;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (config.radiusX <= 0.0f || config.radiusZ <= 0.0f) return water;
    if (config.segments < 24) config.segments = 24;
    if (config.segments > 192) config.segments = 192;
    if (config.rings < 2) config.rings = 2;
    if (config.rings > 32) config.rings = 32;
    water.config = config;

    Mesh lakeMesh = {0};
    int lakeVertices = config.segments * config.rings * 6;
    lakeMesh.vertexCount = lakeVertices;
    lakeMesh.triangleCount = lakeVertices / 3;
    lakeMesh.vertices = MemAlloc((unsigned int)lakeVertices * 3u * sizeof(float));
    lakeMesh.texcoords = MemAlloc((unsigned int)lakeVertices * 2u * sizeof(float));
    lakeMesh.colors = MemAlloc((unsigned int)lakeVertices * 4u * sizeof(unsigned char));
    int cursor = 0;
    for (int ring = 0; ring < config.rings; ring++) {
        float r0 = (float)ring / config.rings;
        float r1 = (float)(ring + 1) / config.rings;
        for (int segment = 0; segment < config.segments; segment++) {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            Vector3 p00 = {cosf(a0) * config.radiusX * r0, 0.0f, sinf(a0) * config.radiusZ * r0};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0, 0.0f, sinf(a1) * config.radiusZ * r0};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1, 0.0f, sinf(a0) * config.radiusZ * r1};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1, 0.0f, sinf(a1) * config.radiusZ * r1};
            Vector2 uv00 = {p00.x / config.radiusX * 0.5f + 0.5f, p00.z / config.radiusZ * 0.5f + 0.5f};
            Vector2 uv01 = {p01.x / config.radiusX * 0.5f + 0.5f, p01.z / config.radiusZ * 0.5f + 0.5f};
            Vector2 uv10 = {p10.x / config.radiusX * 0.5f + 0.5f, p10.z / config.radiusZ * 0.5f + 0.5f};
            Vector2 uv11 = {p11.x / config.radiusX * 0.5f + 0.5f, p11.z / config.radiusZ * 0.5f + 0.5f};
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p10, uv10, WHITE);
        }
    }
    water.waterModel = Nature_ModelFromMesh(lakeMesh, Water_GetShader());

    Mesh bankMesh = Nature_AllocMesh(config.segments * 6);
    cursor = 0;
    for (int segment = 0; segment < config.segments; segment++) {
        float a0 = (float)segment * 2.0f * PI / config.segments;
        float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
        float noise0 = 1.0f + 0.035f * sinf(a0 * 7.0f + (float)config.seed * 0.013f);
        float noise1 = 1.0f + 0.035f * sinf(a1 * 7.0f + (float)config.seed * 0.013f);
        float outerX = config.radiusX + config.bankWidth;
        float outerZ = config.radiusZ + config.bankWidth * 0.72f;
        Vector3 i0 = {cosf(a0) * config.radiusX * 0.985f, -0.018f, sinf(a0) * config.radiusZ * 0.985f};
        Vector3 i1 = {cosf(a1) * config.radiusX * 0.985f, -0.018f, sinf(a1) * config.radiusZ * 0.985f};
        Vector3 o0 = {cosf(a0) * outerX * noise0, -0.035f, sinf(a0) * outerZ * noise0};
        Vector3 o1 = {cosf(a1) * outerX * noise1, -0.035f, sinf(a1) * outerZ * noise1};
        Nature_AddQuad(&bankMesh, &cursor, i0, i1, o1, o0, (Vector3){0.0f, 1.0f, 0.0f},
                       0.0f, 0.0f, 0.0f, config.bankInnerColor, config.bankOuterColor);
    }
    water.bankModel = Nature_ModelFromMesh(bankMesh, Nature_GetShader());
    water.ready = true;
    return water;
}

void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready) return;
    Vector3 position = water->config.center;
    Nature_UpdateShader(time, (Vector2){0.0f, 0.0f}, 0.0f);
    rlDisableBackfaceCulling();
    DrawModel(water->bankModel, position, 1.0f, WHITE);

    Shader shader = Water_GetShader();
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector4 foam = ColorNormalize(water->config.foamColor);
    Vector3 sunRgb = {sun.x, sun.y, sun.z};
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};
    Vector3 foamRgb = {foam.x, foam.y, foam.z};
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_waveHeight"), &water->config.waveHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_waveScale"), &water->config.waveScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_waveSpeed"), &water->config.waveSpeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightColor"), &sunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_ambientColor"), &ambientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_deepColor"), &deepRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_shallowColor"), &shallowRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_foamColor"), &foamRgb, SHADER_UNIFORM_VEC3);
    DrawModel(water->waterModel, position, 1.0f, WHITE);
    rlEnableBackfaceCulling();
}

void MapProp_UnloadWaterSurface(MapWaterSurface *water)
{
    if (!water || !water->ready) return;
    UnloadModel(water->waterModel);
    UnloadModel(water->bankModel);
    water->ready = false;
}

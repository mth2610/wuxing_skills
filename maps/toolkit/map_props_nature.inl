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

static Color Nature_ScaleColor(Color color, float scale)
{
    int r = (int)(color.r * scale);
    int g = (int)(color.g * scale);
    int b = (int)(color.b * scale);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
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
        s_waterShader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(s_waterShader, "texture0");
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

static void Nature_AddQuad4(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                            Vector3 p2, Vector3 p3, Vector3 normal,
                            float phase, Color c0, Color c1, Color c2, Color c3)
{
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, 0.0f, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, 0.0f, c1);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, 0.0f, c2);
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, 0.0f, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, 0.0f, c2);
    Nature_SetVertex(mesh, (*cursor)++, p3, normal, phase, 0.0f, c3);
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

static Model Nature_BuildMeadowChunk(const MapMeadowPlacement *placements, int count,
                                     MapMeadowStyle style, float minX, float maxX,
                                     float minZ, float maxZ, int sampleStride,
                                     int bladesPerClump, int bladeSegments,
                                     float widthMultiplier, int *outPlacementCount)
{
    int selected = 0;
    int ordinal = 0;
    for (int i = 0; i < count; i++) {
        Vector3 p = placements[i].position;
        if (p.x < minX || p.x >= maxX || p.z < minZ || p.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) == 0)
            selected++;
    }
    if (outPlacementCount) *outPlacementCount = selected;
    if (selected <= 0) return (Model){0};

    int vertexCount = selected * bladesPerClump * bladeSegments * 6;
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    const float golden = 2.39996323f;
    ordinal = 0;
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) != 0)
            continue;
        for (int blade = 0; blade < bladesPerClump; blade++) {
            float angle = clump->rotationDeg * DEG2RAD + blade * golden;
            float radial = clump->radius * (0.12f + 0.72f * (float)(blade % 3) / 2.0f);
            float bx = clump->position.x + cosf(angle * 1.73f) * radial;
            float bz = clump->position.z + sinf(angle * 1.73f) * radial;
            float height = clump->height * (0.74f + 0.26f * sinf((float)(i * 13 + blade * 7)) * 0.5f + 0.13f);
            // Slightly widen alternating blades. Sub-pixel-thin vegetation
            // aliases into black needles at gameplay camera distance.
            float width = clump->radius * style.bladeWidthScale * widthMultiplier
                        * (0.88f + 0.20f * (float)(blade & 1));
            Vector3 side = {-sinf(angle), 0.0f, cosf(angle)};
            Vector3 facing = {cosf(angle), 0.12f, sinf(angle)};
            for (int segment = 0; segment < bladeSegments; segment++) {
                float t0 = (float)segment / bladeSegments;
                float t1 = (float)(segment + 1) / bladeSegments;
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
    return Nature_ModelFromMesh(mesh, Nature_GetShader());
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
    if (style.chunkSize <= 0.0f) style.chunkSize = 12.0f;

    float minX = placements[0].position.x;
    float maxX = minX;
    float minZ = placements[0].position.z;
    float maxZ = minZ;
    for (int i = 1; i < count; i++) {
        if (placements[i].position.x < minX) minX = placements[i].position.x;
        if (placements[i].position.x > maxX) maxX = placements[i].position.x;
        if (placements[i].position.z < minZ) minZ = placements[i].position.z;
        if (placements[i].position.z > maxZ) maxZ = placements[i].position.z;
    }
    maxX += 0.001f;
    maxZ += 0.001f;
    int columns = (int)ceilf((maxX - minX) / style.chunkSize);
    int rows = (int)ceilf((maxZ - minZ) / style.chunkSize);
    if (columns < 1) columns = 1;
    if (rows < 1) rows = 1;
    int capacity = columns * rows;
    meadow.chunks = MemAlloc((unsigned int)capacity * sizeof(MapMeadowChunk));
    meadow.lodDistance = style.lodDistance;
    meadow.drawDistance = style.drawDistance;

    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            float x0 = minX + column * style.chunkSize;
            float x1 = x0 + style.chunkSize;
            float z0 = minZ + row * style.chunkSize;
            float z1 = z0 + style.chunkSize;
            int nearCount = 0;
            Model nearModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                style.bladesPerClump, style.bladeSegments, 1.0f, &nearCount);
            if (nearCount <= 0)
                continue;

            int farBlades = style.bladesPerClump / 2;
            if (farBlades < 2) farBlades = 2;
            int farCount = 0;
            Model farModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 2,
                farBlades, 1, 1.65f, &farCount);
            MapMeadowChunk *chunk = &meadow.chunks[meadow.chunkCount++];
            *chunk = (MapMeadowChunk){
                .nearModel = nearModel,
                .farModel = farModel,
                .center = {(x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f},
                .ready = true,
            };
        }
    }
    if (meadow.chunkCount <= 0) {
        MemFree(meadow.chunks);
        meadow.chunks = NULL;
        return meadow;
    }
    meadow.ready = true;
    return meadow;
}

void MapProp_DrawMeadow(const MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready) return;
    Nature_UpdateShader(time, windDirection, windStrength);
    rlDisableBackfaceCulling();
    float lodDistanceSq = meadow->lodDistance * meadow->lodDistance;
    float drawDistanceSq = meadow->drawDistance * meadow->drawDistance;
    for (int i = 0; i < meadow->chunkCount; i++) {
        const MapMeadowChunk *chunk = &meadow->chunks[i];
        float dx = camera.position.x - (chunk->center.x + worldOffset.x);
        float dz = camera.position.z - (chunk->center.z + worldOffset.z);
        float distanceSq = dx * dx + dz * dz;
        if (meadow->drawDistance > 0.0f && distanceSq > drawDistanceSq)
            continue;
        if (meadow->lodDistance > 0.0f && distanceSq > lodDistanceSq)
            DrawModel(chunk->farModel, worldOffset, 1.0f, WHITE);
        else
            DrawModel(chunk->nearModel, worldOffset, 1.0f, WHITE);
    }
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready) return;
    for (int i = 0; i < meadow->chunkCount; i++) {
        UnloadModel(meadow->chunks[i].nearModel);
        UnloadModel(meadow->chunks[i].farModel);
    }
    MemFree(meadow->chunks);
    meadow->chunks = NULL;
    meadow->chunkCount = 0;
    meadow->ready = false;
}

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor)
{
    MapFlowerField field = {0};
    if (!placements || count <= 0) return field;
    int vertexCount = 0;
    for (int i = 0; i < count; i++) {
        int petals = placements[i].petalCount ? placements[i].petalCount : 5;
        if (petals < 4) petals = 4;
        if (petals > 6) petals = 6;
        vertexCount += 30 + petals * 6; // stems + one leaf + petals + center pyramid
    }
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        float stemWidth = fmaxf(0.018f, flower->bloomRadius * 0.22f);
        Vector3 base = flower->position;
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 4.1f;
        float lean = h * (0.025f + 0.055f * (0.5f + 0.5f * sinf((float)i * 2.37f)));
        Vector3 head = {base.x + cosf(leanAngle) * lean, base.y + h,
                        base.z + sinf(leanAngle) * lean};
        float headTilt = 0.24f + 0.28f * (0.5f + 0.5f * sinf((float)i * 1.91f + phase));
        float tiltX = cosf(leanAngle) * headTilt;
        float tiltZ = sinf(leanAngle) * headTilt;
        Vector3 bloomNormal = {-tiltX, 1.0f, -tiltZ};
        for (int cross = 0; cross < 2; cross++) {
            float angle = flower->rotationDeg * DEG2RAD + cross * PI * 0.5f;
            Vector3 side = {cosf(angle) * stemWidth, 0.0f, sinf(angle) * stemWidth};
            Vector3 normal = {-sinf(angle), 0.08f, cosf(angle)};
            Vector3 p0 = {base.x - side.x, base.y, base.z - side.z};
            Vector3 p1 = {base.x + side.x, base.y, base.z + side.z};
            Vector3 p2 = {head.x + side.x * 0.45f, head.y, head.z + side.z * 0.45f};
            Vector3 p3 = {head.x - side.x * 0.45f, head.y, head.z - side.z * 0.45f};
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, normal, phase, 0.0f, 1.0f, stemColor, stemColor);
        }
        {
            float leafAngle = flower->rotationDeg * DEG2RAD + 1.37f;
            Vector3 leafDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 leafSide = {-leafDir.z, 0.0f, leafDir.x};
            float leafLength = flower->bloomRadius * 0.88f;
            float leafWidth = leafLength * 0.19f;
            Vector3 root = {
                base.x + (head.x - base.x) * 0.43f,
                base.y + h * 0.43f,
                base.z + (head.z - base.z) * 0.43f,
            };
            Vector3 leafTip = {root.x + leafDir.x * leafLength, root.y + leafLength * 0.32f,
                               root.z + leafDir.z * leafLength};
            Vector3 l0 = {root.x - leafSide.x * leafWidth, root.y, root.z - leafSide.z * leafWidth};
            Vector3 l1 = {root.x + leafSide.x * leafWidth, root.y, root.z + leafSide.z * leafWidth};
            Vector3 l2 = {leafTip.x + leafSide.x * leafWidth * 0.10f, leafTip.y,
                          leafTip.z + leafSide.z * leafWidth * 0.10f};
            Vector3 l3 = {leafTip.x - leafSide.x * leafWidth * 0.10f, leafTip.y,
                          leafTip.z - leafSide.z * leafWidth * 0.10f};
            Nature_AddQuad(&mesh, &cursor, l0, l1, l2, l3,
                           (Vector3){-leafDir.x * 0.25f, 1.0f, -leafDir.z * 0.25f},
                           phase, 0.35f, 0.72f, stemColor, Nature_ScaleColor(stemColor, 1.12f));
        }
        int petalCount = flower->petalCount ? flower->petalCount : 5;
        if (petalCount < 4) petalCount = 4;
        if (petalCount > 6) petalCount = 6;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        for (int petal = 0; petal < petalCount; petal++) {
            float angle = flower->rotationDeg * DEG2RAD + (float)petal * 2.0f * PI / petalCount;
            Vector3 radial = {cosf(angle), 0.0f, sinf(angle)};
            Vector3 side = {-radial.z, 0.0f, radial.x};
            float irregularity = 0.92f + 0.08f * sinf((float)(i * 17 + petal * 11));
            float r = flower->bloomRadius * petalScale * irregularity;
            float cup = r * (0.035f + 0.055f * sinf((float)(i * 13 + petal * 7)));
            float tipDrop = r * (0.07f + 0.10f * (0.5f + 0.5f * sinf((float)(i * 5 + petal * 19))));
            float ox0 = radial.x * r * 0.10f;
            float oz0 = radial.z * r * 0.10f;
            float ox1 = radial.x * r * 0.48f + side.x * r * 0.29f;
            float oz1 = radial.z * r * 0.48f + side.z * r * 0.29f;
            float ox2 = radial.x * r;
            float oz2 = radial.z * r;
            float ox3 = radial.x * r * 0.48f - side.x * r * 0.29f;
            float oz3 = radial.z * r * 0.48f - side.z * r * 0.29f;
            Vector3 p0 = {head.x + ox0, head.y + 0.018f + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
            Vector3 p1 = {head.x + ox1, head.y + cup + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
            Vector3 p2 = {head.x + ox2, head.y - tipDrop + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
            Vector3 p3 = {head.x + ox3, head.y + cup * 0.72f + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
            Vector3 petalNormal = {bloomNormal.x - radial.x * 0.12f,
                                   bloomNormal.y, bloomNormal.z - radial.z * 0.12f};
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, petalNormal,
                           phase, 1.0f, 1.0f, flower->petalColor, flower->petalColor);
        }
        float c = flower->bloomRadius * 0.17f;
        Vector3 top = {head.x, head.y + c * 0.75f, head.z};
        Vector3 corners[4] = {
            {head.x - c, head.y - tiltX * c - tiltZ * c, head.z - c},
            {head.x + c, head.y + tiltX * c - tiltZ * c, head.z - c},
            {head.x + c, head.y + tiltX * c + tiltZ * c, head.z + c},
            {head.x - c, head.y - tiltX * c + tiltZ * c, head.z + c},
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

static float Water_EdgeScale(float angle, float radial, unsigned int seed)
{
    float seedPhase = (float)(seed & 1023u) * 0.0173f;
    float shorelineNoise = sinf(angle * 5.0f + seedPhase) * 0.024f
                         + sinf(angle * 11.0f - seedPhase * 0.63f) * 0.013f
                         + sinf(angle * 17.0f + 1.7f) * 0.006f;
    float edgeWeight = radial * radial;
    edgeWeight *= edgeWeight;
    return 1.0f + shorelineNoise * edgeWeight;
}

MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (config.radiusX <= 0.0f || config.radiusZ <= 0.0f) return water;
    if (config.segments < 24) config.segments = 24;
    if (config.segments > 192) config.segments = 192;
    if (config.rings < 2) config.rings = 2;
    if (config.rings > 32) config.rings = 32;
    if (config.detailScale <= 0.0f) config.detailScale = 0.18f;
    if (config.detailStrength < 0.0f) config.detailStrength = 0.0f;
    if (config.detailStrength > 0.24f) config.detailStrength = 0.24f;
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
            float e00 = Water_EdgeScale(a0, r0, config.seed);
            float e01 = Water_EdgeScale(a1, r0, config.seed);
            float e10 = Water_EdgeScale(a0, r1, config.seed);
            float e11 = Water_EdgeScale(a1, r1, config.seed);
            Vector3 p00 = {cosf(a0) * config.radiusX * r0 * e00, 0.0f, sinf(a0) * config.radiusZ * r0 * e00};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0 * e01, 0.0f, sinf(a1) * config.radiusZ * r0 * e01};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1 * e10, 0.0f, sinf(a0) * config.radiusZ * r1 * e10};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1 * e11, 0.0f, sinf(a1) * config.radiusZ * r1 * e11};
            // Keep radial UVs idealized while geometry meanders, so depth
            // grading and foam remain locked exactly to the visible edge.
            Vector2 uv00 = {cosf(a0) * r0 * 0.5f + 0.5f, sinf(a0) * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {cosf(a1) * r0 * 0.5f + 0.5f, sinf(a1) * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {cosf(a0) * r1 * 0.5f + 0.5f, sinf(a0) * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {cosf(a1) * r1 * 0.5f + 0.5f, sinf(a1) * r1 * 0.5f + 0.5f};
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p10, uv10, WHITE);
        }
    }
    water.waterModel = Nature_ModelFromMesh(lakeMesh, Water_GetShader());
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    const int bankRings = 3;
    Mesh bankMesh = Nature_AllocMesh(config.segments * bankRings * 6);
    cursor = 0;
    float bankOuterY = config.bankGroundY - config.center.y;
    for (int ring = 0; ring < bankRings; ring++) {
        float t0 = (float)ring / bankRings;
        float t1 = (float)(ring + 1) / bankRings;
        float s0 = t0 * t0 * (3.0f - 2.0f * t0);
        float s1 = t1 * t1 * (3.0f - 2.0f * t1);
        Color c0 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s0);
        Color c1 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s1);
        for (int segment = 0; segment < config.segments; segment++) {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float inner0 = Water_EdgeScale(a0, 1.0f, config.seed);
            float inner1 = Water_EdgeScale(a1, 1.0f, config.seed);
            float seedPhase = (float)(config.seed & 1023u) * 0.0173f;
            float habitat0 = 0.5f + 0.34f * sinf(a0 * 4.0f + seedPhase)
                                   + 0.16f * sinf(a0 * 9.0f - seedPhase * 0.61f);
            float habitat1 = 0.5f + 0.34f * sinf(a1 * 4.0f + seedPhase)
                                   + 0.16f * sinf(a1 * 9.0f - seedPhase * 0.61f);
            habitat0 = fmaxf(0.0f, fminf(1.0f, habitat0));
            habitat1 = fmaxf(0.0f, fminf(1.0f, habitat1));
            float width0 = config.bankWidth * (0.10f + 1.04f * habitat0 * habitat0);
            float width1 = config.bankWidth * (0.10f + 1.04f * habitat1 * habitat1);
            float rx00 = config.radiusX * 0.985f * inner0 + width0 * s0;
            float rz00 = config.radiusZ * 0.985f * inner0 + width0 * 0.72f * s0;
            float rx01 = config.radiusX * 0.985f * inner1 + width1 * s0;
            float rz01 = config.radiusZ * 0.985f * inner1 + width1 * 0.72f * s0;
            float rx10 = config.radiusX * 0.985f * inner0 + width0 * s1;
            float rz10 = config.radiusZ * 0.985f * inner0 + width0 * 0.72f * s1;
            float rx11 = config.radiusX * 0.985f * inner1 + width1 * s1;
            float rz11 = config.radiusZ * 0.985f * inner1 + width1 * 0.72f * s1;
            float y0 = -0.018f + (bankOuterY + 0.018f) * s0;
            float y1 = -0.018f + (bankOuterY + 0.018f) * s1;
            Vector3 p00 = {cosf(a0) * rx00, y0, sinf(a0) * rz00};
            Vector3 p01 = {cosf(a1) * rx01, y0, sinf(a1) * rz01};
            Vector3 p10 = {cosf(a0) * rx10, y1, sinf(a0) * rz10};
            Vector3 p11 = {cosf(a1) * rx11, y1, sinf(a1) * rz11};
            float shade0 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a0 * 7.0f - seedPhase * 0.7f));
            float shade1 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a1 * 7.0f - seedPhase * 0.7f));
            Nature_AddQuad4(&bankMesh, &cursor, p00, p01, p11, p10,
                            (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
                            Nature_ScaleColor(c0, shade0), Nature_ScaleColor(c0, shade1),
                            Nature_ScaleColor(c1, shade1), Nature_ScaleColor(c1, shade0));
        }
    }
    water.bankModel = Nature_ModelFromMesh(bankMesh, Nature_GetShader());
    water.ready = true;
    return water;
}

Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale)
{
    if (!water)
        return (Vector3){0};
    float edge = Water_EdgeScale(angleRad, 1.0f, water->config.seed);
    return (Vector3){
        water->config.center.x + cosf(angleRad) * water->config.radiusX * radialScale * edge,
        water->config.center.y,
        water->config.center.z + sinf(angleRad) * water->config.radiusZ * radialScale * edge,
    };
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
    SetShaderValue(shader, GetShaderLocation(shader, "u_detailScale"), &water->config.detailScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_detailStrength"), &water->config.detailStrength, SHADER_UNIFORM_FLOAT);
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

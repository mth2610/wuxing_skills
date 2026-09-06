// Batched, shader-driven meadow/flower/water surfaces. All geometry is built
// once at map init and submitted as a small number of opaque draw calls.

static Shader s_natureOpaqueShader = {0};
static Shader s_natureCutoutShader = {0};
static Shader s_natureShadowShader = {0};
static Shader s_flowerShadowShader = {0};
static Shader s_waterShader = {0};
static bool s_natureOpaqueShaderReady = false;
static bool s_natureCutoutShaderReady = false;
static bool s_natureShadowShaderReady = false;
static bool s_flowerShadowShaderReady = false;
static bool s_waterShaderReady = false;

#define NATURE_INTERACTION_RESOLUTION 64
#define NATURE_INTERACTION_PIXEL_COUNT \
    (NATURE_INTERACTION_RESOLUTION * NATURE_INTERACTION_RESOLUTION)
static const float kNatureInteractionWorldSize = 18.0f;
static const float kNatureInteractionMaxBend = 0.55f;
static Texture2D s_natureInteractionTexture = {0};
static Color s_natureInteractionPixels[NATURE_INTERACTION_PIXEL_COUNT];
static Color s_natureInteractionScratch[NATURE_INTERACTION_PIXEL_COUNT];
static Vector2 s_natureInteractionCenter = {0};
static bool s_natureInteractionReady = false;
static bool s_natureInteractionOpen = false;
static MapNatureRenderStats s_natureRenderStats = {0};

// Runtime A/B switch for validating the two vegetation-shadow layers without
// rebuilding a map.  The default is the production hybrid path; the explicit
// modes are intentionally diagnostics and are shared by every map using this
// toolkit.
typedef enum NatureShadowMode {
    NATURE_SHADOW_HYBRID = 0,
    NATURE_SHADOW_REAL_ONLY,
    NATURE_SHADOW_PROJECTED_ONLY,
} NatureShadowMode;

static NatureShadowMode Nature_GetShadowMode(void)
{
    const char *mode = getenv("WUXING_NATURE_SHADOW_MODE");
    if (mode != NULL && (mode[0] == 'r' || mode[0] == 'R'))
        return NATURE_SHADOW_REAL_ONLY;
    if (mode != NULL && (mode[0] == 'p' || mode[0] == 'P'))
        return NATURE_SHADOW_PROJECTED_ONLY;
    return NATURE_SHADOW_HYBRID;
}

static bool Nature_ShadowCasterTypeEnabled(bool flower)
{
    const char *filter = getenv("WUXING_NATURE_SHADOW_CASTERS");
    if (filter == NULL || filter[0] == '\0' || filter[0] == 'a' || filter[0] == 'A')
        return true;
    if (filter[0] == 'f' || filter[0] == 'F')
        return flower;
    if (filter[0] == 'm' || filter[0] == 'M' || filter[0] == 'g' || filter[0] == 'G')
        return !flower;
    return true;
}

static bool Nature_ShadowCasterFilterActive(void)
{
    const char *filter = getenv("WUXING_NATURE_SHADOW_CASTERS");
    return filter != NULL && filter[0] != '\0' &&
           filter[0] != 'a' && filter[0] != 'A';
}

void MapProp_ResetNatureRenderStats(void)
{
    s_natureRenderStats = (MapNatureRenderStats){0};
}

MapNatureRenderStats MapProp_GetNatureRenderStats(void)
{
    return s_natureRenderStats;
}

static Color Nature_EmptyInteractionPixel(void)
{
    return (Color){128, 128, 0, 255};
}

static void Nature_InitInteraction(void)
{
    if (s_natureInteractionReady)
        return;
    Color empty = Nature_EmptyInteractionPixel();
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = empty;
    Image image = GenImageColor(NATURE_INTERACTION_RESOLUTION,
                                NATURE_INTERACTION_RESOLUTION, empty);
    s_natureInteractionTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(s_natureInteractionTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_natureInteractionTexture, TEXTURE_WRAP_CLAMP);
    s_natureInteractionReady = s_natureInteractionTexture.id != 0;
}

static void Nature_ScrollAndDecayInteraction(Vector2 newCenter, float dt)
{
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    int shiftX = (int)roundf((newCenter.x - s_natureInteractionCenter.x) / cellSize);
    int shiftY = (int)roundf((newCenter.y - s_natureInteractionCenter.y) / cellSize);
    float decay = expf(-fmaxf(dt, 0.0f) * 2.15f);
    Color empty = Nature_EmptyInteractionPixel();

    for (int y = 0; y < NATURE_INTERACTION_RESOLUTION; y++) {
        for (int x = 0; x < NATURE_INTERACTION_RESOLUTION; x++) {
            int sourceX = x + shiftX;
            int sourceY = y + shiftY;
            Color value = empty;
            if (sourceX >= 0 && sourceX < NATURE_INTERACTION_RESOLUTION &&
                sourceY >= 0 && sourceY < NATURE_INTERACTION_RESOLUTION) {
                value = s_natureInteractionPixels[sourceY * NATURE_INTERACTION_RESOLUTION + sourceX];
                value.b = (unsigned char)((float)value.b * decay);
                if (value.b < 2)
                    value = empty;
            }
            s_natureInteractionScratch[y * NATURE_INTERACTION_RESOLUTION + x] = value;
        }
    }
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = s_natureInteractionScratch[i];
    s_natureInteractionCenter = newCenter;
}

void MapProp_BeginNatureInteraction(Vector3 focus, float dt)
{
    Nature_InitInteraction();
    if (!s_natureInteractionReady)
        return;
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    Vector2 snappedCenter = {
        roundf(focus.x / cellSize) * cellSize,
        roundf(focus.z / cellSize) * cellSize,
    };
    Nature_ScrollAndDecayInteraction(snappedCenter, dt);
    s_natureInteractionOpen = true;
}

void MapProp_AddNatureInteractor(Vector3 position, float radius, float strength)
{
    if (!s_natureInteractionOpen || radius <= 0.0f || strength <= 0.0f)
        return;
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    float halfSize = kNatureInteractionWorldSize * 0.5f;
    int minX = (int)floorf((position.x - radius - (s_natureInteractionCenter.x - halfSize)) / cellSize);
    int maxX = (int)ceilf((position.x + radius - (s_natureInteractionCenter.x - halfSize)) / cellSize);
    int minY = (int)floorf((position.z - radius - (s_natureInteractionCenter.y - halfSize)) / cellSize);
    int maxY = (int)ceilf((position.z + radius - (s_natureInteractionCenter.y - halfSize)) / cellSize);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= NATURE_INTERACTION_RESOLUTION) maxX = NATURE_INTERACTION_RESOLUTION - 1;
    if (maxY >= NATURE_INTERACTION_RESOLUTION) maxY = NATURE_INTERACTION_RESOLUTION - 1;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float worldX = s_natureInteractionCenter.x - halfSize + (x + 0.5f) * cellSize;
            float worldZ = s_natureInteractionCenter.y - halfSize + (y + 0.5f) * cellSize;
            float dx = worldX - position.x;
            float dz = worldZ - position.z;
            float distance = sqrtf(dx * dx + dz * dz);
            if (distance >= radius)
                continue;
            float falloff = 1.0f - distance / radius;
            falloff *= falloff;
            float bend = fminf(strength / kNatureInteractionMaxBend, 1.0f) * falloff;
            Color *pixel = &s_natureInteractionPixels[y * NATURE_INTERACTION_RESOLUTION + x];
            unsigned char encodedBend = (unsigned char)(bend * 255.0f);
            if (encodedBend <= pixel->b)
                continue;
            float inverseDistance = distance > 0.0001f ? 1.0f / distance : 0.0f;
            pixel->r = (unsigned char)((dx * inverseDistance * 0.5f + 0.5f) * 255.0f);
            pixel->g = (unsigned char)((dz * inverseDistance * 0.5f + 0.5f) * 255.0f);
            pixel->b = encodedBend;
            pixel->a = 255;
        }
    }
}

void MapProp_EndNatureInteraction(void)
{
    if (!s_natureInteractionOpen || !s_natureInteractionReady)
        return;
    UpdateTexture(s_natureInteractionTexture, s_natureInteractionPixels);
    s_natureInteractionOpen = false;
}

void MapProp_ClearNatureInteraction(void)
{
    Color empty = Nature_EmptyInteractionPixel();
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = empty;
    if (s_natureInteractionReady)
        UnloadTexture(s_natureInteractionTexture);
    s_natureInteractionTexture = (Texture2D){0};
    s_natureInteractionCenter = (Vector2){0};
    s_natureInteractionReady = false;
    s_natureInteractionOpen = false;
}

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

static Shader Nature_GetShader(bool cutout)
{
    Shader *shader = cutout ? &s_natureCutoutShader : &s_natureOpaqueShader;
    bool *ready = cutout ? &s_natureCutoutShaderReady : &s_natureOpaqueShaderReady;
    if (!*ready) {
        *shader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/nature_lit.vs",
            cutout ? "maps/toolkit/shaders/nature_lit.fs"
                   : "maps/toolkit/shaders/nature_opaque.fs");
        shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
        shader->locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(*shader, "vertexTexCoord");
        shader->locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(*shader, "vertexTexCoord2");
        shader->locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(*shader, "vertexNormal");
        shader->locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(*shader, "vertexColor");
        shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
        shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
        shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
        shader->locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(*shader, "texture0");
        MapShadow_ConfigureShader(*shader);
        VFXLight_RegisterShader(*shader);
        *ready = true;
    }
    return *shader;
}

static Shader NatureShadow_GetShader(void)
{
    if (!s_natureShadowShaderReady) {
        s_natureShadowShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/nature_shadow.vs",
            "maps/toolkit/shaders/nature_shadow.fs");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_POSITION] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexPosition");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexTexCoord");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD02] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexTexCoord2");
        s_natureShadowShader.locs[SHADER_LOC_MATRIX_MVP] =
            GetShaderLocation(s_natureShadowShader, "mvp");
        s_natureShadowShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(s_natureShadowShader, "matModel");
        s_natureShadowShader.locs[SHADER_LOC_MAP_DIFFUSE] =
            GetShaderLocation(s_natureShadowShader, "texture0");
        s_natureShadowShaderReady = true;
    }
    return s_natureShadowShader;
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

static Shader FlowerShadow_GetShader(void)
{
    if (!s_flowerShadowShaderReady) {
        s_flowerShadowShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/flower_shadow.vs",
            "maps/toolkit/shaders/flower_shadow.fs");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_POSITION] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexPosition");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexTexCoord");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_NORMAL] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexNormal");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_COLOR] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexColor");
        s_flowerShadowShader.locs[SHADER_LOC_MATRIX_MVP] =
            GetShaderLocation(s_flowerShadowShader, "mvp");
        s_flowerShadowShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(s_flowerShadowShader, "matModel");
        s_flowerShadowShaderReady = true;
    }
    return s_flowerShadowShader;
}

static void Nature_UpdateProjectedShadowShader(Shader shader, bool realShadowActive)
{
    Vector3 lightTravel = Environment_GetSunDirection();
    Vector4 shadowColor = ColorNormalize(Environment_GetShadowColor());
    Vector3 shadowTint = {shadowColor.x, shadowColor.y, shadowColor.z};
    // With a real directional map this mesh is a short grounding/contact layer.
    // Keep it visibly shorter than the true animated silhouette, but broad and
    // rounded enough to survive a high gameplay camera and the grass texture.
    // MED and SHADOW OFF retain the full inexpensive fallback.
    // Hybrid contact is root occlusion, not a second directional silhouette.
    // Keep it short, broad and restrained so the real animated shadow owns the
    // readable shape. SHADOW OFF retains a softer projected fallback.
    float projectionScale = realShadowActive ? 0.10f : 0.62f;
    float widthScale = realShadowActive ? 1.15f : 1.0f;
    float tipWidth = realShadowActive ? 0.82f : 0.60f;
    float shadowStrength = realShadowActive ? 0.32f : 0.68f;
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightTravel"),
                   &lightTravel, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_shadowTint"),
                   &shadowTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_projectionScale"),
                   &projectionScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_widthScale"),
                   &widthScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_tipWidth"),
                   &tipWidth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_shadowStrength"),
                   &shadowStrength, SHADER_UNIFORM_FLOAT);
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
    mesh->texcoords2[index * 2 + 0] = -1.0f;
    mesh->texcoords2[index * 2 + 1] = heightMask;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

static void Nature_SetUv2(Mesh *mesh, int index, float u, float v)
{
    mesh->texcoords2[index * 2 + 0] = u;
    mesh->texcoords2[index * 2 + 1] = v;
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

static void Nature_AddTexturedQuad(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                   Vector3 p2, Vector3 p3, Vector3 normal,
                                   float phase, float h0, float h1, Color c0, Color c1)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal, phase, h0, h1, c0, c1);
    Nature_SetUv2(mesh, base + 0, 0.0f, h0);
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 1.0f, h1);
    Nature_SetUv2(mesh, base + 3, 0.0f, h0);
    Nature_SetUv2(mesh, base + 4, 1.0f, h1);
    Nature_SetUv2(mesh, base + 5, 0.0f, h1);
}

static void Nature_AddTexturedBloom(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                    Vector3 p2, Vector3 p3, Vector3 normal,
                                    float phase, Color color, Vector4 uvRect)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal,
                   phase, 1.0f, 1.0f, color, color);
    Nature_SetUv2(mesh, base + 0, uvRect.x, uvRect.y);
    Nature_SetUv2(mesh, base + 1, uvRect.z, uvRect.y);
    Nature_SetUv2(mesh, base + 2, uvRect.z, uvRect.w);
    Nature_SetUv2(mesh, base + 3, uvRect.x, uvRect.y);
    Nature_SetUv2(mesh, base + 4, uvRect.z, uvRect.w);
    Nature_SetUv2(mesh, base + 5, uvRect.x, uvRect.w);
}

static void Nature_AddPointedBladeTip(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                      Vector3 tip, Vector3 normal, float phase,
                                      float h0, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, tip, normal, phase, 1.0f, c1);
    Nature_SetUv2(mesh, base + 0, 0.0f, h0);
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 0.5f, 1.0f);
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
    mesh.texcoords2 = MemAlloc((unsigned int)vertexCount * 2u * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)vertexCount * 4u * sizeof(unsigned char));
    return mesh;
}

static Model Nature_ModelFromMesh(Mesh mesh, Shader shader);

static Model Nature_BuildFlowerShadowModel(const MapFlowerPlacement *placements, int count)
{
    Mesh mesh = Nature_AllocMesh(count * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float width = fmaxf(flower->bloomRadius * 0.42f, 0.014f);
        Vector3 root = flower->position;
        root.y += 0.0015f;
        Vector3 encoded = {flower->height, width, flower->phase};
        Color rootShade = {210, 210, 210, 255};
        Color tipShade = {150, 150, 150, 255};
        const Vector2 uv[6] = {
            {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f},
        };
        for (int vertex = 0; vertex < 6; vertex++) {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, flower->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

static Model Nature_BuildMeadowShadowChunk(const MapMeadowPlacement *placements, int count,
                                           float minX, float maxX, float minZ, float maxZ)
{
    int selected = 0;
    for (int i = 0; i < count; i++) {
        Vector3 p = placements[i].position;
        if (p.x >= minX && p.x < maxX && p.z >= minZ && p.z < maxZ)
            selected++;
    }
    if (selected <= 0) return (Model){0};

    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    const Vector2 uv[6] = {
        {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
        {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f},
    };
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        Vector3 root = clump->position;
        root.y += 0.0012f;
        float width = fmaxf(clump->radius * 0.52f, 0.022f);
        Vector3 encoded = {clump->height * 0.92f, width, clump->phase};
        Color rootShade = {200, 200, 200, 255};
        Color tipShade = {135, 135, 135, 255};
        for (int vertex = 0; vertex < 6; vertex++) {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, clump->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

static Model Nature_ModelFromMesh(Mesh mesh, Shader shader)
{
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;
    MapShadow_AttachMaterial(&model.materials[0]);
    return model;
}

static void Nature_UpdateShader(Shader shader, float time, Vector2 windDirection, float windStrength,
                                bool useTexture, float alphaCutoff)
{
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
    int textured = useTexture ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_useTexture"), &textured, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCutoff"), &alphaCutoff, SHADER_UNIFORM_FLOAT);
    int interactionEnabled = s_natureInteractionReady ? 1 : 0;
    Vector2 interactionCenter = s_natureInteractionCenter;
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionEnabled"),
                   &interactionEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionCenter"),
                   &interactionCenter, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionWorldSize"),
                   &kNatureInteractionWorldSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionMaxBend"),
                   &kNatureInteractionMaxBend, SHADER_UNIFORM_FLOAT);
    if (s_natureInteractionReady)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_interactionMap"),
                              s_natureInteractionTexture);
    MapShadow_UpdateShader(shader);
}

static void Nature_UpdateShadowShader(Shader shader, float time, Vector2 windDirection,
                                      float windStrength, bool useTexture, float alphaCutoff)
{
    float windLength = sqrtf(windDirection.x * windDirection.x + windDirection.y * windDirection.y);
    if (windLength > 0.0001f) {
        windDirection.x /= windLength;
        windDirection.y /= windLength;
    }
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"),
                   &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windDirection"),
                   &windDirection, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windStrength"),
                   &windStrength, SHADER_UNIFORM_FLOAT);
    int textured = useTexture ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_useTexture"),
                   &textured, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCutoff"),
                   &alphaCutoff, SHADER_UNIFORM_FLOAT);
    // Conservative alpha coverage is applied only in the shadow capture. It
    // keeps sub-pixel petal tips represented without changing the visible mesh
    // or replacing its authored atlas silhouette with a blob.
    float alphaCoverage = useTexture ? 0.75f : 0.0f;
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCoverage"),
                   &alphaCoverage, SHADER_UNIFORM_FLOAT);
    int interactionEnabled = s_natureInteractionReady ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionEnabled"),
                   &interactionEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionCenter"),
                   &s_natureInteractionCenter, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionWorldSize"),
                   &kNatureInteractionWorldSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionMaxBend"),
                   &kNatureInteractionMaxBend, SHADER_UNIFORM_FLOAT);
    if (s_natureInteractionReady)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_interactionMap"),
                              s_natureInteractionTexture);
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

    int verticesPerBlade = (bladeSegments - 1) * 6 + 3;
    int vertexCount = selected * bladesPerClump * verticesPerBlade;
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
                Color color0 = Nature_LerpColor(style.rootColor, style.tipColor, t0);
                Color color1 = Nature_LerpColor(style.rootColor, style.tipColor, t1);
                if (segment == bladeSegments - 1) {
                    Nature_AddPointedBladeTip(&mesh, &cursor, p0, p1, center1, facing,
                                              clump->phase, t0, color0, color1);
                } else {
                    Nature_AddTexturedQuad(&mesh, &cursor, p0, p1, p2, p3, facing,
                                           clump->phase, t0, t1, color0, color1);
                }
            }
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(style.texturePath != NULL));
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
    if (style.alphaCutoff <= 0.0f) style.alphaCutoff = 0.42f;
    if (style.alphaCutoff > 0.9f) style.alphaCutoff = 0.9f;
    if (style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_HIGH)
        (void)NatureShadow_GetShader();
    Texture2D foliageTexture = {0};
    bool textured = style.texturePath != NULL;
    if (textured) {
        foliageTexture = ResourceManager_LoadTexture(style.texturePath);
        GenTextureMipmaps(&foliageTexture);
        SetTextureFilter(foliageTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(foliageTexture, TEXTURE_WRAP_CLAMP);
    }

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
    meadow.shadowDistance = style.shadowDistance;
    bool buildContactShadows = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_MED;
    bool buildRealShadowLod = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_HIGH;

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

            // Preserve coverage: removing every second clump turns a meadow
            // into isolated spikes. Far LOD reduces each clump instead.
            int farBlades = (style.bladesPerClump + 1) / 2;
            if (farBlades < 2) farBlades = 2;
            int farCount = 0;
            Model farModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                farBlades, 1, 1.16f, &farCount);
            Model shadowModel = {0};
            if (buildContactShadows)
                shadowModel = Nature_BuildMeadowShadowChunk(
                    placements, count, x0, x1, z0, z1);
            // Shadow-only geometry keeps one of every two clumps and two real
            // pointed blades per survivor. Rendering the full near meadow into
            // a low-angle shadow map creates coherent parallel-line moire over
            // the entire terrain; this stable LOD preserves real silhouettes
            // while cutting depth geometry to roughly 13% of the near mesh.
            Model realShadowModel = {0};
            int realShadowCount = 0;
            if (buildRealShadowLod)
                realShadowModel = Nature_BuildMeadowChunk(
                    placements, count, style, x0, x1, z0, z1, 2,
                    2, 1, 1.0f, &realShadowCount);
            MapMeadowChunk *chunk = &meadow.chunks[meadow.chunkCount++];
            if (textured) {
                nearModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                if (realShadowCount > 0)
                    realShadowModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
            }
            *chunk = (MapMeadowChunk){
                .nearModel = nearModel,
                .farModel = farModel,
                .shadowModel = shadowModel,
                .realShadowModel = realShadowModel,
                .center = {(x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f},
                .radius = style.chunkSize * 0.72f + 1.5f,
                .shadowReady = shadowModel.meshCount > 0,
                .realShadowReady = realShadowCount > 0,
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
    meadow.textured = textured;
    meadow.alphaCutoff = style.alphaCutoff;
    return meadow;
}

static bool Nature_IsChunkVisible(Vector3 center, float radius)
{
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    float forwardLength = Vector3Length(forward);
    if (forwardLength < 0.001f)
        return true;
    forward = Vector3Scale(forward, 1.0f / forwardLength);
    Vector3 right = Vector3CrossProduct(forward, camera.up);
    float rightLength = Vector3Length(right);
    if (rightLength < 0.001f)
        return true;
    right = Vector3Scale(right, 1.0f / rightLength);
    Vector3 viewUp = Vector3Normalize(Vector3CrossProduct(right, forward));
    Vector3 toCenter = Vector3Subtract(center, camera.position);
    float depth = Vector3DotProduct(toCenter, forward);
    if (depth < -radius)
        return false;

    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    float halfVertical;
    float halfHorizontal;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        halfVertical = camera.fovy * 0.5f;
        halfHorizontal = halfVertical * aspect;
    } else {
        float positiveDepth = fmaxf(depth, 0.0f);
        halfVertical = tanf(camera.fovy * DEG2RAD * 0.5f) * positiveDepth;
        halfHorizontal = halfVertical * aspect;
    }
    float sideDistance = fabsf(Vector3DotProduct(toCenter, right));
    float verticalDistance = fabsf(Vector3DotProduct(toCenter, viewUp));
    return sideDistance <= halfHorizontal + radius &&
           verticalDistance <= halfVertical + radius;
}

// Shadow casters belong to the cascade, not to a camera-distance sphere. Keep
// this rejection deliberately conservative and backend-independent: applying
// the CPU matrix convention to a GPU-ready light VP caused valid vegetation to
// be rejected on Vulkan even though its real depth draw was otherwise correct.
static bool Nature_IntersectsDynamicShadowCoverage(Vector3 center, float radius)
{
    Vector3 focus = EnvShadow_GetFocus();
    Vector3 sun = Vector3Normalize(Environment_GetSunDirection());
    float extent = fmaxf(EnvShadow_GetHalfExtent(), 8.0f);
    // The tilted light-space vertical axis covers a wider ground footprint as
    // the sun gets lower. A horizontal bounding circle is intentionally wider
    // than the true oriented cascade, trading a few casters for zero holes.
    float groundRadius = extent / fmaxf(fabsf(sun.y), 0.32f);
    float dx = center.x - focus.x;
    float dz = center.z - focus.z;
    float limit = groundRadius + radius * 1.12f + 2.0f;
    return dx * dx + dz * dz <= limit * limit;
}

void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready) return;
    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH ? 1.0f
                     : quality == GFX_MED ? 0.84f
                     : quality == GFX_LOW ? 0.68f : 0.55f;
    float lodScale = quality >= GFX_HIGH ? 1.0f
                   : quality == GFX_MED ? 0.84f : 0.68f;
    float lodDistance = meadow->lodDistance * lodScale;
    float drawDistance = meadow->drawDistance * rangeScale;
    float drawDistanceSq = drawDistance * drawDistance;
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        chunk->visibleThisFrame = false;
        s_natureRenderStats.meadowChunksTested++;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        if (!Nature_IsChunkVisible(center, chunk->radius)) {
            s_natureRenderStats.meadowFrustumCulled++;
            continue;
        }
        float dx = camera.position.x - center.x;
        float dz = camera.position.z - center.z;
        float distanceSq = dx * dx + dz * dz;
        if (drawDistance > 0.0f && distanceSq > drawDistanceSq) {
            s_natureRenderStats.meadowDistanceCulled++;
            continue;
        }
        s_natureRenderStats.meadowChunksVisible++;
        chunk->visibleThisFrame = true;

        if (lodDistance > 0.0f) {
            float spatialHash = sinf(chunk->center.x * 12.9898f + chunk->center.z * 78.233f);
            spatialHash = spatialHash - floorf(spatialHash);
            float threshold = lodDistance + (spatialHash - 0.5f) * 4.0f;
            float hysteresis = quality >= GFX_HIGH ? 1.1f : 1.8f;
            float distance = sqrtf(distanceSq);
            if (chunk->farLod) {
                if (distance < threshold - hysteresis)
                    chunk->farLod = false;
            } else if (distance > threshold + hysteresis) {
                chunk->farLod = true;
            }
        } else {
            chunk->farLod = false;
        }

    }

    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_HIGH && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    if (useProjectedShadows && meadow->shadowDistance > 0.0f) {
        float shadowScale = quality >= GFX_HIGH ? 1.0f : 0.76f;
        float shadowDistance = meadow->shadowDistance * shadowScale;
        Shader shadowShader = FlowerShadow_GetShader();
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        for (int i = 0; i < meadow->chunkCount; i++) {
            MapMeadowChunk *chunk = &meadow->chunks[i];
            if (!chunk->visibleThisFrame || !chunk->shadowReady)
                continue;
            Vector3 center = Vector3Add(chunk->center, worldOffset);
            float dx = camera.position.x - center.x;
            float dz = camera.position.z - center.z;
            float visibleDistance = sqrtf(dx * dx + dz * dz) - chunk->radius;
            if (visibleDistance > shadowDistance) {
                s_natureRenderStats.meadowShadowDistanceCulled++;
                continue;
            }
            DrawModel(chunk->shadowModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowShadowDraws++;
        }
        EndBlendMode();
        rlEnableDepthMask();
    }

    Shader shader = Nature_GetShader(meadow->textured);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        meadow->textured, meadow->alphaCutoff);
    rlDisableBackfaceCulling();
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->visibleThisFrame)
            continue;
        if (chunk->farLod) {
            DrawModel(chunk->farModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowFarDraws++;
        } else {
            DrawModel(chunk->nearModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowNearDraws++;
        }
    }
    rlEnableBackfaceCulling();
}

void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready || GfxQuality_Get() < GFX_HIGH ||
        meadow->shadowDistance <= 0.0f ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(false))
        return;
    Shader shader = NatureShadow_GetShader();
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              meadow->textured, meadow->alphaCutoff);
    rlDisableBackfaceCulling();
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->realShadowReady)
            continue;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        if (!Nature_IntersectsDynamicShadowCoverage(center, chunk->radius) &&
            !Nature_ShadowCasterFilterActive())
            continue;
        Shader previous = chunk->realShadowModel.materials[0].shader;
        chunk->realShadowModel.materials[0].shader = shader;
        DrawModel(chunk->realShadowModel, worldOffset, 1.0f, WHITE);
        chunk->realShadowModel.materials[0].shader = previous;
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready) return;
    for (int i = 0; i < meadow->chunkCount; i++) {
        UnloadModel(meadow->chunks[i].nearModel);
        UnloadModel(meadow->chunks[i].farModel);
        if (meadow->chunks[i].shadowReady)
            UnloadModel(meadow->chunks[i].shadowModel);
        if (meadow->chunks[i].realShadowReady)
            UnloadModel(meadow->chunks[i].realShadowModel);
    }
    MemFree(meadow->chunks);
    meadow->chunks = NULL;
    meadow->chunkCount = 0;
    meadow->shadowDistance = 0.0f;
    meadow->ready = false;
}

static bool Nature_KeepFarFlower(const MapFlowerPlacement *flower)
{
    float hash = sinf(flower->position.x * 12.9898f + flower->position.z * 78.233f
                      + flower->phase * 37.719f) * 43758.5453f;
    return hash - floorf(hash) < 0.56f;
}

static Model Nature_BuildFlowerFarModel(const MapFlowerPlacement *placements, int count,
                                        bool texturedBloom,
                                        int atlasColumns, int atlasRows)
{
    // At distance, stems are sub-pixel and alpha-cutout bloom overdraw dominates.
    // Keep a stable world-space subset and slightly compensate bloom coverage.
    int selected = 0;
    for (int i = 0; i < count; i++) {
        if (Nature_KeepFarFlower(&placements[i]))
            selected++;
    }
    bool forceFirst = selected == 0;
    if (forceFirst) selected = 1;
    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        if (!Nature_KeepFarFlower(flower) && !(forceFirst && i == 0))
            continue;
        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 4.1f;
        float lean = h * (0.025f + 0.055f * (0.5f + 0.5f * sinf((float)i * 2.37f)));
        Vector3 head = {base.x + cosf(leanAngle) * lean, base.y + h,
                        base.z + sinf(leanAngle) * lean};
        float stemAngle = flower->rotationDeg * DEG2RAD;
        float headTilt = 0.24f + 0.28f * (0.5f + 0.5f * sinf((float)i * 1.91f + phase));
        float tiltX = cosf(leanAngle) * headTilt;
        float tiltZ = sinf(leanAngle) * headTilt;
        Vector3 bloomNormal = {-tiltX, 1.0f, -tiltZ};
        Vector3 right = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
        Vector3 forward = {-sinf(stemAngle), 0.0f, cosf(stemAngle)};
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float radius = flower->bloomRadius * petalScale * 1.08f * 1.12f;
        float ox0 = (-right.x - forward.x) * radius;
        float oz0 = (-right.z - forward.z) * radius;
        float ox1 = ( right.x - forward.x) * radius;
        float oz1 = ( right.z - forward.z) * radius;
        float ox2 = ( right.x + forward.x) * radius;
        float oz2 = ( right.z + forward.z) * radius;
        float ox3 = (-right.x + forward.x) * radius;
        float oz3 = (-right.z + forward.z) * radius;
        Vector3 p0 = {head.x + ox0, head.y + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
        Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
        Vector3 p2 = {head.x + ox2, head.y + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
        Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
        if (texturedBloom) {
            int variantCount = atlasColumns * atlasRows;
            int variant = flower->bloomVariant % variantCount;
            int column = variant % atlasColumns;
            int row = variant / atlasColumns;
            float insetU = 0.008f / atlasColumns;
            float insetV = 0.008f / atlasRows;
            Vector4 uvRect = {
                (float)column / atlasColumns + insetU,
                (float)row / atlasRows + insetV,
                (float)(column + 1) / atlasColumns - insetU,
                (float)(row + 1) / atlasRows - insetV,
            };
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);
        } else {
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                           phase, 1.0f, 1.0f, flower->petalColor, flower->petalColor);
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(texturedBloom));
}

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor,
                                         const char *petalTexturePath, float alphaCutoff,
                                         int atlasColumns, int atlasRows)
{
    MapFlowerField field = {0};
    if (!placements || count <= 0) return field;
    Vector3 boundsMin = placements[0].position;
    Vector3 boundsMax = placements[0].position;
    boundsMin.x -= placements[0].bloomRadius;
    boundsMin.z -= placements[0].bloomRadius;
    boundsMax.x += placements[0].bloomRadius;
    boundsMax.y += placements[0].height + placements[0].bloomRadius;
    boundsMax.z += placements[0].bloomRadius;
    bool texturedBloom = petalTexturePath != NULL;
    if (GfxQuality_Get() >= GFX_HIGH)
        (void)NatureShadow_GetShader();
    if (atlasColumns < 1) atlasColumns = 1;
    if (atlasRows < 1) atlasRows = 1;
    int vertexCount = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float extent = flower->bloomRadius * 1.15f;
        boundsMin.x = fminf(boundsMin.x, flower->position.x - extent);
        boundsMin.y = fminf(boundsMin.y, flower->position.y);
        boundsMin.z = fminf(boundsMin.z, flower->position.z - extent);
        boundsMax.x = fmaxf(boundsMax.x, flower->position.x + extent);
        boundsMax.y = fmaxf(boundsMax.y,
                            flower->position.y + flower->height + extent);
        boundsMax.z = fmaxf(boundsMax.z, flower->position.z + extent);
        int petals = placements[i].petalCount ? placements[i].petalCount : 5;
        if (petals < 4) petals = 4;
        if (petals > 6) petals = 6;
        // Two foliage leaves (12 vertices) keep a close flower from reading as
        // a colored disc on a bare pole. Bloom/center counts remain unchanged.
        vertexCount += texturedBloom ? 42 : 36 + petals * 6;
    }
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        float stemWidth = fmaxf(0.007f, flower->bloomRadius * 0.16f);
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
        for (int leaf = 0; leaf < 2; leaf++) {
            float rootT = leaf == 0 ? 0.34f : 0.61f;
            float leafAngle = flower->rotationDeg * DEG2RAD
                            + (leaf == 0 ? 1.37f : -0.83f);
            Vector3 leafDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 leafSide = {-leafDir.z, 0.0f, leafDir.x};
            float leafLength = fmaxf(flower->bloomRadius * 0.88f,
                                     h * (leaf == 0 ? 0.22f : 0.16f));
            float leafWidth = leafLength * (leaf == 0 ? 0.18f : 0.15f);
            Vector3 root = {
                base.x + (head.x - base.x) * rootT,
                base.y + h * rootT,
                base.z + (head.z - base.z) * rootT,
            };
            Vector3 leafTip = {root.x + leafDir.x * leafLength,
                               root.y + leafLength * (leaf == 0 ? 0.30f : 0.20f),
                               root.z + leafDir.z * leafLength};
            Vector3 l0 = {root.x - leafSide.x * leafWidth, root.y,
                          root.z - leafSide.z * leafWidth};
            Vector3 l1 = {root.x + leafSide.x * leafWidth, root.y,
                          root.z + leafSide.z * leafWidth};
            Vector3 l2 = {leafTip.x + leafSide.x * leafWidth * 0.10f, leafTip.y,
                          leafTip.z + leafSide.z * leafWidth * 0.10f};
            Vector3 l3 = {leafTip.x - leafSide.x * leafWidth * 0.10f, leafTip.y,
                          leafTip.z - leafSide.z * leafWidth * 0.10f};
            Nature_AddQuad(&mesh, &cursor, l0, l1, l2, l3,
                           (Vector3){-leafDir.x * 0.25f, 1.0f, -leafDir.z * 0.25f},
                           phase, rootT, fminf(rootT + 0.35f, 0.86f), stemColor,
                           Nature_ScaleColor(stemColor, 1.12f));
        }
        int petalCount = flower->petalCount ? flower->petalCount : 5;
        if (petalCount < 4) petalCount = 4;
        if (petalCount > 6) petalCount = 6;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        if (texturedBloom) {
            float angle = flower->rotationDeg * DEG2RAD;
            Vector3 right = {cosf(angle), 0.0f, sinf(angle)};
            Vector3 forward = {-sinf(angle), 0.0f, cosf(angle)};
            float r = flower->bloomRadius * petalScale * 1.08f;
            float ox0 = (-right.x - forward.x) * r;
            float oz0 = (-right.z - forward.z) * r;
            float ox1 = ( right.x - forward.x) * r;
            float oz1 = ( right.z - forward.z) * r;
            float ox2 = ( right.x + forward.x) * r;
            float oz2 = ( right.z + forward.z) * r;
            float ox3 = (-right.x + forward.x) * r;
            float oz3 = (-right.z + forward.z) * r;
            Vector3 p0 = {head.x + ox0, head.y + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
            Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
            Vector3 p2 = {head.x + ox2, head.y + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
            Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
            int variantCount = atlasColumns * atlasRows;
            int variant = flower->bloomVariant % variantCount;
            int column = variant % atlasColumns;
            int row = variant / atlasColumns;
            float insetU = 0.008f / atlasColumns;
            float insetV = 0.008f / atlasRows;
            Vector4 uvRect = {
                (float)column / atlasColumns + insetU,
                (float)row / atlasRows + insetV,
                (float)(column + 1) / atlasColumns - insetU,
                (float)(row + 1) / atlasRows - insetV,
            };
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);
        } else {
            for (int petal = 0; petal < petalCount; petal++) {
                float angle = flower->rotationDeg * DEG2RAD + (float)petal * 2.0f * PI / petalCount;
                Vector3 radial = {cosf(angle), 0.0f, sinf(angle)};
                Vector3 side = {-radial.z, 0.0f, radial.x};
                float irregularity = 0.92f + 0.08f * sinf((float)(i * 17 + petal * 11));
                float r = flower->bloomRadius * petalScale * irregularity;
                float ox0 = radial.x * r * 0.10f;
                float oz0 = radial.z * r * 0.10f;
                float ox1 = radial.x * r * 0.48f + side.x * r * 0.29f;
                float oz1 = radial.z * r * 0.48f + side.z * r * 0.29f;
                float ox2 = radial.x * r;
                float oz2 = radial.z * r;
                float ox3 = radial.x * r * 0.48f - side.x * r * 0.29f;
                float oz3 = radial.z * r * 0.48f - side.z * r * 0.29f;
                Vector3 p0 = {head.x + ox0, head.y + 0.018f + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
                Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
                Vector3 p2 = {head.x + ox2, head.y - r * 0.10f + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
                Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
                Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                               phase, 1.0f, 1.0f, flower->petalColor, flower->petalColor);
            }
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
    field.model = Nature_ModelFromMesh(mesh, Nature_GetShader(texturedBloom));
    field.farModel = Nature_BuildFlowerFarModel(placements, count, texturedBloom,
                                                 atlasColumns, atlasRows);
    field.farReady = field.farModel.meshCount > 0;
    if (GfxQuality_Get() >= GFX_MED)
        field.shadowModel = Nature_BuildFlowerShadowModel(placements, count);
    field.shadowReady = field.shadowModel.meshCount > 0;
    field.textured = petalTexturePath != NULL;
    field.alphaCutoff = alphaCutoff > 0.0f ? fminf(alphaCutoff, 0.9f) : 0.38f;
    field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
    field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f;
    field.drawDistance = 78.0f;
    field.lodDistance = 34.0f;
    field.shadowDistance = 26.0f;
    if (field.textured) {
        Texture2D petalTexture = ResourceManager_LoadTexture(petalTexturePath);
        GenTextureMipmaps(&petalTexture);
        SetTextureFilter(petalTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(petalTexture, TEXTURE_WRAP_CLAMP);
        field.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
        field.farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
    }
    field.ready = true;
    return field;
}

void MapProp_SetFlowerFieldDrawDistance(MapFlowerField *field, float drawDistance)
{
    if (!field) return;
    field->drawDistance = drawDistance;
}

void MapProp_SetFlowerFieldLod(MapFlowerField *field, float lodDistance, float shadowDistance)
{
    if (!field) return;
    field->lodDistance = lodDistance;
    field->shadowDistance = shadowDistance;
}

void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready) return;
    s_natureRenderStats.flowerFieldsTested++;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    if (!Nature_IsChunkVisible(center, field->boundsRadius)) {
        s_natureRenderStats.flowerFieldsFrustumCulled++;
        return;
    }
    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH ? 1.0f
                     : quality == GFX_MED ? 0.84f
                     : quality == GFX_LOW ? 0.68f : 0.55f;
    float dx = camera.position.x - center.x;
    float dz = camera.position.z - center.z;
    float centerDistance = sqrtf(dx * dx + dz * dz);
    float visibleDistance = centerDistance - field->boundsRadius;
    if (field->drawDistance > 0.0f &&
        visibleDistance > field->drawDistance * rangeScale) {
        s_natureRenderStats.flowerFieldsDistanceCulled++;
        return;
    }
    float lodScale = quality >= GFX_HIGH ? 1.0f
                   : quality == GFX_MED ? 0.82f
                   : quality == GFX_LOW ? 0.58f : 0.45f;
    if (field->farReady && field->lodDistance > 0.0f) {
        float spatialHash = sinf(field->boundsCenter.x * 12.9898f
                                 + field->boundsCenter.z * 78.233f);
        spatialHash -= floorf(spatialHash);
        float threshold = field->lodDistance * lodScale + (spatialHash - 0.5f) * 3.0f;
        float hysteresis = quality >= GFX_HIGH ? 1.25f : 2.0f;
        if (field->farLod) {
            if (visibleDistance < threshold - hysteresis)
                field->farLod = false;
        } else if (visibleDistance > threshold + hysteresis) {
            field->farLod = true;
        }
    } else {
        field->farLod = false;
    }
    bool useFarModel = field->farReady && field->farLod;
    float shadowScale = quality >= GFX_HIGH ? 1.0f : 0.78f;
    bool shadowInRange = field->shadowDistance <= 0.0f ||
                         visibleDistance <= field->shadowDistance * shadowScale;
    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_HIGH && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    if (field->shadowReady && useProjectedShadows && shadowInRange) {
        Shader shadowShader = FlowerShadow_GetShader();
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawModel(field->shadowModel, worldOffset, 1.0f, WHITE);
        s_natureRenderStats.flowerShadowDraws++;
        EndBlendMode();
        rlEnableDepthMask();
    } else if (field->shadowReady && useProjectedShadows && !shadowInRange) {
        s_natureRenderStats.flowerShadowDistanceCulled++;
    }
    Nature_UpdateShader(Nature_GetShader(field->textured), time, windDirection, windStrength,
                        field->textured, field->alphaCutoff);
    rlDisableBackfaceCulling();
    DrawModel(useFarModel ? field->farModel : field->model, worldOffset, 1.0f, WHITE);
    s_natureRenderStats.flowerDraws++;
    if (useFarModel)
        s_natureRenderStats.flowerFarDraws++;
    else
        s_natureRenderStats.flowerNearDraws++;
    rlEnableBackfaceCulling();
}

void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready || GfxQuality_Get() < GFX_HIGH ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(true))
        return;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    if (!Nature_IntersectsDynamicShadowCoverage(center, field->boundsRadius) &&
        !Nature_ShadowCasterFilterActive())
        return;

    Shader shader = NatureShadow_GetShader();
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              field->textured, field->alphaCutoff);
    Shader previous = field->model.materials[0].shader;
    field->model.materials[0].shader = shader;
    rlDisableBackfaceCulling();
    DrawModel(field->model, worldOffset, 1.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    field->model.materials[0].shader = previous;
}

void MapProp_UnloadFlowerField(MapFlowerField *field)
{
    if (!field || !field->ready) return;
    if (field->shadowReady)
        UnloadModel(field->shadowModel);
    if (field->farReady)
        UnloadModel(field->farModel);
    UnloadModel(field->model);
    field->farReady = false;
    field->farLod = false;
    field->shadowReady = false;
    field->boundsRadius = 0.0f;
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
    water.bankModel = Nature_ModelFromMesh(bankMesh, Nature_GetShader(false));
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
    Shader bankShader = Nature_GetShader(false);
    Nature_UpdateShader(bankShader, time, (Vector2){0.0f, 0.0f}, 0.0f, false, 1.0f);
    int noInteraction = 0;
    SetShaderValue(bankShader, GetShaderLocation(bankShader, "u_interactionEnabled"),
                   &noInteraction, SHADER_UNIFORM_INT);
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

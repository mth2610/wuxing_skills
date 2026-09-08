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
    float projectionScale = realShadowActive ? 0.36f : 0.62f;
    float widthScale = realShadowActive ? 1.45f : 1.15f;
    float tipWidth = realShadowActive ? 0.95f : 0.70f;
    float shadowStrength = realShadowActive ? 0.68f : 0.75f;
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

static void Nature_AddTexturedQuadUV(Mesh *mesh, int *cursor,
                                     Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                                     Vector3 normal, float phase, float h0, float h1,
                                     Color c0, Color c1,
                                     float u0, float v0, float u1, float v1)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal, phase, h0, h1, c0, c1);
    Nature_SetUv2(mesh, base + 0, u0, v1);
    Nature_SetUv2(mesh, base + 1, u1, v1);
    Nature_SetUv2(mesh, base + 2, u1, v0);
    Nature_SetUv2(mesh, base + 3, u0, v1);
    Nature_SetUv2(mesh, base + 4, u1, v0);
    Nature_SetUv2(mesh, base + 5, u0, v0);
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

static void Nature_AddCurvedBladeQuad(Mesh *mesh, int *cursor,
                                      Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                                      Vector3 n0, Vector3 n1, Vector3 n2, Vector3 n3,
                                      float phase, float h0, float h1, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, n1, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, n2, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, n2, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, h1, c1);
    // Ghost of Tsushima: transverse coordinate u in [-1.0, +1.0] across blade width
    Nature_SetUv2(mesh, base + 0, -1.0f, h0);
    Nature_SetUv2(mesh, base + 1,  1.0f, h0);
    Nature_SetUv2(mesh, base + 2,  1.0f, h1);
    Nature_SetUv2(mesh, base + 3, -1.0f, h0);
    Nature_SetUv2(mesh, base + 4,  1.0f, h1);
    Nature_SetUv2(mesh, base + 5, -1.0f, h1);
}

static void Nature_AddCurvedBladeTip(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                     Vector3 tip, Vector3 n0, Vector3 n1, Vector3 nTip,
                                     float phase, float h0, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, n1, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, tip, nTip, phase, 1.0f, c1);
    Nature_SetUv2(mesh, base + 0, -1.0f, h0);
    Nature_SetUv2(mesh, base + 1,  1.0f, h0);
    Nature_SetUv2(mesh, base + 2,  0.0f, 1.0f); // Tip apex is center spine (u = 0.0)
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
        float width = fmaxf(flower->bloomRadius * 0.95f, 0.045f);
        Vector3 root = flower->position;
        root.y += 0.0015f;
        Vector3 encoded = {flower->height, width, flower->phase};
        Color rootShade = {180, 180, 180, 255};
        Color tipShade = {110, 110, 110, 255};
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

static inline Vector3 Nature_EvalCubicBezier(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float omt = 1.0f - t;
    float omt2 = omt * omt;
    float omt3 = omt2 * omt;
    float t2 = t * t;
    float t3 = t2 * t;
    return (Vector3){
        omt3 * p0.x + 3.0f * omt2 * t * p1.x + 3.0f * omt * t2 * p2.x + t3 * p3.x,
        omt3 * p0.y + 3.0f * omt2 * t * p1.y + 3.0f * omt * t2 * p2.y + t3 * p3.y,
        omt3 * p0.z + 3.0f * omt2 * t * p1.z + 3.0f * omt * t2 * p2.z + t3 * p3.z
    };
}

static inline Vector3 Nature_EvalCubicBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float omt = 1.0f - t;
    float c0 = 3.0f * omt * omt;
    float c1 = 6.0f * omt * t;
    float c2 = 3.0f * t * t;
    Vector3 v0 = Vector3Subtract(p1, p0);
    Vector3 v1 = Vector3Subtract(p2, p1);
    Vector3 v2 = Vector3Subtract(p3, p2);
    return (Vector3){
        c0 * v0.x + c1 * v1.x + c2 * v2.x,
        c0 * v0.y + c1 * v1.y + c2 * v2.y,
        c0 * v0.z + c1 * v1.z + c2 * v2.z
    };
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
    int extraPerClump = style.hasPlumes ? 18 : 0;
    int vertexCount = selected * (bladesPerClump * verticesPerBlade + extraPerClump);
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    ordinal = 0;
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) != 0)
            continue;
        const float golden = 2.39996323f;
        for (int blade = 0; blade < bladesPerClump; blade++) {
            bool isReed = (clump->height > 0.60f);
            float clumpAngle = clump->rotationDeg * DEG2RAD;

            // Natural per-blade organic variations (Structured Procedural Variation)
            float bHash = sinf((float)(i * 43 + blade * 23)) * 43758.5453f;
            bHash -= floorf(bHash);
            float bHash2 = sinf((float)(i * 67 + blade * 37)) * 28461.1273f;
            bHash2 -= floorf(bHash2);
            float bHash3 = sinf((float)(i * 89 + blade * 13)) * 19283.4721f;
            bHash3 -= floorf(bHash3);

            // Radial base azimuth with organic jitter
            float baseAngle = (float)blade * (2.0f * PI / (float)bladesPerClump);
            float bladeAzimuth = baseAngle + (bHash - 0.5f) * 0.95f;
            float radX = cosf(bladeAzimuth);
            float radZ = sinf(bladeAzimuth);

            float flowX = cosf(clumpAngle);
            float flowZ = sinf(clumpAngle);

            // Controlled blend: broad prevailing wind bias + individual blade outward growth
            // Some blades strictly follow the wind, some arch outward to give the clump 3D bush volume
            float windWeight = isReed ? 0.65f : (0.42f + 0.36f * bHash2); // 0.42 to 0.78
            float radWeight = 1.0f - windWeight;
            float combX = radX * radWeight + flowX * windWeight;
            float combZ = radZ * radWeight + flowZ * windWeight;
            float combLen = sqrtf(combX * combX + combZ * combZ);
            if (combLen < 0.01f) { combX = flowX; combZ = flowZ; combLen = 1.0f; }
            combX /= combLen;
            combZ /= combLen;
            float bladeLeanAngle = atan2f(combZ, combX);

            // Base collar
            float collarRadius = clump->radius * (isReed ? 0.20f : 0.16f) * (0.75f + 0.50f * bHash3);
            float bx = clump->position.x + radX * collarRadius;
            float bz = clump->position.z + radZ * collarRadius;

            // Varied canopy: understory, medium culms, sweeping weeping ribbons
            float tierFrac = (float)blade / (float)bladesPerClump;
            float lengthScale = isReed ? (0.85f + 0.30f * sinf((float)(i * 13 + blade * 7)))
                                       : (0.72f + 0.45f * tierFrac + 0.22f * (bHash - 0.5f));
            float height = clump->height * lengthScale;
            float width = clump->radius * style.bladeWidthScale * widthMultiplier
                        * (0.82f + 0.36f * bHash2);

            // Outward reach & droop variation:
            float lean = height * (isReed ? (0.35f + 0.20f * tierFrac)
                                          : (0.32f + 0.25f * tierFrac + 0.14f * (bHash3 - 0.5f)));
            float droopY = height * (0.06f + 0.18f * bHash2);

            // Section 1.1: Cubic Bézier Control Points
            // P0: root
            // P1: lower control (stiff lower stem, defining initial shoot angle)
            // P2: upper control (smooth arch of middle stem)
            // P3: tip (maximum deflection, graceful bow)
            Vector3 pBase = {bx, clump->position.y, bz};
            Vector3 pP1, pP2, pP3;
            if (isReed) {
                pP1 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.08f,
                    pBase.y + height * 0.42f,
                    bz + sinf(bladeLeanAngle) * lean * 0.08f
                };
                pP2 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.52f,
                    pBase.y + height * 0.82f,
                    bz + sinf(bladeLeanAngle) * lean * 0.52f
                };
                pP3 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 1.25f,
                    pBase.y + height * 0.65f,
                    bz + sinf(bladeLeanAngle) * lean * 1.25f
                };
            } else {
                pP1 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.10f,
                    pBase.y + height * 0.35f,
                    bz + sinf(bladeLeanAngle) * lean * 0.10f
                };
                pP2 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.48f,
                    pBase.y + height * 0.72f,
                    bz + sinf(bladeLeanAngle) * lean * 0.48f
                };
                pP3 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.95f,
                    pBase.y + (height * 0.82f - droopY),
                    bz + sinf(bladeLeanAngle) * lean * 0.95f
                };
            }

            float hueShift = (bHash - 0.5f) * 0.20f;
            int rR = (int)(style.rootColor.r * (1.0f + hueShift * 0.4f));
            int rG = (int)(style.rootColor.g * (1.0f + hueShift));
            int rB = (int)(style.rootColor.b * (1.0f - hueShift * 0.3f));
            int tR = (int)(style.tipColor.r * (1.0f + hueShift * 0.7f));
            int tG = (int)(style.tipColor.g * (1.0f + hueShift * 1.1f));
            int tB = (int)(style.tipColor.b * (1.0f - hueShift * 0.5f));
            Color bladeRoot = {(unsigned char)fminf(255, fmaxf(0, rR)),
                               (unsigned char)fminf(255, fmaxf(0, rG)),
                               (unsigned char)fminf(255, fmaxf(0, rB)), 255};
            Color bladeTip = {(unsigned char)fminf(255, fmaxf(0, tR)),
                              (unsigned char)fminf(255, fmaxf(0, tG)),
                              (unsigned char)fminf(255, fmaxf(0, tB)), 255};
            if (isReed) {
                bool isOuterDrySheath = (blade < 2);
                if (isOuterDrySheath) {
                    bladeRoot = (Color){38, 30, 16, 255}; // dark wet peat base
                    bladeTip  = (Color){188, 154, 76, 255}; // warm golden-straw dried reed leaf
                } else {
                    bladeRoot = (Color){20, 36, 16, 255}; // deep aquatic green
                    bladeTip  = (Color){148, 192, 62, 255}; // fresh bamboo celadon reed
                }
            }

            // Base transverse vector fallback
            Vector3 baseSide = (Vector3){-sinf(bladeLeanAngle), 0.0f, cosf(bladeLeanAngle)};
            Vector3 terrainNormal = (Vector3){0.0f, 1.0f, 0.0f};

            for (int segment = 0; segment < bladeSegments; segment++) {
                float t0 = (float)segment / (float)bladeSegments;
                float t1 = (float)(segment + 1) / (float)bladeSegments;

                Vector3 center0 = Nature_EvalCubicBezier(pBase, pP1, pP2, pP3, t0);
                Vector3 center1 = Nature_EvalCubicBezier(pBase, pP1, pP2, pP3, t1);

                // Section 1.2: Exact Tangent T(t) = dB(t)/dt
                Vector3 T0 = Vector3Normalize(Nature_EvalCubicBezierTangent(pBase, pP1, pP2, pP3, t0));
                Vector3 T1 = Vector3Normalize(Nature_EvalCubicBezierTangent(pBase, pP1, pP2, pP3, t1));

                // Section 1.2: Transverse Vector S(t) = normalize(T(t) x N_terrain)
                Vector3 crossS0 = Vector3CrossProduct(T0, terrainNormal);
                float lenS0 = Vector3Length(crossS0);
                Vector3 S0 = (lenS0 > 0.001f) ? Vector3Scale(crossS0, 1.0f / lenS0) : baseSide;

                Vector3 crossS1 = Vector3CrossProduct(T1, terrainNormal);
                float lenS1 = Vector3Length(crossS1);
                Vector3 S1 = (lenS1 > 0.001f) ? Vector3Scale(crossS1, 1.0f / lenS1) : baseSide;

                // Section 1.3: Geometric Normal N_geo(t) = normalize(S(t) x T(t))
                Vector3 Ngeo0 = Vector3Normalize(Vector3CrossProduct(S0, T0));
                if (Ngeo0.y < 0.0f) Ngeo0 = Vector3Negate(Ngeo0);
                Ngeo0.y = fmaxf(Ngeo0.y, 0.25f);
                Ngeo0 = Vector3Normalize(Ngeo0);

                Vector3 Ngeo1 = Vector3Normalize(Vector3CrossProduct(S1, T1));
                if (Ngeo1.y < 0.0f) Ngeo1 = Vector3Negate(Ngeo1);
                Ngeo1.y = fmaxf(Ngeo1.y, 0.25f);
                Ngeo1 = Vector3Normalize(Ngeo1);

                // Section 1.2: Width Modulation W(t) = W_base * (1 - t)^p
                float halfW0 = (width * 0.5f) * (t0 < 0.12f ? (0.85f + 0.15f * (t0 / 0.12f)) : powf(1.0f - t0, 1.25f));
                float halfW1 = (width * 0.5f) * (t1 < 0.12f ? (0.85f + 0.15f * (t1 / 0.12f)) : powf(1.0f - t1, 1.25f));

                // Section 1.2: V-shape fold delta_z
                Vector3 fold0 = Vector3Scale(Ngeo0, -halfW0 * 0.15f);
                Vector3 fold1 = Vector3Scale(Ngeo1, -halfW1 * 0.15f);

                // Section 1.2: Vertices V_left and V_right
                Vector3 p0 = Vector3Add(Vector3Subtract(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p1 = Vector3Add(Vector3Add(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p2 = Vector3Add(Vector3Add(center1, Vector3Scale(S1, halfW1)), fold1);
                Vector3 p3 = Vector3Add(Vector3Subtract(center1, Vector3Scale(S1, halfW1)), fold1);

                // Deep root ambient occlusion
                float occ0 = (t0 < 0.28f) ? (0.74f + 0.26f * (t0 / 0.28f)) : 1.0f;
                float occ1 = (t1 < 0.28f) ? (0.74f + 0.26f * (t1 / 0.28f)) : 1.0f;
                Color color0 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t0), occ0);
                Color color1 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t1), occ1);

                // Section 1.3: Pixel Normal Rounding: N_pixel(t, v) = normalize(N_geo(t) + alpha * v * S(t))
                Vector3 nL0 = Vector3Normalize(Vector3Subtract(Ngeo0, Vector3Scale(S0, 0.40f)));
                Vector3 nR0 = Vector3Normalize(Vector3Add(Ngeo0, Vector3Scale(S0, 0.40f)));
                Vector3 nL1 = Vector3Normalize(Vector3Subtract(Ngeo1, Vector3Scale(S1, 0.36f)));
                Vector3 nR1 = Vector3Normalize(Vector3Add(Ngeo1, Vector3Scale(S1, 0.36f)));
                Vector3 nTip = Vector3Normalize((Vector3){Ngeo1.x, 0.72f, Ngeo1.z});

                // Section 3.2: Shape Normal Blending with terrain normal
                float blend0 = powf(t0, 0.70f);
                float blend1 = powf(t1, 0.70f);
                nL0 = Vector3Normalize(Vector3Lerp(terrainNormal, nL0, blend0));
                nR0 = Vector3Normalize(Vector3Lerp(terrainNormal, nR0, blend0));
                nL1 = Vector3Normalize(Vector3Lerp(terrainNormal, nL1, blend1));
                nR1 = Vector3Normalize(Vector3Lerp(terrainNormal, nR1, blend1));
                nTip = Vector3Normalize(Vector3Lerp(terrainNormal, nTip, 0.85f));

                if (style.hasPlumes) {
                    if (segment == bladeSegments - 1) {
                        Nature_SetVertex(&mesh, cursor++, p0, nL0, clump->phase, t0, color0);
                        Nature_SetVertex(&mesh, cursor++, p1, nR0, clump->phase, t0, color0);
                        Nature_SetVertex(&mesh, cursor++, center1, nTip, clump->phase, 1.0f, color1);
                    } else {
                        Nature_AddCurvedBladeQuad(&mesh, &cursor, p0, p1, p2, p3,
                                                  nL0, nR0, nR1, nL1,
                                                  clump->phase, t0, t1, color0, color1);
                    }
                } else if (segment == bladeSegments - 1) {
                    Nature_AddCurvedBladeTip(&mesh, &cursor, p0, p1, center1,
                                             nL0, nR0, nTip,
                                             clump->phase, t0, color0, color1);
                } else {
                    Nature_AddCurvedBladeQuad(&mesh, &cursor, p0, p1, p2, p3,
                                              nL0, nR0, nR1, nL1,
                                              clump->phase, t0, t1, color0, color1);
                }
            }
        }

        // Shore reeds: 3D fluted silky plumes and arching lateral leaves (54 + 12 = 66 vertices)
        if (style.hasPlumes) {
            float caneHeight = clump->height * 1.35f;
            float caneAngle = clump->rotationDeg * DEG2RAD + 0.22f * sinf((float)i * 1.7f);
            float caneLean = caneHeight * 0.32f;
            Vector3 caneP0 = {clump->position.x, clump->position.y, clump->position.z};
            Vector3 caneP1 = {clump->position.x + cosf(caneAngle) * caneLean * 0.45f,
                              clump->position.y + caneHeight * 0.55f,
                              clump->position.z + sinf(caneAngle) * caneLean * 0.45f};
            Vector3 caneP2 = {clump->position.x + cosf(caneAngle) * caneLean,
                              clump->position.y + caneHeight,
                              clump->position.z + sinf(caneAngle) * caneLean};

            // 4 knot points along curved plume spindle (t = 0.65 to 1.0)
            float tKnots[4] = {0.65f, 0.78f, 0.90f, 1.00f};
            Vector3 plPts[4];
            for (int k = 0; k < 4; k++) {
                float tk = tKnots[k];
                float omtk = 1.0f - tk;
                plPts[k] = (Vector3){
                    omtk*omtk*caneP0.x + 2*omtk*tk*caneP1.x + tk*tk*caneP2.x,
                    omtk*omtk*caneP0.y + 2*omtk*tk*caneP1.y + tk*tk*caneP2.y,
                    omtk*omtk*caneP0.z + 2*omtk*tk*caneP1.z + tk*tk*caneP2.z
                };
            }

            // Parametric Reed Plume Density Envelope: R(u) = R_max * sin(pi * u^0.8)^0.6
            // Volumetric envelope with feathery fiber alpha texture mapping
            // Single flat textured plume quad (1 quad: 6 vertices)
            float stripAngle = caneAngle + 1.5707963f;
            Vector3 side = {-sinf(stripAngle), 0.0f, cosf(stripAngle)};
            Vector3 norm = Vector3Normalize((Vector3){cosf(stripAngle), 0.25f, sinf(stripAngle)});

            Vector3 qBase = plPts[0];
            Vector3 qTip = plPts[3];
            float plumeH = Vector3Length(Vector3Subtract(qTip, qBase));
            float halfW = fmaxf(0.12f, plumeH * 0.28f);

            Vector3 q0 = {qBase.x - side.x * halfW, qBase.y, qBase.z - side.z * halfW};
            Vector3 q1 = {qBase.x + side.x * halfW, qBase.y, qBase.z + side.z * halfW};
            Vector3 q2 = {qTip.x + side.x * halfW * 0.70f, qTip.y, qTip.z + side.z * halfW * 0.70f};
            Vector3 q3 = {qTip.x - side.x * halfW * 0.70f, qTip.y, qTip.z - side.z * halfW * 0.70f};

            Color c0 = {245, 238, 220, 255};
            Color c1 = {255, 255, 255, 255};

            if (style.texturePath != NULL) {
                Nature_AddTexturedQuadUV(&mesh, &cursor, q0, q1, q2, q3, norm,
                                         clump->phase, 0.52f, 1.0f,
                                         c0, c1, 0.0f, 0.0f, 1.0f, 1.0f);
            } else {
                Nature_AddQuad(&mesh, &cursor, q0, q1, q2, q3, norm,
                               clump->phase, 0.52f, 1.0f, c0, c1);
            }

            // Two lateral arching leaves along reed stalk
            float tL0 = 0.32f, tL1 = 0.52f;
            float omtL0 = 1.0f - tL0, omtL1 = 1.0f - tL1;
            Vector3 leafRoot0 = {
                omtL0*omtL0*caneP0.x + 2*omtL0*tL0*caneP1.x + tL0*tL0*caneP2.x,
                omtL0*omtL0*caneP0.y + 2*omtL0*tL0*caneP1.y + tL0*tL0*caneP2.y,
                omtL0*omtL0*caneP0.z + 2*omtL0*tL0*caneP1.z + tL0*tL0*caneP2.z
            };
            Vector3 leafRoot1 = {
                omtL1*omtL1*caneP0.x + 2*omtL1*tL1*caneP1.x + tL1*tL1*caneP2.x,
                omtL1*omtL1*caneP0.y + 2*omtL1*tL1*caneP1.y + tL1*tL1*caneP2.y,
                omtL1*omtL1*caneP0.z + 2*omtL1*tL1*caneP1.z + tL1*tL1*caneP2.z
            };
            float lAng0 = caneAngle + 1.42f;
            float lAng1 = caneAngle - 1.42f;
            Vector3 lDir0 = {cosf(lAng0), 0.0f, sinf(lAng0)};
            Vector3 lDir1 = {cosf(lAng1), 0.0f, sinf(lAng1)};
            Vector3 lSide0 = {-lDir0.z * 0.024f, 0.0f, lDir0.x * 0.024f};
            Vector3 lSide1 = {-lDir1.z * 0.022f, 0.0f, lDir1.x * 0.022f};
            Vector3 lTip0 = {leafRoot0.x + lDir0.x * 0.45f, leafRoot0.y - 0.12f, leafRoot0.z + lDir0.z * 0.45f};
            Vector3 lTip1 = {leafRoot1.x + lDir1.x * 0.40f, leafRoot1.y - 0.10f, leafRoot1.z + lDir1.z * 0.40f};
            Color leafCol = {88, 122, 50, 255};
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(leafRoot0, lSide0), Vector3Add(leafRoot0, lSide0),
                           Vector3Add(lTip0, Vector3Scale(lSide0, 0.15f)), Vector3Subtract(lTip0, Vector3Scale(lSide0, 0.15f)),
                           (Vector3){0, 1, 0}, clump->phase, 0.3f, 0.5f, leafCol, Nature_ScaleColor(leafCol, 1.15f));
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(leafRoot1, lSide1), Vector3Add(leafRoot1, lSide1),
                           Vector3Add(lTip1, Vector3Scale(lSide1, 0.15f)), Vector3Subtract(lTip1, Vector3Scale(lSide1, 0.15f)),
                           (Vector3){0, 1, 0}, clump->phase, 0.4f, 0.6f, leafCol, Nature_ScaleColor(leafCol, 1.15f));
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
    if (style.bladeSegments > 6) style.bladeSegments = 6;
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
                farBlades, 1, 1.35f, &farCount);
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

static inline int Nature_FlowerBloomVertexCount(int variant, bool textured)
{
    if (textured) return 36; // 3 textured bloom cards (18) + 3D pollen disc dome (18) = 36
    int morph = variant % 4;
    switch (morph) {
        case 0: return 210; // 2-layer Rose/Poppy: 5 outer + 3 inner (192) + dome (18) = 210
        case 1: return 210; // 8 oval daisy petals (192) + phyllotaxis disc (18) = 210
        case 2: return 210; // Lavender spike: 4 whorls x 2 florets (192) + tip bud dome (18) = 210
        case 3: return 162; // 6 reflexed lanceolate petals (144) + center dome (18) = 162
        default: return 210;
    }
}

typedef enum {
    PETAL_SHAPE_OVAL,       // w(t) = W_max * sin(pi*t)^n
    PETAL_SHAPE_LANCEOLATE, // asymmetric: rapid bulge, long taper to sharp tip
    PETAL_SHAPE_OBCORDATE,  // heart-shaped with notched apex: w_base(t) - notch
    PETAL_SHAPE_SPATULATE,  // spoon-shaped: slender base, rounded wide tip
} PetalShapeType;

typedef struct {
    PetalShapeType shape;
    float wMax;
    float length;
    float exponent;     // n for oval/obcordate power
    float tPeak;        // peak location (e.g. 0.30 for lanceolate, 0.75 for spatulate)
    float notchDepth;   // notch depth for obcordate apex
    float curveAmount;  // vertical cupping / reflex (dy = curveAmount * t^2.5)
    float twistAmount;  // twist angle around petal axis (radians)
} PetalParams;

static inline float Nature_EvalPetalWidth(float t, const PetalParams *p)
{
    if (t <= 0.0f) return 0.002f;
    if (t >= 1.0f) return 0.000f;

    switch (p->shape) {
        case PETAL_SHAPE_OVAL: {
            // Chamomile / Daisy oval petal: wide body, soft smoothly rounded apex
            // Base at t=0 narrows to receptacle; mid reaches max width; smoothly rounds to zero at apex
            float s = sinf(PI * powf(t, 0.55f));
            if (s < 0.0f) s = 0.0f;
            float n = p->exponent > 0.0f ? p->exponent : 0.60f;
            float baseTaper = fminf(1.0f, t / 0.15f);
            return p->wMax * powf(s, n) * baseTaper;
        }
        case PETAL_SHAPE_LANCEOLATE: {
            float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.30f;
            if (t < tPeak) {
                return p->wMax * powf(t / tPeak, 0.50f);
            } else {
                return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.80f);
            }
        }
        case PETAL_SHAPE_OBCORDATE: {
            float s = sinf(PI * t);
            if (s < 0.0f) s = 0.0f;
            float wBase = p->wMax * powf(s, 1.20f);
            float notch = 0.0f;
            if (t > 0.80f) {
                float dt = (t - 1.0f) / 0.08f;
                float depth = p->notchDepth > 0.0f ? p->notchDepth : p->wMax * 0.40f;
                notch = depth * expf(-0.5f * dt * dt);
            }
            return fmaxf(0.003f, wBase - notch);
        }
        case PETAL_SHAPE_SPATULATE: {
            float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.75f;
            if (t < tPeak) {
                return p->wMax * powf(t / tPeak, 0.65f);
            } else {
                return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.40f);
            }
        }
        default:
            return p->wMax * sinf(PI * t);
    }
}

static inline float Nature_EvalPetalElevation(float t, const PetalParams *p)
{
    // Natural botanical 3D sigmoid cup curve: rises from receptacle with initial bowl slope
    float s = sinf(t * 1.5707963f);
    return p->curveAmount * (s * 1.25f - 0.25f * t * t);
}

static void Nature_AddParametricPetal(Mesh *mesh, int *cursor,
                                     Vector3 head, Vector3 dir, Vector3 side,
                                     const PetalParams *p, float phase,
                                     Color cBase, Color cMid, Color cTip)
{
    float len = p->length;
    float t0 = 0.05f;
    float t1 = 0.42f;
    float t2 = 0.70f;
    float t3 = 0.88f;

    float w0 = Nature_EvalPetalWidth(t0, p);
    float dy0 = Nature_EvalPetalElevation(t0, p);

    float w1 = Nature_EvalPetalWidth(t1, p);
    float dy1 = Nature_EvalPetalElevation(t1, p);

    float w2 = Nature_EvalPetalWidth(t2, p);
    float dy2 = Nature_EvalPetalElevation(t2, p);

    float w3 = Nature_EvalPetalWidth(t3, p);
    float dy3 = Nature_EvalPetalElevation(t3, p);

    float dy4 = Nature_EvalPetalElevation(1.0f, p);

    Vector3 side0 = side;
    Vector3 side1 = side;
    Vector3 side2 = side;
    Vector3 side3 = side;
    if (fabsf(p->twistAmount) > 0.001f) {
        float phi1 = p->twistAmount * t1;
        float phi2 = p->twistAmount * t2;
        float phi3 = p->twistAmount * t3;
        side1 = (Vector3){side.x * cosf(phi1), sinf(phi1) * len * 0.10f, side.z * cosf(phi1)};
        side2 = (Vector3){side.x * cosf(phi2), sinf(phi2) * len * 0.15f, side.z * cosf(phi2)};
        side3 = (Vector3){side.x * cosf(phi3), sinf(phi3) * len * 0.18f, side.z * cosf(phi3)};
    }

    Vector3 p0 = {head.x + dir.x * (len * t0), head.y + dy0 + 0.003f, head.z + dir.z * (len * t0)};
    Vector3 p1 = {head.x + dir.x * (len * t1), head.y + dy1,          head.z + dir.z * (len * t1)};
    Vector3 p2 = {head.x + dir.x * (len * t2), head.y + dy2,          head.z + dir.z * (len * t2)};
    Vector3 p3 = {head.x + dir.x * (len * t3), head.y + dy3,          head.z + dir.z * (len * t3)};
    Vector3 pApex = {head.x + dir.x * len,     head.y + dy4,          head.z + dir.z * len};

    // Transverse cupping: petal margins dish upward across width creating rich 3D volume
    float cup0 = w0 * 0.22f;
    float cup1 = w1 * 0.28f;
    float cup2 = w2 * 0.22f;
    float cup3 = w3 * 0.14f;

    Vector3 b0 = {p0.x - side0.x * w0, p0.y + cup0, p0.z - side0.z * w0};
    Vector3 b1 = {p0.x + side0.x * w0, p0.y + cup0, p0.z + side0.z * w0};

    Vector3 m0 = {p1.x - side1.x * w1, p1.y + cup1, p1.z - side1.z * w1};
    Vector3 m1 = {p1.x + side1.x * w1, p1.y + cup1, p1.z + side1.z * w1};

    Vector3 s0 = {p2.x - side2.x * w2, p2.y + cup2, p2.z - side2.z * w2};
    Vector3 s1 = {p2.x + side2.x * w2, p2.y + cup2, p2.z + side2.z * w2};

    Vector3 c0 = {p3.x - side3.x * w3, p3.y + cup3, p3.z - side3.z * w3};
    Vector3 c1 = {p3.x + side3.x * w3, p3.y + cup3, p3.z + side3.z * w3};

    // Cupped analytical normals across width
    Vector3 nL0 = Vector3Normalize((Vector3){dir.x * 0.15f + side.x * 0.32f, 0.92f, dir.z * 0.15f + side.z * 0.32f});
    Vector3 nR0 = Vector3Normalize((Vector3){dir.x * 0.15f - side.x * 0.32f, 0.92f, dir.z * 0.15f - side.z * 0.32f});
    Vector3 nL1 = Vector3Normalize((Vector3){dir.x * 0.32f + side1.x * 0.38f, 0.86f, dir.z * 0.32f + side1.z * 0.38f});
    Vector3 nR1 = Vector3Normalize((Vector3){dir.x * 0.32f - side1.x * 0.38f, 0.86f, dir.z * 0.32f - side1.z * 0.38f});
    Vector3 nL2 = Vector3Normalize((Vector3){dir.x * 0.52f + side2.x * 0.34f, 0.80f, dir.z * 0.52f + side2.z * 0.34f});
    Vector3 nR2 = Vector3Normalize((Vector3){dir.x * 0.52f - side2.x * 0.34f, 0.80f, dir.z * 0.52f - side2.z * 0.34f});
    Vector3 n3  = Vector3Normalize((Vector3){dir.x * 0.68f, 0.70f, dir.z * 0.68f});

    // Segment 0: Base to Mid (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, b1, nR0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);

    // Segment 1: Mid to Shoulder (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);

    // Segment 2: Shoulder to Rounding Shoulders (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, c1, n3,  phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, c1, n3,  phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c0, n3,  phase, 1.00f, cTip);

    // Segment 3: Botanical Rounded Dome Apex (6 vertices, 2 fan triangles to pApex)
    Vector3 nTipL = Vector3Normalize((Vector3){dir.x * 0.70f - side.x * 0.35f, 0.60f, dir.z * 0.70f - side.z * 0.35f});
    Vector3 nTipR = Vector3Normalize((Vector3){dir.x * 0.70f + side.x * 0.35f, 0.60f, dir.z * 0.70f + side.z * 0.35f});
    Nature_SetVertex(mesh, (*cursor)++, c0,    nTipL, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, p3,    n3,    phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipL, phase, 1.00f, cTip);

    Nature_SetVertex(mesh, (*cursor)++, p3,    n3,    phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c1,    nTipR, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipR, phase, 1.00f, cTip);
}

static void Nature_AddFlowerCenterDome(Mesh *mesh, int *cursor, Vector3 head, float cR, float cHeight, float phase, Color centerColor)
{
    Vector3 apex = {head.x, head.y + cHeight * 1.35f, head.z};
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    Color rimColor = Nature_ScaleColor(centerColor, 0.75f);
    // 6-sided dome with golden angle phyllotaxis micro-modulation
    for (int s = 0; s < 6; s++) {
        float a0 = (float)s * 1.04719755f;
        float a1 = (float)(s + 1) * 1.04719755f;
        float goldMod0 = 1.0f + 0.06f * sinf((float)s * 2.39996f);
        float goldMod1 = 1.0f + 0.06f * sinf((float)(s + 1) * 2.39996f);
        Vector3 r0 = {head.x + cosf(a0) * cR * goldMod0, head.y + 0.005f, head.z + sinf(a0) * cR * goldMod0};
        Vector3 r1 = {head.x + cosf(a1) * cR * goldMod1, head.y + 0.005f, head.z + sinf(a1) * cR * goldMod1};
        Vector3 norm = Vector3Normalize((Vector3){cosf((a0 + a1) * 0.5f) * 0.55f, 0.84f, sinf((a0 + a1) * 0.5f) * 0.55f});
        Nature_SetVertex(mesh, (*cursor)++, apex, (Vector3){0.0f, 1.0f, 0.0f}, phase, 1.0f, apexColor);
        Nature_SetVertex(mesh, (*cursor)++, r0, norm, phase, 0.95f, rimColor);
        Nature_SetVertex(mesh, (*cursor)++, r1, norm, phase, 0.95f, rimColor);
    }
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
    boundsMin.x -= placements[0].bloomRadius * 1.5f;
    boundsMin.z -= placements[0].bloomRadius * 1.5f;
    boundsMax.x += placements[0].bloomRadius * 1.5f;
    boundsMax.y += placements[0].height + placements[0].bloomRadius * 1.5f;
    boundsMax.z += placements[0].bloomRadius * 1.5f;
    if (GfxQuality_Get() >= GFX_HIGH)
        (void)NatureShadow_GetShader();
    field.textured = (petalTexturePath != NULL);
    if (atlasColumns < 1) atlasColumns = 1;
    if (atlasRows < 1) atlasRows = 1;

    // Calculate exact vertex count: 18 (basal rosette) + 24 (curved stem) + 12 (mid leaves) + bloom
    int vertexCount = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float extent = flower->bloomRadius * 1.6f;
        boundsMin.x = fminf(boundsMin.x, flower->position.x - extent);
        boundsMin.y = fminf(boundsMin.y, flower->position.y);
        boundsMin.z = fminf(boundsMin.z, flower->position.z - extent);
        boundsMax.x = fmaxf(boundsMax.x, flower->position.x + extent);
        boundsMax.y = fmaxf(boundsMax.y, flower->position.y + flower->height + extent);
        boundsMax.z = fmaxf(boundsMax.z, flower->position.z + extent);
        vertexCount += 84 + Nature_FlowerBloomVertexCount(flower->bloomVariant, field.textured);
    }
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float r = flower->bloomRadius * petalScale;

        // 1. Basal leaf rosette: 4 spreading leaves hugging soil to anchor plant (24 verts)
        for (int b = 0; b < 4; b++) {
            float bAngle = flower->rotationDeg * DEG2RAD + (float)b * 1.570796f + 0.35f;
            Vector3 bDir = {cosf(bAngle), 0.0f, sinf(bAngle)};
            Vector3 bSide = {-bDir.z * 0.020f, 0.0f, bDir.x * 0.020f};
            float bLen = fmaxf(0.075f, flower->bloomRadius * 1.10f);
            Vector3 r0 = {base.x - bSide.x, base.y + 0.002f, base.z - bSide.z};
            Vector3 r1 = {base.x + bSide.x, base.y + 0.002f, base.z + bSide.z};
            Vector3 r2 = {base.x + bDir.x * bLen + bSide.x * 0.25f, base.y + 0.008f, base.z + bDir.z * bLen + bSide.z * 0.25f};
            Vector3 r3 = {base.x + bDir.x * bLen - bSide.x * 0.25f, base.y + 0.008f, base.z + bDir.z * bLen - bSide.z * 0.25f};
            Color basalColor = {46, 76, 32, 255};
            Nature_AddQuad(&mesh, &cursor, r0, r1, r2, r3, (Vector3){0.0f, 1.0f, 0.0f},
                           phase, 0.0f, 0.25f, basalColor, Nature_ScaleColor(basalColor, 1.25f));
        }

        // 2. Curved stem using quadratic Bezier (bowing organically under wind/gravity, 24 verts)
        float stemWidth = fmaxf(0.005f, flower->bloomRadius * 0.08f);
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 4.1f;
        float leanDist = h * (0.06f + 0.08f * (0.5f + 0.5f * sinf((float)i * 2.37f)));
        Vector3 stemP0 = base;
        Vector3 stemP1 = {base.x + cosf(leanAngle) * leanDist * 0.40f,
                          base.y + h * 0.52f,
                          base.z + sinf(leanAngle) * leanDist * 0.40f};
        Vector3 head = {base.x + cosf(leanAngle) * leanDist, base.y + h,
                        base.z + sinf(leanAngle) * leanDist};

        // Mid point on Bezier curve
        Vector3 midStem = {
            0.25f * stemP0.x + 0.50f * stemP1.x + 0.25f * head.x,
            0.25f * stemP0.y + 0.50f * stemP1.y + 0.25f * head.y,
            0.25f * stemP0.z + 0.50f * stemP1.z + 0.25f * head.z
        };

        for (int cross = 0; cross < 2; cross++) {
            float angle = flower->rotationDeg * DEG2RAD + cross * PI * 0.5f;
            Vector3 side = {cosf(angle) * stemWidth, 0.0f, sinf(angle) * stemWidth};
            Vector3 normal = {-sinf(angle), 0.08f, cosf(angle)};

            // Segment 0: base to mid
            Vector3 s0_0 = Vector3Subtract(stemP0, side);
            Vector3 s0_1 = Vector3Add(stemP0, side);
            Vector3 s0_2 = Vector3Add(midStem, Vector3Scale(side, 0.8f));
            Vector3 s0_3 = Vector3Subtract(midStem, Vector3Scale(side, 0.8f));
            Nature_AddQuad(&mesh, &cursor, s0_0, s0_1, s0_2, s0_3, normal, phase, 0.0f, 0.5f, stemColor, stemColor);

            // Segment 1: mid to head
            Vector3 s1_0 = s0_3;
            Vector3 s1_1 = s0_2;
            Vector3 s1_2 = Vector3Add(head, Vector3Scale(side, 0.5f));
            Vector3 s1_3 = Vector3Subtract(head, Vector3Scale(side, 0.5f));
            Nature_AddQuad(&mesh, &cursor, s1_0, s1_1, s1_2, s1_3, normal, phase, 0.5f, 1.0f, stemColor, stemColor);
        }

        // 3. Mid-stem foliage leaves (12 verts)
        for (int leaf = 0; leaf < 2; leaf++) {
            float rootT = leaf == 0 ? 0.35f : 0.65f;
            float leafAngle = leanAngle + (leaf == 0 ? 1.40f : -1.40f);
            Vector3 lDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 lSide = {-lDir.z * 0.012f, 0.0f, lDir.x * 0.012f};
            float lLen = fmaxf(0.045f, flower->bloomRadius * 0.75f);
            float omt = 1.0f - rootT;
            Vector3 lRoot = {
                omt*omt*stemP0.x + 2*omt*rootT*stemP1.x + rootT*rootT*head.x,
                omt*omt*stemP0.y + 2*omt*rootT*stemP1.y + rootT*rootT*head.y,
                omt*omt*stemP0.z + 2*omt*rootT*stemP1.z + rootT*rootT*head.z
            };
            Vector3 lTip = {lRoot.x + lDir.x * lLen, lRoot.y + lLen * 0.35f, lRoot.z + lDir.z * lLen};
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(lRoot, lSide), Vector3Add(lRoot, lSide),
                           Vector3Add(lTip, Vector3Scale(lSide, 0.2f)), Vector3Subtract(lTip, Vector3Scale(lSide, 0.2f)),
                           (Vector3){0, 1, 0}, phase, rootT, rootT + 0.3f, stemColor, Nature_ScaleColor(stemColor, 1.15f));
        }

        // 4. Calyx cup: 4 green sepals cradling bloom head from below (24 verts)
        float calyxLen = r * 0.45f;
        for (int c = 0; c < 4; c++) {
            float cAngle = flower->rotationDeg * DEG2RAD + (float)c * 1.570796f + 0.392699f;
            Vector3 cDir = {cosf(cAngle), 0.0f, sinf(cAngle)};
            Vector3 cSide = {-cDir.z * calyxLen * 0.30f, 0.0f, cDir.x * calyxLen * 0.30f};
            Vector3 cBasePt = {head.x, head.y - stemWidth * 0.6f, head.z};
            Vector3 cTipPt = {head.x + cDir.x * calyxLen, head.y - stemWidth * 0.15f, head.z + cDir.z * calyxLen};
            Color calyxColor = Nature_ScaleColor(stemColor, 0.88f);
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(cBasePt, cSide), Vector3Add(cBasePt, cSide),
                           Vector3Add(cTipPt, Vector3Scale(cSide, 0.20f)), Vector3Subtract(cTipPt, Vector3Scale(cSide, 0.20f)),
                           (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.90f, 0.98f, calyxColor, Nature_ScaleColor(calyxColor, 1.22f));
        }

        // 5. Assemble 3D Flower Head
        if (field.textured) {
            int variantCount = atlasColumns * atlasRows;
            int variant = flower->bloomVariant % variantCount;
            int column = variant % atlasColumns;
            int row = variant / atlasColumns;
            float insetU = 0.008f / (float)atlasColumns;
            float insetV = 0.008f / (float)atlasRows;
            Vector4 uvRect = {
                (float)column / (float)atlasColumns + insetU,
                (float)row / (float)atlasRows + insetV,
                (float)(column + 1) / (float)atlasColumns - insetU,
                (float)(row + 1) / (float)atlasRows - insetV,
            };

            float stemAngle = flower->rotationDeg * DEG2RAD;
            float headTilt = 0.22f + 0.25f * (0.5f + 0.5f * sinf((float)i * 1.91f + phase));
            float tiltX = cosf(leanAngle) * headTilt;
            float tiltZ = sinf(leanAngle) * headTilt;
            Vector3 bloomNormal = Vector3Normalize((Vector3){-tiltX, 1.0f, -tiltZ});
            Vector3 right = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
            Vector3 forward = {-sinf(stemAngle), 0.0f, cosf(stemAngle)};

            // Card 0: Horizontal / upward-tilted face bloom disc
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
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);

            // Card 1: Angled cross card 1 (vertical profile rotated 45 deg)
            float a1 = stemAngle + 0.785398f;
            Vector3 r1 = {cosf(a1) * r, 0.0f, sinf(a1) * r};
            Vector3 f1 = {-sinf(a1) * r, 0.0f, cosf(a1) * r};
            float vH = r * 0.75f;
            Vector3 q0 = {head.x - r1.x, head.y - vH * 0.15f, head.z - r1.z};
            Vector3 q1 = {head.x + r1.x, head.y - vH * 0.15f, head.z + r1.z};
            Vector3 q2 = {head.x + r1.x, head.y + vH * 0.85f, head.z + r1.z};
            Vector3 q3 = {head.x - r1.x, head.y + vH * 0.85f, head.z - r1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, q0, q1, q2, q3, f1,
                                    phase, flower->petalColor, uvRect);

            // Card 2: Angled cross card 2 (orthogonal to Card 1)
            Vector3 k0 = {head.x - f1.x, head.y - vH * 0.15f, head.z - f1.z};
            Vector3 k1 = {head.x + f1.x, head.y - vH * 0.15f, head.z + f1.z};
            Vector3 k2 = {head.x + f1.x, head.y + vH * 0.85f, head.z + f1.z};
            Vector3 k3 = {head.x - f1.x, head.y + vH * 0.85f, head.z - f1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, k0, k1, k2, k3, r1,
                                    phase, flower->petalColor, uvRect);

            // 3D Pollen center dome
            Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.22f, r * 0.10f, phase, centerColor);
        } else {
            int morph = flower->bloomVariant % 4;
            Color cBase = Nature_ScaleColor(flower->petalColor, 0.72f);
            Color cMid = flower->petalColor;
            Color cTip = Nature_ScaleColor(flower->petalColor, 1.15f);

            switch (morph) {
                case 0: { // Multi-layer Rose / Poppy: 5 outer (60) + 3 inner (36) + dome (18) = 114 verts
                    PetalParams pOuter = {
                        .shape = PETAL_SHAPE_OBCORDATE,
                        .wMax = r * 0.48f,
                        .length = r * 1.05f,
                        .exponent = 1.25f,
                        .notchDepth = r * 0.16f,
                        .curveAmount = r * 0.38f,
                        .twistAmount = 0.08f,
                    };
                    for (int k = 0; k < 5; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 1.256637f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pOuter,
                                                 phase, cBase, cMid, cTip);
                    }
                    PetalParams pInner = {
                        .shape = PETAL_SHAPE_OBCORDATE,
                        .wMax = r * 0.38f,
                        .length = r * 0.78f,
                        .exponent = 1.15f,
                        .notchDepth = r * 0.12f,
                        .curveAmount = r * 0.52f,
                        .twistAmount = -0.10f,
                    };
                    Color cInBase = Nature_ScaleColor(cBase, 0.90f);
                    Color cInMid = Nature_ScaleColor(cMid, 0.95f);
                    for (int k = 0; k < 3; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + 0.628318f + (float)k * 2.094395f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pInner,
                                                 phase, cInBase, cInMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.28f, r * 0.16f, phase, centerColor);
                    break;
                }
                case 1: { // 8-petal Daisy / Chrysanthemum: oval rounded petals (96 verts) + phyllotaxis dome (18 verts) = 114 verts
                    PetalParams pDaisy = {
                        .shape = PETAL_SHAPE_OVAL,
                        .wMax = r * 0.35f,
                        .length = r * 1.18f,
                        .exponent = 0.52f,
                        .curveAmount = r * 0.22f,
                        .twistAmount = 0.03f,
                    };
                    for (int k = 0; k < 8; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 0.785398f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pDaisy,
                                                 phase, cBase, cMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.35f, r * 0.15f, phase, centerColor);
                    break;
                }
                case 2: { // Lavender Spike Inflorescence: 4 whorls x 2 florets (96 verts) + tip bud (18 verts) = 114 verts
                    float spH = r * 2.2f;
                    float phaseSpiral = 0.38f;
                    for (int k = 0; k < 4; k++) {
                        float s = 0.22f + (float)k * 0.24f;
                        Vector3 tierHead = {head.x, head.y + spH * s, head.z};
                        float bloomFactor = (s < 0.65f) ? 1.0f : fmaxf(0.40f, 1.0f - (s - 0.65f) / 0.35f);
                        PetalParams pFloret = {
                            .shape = PETAL_SHAPE_OVAL,
                            .wMax = r * 0.22f * bloomFactor,
                            .length = r * 0.60f * bloomFactor,
                            .exponent = 1.10f,
                            .curveAmount = r * 0.25f * bloomFactor,
                            .twistAmount = 0.04f,
                        };
                        for (int j = 0; j < 2; j++) {
                            float angle = flower->rotationDeg * DEG2RAD + (float)k * phaseSpiral + (float)j * PI;
                            Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                            Vector3 side = {-dir.z, 0.0f, dir.x};
                            Nature_AddParametricPetal(&mesh, &cursor, tierHead, dir, side, &pFloret,
                                                     phase, cBase, cMid, cTip);
                        }
                    }
                    Vector3 tipApex = {head.x, head.y + spH, head.z};
                    Nature_AddFlowerCenterDome(&mesh, &cursor, tipApex, r * 0.16f, r * 0.18f, phase, centerColor);
                    break;
                }
                case 3: { // 6-petal Bell / Campanula / Star: lanceolate reflexed petals (72) + dome (18) = 90 verts
                    PetalParams pBell = {
                        .shape = PETAL_SHAPE_LANCEOLATE,
                        .wMax = r * 0.34f,
                        .length = r * 1.12f,
                        .tPeak = 0.28f,
                        .curveAmount = r * 0.36f,
                        .twistAmount = 0.06f,
                    };
                    for (int k = 0; k < 6; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 1.04719755f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pBell,
                                                 phase, cBase, cMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.24f, r * 0.14f, phase, centerColor);
                    break;
                }
            }
        }
    }
    field.textured = (petalTexturePath != NULL);
    field.model = Nature_ModelFromMesh(mesh, Nature_GetShader(field.textured));
    field.farModel = Nature_BuildFlowerFarModel(placements, count, field.textured,
                                                 atlasColumns, atlasRows);
    field.farReady = field.farModel.meshCount > 0;
    if (GfxQuality_Get() >= GFX_MED)
        field.shadowModel = Nature_BuildFlowerShadowModel(placements, count);
    field.shadowReady = field.shadowModel.meshCount > 0;
    field.alphaCutoff = alphaCutoff > 0.0f ? fminf(alphaCutoff, 0.9f) : 0.35f;
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

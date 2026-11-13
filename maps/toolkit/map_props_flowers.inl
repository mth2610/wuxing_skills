// Flower Field System (Optimized 3D Diamond-Cupped Petals, Analytical Vein/SSS UVs,
// 3D Far-LOD Blossoms, Front-to-Back Early-Z Batching, and Batched Shadow Casters).
// Split from map_props_nature.inl for clean modular domain separation.

static int s_flowerSpeciesOverride = -2; // -2 = uninitialized, -1 = Mixed, 0..7 = Single species

static void Nature_InitFlowerSpeciesOverride(void)
{
    if (s_flowerSpeciesOverride != -2)
        return;
    const char *envSp = getenv("WUXING_FLOWER_SPECIES");
    if (envSp != NULL && envSp[0] >= '0' && envSp[0] <= '7')
        s_flowerSpeciesOverride = (int)(envSp[0] - '0');
    else
        s_flowerSpeciesOverride = -1;
}

void MapProp_SetFlowerSpeciesOverride(int speciesVariant)
{
    if (speciesVariant < 0)
        s_flowerSpeciesOverride = -1;
    else
        s_flowerSpeciesOverride = speciesVariant & 7;
}

int MapProp_GetFlowerSpeciesOverride(void)
{
    Nature_InitFlowerSpeciesOverride();
    return s_flowerSpeciesOverride;
}

static inline int Nature_ResolveFlowerVariant(const MapFlowerPlacement *flower)
{
    Nature_InitFlowerSpeciesOverride();
    if (s_flowerSpeciesOverride >= 0)
        return s_flowerSpeciesOverride & 7;
    return ((int)flower->bloomVariant) & 7;
}

static inline Color Nature_ResolveFlowerPetalColor(const MapFlowerPlacement *flower, int resolvedVariant)
{
    Nature_InitFlowerSpeciesOverride();
    if (s_flowerSpeciesOverride >= 0)
    {
        bool accent = (flower->phase > 0.72f);
        return MapProp_GetFlowerSpeciesColor(resolvedVariant, accent);
    }
    return flower->petalColor;
}

// ============================================================================
// GPU-INSTANCED PARAMETRIC FLOWER PIPELINE ("Bách Hoa Cốc" 3-LOD Architecture)
// ============================================================================
#define FLOWER_PARAMETRIC_LODS 3
#define FLOWER_TEMPLATE_MAX_VERTS 64
#define FLOWER_TEMPLATE_MAX_INDICES 96

typedef struct
{
    unsigned int vao, vertices, indices;
    int vertexCount, indexCount;
} FlowerParametricTemplate;

static FlowerParametricTemplate s_flowerTemplates[FLOWER_PARAMETRIC_LODS] = {0};
static bool s_flowerTemplatesReady = false;
static Shader s_flowerParametricShader = {0};
static Shader s_flowerParametricShadowShader = {0};
static unsigned int s_flowerCachedShaderId[2] = {0, 0};
static int s_flowerInstanceTexLoc[2] = {-1, -1};
static int s_flowerSpeciesOverrideLoc[2] = {-1, -1};
static int s_flowerLodStrideLoc[2] = {-1, -1};
static int s_flowerWorldOffsetLoc[2] = {-1, -1};

static Shader FlowerParametric_GetShader(bool shadow)
{
    Shader *shader = shadow ? &s_flowerParametricShadowShader : &s_flowerParametricShader;
    int pass = shadow ? 1 : 0;
    if (shader->id == 0 || s_flowerCachedShaderId[pass] != shader->id)
    {
        if (shader->id == 0)
        {
            *shader = ResourceManager_LoadShader(
                shadow ? "maps/toolkit/shaders/flower_parametric_shadow.vs"
                       : "maps/toolkit/shaders/flower_parametric.vs",
                shadow ? "maps/toolkit/shaders/nature_shadow.fs"
                       : "maps/toolkit/shaders/flower_parametric.fs");
        }
        if (shader->id != 0 && shader->locs != NULL)
        {
            shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
            shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
            shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
            shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
            s_flowerInstanceTexLoc[pass] = GetShaderLocation(*shader, "u_flowerInstances");
            s_flowerSpeciesOverrideLoc[pass] = GetShaderLocation(*shader, "u_speciesOverride");
            s_flowerLodStrideLoc[pass] = GetShaderLocation(*shader, "u_lodStride");
            s_flowerWorldOffsetLoc[pass] = GetShaderLocation(*shader, "u_worldOffset");
            if (!shadow)
            {
                MapShadow_ConfigureShader(*shader);
                VFXLight_RegisterShader(*shader);
            }
            (void)Nature_GetCachedLocs(*shader);
            s_flowerCachedShaderId[pass] = shader->id;
        }
    }
    return *shader;
}

static void FlowerParametric_UnloadTemplate(FlowerParametricTemplate *tmpl)
{
    if (!tmpl)
        return;
    if (tmpl->vao)
        rlUnloadVertexArray(tmpl->vao);
    if (tmpl->vertices)
        rlUnloadVertexBuffer(tmpl->vertices);
    if (tmpl->indices)
        rlUnloadVertexBuffer(tmpl->indices);
    *tmpl = (FlowerParametricTemplate){0};
}

static bool FlowerParametric_BuildTemplate(FlowerParametricTemplate *tmpl, int lod)
{
    FlowerParametric_UnloadTemplate(tmpl);

    Vector3 verts[FLOWER_TEMPLATE_MAX_VERTS];
    unsigned short indices[FLOWER_TEMPLATE_MAX_INDICES];
    int vCount = 0, iCount = 0;

    // 0. Stem (part = 0): 2 segments for LOD0, 1 segment for LOD1/LOD2
    float tagStem = 0.0f;
    if (lod == 0)
    {
        unsigned short b = (unsigned short)vCount;
        verts[vCount++] = (Vector3){tagStem, 0.0f, 0.0f};
        verts[vCount++] = (Vector3){tagStem, 1.0f, 0.0f};
        verts[vCount++] = (Vector3){tagStem, 0.0f, 0.5f};
        verts[vCount++] = (Vector3){tagStem, 1.0f, 0.5f};
        verts[vCount++] = (Vector3){tagStem, 0.5f, 1.0f};
        indices[iCount++] = b + 0;
        indices[iCount++] = b + 1;
        indices[iCount++] = b + 2;
        indices[iCount++] = b + 1;
        indices[iCount++] = b + 3;
        indices[iCount++] = b + 2;
        indices[iCount++] = b + 2;
        indices[iCount++] = b + 3;
        indices[iCount++] = b + 4;
    }
    else
    {
        unsigned short b = (unsigned short)vCount;
        verts[vCount++] = (Vector3){tagStem, 0.0f, 0.0f};
        verts[vCount++] = (Vector3){tagStem, 1.0f, 0.0f};
        verts[vCount++] = (Vector3){tagStem, 0.5f, 1.0f};
        indices[iCount++] = b + 0;
        indices[iCount++] = b + 1;
        indices[iCount++] = b + 2;
    }

    // 1. Basal Sword Leaves (part = 1): 2 diamond leaves on LOD0
    if (lod == 0)
    {
        for (int l = 0; l < 2; l++)
        {
            float tagLeaf = (float)(1 * 16 + l);
            unsigned short b = (unsigned short)vCount;
            verts[vCount++] = (Vector3){tagLeaf, 0.5f, 0.0f};
            verts[vCount++] = (Vector3){tagLeaf, 1.0f, 0.38f};
            verts[vCount++] = (Vector3){tagLeaf, 0.5f, 1.0f};
            verts[vCount++] = (Vector3){tagLeaf, 0.0f, 0.38f};
            indices[iCount++] = b + 0;
            indices[iCount++] = b + 1;
            indices[iCount++] = b + 2;
            indices[iCount++] = b + 0;
            indices[iCount++] = b + 2;
            indices[iCount++] = b + 3;
        }
    }

    // 2. Outer Cupped Petals (part = 2): 6 diamond petals on LOD0, 6 tris on LOD1, 4 tris on LOD2
    int outerCount = (lod == 2) ? 4 : 6;
    for (int p = 0; p < outerCount; p++)
    {
        float tagPetal = (float)(2 * 16 + p);
        unsigned short b = (unsigned short)vCount;
        if (lod == 0)
        {
            verts[vCount++] = (Vector3){tagPetal, 0.5f, 0.0f};
            verts[vCount++] = (Vector3){tagPetal, 1.0f, 0.45f};
            verts[vCount++] = (Vector3){tagPetal, 0.5f, 1.0f};
            verts[vCount++] = (Vector3){tagPetal, 0.0f, 0.45f};
            indices[iCount++] = b + 0;
            indices[iCount++] = b + 1;
            indices[iCount++] = b + 2;
            indices[iCount++] = b + 0;
            indices[iCount++] = b + 2;
            indices[iCount++] = b + 3;
        }
        else
        {
            verts[vCount++] = (Vector3){tagPetal, 0.5f, 0.0f};
            verts[vCount++] = (Vector3){tagPetal, 1.0f, 0.88f};
            verts[vCount++] = (Vector3){tagPetal, 0.0f, 0.88f};
            indices[iCount++] = b + 0;
            indices[iCount++] = b + 1;
            indices[iCount++] = b + 2;
        }
    }

    // 3. Inner Petals / Stamens (part = 3): 6 on LOD0, 3 on LOD1, 0 on LOD2
    int innerCount = (lod == 0) ? 6 : ((lod == 1) ? 3 : 0);
    for (int p = 0; p < innerCount; p++)
    {
        float tagInner = (float)(3 * 16 + p);
        unsigned short b = (unsigned short)vCount;
        verts[vCount++] = (Vector3){tagInner, 0.5f, 0.0f};
        verts[vCount++] = (Vector3){tagInner, 1.0f, 0.78f};
        verts[vCount++] = (Vector3){tagInner, 0.0f, 0.78f};
        indices[iCount++] = b + 0;
        indices[iCount++] = b + 1;
        indices[iCount++] = b + 2;
    }

    tmpl->vao = rlLoadVertexArray();
    if (!tmpl->vao || !rlEnableVertexArray(tmpl->vao))
    {
        FlowerParametric_UnloadTemplate(tmpl);
        return false;
    }
    tmpl->vertices = rlLoadVertexBuffer(verts, vCount * (int)sizeof(Vector3), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, sizeof(Vector3), 0);
    rlEnableVertexAttribute(0);
    tmpl->indices = rlLoadVertexBufferElement(indices, iCount * (int)sizeof(unsigned short), false);
    rlDisableVertexArray();
    tmpl->vertexCount = vCount;
    tmpl->indexCount = iCount;
    if (tmpl->vao == 0 || tmpl->vertices == 0 || tmpl->indices == 0)
    {
        FlowerParametric_UnloadTemplate(tmpl);
        return false;
    }
    return true;
}

static bool FlowerParametric_Init(void)
{
    const char *renderer = getenv("WUXING_FLOWER_RENDERER");
    if (renderer && strcmp(renderer, "legacy") == 0)
        return false;
    if (s_flowerTemplatesReady)
        return true;

    Shader vis = FlowerParametric_GetShader(false);
    Shader shd = FlowerParametric_GetShader(true);
    if (vis.id == 0 || vis.id == rlGetShaderIdDefault() ||
        shd.id == 0 || shd.id == rlGetShaderIdDefault() ||
        s_flowerInstanceTexLoc[0] < 0)
        return false;

    for (int lod = 0; lod < FLOWER_PARAMETRIC_LODS; lod++)
    {
        if (!FlowerParametric_BuildTemplate(&s_flowerTemplates[lod], lod))
        {
            for (int j = 0; j < lod; j++)
                FlowerParametric_UnloadTemplate(&s_flowerTemplates[j]);
            return false;
        }
    }
    s_flowerTemplatesReady = true;
    return true;
}

static Model Nature_BuildFlowerShadowModel(const MapFlowerPlacement *placements, int count)
{
    Mesh mesh = Nature_AllocMesh(count * 6);
    int cursor = 0;
    const Vector2 uv[6] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {0.0f, 0.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    const Color rootShade = {245, 245, 245, 255};
    const Color tipShade = {210, 210, 210, 255};
    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        float width = fmaxf(flower->bloomRadius * 1.48f, 0.088f);
        Vector3 root = flower->position;
        root.y += 0.016f;
        Vector3 encoded = {flower->height * 1.08f, width, flower->phase};
        for (int vertex = 0; vertex < 6; vertex++)
        {
            Color shade = (vertex < 2 || vertex == 3) ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, flower->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

static inline bool Nature_KeepFarFlower(const MapFlowerPlacement *flower)
{
    unsigned int ux, uz, up;
    memcpy(&ux, &flower->position.x, sizeof(ux));
    memcpy(&uz, &flower->position.z, sizeof(uz));
    memcpy(&up, &flower->phase, sizeof(up));
    unsigned int seed = (ux * 73856093u) ^ (uz * 19349663u) ^ (up * 83492791u);
    return Nature_Hash01(seed) < 0.80f;
}

static Color Nature_GetSpeciesCenterColor(int variant)
{
    switch (variant & 7)
    {
    case 0:
        return (Color){248, 185, 22, 255}; // 0: Bạch Cúc / Tuyết Liên: nhụy hoàng kim
    case 1:
        return (Color){195, 115, 18, 255}; // 1: Hoàng Anh / Kim Cúc: nhụy cam hổ phách
    case 2:
        return (Color){250, 175, 68, 255}; // 2: Bỉ Ngạn Hoa: đầu tua nhụy ánh kim đỏ
    case 3:
        return (Color){165, 245, 232, 255}; // 3: U Lan Linh Thảo: tâm ngọc bích phát sáng
    case 4:
        return (Color){238, 165, 24, 255}; // 4: Vân Mộng Đào Thảo: nhụy mật ong
    case 5:
        return (Color){245, 200, 32, 255}; // 5: Tử Linh Đang: nhụy chuông vàng tươi
    case 6:
        return (Color){92, 58, 128, 255}; // 6: Huân Y Thảo: nhụy tím sẫm
    case 7:
        return (Color){242, 198, 42, 255}; // 7: Ngọc Lan: nhụy kim quang
    default:
        return (Color){248, 185, 22, 255};
    }
}

// [TỐI ƯU 3D FAR-LOD & SHADOW CASTER]:
// Mỗi bông hoa ở xa được dựng bằng 1 tam giác thân + 3 cánh tam giác khum 3D (12 đỉnh = 4 tam giác/bông).
static Model Nature_BuildFlowerFarModel(const MapFlowerPlacement *placements, int count,
                                        Color stemColor, bool texturedBloom,
                                        int atlasColumns, int atlasRows)
{
    int selected = 0;
    for (int i = 0; i < count; i++)
    {
        if (Nature_KeepFarFlower(&placements[i]))
            selected++;
    }
    bool forceFirst = (selected == 0);
    if (forceFirst)
        selected = 1;

    int vertsPerFarFlower = texturedBloom ? 6 : 12;
    Mesh mesh = Nature_AllocMesh(selected * vertsPerFarFlower);
    int cursor = 0;
    const float invCols = 1.0f / (float)atlasColumns;
    const float invRows = 1.0f / (float)atlasRows;
    const float insetU = 0.008f * invCols;
    const float insetV = 0.008f * invRows;
    const int variantCount = atlasColumns * atlasRows;

    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        if (!Nature_KeepFarFlower(flower) && !(forceFirst && i == 0))
            continue;
        int variant = Nature_ResolveFlowerVariant(flower);
        Color petalCol = Nature_ResolveFlowerPetalColor(flower, variant);
        Color centerCol = Nature_GetSpeciesCenterColor(variant);

        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 3.14f;
        float lean = h * 0.22f;
        float sinLean, cosLean;
        Nature_FastSinCos(leanAngle, &sinLean, &cosLean);
        Vector3 head = {base.x + cosLean * lean, base.y + h * 0.94f,
                        base.z + sinLean * lean};

        float stemAngle = flower->rotationDeg * DEG2RAD;
        float sinStem, cosStem;
        Nature_FastSinCos(stemAngle, &sinStem, &cosStem);
        float sinPhase, cosPhase;
        Nature_FastSinCos(phase * 6.28f, &sinPhase, &cosPhase);
        float tiltAmount = 0.22f + 0.08f * sinPhase;

        Vector3 headNormal = Nature_FastNormalize((Vector3){cosLean * tiltAmount, 0.96f, sinLean * tiltAmount});
        Vector3 refRight = {cosStem, 0.0f, sinStem};
        Vector3 right = Nature_FastNormalize(Vector3Subtract(refRight, Vector3Scale(headNormal, Vector3DotProduct(refRight, headNormal))));
        Vector3 forward = Vector3CrossProduct(headNormal, right);
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float radius = flower->bloomRadius * petalScale * 1.25f;

        if (texturedBloom)
        {
            int cell = variant % variantCount;
            int column = cell % atlasColumns;
            int row = cell / atlasColumns;
            Vector4 uvRect = {
                (float)column * invCols + insetU,
                (float)row * invRows + insetV,
                (float)(column + 1) * invCols - insetU,
                (float)(row + 1) * invRows - insetV,
            };
            Vector3 p0 = Vector3Add(head, Vector3Scale(Vector3Add(Vector3Negate(right), Vector3Negate(forward)), radius));
            Vector3 p1 = Vector3Add(head, Vector3Scale(Vector3Subtract(right, forward), radius));
            Vector3 p2 = Vector3Add(head, Vector3Scale(Vector3Add(right, forward), radius));
            Vector3 p3 = Vector3Add(head, Vector3Scale(Vector3Subtract(forward, right), radius));
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, headNormal,
                                    phase, petalCol, uvRect);
        }
        else
        {
            float stemHalfW = fmaxf(0.012f, flower->bloomRadius * 0.16f);
            Vector3 stemSide = Vector3Scale(right, stemHalfW);
            Color rootStemCol = Nature_ScaleColor(stemColor, 0.78f);
            int stemIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, Vector3Subtract(base, stemSide), right, phase, 0.0f, rootStemCol);
            Nature_SetVertex(&mesh, cursor++, Vector3Add(base, stemSide), right, phase, 0.0f, rootStemCol);
            Nature_SetVertex(&mesh, cursor++, head, headNormal, phase, 0.90f, stemColor);
            Nature_SetUv2(&mesh, stemIdx + 0, -1.0f, 0.0f);
            Nature_SetUv2(&mesh, stemIdx + 1, 1.0f, 0.0f);
            Nature_SetUv2(&mesh, stemIdx + 2, 0.0f, 0.9f);

            Color cCore = Nature_LerpColor(centerCol, petalCol, 0.45f);
            Color cTip = Nature_ScaleColor(petalCol, 1.08f);
            float cupLift = (variant == 5) ? (-radius * 0.25f) : (radius * 0.28f);
            Vector3 normalLift = Vector3Scale(headNormal, cupLift);
            for (int p = 0; p < 3; p++)
            {
                float aMid = stemAngle + (float)p * 2.0943951f;
                float sinL, cosL, sinR, cosR;
                Nature_FastSinCos(aMid - 0.72f, &sinL, &cosL);
                Nature_FastSinCos(aMid + 0.72f, &sinR, &cosR);
                Vector3 dL = Vector3Add(Vector3Scale(right, cosL * radius), Vector3Scale(forward, sinL * radius));
                Vector3 dR = Vector3Add(Vector3Scale(right, cosR * radius), Vector3Scale(forward, sinR * radius));
                Vector3 pL = Vector3Add(head, Vector3Add(dL, normalLift));
                Vector3 pR = Vector3Add(head, Vector3Add(dR, normalLift));
                Vector3 nPetal = Nature_FastNormalize(Vector3Add(headNormal, Vector3Scale(Vector3Add(dL, dR), 0.35f)));

                int baseIdx = cursor;
                Nature_SetVertex(&mesh, cursor++, head, nPetal, phase, 0.88f, cCore);
                Nature_SetVertex(&mesh, cursor++, pL, nPetal, phase, 1.00f, cTip);
                Nature_SetVertex(&mesh, cursor++, pR, nPetal, phase, 1.00f, cTip);
                Nature_SetUv2(&mesh, baseIdx + 0, 0.0f, 0.0f);
                Nature_SetUv2(&mesh, baseIdx + 1, -1.0f, 1.0f);
                Nature_SetUv2(&mesh, baseIdx + 2, 1.0f, 1.0f);
            }
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(true));
}

// Số lượng đỉnh phần Đài Hoa 3D theo từng loài (Variant 0..7)
static inline int Nature_FlowerBloomVertexCount(int variant, bool textured)
{
    if (textured)
        return 30;
    switch (variant & 7)
    {
    case 0:
    case 4:
    case 7:
        return 108;
    case 1:
    case 2:
        return 96;
    case 3:
    case 5:
    case 6:
        return 84;
    default:
        return 96;
    }
}

typedef enum
{
    PETAL_SHAPE_OVAL,
    PETAL_SHAPE_LANCEOLATE,
    PETAL_SHAPE_OBCORDATE,
    PETAL_SHAPE_SPATULATE,
} PetalShapeType;

typedef struct
{
    PetalShapeType shape;
    float wMax;
    float length;
    float exponent;
    float tPeak;
    float notchDepth;
    float curveAmount;
    float twistAmount;
} PetalParams;

// Tiền tính toán tham số hình học cánh hoa 1 lần/bông thay vì lặp lại powf/sinf/expf cho từng cánh
typedef struct
{
    float len;
    float w0, w1, w2;
    float dy0, dy1, dy2, dyApex;
    float cup0, cup1, cup2;
    float sin1, cos1, sin2, cos2;
    bool hasTwist;
} PetalEvalCache;

static inline float Nature_EvalPetalWidth(float t, const PetalParams *p)
{
    if (t <= 0.0f)
        return 0.002f;
    if (t >= 1.0f)
        return 0.000f;

    switch (p->shape)
    {
    case PETAL_SHAPE_OVAL:
    {
        float s, c;
        Nature_FastSinCos(PI * powf(t, 0.55f), &s, &c);
        if (s <= 0.0f)
            return 0.0f;
        float n = p->exponent > 0.0f ? p->exponent : 0.60f;
        float baseTaper = fminf(1.0f, t * (1.0f / 0.15f));
        return p->wMax * powf(s, n) * baseTaper;
    }
    case PETAL_SHAPE_LANCEOLATE:
    {
        float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.30f;
        if (t < tPeak)
            return p->wMax * sqrtf(t / tPeak);
        else
            return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.80f);
    }
    case PETAL_SHAPE_OBCORDATE:
    {
        float s, c;
        Nature_FastSinCos(PI * t, &s, &c);
        if (s < 0.0f)
            s = 0.0f;
        float wBase = p->wMax * powf(s, 1.20f);
        float notch = 0.0f;
        if (t > 0.80f)
        {
            float dt = (t - 1.0f) * (1.0f / 0.08f);
            float depth = p->notchDepth > 0.0f ? p->notchDepth : p->wMax * 0.40f;
            notch = depth * expf(-0.5f * dt * dt);
        }
        return fmaxf(0.003f, wBase - notch);
    }
    case PETAL_SHAPE_SPATULATE:
    {
        float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.75f;
        if (t < tPeak)
            return p->wMax * powf(t / tPeak, 0.65f);
        else
            return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.40f);
    }
    default:
    {
        float s, c;
        Nature_FastSinCos(PI * t, &s, &c);
        return p->wMax * s;
    }
    }
}

static inline float Nature_EvalPetalElevation(float t, const PetalParams *p)
{
    float s, c;
    Nature_FastSinCos(t * 1.5707963f, &s, &c);
    float t2 = t * t;
    float conical = p->length * 0.26f * (t * 0.85f + 0.15f * t2);
    return conical + p->curveAmount * (s * 1.25f - 0.25f * t2);
}

static inline void Nature_PreparePetalEvalCache(const PetalParams *p, PetalEvalCache *out)
{
    const float t0 = 0.05f;
    const float t1 = 0.48f;
    const float t2 = 0.84f;

    out->len = p->length;
    out->w0 = Nature_EvalPetalWidth(t0, p);
    out->w1 = Nature_EvalPetalWidth(t1, p);
    out->w2 = Nature_EvalPetalWidth(t2, p);

    out->dy0 = Nature_EvalPetalElevation(t0, p) + 0.003f;
    out->dy1 = Nature_EvalPetalElevation(t1, p);
    out->dy2 = Nature_EvalPetalElevation(t2, p);
    out->dyApex = Nature_EvalPetalElevation(1.0f, p);

    out->cup0 = out->w0 * 0.26f;
    out->cup1 = out->w1 * 0.38f;
    out->cup2 = out->w2 * 0.26f;

    out->hasTwist = (fabsf(p->twistAmount) > 0.001f);
    if (out->hasTwist)
    {
        Nature_FastSinCos(p->twistAmount * t1, &out->sin1, &out->cos1);
        Nature_FastSinCos(p->twistAmount * t2, &out->sin2, &out->cos2);
    }
    else
    {
        out->sin1 = 0.0f;
        out->cos1 = 1.0f;
        out->sin2 = 0.0f;
        out->cos2 = 1.0f;
    }
}

// Dựng cánh hoa 3D 2 đốt khum lòng chảo (12 đỉnh = 4 tam giác) kèm tọa độ (u, v) chuẩn cho SSS & Gân hoa
static void Nature_AddParametricPetalCached(Mesh *mesh, int *cursor,
                                            Vector3 head, Vector3 headNormal, Vector3 dir, Vector3 side,
                                            const PetalEvalCache *ec, float phase,
                                            Color cBase, Color cMid, Color cTip)
{
    const float len = ec->len;
    const float t0 = 0.05f;
    const float t1 = 0.48f;
    const float t2 = 0.84f;

    Vector3 side1 = side;
    Vector3 side2 = side;
    if (ec->hasTwist)
    {
        side1 = Vector3Add(Vector3Scale(side, ec->cos1), Vector3Scale(headNormal, ec->sin1 * len * 0.12f));
        side2 = Vector3Add(Vector3Scale(side, ec->cos2), Vector3Scale(headNormal, ec->sin2 * len * 0.16f));
    }

    Vector3 p0 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t0), Vector3Scale(headNormal, ec->dy0)));
    Vector3 p1 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t1), Vector3Scale(headNormal, ec->dy1)));
    Vector3 p2 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t2), Vector3Scale(headNormal, ec->dy2)));
    Vector3 pApex = Vector3Add(head, Vector3Add(Vector3Scale(dir, len), Vector3Scale(headNormal, ec->dyApex)));

    Vector3 b0 = Vector3Add(Vector3Subtract(p0, Vector3Scale(side, ec->w0)), Vector3Scale(headNormal, ec->cup0));
    Vector3 b1 = Vector3Add(Vector3Add(p0, Vector3Scale(side, ec->w0)), Vector3Scale(headNormal, ec->cup0));

    Vector3 m0 = Vector3Add(Vector3Subtract(p1, Vector3Scale(side1, ec->w1)), Vector3Scale(headNormal, ec->cup1));
    Vector3 m1 = Vector3Add(Vector3Add(p1, Vector3Scale(side1, ec->w1)), Vector3Scale(headNormal, ec->cup1));

    Vector3 s1 = Vector3Add(Vector3Add(p2, Vector3Scale(side2, ec->w2)), Vector3Scale(headNormal, ec->cup2));

    Vector3 baseN0 = Vector3Add(Vector3Scale(dir, 0.22f), Vector3Scale(headNormal, 0.86f));
    Vector3 sideN0 = Vector3Scale(side, 0.40f);
    Vector3 nL0 = Nature_FastNormalize(Vector3Add(baseN0, sideN0));
    Vector3 nR0 = Nature_FastNormalize(Vector3Subtract(baseN0, sideN0));

    Vector3 baseN1 = Vector3Add(Vector3Scale(dir, 0.45f), Vector3Scale(headNormal, 0.76f));
    Vector3 sideN1 = Vector3Scale(side1, 0.44f);
    Vector3 nL1 = Nature_FastNormalize(Vector3Add(baseN1, sideN1));
    Vector3 nR1 = Nature_FastNormalize(Vector3Subtract(baseN1, sideN1));

    Vector3 nTip = Nature_FastNormalize(Vector3Add(Vector3Scale(dir, 0.72f), Vector3Scale(headNormal, 0.64f)));

    int baseIdx = *cursor;

    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.76f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, b1, nR0, phase, 0.76f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.90f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.76f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.90f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.90f, cMid);

    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.90f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.90f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nTip, phase, 0.96f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.90f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nTip, phase, 0.96f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTip, phase, 1.00f, cTip);

    Nature_SetUv2(mesh, baseIdx + 0, -0.65f, t0);
    Nature_SetUv2(mesh, baseIdx + 1, 0.65f, t0);
    Nature_SetUv2(mesh, baseIdx + 2, 1.00f, t1);
    Nature_SetUv2(mesh, baseIdx + 3, -0.65f, t0);
    Nature_SetUv2(mesh, baseIdx + 4, 1.00f, t1);
    Nature_SetUv2(mesh, baseIdx + 5, -1.00f, t1);

    Nature_SetUv2(mesh, baseIdx + 6, -1.00f, t1);
    Nature_SetUv2(mesh, baseIdx + 7, 1.00f, t1);
    Nature_SetUv2(mesh, baseIdx + 8, 0.75f, t2);
    Nature_SetUv2(mesh, baseIdx + 9, -1.00f, t1);
    Nature_SetUv2(mesh, baseIdx + 10, 0.75f, t2);
    Nature_SetUv2(mesh, baseIdx + 11, 0.00f, 1.0f);
}

// Đài nhụy 3D hình chóp tứ giác khum (12 đỉnh = 4 tam giác)
static void Nature_AddFlowerCenterDome(Mesh *mesh, int *cursor,
                                       Vector3 head, Vector3 headNormal,
                                       Vector3 headRight, Vector3 headForward,
                                       float cR, float cHeight, float phase, Color centerColor)
{
    static const float kQuadCos[5] = {1.0f, 0.0f, -1.0f, 0.0f, 1.0f};
    static const float kQuadSin[5] = {0.0f, 1.0f, 0.0f, -1.0f, 0.0f};
    static const float kMidCos[4] = {0.70710678f, -0.70710678f, -0.70710678f, 0.70710678f};
    static const float kMidSin[4] = {0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f};

    Vector3 apex = Vector3Add(head, Vector3Scale(headNormal, cHeight * 1.45f));
    Vector3 rimLift = Vector3Add(head, Vector3Scale(headNormal, 0.004f));
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    Color rimColor = Nature_ScaleColor(centerColor, 0.52f);

    for (int s = 0; s < 4; s++)
    {
        Vector3 rad0 = Vector3Add(Vector3Scale(headRight, kQuadCos[s] * cR),
                                  Vector3Scale(headForward, kQuadSin[s] * cR));
        Vector3 rad1 = Vector3Add(Vector3Scale(headRight, kQuadCos[s + 1] * cR),
                                  Vector3Scale(headForward, kQuadSin[s + 1] * cR));
        Vector3 r0 = Vector3Add(rimLift, rad0);
        Vector3 r1 = Vector3Add(rimLift, rad1);

        Vector3 midRad = Vector3Add(Vector3Scale(headRight, kMidCos[s] * 0.55f),
                                    Vector3Scale(headForward, kMidSin[s] * 0.55f));
        Vector3 norm = Nature_FastNormalize(Vector3Add(midRad, Vector3Scale(headNormal, 0.84f)));

        int baseIdx = *cursor;
        Nature_SetVertex(mesh, (*cursor)++, apex, headNormal, phase, 0.96f, apexColor);
        Nature_SetVertex(mesh, (*cursor)++, r0, norm, phase, 0.86f, rimColor);
        Nature_SetVertex(mesh, (*cursor)++, r1, norm, phase, 0.86f, rimColor);
        Nature_SetUv2(mesh, baseIdx + 0, 0.0f, 0.15f);
        Nature_SetUv2(mesh, baseIdx + 1, -0.5f, 0.05f);
        Nature_SetUv2(mesh, baseIdx + 2, 0.5f, 0.05f);
    }
}

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor,
                                         const char *petalTexturePath, float alphaCutoff,
                                         int atlasColumns, int atlasRows)
{
    MapFlowerField field = {0};
    if (!placements || count <= 0)
        return field;
    Vector3 boundsMin = placements[0].position;
    Vector3 boundsMax = placements[0].position;
    boundsMin.x -= placements[0].bloomRadius * 1.5f;
    boundsMin.z -= placements[0].bloomRadius * 1.5f;
    boundsMax.x += placements[0].bloomRadius * 1.5f;
    boundsMax.y += placements[0].height + placements[0].bloomRadius * 1.5f;
    boundsMax.z += placements[0].bloomRadius * 1.5f;
    (void)NatureShadow_GetShader();
    field.textured = (petalTexturePath != NULL);
    if (atlasColumns < 1)
        atlasColumns = 1;
    if (atlasRows < 1)
        atlasRows = 1;

    GfxQuality quality = GfxQuality_Get();
    const int kStemAndFoliageVerts = 30;
    int vertexCount = 0;
    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        int variant = Nature_ResolveFlowerVariant(flower);
        float extent = flower->bloomRadius * 1.6f;
        boundsMin.x = fminf(boundsMin.x, flower->position.x - extent);
        boundsMin.y = fminf(boundsMin.y, flower->position.y);
        boundsMin.z = fminf(boundsMin.z, flower->position.z - extent);
        boundsMax.x = fmaxf(boundsMax.x, flower->position.x + extent);
        boundsMax.y = fmaxf(boundsMax.y, flower->position.y + flower->height + extent);
        boundsMax.z = fmaxf(boundsMax.z, flower->position.z + extent);
        vertexCount += kStemAndFoliageVerts + Nature_FlowerBloomVertexCount(variant, field.textured);
    }

    const char *envFlowerParametric = getenv("WUXING_FLOWER_PARAMETRIC");
    if (envFlowerParametric && strcmp(envFlowerParametric, "1") == 0 && !field.textured && FlowerParametric_Init())
    {
        int rows = (count + 255) / 256;
        if (rows < 1)
            rows = 1;
        float *pixels = MemAlloc((unsigned int)(rows * 512 * 4) * sizeof(float));
        if (pixels)
        {
            memset(pixels, 0, (size_t)(rows * 512 * 4) * sizeof(float));
            for (int i = 0; i < count; i++)
            {
                const MapFlowerPlacement *flower = &placements[i];
                int row = i >> 8;
                int col = (i & 255) * 2;
                int base = (row * 512 + col) * 4;
                float fracPhase = flower->phase - floorf(flower->phase);
                if (fracPhase < 0.0f)
                    fracPhase += 1.0f;
                int rawVar = ((int)flower->bloomVariant) & 7;
                float pScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
                int packedRGB = ((int)flower->petalColor.r << 16) |
                                ((int)flower->petalColor.g << 8) |
                                (int)flower->petalColor.b;

                pixels[base + 0] = flower->position.x;
                pixels[base + 1] = flower->position.y;
                pixels[base + 2] = flower->position.z;
                pixels[base + 3] = (float)rawVar * 100.0f + fminf(fracPhase, 0.9999f);

                pixels[base + 4] = flower->height;
                pixels[base + 5] = flower->bloomRadius * pScale;
                pixels[base + 6] = flower->rotationDeg * DEG2RAD;
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
                field.parametric = true;
                field.instanceCount = count;
                field.farReady = true;
                field.shadowReady = false;
                field.alphaCutoff = 0.0f;
                field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
                field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f;
                field.drawDistance = 125.0f;
                field.lodDistance = 28.0f;
                field.shadowDistance = 20.0f;
                field.ready = true;
                return field;
            }
        }
    }

    // Mobile Low: dùng trực tiếp mô hình 3D tối giản 12 đỉnh/bông để giảm ~88% bộ nhớ đỉnh & load time
    if (quality == GFX_LOW && !field.textured)
    {
        field.textured = false;
        field.model = Nature_BuildFlowerFarModel(placements, count, stemColor, false,
                                                 atlasColumns, atlasRows);
        field.farReady = false;
        field.shadowReady = false;
        field.alphaCutoff = 0.35f;
        field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
        field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f;
        field.drawDistance = 56.0f;
        field.lodDistance = 18.0f;
        field.shadowDistance = 0.0f;
        field.ready = field.model.meshCount > 0;
        return field;
    }

    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    const float invCols = 1.0f / (float)atlasColumns;
    const float invRows = 1.0f / (float)atlasRows;
    const float insetU = 0.008f * invCols;
    const float insetV = 0.008f * invRows;
    const int variantCount = atlasColumns * atlasRows;

    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        int variant = Nature_ResolveFlowerVariant(flower);
        Color petalColor = Nature_ResolveFlowerPetalColor(flower, variant);

        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float r = flower->bloomRadius * petalScale;
        float stemAngle = flower->rotationDeg * DEG2RAD;

        // 1. Basal leaf blades
        float basalScale = (variant == 2) ? 0.12f : ((variant == 3) ? 1.42f : 1.0f);
        Color basalRoot = {34, 58, 24, 255};
        Color basalTip = {66, 112, 44, 255};
        for (int b = 0; b < 2; b++)
        {
            float bAngle = stemAngle + (float)b * PI + 0.42f;
            float sinB, cosB;
            Nature_FastSinCos(bAngle, &sinB, &cosB);
            Vector3 bDir = {cosB, 0.0f, sinB};
            Vector3 bSide = {-bDir.z * 0.024f * basalScale, 0.0f, bDir.x * 0.024f * basalScale};
            float bLen = fmaxf(0.090f, flower->bloomRadius * 1.20f) * basalScale;
            Vector3 r0 = {base.x - bSide.x, base.y + 0.002f, base.z - bSide.z};
            Vector3 r1 = {base.x + bSide.x, base.y + 0.002f, base.z + bSide.z};
            Vector3 rTip = {base.x + bDir.x * bLen, base.y + 0.036f * basalScale, base.z + bDir.z * bLen};
            int bIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, r0, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.0f, basalRoot);
            Nature_SetVertex(&mesh, cursor++, r1, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.0f, basalRoot);
            Nature_SetVertex(&mesh, cursor++, rTip, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.32f, basalTip);
            Nature_SetUv2(&mesh, bIdx + 0, -1.0f, 0.0f);
            Nature_SetUv2(&mesh, bIdx + 1, 1.0f, 0.0f);
            Nature_SetUv2(&mesh, bIdx + 2, 0.0f, 0.5f);
        }

        // 2. Cuống hoa cong mềm mại 2 đoạn
        float stemWidth = fmaxf(0.0052f, flower->bloomRadius * 0.078f);
        float stemLeanAngle = stemAngle * 0.73f + phase * 3.14f;
        float stemLean = h * 0.22f;
        float stemDirZ, stemDirX;
        Nature_FastSinCos(stemLeanAngle, &stemDirZ, &stemDirX);
        Vector3 stemP0 = base;
        Vector3 midStem = (Vector3){base.x + stemDirX * (stemLean * 0.38f), base.y + h * 0.52f, base.z + stemDirZ * (stemLean * 0.38f)};
        Vector3 head = (Vector3){base.x + stemDirX * stemLean, base.y + h * 0.94f, base.z + stemDirZ * stemLean};

        Color effStemCol = (variant == 2) ? (Color){78, 36, 38, 255} : stemColor;
        float sinStem, cosStem;
        Nature_FastSinCos(stemAngle, &sinStem, &cosStem);
        Vector3 sideA = {cosStem * stemWidth, 0.0f, sinStem * stemWidth};
        Vector3 normA = {-sinStem, 0.15f, cosStem};

        Vector3 s0_0 = Vector3Subtract(stemP0, sideA);
        Vector3 s0_1 = Vector3Add(stemP0, sideA);
        Vector3 s0_2 = Vector3Add(midStem, Vector3Scale(sideA, 0.82f));
        Vector3 s0_3 = Vector3Subtract(midStem, Vector3Scale(sideA, 0.82f));
        Nature_AddQuad(&mesh, &cursor, s0_0, s0_1, s0_2, s0_3, normA, phase, 0.0f, 0.5f,
                       Nature_ScaleColor(effStemCol, 0.75f), effStemCol);

        Vector3 s1_0 = s0_3;
        Vector3 s1_1 = s0_2;
        Vector3 s1_2 = Vector3Add(head, Vector3Scale(sideA, 0.55f));
        Vector3 s1_3 = Vector3Subtract(head, Vector3Scale(sideA, 0.55f));
        Nature_AddQuad(&mesh, &cursor, s1_0, s1_1, s1_2, s1_3, normA, phase, 0.5f, 0.90f,
                       effStemCol, Nature_ScaleColor(effStemCol, 1.08f));

        // 3. 2 Lá non giữa thân + 2 Đài hoa đỡ dưới đầu bông
        float stemLeafScale = (variant == 2) ? 0.15f : 1.0f;
        for (int leaf = 0; leaf < 2; leaf++)
        {
            float rootT = leaf == 0 ? 0.42f : 0.68f;
            float leafAngle = stemLeanAngle + (leaf == 0 ? 1.45f : -1.45f);
            float sinL, cosL;
            Nature_FastSinCos(leafAngle, &sinL, &cosL);
            Vector3 lDir = {cosL, 0.0f, sinL};
            Vector3 lSide = {-lDir.z * 0.015f * stemLeafScale, 0.0f, lDir.x * 0.015f * stemLeafScale};
            float lLen = fmaxf(0.054f, flower->bloomRadius * 0.82f) * stemLeafScale;
            Vector3 lRoot = Vector3Lerp(stemP0, head, rootT);
            Vector3 lTip = {lRoot.x + lDir.x * lLen, lRoot.y + lLen * 0.36f, lRoot.z + lDir.z * lLen};
            int lIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, Vector3Subtract(lRoot, lSide), (Vector3){0, 1, 0}, phase, rootT, effStemCol);
            Nature_SetVertex(&mesh, cursor++, Vector3Add(lRoot, lSide), (Vector3){0, 1, 0}, phase, rootT, effStemCol);
            Nature_SetVertex(&mesh, cursor++, lTip, (Vector3){0, 1, 0}, phase, rootT + 0.25f, Nature_ScaleColor(effStemCol, 1.22f));
            Nature_SetUv2(&mesh, lIdx + 0, -1.0f, 0.1f);
            Nature_SetUv2(&mesh, lIdx + 1, 1.0f, 0.1f);
            Nature_SetUv2(&mesh, lIdx + 2, 0.0f, 0.6f);
        }

        float calyxLen = r * 0.48f;
        Color calyxColor = Nature_ScaleColor(effStemCol, 0.88f);
        Color calyxTipColor = Nature_ScaleColor(calyxColor, 1.2f);
        for (int c = 0; c < 2; c++)
        {
            float cAngle = stemAngle + (float)c * PI + 0.785398f;
            float sinC, cosC;
            Nature_FastSinCos(cAngle, &sinC, &cosC);
            Vector3 cDir = {cosC, 0.0f, sinC};
            Vector3 cSide = {-cDir.z * calyxLen * 0.35f, 0.0f, cDir.x * calyxLen * 0.35f};
            Vector3 cBasePt = {head.x, head.y - stemWidth * 0.5f, head.z};
            Vector3 cTipPt = {head.x + cDir.x * calyxLen, head.y + stemWidth * 0.2f, head.z + cDir.z * calyxLen};
            int cIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, Vector3Subtract(cBasePt, cSide), (Vector3){0, 1, 0}, phase, 0.86f, calyxColor);
            Nature_SetVertex(&mesh, cursor++, Vector3Add(cBasePt, cSide), (Vector3){0, 1, 0}, phase, 0.86f, calyxColor);
            Nature_SetVertex(&mesh, cursor++, cTipPt, (Vector3){0, 1, 0}, phase, 0.92f, calyxTipColor);
            Nature_SetUv2(&mesh, cIdx + 0, -0.8f, 0.0f);
            Nature_SetUv2(&mesh, cIdx + 1, 0.8f, 0.0f);
            Nature_SetUv2(&mesh, cIdx + 2, 0.0f, 0.4f);
        }

        // 4. Lắp ráp Đài Hoa 3D theo từng loài đặc trưng
        float sinPhase, cosPhase;
        Nature_FastSinCos(phase * 6.28f, &sinPhase, &cosPhase);
        float tiltAmount = 0.22f + 0.08f * sinPhase;
        Vector3 headNormal = Nature_FastNormalize((Vector3){stemDirX * tiltAmount, 0.96f, stemDirZ * tiltAmount});
        Vector3 refRight = {cosStem, 0.0f, sinStem};
        Vector3 headRight = Nature_FastNormalize(Vector3Subtract(refRight, Vector3Scale(headNormal, Vector3DotProduct(refRight, headNormal))));
        Vector3 headForward = Vector3CrossProduct(headNormal, headRight);
        Color effCenterColor = Nature_GetSpeciesCenterColor(variant);
        if (centerColor.a > 0 && flower->bloomVariant < 0)
            effCenterColor = centerColor;

        if (field.textured)
        {
            int cell = variant % variantCount;
            int column = cell % atlasColumns;
            int row = cell / atlasColumns;
            Vector4 uvRect = {
                (float)column * invCols + insetU,
                (float)row * invRows + insetV,
                (float)(column + 1) * invCols - insetU,
                (float)(row + 1) * invRows - insetV,
            };

            Vector3 right = headRight;
            Vector3 forward = headForward;
            Vector3 p0 = {head.x + (-right.x - forward.x) * r, head.y, head.z + (-right.z - forward.z) * r};
            Vector3 p1 = {head.x + (right.x - forward.x) * r, head.y, head.z + (right.z - forward.z) * r};
            Vector3 p2 = {head.x + (right.x + forward.x) * r, head.y, head.z + (right.z + forward.z) * r};
            Vector3 p3 = {head.x + (-right.x + forward.x) * r, head.y, head.z + (-right.z + forward.z) * r};
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, headNormal,
                                    phase, petalColor, uvRect);

            float a1 = stemAngle + 0.785398f;
            float sinA1, cosA1;
            Nature_FastSinCos(a1, &sinA1, &cosA1);
            Vector3 r1 = {cosA1 * r, 0.0f, sinA1 * r};
            Vector3 f1 = {-sinA1 * r, 0.0f, cosA1 * r};
            float vH = r * 0.75f;
            Vector3 q0 = {head.x - r1.x, head.y - vH * 0.15f, head.z - r1.z};
            Vector3 q1 = {head.x + r1.x, head.y - vH * 0.15f, head.z + r1.z};
            Vector3 q2 = {head.x + r1.x, head.y + vH * 0.85f, head.z + r1.z};
            Vector3 q3 = {head.x - r1.x, head.y + vH * 0.85f, head.z - r1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, q0, q1, q2, q3, f1,
                                    phase, petalColor, uvRect);

            Vector3 k0 = {head.x - f1.x, head.y - vH * 0.15f, head.z - f1.z};
            Vector3 k1 = {head.x + f1.x, head.y - vH * 0.15f, head.z + f1.z};
            Vector3 k2 = {head.x + f1.x, head.y + vH * 0.85f, head.z + f1.z};
            Vector3 k3 = {head.x - f1.x, head.y + vH * 0.85f, head.z - f1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, k0, k1, k2, k3, r1,
                                    phase, petalColor, uvRect);

            Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.22f, r * 0.10f, phase, effCenterColor);
        }
        else
        {
            Color cBase = Nature_ScaleColor(petalColor, 0.68f);
            Color cMid = petalColor;
            Color cTip = Nature_ScaleColor(petalColor, 1.18f);

            switch (variant & 7)
            {
            case 0:
            case 4:
            case 7:
            {
                bool isLotus = (variant == 0 || variant == 7);
                PetalParams pOuter = {
                    .shape = isLotus ? PETAL_SHAPE_OVAL : PETAL_SHAPE_OBCORDATE,
                    .wMax = r * (isLotus ? 0.44f : 0.48f),
                    .length = r * 1.06f,
                    .exponent = isLotus ? 0.72f : 1.25f,
                    .notchDepth = isLotus ? 0.0f : (r * 0.16f),
                    .curveAmount = r * (isLotus ? 0.48f : 0.36f),
                    .twistAmount = 0.07f,
                };
                PetalEvalCache ecOuter;
                Nature_PreparePetalEvalCache(&pOuter, &ecOuter);
                for (int k = 0; k < 5; k++)
                {
                    float angle = (float)k * 1.256637f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetalCached(&mesh, &cursor, head, headNormal, dir, side, &ecOuter,
                                                    phase, cBase, cMid, cTip);
                }
                PetalParams pInner = {
                    .shape = isLotus ? PETAL_SHAPE_OVAL : PETAL_SHAPE_OBCORDATE,
                    .wMax = r * 0.38f,
                    .length = r * 0.80f,
                    .exponent = isLotus ? 0.68f : 1.15f,
                    .notchDepth = isLotus ? 0.0f : (r * 0.12f),
                    .curveAmount = r * (isLotus ? 0.64f : 0.52f),
                    .twistAmount = -0.09f,
                };
                PetalEvalCache ecInner;
                Nature_PreparePetalEvalCache(&pInner, &ecInner);
                Color cInBase = Nature_LerpColor(effCenterColor, cBase, 0.65f);
                Color cInMid = Nature_ScaleColor(cMid, 0.96f);
                for (int k = 0; k < 3; k++)
                {
                    float angle = 0.628318f + (float)k * 2.094395f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetalCached(&mesh, &cursor, head, headNormal, dir, side, &ecInner,
                                                    phase, cInBase, cInMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.28f, r * 0.16f, phase, effCenterColor);
                break;
            }
            case 1:
            {
                PetalParams pDaisy = {
                    .shape = PETAL_SHAPE_OVAL,
                    .wMax = r * 0.36f,
                    .length = r * 1.18f,
                    .exponent = 0.52f,
                    .curveAmount = r * 0.22f,
                    .twistAmount = 0.03f,
                };
                PetalEvalCache ecDaisy;
                Nature_PreparePetalEvalCache(&pDaisy, &ecDaisy);
                for (int k = 0; k < 7; k++)
                {
                    float angle = (float)k * 0.8975979f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetalCached(&mesh, &cursor, head, headNormal, dir, side, &ecDaisy,
                                                    phase, cBase, cMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.35f, r * 0.15f, phase, effCenterColor);
                break;
            }
            case 2:
            {
                PetalParams pSpider = {
                    .shape = PETAL_SHAPE_LANCEOLATE,
                    .wMax = r * 0.28f,
                    .length = r * 1.18f,
                    .tPeak = 0.26f,
                    .curveAmount = -r * 0.32f,
                    .twistAmount = 0.11f,
                };
                PetalEvalCache ecSpider;
                Nature_PreparePetalEvalCache(&pSpider, &ecSpider);
                for (int k = 0; k < 6; k++)
                {
                    float angle = (float)k * 1.04719755f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetalCached(&mesh, &cursor, head, headNormal, dir, side, &ecSpider,
                                                    phase, cBase, cMid, cTip);
                }
                Color stamenBase = cMid;
                Color stamenTip = effCenterColor;
                float stamenLen = r * 1.48f;
                float stamenLift = r * 0.72f;
                for (int k = 0; k < 6; k++)
                {
                    float angle = 0.5235988f + (float)k * 1.04719755f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3Scale(Vector3CrossProduct(headNormal, dir), r * 0.055f);
                    Vector3 sL = Vector3Subtract(head, side);
                    Vector3 sR = Vector3Add(head, side);
                    Vector3 sApex = Vector3Add(head, Vector3Add(Vector3Scale(dir, stamenLen), Vector3Scale(headNormal, stamenLift)));
                    Vector3 nStamen = Nature_FastNormalize(Vector3Add(headNormal, Vector3Scale(dir, 0.45f)));
                    int sIdx = cursor;
                    Nature_SetVertex(&mesh, cursor++, sL, nStamen, phase, 0.88f, stamenBase);
                    Nature_SetVertex(&mesh, cursor++, sR, nStamen, phase, 0.88f, stamenBase);
                    Nature_SetVertex(&mesh, cursor++, sApex, nStamen, phase, 1.00f, stamenTip);
                    Nature_SetUv2(&mesh, sIdx + 0, -0.8f, 0.2f);
                    Nature_SetUv2(&mesh, sIdx + 1, 0.8f, 0.2f);
                    Nature_SetUv2(&mesh, sIdx + 2, 0.0f, 1.0f);
                }
                Vector3 c0 = Vector3Add(head, Vector3Scale(headRight, -r * 0.16f));
                Vector3 c1 = Vector3Add(head, Vector3Scale(headRight, r * 0.16f));
                Vector3 c2 = Vector3Add(head, Vector3Add(Vector3Scale(headForward, r * 0.16f), Vector3Scale(headNormal, r * 0.12f)));
                Vector3 c3 = Vector3Add(head, Vector3Add(Vector3Scale(headForward, -r * 0.16f), Vector3Scale(headNormal, r * 0.12f)));
                Nature_SetVertex(&mesh, cursor++, c0, headNormal, phase, 0.92f, cBase);
                Nature_SetVertex(&mesh, cursor++, c1, headNormal, phase, 0.92f, cBase);
                Nature_SetVertex(&mesh, cursor++, c2, headNormal, phase, 0.96f, stamenTip);
                Nature_SetVertex(&mesh, cursor++, c0, headNormal, phase, 0.92f, cBase);
                Nature_SetVertex(&mesh, cursor++, c2, headNormal, phase, 0.96f, stamenTip);
                Nature_SetVertex(&mesh, cursor++, c3, headNormal, phase, 0.96f, stamenTip);
                break;
            }
            case 5:
            case 6:
            {
                bool isBell = (variant == 5);
                float spH = r * 2.1f;
                float phaseSpiral = 0.48f;
                for (int k = 0; k < 3; k++)
                {
                    float s = 0.24f + (float)k * 0.32f;
                    Vector3 tierHead = Vector3Add(head, Vector3Scale(headNormal, spH * s));
                    float bloomFactor = (s < 0.68f) ? 1.0f : fmaxf(0.48f, 1.0f - (s - 0.68f) * (1.0f / 0.35f));
                    PetalParams pFloret = {
                        .shape = PETAL_SHAPE_OVAL,
                        .wMax = r * 0.28f * bloomFactor,
                        .length = r * 0.68f * bloomFactor,
                        .exponent = 1.05f,
                        .curveAmount = r * (isBell ? -0.22f : 0.26f) * bloomFactor,
                        .twistAmount = 0.04f,
                    };
                    PetalEvalCache ecFloret;
                    Nature_PreparePetalEvalCache(&pFloret, &ecFloret);
                    for (int j = 0; j < 2; j++)
                    {
                        float angle = (float)k * phaseSpiral + (float)j * PI;
                        float sinJ, cosJ;
                        Nature_FastSinCos(angle, &sinJ, &cosJ);
                        Vector3 dir = Vector3Add(Vector3Scale(headRight, cosJ), Vector3Scale(headForward, sinJ));
                        Vector3 side = Vector3CrossProduct(headNormal, dir);
                        Nature_AddParametricPetalCached(&mesh, &cursor, tierHead, headNormal, dir, side, &ecFloret,
                                                        phase, cBase, cMid, cTip);
                    }
                }
                Vector3 tipApex = Vector3Add(head, Vector3Scale(headNormal, spH));
                Nature_AddFlowerCenterDome(&mesh, &cursor, tipApex, headNormal, headRight, headForward, r * 0.16f, r * 0.18f, phase, effCenterColor);
                break;
            }
            case 3:
            default:
            {
                PetalParams pOrchid = {
                    .shape = PETAL_SHAPE_LANCEOLATE,
                    .wMax = r * 0.35f,
                    .length = r * 1.16f,
                    .tPeak = 0.30f,
                    .curveAmount = r * 0.38f,
                    .twistAmount = 0.06f,
                };
                PetalEvalCache ecOrchid;
                Nature_PreparePetalEvalCache(&pOrchid, &ecOrchid);
                for (int k = 0; k < 6; k++)
                {
                    float angle = (float)k * 1.04719755f;
                    float sinK, cosK;
                    Nature_FastSinCos(angle, &sinK, &cosK);
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosK), Vector3Scale(headForward, sinK));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetalCached(&mesh, &cursor, head, headNormal, dir, side, &ecOrchid,
                                                    phase, cBase, cMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.24f, r * 0.14f, phase, effCenterColor);
                break;
            }
            }
        }
    }

    if (!field.textured)
        (void)Nature_IndexBladeMesh(&mesh);

    field.textured = (petalTexturePath != NULL);
    field.model = Nature_ModelFromMesh(mesh, Nature_GetShader(true));
    field.farModel = Nature_BuildFlowerFarModel(placements, count, stemColor, field.textured,
                                                atlasColumns, atlasRows);
    field.farReady = field.farModel.meshCount > 0;
    if (quality >= GFX_MED)
        field.shadowModel = Nature_BuildFlowerShadowModel(placements, count);
    field.shadowReady = field.shadowModel.meshCount > 0;
    field.alphaCutoff = alphaCutoff > 0.0f ? fminf(alphaCutoff, 0.9f) : 0.35f;
    field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
    field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f;
    field.drawDistance = 72.0f;
    field.lodDistance = 24.0f;
    field.shadowDistance = 16.0f;
    if (field.textured)
    {
        Texture2D petalTexture = MapLoadMippedTexture(petalTexturePath, TEXTURE_WRAP_CLAMP);
        field.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
        field.farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
    }
    field.ready = true;
    return field;
}

void MapProp_SetFlowerFieldDrawDistance(MapFlowerField *field, float drawDistance)
{
    if (!field)
        return;
    field->drawDistance = drawDistance;
}

void MapProp_SetFlowerFieldLod(MapFlowerField *field, float lodDistance, float shadowDistance)
{
    if (!field)
        return;
    field->lodDistance = lodDistance;
    field->shadowDistance = shadowDistance;
}

#define NATURE_FLOWER_BATCH_DRAW 1u
#define NATURE_FLOWER_BATCH_FAR 2u
#define NATURE_FLOWER_BATCH_SHADOW 4u
#define NATURE_FLOWER_BATCH_VERY_FAR 8u

static void Nature_SortFlowerFieldsFrontToBack(int *order, float *depth, int count)
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

void MapProp_DrawFlowerFieldBatch(MapFlowerField *fields, int fieldCount, Vector3 worldOffset,
                                  float time, Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0)
        return;

    static unsigned char *s_batchFlags = NULL;
    static int *s_batchOrder = NULL;
    static float *s_batchDepth = NULL;
    static int s_batchCapacity = 0;

    unsigned char localFlags[128];
    int localOrder[128];
    float localDepth[128];
    unsigned char *flags = localFlags;
    int *drawOrder = localOrder;
    float *drawDepth = localDepth;

    if (fieldCount > 128)
    {
        if (fieldCount > s_batchCapacity)
        {
            unsigned char *grownFlags = (unsigned char *)RL_REALLOC(s_batchFlags, (size_t)fieldCount);
            if (grownFlags)
                s_batchFlags = grownFlags;
            int *grownOrder = (int *)RL_REALLOC(s_batchOrder, (size_t)fieldCount * sizeof(int));
            if (grownOrder)
                s_batchOrder = grownOrder;
            float *grownDepth = (float *)RL_REALLOC(s_batchDepth, (size_t)fieldCount * sizeof(float));
            if (grownDepth)
                s_batchDepth = grownDepth;
            if (!grownFlags || !grownOrder || !grownDepth)
                return;
            s_batchCapacity = fieldCount;
        }
        flags = s_batchFlags;
        drawOrder = s_batchOrder;
        drawDepth = s_batchDepth;
    }
    int drawCount = 0;

    Nature_RefreshFrustumCache();

    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH  ? 1.0f
                       : quality == GFX_MED ? 0.84f
                       : quality == GFX_LOW ? 0.68f
                                            : 0.55f;
    float lodScale = quality >= GFX_HIGH  ? 1.0f
                     : quality == GFX_MED ? 0.82f
                     : quality == GFX_LOW ? 0.58f
                                          : 0.45f;
    float focalDx = camera.position.x - camera.target.x;
    float focalDz = camera.position.z - camera.target.z;
    float focalDistH = sqrtf(focalDx * focalDx + focalDz * focalDz);
    float fovyRad = fmaxf(camera.fovy, 15.0f) * DEG2RAD;
    float zoomFactor = 0.41421356f / tanf(fovyRad * 0.5f);
    if (zoomFactor < 0.8f)
        zoomFactor = 0.8f;
    if (zoomFactor > 2.5f)
        zoomFactor = 2.5f;
    float hysteresis = quality >= GFX_HIGH ? 1.25f : 2.0f;
    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_LOW && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = !realShadowActive && quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;

    bool anyShadow = false;
    bool anyParametric = false;
    bool anyLegacy = false;
    for (int i = 0; i < fieldCount; i++)
    {
        MapFlowerField *field = &fields[i];
        flags[i] = 0;
        if (!field->ready)
            continue;
        s_natureRenderStats.flowerFieldsTested++;
        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);

        float dx = camera.position.x - center.x;
        float dy = camera.position.y - center.y;
        float dz = camera.position.z - center.z;
        float distSqXZ = dx * dx + dz * dz;
        float maxDrawDist = field->drawDistance * rangeScale + field->boundsRadius;
        if (field->drawDistance > 0.0f && distSqXZ > maxDrawDist * maxDrawDist)
        {
            s_natureRenderStats.flowerFieldsDistanceCulled++;
            continue;
        }
        if (!Nature_IsChunkVisibleFast(center, field->boundsRadius))
        {
            s_natureRenderStats.flowerFieldsFrustumCulled++;
            continue;
        }

        float centerDistance = sqrtf(distSqXZ);
        float visibleDistance = centerDistance - field->boundsRadius;
        float threshold = 0.0f;
        if (field->farReady && field->lodDistance > 0.0f)
        {
            float hSeed = field->boundsCenter.x * 0.1031f + field->boundsCenter.z * 0.3183f;
            float spatialHash = hSeed - floorf(hSeed);
            threshold = focalDistH + field->lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 3.0f;
            if (field->farLod)
            {
                if (visibleDistance < threshold - hysteresis)
                    field->farLod = false;
            }
            else if (visibleDistance > threshold + hysteresis)
            {
                field->farLod = true;
            }
        }
        else
        {
            field->farLod = false;
        }

        unsigned char f = NATURE_FLOWER_BATCH_DRAW;
        if (field->farReady && field->farLod)
        {
            f |= NATURE_FLOWER_BATCH_FAR;
            if (field->parametric && threshold > 0.0f && visibleDistance > threshold * 2.35f)
                f |= NATURE_FLOWER_BATCH_VERY_FAR;
        }

        if (field->parametric)
            anyParametric = true;
        else
            anyLegacy = true;

        float maxContactDist = realShadowActive
                                   ? fminf(field->shadowDistance, 16.0f)
                                   : (field->shadowDistance * (quality >= GFX_HIGH ? 1.0f : 0.78f));
        bool shadowInRange = field->shadowDistance <= 0.0f || visibleDistance <= maxContactDist;
        if (field->shadowReady && useProjectedShadows)
        {
            if (shadowInRange)
            {
                f |= NATURE_FLOWER_BATCH_SHADOW;
                anyShadow = true;
            }
            else
            {
                s_natureRenderStats.flowerShadowDistanceCulled++;
            }
        }
        flags[i] = f;

        float distSq3D = distSqXZ + dy * dy;
        int slot = drawCount++;
        drawOrder[slot] = i;
        drawDepth[slot] = distSq3D;
    }
    if (drawCount <= 0)
        return;

    if (drawCount > 1)
        Nature_SortFlowerFieldsFrontToBack(drawOrder, drawDepth, drawCount);

    Matrix worldTransform = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);

    if (anyShadow)
    {
        Shader shadowShader = FlowerShadow_GetShader();
        BeginShaderMode(shadowShader);
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        EndShaderMode();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        for (int d = 0; d < drawCount; d++)
        {
            int i = drawOrder[d];
            if (!(flags[i] & NATURE_FLOWER_BATCH_SHADOW))
                continue;
            const Model *sm = &fields[i].shadowModel;
            for (int sub = 0; sub < sm->meshCount; sub++)
                DrawMesh(sm->meshes[sub], sm->materials[sm->meshMaterial[sub]], worldTransform);
            s_natureRenderStats.flowerShadowDraws++;
        }
        EndBlendMode();
        rlEnableDepthMask();
    }

    if (anyParametric)
    {
        Shader shader = FlowerParametric_GetShader(false);
        Nature_BeginWindReceiverShader(shader);
        Nature_UpdateShader(shader, time, windDirection, windStrength,
                            false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
        rlDisableBackfaceCulling();

        Matrix model = MatrixMultiply(worldTransform, rlGetMatrixTransform());
        Matrix mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
        SetShaderValueMatrix(shader, shader.locs[SHADER_LOC_MATRIX_MVP], mvp);
        if (s_flowerWorldOffsetLoc[0] >= 0)
            SetShaderValue(shader, s_flowerWorldOffsetLoc[0], &worldOffset, SHADER_UNIFORM_VEC3);
        int spOverride = MapProp_GetFlowerSpeciesOverride();
        if (s_flowerSpeciesOverrideLoc[0] >= 0)
            SetShaderValue(shader, s_flowerSpeciesOverrideLoc[0], &spOverride, SHADER_UNIFORM_INT);
        int unit0 = 0;
        if (s_flowerInstanceTexLoc[0] >= 0)
            SetShaderValue(shader, s_flowerInstanceTexLoc[0], &unit0, SHADER_UNIFORM_INT);

        int activeLod = -1;
        int activeStride = -1;
        unsigned int activeTexId = 0;
        for (int d = 0; d < drawCount; d++)
        {
            int i = drawOrder[d];
            MapFlowerField *field = &fields[i];
            if (!field->parametric || field->instanceCount <= 0)
                continue;

            int lod = 0;
            int stride = (quality >= GFX_MED) ? 1 : 2;
            if (flags[i] & NATURE_FLOWER_BATCH_VERY_FAR)
            {
                lod = 2;
                stride = 2;
            }
            else if (flags[i] & NATURE_FLOWER_BATCH_FAR)
            {
                lod = (quality >= GFX_MED) ? 1 : 2;
                stride = (quality >= GFX_MED) ? 1 : 2;
            }
            int instances = (stride > 1) ? ((field->instanceCount + stride - 1) / stride) : field->instanceCount;
            if (instances <= 0)
                continue;

            if (activeStride != stride && s_flowerLodStrideLoc[0] >= 0)
            {
                SetShaderValue(shader, s_flowerLodStrideLoc[0], &stride, SHADER_UNIFORM_INT);
                activeStride = stride;
            }

            if (activeTexId != field->instanceTexture.id)
            {
                rlActiveTextureSlot(0);
                rlEnableTexture(field->instanceTexture.id);
                activeTexId = field->instanceTexture.id;
            }

            FlowerParametricTemplate *tmpl = &s_flowerTemplates[lod];
            if (activeLod != lod)
            {
                rlEnableVertexArray(tmpl->vao);
                activeLod = lod;
            }
            rlDrawVertexArrayElementsInstanced(0, tmpl->indexCount, NULL, instances);

            s_natureRenderStats.flowerDraws++;
            if (lod > 0)
                s_natureRenderStats.flowerFarDraws++;
            else
                s_natureRenderStats.flowerNearDraws++;
        }
        if (activeLod >= 0)
            rlDisableVertexArray();
        Nature_EndWindReceiverShader();
        rlEnableBackfaceCulling();
    }

    if (anyLegacy)
    {
        bool groupOpen = false;
        bool groupTextured = false;
        float groupAlpha = 0.0f;
        for (int d = 0; d < drawCount; d++)
        {
            int i = drawOrder[d];
            MapFlowerField *field = &fields[i];
            if (field->parametric)
                continue;
            if (!groupOpen || field->textured != groupTextured || field->alphaCutoff != groupAlpha)
            {
                if (groupOpen)
                    Nature_EndWindReceiverShader();
                Shader shader = Nature_GetShader(true);
                Nature_BeginWindReceiverShader(shader);
                Nature_UpdateShader(shader, time, windDirection, windStrength,
                                    field->textured, field->alphaCutoff,
                                    NATURE_WIND_RESPONSE_FLOWER);
                if (!groupOpen)
                    rlDisableBackfaceCulling();
                groupOpen = true;
                groupTextured = field->textured;
                groupAlpha = field->alphaCutoff;
            }
            bool useFarModel = (flags[i] & NATURE_FLOWER_BATCH_FAR) != 0;
            const Model *fm = useFarModel ? &field->farModel : &field->model;
            for (int sub = 0; sub < fm->meshCount; sub++)
                DrawMesh(fm->meshes[sub], fm->materials[fm->meshMaterial[sub]], worldTransform);
            s_natureRenderStats.flowerDraws++;
            if (useFarModel)
                s_natureRenderStats.flowerFarDraws++;
            else
                s_natureRenderStats.flowerNearDraws++;
        }
        if (groupOpen)
        {
            Nature_EndWindReceiverShader();
            rlEnableBackfaceCulling();
        }
    }
}

void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    MapProp_DrawFlowerFieldBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

// [TỐI ƯU BATCHED SHADOW CASTER]:
// Culling theo Frustum bóng đổ + giảm LOD/mật độ trên mobile để tối ưu tile binning Shadow Map
void MapProp_DrawFlowerFieldShadowCasterBatch(MapFlowerField *fields, int fieldCount,
                                              Vector3 worldOffset, float time,
                                              Vector2 windDirection, float windStrength)
{
    GfxQuality quality = GfxQuality_Get();
    if (!fields || fieldCount <= 0 || quality < GFX_MED ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(true))
        return;

    Vector3 sunDir = Nature_FastNormalize(Environment_GetSunDirection());
    if (fabsf(sunDir.y) > 0.98f)
        return;

    Nature_RefreshFrustumCache();

    bool filterActive = Nature_ShadowCasterFilterActive();
    Vector3 shadowFocus = EnvShadow_GetFocus();
    float extent = fmaxf(EnvShadow_GetHalfExtent(), 8.0f);
    float groundRadius = extent / fmaxf(fabsf(sunDir.y), 0.32f);
    float qualityDistScale = (quality >= GFX_HIGH) ? 1.0f : 0.68f;
    Vector3 shadowSweepOffset = {sunDir.x * 1.8f, 0.0f, sunDir.z * 1.8f};

    bool paramBound = false;
    int shadowLod = (quality >= GFX_HIGH) ? 1 : 2;
    int shadowStride = (quality >= GFX_HIGH) ? 2 : 3;
    unsigned int activeTexId = 0;

    for (int i = 0; i < fieldCount; i++)
    {
        MapFlowerField *field = &fields[i];
        if (!field->ready || !field->parametric || field->instanceCount <= 0)
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

        if (!paramBound)
        {
            Shader shd = FlowerParametric_GetShader(true);
            Nature_BeginWindReceiverShader(shd);
            Nature_UpdateShadowShader(shd, time, windDirection, windStrength,
                                      false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
            Matrix model = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);
            Matrix mvp = MatrixMultiply(model, EnvShadow_GetLightVP());
            SetShaderValueMatrix(shd, shd.locs[SHADER_LOC_MATRIX_MVP], mvp);
            if (s_flowerWorldOffsetLoc[1] >= 0)
                SetShaderValue(shd, s_flowerWorldOffsetLoc[1], &worldOffset, SHADER_UNIFORM_VEC3);
            int spOverride = MapProp_GetFlowerSpeciesOverride();
            if (s_flowerSpeciesOverrideLoc[1] >= 0)
                SetShaderValue(shd, s_flowerSpeciesOverrideLoc[1], &spOverride, SHADER_UNIFORM_INT);
            if (s_flowerLodStrideLoc[1] >= 0)
                SetShaderValue(shd, s_flowerLodStrideLoc[1], &shadowStride, SHADER_UNIFORM_INT);
            int unit0 = 0;
            if (s_flowerInstanceTexLoc[1] >= 0)
                SetShaderValue(shd, s_flowerInstanceTexLoc[1], &unit0, SHADER_UNIFORM_INT);
            rlDisableBackfaceCulling();
            rlEnableVertexArray(s_flowerTemplates[shadowLod].vao);
            paramBound = true;
        }

        int instances = (field->instanceCount + shadowStride - 1) / shadowStride;
        if (activeTexId != field->instanceTexture.id)
        {
            rlActiveTextureSlot(0);
            rlEnableTexture(field->instanceTexture.id);
            activeTexId = field->instanceTexture.id;
        }
        rlDrawVertexArrayElementsInstanced(0, s_flowerTemplates[shadowLod].indexCount, NULL, instances);
    }
    if (paramBound)
    {
        rlDisableVertexArray();
        Nature_EndWindReceiverShader();
        rlEnableBackfaceCulling();
    }

    Shader shader = NatureShadow_GetShader();
    bool shaderBound = false;
    bool lastTextured = false;
    float lastAlpha = -1.0f;
    Matrix worldTransform = MatrixTranslate(worldOffset.x, worldOffset.y, worldOffset.z);

    for (int i = 0; i < fieldCount; i++)
    {
        MapFlowerField *field = &fields[i];
        if (!field->ready || field->parametric)
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

        if (!shaderBound || field->textured != lastTextured || field->alphaCutoff != lastAlpha)
        {
            if (shaderBound)
                Nature_EndWindReceiverShader();
            Nature_BeginWindReceiverShader(shader);
            Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                                      field->textured, field->alphaCutoff,
                                      NATURE_WIND_RESPONSE_FLOWER);
            if (!shaderBound)
                rlDisableBackfaceCulling();
            shaderBound = true;
            lastTextured = field->textured;
            lastAlpha = field->alphaCutoff;
        }

        Model *castModel = (field->farReady && !field->textured)
                               ? &field->farModel
                               : &field->model;
        for (int sub = 0; sub < castModel->meshCount; sub++)
        {
            Material mat = castModel->materials[castModel->meshMaterial[sub]];
            mat.shader = shader;
            DrawMesh(castModel->meshes[sub], mat, worldTransform);
        }
    }

    if (shaderBound)
    {
        Nature_EndWindReceiverShader();
        rlEnableBackfaceCulling();
    }
}

void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength)
{
    MapProp_DrawFlowerFieldShadowCasterBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

void MapProp_UnloadFlowerField(MapFlowerField *field)
{
    if (!field || !field->ready)
        return;
    if (field->instanceTexture.id != 0)
    {
        UnloadTexture(field->instanceTexture);
        field->instanceTexture = (Texture2D){0};
    }
    if (!field->parametric)
    {
        if (field->shadowReady)
            UnloadModel(field->shadowModel);
        if (field->farReady)
            UnloadModel(field->farModel);
        UnloadModel(field->model);
    }
    field->parametric = false;
    field->instanceCount = 0;
    field->farReady = false;
    field->farLod = false;
    field->shadowReady = false;
    field->boundsRadius = 0.0f;
    field->ready = false;
}
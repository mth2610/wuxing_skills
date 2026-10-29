// Flower Field System (Optimized 3D Diamond-Cupped Petals, Analytical Vein/SSS UVs,
// 3D Far-LOD Blossoms, and Batched Shadow Casters).
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

static Model Nature_BuildFlowerShadowModel(const MapFlowerPlacement *placements, int count)
{
    Mesh mesh = Nature_AllocMesh(count * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        float width = fmaxf(flower->bloomRadius * 1.18f, 0.068f);
        Vector3 root = flower->position;
        root.y += 0.002f;
        Vector3 encoded = {flower->height, width, flower->phase};
        Color rootShade = {225, 225, 225, 255};
        Color tipShade = {150, 150, 150, 255};
        const Vector2 uv[6] = {
            {0.0f, 0.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
        };
        for (int vertex = 0; vertex < 6; vertex++)
        {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
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
    float hash = sinf(flower->position.x * 12.9898f + flower->position.z * 78.233f + flower->phase * 37.719f) * 43758.5453f;
    return hash - floorf(hash) < 0.62f;
}

static Color Nature_GetSpeciesCenterColor(int variant)
{
    switch (variant & 7)
    {
    case 0:
        return (Color){248, 185, 22, 255}; // 0: Bạch Cúc (Daisy): nhụy hoàng kim
    case 1:
        return (Color){195, 115, 18, 255}; // 1: Hoàng Anh (Buttercup): nhụy cam hổ phách
    case 2:
        return (Color){44, 18, 22, 255}; // 2: Chu Sa Hồng / Bỉ Ngạn (Crimson Poppy/Lily): tâm huyết mặc
    case 3:
        return (Color){45, 40, 78, 255}; // 3: Lam Điệp (Cornflower): tâm lam sẫm
    case 4:
        return (Color){238, 165, 24, 255}; // 4: Đào Hoa (Peach-rose): nhụy mật ong
    case 5:
        return (Color){245, 200, 32, 255}; // 5: Tử Mẫu (Cosmos): nhụy vàng tươi
    case 6:
        return (Color){92, 58, 128, 255}; // 6: Huân Y Thảo / Tử Linh Đang (Lavender/Bell): nhụy tím sẫm
    case 7:
        return (Color){168, 185, 48, 255}; // 7: Ngọc Lan / Tuyết Liên (Primrose/Lotus): nhụy lục kim
    default:
        return (Color){248, 185, 22, 255};
    }
}

// [TỐI ƯU 3D FAR-LOD & SHADOW CASTER]:
// Thay vì dùng 1 tấm quad phẳng 2D ở cự ly xa, mỗi bông hoa ở xa được dựng bằng đài hoa 3D
// gồm 3 cánh tam giác khum hướng thiên (9 đỉnh/bông). Giữ trọn khối 3D từ mọi góc nhìn
// và dùng làm mesh đổ bóng siêu nhẹ cho lượt Shadow Map Pass!
static Model Nature_BuildFlowerFarModel(const MapFlowerPlacement *placements, int count,
                                        bool texturedBloom,
                                        int atlasColumns, int atlasRows)
{
    int selected = 0;
    for (int i = 0; i < count; i++)
    {
        if (Nature_KeepFarFlower(&placements[i]))
            selected++;
    }
    bool forceFirst = selected == 0;
    if (forceFirst)
        selected = 1;

    int vertsPerFarFlower = texturedBloom ? 6 : 9;
    Mesh mesh = Nature_AllocMesh(selected * vertsPerFarFlower);
    int cursor = 0;
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
        Vector3 head = {base.x + cosf(leanAngle) * lean, base.y + h * 0.94f,
                        base.z + sinf(leanAngle) * lean};
        float stemAngle = flower->rotationDeg * DEG2RAD;
        float tiltAmount = 0.22f + 0.08f * sinf(phase * 6.28f);
        Vector3 headNormal = Vector3Normalize((Vector3){cosf(leanAngle) * tiltAmount, 0.96f, sinf(leanAngle) * tiltAmount});
        Vector3 refRight = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
        Vector3 right = Vector3Normalize(Vector3Subtract(refRight, Vector3Scale(headNormal, Vector3DotProduct(refRight, headNormal))));
        Vector3 forward = Vector3CrossProduct(headNormal, right);
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float radius = flower->bloomRadius * petalScale * 1.18f;

        if (texturedBloom)
        {
            int variantCount = atlasColumns * atlasRows;
            int cell = variant % variantCount;
            int column = cell % atlasColumns;
            int row = cell / atlasColumns;
            float insetU = 0.008f / atlasColumns;
            float insetV = 0.008f / atlasRows;
            Vector4 uvRect = {
                (float)column / atlasColumns + insetU,
                (float)row / atlasRows + insetV,
                (float)(column + 1) / atlasColumns - insetU,
                (float)(row + 1) / atlasRows - insetV,
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
            // Đài hoa 3D 3 cánh khum hướng nắng (9 đỉnh/bông, Zero-Discard)
            Color cCore = Nature_LerpColor(centerCol, petalCol, 0.45f);
            Color cTip = Nature_ScaleColor(petalCol, 1.06f);
            float cupLift = radius * 0.28f;
            for (int p = 0; p < 3; p++)
            {
                float aMid = stemAngle + (float)p * 2.0943951f;
                float aL = aMid - 0.68f;
                float aR = aMid + 0.68f;
                Vector3 dL = Vector3Add(Vector3Scale(right, cosf(aL) * radius), Vector3Scale(forward, sinf(aL) * radius));
                Vector3 dR = Vector3Add(Vector3Scale(right, cosf(aR) * radius), Vector3Scale(forward, sinf(aR) * radius));
                Vector3 pL = Vector3Add(head, Vector3Add(dL, Vector3Scale(headNormal, cupLift)));
                Vector3 pR = Vector3Add(head, Vector3Add(dR, Vector3Scale(headNormal, cupLift)));
                Vector3 nPetal = Vector3Normalize(Vector3Add(headNormal, Vector3Scale(Vector3Add(dL, dR), 0.35f)));

                int baseIdx = cursor;
                Nature_SetVertex(&mesh, cursor++, head, nPetal, phase, 0.90f, cCore);
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

// [TỐI ƯU ĐỈNH HOA 3D]:
// Mỗi cánh hoa 3D dùng lưới Kim Cương Khum 2 tầng (12 đỉnh = 4 tam giác) thay vì 24 đỉnh,
// giảm 50% số đỉnh mà vẫn giữ nguyên độ cong vòm 3D cả chiều dọc lẫn chiều ngang!
static inline int Nature_FlowerBloomVertexCount(int variant, bool textured)
{
    if (textured)
        return 30; // 3 textured bloom cards (18) + 3D pollen disc dome (12) = 30
    int morph = variant % 4;
    switch (morph)
    {
    case 0:
        return 108; // 2-layer Rose/Poppy/Peach: 5 outer + 3 inner (8 * 12 = 96) + dome (12) = 108
    case 1:
        return 96; // 7 oval daisy/lotus petals (7 * 12 = 84) + phyllotaxis disc (12) = 96
    case 2:
        return 84; // Lavender/Bell spike: 3 whorls x 2 florets (6 * 12 = 72) + tip bud dome (12) = 84
    case 3:
        return 84; // 6 reflexed spider-lily/lanceolate petals (6 * 12 = 72) + center dome (12) = 84
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
        float s = sinf(PI * powf(t, 0.55f));
        if (s < 0.0f)
            s = 0.0f;
        float n = p->exponent > 0.0f ? p->exponent : 0.60f;
        float baseTaper = fminf(1.0f, t / 0.15f);
        return p->wMax * powf(s, n) * baseTaper;
    }
    case PETAL_SHAPE_LANCEOLATE:
    {
        float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.30f;
        if (t < tPeak)
            return p->wMax * powf(t / tPeak, 0.50f);
        else
            return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.80f);
    }
    case PETAL_SHAPE_OBCORDATE:
    {
        float s = sinf(PI * t);
        if (s < 0.0f)
            s = 0.0f;
        float wBase = p->wMax * powf(s, 1.20f);
        float notch = 0.0f;
        if (t > 0.80f)
        {
            float dt = (t - 1.0f) / 0.08f;
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
        return p->wMax * sinf(PI * t);
    }
}

static inline float Nature_EvalPetalElevation(float t, const PetalParams *p)
{
    float s = sinf(t * 1.5707963f);
    float conical = p->length * 0.28f * (t * 0.85f + 0.15f * t * t);
    return conical + p->curveAmount * (s * 1.25f - 0.25f * t * t);
}

// Dựng cánh hoa 3D 2 đốt khum lòng chảo (12 đỉnh = 4 tam giác) kèm tọa độ (u, v) chuẩn cho SSS & Gân hoa
static void Nature_AddParametricPetal(Mesh *mesh, int *cursor,
                                      Vector3 head, Vector3 headNormal, Vector3 dir, Vector3 side,
                                      const PetalParams *p, float phase,
                                      Color cBase, Color cMid, Color cTip)
{
    float len = p->length;
    float t0 = 0.05f;
    float t1 = 0.48f;
    float t2 = 0.84f;

    float w0 = Nature_EvalPetalWidth(t0, p);
    float dy0 = Nature_EvalPetalElevation(t0, p);

    float w1 = Nature_EvalPetalWidth(t1, p);
    float dy1 = Nature_EvalPetalElevation(t1, p);

    float w2 = Nature_EvalPetalWidth(t2, p);
    float dy2 = Nature_EvalPetalElevation(t2, p);

    float dyApex = Nature_EvalPetalElevation(1.0f, p);

    Vector3 side1 = side;
    Vector3 side2 = side;
    if (fabsf(p->twistAmount) > 0.001f)
    {
        float phi1 = p->twistAmount * t1;
        float phi2 = p->twistAmount * t2;
        side1 = Vector3Add(Vector3Scale(side, cosf(phi1)), Vector3Scale(headNormal, sinf(phi1) * len * 0.12f));
        side2 = Vector3Add(Vector3Scale(side, cosf(phi2)), Vector3Scale(headNormal, sinf(phi2) * len * 0.16f));
    }

    Vector3 p0 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t0), Vector3Scale(headNormal, dy0 + 0.003f)));
    Vector3 p1 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t1), Vector3Scale(headNormal, dy1)));
    Vector3 p2 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t2), Vector3Scale(headNormal, dy2)));
    Vector3 pApex = Vector3Add(head, Vector3Add(Vector3Scale(dir, len), Vector3Scale(headNormal, dyApex)));

    // Độ khum ngang cánh hoa 3D (Transverse 3D cupping)
    float cup0 = w0 * 0.26f;
    float cup1 = w1 * 0.38f;
    float cup2 = w2 * 0.26f;

    Vector3 b0 = Vector3Add(Vector3Subtract(p0, Vector3Scale(side, w0)), Vector3Scale(headNormal, cup0));
    Vector3 b1 = Vector3Add(Vector3Add(p0, Vector3Scale(side, w0)), Vector3Scale(headNormal, cup0));

    Vector3 m0 = Vector3Add(Vector3Subtract(p1, Vector3Scale(side1, w1)), Vector3Scale(headNormal, cup1));
    Vector3 m1 = Vector3Add(Vector3Add(p1, Vector3Scale(side1, w1)), Vector3Scale(headNormal, cup1));

    Vector3 s0 = Vector3Add(Vector3Subtract(p2, Vector3Scale(side2, w2)), Vector3Scale(headNormal, cup2));
    Vector3 s1 = Vector3Add(Vector3Add(p2, Vector3Scale(side2, w2)), Vector3Scale(headNormal, cup2));

    Vector3 nL0 = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.22f), Vector3Scale(side, 0.40f)), Vector3Scale(headNormal, 0.86f)));
    Vector3 nR0 = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.22f), Vector3Scale(side, 0.40f)), Vector3Scale(headNormal, 0.86f)));
    Vector3 nL1 = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.45f), Vector3Scale(side1, 0.44f)), Vector3Scale(headNormal, 0.76f)));
    Vector3 nR1 = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.45f), Vector3Scale(side1, 0.44f)), Vector3Scale(headNormal, 0.76f)));
    Vector3 nTip = Vector3Normalize(Vector3Add(Vector3Scale(dir, 0.72f), Vector3Scale(headNormal, 0.64f)));

    int baseIdx = *cursor;

    // Đốt 0: Gốc cánh -> Bụng cánh rộng nhất (6 đỉnh)
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.84f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, b1, nR0, phase, 0.84f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.94f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.84f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.94f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.94f, cMid);

    // Đốt 1: Bụng cánh -> Vai cánh & Đỉnh cánh nhọn/bầu (6 đỉnh)
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.94f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.94f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nTip, phase, 0.98f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.94f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nTip, phase, 0.98f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTip, phase, 1.00f, cTip);

    // Ghi tọa độ sinh học (u in [-1..1], v in [0..1]) vào texcoords2 để Shader tính Gân cánh hoa & Xuyên sáng SSS
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
    Vector3 apex = Vector3Add(head, Vector3Scale(headNormal, cHeight * 1.45f));
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    Color rimColor = Nature_ScaleColor(centerColor, 0.48f);
    for (int s = 0; s < 4; s++)
    {
        float a0 = (float)s * 1.5707963f;
        float a1 = (float)(s + 1) * 1.5707963f;
        Vector3 rad0 = Vector3Add(Vector3Scale(headRight, cosf(a0) * cR),
                                  Vector3Scale(headForward, sinf(a0) * cR));
        Vector3 rad1 = Vector3Add(Vector3Scale(headRight, cosf(a1) * cR),
                                  Vector3Scale(headForward, sinf(a1) * cR));
        Vector3 r0 = Vector3Add(head, Vector3Add(rad0, Vector3Scale(headNormal, 0.004f)));
        Vector3 r1 = Vector3Add(head, Vector3Add(rad1, Vector3Scale(headNormal, 0.004f)));

        float midA = (a0 + a1) * 0.5f;
        Vector3 midRad = Vector3Add(Vector3Scale(headRight, cosf(midA) * 0.55f),
                                    Vector3Scale(headForward, sinf(midA) * 0.55f));
        Vector3 norm = Vector3Normalize(Vector3Add(midRad, Vector3Scale(headNormal, 0.84f)));

        int baseIdx = *cursor;
        Nature_SetVertex(mesh, (*cursor)++, apex, headNormal, phase, 1.0f, apexColor);
        Nature_SetVertex(mesh, (*cursor)++, r0, norm, phase, 0.94f, rimColor);
        Nature_SetVertex(mesh, (*cursor)++, r1, norm, phase, 0.94f, rimColor);
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

    // Phần thân + lá gốc + lá thân + đài hoa được tối ưu gọn còn 30 đỉnh/bông (thay vì 84 đỉnh)
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

    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
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

        // 1. Basal leaf blades: 2 lá kiếm vươn đối xứng ở gốc (6 đỉnh = 2 tam giác nhọn)
        // Đặc biệt với biến thể 2 (Chu Sa Hồng / Bỉ Ngạn Hoa): thu nhỏ lá gốc sát đất
        float basalScale = (variant == 2) ? 0.25f : 1.0f;
        Color basalRoot = {38, 64, 26, 255};
        Color basalTip = {62, 102, 42, 255};
        for (int b = 0; b < 2; b++)
        {
            float bAngle = flower->rotationDeg * DEG2RAD + (float)b * PI + 0.42f;
            Vector3 bDir = {cosf(bAngle), 0.0f, sinf(bAngle)};
            Vector3 bSide = {-bDir.z * 0.022f * basalScale, 0.0f, bDir.x * 0.022f * basalScale};
            float bLen = fmaxf(0.085f, flower->bloomRadius * 1.15f) * basalScale;
            Vector3 r0 = {base.x - bSide.x, base.y + 0.002f, base.z - bSide.z};
            Vector3 r1 = {base.x + bSide.x, base.y + 0.002f, base.z + bSide.z};
            Vector3 rTip = {base.x + bDir.x * bLen, base.y + 0.032f * basalScale, base.z + bDir.z * bLen};
            int bIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, r0, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.0f, basalRoot);
            Nature_SetVertex(&mesh, cursor++, r1, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.0f, basalRoot);
            Nature_SetVertex(&mesh, cursor++, rTip, (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.28f, basalTip);
            Nature_SetUv2(&mesh, bIdx + 0, -1.0f, 0.0f);
            Nature_SetUv2(&mesh, bIdx + 1, 1.0f, 0.0f);
            Nature_SetUv2(&mesh, bIdx + 2, 0.0f, 0.5f);
        }

        // 2. Cuống hoa cong mềm mại 2 đoạn (12 đỉnh)
        float stemWidth = fmaxf(0.0048f, flower->bloomRadius * 0.075f);
        float stemLeanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 3.14f;
        float stemLean = h * 0.22f;
        float stemDirX = cosf(stemLeanAngle);
        float stemDirZ = sinf(stemLeanAngle);
        Vector3 stemP0 = base;
        Vector3 midStem = (Vector3){base.x + stemDirX * (stemLean * 0.38f), base.y + h * 0.52f, base.z + stemDirZ * (stemLean * 0.38f)};
        Vector3 head = (Vector3){base.x + stemDirX * stemLean, base.y + h * 0.94f, base.z + stemDirZ * stemLean};

        float stemAngle = flower->rotationDeg * DEG2RAD;
        Vector3 sideA = {cosf(stemAngle) * stemWidth, 0.0f, sinf(stemAngle) * stemWidth};
        Vector3 normA = {-sinf(stemAngle), 0.12f, cosf(stemAngle)};

        Vector3 s0_0 = Vector3Subtract(stemP0, sideA);
        Vector3 s0_1 = Vector3Add(stemP0, sideA);
        Vector3 s0_2 = Vector3Add(midStem, Vector3Scale(sideA, 0.82f));
        Vector3 s0_3 = Vector3Subtract(midStem, Vector3Scale(sideA, 0.82f));
        Nature_AddQuad(&mesh, &cursor, s0_0, s0_1, s0_2, s0_3, normA, phase, 0.0f, 0.5f, stemColor, stemColor);

        Vector3 s1_0 = s0_3;
        Vector3 s1_1 = s0_2;
        Vector3 s1_2 = Vector3Add(head, Vector3Scale(sideA, 0.55f));
        Vector3 s1_3 = Vector3Subtract(head, Vector3Scale(sideA, 0.55f));
        Nature_AddQuad(&mesh, &cursor, s1_0, s1_1, s1_2, s1_3, normA, phase, 0.5f, 0.95f, stemColor, stemColor);

        // 3. 2 Lá non giữa thân + 2 Đài hoa đỡ dưới đầu bông (4 tam giác = 12 đỉnh)
        for (int leaf = 0; leaf < 2; leaf++)
        {
            float rootT = leaf == 0 ? 0.42f : 0.68f;
            float leafAngle = stemLeanAngle + (leaf == 0 ? 1.45f : -1.45f);
            Vector3 lDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 lSide = {-lDir.z * 0.014f, 0.0f, lDir.x * 0.014f};
            float lLen = fmaxf(0.052f, flower->bloomRadius * 0.80f);
            Vector3 lRoot = Vector3Lerp(stemP0, head, rootT);
            Vector3 lTip = {lRoot.x + lDir.x * lLen, lRoot.y + lLen * 0.36f, lRoot.z + lDir.z * lLen};
            int lIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, Vector3Subtract(lRoot, lSide), (Vector3){0, 1, 0}, phase, rootT, stemColor);
            Nature_SetVertex(&mesh, cursor++, Vector3Add(lRoot, lSide), (Vector3){0, 1, 0}, phase, rootT, stemColor);
            Nature_SetVertex(&mesh, cursor++, lTip, (Vector3){0, 1, 0}, phase, rootT + 0.25f, Nature_ScaleColor(stemColor, 1.18f));
            Nature_SetUv2(&mesh, lIdx + 0, -1.0f, 0.1f);
            Nature_SetUv2(&mesh, lIdx + 1, 1.0f, 0.1f);
            Nature_SetUv2(&mesh, lIdx + 2, 0.0f, 0.6f);
        }

        float calyxLen = r * 0.48f;
        Color calyxColor = Nature_ScaleColor(stemColor, 0.90f);
        for (int c = 0; c < 2; c++)
        {
            float cAngle = stemAngle + (float)c * PI + 0.785398f;
            Vector3 cDir = {cosf(cAngle), 0.0f, sinf(cAngle)};
            Vector3 cSide = {-cDir.z * calyxLen * 0.35f, 0.0f, cDir.x * calyxLen * 0.35f};
            Vector3 cBasePt = {head.x, head.y - stemWidth * 0.5f, head.z};
            Vector3 cTipPt = {head.x + cDir.x * calyxLen, head.y + stemWidth * 0.2f, head.z + cDir.z * calyxLen};
            int cIdx = cursor;
            Nature_SetVertex(&mesh, cursor++, Vector3Subtract(cBasePt, cSide), (Vector3){0, 1, 0}, phase, 0.90f, calyxColor);
            Nature_SetVertex(&mesh, cursor++, Vector3Add(cBasePt, cSide), (Vector3){0, 1, 0}, phase, 0.90f, calyxColor);
            Nature_SetVertex(&mesh, cursor++, cTipPt, (Vector3){0, 1, 0}, phase, 0.96f, Nature_ScaleColor(calyxColor, 1.2f));
            Nature_SetUv2(&mesh, cIdx + 0, -0.8f, 0.0f);
            Nature_SetUv2(&mesh, cIdx + 1, 0.8f, 0.0f);
            Nature_SetUv2(&mesh, cIdx + 2, 0.0f, 0.4f);
        }

        // 4. Lắp ráp Đài Hoa 3D theo loài (5. Assemble 3D Flower Head)
        float tiltAmount = 0.22f + 0.08f * sinf(phase * 6.28f);
        Vector3 headNormal = Vector3Normalize((Vector3){stemDirX * tiltAmount, 0.96f, stemDirZ * tiltAmount});
        Vector3 refRight = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
        Vector3 headRight = Vector3Normalize(Vector3Subtract(refRight, Vector3Scale(headNormal, Vector3DotProduct(refRight, headNormal))));
        Vector3 headForward = Vector3CrossProduct(headNormal, headRight);
        Color effCenterColor = Nature_GetSpeciesCenterColor(variant);
        if (centerColor.a > 0 && flower->bloomVariant < 0)
            effCenterColor = centerColor;

        if (field.textured)
        {
            int variantCount = atlasColumns * atlasRows;
            int cell = variant % variantCount;
            int column = cell % atlasColumns;
            int row = cell / atlasColumns;
            float insetU = 0.008f / (float)atlasColumns;
            float insetV = 0.008f / (float)atlasRows;
            Vector4 uvRect = {
                (float)column / (float)atlasColumns + insetU,
                (float)row / (float)atlasRows + insetV,
                (float)(column + 1) / (float)atlasColumns - insetU,
                (float)(row + 1) / (float)atlasRows - insetV,
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
            Vector3 r1 = {cosf(a1) * r, 0.0f, sinf(a1) * r};
            Vector3 f1 = {-sinf(a1) * r, 0.0f, cosf(a1) * r};
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
            int morph = variant % 4;
            Color cBase = Nature_ScaleColor(petalColor, 0.72f);
            Color cMid = petalColor;
            Color cTip = Nature_ScaleColor(petalColor, 1.15f);

            switch (morph)
            {
            case 0:
            {
                // Morph 0: Đài hoa 2 tầng khum 3D (Đào Hoa / Tuyết Liên / Mẫu Đơn)
                PetalParams pOuter = {
                    .shape = PETAL_SHAPE_OBCORDATE,
                    .wMax = r * 0.48f,
                    .length = r * 1.05f,
                    .exponent = 1.25f,
                    .notchDepth = r * 0.16f,
                    .curveAmount = r * 0.38f,
                    .twistAmount = 0.08f,
                };
                for (int k = 0; k < 5; k++)
                {
                    float angle = (float)k * 1.256637f;
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosf(angle)), Vector3Scale(headForward, sinf(angle)));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetal(&mesh, &cursor, head, headNormal, dir, side, &pOuter,
                                              phase, cBase, cMid, cTip);
                }
                PetalParams pInner = {
                    .shape = PETAL_SHAPE_OBCORDATE,
                    .wMax = r * 0.38f,
                    .length = r * 0.78f,
                    .exponent = 1.15f,
                    .notchDepth = r * 0.12f,
                    .curveAmount = r * 0.54f,
                    .twistAmount = -0.10f,
                };
                Color cInBase = Nature_ScaleColor(cBase, 0.90f);
                Color cInMid = Nature_ScaleColor(cMid, 0.95f);
                for (int k = 0; k < 3; k++)
                {
                    float angle = 0.628318f + (float)k * 2.094395f;
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosf(angle)), Vector3Scale(headForward, sinf(angle)));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetal(&mesh, &cursor, head, headNormal, dir, side, &pInner,
                                              phase, cInBase, cInMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.28f, r * 0.16f, phase, effCenterColor);
                break;
            }
            case 1:
            {
                // Morph 1: Kim Cúc / Bạch Cúc 7 cánh xòe rộng đón nắng
                PetalParams pDaisy = {
                    .shape = PETAL_SHAPE_OVAL,
                    .wMax = r * 0.36f,
                    .length = r * 1.18f,
                    .exponent = 0.52f,
                    .curveAmount = r * 0.22f,
                    .twistAmount = 0.03f,
                };
                for (int k = 0; k < 7; k++)
                {
                    float angle = (float)k * 0.8975979f;
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosf(angle)), Vector3Scale(headForward, sinf(angle)));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetal(&mesh, &cursor, head, headNormal, dir, side, &pDaisy,
                                              phase, cBase, cMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.35f, r * 0.15f, phase, effCenterColor);
                break;
            }
            case 2:
            {
                // Morph 2: Chuỗi hoa linh thảo / Tử Linh Đang 3 tầng xoắn ốc
                float spH = r * 2.1f;
                float phaseSpiral = 0.48f;
                for (int k = 0; k < 3; k++)
                {
                    float s = 0.24f + (float)k * 0.32f;
                    Vector3 tierHead = Vector3Add(head, Vector3Scale(headNormal, spH * s));
                    float bloomFactor = (s < 0.68f) ? 1.0f : fmaxf(0.45f, 1.0f - (s - 0.68f) / 0.35f);
                    PetalParams pFloret = {
                        .shape = PETAL_SHAPE_OVAL,
                        .wMax = r * 0.26f * bloomFactor,
                        .length = r * 0.66f * bloomFactor,
                        .exponent = 1.10f,
                        .curveAmount = r * 0.26f * bloomFactor,
                        .twistAmount = 0.04f,
                    };
                    for (int j = 0; j < 2; j++)
                    {
                        float angle = (float)k * phaseSpiral + (float)j * PI;
                        Vector3 dir = Vector3Add(Vector3Scale(headRight, cosf(angle)), Vector3Scale(headForward, sinf(angle)));
                        Vector3 side = Vector3CrossProduct(headNormal, dir);
                        Nature_AddParametricPetal(&mesh, &cursor, tierHead, headNormal, dir, side, &pFloret,
                                                  phase, cBase, cMid, cTip);
                    }
                }
                Vector3 tipApex = Vector3Add(head, Vector3Scale(headNormal, spH));
                Nature_AddFlowerCenterDome(&mesh, &cursor, tipApex, headNormal, headRight, headForward, r * 0.16f, r * 0.18f, phase, effCenterColor);
                break;
            }
            case 3:
            {
                // Morph 3: Cánh vuốt rồng uốn cong 6 cánh (U Lan / Bỉ Ngạn / Lam Điệp)
                PetalParams pBell = {
                    .shape = PETAL_SHAPE_LANCEOLATE,
                    .wMax = r * 0.34f,
                    .length = r * 1.14f,
                    .tPeak = 0.28f,
                    .curveAmount = r * 0.36f,
                    .twistAmount = 0.06f,
                };
                for (int k = 0; k < 6; k++)
                {
                    float angle = (float)k * 1.04719755f;
                    Vector3 dir = Vector3Add(Vector3Scale(headRight, cosf(angle)), Vector3Scale(headForward, sinf(angle)));
                    Vector3 side = Vector3CrossProduct(headNormal, dir);
                    Nature_AddParametricPetal(&mesh, &cursor, head, headNormal, dir, side, &pBell,
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
    field.farModel = Nature_BuildFlowerFarModel(placements, count, field.textured,
                                                atlasColumns, atlasRows);
    field.farReady = field.farModel.meshCount > 0;
    if (GfxQuality_Get() >= GFX_MED)
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

void MapProp_DrawFlowerFieldBatch(MapFlowerField *fields, int fieldCount, Vector3 worldOffset,
                                  float time, Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0)
        return;

    static unsigned char *s_batchFlags = NULL;
    static int s_batchFlagsCapacity = 0;
    if (fieldCount > s_batchFlagsCapacity)
    {
        unsigned char *grown = (unsigned char *)RL_REALLOC(s_batchFlags, (size_t)fieldCount);
        if (!grown)
            return;
        s_batchFlags = grown;
        s_batchFlagsCapacity = fieldCount;
    }
    unsigned char *flags = s_batchFlags;

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
    float zoomFactor = tanf(45.0f * 0.5f * DEG2RAD) / tanf(fovyRad * 0.5f);
    if (zoomFactor < 0.8f)
        zoomFactor = 0.8f;
    if (zoomFactor > 2.5f)
        zoomFactor = 2.5f;
    float hysteresis = quality >= GFX_HIGH ? 1.25f : 2.0f;
    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_LOW && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;

    bool anyShadow = false;
    bool anyDraw = false;
    for (int i = 0; i < fieldCount; i++)
    {
        MapFlowerField *field = &fields[i];
        flags[i] = 0;
        if (!field->ready)
            continue;
        s_natureRenderStats.flowerFieldsTested++;
        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
        if (!Nature_IsChunkVisible(center, field->boundsRadius))
        {
            s_natureRenderStats.flowerFieldsFrustumCulled++;
            continue;
        }
        float dx = camera.position.x - center.x;
        float dz = camera.position.z - center.z;
        float centerDistance = sqrtf(dx * dx + dz * dz);
        float visibleDistance = centerDistance - field->boundsRadius;
        if (field->drawDistance > 0.0f &&
            visibleDistance > field->drawDistance * rangeScale)
        {
            s_natureRenderStats.flowerFieldsDistanceCulled++;
            continue;
        }
        if (field->farReady && field->lodDistance > 0.0f)
        {
            float hSeed = field->boundsCenter.x * 0.1031f + field->boundsCenter.z * 0.3183f;
            float spatialHash = hSeed - floorf(hSeed);
            float threshold = focalDistH + field->lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 3.0f;
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
            f |= NATURE_FLOWER_BATCH_FAR;

        float maxContactDist = realShadowActive
                                   ? fminf(field->shadowDistance, 14.0f)
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
        anyDraw = true;
    }
    if (!anyDraw)
        return;

    if (anyShadow)
    {
        Shader shadowShader = FlowerShadow_GetShader();
        BeginShaderMode(shadowShader);
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        EndShaderMode();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        for (int i = 0; i < fieldCount; i++)
        {
            if (!(flags[i] & NATURE_FLOWER_BATCH_SHADOW))
                continue;
            DrawModel(fields[i].shadowModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.flowerShadowDraws++;
        }
        EndBlendMode();
        rlEnableDepthMask();
    }

    bool groupOpen = false;
    bool groupTextured = false;
    float groupAlpha = 0.0f;
    for (int i = 0; i < fieldCount; i++)
    {
        if (!(flags[i] & NATURE_FLOWER_BATCH_DRAW))
            continue;
        MapFlowerField *field = &fields[i];
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
        DrawModel(useFarModel ? field->farModel : field->model, worldOffset, 1.0f, WHITE);
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

void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    MapProp_DrawFlowerFieldBatch(field, 1, worldOffset, time, windDirection, windStrength);
}

// [TỐI ƯU BATCHED SHADOW CASTER]:
// Gom tất cả các ô hoa cần đổ bóng vào chung 1 lần mở/đóng Shader và sử dụng `farModel` 3D (9 đỉnh/bông)
// khi không bật bộ lọc chẩn đoán, giúp lượt vẽ bóng đổ hoa nhanh gấp ~12 lần!
void MapProp_DrawFlowerFieldShadowCasterBatch(MapFlowerField *fields, int fieldCount,
                                              Vector3 worldOffset, float time,
                                              Vector2 windDirection, float windStrength)
{
    if (!fields || fieldCount <= 0 || GfxQuality_Get() < GFX_LOW ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(true))
        return;

    bool filterActive = Nature_ShadowCasterFilterActive();
    Vector3 shadowFocus = EnvShadow_GetFocus();
    Shader shader = NatureShadow_GetShader();
    bool shaderBound = false;

    for (int i = 0; i < fieldCount; i++)
    {
        MapFlowerField *field = &fields[i];
        if (!field->ready)
            continue;
        Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
        if (!filterActive)
        {
            if (field->shadowDistance > 0.0f)
            {
                float dx = center.x - shadowFocus.x;
                float dz = center.z - shadowFocus.z;
                float maxDist = field->shadowDistance + field->boundsRadius + 2.0f;
                if (dx * dx + dz * dz > maxDist * maxDist)
                    continue;
            }
            if (!Nature_IntersectsDynamicShadowCoverage(center, field->boundsRadius))
                continue;
        }

        if (!shaderBound)
        {
            Nature_BeginWindReceiverShader(shader);
            Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                                      field->textured, field->alphaCutoff,
                                      NATURE_WIND_RESPONSE_FLOWER);
            rlDisableBackfaceCulling();
            shaderBound = true;
        }

        // Dùng farModel 3D nhẹ cho đổ bóng thực tế; giữ near model khi chạy test filterActive
        Model *castModel = (!filterActive && field->farReady && !field->textured)
                               ? &field->farModel
                               : &field->model;
        Shader previous = castModel->materials[0].shader;
        castModel->materials[0].shader = shader;
        DrawModel(*castModel, worldOffset, 1.0f, WHITE);
        castModel->materials[0].shader = previous;
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
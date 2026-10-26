// Flower Field System (3D parametric dished petals, phyllotaxis center domes, flower materials, and drawing).
// Split from map_props_nature.inl for clean modular domain separation.

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

static bool Nature_KeepFarFlower(const MapFlowerPlacement *flower)
{
    float hash = sinf(flower->position.x * 12.9898f + flower->position.z * 78.233f + flower->phase * 37.719f) * 43758.5453f;
    return hash - floorf(hash) < 0.56f;
}

static Model Nature_BuildFlowerFarModel(const MapFlowerPlacement *placements, int count,
                                        bool texturedBloom,
                                        int atlasColumns, int atlasRows)
{
    // At distance, stems are sub-pixel and alpha-cutout bloom overdraw dominates.
    // Keep a stable world-space subset and slightly compensate bloom coverage.
    int selected = 0;
    for (int i = 0; i < count; i++)
    {
        if (Nature_KeepFarFlower(&placements[i]))
            selected++;
    }
    bool forceFirst = selected == 0;
    if (forceFirst)
        selected = 1;
    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++)
    {
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
        float radius = flower->bloomRadius * petalScale * 0.72f;
        float ox0 = (-right.x - forward.x) * radius;
        float oz0 = (-right.z - forward.z) * radius;
        float ox1 = (right.x - forward.x) * radius;
        float oz1 = (right.z - forward.z) * radius;
        float ox2 = (right.x + forward.x) * radius;
        float oz2 = (right.z + forward.z) * radius;
        float ox3 = (-right.x + forward.x) * radius;
        float oz3 = (-right.z + forward.z) * radius;
        Vector3 p0 = {head.x + ox0, head.y + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
        Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
        Vector3 p2 = {head.x + ox2, head.y + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
        Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
        if (texturedBloom)
        {
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
        }
        else
        {
            Color softPetal = Nature_ScaleColor(flower->petalColor, 0.84f);
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                           phase, 0.55f, 0.65f, softPetal, softPetal);
        }
    }
    // Hoa luôn sử dụng nature_lit.fs (NatureShade) thay vì nature_opaque.fs (GrassShade)
    return Nature_ModelFromMesh(mesh, Nature_GetShader(true));
}

static inline int Nature_FlowerBloomVertexCount(int variant, bool textured)
{
    if (textured)
        return 36; // 3 textured bloom cards (18) + 3D pollen disc dome (18) = 36
    int morph = variant % 4;
    switch (morph)
    {
    case 0:
        return 210; // 2-layer Rose/Poppy: 5 outer + 3 inner (192) + dome (18) = 210
    case 1:
        return 210; // 8 oval daisy petals (192) + phyllotaxis disc (18) = 210
    case 2:
        return 210; // Lavender spike: 4 whorls x 2 florets (192) + tip bud dome (18) = 210
    case 3:
        return 162; // 6 reflexed lanceolate petals (144) + center dome (18) = 162
    default:
        return 210;
    }
}

typedef enum
{
    PETAL_SHAPE_OVAL,       // w(t) = W_max * sin(pi*t)^n
    PETAL_SHAPE_LANCEOLATE, // asymmetric: rapid bulge, long taper to sharp tip
    PETAL_SHAPE_OBCORDATE,  // heart-shaped with notched apex: w_base(t) - notch
    PETAL_SHAPE_SPATULATE,  // spoon-shaped: slender base, rounded wide tip
} PetalShapeType;

typedef struct
{
    PetalShapeType shape;
    float wMax;
    float length;
    float exponent;    // n for oval/obcordate power
    float tPeak;       // peak location (e.g. 0.30 for lanceolate, 0.75 for spatulate)
    float notchDepth;  // notch depth for obcordate apex
    float curveAmount; // vertical cupping / reflex (dy = curveAmount * t^2.5)
    float twistAmount; // twist angle around petal axis (radians)
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
        {
            return p->wMax * powf(t / tPeak, 0.50f);
        }
        else
        {
            return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.80f);
        }
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
        {
            return p->wMax * powf(t / tPeak, 0.65f);
        }
        else
        {
            return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.40f);
        }
    }
    default:
        return p->wMax * sinf(PI * t);
    }
}

static inline float Nature_EvalPetalElevation(float t, const PetalParams *p)
{
    // Botanical 3D Conical Chalice Cup:
    // Petals rise in a smooth upward bowl (conical lift of ~25-30 deg + curveAmount)
    float s = sinf(t * 1.5707963f);
    float conical = p->length * 0.28f * (t * 0.85f + 0.15f * t * t);
    return conical + p->curveAmount * (s * 1.25f - 0.25f * t * t);
}

static void Nature_AddParametricPetal(Mesh *mesh, int *cursor,
                                      Vector3 head, Vector3 headNormal, Vector3 dir, Vector3 side,
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
    if (fabsf(p->twistAmount) > 0.001f)
    {
        float phi1 = p->twistAmount * t1;
        float phi2 = p->twistAmount * t2;
        float phi3 = p->twistAmount * t3;
        side1 = Vector3Add(Vector3Scale(side, cosf(phi1)), Vector3Scale(headNormal, sinf(phi1) * len * 0.10f));
        side2 = Vector3Add(Vector3Scale(side, cosf(phi2)), Vector3Scale(headNormal, sinf(phi2) * len * 0.15f));
        side3 = Vector3Add(Vector3Scale(side, cosf(phi3)), Vector3Scale(headNormal, sinf(phi3) * len * 0.18f));
    }

    Vector3 p0 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t0), Vector3Scale(headNormal, dy0 + 0.003f)));
    Vector3 p1 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t1), Vector3Scale(headNormal, dy1)));
    Vector3 p2 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t2), Vector3Scale(headNormal, dy2)));
    Vector3 p3 = Vector3Add(head, Vector3Add(Vector3Scale(dir, len * t3), Vector3Scale(headNormal, dy3)));
    Vector3 pApex = Vector3Add(head, Vector3Add(Vector3Scale(dir, len), Vector3Scale(headNormal, dy4)));

    // Transverse cupping: petal margins dish upward across width creating rich 3D volume
    float cup0 = w0 * 0.28f;
    float cup1 = w1 * 0.38f;
    float cup2 = w2 * 0.32f;
    float cup3 = w3 * 0.20f;

    Vector3 b0 = Vector3Add(Vector3Subtract(p0, Vector3Scale(side0, w0)), Vector3Scale(headNormal, cup0));
    Vector3 b1 = Vector3Add(Vector3Add(p0, Vector3Scale(side0, w0)), Vector3Scale(headNormal, cup0));

    Vector3 m0 = Vector3Add(Vector3Subtract(p1, Vector3Scale(side1, w1)), Vector3Scale(headNormal, cup1));
    Vector3 m1 = Vector3Add(Vector3Add(p1, Vector3Scale(side1, w1)), Vector3Scale(headNormal, cup1));

    Vector3 s0 = Vector3Add(Vector3Subtract(p2, Vector3Scale(side2, w2)), Vector3Scale(headNormal, cup2));
    Vector3 s1 = Vector3Add(Vector3Add(p2, Vector3Scale(side2, w2)), Vector3Scale(headNormal, cup2));

    Vector3 c0 = Vector3Add(Vector3Subtract(p3, Vector3Scale(side3, w3)), Vector3Scale(headNormal, cup3));
    Vector3 c1 = Vector3Add(Vector3Add(p3, Vector3Scale(side3, w3)), Vector3Scale(headNormal, cup3));

    // Cupped analytical normals across width angling outward and upward
    Vector3 nL0 = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.20f), Vector3Scale(side, 0.40f)), Vector3Scale(headNormal, 0.88f)));
    Vector3 nR0 = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.20f), Vector3Scale(side, 0.40f)), Vector3Scale(headNormal, 0.88f)));
    Vector3 nL1 = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.40f), Vector3Scale(side1, 0.45f)), Vector3Scale(headNormal, 0.78f)));
    Vector3 nR1 = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.40f), Vector3Scale(side1, 0.45f)), Vector3Scale(headNormal, 0.78f)));
    Vector3 nL2 = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.58f), Vector3Scale(side2, 0.38f)), Vector3Scale(headNormal, 0.70f)));
    Vector3 nR2 = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.58f), Vector3Scale(side2, 0.38f)), Vector3Scale(headNormal, 0.70f)));
    Vector3 n3 = Vector3Normalize(Vector3Add(Vector3Scale(dir, 0.75f), Vector3Scale(headNormal, 0.62f)));

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
    Nature_SetVertex(mesh, (*cursor)++, c1, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, c1, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c0, n3, phase, 1.00f, cTip);

    // Segment 3: Botanical Rounded Dome Apex (6 vertices, 2 fan triangles to pApex)
    Vector3 nTipL = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(dir, 0.70f), Vector3Scale(side, 0.35f)), Vector3Scale(headNormal, 0.60f)));
    Vector3 nTipR = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(dir, 0.70f), Vector3Scale(side, 0.35f)), Vector3Scale(headNormal, 0.60f)));
    Nature_SetVertex(mesh, (*cursor)++, c0, nTipL, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipL, phase, 1.00f, cTip);

    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c1, nTipR, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipR, phase, 1.00f, cTip);
}

static Color Nature_GetSpeciesCenterColor(int variant)
{
    switch (variant & 7)
    {
    case 0: return (Color){248, 185, 22, 255}; // Daisy: rich golden amber
    case 1: return (Color){195, 115, 18, 255}; // Buttercup: deep warm burnt-orange/amber
    case 2: return (Color){34, 24, 24, 255};   // Poppy: dark velvety charcoal
    case 3: return (Color){45, 40, 78, 255};   // Cornflower: deep sapphire navy
    case 4: return (Color){238, 165, 24, 255}; // Peach-rose: bright honey gold
    case 5: return (Color){245, 200, 32, 255}; // Cosmos: sunny golden yolk
    case 6: return (Color){92, 58, 128, 255};  // Lavender: deep violet-aubergine
    case 7: return (Color){168, 185, 48, 255}; // Primrose: chartreuse lime-gold
    default: return (Color){248, 185, 22, 255};
    }
}

static void Nature_AddFlowerCenterDome(Mesh *mesh, int *cursor,
                                       Vector3 head, Vector3 headNormal,
                                       Vector3 headRight, Vector3 headForward,
                                       float cR, float cHeight, float phase, Color centerColor)
{
    Vector3 apex = Vector3Add(head, Vector3Scale(headNormal, cHeight * 1.45f));
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    // Outer stamen collar: rich contrasting darker rim for floral depth
    Color rimColor = Nature_ScaleColor(centerColor, 0.45f);
    for (int s = 0; s < 6; s++)
    {
        float a0 = (float)s * 1.04719755f;
        float a1 = (float)(s + 1) * 1.04719755f;
        float goldMod0 = 1.0f + 0.08f * sinf((float)s * 2.39996f);
        float goldMod1 = 1.0f + 0.08f * sinf((float)(s + 1) * 2.39996f);
        Vector3 rad0 = Vector3Add(Vector3Scale(headRight, cosf(a0) * cR * goldMod0),
                                  Vector3Scale(headForward, sinf(a0) * cR * goldMod0));
        Vector3 rad1 = Vector3Add(Vector3Scale(headRight, cosf(a1) * cR * goldMod1),
                                  Vector3Scale(headForward, sinf(a1) * cR * goldMod1));
        Vector3 r0 = Vector3Add(head, Vector3Add(rad0, Vector3Scale(headNormal, 0.005f)));
        Vector3 r1 = Vector3Add(head, Vector3Add(rad1, Vector3Scale(headNormal, 0.005f)));

        float midA = (a0 + a1) * 0.5f;
        Vector3 midRad = Vector3Add(Vector3Scale(headRight, cosf(midA) * 0.55f),
                                    Vector3Scale(headForward, sinf(midA) * 0.55f));
        Vector3 norm = Vector3Normalize(Vector3Add(midRad, Vector3Scale(headNormal, 0.84f)));

        Nature_SetVertex(mesh, (*cursor)++, apex, headNormal, phase, 1.0f, apexColor);
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

    int vertexCount = 0;
    for (int i = 0; i < count; i++)
    {
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
    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float r = flower->bloomRadius * petalScale;

        // 1. Basal leaf rosette: 4 spreading leaves hugging soil to anchor plant (24 verts)
        for (int b = 0; b < 4; b++)
        {
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

        // 2. Gracefully curved stem leaning with the meadow breeze (no rigid vertical T-stick)
        float stemWidth = fmaxf(0.0045f, flower->bloomRadius * 0.07f);
        float stemLeanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 3.14f;
        float stemLean = h * 0.22f;
        float stemDirX = cosf(stemLeanAngle);
        float stemDirZ = sinf(stemLeanAngle);
        Vector3 stemP0 = base;
        Vector3 midStem = (Vector3){base.x + stemDirX * (stemLean * 0.38f), base.y + h * 0.52f, base.z + stemDirZ * (stemLean * 0.38f)};
        Vector3 head = (Vector3){base.x + stemDirX * stemLean, base.y + h * 0.94f, base.z + stemDirZ * stemLean};

        for (int cross = 0; cross < 2; cross++)
        {
            float angle = flower->rotationDeg * DEG2RAD + cross * PI * 0.5f;
            Vector3 side = {cosf(angle) * stemWidth, 0.0f, sinf(angle) * stemWidth};
            Vector3 normal = {-sinf(angle), 0.08f, cosf(angle)};

            Vector3 s0_0 = Vector3Subtract(stemP0, side);
            Vector3 s0_1 = Vector3Add(stemP0, side);
            Vector3 s0_2 = Vector3Add(midStem, Vector3Scale(side, 0.8f));
            Vector3 s0_3 = Vector3Subtract(midStem, Vector3Scale(side, 0.8f));
            Nature_AddQuad(&mesh, &cursor, s0_0, s0_1, s0_2, s0_3, normal, phase, 0.0f, 0.5f, stemColor, stemColor);

            Vector3 s1_0 = s0_3;
            Vector3 s1_1 = s0_2;
            Vector3 s1_2 = Vector3Add(head, Vector3Scale(side, 0.5f));
            Vector3 s1_3 = Vector3Subtract(head, Vector3Scale(side, 0.5f));
            Nature_AddQuad(&mesh, &cursor, s1_0, s1_1, s1_2, s1_3, normal, phase, 0.5f, 1.0f, stemColor, stemColor);
        }

        // 3. Mid-stem foliage leaves (12 verts)
        for (int leaf = 0; leaf < 2; leaf++)
        {
            float rootT = leaf == 0 ? 0.35f : 0.65f;
            float leafAngle = flower->rotationDeg * DEG2RAD * 0.73f + (leaf == 0 ? 1.40f : -1.40f);
            Vector3 lDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 lSide = {-lDir.z * 0.012f, 0.0f, lDir.x * 0.012f};
            float lLen = fmaxf(0.045f, flower->bloomRadius * 0.75f);
            Vector3 lRoot = {base.x, base.y + h * rootT, base.z};
            Vector3 lTip = {lRoot.x + lDir.x * lLen, lRoot.y + lLen * 0.35f, lRoot.z + lDir.z * lLen};
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(lRoot, lSide), Vector3Add(lRoot, lSide),
                           Vector3Add(lTip, Vector3Scale(lSide, 0.2f)), Vector3Subtract(lTip, Vector3Scale(lSide, 0.2f)),
                           (Vector3){0, 1, 0}, phase, rootT, rootT + 0.3f, stemColor, Nature_ScaleColor(stemColor, 1.15f));
        }

        // 4. Calyx cup: 4 green sepals cradling bloom head from below (24 verts)
        float calyxLen = r * 0.45f;
        for (int c = 0; c < 4; c++)
        {
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
        // Natural flower head tilt towards light/wind (12 deg ~ 16 deg) along stem lean direction
        float tiltAmount = 0.22f + 0.08f * sinf(phase * 6.28f);
        Vector3 headNormal = Vector3Normalize((Vector3){stemDirX * tiltAmount, 0.96f, stemDirZ * tiltAmount});
        float stemAngle = flower->rotationDeg * DEG2RAD;
        Vector3 refRight = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
        Vector3 headRight = Vector3Normalize(Vector3Subtract(refRight, Vector3Scale(headNormal, Vector3DotProduct(refRight, headNormal))));
        Vector3 headForward = Vector3CrossProduct(headNormal, headRight);
        Color effCenterColor = Nature_GetSpeciesCenterColor(flower->bloomVariant);
        if (centerColor.a > 0 && flower->bloomVariant < 0)
            effCenterColor = centerColor;

        if (field.textured)
        {
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

            Vector3 bloomNormal = headNormal;
            Vector3 right = headRight;
            Vector3 forward = headForward;

            float ox0 = (-right.x - forward.x) * r;
            float oz0 = (-right.z - forward.z) * r;
            float ox1 = (right.x - forward.x) * r;
            float oz1 = (right.z - forward.z) * r;
            float ox2 = (right.x + forward.x) * r;
            float oz2 = (right.z + forward.z) * r;
            float ox3 = (-right.x + forward.x) * r;
            float oz3 = (-right.z + forward.z) * r;
            Vector3 p0 = {head.x + ox0, head.y, head.z + oz0};
            Vector3 p1 = {head.x + ox1, head.y, head.z + oz1};
            Vector3 p2 = {head.x + ox2, head.y, head.z + oz2};
            Vector3 p3 = {head.x + ox3, head.y, head.z + oz3};
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);

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

            Vector3 k0 = {head.x - f1.x, head.y - vH * 0.15f, head.z - f1.z};
            Vector3 k1 = {head.x + f1.x, head.y - vH * 0.15f, head.z + f1.z};
            Vector3 k2 = {head.x + f1.x, head.y + vH * 0.85f, head.z + f1.z};
            Vector3 k3 = {head.x - f1.x, head.y + vH * 0.85f, head.z - f1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, k0, k1, k2, k3, r1,
                                    phase, flower->petalColor, uvRect);

            Nature_AddFlowerCenterDome(&mesh, &cursor, head, headNormal, headRight, headForward, r * 0.22f, r * 0.10f, phase, effCenterColor);
        }
        else
        {
            int morph = flower->bloomVariant % 4;
            Color cBase = Nature_ScaleColor(flower->petalColor, 0.72f);
            Color cMid = flower->petalColor;
            Color cTip = Nature_ScaleColor(flower->petalColor, 1.15f);

            switch (morph)
            {
            case 0:
            {
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
                    .curveAmount = r * 0.52f,
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
                PetalParams pDaisy = {
                    .shape = PETAL_SHAPE_OVAL,
                    .wMax = r * 0.35f,
                    .length = r * 1.18f,
                    .exponent = 0.52f,
                    .curveAmount = r * 0.22f,
                    .twistAmount = 0.03f,
                };
                for (int k = 0; k < 8; k++)
                {
                    float angle = (float)k * 0.785398f;
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
                float spH = r * 2.2f;
                float phaseSpiral = 0.38f;
                for (int k = 0; k < 4; k++)
                {
                    float s = 0.22f + (float)k * 0.24f;
                    Vector3 tierHead = Vector3Add(head, Vector3Scale(headNormal, spH * s));
                    float bloomFactor = (s < 0.65f) ? 1.0f : fmaxf(0.40f, 1.0f - (s - 0.65f) / 0.35f);
                    PetalParams pFloret = {
                        .shape = PETAL_SHAPE_OVAL,
                        .wMax = r * 0.22f * bloomFactor,
                        .length = r * 0.60f * bloomFactor,
                        .exponent = 1.10f,
                        .curveAmount = r * 0.25f * bloomFactor,
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
                PetalParams pBell = {
                    .shape = PETAL_SHAPE_LANCEOLATE,
                    .wMax = r * 0.34f,
                    .length = r * 1.12f,
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

    // Pack shared 3D petal/stem vertices into 16-bit indices when eligible (~50% VS reduction)
    if (!field.textured)
        (void)Nature_IndexBladeMesh(&mesh);

    field.textured = (petalTexturePath != NULL);
    // Luôn sử dụng Nature_GetShader(true) để gọi nature_lit.fs (chứa NatureShade cho hoa/cánh hoa)
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
    field.lodDistance = 28.0f;
    field.shadowDistance = 18.0f;
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

// Batched flower draw.
//
// Flower fields are small spatial cells (see MapFlora_SplitFlowerCell in map_props_flora.inl), so
// frustum / distance / near-far LOD decisions are made per cell instead of per 20m+ cluster.
// Everything that used to be repeated per field is now done once per batch:
//   - per-frame camera / quality constants (focal distance, zoom, ranges)
//   - projected contact-shadow pass state (shader mode, blend mode, depth mask flushes)
//   - wind-receiver shader binding + Nature_UpdateShader uniforms
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

    // ---- per-frame constants (previously recomputed for every field) ----
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

    // ---- pass 1: cull + LOD (identical rules to the old per-field path) ----
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
            float spatialHash = sinf(field->boundsCenter.x * 12.9898f + field->boundsCenter.z * 78.233f);
            spatialHash -= floorf(spatialHash);
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

        // When real directional shadows are active, limit contact shadow pass to close range
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

    // ---- pass 2: projected contact shadows, one state setup for all fields ----
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

    // ---- pass 3: flower geometry, shader/wind uniforms set once per (textured, alpha) group ----
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

void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready || GfxQuality_Get() < GFX_LOW ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(true))
        return;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    bool filterActive = Nature_ShadowCasterFilterActive();
    if (!filterActive)
    {
        if (field->shadowDistance > 0.0f)
        {
            Vector3 shadowFocus = EnvShadow_GetFocus();
            float dx = center.x - shadowFocus.x;
            float dz = center.z - shadowFocus.z;
            float maxDist = field->shadowDistance + field->boundsRadius + 2.0f;
            if (dx * dx + dz * dz > maxDist * maxDist)
                return;
        }
        if (!Nature_IntersectsDynamicShadowCoverage(center, field->boundsRadius))
            return;
    }

    Shader shader = NatureShadow_GetShader();
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              field->textured, field->alphaCutoff,
                              NATURE_WIND_RESPONSE_FLOWER);
    // Preserve full near geometry inside the primary focus and for diagnostic verification
    Model castModel = field->model;
    Shader previous = castModel.materials[0].shader;
    castModel.materials[0].shader = shader;
    rlDisableBackfaceCulling();
    DrawModel(castModel, worldOffset, 1.0f, WHITE);
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
    castModel.materials[0].shader = previous;
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
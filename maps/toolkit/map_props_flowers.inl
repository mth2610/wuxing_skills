// Flower Field System (3D parametric dished petals, phyllotaxis center domes, flower materials, and drawing).
// Split from map_props_nature.inl for clean modular domain separation.

static Model Nature_BuildFlowerShadowModel(const MapFlowerPlacement *placements, int count)
{
    Mesh mesh = Nature_AllocMesh(count * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++)
    {
        const MapFlowerPlacement *flower = &placements[i];
        float width = fmaxf(flower->bloomRadius * 0.95f, 0.045f);
        Vector3 root = flower->position;
        root.y += 0.0015f;
        Vector3 encoded = {flower->height, width, flower->phase};
        Color rootShade = {180, 180, 180, 255};
        Color tipShade = {110, 110, 110, 255};
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
        float radius = flower->bloomRadius * petalScale * 1.08f * 1.12f;
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
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                           phase, 1.0f, 1.0f, flower->petalColor, flower->petalColor);
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(texturedBloom));
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
    if (fabsf(p->twistAmount) > 0.001f)
    {
        float phi1 = p->twistAmount * t1;
        float phi2 = p->twistAmount * t2;
        float phi3 = p->twistAmount * t3;
        side1 = (Vector3){side.x * cosf(phi1), sinf(phi1) * len * 0.10f, side.z * cosf(phi1)};
        side2 = (Vector3){side.x * cosf(phi2), sinf(phi2) * len * 0.15f, side.z * cosf(phi2)};
        side3 = (Vector3){side.x * cosf(phi3), sinf(phi3) * len * 0.18f, side.z * cosf(phi3)};
    }

    Vector3 p0 = {head.x + dir.x * (len * t0), head.y + dy0 + 0.003f, head.z + dir.z * (len * t0)};
    Vector3 p1 = {head.x + dir.x * (len * t1), head.y + dy1, head.z + dir.z * (len * t1)};
    Vector3 p2 = {head.x + dir.x * (len * t2), head.y + dy2, head.z + dir.z * (len * t2)};
    Vector3 p3 = {head.x + dir.x * (len * t3), head.y + dy3, head.z + dir.z * (len * t3)};
    Vector3 pApex = {head.x + dir.x * len, head.y + dy4, head.z + dir.z * len};

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
    Vector3 n3 = Vector3Normalize((Vector3){dir.x * 0.68f, 0.70f, dir.z * 0.68f});

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
    Vector3 nTipL = Vector3Normalize((Vector3){dir.x * 0.70f - side.x * 0.35f, 0.60f, dir.z * 0.70f - side.z * 0.35f});
    Vector3 nTipR = Vector3Normalize((Vector3){dir.x * 0.70f + side.x * 0.35f, 0.60f, dir.z * 0.70f + side.z * 0.35f});
    Nature_SetVertex(mesh, (*cursor)++, c0, nTipL, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipL, phase, 1.00f, cTip);

    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c1, nTipR, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipR, phase, 1.00f, cTip);
}

static void Nature_AddFlowerCenterDome(Mesh *mesh, int *cursor, Vector3 head, float cR, float cHeight, float phase, Color centerColor)
{
    Vector3 apex = {head.x, head.y + cHeight * 1.35f, head.z};
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    Color rimColor = Nature_ScaleColor(centerColor, 0.75f);
    for (int s = 0; s < 6; s++)
    {
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

        // 2. Straight upright stem (24 verts)
        float stemWidth = fmaxf(0.005f, flower->bloomRadius * 0.08f);
        Vector3 stemP0 = base;
        Vector3 midStem = (Vector3){base.x, base.y + h * 0.50f, base.z};
        Vector3 head = (Vector3){base.x, base.y + h, base.z};

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

            float stemAngle = flower->rotationDeg * DEG2RAD;
            Vector3 bloomNormal = (Vector3){0.0f, 1.0f, 0.0f};
            Vector3 right = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
            Vector3 forward = {-sinf(stemAngle), 0.0f, cosf(stemAngle)};

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

            Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.22f, r * 0.10f, phase, centerColor);
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
                for (int k = 0; k < 3; k++)
                {
                    float angle = flower->rotationDeg * DEG2RAD + 0.628318f + (float)k * 2.094395f;
                    Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                    Vector3 side = {-dir.z, 0.0f, dir.x};
                    Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pInner,
                                              phase, cInBase, cInMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.28f, r * 0.16f, phase, centerColor);
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
                    float angle = flower->rotationDeg * DEG2RAD + (float)k * 0.785398f;
                    Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                    Vector3 side = {-dir.z, 0.0f, dir.x};
                    Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pDaisy,
                                              phase, cBase, cMid, cTip);
                }
                Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.35f, r * 0.15f, phase, centerColor);
                break;
            }
            case 2:
            {
                float spH = r * 2.2f;
                float phaseSpiral = 0.38f;
                for (int k = 0; k < 4; k++)
                {
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
                    for (int j = 0; j < 2; j++)
                    {
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

    // Pack shared 3D petal/stem vertices into 16-bit indices when eligible (~50% VS reduction)
    if (!field.textured)
        (void)Nature_IndexBladeMesh(&mesh);

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

void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready)
        return;
    s_natureRenderStats.flowerFieldsTested++;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    if (!Nature_IsChunkVisible(center, field->boundsRadius))
    {
        s_natureRenderStats.flowerFieldsFrustumCulled++;
        return;
    }
    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH  ? 1.0f
                       : quality == GFX_MED ? 0.84f
                       : quality == GFX_LOW ? 0.68f
                                            : 0.55f;
    float dx = camera.position.x - center.x;
    float dz = camera.position.z - center.z;
    float centerDistance = sqrtf(dx * dx + dz * dz);
    float visibleDistance = centerDistance - field->boundsRadius;
    if (field->drawDistance > 0.0f &&
        visibleDistance > field->drawDistance * rangeScale)
    {
        s_natureRenderStats.flowerFieldsDistanceCulled++;
        return;
    }
    float lodScale = quality >= GFX_HIGH  ? 1.0f
                     : quality == GFX_MED ? 0.82f
                     : quality == GFX_LOW ? 0.58f
                                          : 0.45f;
    if (field->farReady && field->lodDistance > 0.0f)
    {
        float focalDx = camera.position.x - camera.target.x;
        float focalDz = camera.position.z - camera.target.z;
        float focalDistH = sqrtf(focalDx * focalDx + focalDz * focalDz);
        float fovyRad = fmaxf(camera.fovy, 15.0f) * DEG2RAD;
        float zoomFactor = tanf(45.0f * 0.5f * DEG2RAD) / tanf(fovyRad * 0.5f);
        if (zoomFactor < 0.8f)
            zoomFactor = 0.8f;
        if (zoomFactor > 2.5f)
            zoomFactor = 2.5f;
        float spatialHash = sinf(field->boundsCenter.x * 12.9898f + field->boundsCenter.z * 78.233f);
        spatialHash -= floorf(spatialHash);
        float threshold = focalDistH + field->lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 3.0f;
        float hysteresis = quality >= GFX_HIGH ? 1.25f : 2.0f;
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
    bool useFarModel = field->farReady && field->farLod;
    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_LOW && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    // When real directional shadows are active, limit contact shadow pass to close range
    float maxContactDist = realShadowActive
                               ? fminf(field->shadowDistance, 14.0f)
                               : (field->shadowDistance * (quality >= GFX_HIGH ? 1.0f : 0.78f));
    bool shadowInRange = field->shadowDistance <= 0.0f || visibleDistance <= maxContactDist;

    if (field->shadowReady && useProjectedShadows && shadowInRange)
    {
        Shader shadowShader = FlowerShadow_GetShader();
        BeginShaderMode(shadowShader);
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        EndShaderMode();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawModel(field->shadowModel, worldOffset, 1.0f, WHITE);
        s_natureRenderStats.flowerShadowDraws++;
        EndBlendMode();
        rlEnableDepthMask();
    }
    else if (field->shadowReady && useProjectedShadows && !shadowInRange)
    {
        s_natureRenderStats.flowerShadowDistanceCulled++;
    }
    Shader shader = Nature_GetShader(field->textured);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        field->textured, field->alphaCutoff,
                        NATURE_WIND_RESPONSE_FLOWER);
    rlDisableBackfaceCulling();
    DrawModel(useFarModel ? field->farModel : field->model, worldOffset, 1.0f, WHITE);
    Nature_EndWindReceiverShader();
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
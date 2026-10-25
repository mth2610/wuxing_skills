// Meadow System (Grass tufts, cubic Bézier blade generation, LODs, and meadow rendering).
// Split from map_props_nature.inl for clean modular domain separation.

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
    // Transverse coordinate u in [-1.0, +1.0] across blade width
    Nature_SetUv2(mesh, base + 0, -1.0f, h0);
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 1.0f, h1);
    Nature_SetUv2(mesh, base + 3, -1.0f, h0);
    Nature_SetUv2(mesh, base + 4, 1.0f, h1);
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
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 0.0f, 1.0f); // Tip apex is center spine (u = 0.0)
}

static Model Nature_BuildMeadowShadowChunk(const MapMeadowPlacement *placements, int count,
                                           float minX, float maxX, float minZ, float maxZ)
{
    int selected = 0;
    for (int i = 0; i < count; i++)
    {
        Vector3 p = placements[i].position;
        if (p.x >= minX && p.x < maxX && p.z >= minZ && p.z < maxZ)
            selected++;
    }
    if (selected <= 0)
        return (Model){0};

    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    const Vector2 uv[6] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {0.0f, 0.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (int i = 0; i < count; i++)
    {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        Vector3 root = clump->position;
        root.y += 0.0012f;
        float width = fmaxf(clump->radius * 0.72f, 0.038f);
        Vector3 encoded = {clump->height * 1.05f, width, clump->phase};
        Color rootShade = {240, 240, 240, 255};
        Color tipShade = {178, 178, 178, 255};
        for (int vertex = 0; vertex < 6; vertex++)
        {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, clump->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

#include "maps/toolkit/meadow_palette.h"

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData)
{
    if (!outPlacements || maxCount <= 0 || distribution.spacing <= 0.0f)
        return 0;
    if (distribution.jitter < 0.0f)
        distribution.jitter = 0.0f;
    if (distribution.jitter > 0.95f)
        distribution.jitter = 0.95f;

    float rowSpacing = distribution.spacing * 0.8660254f;
    int columns = (int)ceilf((distribution.maxBounds.x - distribution.minBounds.x) /
                             distribution.spacing);
    int rows = (int)ceilf((distribution.maxBounds.y - distribution.minBounds.y) /
                          rowSpacing);
    unsigned int rng = distribution.seed ? distribution.seed : 1u;
    int count = 0;
    unsigned int accepted = 0;
    unsigned int selectionRng = rng ^ 0xa511e9b3u;
    for (int row = 0; row < rows; row++)
    {
        float rowOffset = (row & 1) ? (distribution.spacing * 0.5f) : 0.0f;
        for (int column = 0; column < columns; column++)
        {
            float jx = (Nature_Random01(&rng) - 0.5f) * distribution.spacing * distribution.jitter;
            float jz = (Nature_Random01(&rng) - 0.5f) * rowSpacing * distribution.jitter;
            float x = distribution.minBounds.x + column * distribution.spacing + rowOffset + jx;
            float z = distribution.minBounds.y + (row + 0.5f) * rowSpacing + jz;
            float density = densityFn ? densityFn(x, z, userData) : 1.0f;
            if (density < 0.0f)
                density = 0.0f;
            if (density > 1.0f)
                density = 1.0f;
            if (Nature_Random01(&rng) > density)
                continue;

            float y = groundCenter.y;
            if (ground && ground->ready)
                y = MapProp_SampleGroundHeight(ground, groundCenter, x, z);
            MapMeadowPlacement candidate;
            MapMeadowPlacement *placement = &candidate;
            placement->position = (Vector3){x, y + distribution.yOffset, z};
            placement->radius = distribution.minRadius + (distribution.maxRadius - distribution.minRadius) * Nature_Random01(&rng);
            placement->height = distribution.minHeight + (distribution.maxHeight - distribution.minHeight) * Nature_Random01(&rng);
            placement->rotationDeg = Nature_Random01(&rng) * 360.0f;
            placement->phase = Nature_Random01(&rng);
            accepted++;
            if (count < maxCount)
                outPlacements[count++] = candidate;
            else
            {
                unsigned int slot = (unsigned int)((double)Nature_NextRandom(&selectionRng) *
                                                   (double)accepted / 4294967296.0);
                if (slot < (unsigned int)maxCount)
                    outPlacements[slot] = candidate;
            }
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
    float b1 = 3.0f * omt2 * t;
    float b2 = 3.0f * omt * t2;
    return (Vector3){
        omt3 * p0.x + b1 * p1.x + b2 * p2.x + t3 * p3.x,
        omt3 * p0.y + b1 * p1.y + b2 * p2.y + t3 * p3.y,
        omt3 * p0.z + b1 * p1.z + b2 * p2.z + t3 * p3.z};
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
        c0 * v0.z + c1 * v1.z + c2 * v2.z};
}

typedef struct
{
    Vector3 p0, p1, p2, p3, clump;
    Color rootColor, tipColor;
    float width, phase, leanAngle;
    bool isReed;
    bool isSeedhead;
} NatureBladeDescriptor;

// Single authoring implementation for expanded meshes and compact templates.
static NatureBladeDescriptor Nature_DescribeMeadowBlade(const MapMeadowPlacement *clump,
                                                        int i, int blade, MapMeadowStyle style, int bladesPerClump, int bladeSegments,
                                                        float widthMultiplier)
{
    const float golden = 2.39996323f;
    bool isReed = style.growthForm == MAP_MEADOW_GROWTH_REED ||
                  (style.growthForm == MAP_MEADOW_GROWTH_AUTO && clump->height > 0.95f);
    bool isPampas = style.growthForm == MAP_MEADOW_GROWTH_PAMPAS;
    float clumpAngle = clump->rotationDeg * DEG2RAD;

    int bladeId = bladesPerClump > 1 && bladesPerClump < style.bladesPerClump
                      ? blade * (style.bladesPerClump - 1) / (bladesPerClump - 1)
                      : blade;
    unsigned int bladeSeed = (unsigned int)i * 747796405u + (unsigned int)bladeId * 2891336453u + 277803737u;
    float bHash = Nature_Random01(&bladeSeed);
    float bHash2 = Nature_Random01(&bladeSeed);
    float bHash3 = Nature_Random01(&bladeSeed);

    float flowX = cosf(clumpAngle);
    float flowZ = sinf(clumpAngle);

    float height = 0.0f;
    float width = 0.0f;
    float lean = 0.0f;
    float droopY = 0.0f;
    float bladeLeanAngle = 0.0f;
    Vector3 pBase = {0};
    Vector3 pP1 = {0}, pP2 = {0}, pP3 = {0};
    Color bladeRoot = {0};
    Color bladeTip = {0};
    bool isSeedhead = false;

    if (isReed)
    {
        float baseAngle = (float)blade * (2.0f * PI / (float)bladesPerClump);
        float bladeAzimuth = baseAngle + (bHash - 0.5f) * 0.95f;
        float radX = cosf(bladeAzimuth);
        float radZ = sinf(bladeAzimuth);

        float windWeight = 0.65f;
        float radWeight = 1.0f - windWeight;
        float combX = radX * radWeight + flowX * windWeight;
        float combZ = radZ * radWeight + flowZ * windWeight;
        float combLen = sqrtf(combX * combX + combZ * combZ);
        float dirX, dirZ;
        if (combLen < 0.01f)
        {
            dirX = flowX;
            dirZ = flowZ;
        }
        else
        {
            float invLen = 1.0f / combLen;
            dirX = combX * invLen;
            dirZ = combZ * invLen;
        }
        bladeLeanAngle = atan2f(dirZ, dirX);

        float collarRadius = clump->radius * 0.22f * (0.75f + 0.50f * bHash3);
        float bx = clump->position.x + radX * collarRadius;
        float bz = clump->position.z + radZ * collarRadius;

        float tierFrac = (float)blade / (float)bladesPerClump;
        float lengthScale = 0.85f + 0.30f * sinf((float)(i * 13 + blade * 7));
        height = clump->height * lengthScale;
        width = clump->radius * style.bladeWidthScale * widthMultiplier * (0.85f + 0.34f * bHash2);
        lean = height * (0.38f + 0.22f * tierFrac);
        droopY = height * (0.08f + 0.16f * bHash2);

        pBase = (Vector3){bx, clump->position.y - 0.010f, bz};
        pP1 = (Vector3){
            bx + dirX * (lean * 0.24f),
            pBase.y + height * 0.36f,
            bz + dirZ * (lean * 0.24f)};
        pP2 = (Vector3){
            bx + dirX * (lean * 0.60f),
            pBase.y + height * 0.72f,
            bz + dirZ * (lean * 0.60f)};
        pP3 = (Vector3){
            bx + dirX * (lean * 0.98f),
            pBase.y + (height * 0.86f - droopY),
            bz + dirZ * (lean * 0.98f)};

        // [TỐI ƯU VISUAL]: Nâng nhẹ sắc độ gốc cỏ sậy để không tạo mảng đen gắt trên nền đất ẩm
        bool isOuterDrySheath = (blade < 2);
        if (isOuterDrySheath)
        {
            bladeRoot = (Color){48, 56, 26, 255};
            bladeTip = (Color){164, 172, 84, 255};
        }
        else
        {
            bladeRoot = (Color){36, 58, 25, 255};
            bladeTip = (Color){128, 178, 70, 255};
        }
    }
    else if (isPampas)
    {
        float baseAzimuth = clumpAngle + (float)bladeId * golden + (bHash - 0.5f) * 0.65f;
        float radX = cosf(baseAzimuth);
        float radZ = sinf(baseAzimuth);

        float windWeight = (bladeId < 2) ? 0.38f : 0.25f;
        float combX = radX * (1.0f - windWeight) + flowX * windWeight;
        float combZ = radZ * (1.0f - windWeight) + flowZ * windWeight;
        float combLen = sqrtf(combX * combX + combZ * combZ);
        float dirX = (combLen > 0.01f) ? (combX / combLen) : flowX;
        float dirZ = (combLen > 0.01f) ? (combZ / combLen) : flowZ;
        bladeLeanAngle = atan2f(dirZ, dirX);

        float collarRadius = clump->radius * ((bladeId < 2) ? 0.12f : (0.24f + 0.12f * bHash3));
        float bx = clump->position.x + radX * collarRadius;
        float bz = clump->position.z + radZ * collarRadius;

        if (bladeId == 0)
        {
            // [TỐI ƯU VISUAL]: Tăng độ uốn cong mềm mại (lean & droop) và dịu nhẹ độ chói cho thân
            // bông cỏ tranh để không bị dựng đứng như thanh kiếm trắng cứng ở mức zoom gần.
            isSeedhead = true;
            height = clump->height * (1.24f + 0.26f * bHash2);
            width = clump->radius * style.bladeWidthScale * (1.02f + 0.18f * bHash3);
            lean = height * (0.34f + 0.12f * bHash);
            droopY = height * (0.11f + 0.05f * bHash3);

            pBase = (Vector3){bx, clump->position.y - 0.010f, bz};
            pP1 = (Vector3){bx + dirX * (lean * 0.24f), pBase.y + height * 0.38f, bz + dirZ * (lean * 0.24f)};
            pP2 = (Vector3){bx + dirX * (lean * 0.58f), pBase.y + height * 0.73f, bz + dirZ * (lean * 0.58f)};
            pP3 = (Vector3){bx + dirX * (lean * 0.96f), pBase.y + (height * 0.92f - droopY), bz + dirZ * (lean * 0.96f)};

            bladeRoot = (Color){44, 54, 26, 255};
            bladeTip = (Color){158, 168, 96, 255};
        }
        else
        {
            isSeedhead = false;
            height = clump->height * (0.68f + 0.18f * bHash2);
            width = clump->radius * style.bladeWidthScale * 0.90f;
            lean = height * (0.65f + 0.18f * bHash);
            droopY = height * (0.24f + 0.08f * bHash3);

            pBase = (Vector3){bx, clump->position.y - 0.012f, bz};
            pP1 = (Vector3){bx + dirX * (lean * 0.24f), pBase.y + height * 0.34f, bz + dirZ * (lean * 0.24f)};
            pP2 = (Vector3){bx + dirX * (lean * 0.58f), pBase.y + height * 0.66f, bz + dirZ * (lean * 0.58f)};
            pP3 = (Vector3){bx + dirX * (lean * 0.96f), pBase.y + (height * 0.86f - droopY), bz + dirZ * (lean * 0.96f)};

            bladeRoot = (Color){32, 46, 20, 255};
            bladeTip = (Color){122, 160, 66, 255};
        }
    }
    else if (bladesPerClump <= 2 && bladeSegments <= 2)
    {
        float splay = bladesPerClump == 1 ? 0.0f : (blade == 0 ? -0.26f : 0.26f);
        bladeLeanAngle = clumpAngle + splay + (bHash - 0.5f) * 0.22f;
        float dirX = cosf(bladeLeanAngle);
        float dirZ = sinf(bladeLeanAngle);
        float diskFrac = ((float)bladeId + 0.5f) / (float)fmax(style.bladesPerClump, 1);
        float collarRadius = clump->radius * (0.20f + 0.72f * sqrtf(diskFrac)) * (0.86f + 0.26f * bHash3);
        float rootAzimuth = clumpAngle + (float)bladeId * golden + (bHash3 - 0.5f) * 0.50f;
        float bx = clump->position.x + cosf(rootAzimuth) * collarRadius;
        float bz = clump->position.z + sinf(rootAzimuth) * collarRadius;

        height = clump->height * (0.94f + 0.18f * bHash2);
        width = clump->radius * style.bladeWidthScale * widthMultiplier * 0.94f;
        lean = height * (0.58f + 0.18f * bHash3);
        droopY = height * 0.16f;

        pBase = (Vector3){bx, clump->position.y - 0.012f, bz};
        pP1 = (Vector3){bx + dirX * (lean * 0.22f), pBase.y + height * 0.36f, bz + dirZ * (lean * 0.22f)};
        pP2 = (Vector3){bx + dirX * (lean * 0.56f), pBase.y + height * 0.70f, bz + dirZ * (lean * 0.56f)};
        pP3 = (Vector3){bx + dirX * (lean * 0.98f), pBase.y + height * 0.94f, bz + dirZ * (lean * 0.98f)};

        bladeRoot = style.rootColor;
        bladeTip = style.tipColor;
    }
    else
    {
        // Ghost of Tsushima Coherent Clump Posture + Sunflower Disk Root Distribution:
        // Distribute blade roots across the full tuft radius (0.20..0.94 * radius) using Vogel's
        // golden-angle spiral so individual blades cover the ground uniformly (~10cm apart)
        // instead of bunching into 5-blade points with bare dirt between tufts!
        float rootAzimuth = clumpAngle + (float)bladeId * golden + (bHash - 0.5f) * 0.55f;
        float radX = cosf(rootAzimuth);
        float radZ = sinf(rootAzimuth);
        float diskFrac = ((float)bladeId + 0.5f) / (float)fmax(style.bladesPerClump, 1);
        float collarRadius = clump->radius * (0.20f + 0.74f * sqrtf(diskFrac)) * (0.86f + 0.28f * bHash3);
        float bx = clump->position.x + radX * collarRadius;
        float bz = clump->position.z + radZ * collarRadius;

        float bladeFrac = (style.bladesPerClump > 1)
                              ? ((float)bladeId / (float)(style.bladesPerClump - 1) - 0.5f)
                              : 0.0f;
        float fanYaw = bladeFrac * 0.46f + (bHash - 0.5f) * 0.18f;
        bladeLeanAngle = clumpAngle + fanYaw;
        float dirX = cosf(bladeLeanAngle);
        float dirZ = sinf(bladeLeanAngle);

        float lengthScale, widthScaleVal, leanFactor, droopFactor;
        if (bladeId < 2)
        {
            lengthScale = 1.08f + 0.14f * bHash2;
            widthScaleVal = 0.92f + 0.10f * bHash3;
            leanFactor = 0.44f + 0.10f * bHash;
            droopFactor = 0.08f + 0.04f * bHash3;
        }
        else if (bladeId < 4)
        {
            lengthScale = 0.96f + 0.14f * bHash2;
            widthScaleVal = 0.98f + 0.12f * bHash3;
            leanFactor = 0.52f + 0.12f * bHash;
            droopFactor = 0.10f + 0.05f * bHash3;
        }
        else
        {
            lengthScale = 0.86f + 0.12f * bHash2;
            widthScaleVal = 1.04f + 0.12f * bHash3;
            leanFactor = 0.58f + 0.12f * bHash;
            droopFactor = 0.12f + 0.05f * bHash3;
        }

        height = clump->height * lengthScale;
        width = clump->radius * style.bladeWidthScale * widthMultiplier * widthScaleVal;
        lean = height * leanFactor;
        droopY = height * droopFactor;
        float botanical = fminf(fmaxf(style.botanicalVariation, 0.0f), 1.0f);
        if (botanical > 0.0f)
        {
            float habit = 0.5f + 0.5f * (sinf(clump->position.x * 0.19f + clump->position.z * 0.11f) * 0.60f +
                                         sinf(clump->position.z * 0.16f - clump->position.x * 0.06f) * 0.40f);
            float stretch = 1.02f + 0.14f * habit;
            if (bHash2 < 0.07f)
                stretch *= 0.88f;
            else if (bHash2 < 0.14f)
                stretch *= 1.08f;
            stretch = 1.0f + (stretch - 1.0f) * botanical;
            height *= stretch;
            width /= sqrtf(stretch);
            lean *= stretch * (1.0f + botanical * (0.06f + 0.10f * habit));
            droopY = height * (droopFactor + botanical * 0.04f * habit);
        }

        float curlSign = ((bladeId & 1) == 0) ? 1.0f : -1.0f;
        float curlAmt = (bHash - 0.5f) * 0.12f + curlSign * 0.05f;
        float latX = -dirZ;
        float latZ = dirX;

        // Smooth monotonic cubic Bezier arc (Ghost of Tsushima katana-stroke posture):
        // y rises monotonically (0.0 -> 0.36 -> 0.71 -> 0.96 * height) while horizontal reach
        // accelerates (0.0 -> 0.18 -> 0.48 -> 0.92 * lean), eliminating perspective tip foldover.
        pBase = (Vector3){bx, clump->position.y - 0.014f, bz};
        pP1 = (Vector3){
            bx + dirX * (lean * 0.18f),
            pBase.y + height * 0.36f,
            bz + dirZ * (lean * 0.18f)};
        pP2 = (Vector3){
            bx + dirX * (lean * 0.48f) + latX * (lean * 0.04f * curlAmt),
            pBase.y + height * 0.71f,
            bz + dirZ * (lean * 0.48f) + latZ * (lean * 0.04f * curlAmt)};
        pP3 = (Vector3){
            bx + dirX * (lean * 0.92f) + latX * (lean * 0.08f * curlAmt),
            pBase.y + height * (0.96f - 0.04f * droopFactor),
            bz + dirZ * (lean * 0.92f) + latZ * (lean * 0.08f * curlAmt)};

        isSeedhead = false;
        float colorField = sinf(clump->position.x * 0.12f + clump->position.z * 0.08f) * 0.55f +
                           sinf(clump->position.z * 0.14f - clump->position.x * 0.06f + 1.2f) * 0.45f;
        float tone = 1.0f + colorField * 0.05f + (bHash - 0.5f) * 0.06f;
        // [TỐI ƯU VISUAL]: Nâng nhẹ độ sáng gốc cỏ để giảm độ tương phản gắt giữa chân bụi cỏ và mặt đất
        int rR = (int)(style.rootColor.r * tone * 1.06f + 4.0f);
        int rG = (int)(style.rootColor.g * tone * 1.06f + 6.0f);
        int rB = (int)(style.rootColor.b * tone * 1.04f + 3.0f);
        int tR = (int)(style.tipColor.r * tone * 0.96f);
        int tG = (int)(style.tipColor.g * tone * 1.02f);
        int tB = (int)(style.tipColor.b * tone * 1.00f);

        if (bHash2 < 0.12f && bladeId < 2)
        {
            tR = (int)(tR * 1.04f);
            tG = (int)(tG * 1.04f);
            tB = (int)(tB * 0.96f);
        }

        if (bladeId >= 4)
        {
            rR = (int)(rR * 0.95f);
            rG = (int)(rG * 0.96f);
            rB = (int)(rB * 0.95f);
            tR = (int)(tR * 0.92f);
            tG = (int)(tG * 0.95f);
            tB = (int)(tB * 0.92f);
        }
        bladeRoot = (Color){(unsigned char)fminf(255, fmaxf(0, rR)),
                            (unsigned char)fminf(255, fmaxf(0, rG)),
                            (unsigned char)fminf(255, fmaxf(0, rB)), 255};
        bladeTip = (Color){(unsigned char)fminf(255, fmaxf(0, tR)),
                           (unsigned char)fminf(255, fmaxf(0, tG)),
                           (unsigned char)fminf(255, fmaxf(0, tB)), 255};
    }
    if (!isReed && !isPampas && style.botanicalVariation > 0.0f)
    {
        Vector3 canopy = MeadowCanopyColor(MeadowHabitat(clump->position.x, clump->position.z));
        float variation = 0.96f + 0.08f * bHash2;
        Color canopyTip = {(unsigned char)(canopy.x * variation * 255.0f),
                           (unsigned char)(canopy.y * variation * 255.0f),
                           (unsigned char)(canopy.z * variation * 255.0f), 255};
        bladeTip = Nature_LerpColor(bladeTip, canopyTip,
                                    fminf(style.botanicalVariation, 1.0f) * 0.42f);
    }
    return (NatureBladeDescriptor){pBase, pP1, pP2, pP3, clump->position,
                                   bladeRoot, bladeTip, width, clump->phase, bladeLeanAngle, isReed, isSeedhead};
}

typedef struct
{
    float t;
    Vector3 center;
    Vector3 pL, pR;
    Vector3 nL, nR, nTip;
    Color color;
} NatureBladeKnot;

static Model Nature_BuildMeadowChunk(const MapMeadowPlacement *placements, int count,
                                     MapMeadowStyle style, float minX, float maxX,
                                     float minZ, float maxZ, int sampleStride,
                                     int bladesPerClump, int bladeSegments,
                                     float widthMultiplier, int *outPlacementCount)
{
    int selected = 0;
    int ordinal = 0;
    for (int i = 0; i < count; i++)
    {
        Vector3 p = placements[i].position;
        if (p.x < minX || p.x >= maxX || p.z < minZ || p.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) == 0)
            selected++;
    }
    if (outPlacementCount)
        *outPlacementCount = selected;
    if (selected <= 0)
        return (Model){0};

    int verticesPerBlade = (bladeSegments - 1) * 6 + 3;
    int extraPerClump = style.hasPlumes ? 18 : 0;
    int vertexCount = selected * (bladesPerClump * verticesPerBlade + extraPerClump);
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    ordinal = 0;

    const Vector3 terrainNormal = {0.0f, 1.0f, 0.0f};
    const float tipStart = (bladeSegments == 2) ? 0.54f : (bladeSegments > 2 ? 0.76f : 0.0f);
    const float invSegMinus1 = (bladeSegments > 1) ? (1.0f / (float)(bladeSegments - 1)) : 0.0f;

    for (int i = 0; i < count; i++)
    {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) != 0)
            continue;

        Vector3 clumpSphereCenter = {clump->position.x, clump->position.y - 0.04f, clump->position.z};

        for (int blade = 0; blade < bladesPerClump; blade++)
        {
            NatureBladeDescriptor desc = Nature_DescribeMeadowBlade(clump, i, blade,
                                                                    style, bladesPerClump, bladeSegments, widthMultiplier);
            bool isReed = desc.isReed;
            float width = desc.width, bladeLeanAngle = desc.leanAngle;
            Vector3 pBase = desc.p0, pP1 = desc.p1, pP2 = desc.p2, pP3 = desc.p3;
            Color bladeRoot = desc.rootColor, bladeTip = desc.tipColor;
            Vector3 baseSide = {-sinf(bladeLeanAngle), 0.0f, cosf(bladeLeanAngle)};

            // [TỐI ƯU HIỆU NĂNG]: Chỉ tính toán mặt cắt ngang tại (bladeSegments + 1) nút đúng 1 lần
            // thay vì tính lặp lại cả 2 đầu cho từng đốt!
            NatureBladeKnot knots[7];
            for (int k = 0; k <= bladeSegments; k++)
            {
                float tk = (k == bladeSegments) ? 1.0f : (tipStart * (float)k * invSegMinus1);
                knots[k].t = tk;
                Vector3 center = Nature_EvalCubicBezier(pBase, pP1, pP2, pP3, tk);
                knots[k].center = center;

                Vector3 T = Vector3Normalize(Nature_EvalCubicBezierTangent(pBase, pP1, pP2, pP3, tk));
                Vector3 crossS = Vector3CrossProduct(T, terrainNormal);
                float lenS = Vector3Length(crossS);
                Vector3 S = (lenS > 0.001f) ? Vector3Scale(crossS, 1.0f / lenS) : baseSide;

                Vector3 Ngeo = Vector3Normalize(Vector3CrossProduct(S, T));
                if (Ngeo.y < 0.0f)
                    Ngeo = Vector3Negate(Ngeo);
                Ngeo.y = fmaxf(Ngeo.y, 0.30f);
                Ngeo = Vector3Normalize(Ngeo);

                float occ = (tk < 0.25f) ? (0.90f + 0.40f * tk) : 1.0f;
                float sCurve = tk * tk * (3.0f - 2.0f * tk);
                knots[k].color = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, sCurve), occ);

                if (k == bladeSegments)
                {
                    // Nút đỉnh ngọn chỉ cần pháp tuyến nTip, tiết kiệm 6 lần chuẩn hóa Vector3Normalize
                    Vector3 dSphereTip = Vector3Normalize(Vector3Subtract(center, clumpSphereCenter));
                    Vector3 nTargetTip = Vector3Normalize(Vector3Lerp(dSphereTip, terrainNormal, 0.56f));
                    knots[k].nTip = Vector3Normalize(Vector3Lerp(Ngeo, nTargetTip, 0.60f));
                }
                else
                {
                    float uk = 1.0f - tk;
                    float profile;
                    if (isReed)
                    {
                        profile = (0.78f + 0.36f * sinf(PI * tk)) * (1.0f - powf(tk, 1.8f));
                    }
                    else
                    {
                        float sheath = fminf(1.0f, 0.65f + 2.5f * tk);
                        float body = 0.82f + 0.32f * sinf(PI * fminf(tk * 1.35f, 1.0f));
                        float tipT = fmaxf(0.0f, (tk - 0.35f) * (1.0f / 0.65f));
                        float taper = fmaxf(0.0f, 1.0f - tipT * tipT);
                        profile = sheath * body * taper;
                    }
                    float halfW = (width * 0.5f) * profile;
                    Vector3 fold = Vector3Scale(Ngeo, width * 0.055f * (4.0f * tk * uk));

                    Vector3 pL = Vector3Add(Vector3Subtract(center, Vector3Scale(S, halfW)), fold);
                    Vector3 pR = Vector3Add(Vector3Add(center, Vector3Scale(S, halfW)), fold);
                    knots[k].pL = pL;
                    knots[k].pR = pR;

                    Vector3 dSphereL = Vector3Normalize(Vector3Subtract(pL, clumpSphereCenter));
                    Vector3 dSphereR = Vector3Normalize(Vector3Subtract(pR, clumpSphereCenter));

                    // [TỐI ƯU VISUAL]: Neo pháp tuyến gốc cỏ mạnh hơn về phía pháp tuyến mặt đất (0,1,0)
                    // để chân cỏ bắt sáng tiệp màu với địa hình, và giảm độ gãy ngang mặt lá (0.32 -> 0.18).
                    float groundAnchor = fmaxf(0.0f, 1.0f - tk * 2.5f);
                    float terrainBlend = 0.52f + 0.34f * groundAnchor;
                    Vector3 nTargetL = Vector3Normalize(Vector3Lerp(dSphereL, terrainNormal, terrainBlend));
                    Vector3 nTargetR = Vector3Normalize(Vector3Lerp(dSphereR, terrainNormal, terrainBlend));

                    float edgeSpread = 0.18f * (1.0f - 0.35f * groundAnchor);
                    Vector3 nMeshL = Vector3Normalize(Vector3Subtract(Ngeo, Vector3Scale(S, edgeSpread)));
                    Vector3 nMeshR = Vector3Normalize(Vector3Add(Ngeo, Vector3Scale(S, edgeSpread)));

                    float normalLerp = 0.58f + 0.24f * groundAnchor;
                    knots[k].nL = Vector3Normalize(Vector3Lerp(nMeshL, nTargetL, normalLerp));
                    knots[k].nR = Vector3Normalize(Vector3Lerp(nMeshR, nTargetR, normalLerp));
                }
            }

            for (int segment = 0; segment < bladeSegments; segment++)
            {
                const NatureBladeKnot *k0 = &knots[segment];
                const NatureBladeKnot *k1 = &knots[segment + 1];

                if (segment == bladeSegments - 1)
                {
                    if (style.hasPlumes)
                    {
                        Nature_SetVertex(&mesh, cursor++, k0->pL, k0->nL, clump->phase, k0->t, k0->color);
                        Nature_SetVertex(&mesh, cursor++, k0->pR, k0->nR, clump->phase, k0->t, k0->color);
                        Nature_SetVertex(&mesh, cursor++, k1->center, k1->nTip, clump->phase, 1.0f, k1->color);
                    }
                    else
                    {
                        Nature_AddCurvedBladeTip(&mesh, &cursor, k0->pL, k0->pR, k1->center,
                                                 k0->nL, k0->nR, k1->nTip,
                                                 clump->phase, k0->t, k0->color, k1->color);
                    }
                }
                else
                {
                    Nature_AddCurvedBladeQuad(&mesh, &cursor, k0->pL, k0->pR, k1->pR, k1->pL,
                                              k0->nL, k0->nR, k1->nR, k1->nL,
                                              clump->phase, k0->t, k1->t, k0->color, k1->color);
                }
            }
        }

        if (style.hasPlumes)
        {
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

            float tKnots[4] = {0.65f, 0.78f, 0.90f, 1.00f};
            Vector3 plPts[4];
            for (int k = 0; k < 4; k++)
            {
                float tk = tKnots[k];
                float omtk = 1.0f - tk;
                plPts[k] = (Vector3){
                    omtk * omtk * caneP0.x + 2 * omtk * tk * caneP1.x + tk * tk * caneP2.x,
                    omtk * omtk * caneP0.y + 2 * omtk * tk * caneP1.y + tk * tk * caneP2.y,
                    omtk * omtk * caneP0.z + 2 * omtk * tk * caneP1.z + tk * tk * caneP2.z};
            }

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

            Color c0 = {240, 234, 212, 255};
            Color c1 = {250, 248, 236, 255};

            if (style.texturePath != NULL)
            {
                Nature_AddTexturedQuadUV(&mesh, &cursor, q0, q1, q2, q3, norm,
                                         clump->phase, 0.52f, 1.0f,
                                         c0, c1, 0.0f, 0.0f, 1.0f, 1.0f);
            }
            else
            {
                Nature_AddQuad(&mesh, &cursor, q0, q1, q2, q3, norm,
                               clump->phase, 0.52f, 1.0f, c0, c1);
            }

            float tL0 = 0.32f, tL1 = 0.52f;
            float omtL0 = 1.0f - tL0, omtL1 = 1.0f - tL1;
            Vector3 leafRoot0 = {
                omtL0 * omtL0 * caneP0.x + 2 * omtL0 * tL0 * caneP1.x + tL0 * tL0 * caneP2.x,
                omtL0 * omtL0 * caneP0.y + 2 * omtL0 * tL0 * caneP1.y + tL0 * tL0 * caneP2.y,
                omtL0 * omtL0 * caneP0.z + 2 * omtL0 * tL0 * caneP1.z + tL0 * tL0 * caneP2.z};
            Vector3 leafRoot1 = {
                omtL1 * omtL1 * caneP0.x + 2 * omtL1 * tL1 * caneP1.x + tL1 * tL1 * caneP2.x,
                omtL1 * omtL1 * caneP0.y + 2 * omtL1 * tL1 * caneP1.y + tL1 * tL1 * caneP2.y,
                omtL1 * omtL1 * caneP0.z + 2 * omtL1 * tL1 * caneP1.z + tL1 * tL1 * caneP2.z};
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
    if (!style.hasPlumes && bladeSegments > 1)
        (void)Nature_IndexBladeMesh(&mesh);
    return Nature_ModelFromMesh(mesh, Nature_GetShader(style.texturePath != NULL));
}

#include "maps/toolkit/map_props_meadow_parametric.inl"

MapMeadowSurface MapProp_CreateMeadow(const MapMeadowPlacement *placements, int count,
                                      MapMeadowStyle style)
{
    MapMeadowSurface meadow = {0};
    if (!placements || count <= 0)
        return meadow;
    if (style.bladesPerClump < 1)
        style.bladesPerClump = 1;
    if (style.bladesPerClump > 10)
        style.bladesPerClump = 10;
    if (style.bladeSegments < 1)
        style.bladeSegments = 1;
    if (style.bladeSegments > 6)
        style.bladeSegments = 6;
    if (style.bladeWidthScale <= 0.0f)
        style.bladeWidthScale = 0.24f;
    if (style.chunkSize <= 0.0f)
        style.chunkSize = 12.0f;
    if (style.alphaCutoff <= 0.0f)
        style.alphaCutoff = 0.42f;
    if (style.alphaCutoff > 0.9f)
        style.alphaCutoff = 0.9f;
    if (style.grassState != MAP_GRASS_STATE_LUSH)
    {
        MapProp_SetGrassState(style.grassState, 0.85f);
    }
    if (style.dryness > 0.0f || style.scorch > 0.0f)
    {
        MapGrassCondition cond = MapProp_GetGrassCondition();
        if (style.dryness > 0.0f)
            cond.dryness = style.dryness;
        if (style.scorch > 0.0f)
        {
            cond.scorch = style.scorch;
            if (cond.emberIntensity <= 0.0f)
                cond.emberIntensity = 0.85f;
        }
        MapProp_SetGrassCondition(cond);
    }
    if (NatureParametric_Create(&meadow, placements, count, style))
        return meadow;
    if (style.shadowDistance > 0.0f)
        (void)NatureShadow_GetShader();
    Texture2D foliageTexture = {0};
    bool textured = style.texturePath != NULL;
    if (textured)
    {
        foliageTexture = ResourceManager_LoadTexture(style.texturePath);
        GenTextureMipmaps(&foliageTexture);
        SetTextureFilter(foliageTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(foliageTexture, TEXTURE_WRAP_CLAMP);
    }

    float minX = placements[0].position.x;
    float maxX = minX;
    float minZ = placements[0].position.z;
    float maxZ = minZ;
    for (int i = 1; i < count; i++)
    {
        if (placements[i].position.x < minX)
            minX = placements[i].position.x;
        if (placements[i].position.x > maxX)
            maxX = placements[i].position.x;
        if (placements[i].position.z < minZ)
            minZ = placements[i].position.z;
        if (placements[i].position.z > maxZ)
            maxZ = placements[i].position.z;
    }
    maxX += 0.001f;
    maxZ += 0.001f;
    int columns = (int)ceilf((maxX - minX) / style.chunkSize);
    int rows = (int)ceilf((maxZ - minZ) / style.chunkSize);
    if (columns < 1)
        columns = 1;
    if (rows < 1)
        rows = 1;
    int capacity = columns * rows;
    meadow.chunks = MemAlloc((unsigned int)capacity * sizeof(MapMeadowChunk));
    meadow.lodDistance = style.lodDistance;
    meadow.midLodDistance = style.midLodDistance > 0.0f ? style.midLodDistance
                                                        : (style.lodDistance > 0.0f ? style.lodDistance * 0.48f : 0.0f);
    meadow.drawDistance = style.drawDistance;
    meadow.shadowDistance = style.shadowDistance;
    bool buildRealShadowLod = style.shadowDistance > 0.0f;

    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
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

            int midBlades = style.bladesPerClump >= 5 ? 4
                                                      : (style.bladesPerClump >= 3 ? 3 : style.bladesPerClump);
            int midSegments = (style.bladeSegments >= 3) ? 2 : 1;
            int midCount = 0;
            Model midModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                midBlades, midSegments, 1.22f, &midCount);

            int farBlades = 3;
            int farCount = 0;
            Model farModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                farBlades, 1, 1.65f, &farCount);
            Model shadowModel = {0};
            Model realShadowModel = {0};
            int realShadowCount = 0;
            if (buildRealShadowLod)
                realShadowModel = Nature_BuildMeadowChunk(
                    placements, count, style, x0, x1, z0, z1, 1,
                    3, 2, 1.30f, &realShadowCount);
            MapMeadowChunk *chunk = &meadow.chunks[meadow.chunkCount++];
            if (textured)
            {
                nearModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                if (midCount > 0)
                    midModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                if (realShadowCount > 0)
                    realShadowModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
            }
            *chunk = (MapMeadowChunk){
                .nearModel = nearModel,
                .midModel = midModel,
                .farModel = farModel,
                .shadowModel = shadowModel,
                .realShadowModel = realShadowModel,
                .center = {(x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f},
                .radius = style.chunkSize * 0.72f + 1.5f,
                .lodLevel = 0,
                .farLod = false,
                .midReady = (midCount > 0),
                .shadowReady = shadowModel.meshCount > 0,
                .realShadowReady = realShadowCount > 0,
                .ready = true,
            };
        }
    }
    if (meadow.chunkCount <= 0)
    {
        MemFree(meadow.chunks);
        meadow.chunks = NULL;
        return meadow;
    }
    meadow.ready = true;
    meadow.textured = textured;
    meadow.alphaCutoff = style.alphaCutoff;
    long long rawVertices = 0, storedVertices = 0, indexBytes = 0;
    for (int i = 0; i < meadow.chunkCount; i++)
    {
        Model models[4] = {meadow.chunks[i].nearModel, meadow.chunks[i].midModel,
                           meadow.chunks[i].farModel, meadow.chunks[i].realShadowModel};
        for (int lod = 0; lod < 4; lod++)
        {
            for (int j = 0; j < models[lod].meshCount; j++)
            {
                const Mesh *mesh = &models[lod].meshes[j];
                rawVertices += (long long)mesh->triangleCount * 3;
                storedVertices += mesh->vertexCount;
                if (mesh->indices)
                    indexBytes += (long long)mesh->triangleCount * 3 * sizeof(unsigned short);
            }
        }
    }
    TraceLog(LOG_INFO, "MEADOW_MESH: chunks=%d raw_vertices=%lld stored_vertices=%lld vertex_bytes=%lld index_bytes=%lld",
             meadow.chunkCount, rawVertices, storedVertices,
             storedVertices * (10 * sizeof(float) + 4), indexBytes);
    return meadow;
}

// ============================================================================
// CACHED CAMERA FRUSTUM (Loại bỏ 3x sqrtf, 1x tanf và 2x GetScreenWidth/Height mỗi chunk)
// ============================================================================
typedef struct
{
    Vector3 pos;
    Vector3 target;
    Vector3 up;
    float fovy;
    int projection;
    Vector3 forward;
    Vector3 right;
    Vector3 viewUp;
    float tanHalfFov;
    float aspect;
    float orthoHalfV;
    float orthoHalfH;
    bool degenerate;
    bool valid;
} NatureFrustumCache;

static NatureFrustumCache s_natureFrustumCache = {0};

static inline void Nature_RefreshFrustumCache(void)
{
    if (s_natureFrustumCache.valid &&
        s_natureFrustumCache.pos.x == camera.position.x &&
        s_natureFrustumCache.pos.y == camera.position.y &&
        s_natureFrustumCache.pos.z == camera.position.z &&
        s_natureFrustumCache.target.x == camera.target.x &&
        s_natureFrustumCache.target.y == camera.target.y &&
        s_natureFrustumCache.target.z == camera.target.z &&
        s_natureFrustumCache.up.x == camera.up.x &&
        s_natureFrustumCache.up.y == camera.up.y &&
        s_natureFrustumCache.up.z == camera.up.z &&
        s_natureFrustumCache.fovy == camera.fovy &&
        s_natureFrustumCache.projection == camera.projection)
    {
        return;
    }

    s_natureFrustumCache.pos = camera.position;
    s_natureFrustumCache.target = camera.target;
    s_natureFrustumCache.up = camera.up;
    s_natureFrustumCache.fovy = camera.fovy;
    s_natureFrustumCache.projection = camera.projection;
    s_natureFrustumCache.valid = true;

    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    float forwardLength = Vector3Length(forward);
    if (forwardLength < 0.001f)
    {
        s_natureFrustumCache.degenerate = true;
        return;
    }
    forward = Vector3Scale(forward, 1.0f / forwardLength);

    Vector3 right = Vector3CrossProduct(forward, camera.up);
    float rightLength = Vector3Length(right);
    if (rightLength < 0.001f)
    {
        s_natureFrustumCache.degenerate = true;
        return;
    }
    right = Vector3Scale(right, 1.0f / rightLength);
    Vector3 viewUp = Vector3Normalize(Vector3CrossProduct(right, forward));

    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    s_natureFrustumCache.forward = forward;
    s_natureFrustumCache.right = right;
    s_natureFrustumCache.viewUp = viewUp;
    s_natureFrustumCache.aspect = aspect;
    s_natureFrustumCache.tanHalfFov = tanf(camera.fovy * DEG2RAD * 0.5f);
    s_natureFrustumCache.orthoHalfV = camera.fovy * 0.5f;
    s_natureFrustumCache.orthoHalfH = s_natureFrustumCache.orthoHalfV * aspect;
    s_natureFrustumCache.degenerate = false;
}

static bool Nature_IsChunkVisible(Vector3 center, float radius)
{
    Nature_RefreshFrustumCache();
    if (s_natureFrustumCache.degenerate)
        return true;

    Vector3 toCenter = {
        center.x - s_natureFrustumCache.pos.x,
        center.y - s_natureFrustumCache.pos.y,
        center.z - s_natureFrustumCache.pos.z,
    };
    float depth = toCenter.x * s_natureFrustumCache.forward.x +
                  toCenter.y * s_natureFrustumCache.forward.y +
                  toCenter.z * s_natureFrustumCache.forward.z;
    if (depth < -radius)
        return false;

    float halfVertical;
    float halfHorizontal;
    if (s_natureFrustumCache.projection == CAMERA_ORTHOGRAPHIC)
    {
        halfVertical = s_natureFrustumCache.orthoHalfV;
        halfHorizontal = s_natureFrustumCache.orthoHalfH;
    }
    else
    {
        float positiveDepth = depth > 0.0f ? depth : 0.0f;
        halfVertical = s_natureFrustumCache.tanHalfFov * positiveDepth;
        halfHorizontal = halfVertical * s_natureFrustumCache.aspect;
    }

    float sideDistance = fabsf(toCenter.x * s_natureFrustumCache.right.x +
                               toCenter.y * s_natureFrustumCache.right.y +
                               toCenter.z * s_natureFrustumCache.right.z);
    if (sideDistance > halfHorizontal + radius)
        return false;

    float verticalDistance = fabsf(toCenter.x * s_natureFrustumCache.viewUp.x +
                                   toCenter.y * s_natureFrustumCache.viewUp.y +
                                   toCenter.z * s_natureFrustumCache.viewUp.z);
    return verticalDistance <= halfVertical + radius;
}

static bool Nature_IntersectsDynamicShadowCoverage(Vector3 center, float radius)
{
    Vector3 focus = EnvShadow_GetFocus();
    Vector3 sun = Vector3Normalize(Environment_GetSunDirection());
    float extent = fmaxf(EnvShadow_GetHalfExtent(), 8.0f);
    float groundRadius = extent / fmaxf(fabsf(sun.y), 0.32f);
    float dx = center.x - focus.x;
    float dz = center.z - focus.z;
    float limit = groundRadius + radius * 1.12f + 2.0f;
    return dx * dx + dz * dz <= limit * limit;
}

void MapProp_PrepareMeadow(MapMeadowSurface *meadow, Vector3 worldOffset)
{
    if (!meadow || !meadow->ready || !meadow->parametric)
        return;
    NatureParametricMeadow *data = meadow->parametric;
    if (!data->compact && !data->nearFirst)
        return;
    NatureMeadowView view = NatureParametric_View(meadow);
    if (NatureParametric_IsPrepared(meadow, worldOffset, view.bands))
        return;
    Nature_RefreshFrustumCache();
    float drawDistanceSq = view.drawDistance * view.drawDistance;
    for (int i = 0; i < meadow->chunkCount; i++)
    {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        float dx = camera.position.x - center.x, dz = camera.position.z - center.z;
        if (view.drawDistance > 0.0f && dx * dx + dz * dz > drawDistanceSq)
        {
            chunk->visibleThisFrame = false;
            continue;
        }
        chunk->visibleThisFrame = Nature_IsChunkVisible(center, chunk->radius);
    }
    NatureParametric_PrepareVisible(meadow, worldOffset, view.bands);
}

void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready)
        return;
    GfxQuality quality = GfxQuality_Get();
    NatureMeadowView view = NatureParametric_View(meadow);
    float lodScale = view.lodScale, zoomFactor = view.zoomFactor;
    float lodDistance = view.lodDistance, drawDistance = view.drawDistance;
    float drawDistanceSq = drawDistance * drawDistance;

    Nature_RefreshFrustumCache();

    float fdx = camera.position.x - camera.target.x;
    float fdz = camera.position.z - camera.target.z;
    float focalDist = (!meadow->parametric && lodDistance > 0.0f) ? sqrtf(fdx * fdx + fdz * fdz) : 0.0f;
    bool hasAnyContactShadow = false;

    for (int i = 0; i < meadow->chunkCount; i++)
    {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        chunk->visibleThisFrame = false;
        s_natureRenderStats.meadowChunksTested++;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        if (!Nature_IsChunkVisible(center, chunk->radius))
        {
            s_natureRenderStats.meadowFrustumCulled++;
            continue;
        }
        float dx = camera.position.x - center.x;
        float dz = camera.position.z - center.z;
        float distanceSq = dx * dx + dz * dz;
        if (drawDistance > 0.0f && distanceSq > drawDistanceSq)
        {
            s_natureRenderStats.meadowDistanceCulled++;
            continue;
        }
        s_natureRenderStats.meadowChunksVisible++;
        chunk->visibleThisFrame = true;
        if (chunk->shadowReady)
            hasAnyContactShadow = true;

        if (!meadow->parametric)
        {
            if (lodDistance > 0.0f)
            {
                // Hash nhanh không cần sinf() đắt đỏ mỗi frame
                float hSeed = chunk->center.x * 0.1031f + chunk->center.z * 0.3183f;
                float spatialHash = hSeed - floorf(hSeed);
                float farThreshold = focalDist + lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 4.0f;
                float midThreshold = (meadow->midLodDistance > 0.0f && chunk->midReady)
                                         ? (focalDist + meadow->midLodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 2.5f)
                                         : 0.0f;
                float hysteresis = quality >= GFX_HIGH ? 1.1f : 1.8f;
                float dy = camera.position.y - center.y;
                float distance = sqrtf(distanceSq + dy * dy);

                if (midThreshold > 0.0f)
                {
                    if (chunk->lodLevel == 2)
                    {
                        if (distance < farThreshold - hysteresis)
                            chunk->lodLevel = (distance < midThreshold - hysteresis) ? 0 : 1;
                    }
                    else if (chunk->lodLevel == 1)
                    {
                        if (distance > farThreshold + hysteresis)
                            chunk->lodLevel = 2;
                        else if (distance < midThreshold - hysteresis)
                            chunk->lodLevel = 0;
                    }
                    else
                    {
                        if (distance > farThreshold + hysteresis)
                            chunk->lodLevel = 2;
                        else if (distance > midThreshold + hysteresis)
                            chunk->lodLevel = 1;
                    }
                }
                else
                {
                    if (chunk->lodLevel == 2)
                    {
                        if (distance < farThreshold - hysteresis)
                            chunk->lodLevel = 0;
                    }
                    else if (distance > farThreshold + hysteresis)
                    {
                        chunk->lodLevel = 2;
                    }
                }
                chunk->farLod = (chunk->lodLevel == 2);
            }
            else
            {
                chunk->lodLevel = 0;
                chunk->farLod = false;
            }
        }
    }

    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_LOW && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = hasAnyContactShadow && quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    if (useProjectedShadows && meadow->shadowDistance > 0.0f)
    {
        float shadowScale = quality >= GFX_HIGH ? 1.0f : 0.76f;
        float shadowDistance = meadow->shadowDistance * shadowScale;
        Shader shadowShader = FlowerShadow_GetShader();
        BeginShaderMode(shadowShader);
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        EndShaderMode();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        for (int i = 0; i < meadow->chunkCount; i++)
        {
            MapMeadowChunk *chunk = &meadow->chunks[i];
            if (!chunk->visibleThisFrame || !chunk->shadowReady)
                continue;
            Vector3 center = Vector3Add(chunk->center, worldOffset);
            float dx = camera.position.x - center.x;
            float dz = camera.position.z - center.z;
            float maxShadowDist = shadowDistance + chunk->radius;
            if (dx * dx + dz * dz > maxShadowDist * maxShadowDist)
            {
                s_natureRenderStats.meadowShadowDistanceCulled++;
                continue;
            }
            DrawModel(chunk->shadowModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowShadowDraws++;
        }
        EndBlendMode();
        rlEnableDepthMask();
    }

    Vector4 tuftBands = view.bands;
    bool compactNow = false;
    {
        NatureParametricMeadow *parametricData = meadow->parametric;
        compactNow = parametricData && parametricData->compact &&
                     NatureParametric_IsPrepared(meadow, worldOffset, tuftBands);
    }
    Shader shader = meadow->parametric ? NatureParametric_Shader(false)
                                       : Nature_GetShader(meadow->textured);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        meadow->textured, meadow->alphaCutoff,
                        NATURE_WIND_RESPONSE_GRASS);
    if (meadow->parametric)
    {
        NatureParametric_BindReceivers(shader, false);
        SetShaderValue(shader, s_natureTuftLodBandsLoc, &tuftBands, SHADER_UNIFORM_VEC4);
        SetShaderValue(shader, s_natureTuftLodCameraLoc, &camera.position, SHADER_UNIFORM_VEC3);
        // [TỐI ƯU VISUAL]: Nới rộng vùng chuyển tiếp mờ dần ở xa (0.76 -> 1.00) để cỏ trên
        // sườn đồi xa hòa vào địa hình tự nhiên hơn.
        Vector2 fadeRange = {view.drawDistance * 0.76f, view.drawDistance};
        SetShaderValue(shader, s_natureTuftFadeRangeLoc, &fadeRange, SHADER_UNIFORM_VEC2);
    }
    if (!meadow->textured)
    {
        float tipSoftening = 1.0f;
        const NatureShaderLocs *locs = Nature_GetCachedLocs(shader);
        if (locs->grassTipSoftening >= 0)
            SetShaderValue(shader, locs->grassTipSoftening, &tipSoftening, SHADER_UNIFORM_FLOAT);
    }
    rlDisableBackfaceCulling();
    NatureParametricMeadow *ordered = meadow->parametric;
    int drawCount = 0;
    int localOrder[256];
    float localDepth[256];
    int *drawOrder = localOrder;
    float *drawDepth = localDepth;
    bool hasPrecomputedDepth = false;

    if (ordered && ordered->nearFirst)
    {
        drawCount = ordered->drawCount;
        drawOrder = ordered->drawOrder;
    }
    else
    {
        hasPrecomputedDepth = true;
        if (meadow->chunkCount > 256)
        {
            drawOrder = (int *)MemAlloc(meadow->chunkCount * sizeof(int));
            drawDepth = (float *)MemAlloc(meadow->chunkCount * sizeof(float));
        }
        for (int c = 0; c < meadow->chunkCount; c++)
        {
            if (!meadow->chunks[c].visibleThisFrame)
                continue;
            Vector3 center = Vector3Add(meadow->chunks[c].center, worldOffset);
            float dx = camera.position.x - center.x;
            float dy = camera.position.y - center.y;
            float dz = camera.position.z - center.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            int slot = drawCount++;
            while (slot > 0 && drawDepth[slot - 1] > distSq)
            {
                drawOrder[slot] = drawOrder[slot - 1];
                drawDepth[slot] = drawDepth[slot - 1];
                slot--;
            }
            drawOrder[slot] = c;
            drawDepth[slot] = distSq;
        }
    }

    bool bandBatched = ordered && compactNow && ordered->canonical;
    if (bandBatched)
    {
        for (int lod = 0; lod < 3; lod++)
        {
            if (ordered->bandCount[lod] <= 0)
                continue;
            NatureParametric_DrawBand(meadow, lod, shader, worldOffset);
            if (lod == 2)
                s_natureRenderStats.meadowFarDraws++;
            else if (lod == 1)
                s_natureRenderStats.meadowMidDraws++;
            else
                s_natureRenderStats.meadowNearDraws++;
        }
        drawCount = 0;
    }

    for (int draw = 0; draw < drawCount; draw++)
    {
        int i = drawOrder[draw];
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->visibleThisFrame)
            continue;
        if (meadow->parametric)
        {
            float d;
            if (hasPrecomputedDepth)
            {
                d = sqrtf(drawDepth[draw]);
            }
            else
            {
                Vector3 center = Vector3Add(chunk->center, worldOffset);
                d = Vector3Distance(camera.position, center);
            }
            float minDistance = fmaxf(0.0f, d - chunk->radius);
            float maxDistance = d + chunk->radius;
            for (int lod = 0; lod < 3; lod++)
            {
                if (!NatureParametric_LodIntersectsSphere(lod, minDistance, maxDistance, tuftBands))
                    continue;
                NatureParametricMeadow *data = meadow->parametric;
                if (compactNow && data->ranges[i].visibleCount[lod] == 0)
                    continue;
                NatureParametric_DrawChunk(meadow, i, lod, shader, worldOffset, compactNow);
                if (lod == 2)
                    s_natureRenderStats.meadowFarDraws++;
                else if (lod == 1)
                    s_natureRenderStats.meadowMidDraws++;
                else
                    s_natureRenderStats.meadowNearDraws++;
            }
            continue;
        }
        if (chunk->lodLevel == 2)
        {
            DrawModel(chunk->farModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowFarDraws++;
        }
        else if (chunk->lodLevel == 1 && chunk->midReady)
        {
            DrawModel(chunk->midModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowMidDraws++;
        }
        else
        {
            DrawModel(chunk->nearModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowNearDraws++;
        }
    }

    if ((!ordered || !ordered->nearFirst) && drawOrder != localOrder)
    {
        MemFree(drawOrder);
        MemFree(drawDepth);
    }
    if (meadow->parametric)
        NatureParametric_EndReceivers();
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength)
{
    GfxQuality quality = GfxQuality_Get();
    if (!meadow || !meadow->ready || quality < GFX_MED ||
        meadow->shadowDistance <= 0.0f ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(false))
        return;

    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    if (fabsf(sunDir.y) > 0.98f)
        return;

    Shader shader = meadow->parametric ? NatureParametric_Shader(true) : NatureShadow_GetShader();
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              meadow->textured, meadow->alphaCutoff,
                              NATURE_WIND_RESPONSE_GRASS);
    if (meadow->parametric)
        NatureParametric_BindReceivers(shader, true);
    rlDisableBackfaceCulling();

    // [TỐI ƯU HIỆU NĂNG]: Đưa phép tính bán kính vùng phủ bóng ra ngoài vòng lặp chunk
    Vector3 shadowFocus = EnvShadow_GetFocus();
    float halfExtent = EnvShadow_GetHalfExtent();
    float groundRadius = fmaxf(halfExtent, 8.0f) / fmaxf(fabsf(sunDir.y), 0.32f);
    float qualityDistScale = (quality >= GFX_HIGH) ? 1.0f : 0.80f;
    float maxDist = fminf(meadow->shadowDistance * qualityDistScale + 4.0f, halfExtent + 4.0f);
    bool filterActive = Nature_ShadowCasterFilterActive();

    for (int i = 0; i < meadow->chunkCount; i++)
    {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->realShadowReady)
            continue;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        float dx = center.x - shadowFocus.x;
        float dz = center.z - shadowFocus.z;
        float distSq = dx * dx + dz * dz;
        float limit = fminf(maxDist + chunk->radius * 0.35f, halfExtent + 2.0f);
        if (distSq > limit * limit)
            continue;
        if (!Nature_IsChunkVisible(center, chunk->radius + 3.5f))
            continue;
        if (!filterActive)
        {
            float covLimit = groundRadius + chunk->radius * 1.12f + 2.0f;
            if (distSq > covLimit * covLimit)
                continue;
        }
        if (meadow->parametric)
        {
            NatureParametric_DrawChunk(meadow, i, 3, shader, worldOffset, false);
            s_natureRenderStats.meadowShadowDraws++;
            continue;
        }
        Shader previous = chunk->realShadowModel.materials[0].shader;
        chunk->realShadowModel.materials[0].shader = shader;
        DrawModel(chunk->realShadowModel, worldOffset, 1.0f, WHITE);
        chunk->realShadowModel.materials[0].shader = previous;
    }
    if (meadow->parametric)
        NatureParametric_EndReceivers();
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready)
        return;
    if (meadow->parametric)
    {
        NatureParametric_Destroy(meadow);
        MemFree(meadow->chunks);
        memset(meadow, 0, sizeof(*meadow));
        return;
    }
    for (int i = 0; i < meadow->chunkCount; i++)
    {
        UnloadModel(meadow->chunks[i].nearModel);
        if (meadow->chunks[i].midReady)
            UnloadModel(meadow->chunks[i].midModel);
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
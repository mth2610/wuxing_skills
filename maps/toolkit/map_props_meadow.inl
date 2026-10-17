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
        float width = fmaxf(clump->radius * 0.72f, 0.038f);
        Vector3 encoded = {clump->height * 1.05f, width, clump->phase};
        Color rootShade = {240, 240, 240, 255};
        Color tipShade = {170, 170, 170, 255};
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

#include "maps/toolkit/meadow_palette.h"

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData)
{
    if (!outPlacements || maxCount <= 0 || distribution.spacing <= 0.0f)
        return 0;
    if (distribution.jitter < 0.0f) distribution.jitter = 0.0f;
    if (distribution.jitter > 0.95f) distribution.jitter = 0.95f;

    float rowSpacing = distribution.spacing * 0.8660254f; // Equilateral hexagonal honeycomb row step
    int columns = (int)ceilf((distribution.maxBounds.x - distribution.minBounds.x) /
                             distribution.spacing);
    int rows = (int)ceilf((distribution.maxBounds.y - distribution.minBounds.y) /
                          rowSpacing);
    unsigned int rng = distribution.seed ? distribution.seed : 1u;
    int count = 0;
    unsigned int accepted = 0;
    unsigned int selectionRng = rng ^ 0xa511e9b3u;
    for (int row = 0; row < rows; row++) {
        float rowOffset = (row & 1) ? (distribution.spacing * 0.5f) : 0.0f;
        for (int column = 0; column < columns; column++) {
            float jx = (Nature_Random01(&rng) - 0.5f) * distribution.spacing * distribution.jitter;
            float jz = (Nature_Random01(&rng) - 0.5f) * rowSpacing * distribution.jitter;
            float x = distribution.minBounds.x + column * distribution.spacing + rowOffset + jx;
            float z = distribution.minBounds.y + (row + 0.5f) * rowSpacing + jz;
            float density = densityFn ? densityFn(x, z, userData) : 1.0f;
            if (density < 0.0f) density = 0.0f;
            if (density > 1.0f) density = 1.0f;
            if (Nature_Random01(&rng) > density)
                continue;

            float y = groundCenter.y;
            if (ground && ground->ready)
                y = MapProp_SampleGroundHeight(ground, groundCenter, x, z);
            MapMeadowPlacement candidate;
            MapMeadowPlacement *placement = &candidate;
            placement->position = (Vector3){x, y + distribution.yOffset, z};
            placement->radius = distribution.minRadius
                + (distribution.maxRadius - distribution.minRadius) * Nature_Random01(&rng);
            placement->height = distribution.minHeight
                + (distribution.maxHeight - distribution.minHeight) * Nature_Random01(&rng);
            placement->rotationDeg = Nature_Random01(&rng) * 360.0f;
            placement->phase = Nature_Random01(&rng);
            // Reservoir selection covers the whole map when the budget fills.
            // Keep a separate RNG so changing the budget cannot change habitat.
            accepted++;
            if (count < maxCount) outPlacements[count++] = candidate;
            else {
                unsigned int slot = (unsigned int)((double)Nature_NextRandom(&selectionRng) *
                                                   (double)accepted / 4294967296.0);
                if (slot < (unsigned int)maxCount) outPlacements[slot] = candidate;
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

typedef struct {
    Vector3 p0, p1, p2, p3, clump;
    Color rootColor, tipColor;
    float width, phase, leanAngle;
    bool isReed;
} NatureBladeDescriptor;

// Single authoring implementation for expanded meshes and compact templates.
static NatureBladeDescriptor Nature_DescribeMeadowBlade(const MapMeadowPlacement *clump,
    int i, int blade, MapMeadowStyle style, int bladesPerClump, int bladeSegments,
    float widthMultiplier)
{
    const float golden = 2.39996323f;
    bool isReed = style.growthForm == MAP_MEADOW_GROWTH_REED ||
        (style.growthForm == MAP_MEADOW_GROWTH_AUTO && clump->height > 0.95f);
    float clumpAngle = clump->rotationDeg * DEG2RAD;

    // Stable blade identities across LODs: thinning retains the same
    // botanical samples instead of rebuilding a different fan.
    int bladeId = bladesPerClump > 1 && bladesPerClump < style.bladesPerClump
        ? blade * (style.bladesPerClump - 1) / (bladesPerClump - 1) : blade;
    unsigned int bladeSeed = (unsigned int)i * 747796405u
                          + (unsigned int)bladeId * 2891336453u + 277803737u;
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

    if (isReed) {
        float baseAngle = (float)blade * (2.0f * PI / (float)bladesPerClump);
        float bladeAzimuth = baseAngle + (bHash - 0.5f) * 0.95f;
        float radX = cosf(bladeAzimuth);
        float radZ = sinf(bladeAzimuth);

        float windWeight = 0.65f;
        float radWeight = 1.0f - windWeight;
        float combX = radX * radWeight + flowX * windWeight;
        float combZ = radZ * radWeight + flowZ * windWeight;
        float combLen = sqrtf(combX * combX + combZ * combZ);
        if (combLen < 0.01f) { combX = flowX; combZ = flowZ; combLen = 1.0f; }
        bladeLeanAngle = atan2f(combZ / combLen, combX / combLen);

        float collarRadius = clump->radius * 0.20f * (0.75f + 0.50f * bHash3);
        float bx = clump->position.x + radX * collarRadius;
        float bz = clump->position.z + radZ * collarRadius;

        float tierFrac = (float)blade / (float)bladesPerClump;
        float lengthScale = 0.85f + 0.30f * sinf((float)(i * 13 + blade * 7));
        height = clump->height * lengthScale;
        width = clump->radius * style.bladeWidthScale * widthMultiplier * (0.82f + 0.36f * bHash2);
        lean = height * (0.35f + 0.20f * tierFrac);
        droopY = height * (0.06f + 0.18f * bHash2);

        pBase = (Vector3){bx, clump->position.y, bz};
        pP1 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.10f,
            pBase.y + height * 0.38f,
            bz + sinf(bladeLeanAngle) * lean * 0.10f
        };
        pP2 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.44f,
            pBase.y + height * 0.74f,
            bz + sinf(bladeLeanAngle) * lean * 0.44f
        };
        pP3 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.95f,
            pBase.y + (height * 0.86f - droopY),
            bz + sinf(bladeLeanAngle) * lean * 0.95f
        };

        bool isOuterDrySheath = (blade < 2);
        if (isOuterDrySheath) {
            bladeRoot = (Color){42, 34, 18, 255};
            bladeTip  = (Color){192, 160, 82, 255};
        } else {
            bladeRoot = (Color){24, 40, 18, 255};
            bladeTip  = (Color){152, 196, 68, 255};
        }
    } else if (bladesPerClump <= 2 && bladeSegments <= 2) {
        // Two splayed silhouettes keep distant clumps from forming
        // parallel diagonal strokes across the whole meadow.
        float splay = bladesPerClump == 1 ? 0.0f : (blade == 0 ? -0.54f : 0.54f);
        bladeLeanAngle = clumpAngle + splay
                       + (bHash - 0.5f) * 0.46f;
        float bx = clump->position.x + cosf(bladeLeanAngle) * clump->radius * 0.09f;
        float bz = clump->position.z + sinf(bladeLeanAngle) * clump->radius * 0.09f;

        height = clump->height * (0.83f + 0.22f * bHash2);
        width = clump->radius * style.bladeWidthScale * widthMultiplier * 0.86f;
        lean = height * (0.35f + 0.13f * bHash3);
        droopY = height * 0.06f;

        pBase = (Vector3){bx, clump->position.y, bz};
        pP1 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.08f, pBase.y + height * 0.38f, bz + sinf(bladeLeanAngle) * lean * 0.08f};
        pP2 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.40f, pBase.y + height * 0.74f, bz + sinf(bladeLeanAngle) * lean * 0.40f};
        pP3 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.90f, pBase.y + (height * 0.88f - droopY), bz + sinf(bladeLeanAngle) * lean * 0.90f};

        bladeRoot = style.rootColor;
        bladeTip = style.tipColor;
    } else {
        // Volumetric clump architecture, including reduced far LOD.
        // Blades emerge from a collar and spread beyond the wind axis.
        float baseAzimuth = clumpAngle + (float)bladeId * golden
                          + (bHash - 0.5f) * 1.10f;
        float collarRadius = clump->radius * (0.16f + 0.12f * bHash3);
        float bx = clump->position.x + cosf(baseAzimuth) * collarRadius;
        float bz = clump->position.z + sinf(baseAzimuth) * collarRadius;

        float radX = cosf(baseAzimuth);
        float radZ = sinf(baseAzimuth);
        float windWeight = 0.22f;
        float combX = radX * (1.0f - windWeight) + flowX * windWeight;
        float combZ = radZ * (1.0f - windWeight) + flowZ * windWeight;
        float combLen = sqrtf(combX * combX + combZ * combZ);
        if (combLen < 0.01f) { combX = flowX; combZ = flowZ; combLen = 1.0f; }
        bladeLeanAngle = atan2f(combZ / combLen, combX / combLen);

        // Ghost of Tsushima 3-Tier Morphology Hierarchy (Enhanced sweeping slant)
        float lengthScale, widthScaleVal, leanFactor, droopFactor;
        if (bladeId < 2) {
            // Tier 1: Hero Arching Blades (sweeping cantilever arch, lower vertical apex)
            lengthScale = 1.02f + 0.20f * bHash2;
            widthScaleVal = 0.88f + 0.16f * bHash3;
            leanFactor = 0.38f + 0.18f * bHash;
            droopFactor = 0.08f + 0.14f * bHash3;
        } else if (bladeId < 4) {
            // Tier 2: Mid-Arching Foliage (wider outward slant for volume)
            lengthScale = 0.80f + 0.16f * bHash2;
            widthScaleVal = 1.02f + 0.18f * bHash3;
            leanFactor = 0.50f + 0.22f * bHash;
            droopFactor = 0.05f + 0.08f * bHash3;
        } else {
            // Tier 3: Ground Thatch & Undergrowth (carpets the soil at low angle)
            lengthScale = 0.46f + 0.12f * bHash2;
            widthScaleVal = 1.35f + 0.22f * bHash3;
            leanFactor = 0.62f + 0.25f * bHash;
            droopFactor = 0.03f;
        }

        height = clump->height * lengthScale;
        width = clump->radius * style.bladeWidthScale * widthMultiplier * widthScaleVal;
        lean = height * leanFactor;
        droopY = height * droopFactor;
        float botanical = fminf(fmaxf(style.botanicalVariation, 0.0f), 1.0f);
        if (botanical > 0.0f) {
            // Neighboring tufts share a growth habit; stable blade identities
            // add occasional broad leaves and needles without random patch noise.
            float habit = 0.5f + 0.5f * (
                sinf(clump->position.x * 0.19f + clump->position.z * 0.11f) * 0.60f +
                sinf(clump->position.z * 0.16f - clump->position.x * 0.06f) * 0.40f);
            float stretch = 1.08f + 0.24f * habit;
            if (bHash2 < 0.07f) stretch *= 0.78f;
            else if (bHash2 < 0.14f) stretch *= 1.12f;
            stretch = 1.0f + (stretch - 1.0f) * botanical;
            height *= stretch;
            width /= stretch; // Retain approximate leaf area as silhouettes change.
            lean *= stretch * (1.0f + botanical * (0.20f + 0.65f * habit));
            droopY = height * (droopFactor + botanical * 0.20f * habit);
        }
        if (style.growthForm == MAP_MEADOW_GROWTH_GRASS) {
            // Upright meadow leaves keep a visible canopy instead of forming
            // the wide, low lawn arches retained by legacy AUTO morphology.
            lean *= 0.65f;
            droopY *= 0.50f;
        }
        if (bladeSegments == 1) {
            // A full-length distant triangle rasterizes as a thin,
            // bright diagonal. Keep its area in a shorter, wider tuft.
            height *= 0.78f;
            width *= 1.28f;
            lean *= 0.67f;
            droopY *= 0.65f;
        }

        // Cantilever progressive Bézier curve (slanted arch, comfortable vertical canopy height)
        pBase = (Vector3){bx, clump->position.y, bz};
        pP1 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.12f,
            pBase.y + height * 0.28f,
            bz + sinf(bladeLeanAngle) * lean * 0.12f
        };
        pP2 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.52f,
            pBase.y + height * 0.56f,
            bz + sinf(bladeLeanAngle) * lean * 0.52f
        };
        pP3 = (Vector3){
            bx + cosf(bladeLeanAngle) * lean * 0.95f,
            pBase.y + fmaxf(height * 0.12f, height * 0.68f - droopY),
            bz + sinf(bladeLeanAngle) * lean * 0.95f
        };

        // Coherent Macro Seedhead Biome Field (Ghost of Tsushima golden pampas/wheat waves, scale ~16m):
        float strawNoise = sinf(clump->position.x * 0.16f + clump->position.z * 0.12f + 1.7f) * 0.5f
                         + sinf(clump->position.x * -0.10f + clump->position.z * 0.22f + 0.5f) * 0.5f;
        bool isStrawBiome = strawNoise > 0.18f;
        bool isSeedhead = false;
        if (isStrawBiome) {
            if (strawNoise > 0.42f) {
                // Dense golden wheat ridge: 65% of blades catch the warm straw plume
                isSeedhead = (bladeId < 4) || (bHash3 > 0.35f);
            } else {
                // Transition zone: top arching hero blades carry golden seedheads
                isSeedhead = (bladeId < 2 && bHash3 > 0.20f) || (bladeId == style.bladesPerClump - 1);
            }
        }
        if (isSeedhead) {
            bladeRoot = (Color){52, 62, 24, 255};    // warm golden-olive sheath
            bladeTip  = (Color){222, 194, 92, 255};  // ripe golden-amber wheat straw tip
            if (bHash2 > 0.60f)
                bladeTip = (Color){238, 222, 172, 255}; // ivory pampas feather plume
            bladeTip = Nature_LerpColor(bladeTip, (Color){182, 168, 98, 255}, botanical * 0.5f);
        } else {
            float colorField = sinf(clump->position.x * 0.12f + clump->position.z * 0.08f) * 0.55f
                             + sinf(clump->position.z * 0.14f - clump->position.x * 0.06f + 1.2f) * 0.45f;
            float tone = 1.0f + colorField * 0.14f + (bHash - 0.5f) * 0.16f;
            float warmth = colorField * 0.13f + (bHash3 - 0.5f) * 0.08f;
            int rR = (int)(style.rootColor.r * tone * (1.0f + warmth));
            int rG = (int)(style.rootColor.g * tone);
            int rB = (int)(style.rootColor.b * tone * (1.0f - warmth));
            int tR = (int)(style.tipColor.r * tone * (1.0f + warmth));
            int tG = (int)(style.tipColor.g * tone);
            int tB = (int)(style.tipColor.b * tone * (1.0f - warmth));

            // Botanical micro-diversity: 18% of blades have sun-kissed golden tips
            if (bHash2 < 0.18f && bladeId < 3) {
                tR = (int)(tR * 1.38f);
                tG = (int)(tG * 1.12f);
                tB = (int)(tB * 0.62f);
            }

            if (bladeId >= 4) {
                // Ground thatch has deeper forest shadow tones to anchor roots
                rR = (int)(rR * 0.82f); rG = (int)(rG * 0.85f); rB = (int)(rB * 0.82f);
                tR = (int)(tR * 0.74f); tG = (int)(tG * 0.80f); tB = (int)(tB * 0.74f);
            }
            bladeRoot = (Color){(unsigned char)fminf(255, fmaxf(0, rR)),
                                (unsigned char)fminf(255, fmaxf(0, rG)),
                                (unsigned char)fminf(255, fmaxf(0, rB)), 255};
            bladeTip  = (Color){(unsigned char)fminf(255, fmaxf(0, tR)),
                                (unsigned char)fminf(255, fmaxf(0, tG)),
                                (unsigned char)fminf(255, fmaxf(0, tB)), 255};
        }
    }
    if (!isReed && style.botanicalVariation > 0.0f) {
        Vector3 canopy = MeadowCanopyColor(MeadowHabitat(clump->position.x, clump->position.z));
        float variation = 0.90f + 0.20f * bHash2;
        Color canopyTip = {(unsigned char)(canopy.x * variation * 255.0f),
                           (unsigned char)(canopy.y * variation * 255.0f),
                           (unsigned char)(canopy.z * variation * 255.0f), 255};
        bladeTip = Nature_LerpColor(bladeTip, canopyTip,
            fminf(style.botanicalVariation, 1.0f) * 0.85f);
    }
    return (NatureBladeDescriptor){pBase, pP1, pP2, pP3, clump->position,
        bladeRoot, bladeTip, width, clump->phase, bladeLeanAngle, isReed};
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
        for (int blade = 0; blade < bladesPerClump; blade++) {
            NatureBladeDescriptor desc = Nature_DescribeMeadowBlade(clump, i, blade,
                style, bladesPerClump, bladeSegments, widthMultiplier);
            bool isReed = desc.isReed;
            float width = desc.width, bladeLeanAngle = desc.leanAngle;
            Vector3 pBase = desc.p0, pP1 = desc.p1, pP2 = desc.p2, pP3 = desc.p3;
            Color bladeRoot = desc.rootColor, bladeTip = desc.tipColor;

            // Base transverse vector fallback
            Vector3 baseSide = (Vector3){-sinf(bladeLeanAngle), 0.0f, cosf(bladeLeanAngle)};
            Vector3 terrainNormal = (Vector3){0.0f, 1.0f, 0.0f};

            for (int segment = 0; segment < bladeSegments; segment++) {
                // Long needle triangles create the bright leaf-tip streaks.
                // A shorter pointed section limits subpixel bright streaks.
                float tipStart = bladeSegments > 1 ? 0.80f : 0.0f;
                float t0 = segment == bladeSegments - 1 ? tipStart
                         : tipStart * (float)segment / (float)(bladeSegments - 1);
                float t1 = segment == bladeSegments - 1 ? 1.0f
                         : tipStart * (float)(segment + 1) / (float)(bladeSegments - 1);

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

                // Botanical Spear Blade Profile: fuller mid-body for lush coverage, needle tip
                float profile0 = isReed ? (0.85f + 0.25f * sinf(PI * t0)) * (1.0f - powf(t0, 1.8f))
                                        : (0.72f + 0.50f * t0) * (1.0f - t0 * t0);
                float profile1 = isReed ? (0.85f + 0.25f * sinf(PI * t1)) * (1.0f - powf(t1, 1.8f))
                                        : (0.72f + 0.50f * t1) * (1.0f - t1 * t1);
                float halfW0 = (width * 0.5f) * fmaxf(profile0, 0.08f);
                float halfW1 = (width * 0.5f) * fmaxf(profile1, 0.03f);

                // Section 1.2: V-shaped cross section (creased spine, winged edges)
                Vector3 fold0 = Vector3Scale(Ngeo0, halfW0 * 0.18f);
                Vector3 fold1 = Vector3Scale(Ngeo1, halfW1 * 0.18f);

                Vector3 p0 = Vector3Add(Vector3Subtract(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p1 = Vector3Add(Vector3Add(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p2 = Vector3Add(Vector3Add(center1, Vector3Scale(S1, halfW1)), fold1);
                Vector3 p3 = Vector3Add(Vector3Subtract(center1, Vector3Scale(S1, halfW1)), fold1);

                // Root ambient occlusion: soft, natural ground grounding without harsh pitch-black spots
                float occ0 = (t0 < 0.22f) ? (0.88f + 0.12f * (t0 / 0.22f)) : 1.0f;
                float occ1 = (t1 < 0.22f) ? (0.88f + 0.12f * (t1 / 0.22f)) : 1.0f;
                Color color0 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t0), occ0);
                float tipTint = (segment == bladeSegments - 1 && !style.hasPlumes) ? 0.86f : 1.0f;
                Color color1 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t1),
                                                 occ1 * tipTint);

                // Ghost of Tsushima / AAA Reference: Bent Vertex Normals
                // Blend polygon normal with Spherical Clump Normal + Upward Ground Normal
                Vector3 clumpSphereCenter = (Vector3){clump->position.x, clump->position.y - 0.04f, clump->position.z};

                Vector3 dSphere0L = Vector3Normalize(Vector3Subtract(p0, clumpSphereCenter));
                Vector3 dSphere0R = Vector3Normalize(Vector3Subtract(p1, clumpSphereCenter));
                Vector3 dSphere1L = Vector3Normalize(Vector3Subtract(p3, clumpSphereCenter));
                Vector3 dSphere1R = Vector3Normalize(Vector3Subtract(p2, clumpSphereCenter));
                Vector3 dSphereTip = Vector3Normalize(Vector3Subtract(center1, clumpSphereCenter));

                Vector3 nTarget0L = Vector3Normalize(Vector3Lerp(dSphere0L, terrainNormal, 0.45f));
                Vector3 nTarget0R = Vector3Normalize(Vector3Lerp(dSphere0R, terrainNormal, 0.45f));
                Vector3 nTarget1L = Vector3Normalize(Vector3Lerp(dSphere1L, terrainNormal, 0.45f));
                Vector3 nTarget1R = Vector3Normalize(Vector3Lerp(dSphere1R, terrainNormal, 0.45f));
                Vector3 nTargetTip = Vector3Normalize(Vector3Lerp(dSphereTip, terrainNormal, 0.55f));

                // Blade edge transverse tilt
                Vector3 nMesh0L = Vector3Normalize(Vector3Subtract(Ngeo0, Vector3Scale(S0, 0.35f)));
                Vector3 nMesh0R = Vector3Normalize(Vector3Add(Ngeo0, Vector3Scale(S0, 0.35f)));
                Vector3 nMesh1L = Vector3Normalize(Vector3Subtract(Ngeo1, Vector3Scale(S1, 0.32f)));
                Vector3 nMesh1R = Vector3Normalize(Vector3Add(Ngeo1, Vector3Scale(S1, 0.32f)));

                // 72% Bent Normal blending for silky, continuous velvet lighting
                Vector3 nL0 = Vector3Normalize(Vector3Lerp(nMesh0L, nTarget0L, 0.72f));
                Vector3 nR0 = Vector3Normalize(Vector3Lerp(nMesh0R, nTarget0R, 0.72f));
                Vector3 nL1 = Vector3Normalize(Vector3Lerp(nMesh1L, nTarget1L, 0.72f));
                Vector3 nR1 = Vector3Normalize(Vector3Lerp(nMesh1R, nTarget1R, 0.72f));
                Vector3 nTip = Vector3Normalize(Vector3Lerp(Ngeo1, nTargetTip, 0.78f));

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
    if (!style.hasPlumes && bladeSegments > 1)
        (void)Nature_IndexBladeMesh(&mesh);
    return Nature_ModelFromMesh(mesh, Nature_GetShader(style.texturePath != NULL));
}

#include "maps/toolkit/map_props_meadow_parametric.inl"

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
    if (NatureParametric_Create(&meadow, placements, count, style)) return meadow;
    if (style.shadowDistance > 0.0f)
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
    meadow.midLodDistance = style.midLodDistance > 0.0f ? style.midLodDistance
                          : (style.lodDistance > 0.0f ? style.lodDistance * 0.45f : 0.0f);
    meadow.drawDistance = style.drawDistance;
    meadow.shadowDistance = style.shadowDistance;
    bool buildContactShadows = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_MED;
    bool buildRealShadowLod = style.shadowDistance > 0.0f;

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

            // Mid LOD: intermediate distance (e.g. 10m - 22m).
            // Uses fewer blades and segments with slight widening (1.22x) to preserve silhouette and volume
            // while drastically reducing subpixel polygon workload and overdraw.
            int midBlades = style.bladesPerClump >= 5 ? 4
                          : (style.bladesPerClump >= 3 ? 3 : style.bladesPerClump);
            int midSegments = (style.bladeSegments >= 3) ? 2 : 1;
            int midCount = 0;
            Model midModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                midBlades, midSegments, 1.22f, &midCount);

            // Preserve coverage: removing every second clump turns a meadow
            // into isolated spikes. Far LOD reduces each clump instead.
            // Compensate for the lost blade count in projected coverage. Thin
            // far blades reveal the dark ground as a stippled LOD boundary.
            // Three distinct silhouettes retain the clump shape at one segment.
            int farBlades = 3;
            int farCount = 0;
            Model farModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                farBlades, 1, 1.65f, &farCount);
            Model shadowModel = {0};
            // Eliminate crude 6-vertex trapezoid wedges.
            // Grass uses real dynamic shadow map casting via realShadowModel.
            Model realShadowModel = {0};
            int realShadowCount = 0;
            if (buildRealShadowLod)
                realShadowModel = Nature_BuildMeadowChunk(
                    placements, count, style, x0, x1, z0, z1, 1,
                    3, 2, 1.30f, &realShadowCount);
            MapMeadowChunk *chunk = &meadow.chunks[meadow.chunkCount++];
            if (textured) {
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
    if (meadow.chunkCount <= 0) {
        MemFree(meadow.chunks);
        meadow.chunks = NULL;
        return meadow;
    }
    meadow.ready = true;
    meadow.textured = textured;
    meadow.alphaCutoff = style.alphaCutoff;
    long long rawVertices = 0, storedVertices = 0, indexBytes = 0;
    for (int i = 0; i < meadow.chunkCount; i++) {
        Model models[4] = {meadow.chunks[i].nearModel, meadow.chunks[i].midModel,
                           meadow.chunks[i].farModel, meadow.chunks[i].realShadowModel};
        for (int lod = 0; lod < 4; lod++) {
            for (int j = 0; j < models[lod].meshCount; j++) {
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

// Stage mutable visibility IDs before shadow/color rendering. Draw retains a
// camera-signature fallback for callers without a preparation phase.
void MapProp_PrepareMeadow(MapMeadowSurface *meadow, Vector3 worldOffset)
{
    if (!meadow || !meadow->ready || !meadow->parametric) return;
    NatureParametricMeadow *data = meadow->parametric;
    if (!data->compact && !data->nearFirst) return;
    NatureMeadowView view = NatureParametric_View(meadow);
    if (NatureParametric_IsPrepared(meadow,worldOffset,view.bands)) return;
    float drawDistanceSq = view.drawDistance*view.drawDistance;
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        Vector3 center = Vector3Add(chunk->center,worldOffset);
        chunk->visibleThisFrame = Nature_IsChunkVisible(center,chunk->radius);
        float dx = camera.position.x-center.x, dz = camera.position.z-center.z;
        if (view.drawDistance > 0.0f && dx*dx+dz*dz > drawDistanceSq) chunk->visibleThisFrame = false;
    }
    NatureParametric_PrepareVisible(meadow,worldOffset,view.bands);
}

void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready) return;
    GfxQuality quality = GfxQuality_Get();
    NatureMeadowView view = NatureParametric_View(meadow);
    float lodScale = view.lodScale, zoomFactor = view.zoomFactor;
    float lodDistance = view.lodDistance, drawDistance = view.drawDistance;
    float drawDistanceSq = drawDistance*drawDistance;
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
            float fdx = camera.position.x - camera.target.x;
            float fdz = camera.position.z - camera.target.z;
            float focalDist = sqrtf(fdx * fdx + fdz * fdz);
            float farThreshold = focalDist + lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 4.0f;
            float midThreshold = (meadow->midLodDistance > 0.0f && chunk->midReady) ?
                                 (focalDist + meadow->midLodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 2.5f) : 0.0f;
            float hysteresis = quality >= GFX_HIGH ? 1.1f : 1.8f;
            float dy = camera.position.y - center.y;
            float distance = sqrtf(distanceSq + dy * dy);

            if (midThreshold > 0.0f) {
                // 3-tier LOD with hysteresis to prevent edge thrashing
                if (chunk->lodLevel == 2) { // currently Far
                    if (distance < farThreshold - hysteresis) {
                        chunk->lodLevel = (distance < midThreshold - hysteresis) ? 0 : 1;
                    }
                } else if (chunk->lodLevel == 1) { // currently Mid
                    if (distance > farThreshold + hysteresis) {
                        chunk->lodLevel = 2;
                    } else if (distance < midThreshold - hysteresis) {
                        chunk->lodLevel = 0;
                    }
                } else { // currently Near
                    if (distance > farThreshold + hysteresis) {
                        chunk->lodLevel = 2;
                    } else if (distance > midThreshold + hysteresis) {
                        chunk->lodLevel = 1;
                    }
                }
            } else {
                // 2-tier fallback
                if (chunk->lodLevel == 2) {
                    if (distance < farThreshold - hysteresis)
                        chunk->lodLevel = 0;
                } else if (distance > farThreshold + hysteresis) {
                    chunk->lodLevel = 2;
                }
            }
            chunk->farLod = (chunk->lodLevel == 2);
        } else {
            chunk->lodLevel = 0;
            chunk->farLod = false;
        }

    }

    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_LOW && EnvShadow_IsEnabled() &&
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

    Vector4 tuftBands = view.bands;
    if (meadow->parametric) NatureParametric_PrepareVisible(meadow,worldOffset,tuftBands);
    Shader shader = meadow->parametric ? NatureParametric_Shader(false)
                                      : Nature_GetShader(meadow->textured);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        meadow->textured, meadow->alphaCutoff,
                        NATURE_WIND_RESPONSE_GRASS);
    if (meadow->parametric) {
        NatureParametric_BindReceivers(shader,false);
        SetShaderValue(shader,s_natureTuftLodBandsLoc,&tuftBands,SHADER_UNIFORM_VEC4);
        SetShaderValue(shader,s_natureTuftLodCameraLoc,&camera.position,SHADER_UNIFORM_VEC3);
        Vector2 fadeRange = {view.drawDistance * 0.82f, view.drawDistance};
        SetShaderValue(shader,s_natureTuftFadeRangeLoc,&fadeRange,SHADER_UNIFORM_VEC2);
    }
    if (!meadow->textured) {
        float tipSoftening = 1.0f;
        int tipSofteningLoc = GetShaderLocation(shader, "u_grassTipSoftening");
        if (tipSofteningLoc >= 0)
            SetShaderValue(shader, tipSofteningLoc, &tipSoftening, SHADER_UNIFORM_FLOAT);
    }
    rlDisableBackfaceCulling();
    NatureParametricMeadow *ordered = meadow->parametric;
    int drawCount = ordered && ordered->nearFirst ? ordered->drawCount : meadow->chunkCount;
    for (int draw = 0; draw < drawCount; draw++) {
        int i = ordered && ordered->nearFirst ? ordered->drawOrder[draw] : draw;
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->visibleThisFrame)
            continue;
        if (meadow->parametric) {
            // Chunks batch and cull geometry; roots select detail themselves.
            // Submit only LODs that intersect this conservative chunk sphere.
            Vector3 center = Vector3Add(chunk->center, worldOffset);
            float d = Vector3Distance(camera.position, center);
            float minDistance = fmaxf(0.0f, d - chunk->radius);
            float maxDistance = d + chunk->radius;
            for (int lod = 0; lod < 3; lod++) {
                if (!NatureParametric_LodIntersectsSphere(lod,minDistance,maxDistance,tuftBands)) continue;
                NatureParametricMeadow *data = meadow->parametric;
                if (data->compact && data->ranges[i].visibleCount[lod] == 0) continue;
                NatureParametric_DrawChunk(meadow,i,lod,shader,worldOffset);
                if (lod == 2) s_natureRenderStats.meadowFarDraws++;
                else if (lod == 1) s_natureRenderStats.meadowMidDraws++;
                else s_natureRenderStats.meadowNearDraws++;
            }
            continue;
        }
        if (chunk->lodLevel == 2) {
            DrawModel(chunk->farModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowFarDraws++;
        } else if (chunk->lodLevel == 1 && chunk->midReady) {
            DrawModel(chunk->midModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowMidDraws++;
        } else {
            DrawModel(chunk->nearModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowNearDraws++;
        }
    }
    if (meadow->parametric) NatureParametric_EndReceivers();
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready || GfxQuality_Get() < GFX_LOW ||
        meadow->shadowDistance <= 0.0f ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(false))
        return;
    Shader shader = meadow->parametric ? NatureParametric_Shader(true) : NatureShadow_GetShader();
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              meadow->textured, meadow->alphaCutoff,
                              NATURE_WIND_RESPONSE_GRASS);
    if (meadow->parametric) NatureParametric_BindReceivers(shader,true);
    rlDisableBackfaceCulling();
    // The shadow box follows the gameplay focus, not the orbit camera. A
    // camera-centred test dropped casters at the far side of the visible box.
    Vector3 shadowFocus = EnvShadow_GetFocus();
    float maxDist = fminf(meadow->shadowDistance + 4.0f,
                          EnvShadow_GetHalfExtent() + 4.0f);
    int draws = 0;
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->realShadowReady)
            continue;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        float dx = center.x - shadowFocus.x;
        float dz = center.z - shadowFocus.z;
        float limit = fminf(maxDist + chunk->radius * 0.35f,
                            EnvShadow_GetHalfExtent() + 2.0f);
        if ((dx * dx + dz * dz) > limit * limit)
            continue;
        if (!Nature_IntersectsDynamicShadowCoverage(center, chunk->radius) &&
            !Nature_ShadowCasterFilterActive())
            continue;
        if (meadow->parametric) {
            NatureParametric_DrawChunk(meadow,i,3,shader,worldOffset);
            s_natureRenderStats.meadowShadowDraws++;
            draws++;
            continue;
        }
        Shader previous = chunk->realShadowModel.materials[0].shader;
        chunk->realShadowModel.materials[0].shader = shader;
        DrawModel(chunk->realShadowModel, worldOffset, 1.0f, WHITE);
        chunk->realShadowModel.materials[0].shader = previous;
        draws++;
    }
    if (meadow->parametric) NatureParametric_EndReceivers();
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready) return;
    if (meadow->parametric) {
        NatureParametric_Destroy(meadow);
        MemFree(meadow->chunks);
        memset(meadow,0,sizeof(*meadow));
        return;
    }
    for (int i = 0; i < meadow->chunkCount; i++) {
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

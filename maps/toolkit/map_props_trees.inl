// Procedural 3D Xianxia & Alpine AAA Trees System (GPU-Instanced, 3-LOD, Indexed)
// Decoupled 3-Stage Botanical Pipeline:
//   Stage 1: TreeSkeleton  (Bishop Parallel-Transport Spline Sweep, Root Buttresses, Pipe-Model Order 0..3)
//   Stage 2: TreeCanopy    (Species Ellipsoidal Cloud Lobes, Spherized Normals, 3D Volumetric AO, Drop Rank)
//   Stage 3: TreeLeaf      (2048x1024 8-Cell Dilated Botanical Sprig Atlas + 3D Cupped Cross-Tufts)
// Zero-Rebuild GPU Lush -> Autumn -> Withered -> Bare Dry / Scorched transitions.
// Included into map_props.c — not a standalone compilation unit.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

typedef struct
{
    Vector3 *pos;
    Vector3 *norm;
    Vector2 *uv;
    unsigned char *col;
    int count;
    int cap;
} TreeMeshBuilder;

static bool TreeBuilder_Init(TreeMeshBuilder *b, int maxVerts)
{
    b->pos = (Vector3 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector3));
    b->norm = (Vector3 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector3));
    b->uv = (Vector2 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector2));
    b->col = (unsigned char *)MemAlloc((unsigned int)maxVerts * 4u);
    b->count = 0;
    b->cap = maxVerts;
    return b->pos && b->norm && b->uv && b->col;
}

static void TreeBuilder_Free(TreeMeshBuilder *b)
{
    if (b->pos) MemFree(b->pos);
    if (b->norm) MemFree(b->norm);
    if (b->uv) MemFree(b->uv);
    if (b->col) MemFree(b->col);
    *b = (TreeMeshBuilder){0};
}

static void TreeBuilder_PushTri(TreeMeshBuilder *b,
                                Vector3 p0, Vector3 n0, Vector2 uv0, Color c0,
                                Vector3 p1, Vector3 n1, Vector2 uv1, Color c1,
                                Vector3 p2, Vector3 n2, Vector2 uv2, Color c2)
{
    if (b->count + 3 > b->cap)
    {
        int newCap = (b->cap > 0) ? (b->cap * 2) : 4096;
        while (newCap < b->count + 3)
            newCap *= 2;
        Vector3 *np = (Vector3 *)MemRealloc(b->pos, (unsigned int)newCap * (unsigned int)sizeof(Vector3));
        Vector3 *nn = (Vector3 *)MemRealloc(b->norm, (unsigned int)newCap * (unsigned int)sizeof(Vector3));
        Vector2 *nu = (Vector2 *)MemRealloc(b->uv, (unsigned int)newCap * (unsigned int)sizeof(Vector2));
        unsigned char *nc = (unsigned char *)MemRealloc(b->col, (unsigned int)newCap * 4u);
        if (!np || !nn || !nu || !nc)
            return;
        b->pos = np;
        b->norm = nn;
        b->uv = nu;
        b->col = nc;
        b->cap = newCap;
    }
    Vector3 ps[3] = {p0, p1, p2};
    Vector3 ns[3] = {n0, n1, n2};
    Vector2 uvs[3] = {uv0, uv1, uv2};
    Color cs[3] = {c0, c1, c2};
    for (int k = 0; k < 3; k++)
    {
        int i = b->count++;
        b->pos[i] = ps[k];
        b->norm[i] = ns[k];
        b->uv[i] = uvs[k];
        b->col[i * 4 + 0] = cs[k].r;
        b->col[i * 4 + 1] = cs[k].g;
        b->col[i * 4 + 2] = cs[k].b;
        b->col[i * 4 + 3] = cs[k].a;
    }
}

static unsigned int Tree_HashVertKey(Vector3 p, Vector3 n, Vector2 uv, const unsigned char c[4])
{
    int px = (int)roundf(p.x * 2000.0f);
    int py = (int)roundf(p.y * 2000.0f);
    int pz = (int)roundf(p.z * 2000.0f);
    int nx = (int)roundf(n.x * 250.0f);
    int ny = (int)roundf(n.y * 250.0f);
    int nz = (int)roundf(n.z * 250.0f);
    int ux = (int)roundf(uv.x * 500.0f);
    int uy = (int)roundf(uv.y * 500.0f);
    unsigned int h = 2166136261u;
    int vals[8] = {px, py, pz, nx, ny, nz, ux, uy};
    for (int i = 0; i < 8; i++)
    {
        h ^= (unsigned int)vals[i];
        h *= 16777619u;
    }
    unsigned int colPacked = ((unsigned int)c[0]) | ((unsigned int)c[1] << 8) |
                             ((unsigned int)c[2] << 16) | ((unsigned int)c[3] << 24);
    h ^= colPacked;
    h *= 16777619u;
    return h;
}

static bool Tree_VertEqual(const TreeMeshBuilder *b, int a, int idx,
                           const float *uPos, const float *uNorm, const float *uUv, const unsigned char *uCol)
{
    const float epsP = 0.0006f;
    const float epsN = 0.006f;
    const float epsU = 0.003f;
    if (fabsf(b->pos[a].x - uPos[idx * 3 + 0]) > epsP ||
        fabsf(b->pos[a].y - uPos[idx * 3 + 1]) > epsP ||
        fabsf(b->pos[a].z - uPos[idx * 3 + 2]) > epsP)
        return false;
    if (fabsf(b->norm[a].x - uNorm[idx * 3 + 0]) > epsN ||
        fabsf(b->norm[a].y - uNorm[idx * 3 + 1]) > epsN ||
        fabsf(b->norm[a].z - uNorm[idx * 3 + 2]) > epsN)
        return false;
    if (fabsf(b->uv[a].x - uUv[idx * 2 + 0]) > epsU ||
        fabsf(b->uv[a].y - uUv[idx * 2 + 1]) > epsU)
        return false;
    return memcmp(&b->col[a * 4], &uCol[idx * 4], 4u) == 0;
}

static Mesh TreeBuilder_ToMesh(TreeMeshBuilder *b)
{
    Mesh m = {0};
    if (b->count < 3)
        return m;

    int hashSize = 1;
    while (hashSize < b->count * 2)
        hashSize <<= 1;
    int *buckets = (int *)MemAlloc((unsigned int)hashSize * (unsigned int)sizeof(int));
    int *next = (int *)MemAlloc((unsigned int)b->count * (unsigned int)sizeof(int));
    float *uPos = (float *)MemAlloc((unsigned int)b->count * 3u * sizeof(float));
    float *uNorm = (float *)MemAlloc((unsigned int)b->count * 3u * sizeof(float));
    float *uUv = (float *)MemAlloc((unsigned int)b->count * 2u * sizeof(float));
    unsigned char *uCol = (unsigned char *)MemAlloc((unsigned int)b->count * 4u);
    unsigned short *indices = (unsigned short *)MemAlloc((unsigned int)b->count * (unsigned int)sizeof(unsigned short));

    bool indexedOk = (buckets && next && uPos && uNorm && uUv && uCol && indices);
    int uniqueCount = 0;
    if (indexedOk)
    {
        for (int i = 0; i < hashSize; i++)
            buckets[i] = -1;
        unsigned int mask = (unsigned int)(hashSize - 1);
        for (int i = 0; i < b->count; i++)
        {
            unsigned int h = Tree_HashVertKey(b->pos[i], b->norm[i], b->uv[i], &b->col[i * 4]) & mask;
            int found = -1;
            for (int cur = buckets[h]; cur >= 0; cur = next[cur])
            {
                if (Tree_VertEqual(b, i, cur, uPos, uNorm, uUv, uCol))
                {
                    found = cur;
                    break;
                }
            }
            if (found < 0)
            {
                if (uniqueCount >= 65530)
                {
                    indexedOk = false;
                    break;
                }
                found = uniqueCount++;
                uPos[found * 3 + 0] = b->pos[i].x;
                uPos[found * 3 + 1] = b->pos[i].y;
                uPos[found * 3 + 2] = b->pos[i].z;
                uNorm[found * 3 + 0] = b->norm[i].x;
                uNorm[found * 3 + 1] = b->norm[i].y;
                uNorm[found * 3 + 2] = b->norm[i].z;
                uUv[found * 2 + 0] = b->uv[i].x;
                uUv[found * 2 + 1] = b->uv[i].y;
                memcpy(&uCol[found * 4], &b->col[i * 4], 4u);
                next[found] = buckets[h];
                buckets[h] = found;
            }
            indices[i] = (unsigned short)found;
        }
    }

    if (buckets) MemFree(buckets);
    if (next) MemFree(next);

    if (indexedOk && uniqueCount > 0)
    {
        m.vertexCount = uniqueCount;
        m.triangleCount = b->count / 3;
        m.vertices = (float *)MemRealloc(uPos, (unsigned int)uniqueCount * 3u * sizeof(float));
        m.normals = (float *)MemRealloc(uNorm, (unsigned int)uniqueCount * 3u * sizeof(float));
        m.texcoords = (float *)MemRealloc(uUv, (unsigned int)uniqueCount * 2u * sizeof(float));
        m.colors = (unsigned char *)MemRealloc(uCol, (unsigned int)uniqueCount * 4u);
        m.indices = indices;
        UploadMesh(&m, false);
        return m;
    }

    if (uPos) MemFree(uPos);
    if (uNorm) MemFree(uNorm);
    if (uUv) MemFree(uUv);
    if (uCol) MemFree(uCol);
    if (indices) MemFree(indices);

    m.vertexCount = b->count;
    m.triangleCount = b->count / 3;
    m.vertices = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.normals = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.texcoords = (float *)MemAlloc((unsigned int)m.vertexCount * 2u * sizeof(float));
    m.colors = (unsigned char *)MemAlloc((unsigned int)m.vertexCount * 4u);
    if (!m.vertices || !m.normals || !m.texcoords || !m.colors)
    {
        MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords); MemFree(m.colors);
        return (Mesh){0};
    }
    memcpy(m.vertices, b->pos, (size_t)m.vertexCount * 3u * sizeof(float));
    memcpy(m.normals, b->norm, (size_t)m.vertexCount * 3u * sizeof(float));
    memcpy(m.texcoords, b->uv, (size_t)m.vertexCount * 2u * sizeof(float));
    memcpy(m.colors, b->col, (size_t)m.vertexCount * 4u);
    UploadMesh(&m, false);
    return m;
}

static float Tree_NextRand(unsigned int *state)
{
    *state = (*state * 1664525u + 1013904223u);
    return (float)((*state >> 8) & 0x00ffffffu) / 16777216.0f;
}

static unsigned int Tree_HashBytes(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < len; i++)
    {
        h ^= (unsigned int)p[i];
        h *= 16777619u;
    }
    return h;
}

static Color Tree_LerpColor(Color a, Color b, float t)
{
    float u = fminf(1.0f, fmaxf(0.0f, t));
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * u),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * u),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * u),
        (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * u)
    };
}

// Bit-exact 24-bit float32 instance state packing:
// bits [0..7]   phaseQ  (0..255)
// bits [8..15]  dryQ    (0..255)
// bits [16..19] scorchQ (0..15)
// bits [20..23] frostQ  (0..15)
static float Tree_PackInstanceState(float phase, float dryness, float scorch, float frost)
{
    float pFrac = phase - floorf(phase);
    unsigned int pQ = (unsigned int)fminf(255.0f, fmaxf(0.0f, floorf(pFrac * 255.0f + 0.5f)));
    unsigned int dQ = (unsigned int)fminf(255.0f, fmaxf(0.0f, floorf(dryness * 255.0f + 0.5f)));
    unsigned int sQ = (unsigned int)fminf(15.0f, fmaxf(0.0f, floorf(scorch * 15.0f + 0.5f)));
    unsigned int fQ = (unsigned int)fminf(15.0f, fmaxf(0.0f, floorf(frost * 15.0f + 0.5f)));
    unsigned int packed = pQ | (dQ << 8) | (sQ << 16) | (fQ << 20);
    return (float)packed;
}

// =========================================================================
// STAGE 3a: PROCEDURAL 2048x1024 (4x2 = 8-CELL) BOTANICAL SPRIG ATLAS
//   Cell 0 (0,   0):   Alpine Spruce / Fir Bough (preserved for mau_son)
//   Cell 1 (512, 0):   Columnar Taiga / Cedar Scale-Foliage (preserved)
//   Cell 2 (1024,0):   Huangshan / Coastal Pine Star-Fascicle Rosettes
//   Cell 3 (1536,0):   Xianxia Peach Blossom 5-Petal Twig Spray
//   Cell 4 (0,   512): Broadleaf Oak / Banyan Airy Lobed Twig Cluster
//   Cell 5 (512, 512): Birch / Poplar Dainty Diamond-Leaf Sprig
//   Cell 6 (1024,512): Bamboo Radiating Hand-Fan Rosette (Mac Truc)
//   Cell 7 (1536,512): Weeping Willow / Acacia Pendulous Feather Spray
// =========================================================================

#define TREE_ATLAS_W 2048
#define TREE_ATLAS_H 1024
#define TREE_CELL_SZ 512

typedef enum
{
    TREE_CELL_SPRUCE = 0,
    TREE_CELL_CEDAR = 1,
    TREE_CELL_PINE_ROSETTE = 2,
    TREE_CELL_PEACH_BLOSSOM = 3,
    TREE_CELL_OAK_BROADLEAF = 4,
    TREE_CELL_BIRCH_SPRIG = 5,
    TREE_CELL_BAMBOO_FAN = 6,
    TREE_CELL_WILLOW_WHIP = 7
} TreeAtlasCell;

static void TreeAtlas_BlendPixel(unsigned char *px, int w, int h, int x, int y,
                                 unsigned char r, unsigned char g, unsigned char b, float alpha)
{
    if (x < 0 || x >= w || y < 0 || y >= h || alpha <= 0.001f)
        return;
    int idx = (y * w + x) * 4;
    float aSrc = fminf(1.0f, fmaxf(0.0f, alpha));
    float aDst = (float)px[idx + 3] * (1.0f / 255.0f);
    float aOut = aSrc + aDst * (1.0f - aSrc);
    if (aOut > 1e-4f)
    {
        float invOut = 1.0f / aOut;
        px[idx + 0] = (unsigned char)fminf(255.0f, ((float)r * aSrc + (float)px[idx + 0] * aDst * (1.0f - aSrc)) * invOut);
        px[idx + 1] = (unsigned char)fminf(255.0f, ((float)g * aSrc + (float)px[idx + 1] * aDst * (1.0f - aSrc)) * invOut);
        px[idx + 2] = (unsigned char)fminf(255.0f, ((float)b * aSrc + (float)px[idx + 2] * aDst * (1.0f - aSrc)) * invOut);
        px[idx + 3] = (unsigned char)fminf(255.0f, aOut * 255.0f);
    }
}

static void TreeAtlas_DrawStroke(unsigned char *px, int atlasW, int atlasH, int ox, int oy,
                                 float x0, float y0, float x1, float y1,
                                 float rad0, float rad1, Color c0, Color c1)
{
    float pad = fmaxf(rad0, rad1) + 1.5f;
    int ix0 = (int)fmaxf(12.0f, floorf(fminf(x0, x1) - pad));
    int ix1 = (int)fminf(499.0f, ceilf(fmaxf(x0, x1) + pad));
    int iy0 = (int)fmaxf(12.0f, floorf(fminf(y0, y1) - pad));
    int iy1 = (int)fminf(499.0f, ceilf(fmaxf(y0, y1) + pad));

    float dx = x1 - x0;
    float dy = y1 - y0;
    float lenSq = dx * dx + dy * dy;
    float invLenSq = (lenSq > 1e-5f) ? (1.0f / lenSq) : 0.0f;

    for (int y = iy0; y <= iy1; y++)
    {
        for (int x = ix0; x <= ix1; x++)
        {
            float t = ((float)x - x0) * dx + ((float)y - y0) * dy;
            t = fminf(1.0f, fmaxf(0.0f, t * invLenSq));
            float cx = x0 + dx * t;
            float cy = y0 + dy * t;
            float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy));
            float rad = rad0 + (rad1 - rad0) * t;
            if (d < rad + 1.0f)
            {
                float cov = fminf(1.0f, fmaxf(0.0f, rad + 0.8f - d));
                unsigned char r = (unsigned char)(c0.r + (c1.r - c0.r) * t);
                unsigned char g = (unsigned char)(c0.g + (c1.g - c0.g) * t);
                unsigned char b = (unsigned char)(c0.b + (c1.b - c0.b) * t);
                TreeAtlas_BlendPixel(px, atlasW, atlasH, ox + x, oy + y, r, g, b, cov);
            }
        }
    }
}

static void TreeAtlas_DrawLeaf(unsigned char *px, int atlasW, int atlasH, int ox, int oy,
                               float cx, float cy, float ang, float len, float wid,
                               float lobeFreq, float lobeAmp, Color cBase, Color cTip)
{
    float ca = cosf(ang), sa = sinf(ang);
    float rad = fmaxf(len, wid) + 2.5f;
    int ix0 = (int)fmaxf(12.0f, floorf(cx - rad));
    int ix1 = (int)fminf(499.0f, ceilf(cx + rad));
    int iy0 = (int)fmaxf(12.0f, floorf(cy - rad));
    int iy1 = (int)fminf(499.0f, ceilf(cy + rad));

    for (int y = iy0; y <= iy1; y++)
    {
        for (int x = ix0; x <= ix1; x++)
        {
            float rx = (float)x - cx;
            float ry = (float)y - cy;
            float lx = rx * ca + ry * sa;
            float ly = -rx * sa + ry * ca;
            float u = lx / fmaxf(len, 1.0f);
            if (u < 0.0f || u > 1.0f)
                continue;
            float env = powf(sinf(u * PI), 0.78f);
            if (lobeAmp > 0.001f)
                env *= (1.0f + lobeAmp * sinf(u * lobeFreq * PI));
            float maxW = wid * env;
            float ad = fabsf(ly);
            if (ad < maxW + 1.0f)
            {
                float cov = fminf(1.0f, fmaxf(0.0f, maxW + 0.75f - ad));
                // Subtle midrib crease and bilateral bevel so individual leaves read crisply
                float rib = (ad < 1.35f) ? 0.86f : 1.0f;
                float edgeLight = 0.94f + 0.10f * (ad / fmaxf(maxW, 1.0f));
                float shade = rib * edgeLight;
                unsigned char r = (unsigned char)fminf(255.0f, ((float)cBase.r + ((float)cTip.r - (float)cBase.r) * u) * shade);
                unsigned char g = (unsigned char)fminf(255.0f, ((float)cBase.g + ((float)cTip.g - (float)cBase.g) * u) * shade);
                unsigned char b = (unsigned char)fminf(255.0f, ((float)cBase.b + ((float)cTip.b - (float)cBase.b) * u) * shade);
                TreeAtlas_BlendPixel(px, atlasW, atlasH, ox + x, oy + y, r, g, b, cov);
            }
        }
    }
}

static void TreeAtlas_DrawDisc(unsigned char *px, int atlasW, int atlasH, int ox, int oy,
                               float cx, float cy, float rad, Color cCenter, Color cEdge)
{
    int ix0 = (int)fmaxf(12.0f, floorf(cx - rad - 1.5f));
    int ix1 = (int)fminf(499.0f, ceilf(cx + rad + 1.5f));
    int iy0 = (int)fmaxf(12.0f, floorf(cy - rad - 1.5f));
    int iy1 = (int)fminf(499.0f, ceilf(cy + rad + 1.5f));
    for (int y = iy0; y <= iy1; y++)
    {
        for (int x = ix0; x <= ix1; x++)
        {
            float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy));
            if (d < rad + 1.0f)
            {
                float cov = fminf(1.0f, fmaxf(0.0f, rad + 0.7f - d));
                float t = fminf(1.0f, d / fmaxf(rad, 1.0f));
                Color c = Tree_LerpColor(cCenter, cEdge, t);
                TreeAtlas_BlendPixel(px, atlasW, atlasH, ox + x, oy + y, c.r, c.g, c.b, cov);
            }
        }
    }
}

static void TreeAtlas_DrawBlossom(unsigned char *px, int atlasW, int atlasH, int ox, int oy,
                                  float cx, float cy, float radius, float rot,
                                  Color petalCore, Color petalTip, Color stamenCol)
{
    // 5 distinct rounded-oval peach petals with subtle notch separation & radiant center-to-tip gradient
    for (int p = 0; p < 5; p++)
    {
        float a = rot + (float)p * (2.0f * PI / 5.0f);
        float px0 = cx + cosf(a) * (radius * 0.16f);
        float py0 = cy + sinf(a) * (radius * 0.16f);
        TreeAtlas_DrawLeaf(px, atlasW, atlasH, ox, oy, px0, py0, a,
                           radius * 0.96f, radius * 0.44f, 0.0f, 0.0f, petalCore, petalTip);
    }
    // Deep crimson-magenta calyx/heart + tiny warm pollen center
    TreeAtlas_DrawDisc(px, atlasW, atlasH, ox, oy, cx, cy, radius * 0.28f, stamenCol, petalCore);
    TreeAtlas_DrawDisc(px, atlasW, atlasH, ox, oy, cx, cy, radius * 0.11f, (Color){248, 214, 132, 255}, stamenCol);
}

// Cell 0: Alpine Spruce / Fir Bough (preserved for mau_son snow conifers)
static void TreeAtlas_BakeSpruceCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 4401u;
    const Color pal[6] = {
        {11, 30, 21, 255}, {16, 41, 28, 255}, {22, 54, 37, 255},
        {29, 67, 46, 255}, {38, 80, 55, 255}, {47, 92, 65, 255}
    };
    const Color stemC0 = {58, 37, 24, 255};
    const Color stemC1 = {45, 28, 18, 255};
    const Color frostTip = {136, 168, 157, 255};

    typedef struct { float t; int side; float len; float ang; } SideShoot;
    const SideShoot shoots[10] = {
        {0.14f, -1, 162.0f, -0.78f}, {0.17f,  1, 158.0f,  0.76f},
        {0.30f, -1, 180.0f, -0.70f}, {0.33f,  1, 178.0f,  0.68f},
        {0.48f, -1, 154.0f, -0.62f}, {0.51f,  1, 152.0f,  0.60f},
        {0.65f, -1, 120.0f, -0.54f}, {0.68f,  1, 118.0f,  0.52f},
        {0.80f, -1,  84.0f, -0.46f}, {0.83f,  1,  82.0f,  0.44f}
    };

    float sx0 = 256.0f, sy0 = 492.0f, sx1 = 256.0f, sy1 = 36.0f;
    for (int s = 0; s < 10; s++)
    {
        float bx = sx0 + (sx1 - sx0) * shoots[s].t;
        float by = sy0 + (sy1 - sy0) * shoots[s].t;
        float angle = -PI * 0.5f + shoots[s].ang;
        float ex = bx + cosf(angle) * shoots[s].len;
        float ey = by + sinf(angle) * shoots[s].len;
        TreeAtlas_DrawStroke(px, w, h, ox, oy, bx, by, ex, ey, 3.8f * (1.0f - shoots[s].t * 0.5f), 1.2f, stemC0, stemC1);

        int nCount = (int)(shoots[s].len * 0.48f);
        for (int i = 0; i < nCount; i++)
        {
            float t = (float)i / (float)(nCount > 1 ? nCount - 1 : 1);
            float nx = bx + (ex - bx) * t + (Tree_NextRand(&rng) - 0.5f) * 3.0f;
            float ny = by + (ey - by) * t + (Tree_NextRand(&rng) - 0.5f) * 3.0f;
            float side = (i & 1) ? -1.0f : 1.0f;
            float nang = angle + side * (0.35f + Tree_NextRand(&rng) * 0.48f);
            float nlen = (54.0f * (1.0f - shoots[s].t * 0.22f)) * (1.0f - t * 0.36f) * (0.78f + Tree_NextRand(&rng) * 0.44f);
            int ci = (int)(Tree_NextRand(&rng) * 5.99f);
            Color tipC = (Tree_NextRand(&rng) > 0.84f) ? frostTip : pal[ci < 5 ? ci + 1 : 5];
            TreeAtlas_DrawStroke(px, w, h, ox, oy, nx, ny, nx + cosf(nang) * nlen, ny + sinf(nang) * nlen, 3.0f, 1.7f, pal[0], tipC);
        }
    }
    TreeAtlas_DrawStroke(px, w, h, ox, oy, sx0, sy0, sx1, sy1, 6.2f, 1.8f, stemC0, stemC1);
    for (int i = 0; i < 115; i++)
    {
        float t = (float)i / 114.0f;
        float nx = sx0 + (Tree_NextRand(&rng) - 0.5f) * 4.5f;
        float ny = (sy0 - 12.0f) + (sy1 - (sy0 - 12.0f)) * t;
        float side = (i & 1) ? -1.0f : 1.0f;
        float nang = -PI * 0.5f + side * (0.32f + Tree_NextRand(&rng) * 0.50f);
        float nlen = 56.0f * (1.0f - t * 0.32f) * (0.78f + Tree_NextRand(&rng) * 0.44f);
        int ci = 1 + (int)(Tree_NextRand(&rng) * 4.99f);
        TreeAtlas_DrawStroke(px, w, h, ox, oy, nx, ny, nx + cosf(nang) * nlen, ny + sinf(nang) * nlen, 3.2f, 1.8f, pal[0], pal[ci]);
    }
}

// Cell 1: Cedar / Taiga Scale-Foliage Spray (preserved)
static void TreeAtlas_BakeCedarCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 5502u;
    const Color pal[5] = {
        {13, 36, 28, 255}, {18, 47, 37, 255}, {23, 58, 45, 255}, {30, 72, 56, 255}, {44, 96, 78, 255}
    };
    const float rayAngs[7] = {-0.56f, -0.34f, -0.14f, 0.02f, 0.18f, 0.38f, 0.58f};
    const float rayLens[7] = {280.0f, 352.0f, 415.0f, 440.0f, 405.0f, 338.0f, 264.0f};
    for (int r = 0; r < 7; r++)
    {
        float ang = -PI * 0.5f + rayAngs[r];
        float bx = 256.0f, by = 488.0f;
        float ex = bx + cosf(ang) * rayLens[r];
        float ey = by + sinf(ang) * rayLens[r];
        TreeAtlas_DrawStroke(px, w, h, ox, oy, bx, by, ex, ey, 3.2f, 1.2f, (Color){45, 32, 24, 255}, pal[1]);
        for (int i = 0; i < 32; i++)
        {
            float t = (float)i / 31.0f;
            float sx = bx + (ex - bx) * t;
            float sy = by + (ey - by) * t;
            for (int sd = -1; sd <= 1; sd += 2)
            {
                float nang = ang + (float)sd * (0.38f + Tree_NextRand(&rng) * 0.42f);
                float nlen = (38.0f + Tree_NextRand(&rng) * 24.0f) * (1.0f - t * 0.42f);
                int ci = (int)(Tree_NextRand(&rng) * 4.99f);
                TreeAtlas_DrawStroke(px, w, h, ox, oy, sx, sy, sx + cosf(nang) * nlen, sy + sinf(nang) * nlen, 3.4f, 1.8f, pal[0], pal[ci]);
            }
        }
    }
}

// Cell 2: Huangshan / Coastal Pine Star-Fascicle Rosettes (Tung Chum Kim)
static void TreeAtlas_BakePineRosetteCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 6109u;
    Color twigA = {68, 46, 32, 255};
    Color twigB = {86, 62, 44, 255};
    Color nDark = {24, 54, 34, 255};
    Color nMid  = {46, 94, 52, 255};
    Color nTip  = {88, 146, 76, 255};

    // Crooked forked pine twig supporting 6 distinct star-burst needle fascicles
    float bx = 256.0f, by = 486.0f;
    float mx = 248.0f, my = 328.0f;
    TreeAtlas_DrawStroke(px, w, h, ox, oy, bx, by, mx, my, 5.5f, 3.6f, twigA, twigB);

    const float centers[6][3] = {
        {138.0f, 325.0f, 84.0f},
        {374.0f, 318.0f, 82.0f},
        {158.0f, 198.0f, 92.0f},
        {354.0f, 192.0f, 90.0f},
        {256.0f, 244.0f, 88.0f},
        {256.0f, 128.0f, 96.0f},
    };
    for (int k = 0; k < 6; k++)
    {
        float cx = centers[k][0];
        float cy = centers[k][1];
        float rad = centers[k][2];
        TreeAtlas_DrawStroke(px, w, h, ox, oy, mx, my, cx, cy, 3.0f, 1.8f, twigA, twigB);

        // Radiating pine needle fascicles (half-fan to full star pom-pom)
        int needles = 34;
        for (int n = 0; n < needles; n++)
        {
            float a = (float)n * (2.0f * PI / (float)needles) + (Tree_NextRand(&rng) - 0.5f) * 0.16f;
            float nlen = rad * (0.74f + Tree_NextRand(&rng) * 0.34f);
            Color tipC = (Tree_NextRand(&rng) > 0.35f) ? nTip : nMid;
            TreeAtlas_DrawStroke(px, w, h, ox, oy, cx, cy,
                                 cx + cosf(a) * nlen, cy + sinf(a) * nlen,
                                 3.1f, 1.7f, nDark, tipC);
        }
        TreeAtlas_DrawDisc(px, w, h, ox, oy, cx, cy, 9.0f, twigA, nDark);
    }
}

// Cell 3: Xianxia Peach Blossom 5-Petal Bouquet Spray (Dao Hoa / Mai Hoa)
// Rich multi-tone sakura/peach petals (deep crimson hearts -> rose-peach -> blush-ivory tips) + tender green loc non buds.
static void TreeAtlas_BakePeachBlossomCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 6617u;
    Color twigA     = {72, 46, 48, 255};
    Color twigB     = {96, 62, 64, 255};
    Color petalCore = {232, 110, 152, 255};
    Color petalMid  = {248, 166, 196, 255};
    Color petalTip  = {255, 246, 250, 255};
    Color stamenCol = {158, 30, 74, 255};
    Color budCore   = {192, 44, 92, 255};
    Color budTip    = {244, 138, 174, 255};
    Color leafBase  = {92, 146, 58, 255};
    Color leafTip   = {156, 204, 96, 255};

    // Slender crooked dark-plum twigs hidden gently inside the floral bouquet
    float p0x = 256.0f, p0y = 480.0f;
    float p1x = 244.0f, p1y = 338.0f;
    float p2x = 266.0f, p2y = 204.0f;
    float p3x = 254.0f, p3y = 92.0f;
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p0x, p0y, p1x, p1y, 4.2f, 3.0f, twigA, twigB);
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p1x, p1y, p2x, p2y, 3.0f, 2.0f, twigA, twigB);
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p2x, p2y, p3x, p3y, 2.0f, 1.1f, twigB, twigB);

    const float branches[4][4] = {
        {246.0f, 356.0f, 114.0f, 282.0f},
        {254.0f, 326.0f, 396.0f, 266.0f},
        {262.0f, 224.0f, 128.0f, 154.0f},
        {262.0f, 206.0f, 384.0f, 142.0f},
    };
    for (int b = 0; b < 4; b++)
        TreeAtlas_DrawStroke(px, w, h, ox, oy, branches[b][0], branches[b][1], branches[b][2], branches[b][3], 2.4f, 1.1f, twigA, twigB);

    // 7 delicate tender spring-jade young leaf shoots (loc non) peeking organically around the twigs
    const float shoots[7][4] = {
        {178.0f, 320.0f, -2.35f, 44.0f},
        {332.0f, 296.0f, -0.72f, 42.0f},
        {192.0f, 198.0f, -2.10f, 38.0f},
        {322.0f, 182.0f, -0.95f, 40.0f},
        {248.0f, 392.0f, -1.95f, 44.0f},
        {268.0f, 378.0f, -1.12f, 40.0f},
        {256.0f, 126.0f, -1.55f, 36.0f},
    };
    for (int s = 0; s < 7; s++)
        TreeAtlas_DrawLeaf(px, w, h, ox, oy, shoots[s][0], shoots[s][1], shoots[s][2], shoots[s][3], 12.0f, 0.0f, 0.0f, leafBase, leafTip);

    // 44 rich clustered 5-petal peach blossoms forming a lush, scalloped organic floral cloud (zero Swiss-cheese slabs!)
    const float clusterCenters[7][3] = {
        {148.0f, 294.0f, 76.0f},
        {362.0f, 276.0f, 76.0f},
        {164.0f, 174.0f, 74.0f},
        {348.0f, 162.0f, 74.0f},
        {256.0f, 114.0f, 78.0f},
        {256.0f, 232.0f, 84.0f},
        {252.0f, 354.0f, 68.0f},
    };
    for (int c = 0; c < 7; c++)
    {
        float cx = clusterCenters[c][0];
        float cy = clusterCenters[c][1];
        float spread = clusterCenters[c][2];
        int perCluster = (c == 5) ? 8 : 6;
        for (int k = 0; k < perCluster; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)perCluster) + (Tree_NextRand(&rng) - 0.5f) * 0.55f;
            float dist = (k == 0) ? (Tree_NextRand(&rng) * 12.0f) : (spread * (0.34f + 0.58f * Tree_NextRand(&rng)));
            float fx = cx + cosf(ang) * dist;
            float fy = cy + sinf(ang) * dist;
            float rad = 20.0f + Tree_NextRand(&rng) * 14.0f;
            float rot = Tree_NextRand(&rng) * 2.0f * PI;
            Color pCore = (Tree_NextRand(&rng) > 0.42f) ? petalCore : petalMid;
            TreeAtlas_DrawBlossom(px, w, h, ox, oy, fx, fy, rad, rot, pCore, petalTip, stamenCol);
        }
    }

    // 16 deep-crimson unopened blossom buds accentuating bouquet margins
    for (int i = 0; i < 16; i++)
    {
        int ci = i % 7;
        float a = Tree_NextRand(&rng) * 2.0f * PI;
        float d = clusterCenters[ci][2] * (0.65f + 0.42f * Tree_NextRand(&rng));
        float bx = clusterCenters[ci][0] + cosf(a) * d;
        float by = clusterCenters[ci][1] + sinf(a) * d;
        TreeAtlas_DrawDisc(px, w, h, ox, oy, bx, by, 7.5f + Tree_NextRand(&rng) * 4.5f, budCore, budTip);
    }
}

// Cell 4: Broadleaf Oak / Banyan Airy Lobed Twig Cluster (neutral tintable)
static void TreeAtlas_BakeOakBroadleafCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 6603u;
    Color twigC = {94, 80, 68, 255};
    Color leafCore = {152, 162, 144, 255};
    Color leafMid  = {206, 214, 196, 255};
    Color leafTip  = {248, 252, 242, 255};

    float sx0 = 256.0f, sy0 = 486.0f, sx1 = 256.0f, sy1 = 68.0f;
    TreeAtlas_DrawStroke(px, w, h, ox, oy, sx0, sy0, sx1, sy1, 4.4f, 1.4f, twigC, twigC);

    // 6 alternating airy side-twigs with well-separated lobed leaves (45% open sky holes!)
    for (int b = 0; b < 6; b++)
    {
        float t = 0.16f + 0.74f * ((float)b / 5.0f);
        float side = (b & 1) ? 1.0f : -1.0f;
        float bx = sx0 + (Tree_NextRand(&rng) - 0.5f) * 6.0f;
        float by = sy0 + (sy1 - sy0) * t;
        float ang = -PI * 0.5f + side * (0.52f + Tree_NextRand(&rng) * 0.34f);
        float blen = 158.0f * (1.0f - t * 0.32f) * (0.88f + Tree_NextRand(&rng) * 0.22f);
        float ex = bx + cosf(ang) * blen;
        float ey = by + sinf(ang) * blen;
        TreeAtlas_DrawStroke(px, w, h, ox, oy, bx, by, ex, ey, 2.6f, 1.1f, twigC, twigC);

        for (int l = 0; l < 5; l++)
        {
            float lt = 0.22f + 0.78f * ((float)l / 4.0f);
            float lx = bx + (ex - bx) * lt;
            float ly = by + (ey - by) * lt;
            float lside = (l == 4) ? 0.0f : ((l & 1) ? 1.0f : -1.0f);
            float lang = ang + lside * (0.68f + (Tree_NextRand(&rng) - 0.5f) * 0.24f);
            float llen = 48.0f + Tree_NextRand(&rng) * 20.0f;
            float lwid = 22.0f + Tree_NextRand(&rng) * 9.0f;
            Color cA = (lt < 0.45f) ? leafCore : leafMid;
            TreeAtlas_DrawLeaf(px, w, h, ox, oy, lx, ly, lang, llen, lwid, 5.0f, 0.14f, cA, leafTip);
        }
    }
    // Terminal 5-leaf rosette at apex
    for (int l = 0; l < 5; l++)
    {
        float lang = -PI * 0.5f + ((float)l - 2.0f) * 0.48f;
        TreeAtlas_DrawLeaf(px, w, h, ox, oy, sx1, sy1 + 14.0f, lang, 54.0f, 24.0f, 5.0f, 0.14f, leafMid, leafTip);
    }
}

// Cell 5: Birch / Poplar Dainty Serrated Diamond-Leaf Sprig (very airy)
static void TreeAtlas_BakeBirchSprigCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 7105u;
    Color twigC = {82, 74, 70, 255};
    Color leafCore = {166, 174, 152, 255};
    Color leafTip  = {250, 252, 240, 255};

    float sx0 = 256.0f, sy0 = 486.0f, sx1 = 248.0f, sy1 = 56.0f;
    TreeAtlas_DrawStroke(px, w, h, ox, oy, sx0, sy0, sx1, sy1, 3.2f, 1.0f, twigC, twigC);

    for (int b = 0; b < 8; b++)
    {
        float t = 0.12f + 0.80f * ((float)b / 7.0f);
        float side = (b & 1) ? 1.0f : -1.0f;
        float bx = sx0 + (sx1 - sx0) * t;
        float by = sy0 + (sy1 - sy0) * t;
        float ang = -PI * 0.5f + side * (0.48f + Tree_NextRand(&rng) * 0.36f);
        float blen = 148.0f * (1.0f - t * 0.35f);
        float ex = bx + cosf(ang) * blen;
        float ey = by + sinf(ang) * blen;
        TreeAtlas_DrawStroke(px, w, h, ox, oy, bx, by, ex, ey, 1.8f, 0.9f, twigC, twigC);

        for (int l = 0; l < 4; l++)
        {
            float lt = 0.28f + 0.72f * ((float)l / 3.0f);
            float lx = bx + (ex - bx) * lt;
            float ly = by + (ey - by) * lt;
            float lside = (l == 3) ? 0.0f : ((l & 1) ? 1.0f : -1.0f);
            float lang = ang + lside * 0.58f + (Tree_NextRand(&rng) - 0.5f) * 0.25f;
            TreeAtlas_DrawLeaf(px, w, h, ox, oy, lx, ly, lang, 38.0f + Tree_NextRand(&rng) * 14.0f,
                               17.0f + Tree_NextRand(&rng) * 6.0f, 7.0f, 0.08f, leafCore, leafTip);
        }
    }
}

// Cell 6: Bamboo Slender Lanceolate Leaf Spray (Mac Truc — separated sword-like blades, zero webbed blobs!)
static void TreeAtlas_BakeBambooFanCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 7704u;
    Color twigA    = {52, 78, 38, 255};
    Color twigB    = {72, 102, 50, 255};
    Color leafBase = {92, 134, 68, 255};
    Color leafTip  = {178, 208, 128, 255};

    // Sturdy central bamboo shoot & 4 alternating side branchlets (thick enough to never clip away in mipmaps)
    float p0x = 256.0f, p0y = 486.0f;
    float p1x = 248.0f, p1y = 336.0f;
    float p2x = 262.0f, p2y = 198.0f;
    float p3x = 254.0f, p3y = 92.0f;
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p0x, p0y, p1x, p1y, 4.4f, 3.2f, twigA, twigB);
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p1x, p1y, p2x, p2y, 3.2f, 2.4f, twigA, twigB);
    TreeAtlas_DrawStroke(px, w, h, ox, oy, p2x, p2y, p3x, p3y, 2.4f, 1.6f, twigB, twigB);

    // 5 well-spaced nodal clusters, each with 3 cleanly separated slender lanceolate bamboo blades (>= 32 deg apart)
    typedef struct { float sx, sy, ex, ey; float baseAng; } BambooShoot;
    const BambooShoot shoots[5] = {
        {249.0f, 354.0f, 146.0f, 326.0f, -2.55f},
        {254.0f, 314.0f, 366.0f, 286.0f, -0.58f},
        {258.0f, 224.0f, 152.0f, 196.0f, -2.42f},
        {260.0f, 192.0f, 360.0f, 168.0f, -0.70f},
        {254.0f,  98.0f, 254.0f,  92.0f, -1.57f},
    };
    const float bladeOffs[3] = {-0.62f, 0.0f, 0.62f};
    const float bladeLens[3] = {122.0f, 146.0f, 118.0f};

    for (int s = 0; s < 5; s++)
    {
        if (s < 4)
            TreeAtlas_DrawStroke(px, w, h, ox, oy, shoots[s].sx, shoots[s].sy, shoots[s].ex, shoots[s].ey, 2.8f, 1.8f, twigA, twigB);
        for (int l = 0; l < 3; l++)
        {
            float ang = shoots[s].baseAng + bladeOffs[l] + (Tree_NextRand(&rng) - 0.5f) * 0.10f;
            float len = bladeLens[l] * (0.90f + Tree_NextRand(&rng) * 0.16f);
            // Slender 1:8 lanceolate bamboo blade (half-width 8.5..10px -> crisp sword silhouette, never webbed!)
            float halfW = 8.2f + Tree_NextRand(&rng) * 2.0f;
            float bx = shoots[s].ex + cosf(ang) * 8.0f;
            float by = shoots[s].ey + sinf(ang) * 8.0f;
            TreeAtlas_DrawLeaf(px, w, h, ox, oy, bx, by, ang, len, halfW, 0.0f, 0.0f, leafBase, leafTip);
        }
    }
}

// Cell 7: Weeping Willow / Acacia Pendulous Feather Spray
static void TreeAtlas_BakeWillowWhipCell(unsigned char *px, int w, int h, int ox, int oy)
{
    unsigned int rng = 8211u;
    Color stemC = {98, 126, 62, 255};
    Color leafBase = {148, 186, 104, 255};
    Color leafTip  = {224, 240, 168, 255};

    // 3 gracefully curved pendulous willow whips with fuller mip-safe alternating leaves
    const float startX[3] = {144.0f, 256.0f, 368.0f};
    for (int s = 0; s < 3; s++)
    {
        float x0 = startX[s];
        float x1 = x0 + (Tree_NextRand(&rng) - 0.5f) * 44.0f;
        TreeAtlas_DrawStroke(px, w, h, ox, oy, x0, 26.0f, x1, 486.0f, 2.6f, 1.1f, stemC, stemC);
        for (int l = 0; l < 16; l++)
        {
            float t = (float)l / 15.0f;
            float lx = x0 + (x1 - x0) * t;
            float ly = 36.0f + t * 430.0f;
            float side = (l & 1) ? 1.0f : -1.0f;
            float lang = PI * 0.5f + side * (0.38f + Tree_NextRand(&rng) * 0.28f);
            TreeAtlas_DrawLeaf(px, w, h, ox, oy, lx, ly, lang,
                               58.0f + Tree_NextRand(&rng) * 20.0f,
                               11.0f + Tree_NextRand(&rng) * 3.5f,
                               0.0f, 0.0f, leafBase, leafTip);
        }
    }
}

// 5-pass RGB border dilation into alpha==0 texels (AGENT_CODE_STANDARD.md §3)
// Prevents dark (0,0,0) halos around pink blossoms & bright leaves under trilinear mipmapping.
static void TreeAtlas_DilateTransparentRGB(unsigned char *px, int w, int h, int passes)
{
    size_t nBytes = (size_t)w * (size_t)h * 4u;
    unsigned char *tmp = (unsigned char *)MemAlloc((unsigned int)nBytes);
    if (!tmp)
        return;

    // Seed an occupancy mask (1 = has valid RGB) so dilation propagates outward per pass
    unsigned char *valid = (unsigned char *)MemAlloc((unsigned int)((size_t)w * (size_t)h));
    if (!valid)
    {
        MemFree(tmp);
        return;
    }
    for (int i = 0; i < w * h; i++)
        valid[i] = (px[i * 4 + 3] > 8) ? 1u : 0u;

    for (int pass = 0; pass < passes; pass++)
    {
        memcpy(tmp, px, nBytes);
        for (int y = 1; y < h - 1; y++)
        {
            for (int x = 1; x < w - 1; x++)
            {
                int idx = y * w + x;
                if (valid[idx])
                    continue;
                int sr = 0, sg = 0, sb = 0, cnt = 0;
                const int offs[4] = {-1, 1, -w, w};
                for (int k = 0; k < 4; k++)
                {
                    int nIdx = idx + offs[k];
                    if (valid[nIdx])
                    {
                        sr += px[nIdx * 4 + 0];
                        sg += px[nIdx * 4 + 1];
                        sb += px[nIdx * 4 + 2];
                        cnt++;
                    }
                }
                if (cnt > 0)
                {
                    tmp[idx * 4 + 0] = (unsigned char)(sr / cnt);
                    tmp[idx * 4 + 1] = (unsigned char)(sg / cnt);
                    tmp[idx * 4 + 2] = (unsigned char)(sb / cnt);
                }
            }
        }
        memcpy(px, tmp, nBytes);
        for (int y = 1; y < h - 1; y++)
        {
            for (int x = 1; x < w - 1; x++)
            {
                int idx = y * w + x;
                if (!valid[idx] && (px[idx * 4 + 0] | px[idx * 4 + 1] | px[idx * 4 + 2]) != 0)
                    valid[idx] = 1u;
            }
        }
    }

    MemFree(valid);
    MemFree(tmp);
}

static Texture2D s_treeFoliageAtlas = {0};

static Texture2D Tree_GetFoliageAtlas(void)
{
    if (s_treeFoliageAtlas.id != 0)
        return s_treeFoliageAtlas;

    unsigned char *px = (unsigned char *)MemAlloc((unsigned int)(TREE_ATLAS_W * TREE_ATLAS_H * 4));
    if (!px)
        return (Texture2D){0};
    memset(px, 0, (size_t)TREE_ATLAS_W * (size_t)TREE_ATLAS_H * 4u);

    TreeAtlas_BakeSpruceCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 0, 0);
    TreeAtlas_BakeCedarCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 512, 0);
    TreeAtlas_BakePineRosetteCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 1024, 0);
    TreeAtlas_BakePeachBlossomCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 1536, 0);
    TreeAtlas_BakeOakBroadleafCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 0, 512);
    TreeAtlas_BakeBirchSprigCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 512, 512);
    TreeAtlas_BakeBambooFanCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 1024, 512);
    TreeAtlas_BakeWillowWhipCell(px, TREE_ATLAS_W, TREE_ATLAS_H, 1536, 512);

    TreeAtlas_DilateTransparentRGB(px, TREE_ATLAS_W, TREE_ATLAS_H, 5);

    Image img = {
        .data = px,
        .width = TREE_ATLAS_W,
        .height = TREE_ATLAS_H,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    s_treeFoliageAtlas = LoadTextureFromImage(img);
    MemFree(px);
    if (s_treeFoliageAtlas.id != 0)
    {
        GenTextureMipmaps(&s_treeFoliageAtlas);
        SetTextureFilter(s_treeFoliageAtlas, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(s_treeFoliageAtlas, TEXTURE_WRAP_CLAMP);
    }
    return s_treeFoliageAtlas;
}

// =========================================================================
// STAGE 1: TRUNK & MULTI-ORDER BRANCH SKELETON GENERATOR (TreeSkeleton)
//   - Bishop Parallel-Transport continuous swept spline tubes (zero banding!)
//   - Exponential root buttress flare & grounding root spurs
//   - Da Vinci pipe-model multi-order branching (Order 0..3)
// =========================================================================

#define TREE_MAX_TWIG_ANCHORS 384
#define TREE_MAX_CANOPY_LOBES 32

typedef struct
{
    Vector3 pos;
    Vector3 dir;
    Vector3 up;
    float radius;
    float crownHeight01;
    float compliance;
    int order;
    int lobeGroup;
} TreeTwigAnchor;

typedef struct
{
    Vector3 center;
    float rx;
    float ry;
    float rz;
    float normalBlend;
    int cell;
    Color colCore;
    Color colTip;
} TreeCanopyLobe;

typedef struct
{
    TreeTwigAnchor anchors[TREE_MAX_TWIG_ANCHORS];
    int anchorCount;
    TreeCanopyLobe lobes[TREE_MAX_CANOPY_LOBES];
    int lobeCount;
    Vector3 crownCenter;
    float crownHeight;
} TreeSkeleton;

static void TreeSkeleton_AddAnchor(TreeSkeleton *sk, Vector3 pos, Vector3 dir,
                                   float radius, float crownH01, float compliance,
                                   int order, int lobeGroup)
{
    if (!sk || sk->anchorCount >= TREE_MAX_TWIG_ANCHORS)
        return;
    Vector3 d = Vector3LengthSqr(dir) > 1e-6f ? Vector3Normalize(dir) : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 ref = (fabsf(d.y) < 0.92f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(ref, d));
    Vector3 up = Vector3CrossProduct(d, right);
    sk->anchors[sk->anchorCount++] = (TreeTwigAnchor){
        .pos = pos,
        .dir = d,
        .up = up,
        .radius = radius,
        .crownHeight01 = fminf(1.0f, fmaxf(0.0f, crownH01)),
        .compliance = fminf(1.0f, fmaxf(0.0f, compliance)),
        .order = order,
        .lobeGroup = lobeGroup
    };
}

static int TreeSkeleton_AddLobe(TreeSkeleton *sk, Vector3 center, float rx, float ry, float rz,
                                float normalBlend, int cell, Color colCore, Color colTip)
{
    if (!sk || sk->lobeCount >= TREE_MAX_CANOPY_LOBES)
        return (sk && sk->lobeCount > 0) ? (sk->lobeCount - 1) : 0;
    int idx = sk->lobeCount++;
    sk->lobes[idx] = (TreeCanopyLobe){
        .center = center,
        .rx = fmaxf(0.25f, rx),
        .ry = fmaxf(0.25f, ry),
        .rz = fmaxf(0.25f, rz),
        .normalBlend = fminf(1.0f, fmaxf(0.0f, normalBlend)),
        .cell = cell,
        .colCore = colCore,
        .colTip = colTip
    };
    return idx;
}

static void Tree_BasisFromDir(Vector3 dir, Vector3 *outRight, Vector3 *outFwd)
{
    Vector3 up = Vector3Normalize(dir);
    Vector3 ref = (fabsf(up.y) < 0.92f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    *outRight = Vector3Normalize(Vector3CrossProduct(ref, up));
    *outFwd = Vector3CrossProduct(up, *outRight);
}

// Minimal-twist Bishop parallel transport of (right, fwd) from oldT to newT
static void Tree_TransportFrame(Vector3 oldT, Vector3 newT, Vector3 *ioRight, Vector3 *ioFwd)
{
    Vector3 axis = Vector3CrossProduct(oldT, newT);
    float sinLen = Vector3Length(axis);
    float cosAng = fminf(1.0f, fmaxf(-1.0f, Vector3DotProduct(oldT, newT)));
    if (sinLen > 1e-5f)
    {
        Vector3 k = Vector3Scale(axis, 1.0f / sinLen);
        float ang = atan2f(sinLen, cosAng);
        float ca = cosf(ang), sa = sinf(ang);
        Vector3 r = *ioRight;
        // Rodrigues rotation
        Vector3 kXr = Vector3CrossProduct(k, r);
        float kDr = Vector3DotProduct(k, r);
        r = Vector3Add(Vector3Add(Vector3Scale(r, ca), Vector3Scale(kXr, sa)), Vector3Scale(k, kDr * (1.0f - ca)));
        // Re-orthogonalize against newT
        r = Vector3Subtract(r, Vector3Scale(newT, Vector3DotProduct(r, newT)));
        if (Vector3LengthSqr(r) > 1e-6f)
            *ioRight = Vector3Normalize(r);
    }
    *ioFwd = Vector3Normalize(Vector3CrossProduct(newT, *ioRight));
}

// Continuous parallel-transport swept bark tube along pts[0..ptCount-1].
// uv.x = -1.0 - 0.25 * branchOrder (bark sentinel + order); uv.y = continuous vStart..vEnd (zero ring banding!).
static void Tree_SweepBranchSpline(TreeMeshBuilder *b, const Vector3 *pts, const float *rads, int ptCount,
                                   int sides, Color colRoot, Color colTip,
                                   float compRoot, float compTip,
                                   float vStart, float vEnd, int branchOrder,
                                   float rootFlare, float flareLobes, bool capTip)
{
    if (ptCount < 2 || sides < 3 || ptCount > 24 || sides > 12)
        return;

    Vector3 ringP[24][13];
    Vector3 ringN[24][13];
    Vector2 ringUV[24];
    Color ringC[24];

    Vector3 tangents[24];
    for (int i = 0; i < ptCount; i++)
    {
        Vector3 d;
        if (i == 0)
            d = Vector3Subtract(pts[1], pts[0]);
        else if (i == ptCount - 1)
            d = Vector3Subtract(pts[i], pts[i - 1]);
        else
            d = Vector3Subtract(pts[i + 1], pts[i - 1]);
        tangents[i] = (Vector3LengthSqr(d) > 1e-6f) ? Vector3Normalize(d) : (Vector3){0.0f, 1.0f, 0.0f};
    }

    Vector3 right, fwd;
    Tree_BasisFromDir(tangents[0], &right, &fwd);
    float barkSentinelX = -1.0f - 0.25f * (float)branchOrder;

    for (int i = 0; i < ptCount; i++)
    {
        if (i > 0)
            Tree_TransportFrame(tangents[i - 1], tangents[i], &right, &fwd);

        float t = (float)i / (float)(ptCount - 1);
        float vCoord = vStart + (vEnd - vStart) * t;
        float comp = compRoot + (compTip - compRoot) * t;
        Color c = Tree_LerpColor(colRoot, colTip, t);
        c.a = (unsigned char)fminf(255.0f, fmaxf(0.0f, comp * 255.0f));
        ringC[i] = c;
        ringUV[i] = (Vector2){barkSentinelX, vCoord};

        float flareDecay = (rootFlare > 0.01f) ? expf(-t * 4.6f) : 0.0f;
        // Early-Z gnarled bark silhouette displacement (replaces fragment silhouette clipping at 0 pixel cost)
        bool gnarlBark = (branchOrder <= 1 && flareLobes >= 4.5f);
        float gnarlAmp = gnarlBark ? (0.085f * (1.0f - t * 0.45f)) : 0.0f;
        for (int j = 0; j <= sides; j++)
        {
            float ang = ((float)(j % sides) / (float)sides) * (2.0f * PI);
            float ca = cosf(ang), sa = sinf(ang);
            Vector3 radial = Vector3Add(Vector3Scale(right, ca), Vector3Scale(fwd, sa));
            Vector3 tangentTheta = Vector3Add(Vector3Scale(right, -sa), Vector3Scale(fwd, ca));
            float flute = (flareDecay > 0.01f)
                              ? (1.0f + rootFlare * flareDecay * (0.58f + 0.42f * cosf(ang * flareLobes + 0.4f)))
                              : 1.0f;
            float gnarl = 1.0f;
            float dGnarl = 0.0f;
            if (gnarlAmp > 0.001f)
            {
                float p1 = ang * 3.0f + t * 5.2f;
                float p2 = ang * 5.0f - t * 8.4f;
                gnarl = 1.0f + gnarlAmp * (0.62f * sinf(p1) + 0.38f * cosf(p2));
                dGnarl = gnarlAmp * (0.62f * 3.0f * cosf(p1) - 0.38f * 5.0f * sinf(p2));
            }
            float rad = rads[i] * flute * gnarl;
            ringP[i][j] = Vector3Add(pts[i], Vector3Scale(radial, rad));
            // Analytical azimuthal normal perturbation from bark fluting
            Vector3 perturbedN = Vector3Subtract(radial, Vector3Scale(tangentTheta, dGnarl * 0.55f));
            ringN[i][j] = Vector3Normalize(perturbedN);
        }
    }

    for (int i = 0; i < ptCount - 1; i++)
    {
        for (int j = 0; j < sides; j++)
        {
            TreeBuilder_PushTri(b,
                                ringP[i][j], ringN[i][j], ringUV[i], ringC[i],
                                ringP[i + 1][j], ringN[i + 1][j], ringUV[i + 1], ringC[i + 1],
                                ringP[i + 1][j + 1], ringN[i + 1][j + 1], ringUV[i + 1], ringC[i + 1]);
            TreeBuilder_PushTri(b,
                                ringP[i][j], ringN[i][j], ringUV[i], ringC[i],
                                ringP[i + 1][j + 1], ringN[i + 1][j + 1], ringUV[i + 1], ringC[i + 1],
                                ringP[i][j + 1], ringN[i][j + 1], ringUV[i], ringC[i]);
        }
    }

    if (capTip)
    {
        int last = ptCount - 1;
        Vector3 apex = Vector3Add(pts[last], Vector3Scale(tangents[last], fmaxf(rads[last] * 2.2f, 0.04f)));
        for (int j = 0; j < sides; j++)
        {
            TreeBuilder_PushTri(b,
                                ringP[last][j], ringN[last][j], ringUV[last], ringC[last],
                                apex, tangents[last], ringUV[last], ringC[last],
                                ringP[last][j + 1], ringN[last][j + 1], ringUV[last], ringC[last]);
        }
    }
}

static Vector3 Tree_EvalQuadBezier(Vector3 p0, Vector3 p1, Vector3 p2, float t)
{
    float u = 1.0f - t;
    return (Vector3){
        u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
        u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y,
        u * u * p0.z + 2.0f * u * t * p1.z + t * t * p2.z
    };
}

// Helper: emit a smooth curved quadratic-Bezier branch + optional secondary/tertiary forks
static void Tree_BuildCurvedLimb(TreeMeshBuilder *b, TreeSkeleton *sk,
                                 Vector3 p0, Vector3 p1, Vector3 p2,
                                 float r0, float r1, int segs, int sides,
                                 Color barkA, Color barkB,
                                 float comp0, float comp1, float v0, float v1,
                                 int order, int lobeGroup, float crownH01)
{
    if (segs < 2) segs = 2;
    if (segs > 10) segs = 10;
    Vector3 pts[11];
    float rads[11];
    // Collar tuck: step start slightly backward along tangent so branch seats cleanly inside parent tube
    Vector3 startTan = Vector3Normalize(Vector3Subtract(p1, p0));
    pts[0] = Vector3Subtract(p0, Vector3Scale(startTan, r0 * 0.45f));
    rads[0] = r0 * 1.12f;
    for (int i = 1; i <= segs; i++)
    {
        float t = (float)i / (float)segs;
        pts[i] = Tree_EvalQuadBezier(p0, p1, p2, t);
        rads[i] = r0 + (r1 - r0) * powf(t, 0.85f);
    }
    Tree_SweepBranchSpline(b, pts, rads, segs + 1, sides, barkA, barkB,
                           comp0, comp1, v0, v1, order, 0.0f, 0.0f, order >= 2);
    if (sk)
    {
        Vector3 tipDir = Vector3Normalize(Vector3Subtract(pts[segs], pts[segs - 1]));
        TreeSkeleton_AddAnchor(sk, pts[segs], tipDir, r1, crownH01, comp1, order, lobeGroup);
    }
}

// =========================================================================
// STAGE 2 & 3b: CANOPY VOLUME SOLVER & 3D CUPPED SPRIG EMITTER
// =========================================================================

// Emits a single arched & cupped 3D sprig card with spherized lobe normal transfer,
// analytical canopy shell AO, and integer-packed (leafRank, canopyShell) in UV.xy.
static void Tree_EmitSprigCard(TreeMeshBuilder *b, Vector3 origin,
                               float yaw, float pitch, float roll,
                               float length, float width, float droop, float cup,
                               const TreeCanopyLobe *lobe, float leafRank,
                               float windFlex, int lenSegs, Color tintMod)
{
    int cell = lobe ? lobe->cell : TREE_CELL_OAK_BROADLEAF;
    int colIdx = cell & 3;
    int rowIdx = cell >> 2;
    float uMin = (float)colIdx * 0.25f + 0.0045f;
    float uMax = (float)(colIdx + 1) * 0.25f - 0.0045f;
    float vMin = (float)rowIdx * 0.50f + 0.0090f;
    float vMax = (float)(rowIdx + 1) * 0.50f - 0.0090f;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);

    Vector3 fwd = {sy * cp, -sp, cy * cp};
    Vector3 baseRight = {cy, 0.0f, -sy};
    Vector3 baseUp = Vector3CrossProduct(fwd, baseRight);
    Vector3 right = Vector3Add(Vector3Scale(baseRight, cr), Vector3Scale(baseUp, sr));
    Vector3 up = Vector3Subtract(Vector3Scale(baseUp, cr), Vector3Scale(baseRight, sr));

    int rankBucket = (int)fminf(63.0f, fmaxf(0.0f, floorf(leafRank * 63.99f)));
    // Evaluate lobe shell at card midpoint so every vertex of the card shares ONE integer bucket (clean mip gradients!)
    Vector3 cardMid = Vector3Add(origin, Vector3Scale(fwd, length * 0.55f));
    cardMid.y -= droop * 0.30f;
    float shell01 = 0.65f;
    if (lobe)
    {
        Vector3 rel = {
            (cardMid.x - lobe->center.x) / lobe->rx,
            (cardMid.y - lobe->center.y) / lobe->ry,
            (cardMid.z - lobe->center.z) / lobe->rz
        };
        float rDist = fminf(1.0f, Vector3Length(rel));
        float hBias = MapMath_SmoothStep01(-1.0f, 1.0f, rel.y);
        shell01 = fminf(1.0f, fmaxf(0.0f, rDist * 0.58f + hBias * 0.42f));
    }
    int shellBucket = (int)fminf(63.0f, fmaxf(0.0f, floorf(shell01 * 63.99f)));

    Vector3 gridP[4][3];
    Vector3 gridN[4][3];
    Vector2 gridUV[4][3];
    Color gridC[4][3];

    int rows = (lenSegs < 1) ? 1 : (lenSegs > 3 ? 3 : lenSegs);
    Color baseCol = lobe ? Tree_LerpColor(lobe->colCore, lobe->colTip, shell01 * 0.55f) : WHITE;
    Color tipCol  = lobe ? Tree_LerpColor(lobe->colCore, lobe->colTip, fminf(1.0f, 0.45f + shell01 * 0.55f)) : WHITE;

    for (int r = 0; r <= rows; r++)
    {
        float t = (float)r / (float)rows;
        float yDrop = -droop * t * t;
        // Petiole-pinned wind compliance: base stays anchored to parent twig, tip flutters freely
        float flex = windFlex * (0.28f + 0.72f * t);
        unsigned char ca = (unsigned char)fminf(255.0f, fmaxf(0.0f, flex * 255.0f));
        Color rowC = Tree_LerpColor(baseCol, tipCol, t);
        Color col = {
            (unsigned char)fminf(255.0f, (float)rowC.r * ((float)tintMod.r / 255.0f)),
            (unsigned char)fminf(255.0f, (float)rowC.g * ((float)tintMod.g / 255.0f)),
            (unsigned char)fminf(255.0f, (float)rowC.b * ((float)tintMod.b / 255.0f)),
            ca
        };
        float vCoord = vMax + (vMin - vMax) * t;

        for (int c = 0; c <= 2; c++)
        {
            float s = (float)c - 1.0f; // -1, 0, +1
            float cupLift = -cup * (s * s) * (0.35f + 0.65f * sinf(t * PI));
            Vector3 pos = Vector3Add(origin, Vector3Scale(fwd, length * t));
            pos = Vector3Add(pos, Vector3Scale(right, s * width * 0.5f));
            pos = Vector3Add(pos, Vector3Scale(up, cupLift));
            pos.y += yDrop;

            Vector3 nGeom = Vector3Normalize(Vector3Add(up, Vector3Add(Vector3Scale(right, s * 0.25f), (Vector3){0.0f, 0.30f, 0.0f})));
            Vector3 nFinal = nGeom;
            if (lobe && lobe->normalBlend > 0.01f)
            {
                Vector3 rel = {
                    (pos.x - lobe->center.x) / lobe->rx,
                    (pos.y - lobe->center.y) / lobe->ry,
                    (pos.z - lobe->center.z) / lobe->rz
                };
                if (Vector3LengthSqr(rel) > 1e-4f)
                {
                    Vector3 nLobe = Vector3Normalize(rel);
                    nFinal = Vector3Normalize(Vector3Lerp(nGeom, nLobe, lobe->normalBlend));
                }
            }

            gridP[r][c] = pos;
            gridN[r][c] = nFinal;
            gridUV[r][c] = (Vector2){
                (float)rankBucket + uMin + (uMax - uMin) * ((float)c * 0.5f),
                (float)shellBucket + vCoord
            };
            gridC[r][c] = col;
        }
    }

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < 2; c++)
        {
            TreeBuilder_PushTri(b, gridP[r][c], gridN[r][c], gridUV[r][c], gridC[r][c],
                                   gridP[r + 1][c], gridN[r + 1][c], gridUV[r + 1][c], gridC[r + 1][c],
                                   gridP[r + 1][c + 1], gridN[r + 1][c + 1], gridUV[r + 1][c + 1], gridC[r + 1][c + 1]);
            TreeBuilder_PushTri(b, gridP[r][c], gridN[r][c], gridUV[r][c], gridC[r][c],
                                   gridP[r + 1][c + 1], gridN[r + 1][c + 1], gridUV[r + 1][c + 1], gridC[r + 1][c + 1],
                                   gridP[r][c + 1], gridN[r][c + 1], gridUV[r][c + 1], gridC[r][c + 1]);
        }
    }
}

// 3-card volumetric conifer bough spray (preserved silhouette for mau_son Alpine Conifers)
static void Tree_EmitConiferBough(TreeMeshBuilder *b, Vector3 origin, float yaw, float pitch,
                                  float length, float width, float droop,
                                  const TreeCanopyLobe *lobe, float leafRank,
                                  Color tintMod, float windFlex, int lod, unsigned int *rng)
{
    int segs = (lod == 0) ? 2 : 1;
    Tree_EmitSprigCard(b, origin, yaw, pitch, (Tree_NextRand(rng) - 0.5f) * 0.14f,
                       length, width, droop, width * 0.22f, lobe, leafRank, windFlex, segs, tintMod);
    if (lod <= 1)
    {
        for (int sd = -1; sd <= 1; sd += 2)
        {
            Vector3 fOrig = {origin.x + sinf(yaw) * length * 0.14f, origin.y - 0.05f, origin.z + cosf(yaw) * length * 0.14f};
            float subRank = fminf(0.99f, leafRank + (sd < 0 ? 0.08f : -0.05f));
            Tree_EmitSprigCard(b, fOrig,
                               yaw + (float)sd * (0.28f + Tree_NextRand(rng) * 0.16f),
                               pitch - 0.14f,
                               (float)sd * (0.48f + Tree_NextRand(rng) * 0.18f),
                               length * (0.82f + Tree_NextRand(rng) * 0.14f),
                               width * 0.84f, droop * 1.08f, width * 0.18f,
                               lobe, fmaxf(0.02f, subRank), windFlex, 1, tintMod);
        }
    }
}

// Multi-angle 3D cupped sprig tuft anchored on a twig tip inside a TreeCanopyLobe
static void Tree_EmitVolumetricSprigTuft(TreeMeshBuilder *b, Vector3 anchor, float baseYaw,
                                         float cardLen, float cardWid, float droop,
                                         const TreeCanopyLobe *lobe, float baseRank,
                                         float windFlex, int lod, unsigned int *rng)
{
    int count = (lod == 0) ? 4 : (lod == 1 ? 2 : 1);
    int segs = (lod == 0) ? 2 : 1;
    for (int i = 0; i < count; i++)
    {
        float yaw = baseYaw + (float)i * (2.0f * PI / (float)count) + (Tree_NextRand(rng) - 0.5f) * 0.42f;
        // Diverse pitches & rolls (including a soft dome-cap plane) prevent gaps from top-down isometric views
        float pitch = (i == 0) ? (-0.08f + (Tree_NextRand(rng) - 0.5f) * 0.14f)
                               : (-0.34f + (float)(i & 1) * 0.48f + (Tree_NextRand(rng) - 0.5f) * 0.22f);
        float roll = ((i == 0) ? 0.14f : ((i == 1) ? -0.58f : ((i == 2) ? 0.62f : -0.32f))) + (Tree_NextRand(rng) - 0.5f) * 0.16f;
        Vector3 orig = {
            anchor.x + (Tree_NextRand(rng) - 0.5f) * cardLen * 0.24f,
            anchor.y + (Tree_NextRand(rng) - 0.42f) * cardLen * 0.20f,
            anchor.z + (Tree_NextRand(rng) - 0.5f) * cardLen * 0.24f
        };
        float len = cardLen * (0.92f + Tree_NextRand(rng) * 0.28f);
        float wid = cardWid * (0.90f + Tree_NextRand(rng) * 0.26f);
        float tv = 0.90f + Tree_NextRand(rng) * 0.16f;
        Color tintMod = {
            (unsigned char)fminf(255.0f, 255.0f * tv),
            (unsigned char)fminf(255.0f, 255.0f * tv),
            (unsigned char)fminf(255.0f, 255.0f * tv),
            255
        };
        float rank = fminf(0.98f, fmaxf(0.02f, baseRank + (Tree_NextRand(rng) - 0.5f) * 0.16f));
        Tree_EmitSprigCard(b, orig, yaw, pitch, roll, len, wid, droop, wid * 0.26f,
                           lobe, rank, windFlex, segs, tintMod);
    }
}

// Dense 3D billowing floral cloud puff for Xianxia Peach Blossom (lush from top-down isometric & eye-level views!)
static void Tree_EmitBlossomPuff(TreeMeshBuilder *b, Vector3 puffCenter, float puffRad,
                                 const TreeCanopyLobe *macroLobe, float baseRank,
                                 float windFlex, int lod, unsigned int *rng)
{
    int cards = (lod == 0) ? 6 : (lod == 1 ? 3 : 2);
    int segs = (lod == 0) ? 2 : 1;
    float sizeScale = (lod == 0) ? 1.0f : ((lod == 1) ? 1.24f : 1.52f);

    for (int i = 0; i < cards; i++)
    {
        float yaw = (float)i * (2.0f * PI / (float)cards) + (Tree_NextRand(rng) - 0.5f) * 0.45f;
        // Card 0 & 1 cap the top/upper dome softly for lush top-down isometric coverage;
        // Cards 2..5 wrap around the sides & lower flanks for full 3D cloud volume.
        float pitch, roll, yOff, rOff;
        if (i == 0)
        {
            pitch = -0.05f + (Tree_NextRand(rng) - 0.5f) * 0.14f;
            roll = (Tree_NextRand(rng) - 0.5f) * 0.22f;
            yOff = puffRad * 0.26f;
            rOff = puffRad * 0.18f;
        }
        else if (i == 1 && cards >= 4)
        {
            pitch = 0.12f + (Tree_NextRand(rng) - 0.5f) * 0.16f;
            roll = 0.28f + (Tree_NextRand(rng) - 0.5f) * 0.18f;
            yOff = puffRad * 0.18f;
            rOff = puffRad * 0.26f;
        }
        else
        {
            pitch = -0.36f + (float)(i & 1) * 0.62f + (Tree_NextRand(rng) - 0.5f) * 0.22f;
            roll = ((i & 1) ? 0.54f : -0.54f) + (Tree_NextRand(rng) - 0.5f) * 0.20f;
            yOff = (Tree_NextRand(rng) - 0.55f) * puffRad * 0.34f;
            rOff = puffRad * (0.20f + 0.26f * Tree_NextRand(rng));
        }

        Vector3 orig = {
            puffCenter.x - sinf(yaw) * (puffRad * 0.36f) + cosf(yaw) * (rOff * 0.4f),
            puffCenter.y + yOff,
            puffCenter.z - cosf(yaw) * (puffRad * 0.36f) - sinf(yaw) * (rOff * 0.4f)
        };
        float len = puffRad * (1.28f + Tree_NextRand(rng) * 0.28f) * sizeScale;
        float wid = puffRad * (1.16f + Tree_NextRand(rng) * 0.26f) * sizeScale;
        float tv = 0.92f + Tree_NextRand(rng) * 0.14f;
        Color tintMod = {
            (unsigned char)fminf(255.0f, 255.0f * tv),
            (unsigned char)fminf(255.0f, 255.0f * tv),
            (unsigned char)fminf(255.0f, 255.0f * tv),
            255
        };
        float rank = fminf(0.98f, fmaxf(0.02f, baseRank + (Tree_NextRand(rng) - 0.5f) * 0.18f));
        Tree_EmitSprigCard(b, orig, yaw, pitch, roll, len, wid, 0.18f, wid * 0.28f,
                           macroLobe, rank, windFlex, segs, tintMod);
    }
}

// Dedicated Hero Xianxia Peach Blossom Tree (Dao Co Thu):
// Leaning gnarled dark-plum trunk, wide tiered arching boughs, crooked secondary forks,
// and billowing 3D multi-tone blossom cloud puffs (full from top-down & eye-level, sheds cleanly when dry!).
static void Tree_BuildPeachBlossom(TreeMeshBuilder *b, TreeSkeleton *sk, int lod, unsigned int *rng)
{
    int trunkSides = (lod == 0) ? 8 : (lod == 1 ? 6 : 4);
    int boughSides = (lod == 0) ? 5 : 4;

    Color barkA = {50, 32, 34, 255};
    Color barkB = {78, 52, 50, 255};
    // Warm-shaded plum-rose interior to luminous blush-ivory exterior so Cell 3's crimson hearts & green loc non pop!
    Color colCore = {174, 116, 144, 255};
    Color colTip  = {255, 248, 250, 255};

    // Stage 1: Gnarled leaning dark-plum trunk (low fork at ~2.2m for classic wide Dao Co Thu poise)
    int rings = (lod == 0) ? 9 : 6;
    Vector3 spine[10];
    float spineR[10];
    float trunkH = 2.35f;
    float baseR = 0.35f;
    for (int s = 0; s < rings; s++)
    {
        float t = (float)s / (float)(rings - 1);
        float sx = 0.42f * t + 0.18f * sinf(t * PI * 1.4f);
        float sz = 0.22f * sinf(t * PI * 1.1f);
        spine[s] = (Vector3){sx, -0.25f + t * (trunkH + 0.25f), sz};
        spineR[s] = baseR * powf(1.0f - t * 0.48f, 0.72f);
    }
    Tree_SweepBranchSpline(b, spine, spineR, rings, trunkSides, barkA, barkB,
                           0.0f, 0.20f, 0.0f, 0.42f, 0, 0.56f, 5.0f, false);

    // 4 smooth surface-hugging root buttresses
    if (lod <= 1)
    {
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.38f + (Tree_NextRand(rng) - 0.5f) * 0.25f;
            Vector3 rp[4] = {
                {spine[0].x + cosf(a) * 0.16f, 0.34f, spine[0].z + sinf(a) * 0.16f},
                {spine[0].x + cosf(a) * 0.42f, 0.12f, spine[0].z + sinf(a) * 0.42f},
                {spine[0].x + cosf(a) * 0.70f, -0.06f, spine[0].z + sinf(a) * 0.70f},
                {spine[0].x + cosf(a) * 0.94f, -0.32f, spine[0].z + sinf(a) * 0.94f}
            };
            float rr[4] = {0.14f, 0.095f, 0.055f, 0.022f};
            Tree_SweepBranchSpline(b, rp, rr, 4, 5, barkA, barkA, 0.0f, 0.0f, 0.0f, 0.14f, 1, 0.0f, 0.0f, true);
        }
    }

    // Unified macro crown dome lobe for coherent sunlit-top -> plum-shadowed-underside cloud lighting!
    int macroLobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){0.45f, 3.35f, 0.10f},
                                            3.65f, 1.85f, 3.65f, 0.78f,
                                            TREE_CELL_PEACH_BLOSSOM, colCore, colTip);
    const TreeCanopyLobe *macroLobe = &sk->lobes[macroLobeIdx];

    // 6 wide, gracefully arching tiered scaffold boughs (3 wide sweeping lower tier + 3 asymmetric upper dome tier)
    int boughCount = (lod == 2) ? 4 : 6;
    for (int i = 0; i < boughCount; i++)
    {
        bool lowerTier = (i < 3);
        float tTrunk = lowerTier ? (0.64f + 0.12f * (float)i) : (0.86f + 0.06f * (float)(i - 3));
        int idx0 = (int)(tTrunk * (float)(rings - 1));
        if (idx0 >= rings - 1) idx0 = rings - 2;
        float subT = tTrunk * (float)(rings - 1) - (float)idx0;
        Vector3 orig = Vector3Lerp(spine[idx0], spine[idx0 + 1], subT);
        float rParent = spineR[idx0] + (spineR[idx0 + 1] - spineR[idx0]) * subT;

        float ang = lowerTier
                        ? ((float)i * (2.0f * PI / 3.0f) + 0.25f + (Tree_NextRand(rng) - 0.5f) * 0.28f)
                        : ((float)(i - 3) * (2.0f * PI / 3.0f) + 1.32f + (Tree_NextRand(rng) - 0.5f) * 0.28f);
        // Wide graceful spread (wider than tall!) with gentle horizontal arch
        float reach = (lowerTier ? 3.25f : 2.35f) * (0.88f + Tree_NextRand(rng) * 0.24f);
        float midLift = lowerTier ? (1.10f + Tree_NextRand(rng) * 0.30f) : (1.55f + Tree_NextRand(rng) * 0.35f);
        float tipLift = lowerTier ? (1.28f + Tree_NextRand(rng) * 0.35f) : (2.15f + Tree_NextRand(rng) * 0.42f);

        Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
        Vector3 mid = {orig.x + dir.x * reach * 0.54f, orig.y + midLift, orig.z + dir.z * reach * 0.54f};
        Vector3 tip = {orig.x + dir.x * reach, orig.y + tipLift, orig.z + dir.z * reach};

        float r1Base = fmaxf(0.065f, rParent * 0.62f);
        float r1Tip  = fmaxf(0.022f, r1Base * 0.32f);
        Tree_BuildCurvedLimb(b, sk, orig, mid, tip, r1Base, r1Tip, (lod == 0) ? 5 : 3, boughSides,
                             barkA, barkB, 0.20f, 0.54f, 0.42f, 0.78f, 1, macroLobeIdx, lowerTier ? 0.55f : 0.82f);

        // Emit a lush mid-bough & tip blossom cloud puff
        Tree_EmitBlossomPuff(b, (Vector3){tip.x, tip.y + 0.22f, tip.z},
                             lowerTier ? 1.18f : 1.08f, macroLobe,
                             0.25f + 0.55f * Tree_NextRand(rng), 0.82f, lod, rng);

        // Order 2 crooked flanking peach sub-branches + blossom puffs
        int secCount = (lod == 0) ? 3 : (lod == 1 ? 2 : 1);
        for (int s = 0; s < secCount; s++)
        {
            float st = 0.34f + 0.28f * (float)s;
            Vector3 sOrig = Tree_EvalQuadBezier(orig, mid, tip, st);
            float side = (s == 1) ? -1.0f : 1.0f;
            float sAng = ang + side * (0.64f + Tree_NextRand(rng) * 0.32f);
            float sReach = reach * (0.46f - 0.05f * (float)s);
            float sLift = (lowerTier ? 0.52f : 0.72f) + Tree_NextRand(rng) * 0.28f;
            Vector3 sMid = {sOrig.x + cosf(sAng) * sReach * 0.52f, sOrig.y + sLift * 0.70f, sOrig.z + sinf(sAng) * sReach * 0.52f};
            Vector3 sTip = {sOrig.x + cosf(sAng) * sReach, sOrig.y + sLift, sOrig.z + sinf(sAng) * sReach};
            float r2Base = fmaxf(0.024f, r1Base * 0.46f * (1.0f - st * 0.30f));
            Tree_BuildCurvedLimb(b, sk, sOrig, sMid, sTip, r2Base, 0.010f, (lod == 0) ? 3 : 2, 4,
                                 barkB, barkB, 0.44f, 0.72f, 0.68f, 0.92f, 2, macroLobeIdx, lowerTier ? 0.62f : 0.88f);

            // Order 3 fine crooked terminal peach shoots (visible when bare/dry!)
            if (lod == 0)
            {
                for (int tw = 0; tw < 2; tw++)
                {
                    float twT = 0.50f + 0.42f * (float)tw;
                    Vector3 twOrig = Tree_EvalQuadBezier(sOrig, sMid, sTip, twT);
                    float twAng = sAng + ((tw == 0) ? 0.58f : -0.58f);
                    float twLen = sReach * 0.44f;
                    Vector3 twMid = {twOrig.x + cosf(twAng) * twLen * 0.5f, twOrig.y + 0.22f, twOrig.z + sinf(twAng) * twLen * 0.5f};
                    Vector3 twTip = {twOrig.x + cosf(twAng) * twLen, twOrig.y + 0.34f, twOrig.z + sinf(twAng) * twLen};
                    Tree_BuildCurvedLimb(b, NULL, twOrig, twMid, twTip, fmaxf(0.012f, r2Base * 0.46f), 0.005f,
                                         2, 3, barkB, barkB, 0.62f, 0.80f, 0.86f, 1.0f, 3, macroLobeIdx, 0.75f);
                }
            }

            // Lush blossom puff seated over each secondary branch
            Tree_EmitBlossomPuff(b, (Vector3){sTip.x, sTip.y + 0.18f, sTip.z},
                                 lowerTier ? 1.02f : 0.96f, macroLobe,
                                 0.18f + 0.74f * Tree_NextRand(rng), 0.86f, lod, rng);
        }
    }

    // Asymmetric upper-crown blossom billows (closes the top dome smoothly for top-down isometric view!)
    Vector3 topP = spine[rings - 1];
    Tree_EmitBlossomPuff(b, (Vector3){topP.x + 0.25f, topP.y + 2.05f, topP.z + 0.15f},
                         1.32f, macroLobe, 0.14f, 0.90f, lod, rng);
    Tree_EmitBlossomPuff(b, (Vector3){topP.x - 0.65f, topP.y + 1.65f, topP.z - 0.45f},
                         1.15f, macroLobe, 0.28f, 0.88f, lod, rng);
    Tree_EmitBlossomPuff(b, (Vector3){topP.x + 0.78f, topP.y + 1.55f, topP.z - 0.55f},
                         1.12f, macroLobe, 0.38f, 0.88f, lod, rng);
}

// =========================================================================
// ARCHETYPE BUILDERS (USING THE 3-STAGE PIPELINE)
// =========================================================================

static void Tree_BuildAlpineConifer(TreeMeshBuilder *b, TreeSkeleton *sk, MapTreeArchetype archetype, int lod, unsigned int *rng)
{
    bool isSlim = (archetype == MAP_TREE_COLUMNAR_TAIGA_SPRUCE);
    bool isBroad = (archetype == MAP_TREE_OLD_GROWTH_FIR);
    int cell = isSlim ? TREE_CELL_CEDAR : TREE_CELL_SPRUCE;
    int trunkSides = (lod == 0) ? 7 : (lod == 1 ? 5 : 4);
    int boughSides = (lod == 0) ? 5 : 4;

    Color barkA = {54, 40, 30, 255};
    Color barkB = {74, 58, 44, 255};
    float H = isSlim ? 9.2f : (isBroad ? 8.2f : 8.6f);
    float baseR = isSlim ? 0.24f : (isBroad ? 0.36f : 0.30f);

    // Stage 1: Continuous parallel-transport trunk + 4 root buttresses + internal downward bough sticks
    int trunkRings = (lod == 0) ? 9 : 6;
    Vector3 trunkPts[10];
    float trunkR[10];
    for (int i = 0; i < trunkRings; i++)
    {
        float t = (float)i / (float)(trunkRings - 1);
        trunkPts[i] = (Vector3){sinf(t * 2.3f) * 0.09f, -0.25f + t * (H + 0.15f), cosf(t * 1.9f) * 0.07f - 0.07f};
        trunkR[i] = baseR * (1.0f - t * 0.93f);
    }
    Tree_SweepBranchSpline(b, trunkPts, trunkR, trunkRings, trunkSides, barkA, barkB,
                           0.0f, 0.45f, 0.0f, 1.0f, 0, 0.56f, 5.0f, true);

    if (lod <= 1)
    {
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.35f;
            Vector3 rp[3] = {
                {cosf(a) * 0.12f, 0.58f, sinf(a) * 0.12f},
                {cosf(a) * 0.48f, 0.12f, sinf(a) * 0.48f},
                {cosf(a) * 0.88f, -0.24f, sinf(a) * 0.88f}
            };
            float rr[3] = {0.13f, 0.08f, 0.025f};
            Tree_SweepBranchSpline(b, rp, rr, 3, 4, barkA, barkA, 0.0f, 0.0f, 0.0f, 0.15f, 1, 0.0f, 0.0f, true);
        }
    }

    // Stage 2: Conical crown lobe for alpine spruce shading (gentle normal blend preserves crisp bough snow shelves)
    int lobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){0.0f, H * 0.48f, 0.0f},
                                       isSlim ? 1.8f : 2.8f, H * 0.46f, isSlim ? 1.8f : 2.8f,
                                       0.24f, cell, (Color){148, 168, 156, 255}, (Color){244, 255, 248, 255});
    const TreeCanopyLobe *lobe = &sk->lobes[lobeIdx];

    int whorls = (lod == 0) ? 12 : (lod == 1 ? 8 : 5);
    for (int wi = 0; wi < whorls; wi++)
    {
        float t = (float)wi / (float)(whorls - 1);
        float y = 0.85f + t * (H - 1.65f);
        float tx = sinf((y / H) * 2.3f) * 0.09f;
        float tz = cosf((y / H) * 1.9f) * 0.07f - 0.07f;
        int bCount = (lod == 2) ? 4 : (wi < 4 ? 8 : (wi < 8 ? 7 : 5));
        float bLen = (isSlim ? 1.75f : (isBroad ? 3.05f : 2.68f)) * (1.0f - powf(t, 0.88f) * 0.76f);
        float bWid = (isSlim ? 1.12f : (isBroad ? 1.62f : 1.46f)) * (1.0f - powf(t, 0.82f) * 0.60f);
        float droop = 0.55f * (1.0f - t * 0.78f);
        float pitch = -0.22f + powf(t, 1.35f) * 0.46f;

        for (int bi = 0; bi < bCount; bi++)
        {
            float yaw = (float)bi * (2.0f * PI / (float)bCount) + (float)wi * 0.68f + (Tree_NextRand(rng) - 0.5f) * 0.24f;
            Vector3 orig = {tx, y + (Tree_NextRand(rng) - 0.5f) * 0.14f, tz};
            float len = bLen * (0.86f + Tree_NextRand(rng) * 0.26f);

            // Real curved downward-arcing woody bough underneath (visible when dryness -> 1.0!)
            if (lod == 0 && (bi & 1) == 0 && wi < whorls - 2)
            {
                Vector3 bMid = {orig.x + sinf(yaw) * len * 0.40f, orig.y - droop * 0.48f, orig.z + cosf(yaw) * len * 0.40f};
                Vector3 bTip = {orig.x + sinf(yaw) * len * 0.74f, orig.y - droop * 0.52f + 0.06f, orig.z + cosf(yaw) * len * 0.74f};
                Tree_BuildCurvedLimb(b, NULL, orig, bMid, bTip, 0.055f * (1.0f - t * 0.62f), 0.012f,
                                     3, boughSides, barkA, barkB, 0.18f + t * 0.18f, 0.42f + t * 0.25f,
                                     0.2f + t * 0.5f, 0.6f + t * 0.35f, 1, lobeIdx, t);
            }

            float tv = 0.88f + Tree_NextRand(rng) * 0.20f;
            Color tintMod = {(unsigned char)(240.0f * tv), (unsigned char)(255.0f * tv), (unsigned char)(245.0f * tv), 255};
            float rank = fminf(0.96f, fmaxf(0.04f, 0.20f + 0.65f * t + (Tree_NextRand(rng) - 0.5f) * 0.24f));
            Tree_EmitConiferBough(b, (Vector3){orig.x, orig.y + 0.06f, orig.z}, yaw, pitch,
                                  len, bWid * (0.88f + Tree_NextRand(rng) * 0.22f), droop,
                                  lobe, rank, tintMod, 0.35f + t * 0.60f, lod, rng);
        }
    }

    // Sharp natural conifer apex (6 upright terminal needle shoots)
    Vector3 apex = {sinf(2.3f) * 0.09f, H - 0.95f, cosf(1.9f) * 0.07f - 0.07f};
    for (int i = 0; i < 6; i++)
    {
        float yaw = (float)i * (PI / 3.0f) + 0.3f;
        Tree_EmitSprigCard(b, (Vector3){apex.x, apex.y + (float)(i & 1) * 0.24f, apex.z},
                           yaw, -1.12f, 0.0f, 0.82f, 0.40f, 0.05f, 0.08f,
                           lobe, 0.18f + 0.12f * (float)i, 0.95f, 1, WHITE);
    }
}

static void Tree_BuildBonsaiPine(TreeMeshBuilder *b, TreeSkeleton *sk, MapTreeArchetype archetype, int lod, unsigned int *rng)
{
    // Stage 1: Smooth S-swept gnarled Huangshan / Coastal / Mountain Pine trunk + 3-order bonsai boughs
    bool isSnow = (archetype == MAP_TREE_GNARLED_MOUNTAIN_PINE);
    bool isCoastal = (archetype == MAP_TREE_COASTAL_PINE);
    int trunkSides = (lod == 0) ? 8 : (lod == 1 ? 6 : 4);
    int boughSides = (lod == 0) ? 5 : 4;

    Color barkA = isSnow ? (Color){66, 56, 48, 255} : (Color){78, 54, 40, 255};
    Color barkB = isSnow ? (Color){102, 88, 78, 255} : (Color){114, 82, 58, 255};
    Color padCore = isSnow ? (Color){58, 86, 68, 255} : (Color){62, 98, 58, 255};
    Color padTip  = isSnow ? (Color){178, 206, 188, 255} : (Color){174, 212, 138, 255};

    float lean = isCoastal ? 1.55f : (isSnow ? 0.88f : 1.12f);
    float H = isCoastal ? 5.6f : 6.4f;
    int rings = (lod == 0) ? 11 : 7;
    Vector3 spine[12];
    float spineR[12];
    for (int i = 0; i < rings; i++)
    {
        float t = (float)i / (float)(rings - 1);
        float sx = -0.32f + lean * (1.35f * t + 0.44f * sinf(t * PI * 1.65f));
        float sz = 0.24f * sinf(t * PI * 2.0f + 0.4f);
        spine[i] = (Vector3){sx, -0.25f + t * (H + 0.25f), sz};
        spineR[i] = 0.38f * powf(1.0f - t * 0.84f, 0.90f);
    }
    Tree_SweepBranchSpline(b, spine, spineR, rings, trunkSides, barkA, barkB,
                           0.0f, 0.36f, 0.0f, 0.72f, 0, 0.62f, 5.0f, true);

    // 4 smooth hugging surface root buttresses
    if (lod <= 1)
    {
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.45f;
            Vector3 rp[4] = {
                {spine[0].x + cosf(a) * 0.14f, 0.36f, spine[0].z + sinf(a) * 0.14f},
                {spine[0].x + cosf(a) * 0.42f, 0.12f, spine[0].z + sinf(a) * 0.42f},
                {spine[0].x + cosf(a) * 0.72f, -0.06f, spine[0].z + sinf(a) * 0.72f},
                {spine[0].x + cosf(a) * 0.98f, -0.32f, spine[0].z + sinf(a) * 0.98f}
            };
            float rr[4] = {0.16f, 0.11f, 0.065f, 0.025f};
            Tree_SweepBranchSpline(b, rp, rr, 4, 5, barkA, barkA, 0.0f, 0.0f, 0.0f, 0.14f, 1, 0.0f, 0.0f, true);
        }
    }

    // 6 cantilevered asymmetrical scaffold boughs -> crooked secondary elbows -> terminal twig forks
    int boughCount = (lod == 2) ? 4 : 6;
    for (int bi = 0; bi < boughCount; bi++)
    {
        float frac = (float)bi / (float)(boughCount - 1);
        float tTrunk = 0.34f + frac * 0.62f;
        int idx0 = (int)(tTrunk * (float)(rings - 1));
        if (idx0 >= rings - 1) idx0 = rings - 2;
        float subT = tTrunk * (float)(rings - 1) - (float)idx0;
        Vector3 orig = Vector3Lerp(spine[idx0], spine[idx0 + 1], subT);
        float rParent = spineR[idx0] + (spineR[idx0 + 1] - spineR[idx0]) * subT;

        float ang = isCoastal
                        ? (-0.55f + (float)bi * 0.42f + (Tree_NextRand(rng) - 0.5f) * 0.25f)
                        : ((float)bi * 2.28f + 0.35f + (Tree_NextRand(rng) - 0.5f) * 0.22f);
        float reach = (2.65f - frac * 0.95f) * (0.90f + Tree_NextRand(rng) * 0.22f);
        Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
        // Classic Huangshan pine elbow: dips slightly outward, then sweeps gently upward at pad tip
        Vector3 mid = {orig.x + dir.x * reach * 0.52f, orig.y - 0.16f + frac * 0.22f, orig.z + dir.z * reach * 0.52f};
        Vector3 tip = {orig.x + dir.x * reach, orig.y + 0.34f + frac * 0.28f, orig.z + dir.z * reach};

        float rB0 = fmaxf(0.065f, rParent * 0.62f);
        float rB1 = rB0 * 0.34f;
        int lobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){tip.x, tip.y + 0.22f, tip.z},
                                           1.35f - frac * 0.22f, 0.48f, 1.35f - frac * 0.22f,
                                           0.74f, TREE_CELL_PINE_ROSETTE, padCore, padTip);

        Tree_BuildCurvedLimb(b, sk, orig, mid, tip, rB0, rB1, (lod == 0) ? 5 : 3, boughSides,
                             barkA, barkB, 0.22f + frac * 0.14f, 0.52f + frac * 0.22f,
                             0.42f, 0.82f, 1, lobeIdx, 0.40f + frac * 0.58f);

        // Order 2 crooked bonsai sub-boughs flanking the pad
        int subForks = (lod == 0) ? 3 : (lod == 1 ? 2 : 0);
        for (int sf = 0; sf < subForks; sf++)
        {
            float st = 0.42f + 0.32f * (float)sf;
            Vector3 fOrig = Tree_EvalQuadBezier(orig, mid, tip, st);
            float fAng = ang + ((sf & 1) ? 0.72f : -0.72f) + (Tree_NextRand(rng) - 0.5f) * 0.22f;
            float fReach = reach * (0.44f - 0.06f * (float)sf);
            Vector3 fMid = {fOrig.x + cosf(fAng) * fReach * 0.55f, fOrig.y + 0.08f, fOrig.z + sinf(fAng) * fReach * 0.55f};
            Vector3 fTip = {fOrig.x + cosf(fAng) * fReach, fOrig.y + 0.32f, fOrig.z + sinf(fAng) * fReach};
            Tree_BuildCurvedLimb(b, sk, fOrig, fMid, fTip, rB0 * 0.48f, 0.016f, 3, 4,
                                 barkB, barkB, 0.40f, 0.66f, 0.65f, 0.94f, 2, lobeIdx, 0.45f + frac * 0.52f);
        }
    }

    // Crown apex pad
    Vector3 apex = spine[rings - 1];
    int apexLobe = TreeSkeleton_AddLobe(sk, (Vector3){apex.x + 0.25f, apex.y + 0.32f, apex.z},
                                        1.42f, 0.52f, 1.42f, 0.76f, TREE_CELL_PINE_ROSETTE, padCore, padTip);
    TreeSkeleton_AddAnchor(sk, (Vector3){apex.x + 0.18f, apex.y + 0.14f, apex.z}, (Vector3){0.2f, 1.0f, 0.0f}, 0.03f, 0.98f, 0.72f, 2, apexLobe);

    // Stage 2 & 3: Populate each horizontal pine cloud pad with lush 3D star-fascicle rosettes
    for (int ai = 0; ai < sk->anchorCount; ai++)
    {
        const TreeTwigAnchor *a = &sk->anchors[ai];
        const TreeCanopyLobe *lobe = &sk->lobes[a->lobeGroup];
        int rosettes = (lod == 0) ? 6 : (lod == 1 ? 3 : 2);
        for (int r = 0; r < rosettes; r++)
        {
            float yaw = (float)r * (2.0f * PI / (float)rosettes) + (float)ai * 0.9f + (Tree_NextRand(rng) - 0.5f) * 0.35f;
            float pitch = -0.20f - Tree_NextRand(rng) * 0.24f; // Gently domed cushion, never flat pancake!
            float roll = ((r & 1) ? 0.32f : -0.32f) + (Tree_NextRand(rng) - 0.5f) * 0.18f;
            Vector3 orig = {
                a->pos.x + (Tree_NextRand(rng) - 0.5f) * 0.54f,
                a->pos.y + 0.08f + (Tree_NextRand(rng) - 0.3f) * 0.24f,
                a->pos.z + (Tree_NextRand(rng) - 0.5f) * 0.54f
            };
            float rank = fminf(0.98f, fmaxf(0.04f, 0.15f + 0.75f * Tree_NextRand(rng)));
            Tree_EmitSprigCard(b, orig, yaw, pitch, roll,
                               1.06f + Tree_NextRand(rng) * 0.30f, 0.98f + Tree_NextRand(rng) * 0.26f,
                               0.16f, 0.22f, lobe, rank, fminf(0.95f, a->compliance + 0.22f),
                               lod == 0 ? 2 : 1, WHITE);
        }
    }
}

static void Tree_BuildBambooGrove(TreeMeshBuilder *b, TreeSkeleton *sk, int lod, unsigned int *rng)
{
    // Stage 1: 5-6 gracefully cantilevered bamboo culms with raised nodal rings + alternating nodal side-twigs
    int culmSides = (lod == 0) ? 7 : (lod == 1 ? 5 : 4);
    Color culmBase  = {44, 78, 36, 255};
    Color culmUpper = {86, 134, 58, 255};
    Color nodeRing  = {34, 58, 26, 255};
    // Deep cool bamboo-jade interior to natural sunlit jade tip (harmonizes with culms, no neon-lime!)
    Color leafCore  = {36, 74, 38, 255};
    Color leafTip   = {108, 158, 72, 255};

    int culms = (lod == 2) ? 3 : 5;
    const float ox[5] = {0.00f,  0.42f, -0.34f,  0.22f, -0.44f};
    const float oz[5] = {0.00f,  0.28f,  0.36f, -0.38f, -0.22f};
    const float hs[5] = {7.8f,   6.9f,   6.1f,   7.3f,   5.4f};
    const float lx[5] = {0.95f, -0.82f,  0.65f,  0.72f, -0.68f};
    const float lz[5] = {0.38f,  0.56f, -0.74f, -0.52f,  0.44f};

    for (int c = 0; c < culms; c++)
    {
        int segs = (lod == 0) ? 10 : 6;
        Vector3 pts[12];
        float rads[12];
        float baseR = 0.086f - (float)c * 0.007f;
        for (int s = 0; s <= segs; s++)
        {
            float t = (float)s / (float)segs;
            // Graceful cubic whip-bow under upper foliage weight!
            float bow = t * t * (0.45f + 0.55f * t);
            pts[s] = (Vector3){
                ox[c] + lx[c] * bow,
                -0.18f + hs[c] * t,
                oz[c] + lz[c] * bow
            };
            rads[s] = baseR * (1.0f - t * 0.62f);
        }
        Tree_SweepBranchSpline(b, pts, rads, segs + 1, culmSides, culmBase, culmUpper,
                               0.05f, 0.85f, 0.0f, 1.0f, 0, 0.16f, 4.0f, true);

        // Raised dark internode diaphragm bands
        if (lod <= 1)
        {
            for (int s = 1; s < segs; s++)
            {
                Vector3 nPts[2] = {Vector3Lerp(pts[s - 1], pts[s], 0.94f), pts[s]};
                float nRad[2] = {rads[s] * 1.22f, rads[s] * 1.15f};
                float t = (float)s / (float)segs;
                Tree_SweepBranchSpline(b, nPts, nRad, 2, culmSides, nodeRing, nodeRing,
                                       t * 0.80f, t * 0.82f, t, t + 0.02f, 1, 0.0f, 0.0f, false);
            }
        }

        // Upper-culm feathery foliage lobe
        Vector3 lobeCtr = {ox[c] + lx[c] * 0.68f, hs[c] * 0.72f, oz[c] + lz[c] * 0.68f};
        int lobeIdx = TreeSkeleton_AddLobe(sk, lobeCtr, 1.35f, hs[c] * 0.34f, 1.35f,
                                           0.55f, TREE_CELL_BAMBOO_FAN, leafCore, leafTip);

        // Real alternating nodal side-twigs on upper 58% of culm (clean bare lower bamboo stalks!)
        for (int s = 4; s <= segs; s++)
        {
            float t = (float)s / (float)segs;
            if (t < 0.42f)
                continue;
            int twigs = (lod == 0) ? 2 : 1;
            for (int tw = 0; tw < twigs; tw++)
            {
                float yaw = (float)c * 1.35f + (float)s * 2.15f + (float)tw * PI + (Tree_NextRand(rng) - 0.5f) * 0.35f;
                float tLen = (0.52f + Tree_NextRand(rng) * 0.24f) * (1.12f - t * 0.36f);
                Vector3 tMid = {pts[s].x + cosf(yaw) * tLen * 0.55f, pts[s].y + 0.14f, pts[s].z + sinf(yaw) * tLen * 0.55f};
                Vector3 tTip = {pts[s].x + cosf(yaw) * tLen, pts[s].y + 0.04f, pts[s].z + sinf(yaw) * tLen};
                if (lod == 0)
                {
                    Tree_BuildCurvedLimb(b, NULL, pts[s], tMid, tTip, 0.018f, 0.007f, 2, 4,
                                         nodeRing, culmUpper, t * 0.78f, fminf(0.95f, t * 0.90f),
                                         t, fminf(1.0f, t + 0.12f), 2, lobeIdx, t);
                }
                // Anchor foliage right at mid-twig (never floating detached past the twig tip!)
                TreeSkeleton_AddAnchor(sk, Vector3Lerp(pts[s], tTip, 0.42f), Vector3Subtract(tTip, pts[s]),
                                       0.012f, t, fminf(0.95f, t * 0.88f), 2, lobeIdx);
            }
        }
    }

    // Stage 2 & 3: Attach cascading, outward-drooping slender bamboo sprays directly from nodal twigs
    for (int ai = 0; ai < sk->anchorCount; ai++)
    {
        const TreeTwigAnchor *a = &sk->anchors[ai];
        const TreeCanopyLobe *lobe = &sk->lobes[a->lobeGroup];
        float baseYaw = atan2f(a->dir.x, a->dir.z);
        int sprays = (lod == 0) ? 2 : 1;
        for (int f = 0; f < sprays; f++)
        {
            float yaw = baseYaw + ((f == 0) ? 0.28f : -0.32f) + (Tree_NextRand(rng) - 0.5f) * 0.30f;
            // Positive pitch + graceful droop makes bamboo leaves cascade downward like real weeping bamboo shoots
            float pitch = 0.12f + (float)f * 0.26f + (Tree_NextRand(rng) - 0.5f) * 0.18f;
            float roll = ((f & 1) ? 0.48f : -0.48f) + (Tree_NextRand(rng) - 0.5f) * 0.18f;
            float rank = fminf(0.97f, fmaxf(0.04f, 0.12f + 0.80f * Tree_NextRand(rng)));
            Tree_EmitSprigCard(b, a->pos, yaw, pitch, roll,
                               1.08f + Tree_NextRand(rng) * 0.24f, 0.72f + Tree_NextRand(rng) * 0.18f,
                               0.38f, 0.16f, lobe, rank, fminf(0.98f, a->compliance + 0.12f),
                               lod == 0 ? 2 : 1, WHITE);
        }
    }
}

static void Tree_BuildWeepingWillow(TreeMeshBuilder *b, TreeSkeleton *sk, int lod, unsigned int *rng)
{
    int trunkSides = (lod == 0) ? 8 : (lod == 1 ? 6 : 4);
    int boughSides = (lod == 0) ? 5 : 4;
    Color barkA = {62, 50, 38, 255};
    Color barkB = {88, 72, 54, 255};
    Color leafCore = {68, 114, 48, 255};
    Color leafTip  = {166, 214, 102, 255};

    // Stage 1: Leaning furrowed trunk + buttress roots -> 6 overarching scaffold boughs -> crown ribs
    Vector3 trunkPts[6] = {
        {0.00f, -0.25f, 0.00f},
        {0.10f,  0.48f, 0.06f},
        {0.24f,  1.22f, 0.14f},
        {0.40f,  1.95f, 0.20f},
        {0.52f,  2.65f, 0.24f},
        {0.58f,  3.18f, 0.26f}
    };
    float trunkR[6] = {0.46f, 0.39f, 0.34f, 0.30f, 0.25f, 0.18f};
    Tree_SweepBranchSpline(b, trunkPts, trunkR, 6, trunkSides, barkA, barkB,
                           0.0f, 0.28f, 0.0f, 0.55f, 0, 0.58f, 6.0f, true);

    if (lod <= 1)
    {
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.25f;
            Vector3 rp[3] = {
                {cosf(a) * 0.22f, 0.55f, sinf(a) * 0.22f},
                {cosf(a) * 0.68f, 0.08f, sinf(a) * 0.68f},
                {cosf(a) * 1.18f, -0.26f, sinf(a) * 1.18f}
            };
            float rr[3] = {0.16f, 0.09f, 0.025f};
            Tree_SweepBranchSpline(b, rp, rr, 3, 4, barkA, barkA, 0.0f, 0.0f, 0.0f, 0.14f, 1, 0.0f, 0.0f, true);
        }
    }

    int crownLobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){0.55f, 4.35f, 0.25f},
                                            3.35f, 2.15f, 3.35f, 0.68f, TREE_CELL_WILLOW_WHIP, leafCore, leafTip);
    int capLobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){0.55f, 4.95f, 0.25f},
                                          2.65f, 1.35f, 2.65f, 0.78f, TREE_CELL_OAK_BROADLEAF, leafCore, leafTip);

    int boughs = (lod == 2) ? 4 : 6;
    for (int i = 0; i < boughs; i++)
    {
        float ang = (float)i * (2.0f * PI / (float)boughs) + 0.30f;
        float reach = 2.65f + Tree_NextRand(rng) * 0.85f;
        Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
        Vector3 orig = trunkPts[4];
        Vector3 mid = {orig.x + dir.x * reach * 0.52f, orig.y + 1.85f, orig.z + dir.z * reach * 0.52f};
        Vector3 tip = {orig.x + dir.x * reach, orig.y + 1.15f, orig.z + dir.z * reach};
        Tree_BuildCurvedLimb(b, sk, orig, mid, tip, 0.16f, 0.045f, (lod == 0) ? 5 : 3, boughSides,
                             barkA, barkB, 0.28f, 0.66f, 0.50f, 0.88f, 1, crownLobeIdx, 0.72f);

        // Secondary flanking crown ribs + upper dome sprigs (eliminates bald top-down spider!)
        if (lod <= 1)
        {
            for (int sd = -1; sd <= 1; sd += 2)
            {
                float fAng = ang + (float)sd * 0.46f;
                Vector3 fOrig = Tree_EvalQuadBezier(orig, mid, tip, 0.48f);
                Vector3 fMid = {fOrig.x + cosf(fAng) * 0.95f, fOrig.y + 0.55f, fOrig.z + sinf(fAng) * 0.95f};
                Vector3 fTip = {fOrig.x + cosf(fAng) * 1.65f, fOrig.y - 0.12f, fOrig.z + sinf(fAng) * 1.65f};
                Tree_BuildCurvedLimb(b, sk, fOrig, fMid, fTip, 0.075f, 0.018f, 3, 4,
                                     barkB, barkB, 0.48f, 0.76f, 0.72f, 0.96f, 2, crownLobeIdx, 0.78f);
            }
        }

        // Upper dome foliage cap (Cell 4 broadleaf tufts so isometric top-down view is lush!)
        Tree_EmitVolumetricSprigTuft(b, (Vector3){mid.x, mid.y + 0.25f, mid.z}, ang,
                                     1.15f, 1.05f, 0.28f, &sk->lobes[capLobeIdx],
                                     0.20f + 0.50f * Tree_NextRand(rng), 0.74f, lod, rng);

        // Cascading weeping willow curtains (Cell 7) hanging along mid..tip
        int curtains = (lod == 0) ? 7 : (lod == 1 ? 4 : 2);
        for (int c = 0; c < curtains; c++)
        {
            float frac = 0.22f + 0.78f * ((float)c / (float)(curtains > 1 ? curtains - 1 : 1));
            Vector3 anc = Tree_EvalQuadBezier(orig, mid, tip, frac);
            anc.y += 0.22f;
            float rank = fminf(0.98f, fmaxf(0.05f, 0.12f + 0.82f * Tree_NextRand(rng)));
            Tree_EmitSprigCard(b, anc, ang + (Tree_NextRand(rng) - 0.5f) * 0.75f, 1.34f,
                               (Tree_NextRand(rng) - 0.5f) * 0.65f,
                               2.45f + Tree_NextRand(rng) * 0.95f, 0.88f, 0.12f, 0.14f,
                               &sk->lobes[crownLobeIdx], rank, 0.92f, lod == 0 ? 2 : 1, WHITE);
        }
    }
    // Central apex cap cluster
    Tree_EmitVolumetricSprigTuft(b, (Vector3){0.55f, 4.95f, 0.25f}, 0.5f, 1.35f, 1.25f, 0.24f,
                                 &sk->lobes[capLobeIdx], 0.35f, 0.80f, lod, rng);
}

// Unified Sympodial Deciduous & Deadwood Builder:
// Covers PEACH_BLOSSOM, MEADOW_OAK, MEADOW_BIRCH, FROSTED_BIRCH, ARID_ACACIA, FROZEN_DEADWOOD
static void Tree_BuildSympodialTree(TreeMeshBuilder *b, TreeSkeleton *sk, MapTreeArchetype archetype, int lod, unsigned int *rng)
{
    bool isPeach   = (archetype == MAP_TREE_PEACH_BLOSSOM);
    bool isBirch   = (archetype == MAP_TREE_MEADOW_BIRCH || archetype == MAP_TREE_FROSTED_BIRCH);
    bool isFrosted = (archetype == MAP_TREE_FROSTED_BIRCH);
    bool isAcacia  = (archetype == MAP_TREE_ARID_ACACIA);
    bool isDead    = (archetype == MAP_TREE_FROZEN_DEADWOOD);

    int trunkSides = (lod == 0) ? 8 : (lod == 1 ? 6 : 4);
    int boughSides = (lod == 0) ? 5 : 4;

    Color barkA = isBirch ? (Color){204, 202, 196, 255}
                : (isDead ? (Color){62, 58, 55, 255}
                : (isPeach ? (Color){58, 38, 36, 255}
                : (isAcacia ? (Color){78, 64, 48, 255} : (Color){68, 50, 38, 255})));
    Color barkB = isBirch ? (Color){238, 236, 230, 255}
                : (isDead ? (Color){98, 92, 86, 255}
                : (isPeach ? (Color){84, 56, 52, 255}
                : (isAcacia ? (Color){106, 88, 66, 255} : (Color){96, 72, 52, 255})));

    int leafCell = isPeach ? TREE_CELL_PEACH_BLOSSOM
                 : (isBirch ? TREE_CELL_BIRCH_SPRIG
                 : (isAcacia ? TREE_CELL_WILLOW_WHIP : TREE_CELL_OAK_BROADLEAF));

    // Rich core-to-tip botanical color ramp (deep shaded interior -> luminous sunlit crown)
    Color colCore = isPeach   ? (Color){176, 82, 118, 255}
                  : (isFrosted ? (Color){168, 108, 42, 255}
                  : (isAcacia  ? (Color){84, 96, 46, 255}
                  : (isBirch   ? (Color){66, 114, 48, 255}
                               : (Color){44, 88, 42, 255})));
    Color colTip  = isPeach   ? (Color){255, 206, 224, 255}
                  : (isFrosted ? (Color){248, 194, 96, 255}
                  : (isAcacia  ? (Color){174, 182, 92, 255}
                  : (isBirch   ? (Color){164, 218, 104, 255}
                               : (Color){126, 188, 82, 255})));

    // Stage 1: Continuous curved trunk + flared root buttresses
    float trunkH = isBirch ? 6.8f : (isAcacia ? 2.6f : (isPeach ? 3.2f : (isDead ? 4.4f : 3.5f)));
    float baseR  = isBirch ? 0.22f : (isAcacia ? 0.28f : (isPeach ? 0.34f : (isDead ? 0.38f : 0.42f)));
    int rings = (lod == 0) ? 9 : 6;
    Vector3 spine[10];
    float spineR[10];
    for (int s = 0; s < rings; s++)
    {
        float t = (float)s / (float)(rings - 1);
        float gnarl = isPeach ? 0.38f : (isDead ? 0.44f : (isBirch ? 0.22f : 0.28f));
        float sx = sinf(t * 2.5f + 0.2f) * gnarl + t * (isPeach ? 0.26f : 0.12f);
        float sz = cosf(t * 2.1f) * gnarl * 0.72f - gnarl * 0.72f;
        spine[s] = (Vector3){sx, -0.25f + t * (trunkH + 0.25f), sz};
        // Sympodial trunk tapers gently until the main fork crown, birch tapers continuously to apex
        float taperExp = isBirch ? 0.88f : 0.62f;
        spineR[s] = baseR * powf(1.0f - t * (isBirch ? 0.86f : 0.56f), taperExp);
    }
    Tree_SweepBranchSpline(b, spine, spineR, rings, trunkSides, barkA, barkB,
                           0.0f, isBirch ? 0.36f : 0.22f, 0.0f, 0.48f, 0,
                           isBirch ? 0.34f : 0.56f, 5.0f, isBirch || isDead);

    // 4 smooth surface-hugging root buttresses
    if (lod <= 1 && !isBirch)
    {
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.40f + (Tree_NextRand(rng) - 0.5f) * 0.30f;
            Vector3 rp[4] = {
                {spine[0].x + cosf(a) * (baseR * 0.45f), 0.34f, spine[0].z + sinf(a) * (baseR * 0.45f)},
                {spine[0].x + cosf(a) * (baseR * 1.15f), 0.12f, spine[0].z + sinf(a) * (baseR * 1.15f)},
                {spine[0].x + cosf(a) * (baseR * 1.85f), -0.06f, spine[0].z + sinf(a) * (baseR * 1.85f)},
                {spine[0].x + cosf(a) * (baseR * 2.45f), -0.32f, spine[0].z + sinf(a) * (baseR * 2.45f)}
            };
            float rr[4] = {baseR * 0.40f, baseR * 0.28f, baseR * 0.16f, 0.025f};
            Tree_SweepBranchSpline(b, rp, rr, 4, 5, barkA, barkA, 0.0f, 0.0f, 0.0f, 0.14f, 1, 0.0f, 0.0f, true);
        }
    }

    // Order 1 Primary Scaffold Boughs -> Order 2 Secondary Limbs -> Order 3 Terminal Crooked Twigs
    int primaryCount = (lod == 2) ? 4 : (isBirch ? 8 : (isAcacia ? 5 : 6));
    for (int i = 0; i < primaryCount; i++)
    {
        float frac = (float)i / (float)(primaryCount > 1 ? primaryCount - 1 : 1);
        float tTrunk = isBirch ? (0.34f + frac * 0.54f) : (0.58f + frac * 0.40f);
        int idx0 = (int)(tTrunk * (float)(rings - 1));
        if (idx0 >= rings - 1) idx0 = rings - 2;
        float subT = tTrunk * (float)(rings - 1) - (float)idx0;
        Vector3 orig = Vector3Lerp(spine[idx0], spine[idx0 + 1], subT);
        float rParent = spineR[idx0] + (spineR[idx0 + 1] - spineR[idx0]) * subT;

        float ang = (float)i * 2.399963f + 0.35f + (Tree_NextRand(rng) - 0.5f) * 0.26f;
        float crownProf = sinf(0.32f + frac * 2.35f);
        float reach = (isAcacia ? 3.15f : (isBirch ? 1.85f : (isPeach ? 2.55f : (isDead ? 2.35f : 2.85f))))
                    * (0.64f + 0.46f * crownProf) * (0.88f + Tree_NextRand(rng) * 0.24f);
        float lift  = (isAcacia ? 1.35f : (isBirch ? 1.85f : (isPeach ? 1.75f : (isDead ? 1.95f : 2.15f))))
                    * (0.72f + 0.36f * (1.0f - frac * 0.35f));

        Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
        // Arching scaffold limb with gentle phototropic upward sweep
        Vector3 mid = {
            orig.x + dir.x * reach * 0.50f + (Tree_NextRand(rng) - 0.5f) * 0.28f,
            orig.y + lift * (isAcacia ? 0.76f : 0.58f),
            orig.z + dir.z * reach * 0.50f + (Tree_NextRand(rng) - 0.5f) * 0.28f
        };
        Vector3 tip = {
            orig.x + dir.x * reach,
            orig.y + lift * (isAcacia ? 0.94f : 1.04f),
            orig.z + dir.z * reach
        };

        // Pipe-model radius scaling (r_child ~ 0.62 * r_parent)
        float r1Base = fmaxf(0.055f, rParent * 0.64f);
        float r1Tip  = fmaxf(0.024f, r1Base * 0.36f);

        // Stage 2: Dedicated Canopy Cloud Lobe centered over this scaffold bough's sub-crown
        float lobeRX = isAcacia ? 1.65f : (isBirch ? 1.15f : (isPeach ? 1.38f : 1.52f));
        float lobeRY = isAcacia ? 0.56f : (isBirch ? 1.05f : (isPeach ? 0.96f : 1.12f));
        int lobeIdx = TreeSkeleton_AddLobe(sk, (Vector3){(mid.x + tip.x) * 0.5f, tip.y + lobeRY * 0.22f, (mid.z + tip.z) * 0.5f},
                                           lobeRX, lobeRY, lobeRX, 0.76f, leafCell, colCore, colTip);

        Tree_BuildCurvedLimb(b, sk, orig, mid, tip, r1Base, r1Tip, (lod == 0) ? 5 : 3, boughSides,
                             barkA, barkB, 0.20f + frac * 0.14f, 0.48f + frac * 0.16f,
                             0.42f, 0.76f, 1, lobeIdx, 0.42f + frac * 0.48f);

        // Order 2 Secondary Limbs (2-3 per scaffold bough)
        int secCount = (lod == 0) ? 3 : (lod == 1 ? 2 : 0);
        for (int s = 0; s < secCount; s++)
        {
            float st = 0.36f + 0.30f * (float)s;
            Vector3 sOrig = Tree_EvalQuadBezier(orig, mid, tip, st);
            float side = (s == 1) ? -1.0f : 1.0f;
            float sAng = ang + side * (0.58f + Tree_NextRand(rng) * 0.34f);
            float sReach = reach * (0.48f - 0.06f * (float)s);
            float sLift = lift * (isAcacia ? 0.22f : (0.38f + Tree_NextRand(rng) * 0.20f));
            Vector3 sMid = {sOrig.x + cosf(sAng) * sReach * 0.52f, sOrig.y + sLift * 0.65f, sOrig.z + sinf(sAng) * sReach * 0.52f};
            Vector3 sTip = {sOrig.x + cosf(sAng) * sReach, sOrig.y + sLift, sOrig.z + sinf(sAng) * sReach};
            float r2Base = fmaxf(0.026f, r1Base * 0.46f * (1.0f - st * 0.35f));
            float r2Tip  = fmaxf(0.011f, r2Base * 0.35f);

            Tree_BuildCurvedLimb(b, sk, sOrig, sMid, sTip, r2Base, r2Tip, (lod == 0) ? 3 : 2, 4,
                                 barkB, barkB, 0.42f, 0.68f, 0.68f, 0.90f, 2, lobeIdx, 0.52f + frac * 0.44f);

            // Order 3 Crooked Terminal Twigs (LOD0 only — gives unmistakable authentic bare-tree silhouette!)
            if (lod == 0)
            {
                for (int tw = 0; tw < 2; tw++)
                {
                    float twT = 0.48f + 0.44f * (float)tw;
                    Vector3 twOrig = Tree_EvalQuadBezier(sOrig, sMid, sTip, twT);
                    float twAng = sAng + ((tw == 0) ? 0.64f : -0.64f) + (Tree_NextRand(rng) - 0.5f) * 0.30f;
                    float twLen = sReach * 0.46f;
                    Vector3 twMid = {twOrig.x + cosf(twAng) * twLen * 0.52f, twOrig.y + 0.24f, twOrig.z + sinf(twAng) * twLen * 0.52f};
                    Vector3 twTip = {twOrig.x + cosf(twAng) * twLen, twOrig.y + (isAcacia ? 0.14f : 0.42f), twOrig.z + sinf(twAng) * twLen};
                    Tree_BuildCurvedLimb(b, sk, twOrig, twMid, twTip, fmaxf(0.014f, r2Base * 0.48f), 0.006f,
                                         2, 3, barkB, barkB, 0.60f, 0.78f, 0.84f, 1.0f, 3, lobeIdx, 0.62f + frac * 0.36f);
                }
            }
        }
    }

    // Upper leader crown fork & dome lobe (natural rounded top instead of flat lid)
    Vector3 topP = spine[rings - 1];
    int topLobe = TreeSkeleton_AddLobe(sk, (Vector3){topP.x, topP.y + (isAcacia ? 0.65f : 1.35f), topP.z},
                                       isAcacia ? 1.85f : (isBirch ? 1.15f : 1.55f),
                                       isAcacia ? 0.58f : 1.15f,
                                       isAcacia ? 1.85f : (isBirch ? 1.15f : 1.55f),
                                       0.78f, leafCell, colCore, colTip);
    if (!isBirch)
    {
        for (int u = 0; u < 3; u++)
        {
            float ua = (float)u * (2.0f * PI / 3.0f) + 0.5f;
            float uReach = isAcacia ? 1.25f : 1.05f;
            float uLift = isAcacia ? 0.85f : 1.55f;
            Vector3 uMid = {topP.x + cosf(ua) * uReach * 0.5f, topP.y + uLift * 0.62f, topP.z + sinf(ua) * uReach * 0.5f};
            Vector3 uTip = {topP.x + cosf(ua) * uReach, topP.y + uLift, topP.z + sinf(ua) * uReach};
            Tree_BuildCurvedLimb(b, sk, topP, uMid, uTip, fmaxf(0.045f, spineR[rings - 1] * 0.65f), 0.012f,
                                 3, 4, barkB, barkB, 0.36f, 0.72f, 0.65f, 0.96f, 2, topLobe, 0.94f);
        }
    }
    else
    {
        TreeSkeleton_AddAnchor(sk, (Vector3){topP.x, topP.y + 0.15f, topP.z}, (Vector3){0.0f, 1.0f, 0.0f}, 0.02f, 0.98f, 0.88f, 2, topLobe);
    }

    if (isDead)
        return; // Pure bare 3-order deadwood skeleton!

    // Stage 3: Populate 3D cupped sprig tufts onto Order 1/2/3 twig anchors with spherized lobe normals
    float cardLen = isPeach ? 0.86f : (isBirch ? 0.82f : (isAcacia ? 0.96f : 0.94f));
    float cardWid = isPeach ? 0.82f : (isBirch ? 0.74f : (isAcacia ? 0.92f : 0.88f));
    float droop   = isBirch ? 0.28f : (isAcacia ? 0.12f : 0.20f);
    if (lod == 1) { cardLen *= 1.22f; cardWid *= 1.20f; }
    if (lod == 2) { cardLen *= 1.55f; cardWid *= 1.52f; }

    for (int ai = 0; ai < sk->anchorCount; ai++)
    {
        const TreeTwigAnchor *a = &sk->anchors[ai];
        const TreeCanopyLobe *lobe = &sk->lobes[a->lobeGroup];
        float baseYaw = atan2f(a->dir.x, a->dir.z);
        // Outer Order-3 twigs get higher leafRank so outer canopy thins first during drying!
        float baseRank = fminf(0.96f, fmaxf(0.05f, (a->order >= 3 ? 0.45f : 0.15f) + 0.48f * Tree_NextRand(rng)));
        float scaleByOrder = (a->order >= 3) ? 0.84f : 1.04f;
        Tree_EmitVolumetricSprigTuft(b, a->pos, baseYaw,
                                     cardLen * scaleByOrder, cardWid * scaleByOrder, droop,
                                     lobe, baseRank, fminf(0.98f, a->compliance + 0.16f), lod, rng);
    }
}

static Mesh Tree_BuildArchetypeMesh(MapTreeArchetype archetype, int lod)
{
    TreeMeshBuilder b = {0};
    if (!TreeBuilder_Init(&b, lod == 0 ? 12288 : (lod == 1 ? 4096 : 1536)))
        return (Mesh){0};

    TreeSkeleton *sk = (TreeSkeleton *)MemAlloc((unsigned int)sizeof(TreeSkeleton));
    if (!sk)
    {
        TreeBuilder_Free(&b);
        return (Mesh){0};
    }
    memset(sk, 0, sizeof(TreeSkeleton));

    unsigned int rng = 1101u + (unsigned int)archetype * 337u;

    if (archetype == MAP_TREE_ALPINE_SNOW_SPRUCE ||
        archetype == MAP_TREE_COLUMNAR_TAIGA_SPRUCE ||
        archetype == MAP_TREE_OLD_GROWTH_FIR)
    {
        Tree_BuildAlpineConifer(&b, sk, archetype, lod, &rng);
    }
    else if (archetype == MAP_TREE_ANCIENT_PINE ||
             archetype == MAP_TREE_GNARLED_MOUNTAIN_PINE ||
             archetype == MAP_TREE_COASTAL_PINE)
    {
        Tree_BuildBonsaiPine(&b, sk, archetype, lod, &rng);
    }
    else if (archetype == MAP_TREE_BAMBOO)
    {
        Tree_BuildBambooGrove(&b, sk, lod, &rng);
    }
    else if (archetype == MAP_TREE_WEEPING_WILLOW)
    {
        Tree_BuildWeepingWillow(&b, sk, lod, &rng);
    }
    else if (archetype == MAP_TREE_PEACH_BLOSSOM)
    {
        Tree_BuildPeachBlossom(&b, sk, lod, &rng);
    }
    else
    {
        Tree_BuildSympodialTree(&b, sk, archetype, lod, &rng);
    }

    MemFree(sk);
    Mesh mesh = TreeBuilder_ToMesh(&b);
    TreeBuilder_Free(&b);
    return mesh;
}

static Shader s_treeShader = {0};
static Shader s_treeShadowShader = {0};
static bool s_treeShaderReady = false;
static int s_treeLocInstTex = -1;
static int s_treeLocAtlasTex = -1;
static int s_treeLocDrawDistSq = -1;
static int s_treeLocBaseOffset = -1;
static int s_treeLocSnowAmount = -1;
static int s_treeLocGlobalDryness = -1;
static int s_treeLocGlobalScorch = -1;
static int s_treeLocLodFade = -1;
static int s_treeShadowLocInstTex = -1;
static int s_treeShadowLocAtlasTex = -1;
static int s_treeShadowLocBaseOffset = -1;
static int s_treeShadowLocGlobalDryness = -1;
static int s_treeShadowLocGlobalScorch = -1;

static Shader Tree_GetShader(void)
{
    if (!s_treeShaderReady)
    {
        s_treeShader = ResourceManager_LoadShader("maps/toolkit/shaders/tree_instanced.vs",
                                                  "maps/toolkit/shaders/tree_foliage.fs");
        s_treeShadowShader = ResourceManager_LoadShader("maps/toolkit/shaders/tree_shadow_instanced.vs",
                                                        "maps/toolkit/shaders/tree_shadow.fs");
        if (s_treeShader.id != 0)
        {
            s_treeLocInstTex = GetShaderLocation(s_treeShader, "u_treeInstanceTex");
            s_treeLocAtlasTex = GetShaderLocation(s_treeShader, "u_treeFoliageAtlas");
            s_treeLocDrawDistSq = GetShaderLocation(s_treeShader, "u_treeDrawDistSq");
            s_treeLocBaseOffset = GetShaderLocation(s_treeShader, "u_instanceBaseOffset");
            s_treeLocSnowAmount = GetShaderLocation(s_treeShader, "u_treeSnowAmount");
            s_treeLocGlobalDryness = GetShaderLocation(s_treeShader, "u_treeGlobalDryness");
            s_treeLocGlobalScorch = GetShaderLocation(s_treeShader, "u_treeGlobalScorch");
            s_treeLocLodFade = GetShaderLocation(s_treeShader, "u_treeLodFade");
        }
        if (s_treeShadowShader.id != 0)
        {
            s_treeShadowLocInstTex = GetShaderLocation(s_treeShadowShader, "u_treeInstanceTex");
            s_treeShadowLocAtlasTex = GetShaderLocation(s_treeShadowShader, "u_treeFoliageAtlas");
            s_treeShadowLocBaseOffset = GetShaderLocation(s_treeShadowShader, "u_instanceBaseOffset");
            s_treeShadowLocGlobalDryness = GetShaderLocation(s_treeShadowShader, "u_treeGlobalDryness");
            s_treeShadowLocGlobalScorch = GetShaderLocation(s_treeShadowShader, "u_treeGlobalScorch");
        }
        Tree_GetFoliageAtlas();
        s_treeShaderReady = true;
    }
    return s_treeShader;
}

// Direct vector-basis Camera3D frustum test (zero matrix-layout assumptions, safe outside BeginMode3D)
typedef struct
{
    Vector3 pos;
    Vector3 fwd;
    Vector3 right;
    Vector3 up;
    float tanY;
    float secY;
    float tanX;
    float secX;
    float maxDist;
} TreeCameraBasis;

static TreeCameraBasis Tree_MakeCameraBasis(Camera3D cam, float maxDist)
{
    TreeCameraBasis b = {0};
    b.pos = cam.position;
    b.fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    b.right = Vector3Normalize(Vector3CrossProduct(b.fwd, cam.up));
    if (Vector3LengthSqr(b.right) < 1e-4f)
        b.right = (Vector3){1.0f, 0.0f, 0.0f};
    b.up = Vector3CrossProduct(b.right, b.fwd);
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float aspect = (sh > 1.0f) ? (sw / sh) : (16.0f / 9.0f);
    float halfFovY = fmaxf(0.10f, cam.fovy * 0.5f * DEG2RAD);
    b.tanY = tanf(halfFovY);
    b.secY = sqrtf(1.0f + b.tanY * b.tanY);
    b.tanX = b.tanY * aspect;
    b.secX = sqrtf(1.0f + b.tanX * b.tanX);
    b.maxDist = maxDist;
    return b;
}

static bool Tree_SphereInCameraView(const TreeCameraBasis *b, Vector3 center, float radius)
{
    Vector3 d = Vector3Subtract(center, b->pos);
    float z = Vector3DotProduct(d, b->fwd);
    if (z < -radius || z > b->maxDist + radius)
        return false;
    float y = Vector3DotProduct(d, b->up);
    if (fabsf(y) > z * b->tanY + radius * b->secY)
        return false;
    float x = Vector3DotProduct(d, b->right);
    if (fabsf(x) > z * b->tanX + radius * b->secX)
        return false;
    return true;
}

static bool Tree_IsSnowArchetype(int a)
{
    return (a == MAP_TREE_ALPINE_SNOW_SPRUCE ||
            a == MAP_TREE_GNARLED_MOUNTAIN_PINE ||
            a == MAP_TREE_FROSTED_BIRCH ||
            a == MAP_TREE_COLUMNAR_TAIGA_SPRUCE ||
            a == MAP_TREE_OLD_GROWTH_FIR ||
            a == MAP_TREE_FROZEN_DEADWOOD);
}

MapTreeSystem MapProp_CreateTreeSystem(const MapTreePlacement *placements, int count,
                                       const MapGroundSurface *ground, Vector3 worldCenter,
                                       float drawDistance)
{
    MapTreeSystem sys = {0};
    sys.drawDistance = (drawDistance > 10.0f) ? drawDistance : 180.0f;
    sys.lod0Distance = 40.0f;
    sys.lod1Distance = 88.0f;
    sys.snowCoverage = -1.0f;
    sys.globalDryness = 0.0f;
    sys.globalScorch = 0.0f;
    if (!placements || count <= 0)
        return sys;

    Shader sh = Tree_GetShader();
    if (sh.id == 0)
        return sys;

    sys.placements = (MapTreePlacement *)MemAlloc((unsigned int)count * (unsigned int)sizeof(MapTreePlacement));
    if (sys.placements)
    {
        for (int i = 0; i < count; i++)
        {
            MapTreePlacement p = placements[i];
            if (ground && ground->ready)
            {
                Vector3 sp = {0}, sn = {0};
                if (MapProp_SampleGroundSurface(ground, worldCenter, p.position.x, p.position.z, &sp, &sn))
                    p.position.y = sp.y;
            }
            sys.placements[i] = p;
        }
        sys.totalPlacements = count;
    }

    int counts[MAP_TREE_ARCHETYPE_COUNT] = {0};
    for (int i = 0; i < count; i++)
    {
        int a = (int)placements[i].archetype;
        if (a >= 0 && a < MAP_TREE_ARCHETYPE_COUNT)
            counts[a]++;
    }

    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (counts[a] <= 0)
            continue;
        bool allLodsOk = true;
        for (int lod = 0; lod < 3; lod++)
        {
            Mesh m = Tree_BuildArchetypeMesh((MapTreeArchetype)a, lod);
            if (m.vertexCount <= 0)
            {
                allLodsOk = false;
                break;
            }
            sys.models[a][lod] = LoadModelFromMesh(m);
            if (sys.models[a][lod].materialCount > 0)
                sys.models[a][lod].materials[0].shader = sh;
        }
        if (!allLodsOk)
            continue;
        sys.modelLoaded[a] = true;

        int typeCount = counts[a];
        // Reserve 2x instance slots in visibleTexels/instanceTextures so trees in the 6m LOD cross-fade band
        // can safely emit into both adjacent LOD ranges without ever overflowing the GPU texture.
        int visCap = typeCount * 2;
        int texelsNeeded = visCap * 2;
        int rows = (texelsNeeded + 511) / 512;
        if (rows < 1) rows = 1;
        size_t allFloats = (size_t)typeCount * 8u;
        size_t visFloats = (size_t)512 * (size_t)rows * 4u;
        float *allPx = (float *)MemAlloc((unsigned int)(allFloats * sizeof(float)));
        float *visPx = (float *)MemAlloc((unsigned int)(visFloats * sizeof(float)));
        if (!allPx || !visPx)
        {
            if (allPx) MemFree(allPx);
            if (visPx) MemFree(visPx);
            continue;
        }
        memset(allPx, 0, allFloats * sizeof(float));
        memset(visPx, 0, visFloats * sizeof(float));

        int k = 0;
        for (int i = 0; i < count; i++)
        {
            const MapTreePlacement *p = sys.placements ? &sys.placements[i] : &placements[i];
            if ((int)p->archetype != a)
                continue;
            int b = k * 8;
            Color tint = p->canopyTint;
            if (tint.r == 0 && tint.g == 0 && tint.b == 0)
                tint = WHITE;
            int packedRGB = ((int)tint.r << 16) | ((int)tint.g << 8) | (int)tint.b;
            allPx[b + 0] = p->position.x;
            allPx[b + 1] = p->position.y;
            allPx[b + 2] = p->position.z;
            allPx[b + 3] = p->rotationDeg * DEG2RAD;
            allPx[b + 4] = (p->scaleXZ > 0.1f) ? p->scaleXZ : 1.0f;
            allPx[b + 5] = (p->scaleY > 0.1f) ? p->scaleY : 1.0f;
            allPx[b + 6] = Tree_PackInstanceState(p->phase, p->dryness, p->scorch, 0.0f);
            allPx[b + 7] = (float)packedRGB;
            k++;
        }
        memcpy(visPx, allPx, allFloats * sizeof(float));

        Image instImg = {
            .data = visPx,
            .width = 512,
            .height = rows,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
        };
        sys.instanceTextures[a] = LoadTextureFromImage(instImg);
        if (sys.instanceTextures[a].id != 0)
        {
            SetTextureFilter(sys.instanceTextures[a], TEXTURE_FILTER_POINT);
            SetTextureWrap(sys.instanceTextures[a], TEXTURE_WRAP_CLAMP);
            sys.instanceCounts[a] = typeCount;
            sys.allTexels[a] = allPx;
            sys.visibleTexels[a] = visPx;
            sys.texWidth[a] = 512;
            sys.texHeight[a] = rows;
            sys.lodOffsets[a][0] = 0;
            sys.lodCounts[a][0] = typeCount;
            sys.lastUploadedHash[a] = Tree_HashBytes(visPx, (size_t)typeCount * 8u * sizeof(float));
        }
        else
        {
            MemFree(allPx);
            MemFree(visPx);
        }
    }

    sys.ready = true;
    return sys;
}

void MapProp_SetTreeSnowCoverage(MapTreeSystem *sys, float coverage)
{
    if (!sys)
        return;
    sys->snowCoverage = fminf(1.0f, fmaxf(0.0f, coverage));
}

void MapProp_SetTreeCondition(MapTreeSystem *sys, float globalDryness, float globalScorch)
{
    if (!sys)
        return;
    sys->globalDryness = fminf(1.0f, fmaxf(0.0f, globalDryness));
    sys->globalScorch = fminf(1.0f, fmaxf(0.0f, globalScorch));
}

void MapProp_SetTreeState(MapTreeSystem *sys, MapTreeState state)
{
    if (!sys)
        return;
    switch (state)
    {
    case MAP_TREE_STATE_LUSH:
        MapProp_SetTreeCondition(sys, 0.0f, 0.0f);
        break;
    case MAP_TREE_STATE_AUTUMN:
        MapProp_SetTreeCondition(sys, 0.36f, 0.0f);
        break;
    case MAP_TREE_STATE_WITHERED:
        MapProp_SetTreeCondition(sys, 0.68f, 0.0f);
        break;
    case MAP_TREE_STATE_DRY_BARE:
        MapProp_SetTreeCondition(sys, 1.0f, 0.0f);
        break;
    case MAP_TREE_STATE_SCORCHED:
        MapProp_SetTreeCondition(sys, 1.0f, 0.85f);
        break;
    default:
        break;
    }
}

void MapProp_PrepareTreeSystem(MapTreeSystem *sys, Camera3D cam)
{
    if (!sys || !sys->ready)
        return;

    TreeCameraBasis camBasis = Tree_MakeCameraBasis(cam, sys->drawDistance + 28.0f);
    float fdx = cam.position.x - cam.target.x;
    float fdz = cam.position.z - cam.target.z;
    float focalDist = sqrtf(fdx * fdx + fdz * fdz);
    float zoomFactor = tanf(22.5f * DEG2RAD) / fmaxf(tanf(cam.fovy * 0.5f * DEG2RAD), 0.15f);
    float lod0Dist = focalDist + sys->lod0Distance * zoomFactor;
    float lod1Dist = focalDist + sys->lod1Distance * zoomFactor;
    float fadeHalf = 3.0f; // 6m Bayer screen-door cross-fade band around LOD0/LOD1 and LOD1/LOD2
    float drawDistSq = sys->drawDistance * sys->drawDistance;
    float lod0LoSq = fmaxf(0.0f, lod0Dist - fadeHalf) * fmaxf(0.0f, lod0Dist - fadeHalf);
    float lod0HiSq = (lod0Dist + fadeHalf) * (lod0Dist + fadeHalf);
    float lod1LoSq = fmaxf(0.0f, lod1Dist - fadeHalf) * fmaxf(0.0f, lod1Dist - fadeHalf);
    float lod1HiSq = (lod1Dist + fadeHalf) * (lod1Dist + fadeHalf);

    // Cache exact camera-scaled LOD thresholds on sys so DrawTreeSystem Bayer fade matches Prepare 1:1
    sys->effectiveLod0Dist = lod0Dist;
    sys->effectiveLod1Dist = lod1Dist;

    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        int n = sys->instanceCounts[a];
        if (!sys->modelLoaded[a] || n <= 0 || !sys->allTexels[a] || !sys->visibleTexels[a] || sys->instanceTextures[a].id == 0)
            continue;

        int counts[3] = {0, 0, 0};
        for (int i = 0; i < n; i++)
        {
            const float *src = &sys->allTexels[a][i * 8];
            Vector3 pos = {src[0], src[1], src[2]};
            float boundR = fmaxf(src[4], src[5]) * 5.2f;
            float dx = pos.x - cam.position.x;
            float dy = pos.y - cam.position.y;
            float dz = pos.z - cam.position.z;
            float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq > drawDistSq)
                continue;
            Vector3 center = {pos.x, pos.y + src[5] * 3.5f, pos.z};
            if (!Tree_SphereInCameraView(&camBasis, center, boundR))
                continue;
            if (dSq <= lod0HiSq) counts[0]++;
            if (dSq >= lod0LoSq && dSq <= lod1HiSq) counts[1]++;
            if (dSq >= lod1LoSq) counts[2]++;
        }

        sys->lodOffsets[a][0] = 0;
        sys->lodOffsets[a][1] = counts[0];
        sys->lodOffsets[a][2] = counts[0] + counts[1];
        sys->lodCounts[a][0] = counts[0];
        sys->lodCounts[a][1] = counts[1];
        sys->lodCounts[a][2] = counts[2];

        int totalVis = counts[0] + counts[1] + counts[2];
        if (totalVis <= 0)
            continue;

        int writeIdx[3] = {sys->lodOffsets[a][0], sys->lodOffsets[a][1], sys->lodOffsets[a][2]};
        for (int i = 0; i < n; i++)
        {
            const float *src = &sys->allTexels[a][i * 8];
            Vector3 pos = {src[0], src[1], src[2]};
            float boundR = fmaxf(src[4], src[5]) * 5.2f;
            float dx = pos.x - cam.position.x;
            float dy = pos.y - cam.position.y;
            float dz = pos.z - cam.position.z;
            float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq > drawDistSq)
                continue;
            Vector3 center = {pos.x, pos.y + src[5] * 3.5f, pos.z};
            if (!Tree_SphereInCameraView(&camBasis, center, boundR))
                continue;
            if (dSq <= lod0HiSq)
                memcpy(&sys->visibleTexels[a][(writeIdx[0]++) * 8], src, 8u * sizeof(float));
            if (dSq >= lod0LoSq && dSq <= lod1HiSq)
                memcpy(&sys->visibleTexels[a][(writeIdx[1]++) * 8], src, 8u * sizeof(float));
            if (dSq >= lod1LoSq)
                memcpy(&sys->visibleTexels[a][(writeIdx[2]++) * 8], src, 8u * sizeof(float));
        }

        unsigned int h = Tree_HashBytes(sys->visibleTexels[a], (size_t)totalVis * 8u * sizeof(float));
        if (h != sys->lastUploadedHash[a])
        {
            int rowsToUpload = (totalVis * 2 + 511) / 512;
            if (rowsToUpload < 1) rowsToUpload = 1;
            if (rowsToUpload > sys->texHeight[a]) rowsToUpload = sys->texHeight[a];
            Rectangle rec = {0.0f, 0.0f, 512.0f, (float)rowsToUpload};
            UpdateTextureRec(sys->instanceTextures[a], rec, sys->visibleTexels[a]);
            sys->lastUploadedHash[a] = h;
        }
    }
    sys->preparedThisFrame = true;
}

static void Tree_DrawMeshInstanced(const Mesh *mesh, int count)
{
    if (!mesh || count <= 0)
        return;
    rlEnableVertexArray(mesh->vaoId);
    if (mesh->indices)
        rlDrawVertexArrayElementsInstanced(0, mesh->triangleCount * 3, NULL, count);
    else
        rlDrawVertexArrayInstanced(0, mesh->vertexCount, count);
    rlDisableVertexArray();
}

void MapProp_DrawTreeSystem(const MapTreeSystem *sys, float time, Vector2 windDirection, float windStrength)
{
    if (!sys || !sys->ready)
        return;

    Shader sh = Tree_GetShader();
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return;

    Nature_BeginWindReceiverShader(sh);
    Nature_UpdateShader(sh, time, windDirection, windStrength,
                        false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
    MapShadow_UpdateShader(sh);
    rlDisableBackfaceCulling();

    float distSq = sys->drawDistance * sys->drawDistance;
    float gDry = sys->globalDryness;
    float gSco = sys->globalScorch;
    if (s_treeLocDrawDistSq >= 0) SetShaderValue(sh, s_treeLocDrawDistSq, &distSq, SHADER_UNIFORM_FLOAT);
    if (s_treeLocGlobalDryness >= 0) SetShaderValue(sh, s_treeLocGlobalDryness, &gDry, SHADER_UNIFORM_FLOAT);
    if (s_treeLocGlobalScorch >= 0) SetShaderValue(sh, s_treeLocGlobalScorch, &gSco, SHADER_UNIFORM_FLOAT);
    if (s_treeLocInstTex >= 0)
    {
        int unit0 = 0;
        SetShaderValue(sh, s_treeLocInstTex, &unit0, SHADER_UNIFORM_INT);
    }
    Texture2D atlas = Tree_GetFoliageAtlas();
    if (s_treeLocAtlasTex >= 0 && atlas.id != 0)
    {
        int unit6 = 6;
        rlActiveTextureSlot(6);
        rlEnableTexture(atlas.id);
        SetShaderValue(sh, s_treeLocAtlasTex, &unit6, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }

    Matrix mvp = MatrixMultiply(MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview()), rlGetMatrixProjection());
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MVP], mvp);

    float globalSnow = MapProp_GetSnow();
    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a] || sys->instanceCounts[a] <= 0 || sys->instanceTextures[a].id == 0)
            continue;
        float snowAmt = 0.0f;
        if (sys->snowCoverage >= 0.0f)
            snowAmt = sys->snowCoverage;
        else
            snowAmt = Tree_IsSnowArchetype(a) ? fmaxf(0.70f, globalSnow) : (globalSnow * 0.55f);
        if (s_treeLocSnowAmount >= 0)
            SetShaderValue(sh, s_treeLocSnowAmount, &snowAmt, SHADER_UNIFORM_FLOAT);

        rlActiveTextureSlot(0);
        rlEnableTexture(sys->instanceTextures[a].id);
        for (int lod = 0; lod < 3; lod++)
        {
            int count = sys->lodCounts[a][lod];
            if (count <= 0)
                continue;
            int offset = sys->lodOffsets[a][lod];
            if (s_treeLocBaseOffset >= 0)
                SetShaderValue(sh, s_treeLocBaseOffset, &offset, SHADER_UNIFORM_INT);
            if (s_treeLocLodFade >= 0)
            {
                float eLod0 = (sys->effectiveLod0Dist > 1.0f) ? sys->effectiveLod0Dist : sys->lod0Distance;
                float eLod1 = (sys->effectiveLod1Dist > 1.0f) ? sys->effectiveLod1Dist : sys->lod1Distance;
                float lodFade[4] = {eLod0, eLod1, 3.0f, (float)lod};
                SetShaderValue(sh, s_treeLocLodFade, lodFade, SHADER_UNIFORM_VEC4);
            }
            Tree_DrawMeshInstanced(&sys->models[a][lod].meshes[0], count);
        }
    }
    rlEnableBackfaceCulling();
    Nature_EndWindReceiverShader();
}

void MapProp_DrawTreeSystemShadowCasters(const MapTreeSystem *sys, Shader depthShader,
                                         float time, Vector2 windDirection, float windStrength)
{
    (void)depthShader;
    if (!sys || !sys->ready || GfxQuality_Get() < GFX_HIGH)
        return;

    Tree_GetShader();
    Shader sh = s_treeShadowShader;
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return;

    Nature_BeginWindReceiverShader(sh);
    Nature_UpdateShadowShader(sh, time, windDirection, windStrength,
                              false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
    rlDisableBackfaceCulling();
    float gDry = sys->globalDryness;
    float gSco = sys->globalScorch;
    if (s_treeShadowLocGlobalDryness >= 0) SetShaderValue(sh, s_treeShadowLocGlobalDryness, &gDry, SHADER_UNIFORM_FLOAT);
    if (s_treeShadowLocGlobalScorch >= 0) SetShaderValue(sh, s_treeShadowLocGlobalScorch, &gSco, SHADER_UNIFORM_FLOAT);
    if (s_treeShadowLocInstTex >= 0)
    {
        int unit0 = 0;
        SetShaderValue(sh, s_treeShadowLocInstTex, &unit0, SHADER_UNIFORM_INT);
    }
    Texture2D atlas = Tree_GetFoliageAtlas();
    if (s_treeShadowLocAtlasTex >= 0 && atlas.id != 0)
    {
        int unit6 = 6;
        rlActiveTextureSlot(6);
        rlEnableTexture(atlas.id);
        SetShaderValue(sh, s_treeShadowLocAtlasTex, &unit6, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
    }
    Matrix mvp = EnvShadow_GetLightVP();
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MVP], mvp);

    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a] || sys->instanceCounts[a] <= 0 || sys->instanceTextures[a].id == 0)
            continue;
        int totalVis = sys->lodCounts[a][0] + sys->lodCounts[a][1] + sys->lodCounts[a][2];
        if (totalVis <= 0)
            continue;
        rlActiveTextureSlot(0);
        rlEnableTexture(sys->instanceTextures[a].id);
        int offset = 0;
        if (s_treeShadowLocBaseOffset >= 0)
            SetShaderValue(sh, s_treeShadowLocBaseOffset, &offset, SHADER_UNIFORM_INT);
        Tree_DrawMeshInstanced(&sys->models[a][1].meshes[0], totalVis);
    }
    rlEnableBackfaceCulling();
    Nature_EndWindReceiverShader();
}

Vector3 MapProp_ResolveTreeSystemCollision(const MapTreeSystem *sys, Vector3 pos, float radius)
{
    if (!sys || !sys->ready || !sys->placements || sys->totalPlacements <= 0)
        return pos;
    for (int i = 0; i < sys->totalPlacements; i++)
    {
        const MapTreePlacement *p = &sys->placements[i];
        float trunkR = (p->archetype == MAP_TREE_BAMBOO) ? (0.45f * p->scaleXZ)
                     : (p->archetype == MAP_TREE_MEADOW_BIRCH || p->archetype == MAP_TREE_FROSTED_BIRCH) ? (0.30f * p->scaleXZ)
                     : (0.48f * p->scaleXZ);
        float minDist = trunkR + radius;
        float dx = pos.x - p->position.x;
        float dz = pos.z - p->position.z;
        float dSq = dx * dx + dz * dz;
        if (dSq < minDist * minDist && dSq > 1e-6f)
        {
            float d = sqrtf(dSq);
            float push = (minDist - d) / d;
            pos.x += dx * push;
            pos.z += dz * push;
        }
    }
    return pos;
}

void MapFlora_EvaluateTreeUnderstorey(float x, float z,
                                      const MapTreePlacement *trees, int treeCount,
                                      float *outGrassDensityScale, float *outGrassHeightScale,
                                      float *outShrubBoost)
{
    float dens = 1.0f;
    float hScale = 1.0f;
    float shrubBoost = 1.0f;
    if (trees && treeCount > 0)
    {
        for (int i = 0; i < treeCount; i++)
        {
            const MapTreePlacement *t = &trees[i];
            float canopyR = 3.2f * ((t->scaleXZ > 0.1f) ? t->scaleXZ : 1.0f);
            float dx = x - t->position.x;
            float dz = z - t->position.z;
            float dSq = dx * dx + dz * dz;
            float outerR = canopyR * 1.48f;
            if (dSq >= outerR * outerR)
                continue;
            float d = sqrtf(dSq);
            float rn = d / fmaxf(canopyR, 0.5f);
            if (rn < 0.30f)
            {
                dens = 0.0f;
                hScale = fminf(hScale, 0.35f);
            }
            else if (rn < 1.05f)
            {
                // Bare dry canopies let more sunlight through to the understorey
                float dryRelax = fminf(1.0f, fmaxf(0.0f, t->dryness)) * 0.65f;
                float shadeT = (rn - 0.30f) / 0.75f;
                float minDens = 0.45f + dryRelax * 0.45f;
                dens = fminf(dens, minDens + (0.93f - minDens) * shadeT);
                hScale = fminf(hScale, 0.55f + 0.35f * shadeT);
            }
            if (rn >= 0.72f && rn <= 1.45f)
            {
                float ring = 1.0f - fabsf(rn - 1.08f) / 0.38f;
                if (ring > 0.0f)
                    shrubBoost = fmaxf(shrubBoost, 1.0f + 2.2f * ring);
            }
        }
    }
    if (outGrassDensityScale) *outGrassDensityScale = dens;
    if (outGrassHeightScale) *outGrassHeightScale = hScale;
    if (outShrubBoost) *outShrubBoost = shrubBoost;
}

void MapProp_UnloadTreeSystem(MapTreeSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (sys->modelLoaded[a])
        {
            for (int lod = 0; lod < 3; lod++)
                UnloadModel(sys->models[a][lod]);
            sys->modelLoaded[a] = false;
        }
        if (sys->instanceTextures[a].id != 0)
        {
            UnloadTexture(sys->instanceTextures[a]);
            sys->instanceTextures[a] = (Texture2D){0};
        }
        if (sys->allTexels[a]) { MemFree(sys->allTexels[a]); sys->allTexels[a] = NULL; }
        if (sys->visibleTexels[a]) { MemFree(sys->visibleTexels[a]); sys->visibleTexels[a] = NULL; }
        sys->instanceCounts[a] = 0;
    }
    if (sys->placements)
    {
        MemFree(sys->placements);
        sys->placements = NULL;
    }
    sys->totalPlacements = 0;
    sys->ready = false;
}

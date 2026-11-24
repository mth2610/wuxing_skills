// Procedural 3D Xianxia & Alpine AAA Trees System (GPU-Instanced, 3-LOD, Indexed, 1024x1024 Procedural Frond Atlas)
// Ported from WebGL2 AAA Showcase into C99 / Vulkan (rlvk) — Included into map_props.c.

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

static float Tree_Hash(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffu) / 16777216.0f;
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

// =========================================================================
// PROCEDURAL 1024x1024 (2x2) BOTANICAL FROND ATLAS BAKERY (C99)
// Q0 (0..511, 0..511): Pine/Spruce Bough Frond (~650 needles)
// Q1 (512..1023, 0..511): Cedar/Taiga Drooping Scale-Foliage Spray
// Q2 (0..511, 512..1023): Birch/Broadleaf Twigs & Leaves (neutral tintable)
// Q3 (512..1023, 512..1023): Willow / Bamboo Lanceolate Frond Spray
// =========================================================================

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

static void TreeAtlas_DrawStroke(unsigned char *px, int atlasW, int ox, int oy,
                                 float x0, float y0, float x1, float y1,
                                 float rad0, float rad1, Color c0, Color c1)
{
    float minX = fminf(x0, x1) - fmaxf(rad0, rad1) - 1.5f;
    float maxX = fmaxf(x0, x1) + fmaxf(rad0, rad1) + 1.5f;
    float minY = fminf(y0, y1) - fmaxf(rad0, rad1) - 1.5f;
    float maxY = fmaxf(y0, y1) + fmaxf(rad0, rad1) + 1.5f;
    int ix0 = (int)fmaxf(0.0f, floorf(minX));
    int ix1 = (int)fminf(511.0f, ceilf(maxX));
    int iy0 = (int)fmaxf(0.0f, floorf(minY));
    int iy1 = (int)fminf(511.0f, ceilf(maxY));

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
                TreeAtlas_BlendPixel(px, atlasW, atlasW, ox + x, oy + y, r, g, b, cov);
            }
        }
    }
}

static void TreeAtlas_DrawLeaf(unsigned char *px, int atlasW, int ox, int oy,
                               float cx, float cy, float ang, float len, float wid, Color cBase, Color cTip)
{
    float ca = cosf(ang), sa = sinf(ang);
    float rad = fmaxf(len, wid) + 2.0f;
    int ix0 = (int)fmaxf(0.0f, floorf(cx - rad));
    int ix1 = (int)fminf(511.0f, ceilf(cx + rad));
    int iy0 = (int)fmaxf(0.0f, floorf(cy - rad));
    int iy1 = (int)fminf(511.0f, ceilf(cy + rad));

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
            float maxW = wid * sinf(u * PI);
            float ad = fabsf(ly);
            if (ad < maxW + 1.0f)
            {
                float cov = fminf(1.0f, fmaxf(0.0f, maxW + 0.7f - ad));
                unsigned char r = (unsigned char)(cBase.r + (cTip.r - cBase.r) * u);
                unsigned char g = (unsigned char)(cBase.g + (cTip.g - cBase.g) * u);
                unsigned char b = (unsigned char)(cBase.b + (cTip.b - cBase.b) * u);
                TreeAtlas_BlendPixel(px, atlasW, atlasW, ox + x, oy + y, r, g, b, cov);
            }
        }
    }
}

static void TreeAtlas_BakePineQuadrant(unsigned char *px, int atlasW, int ox, int oy)
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
        {0.14f, -1, 165.0f, -0.78f}, {0.17f,  1, 160.0f,  0.76f},
        {0.30f, -1, 185.0f, -0.70f}, {0.33f,  1, 182.0f,  0.68f},
        {0.48f, -1, 158.0f, -0.62f}, {0.51f,  1, 156.0f,  0.60f},
        {0.65f, -1, 122.0f, -0.54f}, {0.68f,  1, 120.0f,  0.52f},
        {0.80f, -1,  84.0f, -0.46f}, {0.83f,  1,  82.0f,  0.44f}
    };

    float sx0 = 256.0f, sy0 = 502.0f, sx1 = 256.0f, sy1 = 34.0f;
    for (int s = 0; s < 10; s++)
    {
        float bx = sx0 + (sx1 - sx0) * shoots[s].t;
        float by = sy0 + (sy1 - sy0) * shoots[s].t;
        float angle = -PI * 0.5f + shoots[s].ang;
        float ex = bx + cosf(angle) * shoots[s].len;
        float ey = by + sinf(angle) * shoots[s].len;
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, bx, by, ex, ey, 3.8f * (1.0f - shoots[s].t * 0.5f), 1.2f, stemC0, stemC1);

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
            // Fuller needle strokes (3.0 -> 1.7px) preserve lush evergreen bough silhouettes across mipmaps
            TreeAtlas_DrawStroke(px, atlasW, ox, oy, nx, ny, nx + cosf(nang) * nlen, ny + sinf(nang) * nlen, 3.0f, 1.7f, pal[0], tipC);
        }
    }
    // Central spine & lush central needles
    TreeAtlas_DrawStroke(px, atlasW, ox, oy, sx0, sy0, sx1, sy1, 6.2f, 1.8f, stemC0, stemC1);
    for (int i = 0; i < 115; i++)
    {
        float t = (float)i / 114.0f;
        float nx = sx0 + (Tree_NextRand(&rng) - 0.5f) * 4.5f;
        float ny = (sy0 - 12.0f) + (sy1 - (sy0 - 12.0f)) * t;
        float side = (i & 1) ? -1.0f : 1.0f;
        float nang = -PI * 0.5f + side * (0.32f + Tree_NextRand(&rng) * 0.50f);
        float nlen = 56.0f * (1.0f - t * 0.32f) * (0.78f + Tree_NextRand(&rng) * 0.44f);
        int ci = 1 + (int)(Tree_NextRand(&rng) * 4.99f);
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, nx, ny, nx + cosf(nang) * nlen, ny + sinf(nang) * nlen, 3.2f, 1.8f, pal[0], pal[ci]);
    }
}

static void TreeAtlas_BakeCedarQuadrant(unsigned char *px, int atlasW, int ox, int oy)
{
    unsigned int rng = 5502u;
    const Color pal[5] = {
        {13, 36, 28, 255}, {18, 47, 37, 255}, {23, 58, 45, 255}, {30, 72, 56, 255}, {44, 96, 78, 255}
    };
    const float rayAngs[7] = {-0.56f, -0.34f, -0.14f, 0.02f, 0.18f, 0.38f, 0.58f};
    const float rayLens[7] = {285.0f, 360.0f, 425.0f, 450.0f, 415.0f, 345.0f, 270.0f};
    for (int r = 0; r < 7; r++)
    {
        float ang = -PI * 0.5f + rayAngs[r];
        float bx = 256.0f, by = 496.0f;
        float ex = bx + cosf(ang) * rayLens[r];
        float ey = by + sinf(ang) * rayLens[r];
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, bx, by, ex, ey, 3.2f, 1.2f, (Color){45, 32, 24, 255}, pal[1]);
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
                TreeAtlas_DrawStroke(px, atlasW, ox, oy, sx, sy, sx + cosf(nang) * nlen, sy + sinf(nang) * nlen, 3.4f, 1.8f, pal[0], pal[ci]);
            }
        }
    }
}

static void TreeAtlas_BakeBroadleafQuadrant(unsigned char *px, int atlasW, int ox, int oy)
{
    unsigned int rng = 6603u;
    // Neutral bright-albedo leaf spray so per-archetype vertex colors tint cleanly
    // (Frosted Birch amber, Meadow Oak/Birch spring-jade, Peach Blossom rose-ivory, Acacia olive)
    Color twigC = {96, 82, 70, 255};
    Color leafCore = {148, 152, 138, 255};
    Color leafMid  = {198, 204, 190, 255};
    Color leafTip  = {248, 250, 244, 255};

    float sx0 = 256.0f, sy0 = 496.0f, sx1 = 256.0f, sy1 = 36.0f;
    TreeAtlas_DrawStroke(px, atlasW, ox, oy, sx0, sy0, sx1, sy1, 4.4f, 1.2f, twigC, twigC);

    for (int b = 0; b < 12; b++)
    {
        float t = 0.12f + 0.78f * ((float)b / 11.0f);
        float side = (b & 1) ? 1.0f : -1.0f;
        float bx = sx0 + (Tree_NextRand(&rng) - 0.5f) * 6.0f;
        float by = sy0 + (sy1 - sy0) * t;
        float ang = -PI * 0.5f + side * (0.42f + Tree_NextRand(&rng) * 0.48f);
        float blen = 195.0f * (1.0f - t * 0.38f) * (0.85f + Tree_NextRand(&rng) * 0.28f);
        float mx = bx + cosf(ang) * blen * 0.52f;
        float my = by + sinf(ang) * blen * 0.52f;
        float ang2 = ang + side * (0.14f + Tree_NextRand(&rng) * 0.18f);
        float ex = mx + cosf(ang2) * blen * 0.50f;
        float ey = my + sinf(ang2) * blen * 0.50f;
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, bx, by, mx, my, 2.6f, 1.6f, twigC, twigC);
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, mx, my, ex, ey, 1.6f, 0.9f, twigC, twigC);

        int leaves = 20;
        for (int l = 0; l < leaves; l++)
        {
            float lt = 0.08f + 0.92f * ((float)l / (float)(leaves - 1));
            float lx = (lt < 0.5f)
                ? (bx + (mx - bx) * (lt * 2.0f))
                : (mx + (ex - mx) * ((lt - 0.5f) * 2.0f));
            float ly = (lt < 0.5f)
                ? (by + (my - by) * (lt * 2.0f))
                : (my + (ey - my) * ((lt - 0.5f) * 2.0f));
            lx += (Tree_NextRand(&rng) - 0.5f) * 22.0f;
            ly += (Tree_NextRand(&rng) - 0.5f) * 22.0f;
            float baseAng = (lt < 0.5f) ? ang : ang2;
            float lang = baseAng + ((l & 1) ? 0.62f : -0.62f) + (Tree_NextRand(&rng) - 0.5f) * 0.55f;
            float llen = 30.0f + Tree_NextRand(&rng) * 26.0f;
            float lwid = 15.0f + Tree_NextRand(&rng) * 12.0f;
            Color cA = (lt < 0.35f) ? leafCore : leafMid;
            TreeAtlas_DrawLeaf(px, atlasW, ox, oy, lx, ly, lang, llen, lwid, cA, leafTip);
        }
    }
    // Dense organic crown tip cluster
    for (int l = 0; l < 32; l++)
    {
        float ang = -PI * 0.5f + (Tree_NextRand(&rng) - 0.5f) * 2.2f;
        float dist = sqrtf(Tree_NextRand(&rng)) * 78.0f;
        float llen = 32.0f + Tree_NextRand(&rng) * 24.0f;
        float lwid = 16.0f + Tree_NextRand(&rng) * 11.0f;
        Color cA = (dist < 32.0f) ? leafCore : leafMid;
        TreeAtlas_DrawLeaf(px, atlasW, ox, oy, sx1 + cosf(ang) * dist, sy1 + 32.0f + sinf(ang) * dist,
                           ang + (Tree_NextRand(&rng) - 0.5f) * 0.5f, llen, lwid, cA, leafTip);
    }
}

static void TreeAtlas_BakeWillowBambooQuadrant(unsigned char *px, int atlasW, int ox, int oy)
{
    unsigned int rng = 7704u;
    Color stemC = {104, 134, 68, 255};
    Color leafBase = {152, 196, 108, 255};
    Color leafTip  = {224, 242, 162, 255};
    // 5 slender draping / lanceolate shoots
    for (int s = 0; s < 5; s++)
    {
        float xOff = 96.0f + (float)s * 80.0f + (Tree_NextRand(&rng) - 0.5f) * 18.0f;
        TreeAtlas_DrawStroke(px, atlasW, ox, oy, xOff, 20.0f, xOff + (Tree_NextRand(&rng) - 0.5f) * 32.0f, 492.0f, 2.0f, 0.8f, stemC, stemC);
        for (int l = 0; l < 18; l++)
        {
            float t = (float)l / 17.0f;
            float ly = 32.0f + t * 440.0f;
            float side = (l & 1) ? 1.0f : -1.0f;
            float lang = PI * 0.5f + side * (0.35f + Tree_NextRand(&rng) * 0.35f);
            TreeAtlas_DrawLeaf(px, atlasW, ox, oy, xOff, ly, lang, 52.0f + Tree_NextRand(&rng) * 24.0f, 7.5f + Tree_NextRand(&rng) * 3.0f, leafBase, leafTip);
        }
    }
}

static Texture2D s_treeFoliageAtlas = {0};

static Texture2D Tree_GetFoliageAtlas(void)
{
    if (s_treeFoliageAtlas.id != 0)
        return s_treeFoliageAtlas;

    const int W = 1024;
    unsigned char *px = (unsigned char *)MemAlloc((unsigned int)(W * W * 4));
    if (!px)
        return (Texture2D){0};
    memset(px, 0, (size_t)W * (size_t)W * 4u);

    TreeAtlas_BakePineQuadrant(px, W, 0, 0);
    TreeAtlas_BakeCedarQuadrant(px, W, 512, 0);
    TreeAtlas_BakeBroadleafQuadrant(px, W, 0, 512);
    TreeAtlas_BakeWillowBambooQuadrant(px, W, 512, 512);

    Image img = {
        .data = px,
        .width = W,
        .height = W,
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
// BOTANICAL GEOMETRY PRIMITIVES (PORTED FROM WEBGL2 AAA SHOWCASE)
// =========================================================================

static void Tree_BasisFromDir(Vector3 dir, Vector3 *outRight, Vector3 *outFwd)
{
    Vector3 up = Vector3Normalize(dir);
    Vector3 ref = (fabsf(up.y) < 0.92f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    *outRight = Vector3Normalize(Vector3CrossProduct(ref, up));
    *outFwd = Vector3CrossProduct(up, *outRight);
}

// Bark tube: uv.x = -1.0 (bark sentinel), uv.y = baked AO
static void Tree_AddTube(TreeMeshBuilder *b, Vector3 a, Vector3 c, float rA, float rC,
                         int sides, Color colA, Color colC, float compA, float compC,
                         float rootFlare)
{
    Vector3 dir = Vector3Subtract(c, a);
    if (Vector3LengthSqr(dir) < 1e-6f)
        return;
    Vector3 right, fwd;
    Tree_BasisFromDir(dir, &right, &fwd);

    Color ca = {colA.r, colA.g, colA.b, (unsigned char)fminf(255.0f, fmaxf(0.0f, compA * 255.0f))};
    Color cc = {colC.r, colC.g, colC.b, (unsigned char)fminf(255.0f, fmaxf(0.0f, compC * 255.0f))};
    Vector2 uvA = {-1.0f, 0.68f};
    Vector2 uvC = {-1.0f, 0.95f};

    for (int i = 0; i < sides; i++)
    {
        float ang0 = (float)i * (2.0f * PI / (float)sides);
        float ang1 = (float)(i + 1) * (2.0f * PI / (float)sides);
        Vector3 n0 = Vector3Add(Vector3Scale(right, cosf(ang0)), Vector3Scale(fwd, sinf(ang0)));
        Vector3 n1 = Vector3Add(Vector3Scale(right, cosf(ang1)), Vector3Scale(fwd, sinf(ang1)));
        float flare0 = (rootFlare > 0.01f) ? (1.0f + rootFlare * (0.55f + 0.45f * sinf(ang0 * 5.0f))) : 1.0f;
        float flare1 = (rootFlare > 0.01f) ? (1.0f + rootFlare * (0.55f + 0.45f * sinf(ang1 * 5.0f))) : 1.0f;
        Vector3 p00 = Vector3Add(a, Vector3Scale(n0, rA * flare0));
        Vector3 p01 = Vector3Add(a, Vector3Scale(n1, rA * flare1));
        Vector3 p10 = Vector3Add(c, Vector3Scale(n0, rC));
        Vector3 p11 = Vector3Add(c, Vector3Scale(n1, rC));
        TreeBuilder_PushTri(b, p00, n0, uvA, ca, p10, n0, uvC, cc, p11, n1, uvC, cc);
        TreeBuilder_PushTri(b, p00, n0, uvA, ca, p11, n1, uvC, cc, p01, n1, uvA, ca);
    }
}

// Curved & cupped 3D alpha-tested botanical frond card (ported from WebGL2 addArchedFrond)
// quadrant: 0=Pine, 1=Cedar, 2=Broadleaf/Birch, 3=Willow/Bamboo
static void Tree_AddArchedFrond(TreeMeshBuilder *b, Vector3 origin, float yaw, float pitch, float roll,
                                float length, float width, float droop, float cup,
                                float aoBase, float aoTip, Color tint, float windFlex, int quadrant, int lenSegs)
{
    float uMin = (quadrant & 1) ? 0.505f : 0.005f;
    float uMax = (quadrant & 1) ? 0.995f : 0.495f;
    float vMin = (quadrant >= 2) ? 0.505f : 0.005f;
    float vMax = (quadrant >= 2) ? 0.995f : 0.495f;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);

    // Local forward, right, up basis (fwd x baseRight = +Y upward!)
    Vector3 fwd = {sy * cp, -sp, cy * cp};
    Vector3 baseRight = {cy, 0.0f, -sy};
    Vector3 baseUp = Vector3CrossProduct(fwd, baseRight);
    Vector3 right = Vector3Add(Vector3Scale(baseRight, cr), Vector3Scale(baseUp, sr));
    Vector3 up = Vector3Subtract(Vector3Scale(baseUp, cr), Vector3Scale(baseRight, sr));

    Vector3 gridP[4][3];
    Vector3 gridN[4][3];
    Vector2 gridUV[4][3];
    Color gridC[4][3];

    int rows = (lenSegs < 1) ? 1 : (lenSegs > 3 ? 3 : lenSegs);
    for (int r = 0; r <= rows; r++)
    {
        float t = (float)r / (float)rows;
        float yDrop = -droop * t * t;
        float ao = aoBase + (aoTip - aoBase) * t;
        float flex = windFlex * (0.35f + 0.65f * t);
        unsigned char ca = (unsigned char)fminf(255.0f, fmaxf(0.0f, flex * 255.0f));
        Color col = {
            (unsigned char)fminf(255.0f, (float)tint.r * ao),
            (unsigned char)fminf(255.0f, (float)tint.g * ao),
            (unsigned char)fminf(255.0f, (float)tint.b * ao),
            ca
        };
        // V goes from bottom (vMax, stem base) to top (vMin, frond tip)
        float vCoord = vMax + (vMin - vMax) * t;

        for (int c = 0; c <= 2; c++)
        {
            float s = (float)c - 1.0f; // -1, 0, +1
            float cupLift = -cup * (s * s) * (0.4f + 0.6f * sinf(t * PI));
            Vector3 pos = Vector3Add(origin, Vector3Scale(fwd, length * t));
            pos = Vector3Add(pos, Vector3Scale(right, s * width * 0.5f));
            pos = Vector3Add(pos, Vector3Scale(up, cupLift));
            pos.y += yDrop;

            // Upward-biased volumetric normal so tops catch crisp snow & sunlight
            Vector3 n = Vector3Normalize(Vector3Add(up, Vector3Add(Vector3Scale(right, s * 0.25f), (Vector3){0.0f, 0.35f, 0.0f})));
            gridP[r][c] = pos;
            gridN[r][c] = n;
            gridUV[r][c] = (Vector2){uMin + (uMax - uMin) * ((float)c * 0.5f), vCoord};
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

// 3-card volumetric conifer bough spray (main upper arched frond + 2 angled flank fronds)
static void Tree_AddVolumetricConiferBough(TreeMeshBuilder *b, Vector3 origin, float yaw, float pitch,
                                           float length, float width, float droop, float aoCore, float aoTip,
                                           Color tint, float windFlex, int quadrant, int lod, unsigned int *rng)
{
    int segs = (lod == 0) ? 2 : 1;
    Tree_AddArchedFrond(b, origin, yaw, pitch, (Tree_NextRand(rng) - 0.5f) * 0.14f,
                        length, width, droop, width * 0.22f, aoCore, aoTip, tint, windFlex, quadrant, segs);
    if (lod <= 1)
    {
        for (int sd = -1; sd <= 1; sd += 2)
        {
            Vector3 fOrig = {origin.x + sinf(yaw) * length * 0.14f, origin.y - 0.05f, origin.z + cosf(yaw) * length * 0.14f};
            Tree_AddArchedFrond(b, fOrig,
                                yaw + (float)sd * (0.28f + Tree_NextRand(rng) * 0.16f),
                                pitch - 0.14f,
                                (float)sd * (0.48f + Tree_NextRand(rng) * 0.18f),
                                length * (0.82f + Tree_NextRand(rng) * 0.14f),
                                width * 0.84f, droop * 1.08f, width * 0.18f,
                                aoCore * 0.82f, aoTip * 0.92f, tint, windFlex, quadrant, 1);
        }
    }
}

// Multi-angle 3D volumetric broadleaf leaf-cloud cluster (prevents flat pancake canopy cards)
static void Tree_AddVolumetricBroadleafCluster(TreeMeshBuilder *b, Vector3 center, float baseYaw,
                                               float radius, float droop, float aoCore, float aoTip,
                                               Color tint, float windFlex, int lod, unsigned int *rng)
{
    int count = (lod == 0) ? 4 : (lod == 1 ? 2 : 1);
    int segs = (lod == 0) ? 2 : 1;
    for (int i = 0; i < count; i++)
    {
        float yaw = baseYaw + (float)i * (2.0f * PI / (float)count) + (Tree_NextRand(rng) - 0.5f) * 0.45f;
        float pitch = -0.34f + (float)(i & 1) * 0.36f + (Tree_NextRand(rng) - 0.5f) * 0.24f;
        float roll = ((i & 1) ? 0.42f : -0.42f) + (Tree_NextRand(rng) - 0.5f) * 0.25f;
        Vector3 orig = {
            center.x + (Tree_NextRand(rng) - 0.5f) * radius * 0.38f,
            center.y + (Tree_NextRand(rng) - 0.42f) * radius * 0.32f,
            center.z + (Tree_NextRand(rng) - 0.5f) * radius * 0.38f
        };
        float len = radius * (0.95f + Tree_NextRand(rng) * 0.34f);
        float wid = radius * (0.82f + Tree_NextRand(rng) * 0.28f);
        float tv = 0.88f + Tree_NextRand(rng) * 0.20f;
        Color c = {
            (unsigned char)fminf(255.0f, (float)tint.r * tv),
            (unsigned char)fminf(255.0f, (float)tint.g * tv),
            (unsigned char)fminf(255.0f, (float)tint.b * tv),
            255
        };
        Tree_AddArchedFrond(b, orig, yaw, pitch, roll, len, wid, droop, wid * 0.20f,
                            aoCore, aoTip, c, windFlex, 2, segs);
    }
}

static Mesh Tree_BuildArchetypeMesh(MapTreeArchetype archetype, int lod)
{
    TreeMeshBuilder b = {0};
    if (!TreeBuilder_Init(&b, lod == 0 ? 8192 : (lod == 1 ? 3072 : 1024)))
        return (Mesh){0};

    unsigned int rng = 1101u + (unsigned int)archetype * 337u;
    int trunkSides = (lod == 0) ? 7 : (lod == 1 ? 5 : 4);
    int boughSides = (lod == 0) ? 5 : 4;

    if (archetype == MAP_TREE_ALPINE_SNOW_SPRUCE || archetype == MAP_TREE_COLUMNAR_TAIGA_SPRUCE || archetype == MAP_TREE_OLD_GROWTH_FIR)
    {
        // Ported from WebGL2 Showcase Species 1, 4, 5: Straight dark trunk + flared roots + 9-10 drooping frond whorls + sharp spire apex!
        bool isSlim = (archetype == MAP_TREE_COLUMNAR_TAIGA_SPRUCE);
        bool isBroad = (archetype == MAP_TREE_OLD_GROWTH_FIR);
        int quad = isSlim ? 1 : 0;
        Color barkA = {56, 42, 32, 255};
        Color barkB = {74, 58, 44, 255};

        float H = isSlim ? 9.2f : (isBroad ? 8.2f : 8.6f);
        float baseR = isSlim ? 0.24f : (isBroad ? 0.36f : 0.30f);
        int trunkSegs = (lod == 0) ? 8 : 5;
        Vector3 prev = {0.0f, -0.25f, 0.0f};
        float prevR = baseR;
        for (int i = 1; i <= trunkSegs; i++)
        {
            float t = (float)i / (float)trunkSegs;
            Vector3 cur = {sinf(t * 2.3f) * 0.09f, -0.25f + t * (H + 0.15f), cosf(t * 1.9f) * 0.07f - 0.07f};
            float curR = baseR * (1.0f - t * 0.92f);
            Tree_AddTube(&b, prev, cur, prevR, curR, trunkSides, barkA, barkB, t * 0.35f, t * 0.45f, (i == 1) ? 0.58f : 0.0f);
            prev = cur;
            prevR = curR;
        }
        // 4 flared alpine root buttresses
        if (lod <= 1)
        {
            for (int r = 0; r < 4; r++)
            {
                float a = (float)r * (PI * 0.5f) + 0.35f;
                Vector3 r0 = {cosf(a) * 0.14f, 0.55f, sinf(a) * 0.14f};
                Vector3 r1 = {cosf(a) * 0.85f, -0.22f, sinf(a) * 0.85f};
                Tree_AddTube(&b, r0, r1, 0.12f, 0.03f, 4, barkA, barkA, 0.0f, 0.0f, 0.0f);
            }
        }

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
                float yaw = (float)bi * (2.0f * PI / (float)bCount) + (float)wi * 0.68f + (Tree_NextRand(&rng) - 0.5f) * 0.24f;
                Vector3 orig = {tx, y + (Tree_NextRand(&rng) - 0.5f) * 0.14f, tz};
                float len = bLen * (0.86f + Tree_NextRand(&rng) * 0.26f);
                if (lod == 0 && wi < 5)
                {
                    Vector3 bTip = {orig.x + sinf(yaw) * len * 0.68f, orig.y - droop * 0.65f, orig.z + cosf(yaw) * len * 0.68f};
                    Tree_AddTube(&b, orig, bTip, 0.06f * (1.0f - t * 0.6f), 0.015f, 4, barkA, barkB, 0.2f, 0.5f, 0.0f);
                }
                float tv = 0.88f + Tree_NextRand(&rng) * 0.22f;
                Color tint = {(unsigned char)(240.0f * tv), (unsigned char)(255.0f * tv), (unsigned char)(245.0f * tv), 255};
                Tree_AddVolumetricConiferBough(&b, (Vector3){orig.x, orig.y + 0.06f, orig.z}, yaw, pitch,
                                               len, bWid * (0.88f + Tree_NextRand(&rng) * 0.22f), droop,
                                               0.42f + t * 0.30f, 0.90f + t * 0.10f, tint, 0.35f + t * 0.60f, quad, lod, &rng);
            }
        }
        // Sharp natural conifer apex (6 upright terminal needle shoots — NO ball on top!)
        Vector3 apex = {sinf(2.3f) * 0.09f, H - 0.95f, cosf(1.9f) * 0.07f - 0.07f};
        for (int i = 0; i < 6; i++)
        {
            float yaw = (float)i * (PI / 3.0f) + 0.3f;
            Tree_AddArchedFrond(&b, (Vector3){apex.x, apex.y + (float)(i & 1) * 0.24f, apex.z},
                                yaw, -1.12f, 0.0f, 0.82f, 0.40f, 0.05f, 0.08f, 0.72f, 1.0f, WHITE, 0.95f, quad, 1);
        }
    }
    else if (archetype == MAP_TREE_ANCIENT_PINE || archetype == MAP_TREE_GNARLED_MOUNTAIN_PINE || archetype == MAP_TREE_COASTAL_PINE)
    {
        // Ported from WebGL2 Showcase Species 2: S-curved Huangshan Pine + tiered horizontal pine frond clouds
        bool isSnow = (archetype == MAP_TREE_GNARLED_MOUNTAIN_PINE);
        bool isCoastal = (archetype == MAP_TREE_COASTAL_PINE);
        Color barkA = isSnow ? (Color){68, 56, 48, 255} : (Color){82, 56, 40, 255};
        Color barkB = isSnow ? (Color){98, 84, 74, 255} : (Color){112, 78, 54, 255};

        float lean = isCoastal ? 1.45f : 0.95f;
        Vector3 pts[6] = {
            {-0.35f, -0.25f, 0.08f},
            {-0.10f,  1.25f, 0.20f},
            { 0.45f * lean, 2.45f, 0.14f},
            { 1.05f * lean, 3.65f, -0.08f},
            { 0.78f * lean, 4.85f, -0.14f},
            { 1.38f * lean, 6.05f, 0.10f}
        };
        float rads[6] = {0.40f, 0.30f, 0.23f, 0.17f, 0.12f, 0.06f};
        for (int s = 0; s < 5; s++)
            Tree_AddTube(&b, pts[s], pts[s + 1], rads[s], rads[s + 1], trunkSides, barkA, barkB, (float)s * 0.10f, (float)(s + 1) * 0.12f, (s == 0) ? 0.55f : 0.0f);

        int tiers = (lod == 2) ? 4 : 6;
        for (int ti = 0; ti < tiers; ti++)
        {
            float frac = (float)ti / (float)(tiers - 1);
            int seg = (ti < 2) ? 2 : ((ti < 4) ? 3 : 5);
            Vector3 base = pts[seg];
            float ang = (float)ti * 2.25f + (isCoastal ? 0.15f : 0.55f);
            float reach = 2.55f - frac * 1.05f;
            Vector3 bEnd = {base.x + cosf(ang) * reach, base.y + 0.35f, base.z + sinf(ang) * reach};
            Tree_AddTube(&b, base, bEnd, 0.11f, 0.04f, boughSides, barkA, barkB, 0.28f, 0.55f, 0.0f);

            int fronds = (lod == 0) ? 9 : (lod == 1 ? 5 : 3);
            for (int f = 0; f < fronds; f++)
            {
                float fyaw = (float)f * (2.0f * PI / (float)fronds) + Tree_NextRand(&rng) * 0.35f;
                float flen = (1.45f - frac * 0.30f) * (0.85f + Tree_NextRand(&rng) * 0.28f);
                Tree_AddArchedFrond(&b, (Vector3){bEnd.x, bEnd.y + ((f % 3) - 1) * 0.16f, bEnd.z},
                                    fyaw, -0.08f + (float)(f % 3) * 0.10f, (Tree_NextRand(&rng) - 0.5f) * 0.22f,
                                    flen, 0.96f, 0.24f, 0.16f, 0.52f, 1.0f, WHITE, 0.65f + frac * 0.30f, 0, lod == 0 ? 2 : 1);
            }
        }
    }
    else if (archetype == MAP_TREE_BAMBOO)
    {
        // Multi-culm bamboo grove cluster (3 staggered culms + raised internode rings + Q3 lanceolate leaf plumes)
        Color culmDark = {46, 86, 38, 255};
        Color culmLight = {96, 146, 62, 255};
        Color nodeBand = {34, 62, 28, 255};
        Color leafTint = {168, 224, 118, 255};
        int culms = (lod == 2) ? 2 : 3;
        float ox[3] = {0.0f, 0.36f, -0.30f}, oz[3] = {0.0f, 0.24f, 0.34f};
        float hs[3] = {7.4f, 6.2f, 5.3f};
        float lx[3] = {0.55f, -0.48f, 0.32f}, lz[3] = {0.22f, 0.35f, -0.45f};
        for (int c = 0; c < culms; c++)
        {
            int segs = (lod == 0) ? 9 : 5;
            Vector3 prev = {ox[c], -0.15f, oz[c]};
            float prevR = 0.092f - (float)c * 0.012f;
            for (int s = 1; s <= segs; s++)
            {
                float t = (float)s / (float)segs;
                Vector3 cur = {ox[c] + lx[c] * t * t, hs[c] * t, oz[c] + lz[c] * t * t};
                float curR = prevR * 0.91f;
                Tree_AddTube(&b, prev, cur, prevR, curR, trunkSides, culmDark, culmLight, t * 0.65f, t * 0.78f, 0.0f);
                if (lod <= 1 && s < segs)
                    Tree_AddTube(&b, Vector3Lerp(prev, cur, 0.95f), cur, curR * 1.24f, curR * 1.18f, trunkSides, nodeBand, nodeBand, t * 0.7f, t * 0.75f, 0.0f);
                if (t >= 0.45f)
                {
                    int sprays = (lod == 0) ? 4 : 2;
                    for (int k = 0; k < sprays; k++)
                    {
                        float yaw = (float)k * (2.0f * PI / (float)sprays) + (float)s * 1.1f;
                        Tree_AddArchedFrond(&b, cur, yaw, -0.18f, 0.15f, 1.15f, 0.78f, 0.32f, 0.12f, 0.62f, 1.0f, leafTint, 0.85f, 3, 1);
                    }
                }
                prev = cur;
                prevR = curR;
            }
        }
    }
    else if (archetype == MAP_TREE_WEEPING_WILLOW)
    {
        // Leaning split trunk + 6 overarching boughs + 36 draping Q3 willow frond curtains (Liễu Rũ)
        Color barkA = {64, 52, 40, 255};
        Color barkB = {86, 70, 52, 255};
        Color willowTint = {152, 214, 96, 255};
        Vector3 p0 = {0.0f, -0.25f, 0.0f}, p1 = {0.22f, 1.35f, 0.12f}, p2 = {0.58f, 2.85f, 0.24f};
        Tree_AddTube(&b, p0, p1, 0.44f, 0.32f, trunkSides, barkA, barkB, 0.0f, 0.12f, 0.40f);
        Tree_AddTube(&b, p1, p2, 0.32f, 0.22f, trunkSides, barkB, barkB, 0.12f, 0.28f, 0.0f);

        int boughs = (lod == 2) ? 4 : 6;
        for (int i = 0; i < boughs; i++)
        {
            float ang = (float)i * (2.0f * PI / (float)boughs) + 0.32f;
            float reach = 2.6f + Tree_NextRand(&rng) * 0.9f;
            Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
            Vector3 mid = {p2.x + dir.x * reach * 0.52f, p2.y + 1.45f, p2.z + dir.z * reach * 0.52f};
            Vector3 tip = {p2.x + dir.x * reach, p2.y + 0.95f, p2.z + dir.z * reach};
            Tree_AddTube(&b, p2, mid, 0.16f, 0.10f, boughSides, barkA, barkB, 0.28f, 0.48f, 0.0f);
            Tree_AddTube(&b, mid, tip, 0.10f, 0.05f, boughSides, barkB, barkB, 0.48f, 0.66f, 0.0f);

            int curtains = (lod == 0) ? 6 : (lod == 1 ? 3 : 2);
            for (int c = 0; c < curtains; c++)
            {
                float frac = 0.25f + 0.75f * ((float)c / (float)curtains);
                Vector3 anc = Vector3Lerp(mid, tip, frac);
                anc.y += 0.25f;
                // Pitch +1.35 rad points almost straight downward like weeping curtains
                Tree_AddArchedFrond(&b, anc, ang + (Tree_NextRand(&rng) - 0.5f) * 0.6f, 1.32f,
                                    (Tree_NextRand(&rng) - 0.5f) * 0.5f, 2.55f + Tree_NextRand(&rng) * 0.95f,
                                    0.95f, 0.15f, 0.12f, 0.65f, 1.0f, willowTint, 0.92f, 3, lod == 0 ? 2 : 1);
            }
        }
    }
    else if (archetype == MAP_TREE_FROZEN_DEADWOOD)
    {
        // Sculpted leafless frost/volcanic gnarled deadwood snag
        Color barkA = {56, 52, 50, 255};
        Color barkB = {92, 86, 82, 255};
        Vector3 p0 = {0.0f, -0.25f, 0.0f}, p1 = {0.28f, 1.65f, -0.15f}, p2 = {-0.12f, 3.45f, 0.22f}, p3 = {0.35f, 4.95f, -0.10f};
        Tree_AddTube(&b, p0, p1, 0.38f, 0.26f, trunkSides, barkA, barkB, 0.0f, 0.08f, 0.42f);
        Tree_AddTube(&b, p1, p2, 0.26f, 0.16f, trunkSides, barkA, barkB, 0.08f, 0.16f, 0.0f);
        Tree_AddTube(&b, p2, p3, 0.16f, 0.06f, trunkSides, barkB, barkB, 0.16f, 0.24f, 0.0f);
        int limbs = (lod == 2) ? 3 : 6;
        for (int i = 0; i < limbs; i++)
        {
            float ang = (float)i * 2.15f + 0.4f;
            Vector3 root = (i < 2) ? p1 : p2;
            Vector3 mid = {root.x + cosf(ang) * 1.15f, root.y + 0.85f, root.z + sinf(ang) * 1.15f};
            Vector3 tip = {mid.x + cosf(ang + 0.35f) * 0.95f, mid.y + 0.65f, mid.z + sinf(ang + 0.35f) * 0.95f};
            Tree_AddTube(&b, root, mid, 0.12f, 0.065f, boughSides, barkA, barkB, 0.10f, 0.18f, 0.0f);
            Tree_AddTube(&b, mid, tip, 0.065f, 0.022f, boughSides, barkB, barkB, 0.18f, 0.26f, 0.0f);
        }
    }
    else
    {
        // Broadleaf trees using Q2 rich volumetric leaf-cloud clusters + curved continuous leader trunk
        // & staggered multi-altitude forked limbs (FROSTED_BIRCH, MEADOW_BIRCH, MEADOW_OAK, PEACH_BLOSSOM, ARID_ACACIA)
        bool isPeach = (archetype == MAP_TREE_PEACH_BLOSSOM);
        bool isBirch = (archetype == MAP_TREE_MEADOW_BIRCH || archetype == MAP_TREE_FROSTED_BIRCH);
        bool isFrosted = (archetype == MAP_TREE_FROSTED_BIRCH);
        bool isAcacia = (archetype == MAP_TREE_ARID_ACACIA);

        Color barkA = isBirch ? (Color){206, 204, 198, 255} : (Color){74, 52, 38, 255};
        Color barkB = isBirch ? (Color){238, 236, 232, 255} : (Color){98, 70, 50, 255};
        Color foliageTint = isPeach ? (Color){248, 168, 194, 255}
                          : (isFrosted ? (Color){236, 178, 86, 255}
                          : (isAcacia ? (Color){158, 166, 84, 255}
                          : (isBirch ? (Color){132, 192, 82, 255} : (Color){92, 162, 68, 255})));

        // 1. Smooth curved continuous trunk & tapering central leader into upper crown (zero kinked sticks!)
        float totalH = isBirch ? 7.2f : (isAcacia ? 4.4f : 5.8f);
        float baseR  = isBirch ? 0.24f : (isAcacia ? 0.30f : 0.38f);
        int trunkSegs = (lod == 0) ? 7 : 4;
        Vector3 spinePts[8];
        float spineR[8];
        for (int s = 0; s <= trunkSegs; s++)
        {
            float t = (float)s / (float)trunkSegs;
            float swayX = sinf(t * 2.4f) * (isBirch ? 0.24f : 0.30f) + t * (isBirch ? 0.14f : 0.08f);
            float swayZ = cosf(t * 1.9f) * (isBirch ? 0.18f : 0.22f) - (isBirch ? 0.18f : 0.22f);
            spinePts[s] = (Vector3){swayX, -0.25f + t * (totalH + 0.15f), swayZ};
            spineR[s] = baseR * powf(1.0f - t * 0.88f, 0.92f);
        }
        for (int s = 0; s < trunkSegs; s++)
        {
            float t0 = (float)s / (float)trunkSegs;
            float t1 = (float)(s + 1) / (float)trunkSegs;
            Tree_AddTube(&b, spinePts[s], spinePts[s + 1], spineR[s], spineR[s + 1],
                         trunkSides, (s == 0) ? barkA : barkB, barkB,
                         t0 * 0.25f, t1 * 0.30f, (s == 0) ? (isBirch ? 0.34f : 0.48f) : 0.0f);
        }

        // 2. Staggered multi-altitude primary limbs + curved 2-segment arches + secondary forks
        int limbs = (lod == 2) ? 4 : (isBirch ? 8 : 7);
        for (int i = 0; i < limbs; i++)
        {
            float frac = (float)i / (float)(limbs > 1 ? limbs - 1 : 1);
            // Distribute limbs naturally from 36% up to 84% of trunk height (never all from one point!)
            float tTrunk = (isAcacia ? 0.48f : 0.36f) + frac * (isAcacia ? 0.38f : 0.48f);
            float fIdx = tTrunk * (float)trunkSegs;
            int idx0 = (int)fIdx;
            if (idx0 >= trunkSegs) idx0 = trunkSegs - 1;
            float subT = fIdx - (float)idx0;
            Vector3 orig = Vector3Lerp(spinePts[idx0], spinePts[idx0 + 1], subT);
            float rTrunk = spineR[idx0] + (spineR[idx0 + 1] - spineR[idx0]) * subT;

            // Golden-angle spiral azimuth for balanced 360-deg silhouette
            float ang = (float)i * 2.399963f + 0.35f + (Tree_NextRand(&rng) - 0.5f) * 0.22f;
            float crownProfile = sinf(0.28f + frac * 2.45f); // Widest mid-crown, gracefully tapering toward apex
            float reach = (isBirch ? 2.15f : (isAcacia ? 2.85f : 2.55f)) * (0.55f + 0.55f * crownProfile) * (0.88f + Tree_NextRand(&rng) * 0.24f);
            float lift  = (isBirch ? 1.55f : (isAcacia ? 0.95f : 1.45f)) * (0.65f + 0.45f * (1.0f - frac * 0.4f));

            Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
            Vector3 mid = {orig.x + dir.x * reach * 0.52f, orig.y + lift * 0.62f, orig.z + dir.z * reach * 0.52f};
            Vector3 tip = {orig.x + dir.x * reach, orig.y + lift * (isBirch ? 0.96f : 1.02f), orig.z + dir.z * reach};

            float rBase = rTrunk * 0.58f;
            float rMid  = rBase * 0.56f;
            float rTip  = rBase * 0.22f;
            Tree_AddTube(&b, orig, mid, rBase, rMid, boughSides, barkA, barkB, 0.22f + frac * 0.15f, 0.42f + frac * 0.15f, 0.0f);
            Tree_AddTube(&b, mid, tip, rMid, rTip, boughSides, barkB, barkB, 0.42f + frac * 0.15f, 0.60f + frac * 0.15f, 0.0f);

            float droop = isBirch ? 0.38f : 0.24f;
            float clRad = (isBirch ? 1.18f : 1.38f) * (0.78f + 0.32f * crownProfile);
            // Mid-bough secondary fork + volumetric foliage cluster
            if (lod <= 1)
            {
                float forkSide = (i & 1) ? 0.68f : -0.68f;
                float fAng = ang + forkSide + (Tree_NextRand(&rng) - 0.5f) * 0.20f;
                float fReach = reach * 0.54f;
                Vector3 forkTip = {mid.x + cosf(fAng) * fReach, mid.y + lift * 0.38f, mid.z + sinf(fAng) * fReach};
                Tree_AddTube(&b, mid, forkTip, rMid * 0.72f, rTip * 0.80f, 4, barkB, barkB, 0.45f, 0.66f, 0.0f);
                Tree_AddVolumetricBroadleafCluster(&b, forkTip, fAng, clRad * 0.88f, droop,
                                                   0.62f + frac * 0.18f, 1.0f, foliageTint, 0.78f, lod, &rng);
            }
            // Primary bough tip volumetric foliage cluster
            Tree_AddVolumetricBroadleafCluster(&b, tip, ang, clRad, droop,
                                               0.60f + frac * 0.20f, 1.0f, foliageTint, 0.84f, lod, &rng);
        }

        // 3. Soft layered upper leader crown (graceful ovoid apex instead of flat lid)
        Vector3 topAnchor = spinePts[trunkSegs];
        Tree_AddVolumetricBroadleafCluster(&b, (Vector3){topAnchor.x, topAnchor.y - 0.35f, topAnchor.z},
                                           0.4f, isBirch ? 1.35f : 1.55f, isBirch ? 0.34f : 0.22f,
                                           0.70f, 1.0f, foliageTint, 0.92f, lod, &rng);
        Tree_AddVolumetricBroadleafCluster(&b, (Vector3){topAnchor.x, topAnchor.y + 0.35f, topAnchor.z},
                                           1.8f, isBirch ? 1.05f : 1.25f, isBirch ? 0.28f : 0.20f,
                                           0.78f, 1.0f, foliageTint, 0.96f, lod, &rng);
    }

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
static int s_treeShadowLocInstTex = -1;
static int s_treeShadowLocAtlasTex = -1;
static int s_treeShadowLocBaseOffset = -1;

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
        }
        if (s_treeShadowShader.id != 0)
        {
            s_treeShadowLocInstTex = GetShaderLocation(s_treeShadowShader, "u_treeInstanceTex");
            s_treeShadowLocAtlasTex = GetShaderLocation(s_treeShadowShader, "u_treeFoliageAtlas");
            s_treeShadowLocBaseOffset = GetShaderLocation(s_treeShadowShader, "u_instanceBaseOffset");
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
        int texelsNeeded = typeCount * 2;
        int rows = (texelsNeeded + 511) / 512;
        if (rows < 1) rows = 1;
        size_t bufFloats = (size_t)512 * (size_t)rows * 4u;
        float *allPx = (float *)MemAlloc((unsigned int)(bufFloats * sizeof(float)));
        float *visPx = (float *)MemAlloc((unsigned int)(bufFloats * sizeof(float)));
        if (!allPx || !visPx)
        {
            if (allPx) MemFree(allPx);
            if (visPx) MemFree(visPx);
            continue;
        }
        memset(allPx, 0, bufFloats * sizeof(float));
        memset(visPx, 0, bufFloats * sizeof(float));

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
            allPx[b + 6] = p->phase;
            allPx[b + 7] = (float)packedRGB;
            k++;
        }
        memcpy(visPx, allPx, bufFloats * sizeof(float));

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
    float drawDistSq = sys->drawDistance * sys->drawDistance;
    float lod0Sq = lod0Dist * lod0Dist;
    float lod1Sq = lod1Dist * lod1Dist;

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
            int lod = (dSq <= lod0Sq) ? 0 : ((dSq <= lod1Sq) ? 1 : 2);
            counts[lod]++;
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
            int lod = (dSq <= lod0Sq) ? 0 : ((dSq <= lod1Sq) ? 1 : 2);
            int dstSlot = writeIdx[lod]++;
            memcpy(&sys->visibleTexels[a][dstSlot * 8], src, 8u * sizeof(float));
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
    if (s_treeLocDrawDistSq >= 0) SetShaderValue(sh, s_treeLocDrawDistSq, &distSq, SHADER_UNIFORM_FLOAT);
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
                float shadeT = (rn - 0.30f) / 0.75f;
                dens = fminf(dens, 0.45f + 0.48f * shadeT);
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

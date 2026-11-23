// Procedural 3D Xianxia Trees & Bamboo Groves System (GPU-Instanced)
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
        int newCap = (b->cap > 0) ? (b->cap * 2) : 2400;
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

static Mesh TreeBuilder_ToMesh(TreeMeshBuilder *b)
{
    Mesh m = {0};
    if (b->count < 3)
        return m;
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

static unsigned int Tree_NextRng(unsigned int *s)
{
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

static float Tree_Rand01(unsigned int *s)
{
    return (float)((Tree_NextRng(s) >> 8) & 0xFFFFu) / 65535.0f;
}

static Color Tree_LerpColor(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
        (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * t),
    };
}

// Smooth organic spline-swept woody trunk/branch with root flare, bark fluting, and parallel-transport frames
static void Tree_AddBranchTube(TreeMeshBuilder *b, const Vector3 *pts, int ptCount,
                               float rStart, float rEnd, int radialSegs,
                               float rootFlare, float fluteAmp, float fluteFreq,
                               Color colStart, Color colEnd, bool birchBaseDarken)
{
    if (ptCount < 2 || radialSegs < 3)
        return;

    Vector3 prevT = {0.0f, 1.0f, 0.0f};
    Vector3 right = {1.0f, 0.0f, 0.0f};
    Vector3 fwd = {0.0f, 0.0f, 1.0f};

    Vector3 prevRing[13];
    Vector3 prevNorm[13];
    Color prevCol[13];
    int segs = (radialSegs > 12) ? 12 : radialSegs;

    for (int i = 0; i < ptCount; i++)
    {
        float t = (float)i / (float)(ptCount - 1);
        Vector3 p = pts[i];

        Vector3 tang;
        if (i == 0)
            tang = Vector3Subtract(pts[1], pts[0]);
        else if (i == ptCount - 1)
            tang = Vector3Subtract(pts[ptCount - 1], pts[ptCount - 2]);
        else
            tang = Vector3Subtract(pts[i + 1], pts[i - 1]);
        float tLenSq = Vector3LengthSqr(tang);
        tang = (tLenSq > 1e-6f) ? Vector3Scale(tang, 1.0f / sqrtf(tLenSq)) : prevT;

        if (i == 0)
        {
            Vector3 ref = (fabsf(tang.y) < 0.92f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
            right = Vector3Normalize(Vector3CrossProduct(ref, tang));
            fwd = Vector3Normalize(Vector3CrossProduct(tang, right));
        }
        else
        {
            Vector3 axis = Vector3CrossProduct(prevT, tang);
            float sinA = Vector3Length(axis);
            if (sinA > 1e-5f)
            {
                axis = Vector3Scale(axis, 1.0f / sinA);
                float ang = asinf(fminf(1.0f, fmaxf(-1.0f, sinA)));
                right = Vector3Normalize(Vector3RotateByAxisAngle(right, axis, ang));
            }
            fwd = Vector3Normalize(Vector3CrossProduct(tang, right));
            right = Vector3Normalize(Vector3CrossProduct(fwd, tang));
        }
        prevT = tang;

        float flare = (rootFlare > 0.0f && t < 0.26f) ? rootFlare * powf(fmaxf(0.0f, 1.0f - t * 3.8f), 2.2f) : 0.0f;
        float rad = (rStart + (rEnd - rStart) * powf(t, 0.86f)) * (1.0f + flare);
        Color baseC = Tree_LerpColor(colStart, colEnd, t);

        Vector3 curRing[13];
        Vector3 curNorm[13];
        Color curCol[13];
        for (int j = 0; j <= segs; j++)
        {
            float u = (float)j / (float)segs;
            float th = u * (2.0f * PI);
            float flute = (fluteAmp > 0.0f)
                ? (1.0f + sinf(th * fluteFreq + t * 2.2f) * fluteAmp * (1.0f - t * 0.72f))
                : 1.0f;
            float r = rad * flute;
            Vector3 n = Vector3Add(Vector3Scale(right, cosf(th)), Vector3Scale(fwd, sinf(th)));
            curNorm[j] = Vector3Normalize(n);
            curRing[j] = Vector3Add(p, Vector3Scale(curNorm[j], r));

            Color vc = baseC;
            if (flute < 1.0f)
            {
                vc.r = (unsigned char)((float)vc.r * 0.84f);
                vc.g = (unsigned char)((float)vc.g * 0.84f);
                vc.b = (unsigned char)((float)vc.b * 0.84f);
            }
            if (birchBaseDarken && curRing[j].y < 1.35f)
            {
                float dm = 1.0f - fminf(1.0f, fmaxf(0.0f, (curRing[j].y - 0.1f) / 1.25f));
                vc = Tree_LerpColor(vc, (Color){56, 52, 50, vc.a}, dm * 0.82f);
            }
            curCol[j] = vc;
        }

        if (i > 0)
        {
            Vector2 uv0 = {0.0f, (float)(i - 1) / (float)(ptCount - 1)};
            Vector2 uv1 = {0.0f, t};
            for (int j = 0; j < segs; j++)
            {
                TreeBuilder_PushTri(b, prevRing[j], prevNorm[j], uv0, prevCol[j],
                                    curRing[j], curNorm[j], uv1, curCol[j],
                                    curRing[j + 1], curNorm[j + 1], uv1, curCol[j + 1]);
                TreeBuilder_PushTri(b, prevRing[j], prevNorm[j], uv0, prevCol[j],
                                    curRing[j + 1], curNorm[j + 1], uv1, curCol[j + 1],
                                    prevRing[j + 1], prevNorm[j + 1], uv0, prevCol[j + 1]);
            }
        }
        memcpy(prevRing, curRing, sizeof(curRing));
        memcpy(prevNorm, curNorm, sizeof(curNorm));
        memcpy(prevCol, curCol, sizeof(curCol));
    }
}

// 3D V-camber pointed botanical leaf/needle blade (never a flat rectangular sheet)
static void Tree_AddPointedLeaf(TreeMeshBuilder *b, Vector3 root, Vector3 dir, Vector3 upAxis,
                                float length, float width, float droop,
                                Color baseCol, Color tipCol)
{
    float lenSq = Vector3LengthSqr(dir);
    if (lenSq < 1e-6f)
        return;
    Vector3 fwd = Vector3Scale(dir, 1.0f / sqrtf(lenSq));
    Vector3 sideRaw = Vector3CrossProduct(upAxis, fwd);
    if (Vector3LengthSqr(sideRaw) < 1e-5f)
        sideRaw = Vector3CrossProduct((Vector3){1.0f, 0.0f, 0.0f}, fwd);
    Vector3 side = Vector3Normalize(sideRaw);
    Vector3 localUp = Vector3Normalize(Vector3CrossProduct(fwd, side));

    Vector3 mid = Vector3Add(root, Vector3Add(Vector3Scale(fwd, length * 0.48f), Vector3Scale(localUp, length * 0.10f)));
    Vector3 tip = Vector3Add(root, Vector3Add(Vector3Scale(fwd, length), Vector3Scale(localUp, length * 0.14f - droop)));
    Vector3 p0L = Vector3Subtract(root, Vector3Scale(side, width * 0.24f));
    Vector3 p0R = Vector3Add(root, Vector3Scale(side, width * 0.24f));
    Vector3 p1L = Vector3Add(Vector3Subtract(mid, Vector3Scale(side, width * 0.62f)), Vector3Scale(localUp, width * 0.12f));
    Vector3 p1R = Vector3Add(Vector3Add(mid, Vector3Scale(side, width * 0.62f)), Vector3Scale(localUp, width * 0.12f));

    Vector3 nBase = Vector3Normalize(Vector3Add(localUp, Vector3Scale(fwd, 0.20f)));
    Vector3 nMidL = Vector3Normalize(Vector3Subtract(localUp, Vector3Scale(side, 0.35f)));
    Vector3 nMidR = Vector3Normalize(Vector3Add(localUp, Vector3Scale(side, 0.35f)));
    Vector3 nTip = Vector3Normalize(Vector3Add(localUp, Vector3Scale(fwd, 0.40f)));
    Vector2 uv = {0.5f, 1.0f};

    TreeBuilder_PushTri(b, p0L, nBase, uv, baseCol, p1L, nMidL, uv, baseCol, mid, nBase, uv, baseCol);
    TreeBuilder_PushTri(b, p0L, nBase, uv, baseCol, mid, nBase, uv, baseCol, p0R, nBase, uv, baseCol);
    TreeBuilder_PushTri(b, p0R, nBase, uv, baseCol, mid, nBase, uv, baseCol, p1R, nMidR, uv, baseCol);
    TreeBuilder_PushTri(b, p1L, nMidL, uv, baseCol, tip, nTip, uv, tipCol, mid, nBase, uv, baseCol);
    TreeBuilder_PushTri(b, mid, nBase, uv, baseCol, tip, nTip, uv, tipCol, p1R, nMidR, uv, baseCol);
}

// Dense 3D cluster of small splayed leaves around a twig node
static void Tree_AddLeafRosette(TreeMeshBuilder *b, Vector3 center, float baseYaw, float leafLen, float leafWid,
                                int count, Color coreCol, Color tipCol, unsigned int *rng)
{
    for (int i = 0; i < count; i++)
    {
        float yaw = baseYaw + ((float)i * 2.399963f) + (Tree_Rand01(rng) - 0.5f) * 0.35f;
        float pitch = -0.35f + 0.85f * Tree_Rand01(rng);
        float sa = sinf(yaw), ca = cosf(yaw);
        float sp = sinf(pitch), cp = cosf(pitch);
        Vector3 dir = {ca * cp, sp, sa * cp};
        Vector3 upRef = {-sa * 0.25f, 0.96f, ca * 0.25f};
        float sc = 0.82f + 0.36f * Tree_Rand01(rng);
        Tree_AddPointedLeaf(b, center, dir, upRef, leafLen * sc, leafWid * sc, leafLen * 0.14f, coreCol, tipCol);
    }
}

// Narrow feathery conifer shoot with crossed needle fans (zero giant flat sheets)
static void Tree_AddPineShoot(TreeMeshBuilder *b, Vector3 origin, float yaw, float pitch,
                              float length, float width, float droop,
                              Color coreCol, Color tipCol, unsigned int *rng)
{
    float sy = sinf(yaw), cy = cosf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    Vector3 fwd = {sy * cp, -sp, cy * cp};
    for (int step = 0; step < 3; step++)
    {
        float u = (float)step * 0.30f;
        Vector3 node = Vector3Add(origin, Vector3Scale(fwd, length * u));
        node.y -= droop * u * u;
        float segLen = length * (0.58f - u * 0.30f);
        float segWid = width * (1.0f - u * 0.35f);
        for (int s = -1; s <= 1; s++)
        {
            float syaw = yaw + (float)s * 0.42f + (Tree_Rand01(rng) - 0.5f) * 0.15f;
            float ssy = sinf(syaw), ccy = cosf(syaw);
            Vector3 sdir = {ssy * cp, -sp + 0.12f * (float)(step == 2), ccy * cp};
            Tree_AddPointedLeaf(b, node, sdir, (Vector3){0, 1, 0}, segLen, segWid, droop * 0.35f, coreCol, tipCol);
            // Crossed vertical-tilted needle plane for 3D volume
            Tree_AddPointedLeaf(b, node, sdir, (Vector3){ccy * 0.65f, 0.72f, -ssy * 0.65f},
                                segLen * 0.92f, segWid * 0.80f, droop * 0.30f, coreCol, tipCol);
        }
    }
}

// Adds a layered botanical foliage crown dome with spherical normals for soft Xianxia shading
static void Tree_AddCanopyCluster(TreeMeshBuilder *b, Vector3 center, float rx, float ry, float rz,
                                  Color baseCol, Color topCol)
{
    const int rings = 5;
    const int segs = 9;
    Vector3 prevRing[10];
    Vector3 prevNorm[10];
    for (int r = 0; r < rings; r++)
    {
        float phi0 = ((float)r / (float)rings) * PI - PI * 0.5f;
        float phi1 = ((float)(r + 1) / (float)rings) * PI - PI * 0.5f;
        float y0 = sinf(phi0), cr0 = cosf(phi0);
        float y1 = sinf(phi1), cr1 = cosf(phi1);
        float t0f = (float)r / (float)rings;
        float t1f = (float)(r + 1) / (float)rings;
        Color c0 = {
            (unsigned char)(baseCol.r + (topCol.r - baseCol.r) * t0f),
            (unsigned char)(baseCol.g + (topCol.g - baseCol.g) * t0f),
            (unsigned char)(baseCol.b + (topCol.b - baseCol.b) * t0f),
            (unsigned char)(baseCol.a + (topCol.a - baseCol.a) * t0f),
        };
        Color c1 = {
            (unsigned char)(baseCol.r + (topCol.r - baseCol.r) * t1f),
            (unsigned char)(baseCol.g + (topCol.g - baseCol.g) * t1f),
            (unsigned char)(baseCol.b + (topCol.b - baseCol.b) * t1f),
            (unsigned char)(baseCol.a + (topCol.a - baseCol.a) * t1f),
        };
        Vector2 uvFoliage = {0.5f, 1.0f};

        for (int s = 0; s < segs; s++)
        {
            float t0 = ((float)s / (float)segs) * 2.0f * PI;
            float t1 = ((float)(s + 1) / (float)segs) * 2.0f * PI;

            Vector3 n00 = Vector3Normalize((Vector3){cr0 * cosf(t0), y0 * 0.85f + 0.15f, cr0 * sinf(t0)});
            Vector3 n01 = Vector3Normalize((Vector3){cr0 * cosf(t1), y0 * 0.85f + 0.15f, cr0 * sinf(t1)});
            Vector3 n10 = Vector3Normalize((Vector3){cr1 * cosf(t0), y1 * 0.85f + 0.15f, cr1 * sinf(t0)});
            Vector3 n11 = Vector3Normalize((Vector3){cr1 * cosf(t1), y1 * 0.85f + 0.15f, cr1 * sinf(t1)});

            Vector3 p00 = {center.x + cr0 * cosf(t0) * rx, center.y + y0 * ry, center.z + cr0 * sinf(t0) * rz};
            Vector3 p01 = {center.x + cr0 * cosf(t1) * rx, center.y + y0 * ry, center.z + cr0 * sinf(t1) * rz};
            Vector3 p10 = {center.x + cr1 * cosf(t0) * rx, center.y + y1 * ry, center.z + cr1 * sinf(t0) * rz};
            Vector3 p11 = {center.x + cr1 * cosf(t1) * rx, center.y + y1 * ry, center.z + cr1 * sinf(t1) * rz};

            if (r == 0)
            {
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p10, n10, uvFoliage, c1, p11, n11, uvFoliage, c1);
            }
            else if (r == rings - 1)
            {
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p10, n10, uvFoliage, c1, p01, n01, uvFoliage, c0);
            }
            else
            {
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p10, n10, uvFoliage, c1, p11, n11, uvFoliage, c1);
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p11, n11, uvFoliage, c1, p01, n01, uvFoliage, c0);
            }
        }
    }
    (void)prevRing;
    (void)prevNorm;
}

static Mesh Tree_BuildArchetypeMesh(MapTreeArchetype arch)
{
    TreeMeshBuilder b = {0};
    if (!TreeBuilder_Init(&b, 4800))
    {
        TreeBuilder_Free(&b);
        return (Mesh){0};
    }
    unsigned int rng = 0x71E300u + (unsigned int)arch * 7919u;

    if (arch == MAP_TREE_ANCIENT_PINE)
    {
        Color bark0 = {88, 68, 52, 0}, bark1 = {124, 98, 76, 95};
        Vector3 trunk[6] = {{0,-0.1f,0}, {0.14f,0.9f,0.05f}, {-0.08f,2.0f,0.18f}, {0.10f,3.1f,0.12f}, {0.22f,4.1f,0.04f}, {0.16f,4.8f,0.08f}};
        Tree_AddBranchTube(&b, trunk, 6, 0.25f, 0.05f, 7, 0.52f, 0.10f, 5.0f, bark0, bark1, false);

        Vector3 boughA[3] = {trunk[2], {0.65f, 2.25f, -0.20f}, {1.25f, 2.40f, -0.35f}};
        Vector3 boughB[3] = {trunk[3], {-0.60f, 3.35f, 0.35f}, {-1.15f, 3.60f, 0.55f}};
        Tree_AddBranchTube(&b, boughA, 3, 0.10f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
        Tree_AddBranchTube(&b, boughB, 3, 0.09f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark1, bark1, false);

        Color pineCore = {28, 64, 34, 210}, pineTip = {68, 128, 56, 245};
        for (int k = 0; k < 7; k++)
            Tree_AddPineShoot(&b, boughA[2], (float)k * (2.0f * PI / 7.0f), -0.08f, 0.95f, 0.16f, 0.14f, pineCore, pineTip, &rng);
        for (int k = 0; k < 7; k++)
            Tree_AddPineShoot(&b, boughB[2], (float)k * (2.0f * PI / 7.0f) + 0.3f, -0.08f, 0.90f, 0.15f, 0.14f, pineCore, pineTip, &rng);
        for (int k = 0; k < 8; k++)
            Tree_AddPineShoot(&b, trunk[5], (float)k * (2.0f * PI / 8.0f), -0.14f, 1.15f, 0.18f, 0.16f, pineCore, pineTip, &rng);
    }
    else if (arch == MAP_TREE_BAMBOO)
    {
        Color culm0 = {78, 128, 54, 0}, culm1 = {126, 182, 82, 210};
        Vector3 culm[5] = {{0,-0.08f,0}, {0.06f,1.4f,0.03f}, {0.18f,2.8f,0.10f}, {0.32f,4.2f,0.18f}, {0.48f,5.4f,0.26f}};
        Tree_AddBranchTube(&b, culm, 5, 0.062f, 0.020f, 5, 0.18f, 0.0f, 0.0f, culm0, culm1, false);
        Color leaf0 = {78, 134, 56, 245}, leaf1 = {144, 196, 92, 255};
        for (int tier = 2; tier < 5; tier++)
            Tree_AddLeafRosette(&b, culm[tier], (float)tier * 1.1f, 0.44f, 0.065f, 9, leaf0, leaf1, &rng);
    }
    else if (arch == MAP_TREE_PEACH_BLOSSOM)
    {
        Color bark0 = {84, 62, 52, 0}, bark1 = {118, 88, 74, 110};
        Vector3 trunk[4] = {{0,-0.08f,0}, {0.08f,0.75f,-0.02f}, {0.18f,1.45f,-0.06f}, {0.14f,2.10f,0.04f}};
        Tree_AddBranchTube(&b, trunk, 4, 0.19f, 0.09f, 6, 0.45f, 0.08f, 4.0f, bark0, bark1, false);

        Vector3 limbs[3][3] = {
            {trunk[2], {-0.48f, 2.05f, 0.22f}, {-0.98f, 2.58f, 0.42f}},
            {trunk[2], {0.55f, 2.15f, -0.20f}, {1.05f, 2.75f, -0.38f}},
            {trunk[3], {0.05f, 2.68f, 0.12f}, {0.12f, 3.28f, 0.18f}},
        };
        Color petalDeep = {224, 116, 152, 230}, petalLight = {255, 212, 228, 255};
        for (int l = 0; l < 3; l++)
        {
            Tree_AddBranchTube(&b, limbs[l], 3, 0.09f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
            for (int tw = 0; tw < 3; tw++)
            {
                float az = (float)tw * 2.094f + (float)l;
                Vector3 twig[2] = {limbs[l][2], {limbs[l][2].x + cosf(az)*0.48f, limbs[l][2].y + 0.25f, limbs[l][2].z + sinf(az)*0.48f}};
                Tree_AddBranchTube(&b, twig, 2, 0.03f, 0.01f, 4, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
                Tree_AddLeafRosette(&b, twig[1], az, 0.34f, 0.16f, 8, petalDeep, petalLight, &rng);
            }
            Tree_AddLeafRosette(&b, limbs[l][2], (float)l, 0.36f, 0.18f, 8, petalDeep, petalLight, &rng);
        }
    }
    else if (arch == MAP_TREE_WEEPING_WILLOW)
    {
        Color bark0 = {88, 70, 52, 0}, bark1 = {120, 96, 74, 95};
        Vector3 trunk[4] = {{0,-0.08f,0}, {0.12f,1.05f,0.06f}, {0.22f,2.15f,0.14f}, {0.32f,3.15f,0.22f}};
        Tree_AddBranchTube(&b, trunk, 4, 0.24f, 0.08f, 6, 0.55f, 0.10f, 5.0f, bark0, bark1, false);
        Color w0 = {64, 118, 52, 245}, w1 = {132, 182, 82, 255};
        for (int k = 0; k < 24; k++)
        {
            float yaw = (float)k * (2.0f * PI / 24.0f);
            float rad = 0.55f + 0.55f * (float)(k & 1);
            Vector3 anchor = {trunk[3].x + cosf(yaw) * rad, trunk[3].y + 0.15f - 0.25f * (float)(k & 1), trunk[3].z + sinf(yaw) * rad};
            Vector3 dir = {cosf(yaw) * 0.22f, -0.95f, sinf(yaw) * 0.22f};
            Tree_AddPointedLeaf(&b, anchor, dir, (Vector3){cosf(yaw), 0.2f, sinf(yaw)}, 1.45f, 0.11f, 0.18f, w0, w1);
        }
        Tree_AddLeafRosette(&b, trunk[3], 0.0f, 0.58f, 0.14f, 12, w0, w1, &rng);
    }
    else if (arch == MAP_TREE_MEADOW_OAK)
    {
        // Sồi Cổ Thụ Thảo Nguyên: warm flared trunk + forked boughs + dense 3D leaf rosettes (no giant flat sheets!)
        Color bark0 = {92, 72, 54, 0}, bark1 = {128, 102, 78, 95};
        Vector3 trunk[5] = {{0,-0.1f,0}, {0.08f,0.85f,0.04f}, {-0.06f,1.80f,0.08f}, {0.06f,2.65f,0.04f}, {0.02f,3.25f,0.0f}};
        Tree_AddBranchTube(&b, trunk, 5, 0.28f, 0.10f, 7, 0.62f, 0.14f, 5.0f, bark0, bark1, false);

        Color oakCore = {42, 92, 38, 200}, oakSun = {112, 182, 72, 255};
        for (int bIdx = 0; bIdx < 5; bIdx++)
        {
            float az = (float)bIdx * (2.0f * PI / 5.0f) + 0.2f;
            float sa = sinf(az), ca = cosf(az);
            Vector3 limb[3] = {trunk[3], {ca * 0.78f, 3.15f, sa * 0.78f}, {ca * 1.45f, 3.65f + 0.22f * (float)(bIdx & 1), sa * 1.45f}};
            Tree_AddBranchTube(&b, limb, 3, 0.10f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
            for (int tw = 0; tw < 3; tw++)
            {
                float taz = az + (float)(tw - 1) * 0.65f;
                Vector3 twig[2] = {limb[2], {limb[2].x + cosf(taz)*0.55f, limb[2].y + 0.28f, limb[2].z + sinf(taz)*0.55f}};
                Tree_AddBranchTube(&b, twig, 2, 0.032f, 0.012f, 4, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
                Tree_AddLeafRosette(&b, twig[1], taz, 0.38f, 0.18f, 8, oakCore, oakSun, &rng);
            }
            Tree_AddLeafRosette(&b, limb[2], az, 0.40f, 0.19f, 7, oakCore, oakSun, &rng);
        }
        Tree_AddLeafRosette(&b, trunk[4], 0.5f, 0.42f, 0.20f, 10, oakCore, oakSun, &rng);
    }
    else if (arch == MAP_TREE_MEADOW_BIRCH)
    {
        // Bạch Dương Thảo Nguyên: twin white-barked birch trunks + airy spring-jade 3D leaf rosettes
        Color whiteBark = {222, 220, 214, 45}, twigBark = {174, 168, 158, 110};
        Color leaf0 = {78, 144, 58, 215}, leaf1 = {166, 212, 86, 255};
        for (int s = 0; s < 2; s++)
        {
            float sign = (s == 0) ? -1.0f : 1.0f;
            Vector3 trunk[5] = {
                {sign * 0.10f, -0.08f, 0.0f},
                {sign * 0.24f, 1.25f, sign * 0.08f},
                {sign * 0.38f, 2.55f, sign * 0.14f},
                {sign * 0.34f, 3.85f, sign * 0.06f},
                {sign * 0.42f, 5.10f, sign * 0.10f}
            };
            Tree_AddBranchTube(&b, trunk, 5, 0.13f, 0.025f, 6, 0.35f, 0.0f, 0.0f, whiteBark, twigBark, true);
            for (int t = 2; t < 5; t++)
            {
                for (int tw = 0; tw < 2; tw++)
                {
                    float az = (float)t * 1.4f + (float)tw * 2.8f;
                    Vector3 tip = {trunk[t].x + cosf(az)*0.52f, trunk[t].y + 0.25f, trunk[t].z + sinf(az)*0.52f};
                    Vector3 twig[2] = {trunk[t], tip};
                    Tree_AddBranchTube(&b, twig, 2, 0.024f, 0.008f, 4, 0.0f, 0.0f, 0.0f, twigBark, twigBark, false);
                    Tree_AddLeafRosette(&b, tip, az, 0.34f, 0.15f, 8, leaf0, leaf1, &rng);
                }
            }
        }
    }
    else if (arch == MAP_TREE_ALPINE_SNOW_SPRUCE)
    {
        // Thông Tuyết Cao Sơn: straight trunk + 4 root buttresses + 8 drooping snow-laden needle whorls
        Color bark0 = {84, 68, 56, 0}, bark1 = {116, 96, 82, 85};
        float hTotal = 7.4f;
        Vector3 trunk[7];
        for (int i = 0; i < 7; i++)
        {
            float t = (float)i / 6.0f;
            trunk[i] = (Vector3){sinf(t * 2.3f) * 0.08f, -0.15f + t * hTotal, cosf(t * 1.9f) * 0.06f - 0.05f};
        }
        Tree_AddBranchTube(&b, trunk, 7, 0.28f, 0.022f, 7, 0.58f, 0.12f, 5.0f, bark0, bark1, false);
        for (int r = 0; r < 4; r++)
        {
            float a = (float)r * (PI * 0.5f) + 0.35f;
            Vector3 rootPts[3] = {{cosf(a)*0.14f, 0.48f, sinf(a)*0.14f}, {cosf(a)*0.52f, 0.14f, sinf(a)*0.52f}, {cosf(a)*0.88f, -0.18f, sinf(a)*0.88f}};
            Tree_AddBranchTube(&b, rootPts, 3, 0.12f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark0, bark0, false);
        }

        Color spruceCore = {28, 58, 44, 200}, snowTip = {226, 240, 252, 255};
        int whorls = 8;
        for (int wi = 0; wi < whorls; wi++)
        {
            float t = (float)wi / (float)(whorls - 1);
            float y = 1.20f + t * 5.35f;
            Vector3 orig = {sinf((y / hTotal) * 2.3f) * 0.08f, y, cosf((y / hTotal) * 1.9f) * 0.06f - 0.05f};
            int bc = (wi < 3) ? 6 : 5;
            float bLen = 1.85f + (0.45f - 1.85f) * powf(t, 0.88f);
            float droop = 0.44f + (0.10f - 0.44f) * t;
            float pitch = -0.18f + 0.38f * powf(t, 1.35f);
            for (int bi = 0; bi < bc; bi++)
            {
                float yaw = (float)bi * (2.0f * PI / (float)bc) + (float)wi * 0.68f;
                Tree_AddPineShoot(&b, orig, yaw, pitch, bLen, 0.22f - 0.10f * t, droop, spruceCore, snowTip, &rng);
            }
        }
        Tree_AddLeafRosette(&b, (Vector3){trunk[6].x, hTotal - 0.75f, trunk[6].z}, 0.0f, 0.58f, 0.14f, 6, spruceCore, snowTip, &rng);
    }
    else if (arch == MAP_TREE_GNARLED_MOUNTAIN_PINE)
    {
        // Tùng Tuyết Cổ Thụ: sweeping S-curved Huangshan pine + horizontal snow-capped pine shoot pads
        Color bark0 = {84, 72, 66, 0}, bark1 = {122, 108, 98, 95};
        Vector3 trunk[6] = {{0,-0.1f,0}, {0.35f,0.95f,0.12f}, {-0.28f,2.10f,0.28f}, {0.42f,3.25f,-0.10f}, {0.15f,4.35f,0.12f}, {0.22f,5.15f,0.06f}};
        Tree_AddBranchTube(&b, trunk, 6, 0.26f, 0.05f, 7, 0.55f, 0.14f, 5.0f, bark0, bark1, false);
        Color pineCore = {26, 54, 44, 200}, snowTip = {228, 240, 252, 255};
        Vector3 pads[3] = {{-1.15f, 2.55f, 0.48f}, {1.28f, 3.65f, -0.32f}, {0.25f, 5.20f, 0.08f}};
        for (int p = 0; p < 3; p++)
        {
            Vector3 limb[3] = {trunk[p + 2], Vector3Scale(Vector3Add(trunk[p + 2], pads[p]), 0.5f), pads[p]};
            Tree_AddBranchTube(&b, limb, 3, 0.09f, 0.03f, 5, 0.0f, 0.0f, 0.0f, bark1, bark1, false);
            for (int k = 0; k < 7; k++)
                Tree_AddPineShoot(&b, pads[p], (float)k * (2.0f * PI / 7.0f), -0.06f, 0.95f, 0.16f, 0.12f, pineCore, snowTip, &rng);
        }
    }
    else if (arch == MAP_TREE_FROSTED_BIRCH)
    {
        // Bạch Dương Băng Giá: silver-white bark with dark fissured base + frosted golden-amber leaf rosettes
        Color whiteBark = {226, 230, 236, 45}, twigBark = {178, 184, 192, 110};
        Color frostLeaf0 = {182, 158, 110, 210}, frostLeaf1 = {236, 242, 250, 255};
        Vector3 trunk[6] = {{0,-0.1f,0}, {0.10f,1.2f,0.04f}, {-0.08f,2.4f,0.12f}, {0.14f,3.6f,0.06f}, {0.06f,4.8f,-0.04f}, {0.10f,5.8f,0.02f}};
        Tree_AddBranchTube(&b, trunk, 6, 0.16f, 0.025f, 6, 0.40f, 0.0f, 0.0f, whiteBark, twigBark, true);
        for (int t = 2; t < 6; t++)
        {
            for (int tw = 0; tw < 2; tw++)
            {
                float az = (float)t * 1.5f + (float)tw * 2.9f;
                Vector3 tip = {trunk[t].x + cosf(az)*0.55f, trunk[t].y + 0.28f, trunk[t].z + sinf(az)*0.55f};
                Vector3 twig[2] = {trunk[t], tip};
                Tree_AddBranchTube(&b, twig, 2, 0.022f, 0.008f, 4, 0.0f, 0.0f, 0.0f, twigBark, twigBark, false);
                Tree_AddLeafRosette(&b, tip, az, 0.34f, 0.15f, 8, frostLeaf0, frostLeaf1, &rng);
            }
        }
    }
    else if (arch == MAP_TREE_COLUMNAR_TAIGA_SPRUCE)
    {
        // Bách Tuyết Cột Đứng: tall slender boreal spire with dense short snow shoots
        Color bark0 = {80, 66, 54, 0}, bark1 = {110, 92, 78, 85};
        Vector3 trunk[6] = {{0,-0.15f,0}, {0.04f,1.6f,0.02f}, {-0.03f,3.2f,0.04f}, {0.04f,4.8f,-0.02f}, {0.02f,6.4f,0.02f}, {0.0f,8.2f,0.0f}};
        Tree_AddBranchTube(&b, trunk, 6, 0.20f, 0.018f, 6, 0.42f, 0.08f, 4.0f, bark0, bark1, false);
        Color coreC = {24, 52, 42, 200}, snowC = {228, 242, 254, 255};
        for (int wi = 0; wi < 11; wi++)
        {
            float t = (float)wi / 10.0f;
            Vector3 orig = {0.0f, 1.0f + t * 6.5f, 0.0f};
            float bLen = 1.05f + (0.30f - 1.05f) * powf(t, 0.9f);
            for (int bi = 0; bi < 5; bi++)
                Tree_AddPineShoot(&b, orig, (float)bi * 1.256f + (float)wi * 0.5f, 0.26f, bLen, 0.16f - 0.06f * t, 0.28f, coreC, snowC, &rng);
        }
    }
    else if (arch == MAP_TREE_OLD_GROWTH_FIR)
    {
        // Vân Sam Cổ Thụ Tuyết Phủ: heavy-snow broad old-growth alpine fir
        Color bark0 = {78, 64, 52, 0}, bark1 = {108, 92, 78, 85};
        Vector3 trunk[6] = {{0,-0.15f,0}, {0.08f,1.4f,0.04f}, {-0.05f,2.9f,0.08f}, {0.06f,4.5f,-0.04f}, {0.02f,6.0f,0.02f}, {0.0f,7.4f,0.0f}};
        Tree_AddBranchTube(&b, trunk, 6, 0.34f, 0.028f, 7, 0.68f, 0.14f, 5.0f, bark0, bark1, false);
        Color firCore = {22, 48, 38, 200}, snowCap = {234, 246, 255, 255};
        for (int wi = 0; wi < 7; wi++)
        {
            float t = (float)wi / 6.0f;
            Vector3 orig = {0.0f, 1.15f + t * 5.3f, 0.0f};
            float bLen = 2.15f + (0.50f - 2.15f) * powf(t, 0.85f);
            for (int bi = 0; bi < 6; bi++)
                Tree_AddPineShoot(&b, orig, (float)bi * 1.047f + (float)wi * 0.62f, -0.14f + 0.30f * t, bLen, 0.24f - 0.12f * t, 0.38f - 0.24f * t, firCore, snowCap, &rng);
        }
    }
    else // MAP_TREE_FROZEN_DEADWOOD
    {
        // Cổ Mộc Băng Giá: ancient gnarled leafless alpine deadwood with frost-rimmed limbs
        Color dead0 = {96, 98, 106, 0}, dead1 = {184, 198, 214, 75};
        Vector3 trunk[5] = {{0,-0.12f,0}, {0.25f,1.1f,0.14f}, {-0.22f,2.3f,0.28f}, {0.32f,3.4f,-0.12f}, {0.18f,4.4f,0.05f}};
        Tree_AddBranchTube(&b, trunk, 5, 0.28f, 0.04f, 7, 0.65f, 0.16f, 5.0f, dead0, dead1, false);
        Vector3 limbs[3][3] = {
            {trunk[2], {-0.85f, 2.85f, 0.55f}, {-1.35f, 3.35f, 0.72f}},
            {trunk[3], {0.95f, 3.95f, -0.45f}, {1.45f, 4.45f, -0.68f}},
            {trunk[4], {-0.35f, 4.85f, 0.25f}, {-0.65f, 5.25f, 0.42f}},
        };
        for (int l = 0; l < 3; l++)
            Tree_AddBranchTube(&b, limbs[l], 3, 0.10f, 0.015f, 5, 0.0f, 0.08f, 4.0f, dead0, dead1, false);
    }

    Mesh m = TreeBuilder_ToMesh(&b);
    TreeBuilder_Free(&b);
    return m;
}

static Shader s_treeShader = {0};
static bool s_treeShaderLoaded = false;
static int s_treeLocInstTex = -1;
static int s_treeLocDrawDistSq = -1;
static int s_treeLocLightVP = -1;
static int s_treeLocStaticLightVP = -1;
static int s_treeLocTime = -1;
static int s_treeLocWindStr = -1;
static int s_treeLocViewPos = -1;
static int s_treeLocLightDir = -1;
static int s_treeLocLightCol = -1;
static int s_treeLocAmbCol = -1;

static Shader Tree_GetShader(void)
{
    if (!s_treeShaderLoaded)
    {
        s_treeShaderLoaded = true;
        s_treeShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/tree_instanced.vs",
            "maps/toolkit/shaders/tree_foliage.fs");
        if (s_treeShader.id != 0 && s_treeShader.id != rlGetShaderIdDefault())
        {
            s_treeLocInstTex = GetShaderLocation(s_treeShader, "u_treeInstanceTex");
            s_treeLocDrawDistSq = GetShaderLocation(s_treeShader, "u_treeDrawDistSq");
            s_treeLocLightVP = GetShaderLocation(s_treeShader, "u_lightVP");
            s_treeLocStaticLightVP = GetShaderLocation(s_treeShader, "u_staticLightVP");
            s_treeLocTime = GetShaderLocation(s_treeShader, "u_time");
            s_treeLocWindStr = GetShaderLocation(s_treeShader, "u_windStrength");
            s_treeLocViewPos = GetShaderLocation(s_treeShader, "u_viewPos");
            s_treeLocLightDir = GetShaderLocation(s_treeShader, "u_lightDir");
            s_treeLocLightCol = GetShaderLocation(s_treeShader, "u_lightColor");
            s_treeLocAmbCol = GetShaderLocation(s_treeShader, "u_ambientColor");
            VFXLight_RegisterShader(s_treeShader);
            MapShadow_ConfigureShader(s_treeShader);
        }
    }
    return s_treeShader;
}

MapTreeSystem MapProp_CreateTreeSystem(const MapTreePlacement *placements, int count,
                                       const MapGroundSurface *ground, Vector3 worldCenter,
                                       float drawDistance)
{
    MapTreeSystem sys = {0};
    if (!placements || count <= 0)
        return sys;
    sys.drawDistance = (drawDistance > 10.0f) ? drawDistance : 120.0f;

    Shader sh = Tree_GetShader();
    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        int typeCount = 0;
        for (int i = 0; i < count; i++)
        {
            if (placements[i].archetype == (MapTreeArchetype)a)
                typeCount++;
        }
        if (typeCount <= 0)
            continue;

        Mesh m = Tree_BuildArchetypeMesh((MapTreeArchetype)a);
        if (!m.vertices)
            continue;
        sys.models[a] = LoadModelFromMesh(m);
        if (sh.id != 0 && sys.models[a].materialCount > 0)
            sys.models[a].materials[0].shader = sh;
        sys.modelLoaded[a] = true;

        int texels = typeCount * 2;
        int rows = (texels + 511) / 512;
        float *pixels = (float *)MemAlloc((unsigned int)(512 * rows * 4) * sizeof(float));
        if (pixels)
        {
            memset(pixels, 0, (size_t)(512 * rows * 4) * sizeof(float));
            int k = 0;
            for (int i = 0; i < count; i++)
            {
                if (placements[i].archetype != (MapTreeArchetype)a)
                    continue;
                MapTreePlacement p = placements[i];
                if (ground && ground->ready)
                {
                    Vector3 gp, gn;
                    if (MapProp_SampleGroundSurface(ground, worldCenter, p.position.x, p.position.z, &gp, &gn))
                        p.position.y = gp.y - 0.12f; // sink root slightly into slope
                }
                Color tint = (p.canopyTint.a > 0) ? p.canopyTint : WHITE;
                int packedRGB = ((int)tint.r << 16) | ((int)tint.g << 8) | (int)tint.b;
                int b = k * 8;
                pixels[b + 0] = p.position.x;
                pixels[b + 1] = p.position.y;
                pixels[b + 2] = p.position.z;
                pixels[b + 3] = p.rotationDeg * DEG2RAD;
                pixels[b + 4] = (p.scaleXZ > 0.1f) ? p.scaleXZ : 1.0f;
                pixels[b + 5] = (p.scaleY > 0.1f) ? p.scaleY : 1.0f;
                pixels[b + 6] = p.phase;
                pixels[b + 7] = (float)packedRGB;
                k++;
            }
            Image instImg = {
                .data = pixels,
                .width = 512,
                .height = rows,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
            };
            sys.instanceTextures[a] = LoadTextureFromImage(instImg);
            MemFree(pixels);
            if (sys.instanceTextures[a].id != 0)
            {
                SetTextureFilter(sys.instanceTextures[a], TEXTURE_FILTER_POINT);
                SetTextureWrap(sys.instanceTextures[a], TEXTURE_WRAP_CLAMP);
                sys.instanceCounts[a] = typeCount;
            }
        }
    }

    sys.ready = true;
    return sys;
}

void MapProp_DrawTreeSystem(const MapTreeSystem *sys, float time, Vector2 windDirection, float windStrength)
{
    (void)windDirection;
    if (!sys || !sys->ready)
        return;

    Shader sh = Tree_GetShader();
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return;

    BeginShaderMode(sh);
    Environment_BindCloudShadowShader(sh);
    rlDisableBackfaceCulling();
    if (s_treeLocTime >= 0) SetShaderValue(sh, s_treeLocTime, &time, SHADER_UNIFORM_FLOAT);
    if (s_treeLocWindStr >= 0) SetShaderValue(sh, s_treeLocWindStr, &windStrength, SHADER_UNIFORM_FLOAT);
    if (s_treeLocViewPos >= 0) SetShaderValue(sh, s_treeLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    float distSq = sys->drawDistance * sys->drawDistance;
    if (s_treeLocDrawDistSq >= 0) SetShaderValue(sh, s_treeLocDrawDistSq, &distSq, SHADER_UNIFORM_FLOAT);
    if (s_treeLocInstTex >= 0)
    {
        int unit0 = 0;
        SetShaderValue(sh, s_treeLocInstTex, &unit0, SHADER_UNIFORM_INT);
    }

    EnvFrameLighting lighting = Environment_GetFrameLighting();
    const float inv255 = 1.0f / 255.0f;
    const float sunScale = lighting.sunIntensity * inv255;
    float sunRGB[3] = {(float)lighting.sunColor.r * sunScale, (float)lighting.sunColor.g * sunScale, (float)lighting.sunColor.b * sunScale};
    float ambRGB[3] = {(float)lighting.skyAmbient.r * inv255, (float)lighting.skyAmbient.g * inv255, (float)lighting.skyAmbient.b * inv255};
    if (s_treeLocLightDir >= 0) SetShaderValue(sh, s_treeLocLightDir, &lighting.sunDirection, SHADER_UNIFORM_VEC3);
    if (s_treeLocLightCol >= 0) SetShaderValue(sh, s_treeLocLightCol, sunRGB, SHADER_UNIFORM_VEC3);
    if (s_treeLocAmbCol >= 0) SetShaderValue(sh, s_treeLocAmbCol, ambRGB, SHADER_UNIFORM_VEC3);

    Matrix lightVP = EnvShadow_GetLightVP();
    Matrix staticLightVP = EnvShadow_GetStaticLightVP();
    if (s_treeLocLightVP >= 0) SetShaderValueMatrix(sh, s_treeLocLightVP, lightVP);
    if (s_treeLocStaticLightVP >= 0) SetShaderValueMatrix(sh, s_treeLocStaticLightVP, staticLightVP);
    MapShadow_UpdateShader(sh);

    Matrix mvp = MatrixMultiply(MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview()), rlGetMatrixProjection());
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MVP], mvp);

    // Frustum-cull tree archetypes quickly against the camera frustum
    Nature_RefreshFrustumCache();

    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a] || sys->instanceCounts[a] <= 0 || sys->instanceTextures[a].id == 0)
            continue;
        const Mesh *mesh = &sys->models[a].meshes[0];
        rlActiveTextureSlot(0);
        rlEnableTexture(sys->instanceTextures[a].id);
        rlEnableVertexArray(mesh->vaoId);
        rlDrawVertexArrayInstanced(0, mesh->vertexCount, sys->instanceCounts[a]);
        rlDisableVertexArray();
    }
    rlEnableBackfaceCulling();
    EndShaderMode();
}

void MapProp_DrawTreeSystemShadowCasters(const MapTreeSystem *sys, Shader depthShader,
                                         float time, Vector2 windDirection, float windStrength)
{
    (void)time;
    (void)windDirection;
    (void)windStrength;
    if (!sys || !sys->ready || GfxQuality_Get() < GFX_HIGH)
        return;

    Shader sh = Tree_GetShader();
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return;

    // Submit instanced trees using depthShader for static/dynamic shadow capture
    BeginShaderMode(sh);
    Environment_BindCloudShadowShader(sh);
    rlDisableBackfaceCulling();
    if (s_treeLocTime >= 0) SetShaderValue(sh, s_treeLocTime, &time, SHADER_UNIFORM_FLOAT);
    if (s_treeLocWindStr >= 0) SetShaderValue(sh, s_treeLocWindStr, &windStrength, SHADER_UNIFORM_FLOAT);
    if (s_treeLocViewPos >= 0) SetShaderValue(sh, s_treeLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    float distSq = sys->drawDistance * sys->drawDistance;
    if (s_treeLocDrawDistSq >= 0) SetShaderValue(sh, s_treeLocDrawDistSq, &distSq, SHADER_UNIFORM_FLOAT);
    if (s_treeLocInstTex >= 0)
    {
        int unit0 = 0;
        SetShaderValue(sh, s_treeLocInstTex, &unit0, SHADER_UNIFORM_INT);
    }
    Matrix mvp = MatrixMultiply(rlGetMatrixTransform(), EnvShadow_GetLightVP());
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MVP], mvp);
    (void)depthShader;

    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a] || sys->instanceCounts[a] <= 0 || sys->instanceTextures[a].id == 0)
            continue;
        const Mesh *mesh = &sys->models[a].meshes[0];
        rlActiveTextureSlot(0);
        rlEnableTexture(sys->instanceTextures[a].id);
        rlEnableVertexArray(mesh->vaoId);
        rlDrawVertexArrayInstanced(0, mesh->vertexCount, sys->instanceCounts[a]);
        rlDisableVertexArray();
    }
    rlEnableBackfaceCulling();
    EndShaderMode();
}

void MapProp_UnloadTreeSystem(MapTreeSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    for (int a = 0; a < MAP_TREE_ARCHETYPE_COUNT; a++)
    {
        if (sys->modelLoaded[a])
        {
            UnloadModel(sys->models[a]);
            sys->modelLoaded[a] = false;
        }
        if (sys->instanceTextures[a].id != 0)
        {
            UnloadTexture(sys->instanceTextures[a]);
            sys->instanceTextures[a] = (Texture2D){0};
        }
        sys->instanceCounts[a] = 0;
    }
    sys->ready = false;
}

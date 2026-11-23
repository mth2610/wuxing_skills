// Procedural 3D Biome Small Shrubs & Xerophytic Scrub System (GPU-Instanced, 100% Opaque Early-Z)
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
} ShrubMeshBuilder;

static bool ShrubBuilder_Init(ShrubMeshBuilder *b, int maxVerts)
{
    b->pos = (Vector3 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector3));
    b->norm = (Vector3 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector3));
    b->uv = (Vector2 *)MemAlloc((unsigned int)maxVerts * (unsigned int)sizeof(Vector2));
    b->col = (unsigned char *)MemAlloc((unsigned int)maxVerts * 4u);
    b->count = 0;
    b->cap = maxVerts;
    return b->pos && b->norm && b->uv && b->col;
}

static void ShrubBuilder_Free(ShrubMeshBuilder *b)
{
    if (b->pos) MemFree(b->pos);
    if (b->norm) MemFree(b->norm);
    if (b->uv) MemFree(b->uv);
    if (b->col) MemFree(b->col);
    *b = (ShrubMeshBuilder){0};
}

static void ShrubBuilder_PushTri(ShrubMeshBuilder *b,
                                 Vector3 p0, Vector3 n0, Vector2 uv0, Color c0,
                                 Vector3 p1, Vector3 n1, Vector2 uv1, Color c1,
                                 Vector3 p2, Vector3 n2, Vector2 uv2, Color c2)
{
    if (b->count + 3 > b->cap)
    {
        int newCap = (b->cap > 0) ? (b->cap * 2) : 2048;
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

static Mesh ShrubBuilder_ToMesh(ShrubMeshBuilder *b)
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
        MemFree(m.vertices);
        MemFree(m.normals);
        MemFree(m.texcoords);
        MemFree(m.colors);
        return (Mesh){0};
    }
    memcpy(m.vertices, b->pos, (size_t)m.vertexCount * 3u * sizeof(float));
    memcpy(m.normals, b->norm, (size_t)m.vertexCount * 3u * sizeof(float));
    memcpy(m.texcoords, b->uv, (size_t)m.vertexCount * 2u * sizeof(float));
    memcpy(m.colors, b->col, (size_t)m.vertexCount * 4u);
    UploadMesh(&m, false);
    return m;
}

static void Shrub_AddBranchSeg(ShrubMeshBuilder *b, Vector3 a, Vector3 bPt,
                               float rA, float rB, int sides,
                               float hA, float hB, Color colA, Color colB)
{
    Vector3 diff = Vector3Subtract(bPt, a);
    float lenSq = Vector3DotProduct(diff, diff);
    if (lenSq < 1e-6f)
        return;
    Vector3 dir = Vector3Scale(diff, Nature_FastInvSqrt(lenSq));
    Vector3 ref = (fabsf(dir.y) < 0.94f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(ref, dir));
    Vector3 fwd = Vector3CrossProduct(dir, right);

    for (int i = 0; i < sides; i++)
    {
        float a0 = ((float)i / (float)sides) * 2.0f * PI;
        float a1 = ((float)(i + 1) / (float)sides) * 2.0f * PI;
        float s0, c0, s1, c1;
        Nature_FastSinCos(a0, &s0, &c0);
        Nature_FastSinCos(a1, &s1, &c1);
        Vector3 n0 = Vector3Add(Vector3Scale(right, c0), Vector3Scale(fwd, s0));
        Vector3 n1 = Vector3Add(Vector3Scale(right, c1), Vector3Scale(fwd, s1));

        Vector3 p00 = Vector3Add(a, Vector3Scale(n0, rA));
        Vector3 p01 = Vector3Add(a, Vector3Scale(n1, rA));
        Vector3 p10 = Vector3Add(bPt, Vector3Scale(n0, rB));
        Vector3 p11 = Vector3Add(bPt, Vector3Scale(n1, rB));

        Vector2 uv0 = {hA, 0.0f};
        Vector2 uv1 = {hB, 0.0f};
        ShrubBuilder_PushTri(b, p00, n0, uv0, colA, p10, n0, uv1, colB, p11, n1, uv1, colB);
        ShrubBuilder_PushTri(b, p00, n0, uv0, colA, p11, n1, uv1, colB, p01, n1, uv0, colA);
    }
}

static void Shrub_AddCurvedBranch(ShrubMeshBuilder *b, Vector3 p0, Vector3 p1, Vector3 p2,
                                  float r0, float r2, int segs, int sides,
                                  float h0, float h2, Color col0, Color col2)
{
    Vector3 prev = p0;
    float prevR = r0;
    float prevH = h0;
    Color prevC = col0;
    for (int s = 1; s <= segs; s++)
    {
        float t = (float)s / (float)segs;
        float u = 1.0f - t;
        Vector3 pt = {
            u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y,
            u * u * p0.z + 2.0f * u * t * p1.z + t * t * p2.z,
        };
        float r = r0 + (r2 - r0) * powf(t, 0.85f);
        float h = h0 + (h2 - h0) * t;
        Color c = {
            (unsigned char)((float)col0.r + ((float)col2.r - (float)col0.r) * t),
            (unsigned char)((float)col0.g + ((float)col2.g - (float)col0.g) * t),
            (unsigned char)((float)col0.b + ((float)col2.b - (float)col0.b) * t),
            (unsigned char)((float)col0.a + ((float)col2.a - (float)col0.a) * t),
        };
        Shrub_AddBranchSeg(b, prev, pt, prevR, r, sides, prevH, h, prevC, c);
        prev = pt;
        prevR = r;
        prevH = h;
        prevC = c;
    }
}

static void Shrub_AddBladeLeaf(ShrubMeshBuilder *b, Vector3 root, Vector3 dir, Vector3 upAxis,
                               float length, float width, float archDrop,
                               float h0, float h1, float partMask,
                               Color baseCol, Color tipCol)
{
    Vector3 fwd = Vector3Normalize(dir);
    Vector3 sideRaw = Vector3CrossProduct(upAxis, fwd);
    if (Vector3LengthSqr(sideRaw) < 1e-5f)
        sideRaw = Vector3CrossProduct((Vector3){1.0f, 0.0f, 0.0f}, fwd);
    Vector3 side = Vector3Normalize(sideRaw);
    Vector3 localUp = Vector3Normalize(Vector3CrossProduct(fwd, side));

    Vector3 mid = Vector3Add(root, Vector3Add(Vector3Scale(fwd, length * 0.50f), Vector3Scale(localUp, length * 0.16f)));
    Vector3 tip = Vector3Add(root, Vector3Add(Vector3Scale(fwd, length), Vector3Scale(localUp, length * 0.22f - archDrop)));

    // Slight V-fold camber along margins so 3D leaf blades catch directional sun & rim light
    Vector3 p0L = Vector3Subtract(root, Vector3Scale(side, width * 0.36f));
    Vector3 p0R = Vector3Add(root, Vector3Scale(side, width * 0.36f));
    Vector3 p1L = Vector3Add(Vector3Subtract(mid, Vector3Scale(side, width * 0.68f)), Vector3Scale(localUp, width * 0.14f));
    Vector3 p1R = Vector3Add(Vector3Add(mid, Vector3Scale(side, width * 0.68f)), Vector3Scale(localUp, width * 0.14f));

    Vector3 nBase = Vector3Normalize(Vector3Add(localUp, Vector3Scale(fwd, 0.20f)));
    Vector3 nMidL = Vector3Normalize(Vector3Subtract(localUp, Vector3Scale(side, 0.35f)));
    Vector3 nMidR = Vector3Normalize(Vector3Add(localUp, Vector3Scale(side, 0.35f)));
    Vector3 nTip = Vector3Normalize(Vector3Add(localUp, Vector3Scale(fwd, 0.45f)));
    Vector2 uvBase = {h0, partMask};
    Vector2 uvMid = {(h0 + h1) * 0.5f, partMask};
    Vector2 uvTip = {h1, partMask};

    ShrubBuilder_PushTri(b, p0L, nBase, uvBase, baseCol, p1L, nMidL, uvMid, baseCol, mid, nBase, uvMid, baseCol);
    ShrubBuilder_PushTri(b, p0L, nBase, uvBase, baseCol, mid, nBase, uvMid, baseCol, p0R, nBase, uvBase, baseCol);
    ShrubBuilder_PushTri(b, p0R, nBase, uvBase, baseCol, mid, nBase, uvMid, baseCol, p1R, nMidR, uvMid, baseCol);
    ShrubBuilder_PushTri(b, p1L, nMidL, uvMid, baseCol, tip, nTip, uvTip, tipCol, mid, nBase, uvMid, baseCol);
    ShrubBuilder_PushTri(b, mid, nBase, uvMid, baseCol, tip, nTip, uvTip, tipCol, p1R, nMidR, uvMid, baseCol);
}

static void Shrub_AddLeafSprig(ShrubMeshBuilder *b, Vector3 center, float yaw, float pitch,
                               float leafLen, float leafWid, int blades,
                               float h0, float h1, Color baseCol, Color tipCol)
{
    for (int i = 0; i < blades; i++)
    {
        float fan = (blades > 1) ? (((float)i / (float)(blades - 1)) - 0.5f) * 1.35f : 0.0f;
        float ang = yaw + fan;
        float el = pitch + 0.18f * ((i & 1) ? 1.0f : -0.45f);
        float sa, ca, se, ce;
        Nature_FastSinCos(ang, &sa, &ca);
        Nature_FastSinCos(el, &se, &ce);
        Vector3 dir = {ca * ce, se, sa * ce};
        Vector3 upRef = {-sa * 0.25f, 0.96f, ca * 0.25f};
        Shrub_AddBladeLeaf(b, center, dir, upRef, leafLen, leafWid, leafLen * 0.16f,
                           h0, h1, 1.0f, baseCol, tipCol);
    }
}

static void Shrub_AddTipFloret(ShrubMeshBuilder *b, Vector3 pos, float radius, float hFrac,
                               Color baseCol, Color tipCol, int petals)
{
    for (int i = 0; i < petals; i++)
    {
        float ang = (float)i * (2.0f * PI / (float)petals);
        float sa, ca;
        Nature_FastSinCos(ang, &sa, &ca);
        Vector3 dir = {ca * 0.85f, 0.52f, sa * 0.85f};
        Shrub_AddBladeLeaf(b, pos, dir, (Vector3){0.0f, 1.0f, 0.0f},
                           radius, radius * 0.62f, radius * 0.12f,
                           hFrac, fminf(1.0f, hFrac + 0.12f), 0.72f, baseCol, tipCol);
    }
}

static void Shrub_AddEllipsoidPad(ShrubMeshBuilder *b, Vector3 center, Vector3 axisRight, Vector3 axisUp, Vector3 axisFwd,
                                  float rx, float ry, float rz, int rings, int segs,
                                  float hBase, float hTop, float partMask, Color baseCol, Color topCol)
{
    for (int r = 0; r < rings; r++)
    {
        float phi0 = ((float)r / (float)rings) * PI - PI * 0.5f;
        float phi1 = ((float)(r + 1) / (float)rings) * PI - PI * 0.5f;
        float y0 = sinf(phi0), cr0 = cosf(phi0);
        float y1 = sinf(phi1), cr1 = cosf(phi1);
        float t0 = (float)r / (float)rings;
        float t1 = (float)(r + 1) / (float)rings;
        Color c0 = {
            (unsigned char)(baseCol.r + (topCol.r - baseCol.r) * t0),
            (unsigned char)(baseCol.g + (topCol.g - baseCol.g) * t0),
            (unsigned char)(baseCol.b + (topCol.b - baseCol.b) * t0),
            (unsigned char)(baseCol.a + (topCol.a - baseCol.a) * t0),
        };
        Color c1 = {
            (unsigned char)(baseCol.r + (topCol.r - baseCol.r) * t1),
            (unsigned char)(baseCol.g + (topCol.g - baseCol.g) * t1),
            (unsigned char)(baseCol.b + (topCol.b - baseCol.b) * t1),
            (unsigned char)(baseCol.a + (topCol.a - baseCol.a) * t1),
        };
        Vector2 uv0 = {hBase + (hTop - hBase) * t0, partMask};
        Vector2 uv1 = {hBase + (hTop - hBase) * t1, partMask};

        for (int s = 0; s < segs; s++)
        {
            float a0 = ((float)s / (float)segs) * 2.0f * PI;
            float a1 = ((float)(s + 1) / (float)segs) * 2.0f * PI;
            float st0, ct0, st1, ct1;
            Nature_FastSinCos(a0, &st0, &ct0);
            Nature_FastSinCos(a1, &st1, &ct1);

            Vector3 d00 = {axisRight.x * cr0 * ct0 + axisUp.x * y0 + axisFwd.x * cr0 * st0,
                           axisRight.y * cr0 * ct0 + axisUp.y * y0 + axisFwd.y * cr0 * st0,
                           axisRight.z * cr0 * ct0 + axisUp.z * y0 + axisFwd.z * cr0 * st0};
            Vector3 d01 = {axisRight.x * cr0 * ct1 + axisUp.x * y0 + axisFwd.x * cr0 * st1,
                           axisRight.y * cr0 * ct1 + axisUp.y * y0 + axisFwd.y * cr0 * st1,
                           axisRight.z * cr0 * ct1 + axisUp.z * y0 + axisFwd.z * cr0 * st1};
            Vector3 d10 = {axisRight.x * cr1 * ct0 + axisUp.x * y1 + axisFwd.x * cr1 * st0,
                           axisRight.y * cr1 * ct0 + axisUp.y * y1 + axisFwd.y * cr1 * st0,
                           axisRight.z * cr1 * ct0 + axisUp.z * y1 + axisFwd.z * cr1 * st0};
            Vector3 d11 = {axisRight.x * cr1 * ct1 + axisUp.x * y1 + axisFwd.x * cr1 * st1,
                           axisRight.y * cr1 * ct1 + axisUp.y * y1 + axisFwd.y * cr1 * st1,
                           axisRight.z * cr1 * ct1 + axisUp.z * y1 + axisFwd.z * cr1 * st1};

            Vector3 p00 = {center.x + axisRight.x * cr0 * ct0 * rx + axisUp.x * y0 * ry + axisFwd.x * cr0 * st0 * rz,
                           center.y + axisRight.y * cr0 * ct0 * rx + axisUp.y * y0 * ry + axisFwd.y * cr0 * st0 * rz,
                           center.z + axisRight.z * cr0 * ct0 * rx + axisUp.z * y0 * ry + axisFwd.z * cr0 * st0 * rz};
            Vector3 p01 = {center.x + axisRight.x * cr0 * ct1 * rx + axisUp.x * y0 * ry + axisFwd.x * cr0 * st1 * rz,
                           center.y + axisRight.y * cr0 * ct1 * rx + axisUp.y * y0 * ry + axisFwd.y * cr0 * st1 * rz,
                           center.z + axisRight.z * cr0 * ct1 * rx + axisUp.z * y0 * ry + axisFwd.z * cr0 * st1 * rz};
            Vector3 p10 = {center.x + axisRight.x * cr1 * ct0 * rx + axisUp.x * y1 * ry + axisFwd.x * cr1 * st0 * rz,
                           center.y + axisRight.y * cr1 * ct0 * rx + axisUp.y * y1 * ry + axisFwd.y * cr1 * st0 * rz,
                           center.z + axisRight.z * cr1 * ct0 * rx + axisUp.z * y1 * ry + axisFwd.z * cr1 * st0 * rz};
            Vector3 p11 = {center.x + axisRight.x * cr1 * ct1 * rx + axisUp.x * y1 * ry + axisFwd.x * cr1 * st1 * rz,
                           center.y + axisRight.y * cr1 * ct1 * rx + axisUp.y * y1 * ry + axisFwd.y * cr1 * st1 * rz,
                           center.z + axisRight.z * cr1 * ct1 * rx + axisUp.z * y1 * ry + axisFwd.z * cr1 * st1 * rz};

            Vector3 n00 = Vector3Normalize(d00), n01 = Vector3Normalize(d01);
            Vector3 n10 = Vector3Normalize(d10), n11 = Vector3Normalize(d11);
            if (r > 0)
                ShrubBuilder_PushTri(b, p00, n00, uv0, c0, p10, n10, uv1, c1, p11, n11, uv1, c1);
            if (r < rings - 1)
                ShrubBuilder_PushTri(b, p00, n00, uv0, c0, p11, n11, uv1, c1, p01, n01, uv0, c0);
        }
    }
}

static void Shrub_AddPricklyPadBranch(ShrubMeshBuilder *b, Vector3 basePos, float yaw, float tiltX, float rollZ,
                                      float h, float w, float th, int depth, int lod)
{
    float sy, cy, st, ct, sr, cr;
    Nature_FastSinCos(yaw, &sy, &cy);
    Nature_FastSinCos(tiltX, &st, &ct);
    Nature_FastSinCos(rollZ, &sr, &cr);

    Vector3 right = {cy * cr, sr, -sy * cr};
    Vector3 up = Vector3Normalize((Vector3){-cy * sr + sy * st, cr * ct, sy * sr + cy * st});
    Vector3 fwd = Vector3Normalize(Vector3CrossProduct(right, up));
    right = Vector3Normalize(Vector3CrossProduct(up, fwd));

    Vector3 padCenter = Vector3Add(basePos, Vector3Scale(up, h * 0.48f));
    Color padDark = {66, 118, 52, 60};
    Color padBright = {104, 158, 72, 130};
    int rings = (lod == 0) ? 5 : 3;
    int segs = (lod == 0) ? 8 : 6;
    float hBase = fminf(0.85f, (float)depth * 0.25f);
    Shrub_AddEllipsoidPad(b, padCenter, right, up, fwd, w * 0.5f, h * 0.5f, th * 0.5f,
                          rings, segs, hBase, fminf(0.95f, hBase + 0.30f), 1.0f, padDark, padBright);

    if (lod == 0)
    {
        Color spineCol = {246, 228, 186, 110};
        for (int a = 0; a < 6; a++)
        {
            float ang = (float)a * 1.047197f + (float)depth * 0.4f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            float side = (a & 1) ? 1.0f : -1.0f;
            Vector3 spPos = Vector3Add(padCenter,
                Vector3Add(Vector3Scale(right, ca * w * 0.24f),
                Vector3Add(Vector3Scale(up, sa * h * 0.24f), Vector3Scale(fwd, side * th * 0.44f))));
            Vector3 spDir = Vector3Normalize(Vector3Add(Vector3Scale(fwd, side * 0.85f), Vector3Scale(up, 0.45f)));
            Shrub_AddBladeLeaf(b, spPos, spDir, up, 0.048f, 0.008f, 0.0f, hBase + 0.1f, hBase + 0.2f, 0.5f, spineCol, spineCol);
        }
    }

    if (depth < ((lod == 0) ? 2 : 1))
    {
        int children = (depth == 0) ? 3 : 2;
        for (int c = 0; c < children; c++)
        {
            float edgeAng = (((float)c + 0.5f) / (float)children - 0.5f) * 1.35f;
            Vector3 attach = Vector3Add(basePos,
                Vector3Add(Vector3Scale(right, sinf(edgeAng) * w * 0.34f),
                           Vector3Scale(up, h * (0.72f + cosf(edgeAng) * 0.20f))));
            Shrub_AddPricklyPadBranch(b, attach,
                                      yaw + (float)(c - 1) * 0.55f,
                                      tiltX + ((c & 1) ? 0.22f : -0.18f),
                                      rollZ + ((float)c - 0.5f) * 0.28f,
                                      h * 0.82f, w * 0.82f, th * 0.86f, depth + 1, lod);
        }
    }
    else
    {
        // Yellow-coral cup blooms and ruby prickly pear fruits on upper pad rims
        Color bloomBase = {255, 107, 61, 200};
        Color bloomTip = {255, 208, 56, 255};
        Color fruitDark = {152, 22, 48, 120};
        Color fruitBright = {196, 38, 66, 175};
        Vector3 topEdge = Vector3Add(basePos, Vector3Scale(up, h * 0.96f));
        Shrub_AddTipFloret(b, topEdge, 0.064f, 0.94f, bloomBase, bloomTip, (lod == 0) ? 5 : 3);
        if (lod == 0)
        {
            Vector3 fruitPos = Vector3Add(basePos, Vector3Add(Vector3Scale(right, w * 0.28f), Vector3Scale(up, h * 0.86f)));
            Shrub_AddEllipsoidPad(b, fruitPos, right, up, fwd, 0.028f, 0.042f, 0.028f, 3, 5, 0.85f, 0.96f, 0.72f, fruitDark, fruitBright);
        }
    }
}

static void Shrub_AddAgaveTroughLeaf(ShrubMeshBuilder *b, float az, float elevRad,
                                     float length, float width, float arch,
                                     int segsL, bool addTeeth,
                                     Color baseCol, Color midCol, Color tipCol, Color spineCol)
{
    float sa, ca;
    Nature_FastSinCos(az, &sa, &ca);
    Vector3 fwd = {ca, 0.0f, sa};
    Vector3 side = {-sa, 0.0f, ca};
    float cosE = cosf(elevRad), sinE = sinf(elevRad);

    Vector3 prevL = {0}, prevC = {0}, prevR = {0};
    Vector3 prevNL = {0}, prevNC = {0}, prevNR = {0};
    Color prevCol = baseCol;
    float prevH = 0.08f;

    for (int i = 0; i <= segsL; i++)
    {
        float v = (float)i / (float)segsL;
        float wProf = sinf(powf(1.0f - v, 0.70f) * PI * 0.82f) * width;
        if (i == segsL) wProf = 0.002f;
        float channelDepth = (1.0f - v * 0.65f) * width * 0.22f;

        float localZ = v * length;
        float localY = powf(v, 1.25f) * length * 0.16f - powf(v, 2.4f) * arch;
        float rDist = localZ * cosE - localY * sinE;
        float yPos = 0.04f + localZ * sinE + localY * cosE;

        Vector3 center = {fwd.x * rDist, yPos, fwd.z * rDist};
        Vector3 pL = Vector3Add(Vector3Subtract(center, Vector3Scale(side, wProf * 0.5f)), (Vector3){0, channelDepth, 0});
        Vector3 pR = Vector3Add(Vector3Add(center, Vector3Scale(side, wProf * 0.5f)), (Vector3){0, channelDepth, 0});

        Vector3 nC = Vector3Normalize((Vector3){-fwd.x * sinE * 0.4f, cosE, -fwd.z * sinE * 0.4f});
        Vector3 nL = Vector3Normalize(Vector3Add(nC, Vector3Scale(side, 0.38f)));
        Vector3 nR = Vector3Normalize(Vector3Subtract(nC, Vector3Scale(side, 0.38f)));

        Color c = (v < 0.5f)
            ? (Color){
                (unsigned char)(baseCol.r + (midCol.r - baseCol.r) * (v * 2.0f)),
                (unsigned char)(baseCol.g + (midCol.g - baseCol.g) * (v * 2.0f)),
                (unsigned char)(baseCol.b + (midCol.b - baseCol.b) * (v * 2.0f)),
                (unsigned char)(baseCol.a + (midCol.a - baseCol.a) * (v * 2.0f))}
            : (Color){
                (unsigned char)(midCol.r + (tipCol.r - midCol.r) * ((v - 0.5f) * 2.0f)),
                (unsigned char)(midCol.g + (tipCol.g - midCol.g) * ((v - 0.5f) * 2.0f)),
                (unsigned char)(midCol.b + (tipCol.b - midCol.b) * ((v - 0.5f) * 2.0f)),
                (unsigned char)(midCol.a + (tipCol.a - midCol.a) * ((v - 0.5f) * 2.0f))};
        float hVal = 0.08f + 0.88f * v;

        if (i > 0)
        {
            Vector2 uv0 = {prevH, 1.0f};
            Vector2 uv1 = {hVal, 1.0f};
            ShrubBuilder_PushTri(b, prevL, prevNL, uv0, prevCol, pL, nL, uv1, c, center, nC, uv1, c);
            ShrubBuilder_PushTri(b, prevL, prevNL, uv0, prevCol, center, nC, uv1, c, prevC, prevNC, uv0, prevCol);
            ShrubBuilder_PushTri(b, prevC, prevNC, uv0, prevCol, center, nC, uv1, c, pR, nR, uv1, c);
            ShrubBuilder_PushTri(b, prevC, prevNC, uv0, prevCol, pR, nR, uv1, c, prevR, prevNR, uv0, prevCol);

            if (addTeeth && i >= 2 && i < segsL)
            {
                Vector3 tDirL = Vector3Normalize(Vector3Add(Vector3Scale(side, -0.8f), Vector3Scale(fwd, 0.45f)));
                Vector3 tDirR = Vector3Normalize(Vector3Add(Vector3Scale(side, 0.8f), Vector3Scale(fwd, 0.45f)));
                Shrub_AddBladeLeaf(b, pL, tDirL, (Vector3){0, 1, 0}, 0.028f, 0.014f, 0.0f, hVal, hVal, 0.5f, spineCol, spineCol);
                Shrub_AddBladeLeaf(b, pR, tDirR, (Vector3){0, 1, 0}, 0.028f, 0.014f, 0.0f, hVal, hVal, 0.5f, spineCol, spineCol);
            }
        }
        prevL = pL; prevC = center; prevR = pR;
        prevNL = nL; prevNC = nC; prevNR = nR;
        prevCol = c; prevH = hVal;
    }

    // Dark mahogany terminal spine at apex
    Vector3 tipDir = Vector3Normalize((Vector3){fwd.x * cosE, sinE - arch * 0.4f, fwd.z * cosE});
    Shrub_AddBranchSeg(b, prevC, Vector3Add(prevC, Vector3Scale(tipDir, 0.065f)),
                       0.008f, 0.001f, 3, 0.94f, 1.0f, spineCol, spineCol);
}

static Mesh Shrub_BuildArchetypeMesh(MapShrubArchetype arch, int lod)
{
    ShrubMeshBuilder b = {0};
    if (!ShrubBuilder_Init(&b, lod == 0 ? 4096 : 1536))
    {
        ShrubBuilder_Free(&b);
        return (Mesh){0};
    }

    int segSides = (lod == 0) ? 4 : 3;

    if (arch == MAP_SHRUB_XERIC_THORN)
    {
        // Bụi Nhựa Thơm & Gai Sa Mạc (Larrea tridentata): V-forked resinous stems + airy olive-amber leaf sprigs + yellow florets
        Color bark0 = {64, 48, 36, 0};
        Color bark1 = {92, 72, 54, 55};
        Color bark2 = {118, 96, 72, 115};
        Color leafBase = {94, 118, 48, 175};
        Color leafTop = {166, 176, 74, 245};
        Color bloomBase = {216, 168, 36, 200};
        Color bloomTip = {252, 218, 72, 255};
        Color thornCol = {184, 166, 128, 140};

        int canes = (lod == 0) ? 7 : 4;
        for (int k = 0; k < canes; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)canes) + 0.25f * (float)(k & 1);
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            float reach = 0.44f + 0.14f * ((k & 1) ? 1.0f : -0.35f);
            float hTop = 0.62f + 0.16f * ((k % 3) * 0.4f);

            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * reach * 0.44f, hTop * 0.46f, sa * reach * 0.44f};
            Vector3 p2 = {ca * reach, hTop, sa * reach};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.026f, 0.008f, (lod == 0) ? 4 : 2, segSides, 0.0f, 0.85f, bark0, bark2);

            int twigs = (lod == 0) ? 3 : 2;
            for (int tw = 0; tw < twigs; tw++)
            {
                float tFrac = 0.45f + 0.24f * (float)tw;
                float subAz = ang + ((tw & 1) ? 0.58f : -0.58f);
                float tsa, tca;
                Nature_FastSinCos(subAz, &tsa, &tca);
                Vector3 sp0 = {p0.x + (p2.x - p0.x) * tFrac, p0.y + (p2.y - p0.y) * tFrac, p0.z + (p2.z - p0.z) * tFrac};
                Vector3 sp1 = {sp0.x + tca * 0.14f, sp0.y + 0.12f, sp0.z + tsa * 0.14f};
                Vector3 sp2 = {sp0.x + tca * 0.26f, sp0.y + 0.22f, sp0.z + tsa * 0.26f};
                Shrub_AddCurvedBranch(&b, sp0, sp1, sp2, 0.011f, 0.004f, 2, 3, 0.45f, 0.88f, bark1, bark2);

                int sprigs = (lod == 0) ? 3 : 1;
                for (int s = 0; s < sprigs; s++)
                {
                    float u = (s == 0) ? 0.45f : ((s == 1) ? 0.78f : 1.0f);
                    Vector3 lp = {sp0.x + (sp2.x - sp0.x) * u, sp0.y + (sp2.y - sp0.y) * u, sp0.z + (sp2.z - sp0.z) * u};
                    Shrub_AddLeafSprig(&b, lp, subAz + (float)s * 1.1f, 0.28f + 0.15f * (float)s,
                                       0.13f, 0.044f, (lod == 0) ? 3 : 2, 0.55f, 0.95f, leafBase, leafTop);
                }
                if (lod == 0 && (tw == 1 || k % 2 == 0))
                {
                    Shrub_AddTipFloret(&b, sp2, 0.048f, 0.92f, bloomBase, bloomTip, 4);
                }
            }
            if (lod == 0 && (k & 1) == 0)
            {
                Vector3 thDir = {-sa * 0.65f + ca * 0.45f, 0.45f, ca * 0.65f + sa * 0.45f};
                Shrub_AddBladeLeaf(&b, p1, thDir, (Vector3){0, 1, 0}, 0.11f, 0.015f, 0.0f, 0.45f, 0.65f, 0.5f, bark2, thornCol);
            }
        }
    }
    else if (arch == MAP_SHRUB_DESERT_SAGE)
    {
        // Ngải Đắng Sa Mạc (Artemisia tridentata): gnarled silver-bark trunks + dense airy blue-silver 3-lobed leaf clouds + ochre panicles
        Color stem0 = {94, 86, 76, 0};
        Color stem1 = {138, 132, 118, 95};
        Color sageBase = {112, 134, 114, 175};
        Color sageTop = {178, 198, 176, 255};
        Color ochreBase = {196, 158, 62, 200};
        Color ochreTip = {236, 204, 102, 255};

        int stems = (lod == 0) ? 5 : 3;
        for (int k = 0; k < stems; k++)
        {
            float az = (float)k * (2.0f * PI / (float)stems) + 0.22f * (float)(k & 1);
            float sa, ca;
            Nature_FastSinCos(az, &sa, &ca);
            float lean = 0.26f + 0.10f * (float)(k % 2);
            float h = 0.56f + 0.14f * (float)(k % 3);

            Vector3 p0 = {ca * 0.03f, -0.04f, sa * 0.03f};
            Vector3 p1 = {cosf(az + 0.35f) * lean * 0.55f, h * 0.48f, sinf(az + 0.35f) * lean * 0.55f};
            Vector3 p2 = {cosf(az - 0.20f) * lean * 1.25f, h, sinf(az - 0.20f) * lean * 1.25f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.030f, 0.010f, (lod == 0) ? 4 : 2, segSides, 0.0f, 0.78f, stem0, stem1);

            int forks = (lod == 0) ? 3 : 2;
            for (int f = 0; f < forks; f++)
            {
                float subAz = az + (float)(f - 1) * 0.68f;
                float subLen = 0.24f + 0.06f * (float)f;
                Vector3 sp0 = {p1.x + (p2.x - p1.x) * (0.35f + 0.25f * (float)f),
                               p1.y + (p2.y - p1.y) * (0.35f + 0.25f * (float)f),
                               p1.z + (p2.z - p1.z) * (0.35f + 0.25f * (float)f)};
                Vector3 sp1 = {sp0.x + cosf(subAz) * subLen * 0.55f, sp0.y + subLen * 0.45f, sp0.z + sinf(subAz) * subLen * 0.55f};
                Vector3 sp2 = {sp0.x + cosf(subAz) * subLen, sp0.y + subLen * 0.80f, sp0.z + sinf(subAz) * subLen};
                Shrub_AddCurvedBranch(&b, sp0, sp1, sp2, 0.012f, 0.004f, 2, 3, 0.45f, 0.85f, stem0, stem1);

                // Dense silvery-sage 3-blade leaf clouds around sp1 and sp2
                int clouds = (lod == 0) ? 4 : 2;
                for (int c = 0; c < clouds; c++)
                {
                    float u = 0.35f + 0.65f * ((float)c / (float)(clouds - 1));
                    float cAng = subAz + (float)c * 2.399963f;
                    Vector3 cp = {
                        sp0.x + (sp2.x - sp0.x) * u + cosf(cAng) * 0.07f,
                        sp0.y + (sp2.y - sp0.y) * u + 0.02f * (float)(c & 1),
                        sp0.z + (sp2.z - sp0.z) * u + sinf(cAng) * 0.07f,
                    };
                    Shrub_AddLeafSprig(&b, cp, cAng, 0.22f + 0.20f * (float)(c % 2),
                                       0.15f, 0.056f, (lod == 0) ? 3 : 2, 0.50f, 0.96f, sageBase, sageTop);
                }
                if (lod == 0)
                {
                    Shrub_AddTipFloret(&b, (Vector3){sp2.x, sp2.y + 0.06f, sp2.z}, 0.052f, 0.95f, ochreBase, ochreTip, 3);
                }
            }
        }
        // Central crown tuft
        Shrub_AddLeafSprig(&b, (Vector3){0.0f, 0.44f, 0.0f}, 0.5f, 0.45f, 0.16f, 0.062f, 3, 0.70f, 1.0f, sageBase, sageTop);
    }
    else if (arch == MAP_SHRUB_ARID_AGAVE)
    {
        // Thùa Xanh Sa Mạc (Agave americana): Fibonacci spiral concave U-channel succulent rosette + terminal spines
        Color baseLeaf = {56, 94, 72, 40};
        Color midLeaf = {94, 146, 118, 160};
        Color tipLeaf = {132, 178, 146, 225};
        Color spineCol = {78, 36, 22, 160};

        int leafCount = (lod == 0) ? 26 : 14;
        int segsL = (lod == 0) ? 6 : 4;
        const float goldenAngle = 2.39996323f;

        for (int i = 0; i < leafCount; i++)
        {
            float t = (float)i / (float)(leafCount - 1);
            float az = (float)i * goldenAngle;
            float len = 0.38f + 0.36f * powf(t, 0.72f);
            float wid = 0.068f + 0.062f * powf(t, 0.78f);
            float archCurve = 0.03f + 0.24f * (t * t);
            float elevRad = 1.18f - 0.88f * powf(t, 0.85f); // upright inner spears -> recurved outer blades
            Shrub_AddAgaveTroughLeaf(&b, az, elevRad, len, wid, archCurve, segsL,
                                     (lod == 0 && t > 0.35f),
                                     baseLeaf, midLeaf, tipLeaf, spineCol);
        }
    }
    else if (arch == MAP_SHRUB_GNARLED_DEADWOOD)
    {
        // Cây Roi Lửa & Gỗ Khô Sa Mạc (Fouquieria splendens): fountain of curving desert canes + scarlet tip florets (charred in scorch mode)
        Color wood0 = {62, 50, 42, 0};
        Color wood1 = {96, 80, 68, 45};
        Color wood2 = {132, 114, 96, 95};
        Color caneLeafBase = {88, 118, 56, 150};
        Color caneLeafTop = {124, 156, 76, 210};
        Color scarletBase = {196, 44, 28, 220};
        Color scarletTip = {252, 94, 48, 255};

        int canes = (lod == 0) ? 11 : 6;
        for (int k = 0; k < canes; k++)
        {
            float ang = (float)k * 2.39996323f + 0.15f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            float spread = 0.22f + 0.24f * ((float)(k % 3) * 0.5f);
            float hTop = 0.72f + 0.26f * ((float)((k * 7) % 5) * 0.25f);

            Vector3 p0 = {ca * 0.03f, -0.04f, sa * 0.03f};
            Vector3 p1 = {ca * spread * 0.38f, hTop * 0.52f, sa * spread * 0.38f};
            Vector3 p2 = {ca * spread, hTop, sa * spread};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.022f, 0.006f, (lod == 0) ? 4 : 2, segSides, 0.0f, 0.92f, wood0, wood2);

            if (lod == 0)
            {
                Vector3 midPt = {0.25f * p0.x + 0.5f * p1.x + 0.25f * p2.x,
                                 0.25f * p0.y + 0.5f * p1.y + 0.25f * p2.y,
                                 0.25f * p0.z + 0.5f * p1.z + 0.25f * p2.z};
                Shrub_AddLeafSprig(&b, midPt, ang + 0.4f, 0.25f, 0.075f, 0.026f, 2, 0.45f, 0.70f, caneLeafBase, caneLeafTop);
            }
            // Scarlet flame panicle at cane tip (chars automatically when scorch > 0.3 in shrub_foliage.fs)
            Shrub_AddTipFloret(&b, p2, (lod == 0) ? 0.085f : 0.068f, 0.96f, scarletBase, scarletTip, (lod == 0) ? 4 : 3);
        }
    }
    else if (arch == MAP_SHRUB_LUSH_AZALEA)
    {
        // Đỗ Quyên / Cúc Bụi: layered 3D leaf-sprig dome + raised slender flower stalks with crisp blossoms
        Color bark = {66, 50, 36, 0};
        Color leafDark = {48, 88, 36, 155};
        Color leafBright = {98, 152, 62, 235};
        Color blossomBase = {228, 126, 158, 220};
        Color blossomTip = {254, 198, 214, 255};

        int lobes = (lod == 0) ? 7 : 4;
        for (int k = 0; k < lobes; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)lobes);
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * 0.22f, 0.30f, sa * 0.22f};
            Vector3 p2 = {ca * 0.38f, 0.52f + 0.08f * (float)(k & 1), sa * 0.38f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.028f, 0.010f, 2, segSides, 0.0f, 0.55f, bark, bark);

            int sprigs = (lod == 0) ? 3 : 2;
            for (int s = 0; s < sprigs; s++)
            {
                float sAng = ang + (float)(s - 1) * 0.65f;
                Shrub_AddLeafSprig(&b, p2, sAng, 0.22f + 0.18f * (float)s,
                                   0.22f, 0.082f, (lod == 0) ? 3 : 2, 0.55f, 0.94f, leafDark, leafBright);
            }
            if (lod == 0)
            {
                Vector3 stalkTop = {p2.x * 1.12f, p2.y + 0.20f, p2.z * 1.12f};
                Shrub_AddBranchSeg(&b, p2, stalkTop, 0.007f, 0.003f, 3, 0.75f, 0.92f, leafDark, leafBright);
                Shrub_AddTipFloret(&b, stalkTop, 0.085f, 0.98f, blossomBase, blossomTip, 5);
            }
        }
        Shrub_AddLeafSprig(&b, (Vector3){0.0f, 0.56f, 0.0f}, 0.4f, 0.52f, 0.22f, 0.082f, 3, 0.72f, 1.0f, leafDark, leafBright);
    }
    else if (arch == MAP_SHRUB_ALPINE_JUNIPER)
    {
        // Tùng Tuyết Lùn (Krummholz): low wind-swept prostrate branches + flat layered 3D needle fans
        Color bark = {68, 56, 48, 0};
        Color needleBase = {36, 66, 52, 135};
        Color needleTop = {82, 126, 94, 215};

        int mats = (lod == 0) ? 6 : 3;
        for (int k = 0; k < mats; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)mats) + 0.28f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {0.0f, -0.03f, 0.0f};
            Vector3 p1 = {ca * 0.20f, 0.12f, sa * 0.20f};
            Vector3 p2 = {ca * 0.40f, 0.22f + 0.05f * (float)(k & 1), sa * 0.40f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.030f, 0.008f, 2, segSides, 0.0f, 0.45f, bark, bark);

            int fans = (lod == 0) ? 3 : 2;
            for (int f = 0; f < fans; f++)
            {
                float u = 0.45f + 0.55f * ((float)f / (float)(fans - 1));
                Vector3 fp = {p0.x + (p2.x - p0.x) * u, p0.y + (p2.y - p0.y) * u + 0.02f, p0.z + (p2.z - p0.z) * u};
                Shrub_AddLeafSprig(&b, fp, ang + (float)(f - 1) * 0.55f, 0.14f,
                                   0.20f, 0.072f, (lod == 0) ? 3 : 2, 0.45f, 0.92f, needleBase, needleTop);
            }
        }
        Shrub_AddLeafSprig(&b, (Vector3){0.0f, 0.22f, 0.0f}, 0.2f, 0.28f, 0.18f, 0.068f, 3, 0.65f, 0.98f, needleBase, needleTop);
    }
    else if (arch == MAP_SHRUB_PRICKLY_PEAR)
    {
        // Xương Rồng Tai Thỏ (Opuntia littoralis): 3-tier branching cladode pads + golden spines + yellow-coral blooms & ruby fruit
        Shrub_AddPricklyPadBranch(&b, (Vector3){0.0f, -0.03f, 0.0f}, 0.2f, 0.06f, 0.04f, 0.34f, 0.26f, 0.072f, 0, lod);
        Shrub_AddPricklyPadBranch(&b, (Vector3){0.04f, -0.03f, 0.02f}, 2.4f, -0.10f, -0.14f, 0.30f, 0.23f, 0.066f, 1, lod);
    }
    else if (arch == MAP_SHRUB_MEADOW_HAWTHORN)
    {
        // Táo Gai Thảo Nguyên (Crataegus monogyna): thorny woody stems + fresh emerald leaf clouds + snow-white blossom clusters
        Color bark0 = {68, 52, 40, 0};
        Color bark1 = {104, 84, 68, 70};
        Color leafDark = {46, 102, 42, 160};
        Color leafBright = {98, 168, 72, 235};
        Color whiteBase = {236, 224, 210, 210};
        Color whiteTip = {255, 252, 246, 255};

        int stems = (lod == 0) ? 6 : 3;
        for (int k = 0; k < stems; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)stems) + 0.18f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * 0.24f, 0.42f, sa * 0.24f};
            Vector3 p2 = {ca * 0.45f, 0.82f + 0.12f * (float)(k & 1), sa * 0.45f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.030f, 0.010f, 3, segSides, 0.0f, 0.72f, bark0, bark1);
            Shrub_AddLeafSprig(&b, p2, ang, 0.22f, 0.22f, 0.078f, (lod == 0) ? 3 : 2, 0.55f, 0.94f, leafDark, leafBright);
            if (lod == 0)
            {
                Shrub_AddLeafSprig(&b, p1, ang + 0.6f, 0.28f, 0.19f, 0.070f, 3, 0.45f, 0.85f, leafDark, leafBright);
                Shrub_AddTipFloret(&b, (Vector3){p2.x * 1.06f, p2.y + 0.05f, p2.z * 1.06f}, 0.090f, 0.95f, whiteBase, whiteTip, 5);
            }
        }
        Shrub_AddLeafSprig(&b, (Vector3){0.0f, 0.68f, 0.0f}, 0.4f, 0.42f, 0.22f, 0.082f, 3, 0.72f, 0.98f, leafDark, leafBright);
    }
    else if (arch == MAP_SHRUB_MEADOW_LILAC)
    {
        // Tử Đinh Hương Dại (Syringa vulgaris): upright stems + heart leaf clouds + pyramidal violet-lavender panicles
        Color bark = {86, 76, 66, 0};
        Color leafDark = {44, 96, 48, 155};
        Color leafBright = {86, 154, 78, 230};
        Color lilacBase = {126, 76, 168, 210};
        Color lilacTip = {198, 154, 236, 255};

        int stems = (lod == 0) ? 6 : 3;
        for (int k = 0; k < stems; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)stems);
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * 0.16f, 0.48f, sa * 0.16f};
            Vector3 p2 = {ca * 0.34f, 0.96f + 0.14f * (float)(k % 2), sa * 0.34f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.026f, 0.008f, 3, segSides, 0.0f, 0.75f, bark, bark);
            Shrub_AddLeafSprig(&b, p1, ang + 0.4f, 0.25f, 0.21f, 0.082f, (lod == 0) ? 3 : 2, 0.45f, 0.85f, leafDark, leafBright);
            Shrub_AddLeafSprig(&b, p2, ang - 0.3f, 0.20f, 0.19f, 0.078f, (lod == 0) ? 3 : 2, 0.65f, 0.95f, leafDark, leafBright);
            Shrub_AddTipFloret(&b, (Vector3){p2.x, p2.y + 0.06f, p2.z}, 0.088f, 0.92f, lilacBase, lilacTip, 4);
            if (lod == 0)
                Shrub_AddTipFloret(&b, (Vector3){p2.x, p2.y + 0.14f, p2.z}, 0.058f, 0.98f, lilacBase, lilacTip, 3);
        }
    }
    else if (arch == MAP_SHRUB_MEADOW_DOG_ROSE)
    {
        // Hoa Hồng Tầm Xuân (Rosa canina): gracefully arching fountain canes + serrated sprigs + blush-pink wild roses
        Color cane0 = {78, 92, 52, 0};
        Color cane1 = {96, 118, 62, 85};
        Color leafDark = {48, 98, 42, 160};
        Color leafBright = {96, 162, 72, 235};
        Color roseBase = {238, 128, 164, 215};
        Color roseTip = {255, 216, 230, 255};

        int canes = (lod == 0) ? 8 : 4;
        for (int k = 0; k < canes; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)canes) + 0.15f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            float reach = 0.56f + 0.16f * (float)(k & 1);
            float peakH = 0.78f + 0.18f * (float)(k % 3);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * reach * 0.50f, peakH, sa * reach * 0.50f};
            Vector3 p2 = {ca * reach * 1.15f, peakH * 0.72f, sa * reach * 1.15f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.022f, 0.006f, (lod == 0) ? 4 : 2, segSides, 0.0f, 0.85f, cane0, cane1);

            Shrub_AddLeafSprig(&b, p1, ang + 0.35f, 0.18f, 0.21f, 0.074f, (lod == 0) ? 3 : 2, 0.60f, 0.92f, leafDark, leafBright);
            Shrub_AddLeafSprig(&b, p2, ang - 0.25f, -0.08f, 0.19f, 0.070f, 2, 0.55f, 0.88f, leafDark, leafBright);
            Shrub_AddTipFloret(&b, (Vector3){p1.x * 1.05f, p1.y + 0.06f, p1.z * 1.05f}, 0.092f, 0.96f, roseBase, roseTip, (lod == 0) ? 5 : 3);
            if (lod == 0 && (k & 1) == 0)
                Shrub_AddTipFloret(&b, p2, 0.080f, 0.90f, roseBase, roseTip, 4);
        }
    }
    else if (arch == MAP_SHRUB_MEADOW_ELDERBERRY)
    {
        // Cơm Cháy Thảo Nguyên (Sambucus nigra): spreading woody stems + creamy-white flat umbrella corymbs
        Color bark = {88, 78, 62, 0};
        Color leafDark = {38, 86, 38, 165};
        Color leafBright = {76, 138, 56, 235};
        Color creamBase = {236, 228, 186, 210};
        Color creamTip = {255, 252, 234, 255};

        int stems = (lod == 0) ? 6 : 3;
        for (int k = 0; k < stems; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)stems) + 0.30f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * 0.28f, 0.46f, sa * 0.28f};
            Vector3 p2 = {ca * 0.52f, 0.82f + 0.12f * (float)(k & 1), sa * 0.52f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.028f, 0.009f, 3, segSides, 0.0f, 0.78f, bark, bark);
            Shrub_AddLeafSprig(&b, p1, ang + 0.4f, 0.16f, 0.24f, 0.084f, (lod == 0) ? 3 : 2, 0.48f, 0.88f, leafDark, leafBright);
            Shrub_AddLeafSprig(&b, p2, ang - 0.3f, 0.10f, 0.22f, 0.078f, 2, 0.65f, 0.94f, leafDark, leafBright);
            Shrub_AddEllipsoidPad(&b, (Vector3){p2.x, p2.y + 0.08f, p2.z},
                                  (Vector3){1, 0, 0}, (Vector3){0, 1, 0}, (Vector3){0, 0, 1},
                                  0.14f, 0.042f, 0.14f, 2, (lod == 0) ? 6 : 4, 0.88f, 0.98f, 0.78f, creamBase, creamTip);
        }
    }
    else // MAP_SHRUB_MEADOW_ROWAN
    {
        // Thanh Lương Trà Bụi (Sorbus aucuparia): silvery-bronze trunks + feathery pinnate leaf sprigs + vermilion berry umbels
        Color bark = {102, 86, 72, 0};
        Color leafDark = {52, 108, 48, 160};
        Color leafBright = {104, 168, 72, 235};
        Color berryDark = {196, 46, 22, 150};
        Color berryBright = {248, 92, 34, 210};

        int trunks = (lod == 0) ? 6 : 3;
        for (int k = 0; k < trunks; k++)
        {
            float ang = (float)k * (2.0f * PI / (float)trunks) + 0.22f;
            float sa, ca;
            Nature_FastSinCos(ang, &sa, &ca);
            Vector3 p0 = {ca * 0.02f, -0.04f, sa * 0.02f};
            Vector3 p1 = {ca * 0.24f, 0.52f, sa * 0.24f};
            Vector3 p2 = {ca * 0.46f, 0.98f + 0.14f * (float)(k % 2), sa * 0.46f};
            Shrub_AddCurvedBranch(&b, p0, p1, p2, 0.026f, 0.008f, 3, segSides, 0.0f, 0.82f, bark, bark);
            Shrub_AddLeafSprig(&b, p1, ang + 0.3f, 0.20f, 0.22f, 0.072f, (lod == 0) ? 3 : 2, 0.50f, 0.88f, leafDark, leafBright);
            Shrub_AddLeafSprig(&b, p2, ang - 0.2f, 0.15f, 0.21f, 0.068f, 2, 0.70f, 0.95f, leafDark, leafBright);
            Shrub_AddEllipsoidPad(&b, (Vector3){p2.x * 1.05f, p2.y - 0.02f, p2.z * 1.05f},
                                  (Vector3){1, 0, 0}, (Vector3){0, 1, 0}, (Vector3){0, 0, 1},
                                  0.088f, 0.064f, 0.088f, 2, (lod == 0) ? 5 : 4, 0.85f, 0.96f, 0.72f, berryDark, berryBright);
        }
    }

    Mesh m = ShrubBuilder_ToMesh(&b);
    ShrubBuilder_Free(&b);
    return m;
}

static Shader s_shrubShader = {0};
static bool s_shrubShaderLoaded = false;
static int s_shrubLocInstTex = -1;
static int s_shrubLocWorldOffset = -1;
static int s_shrubLocLightVP = -1;
static int s_shrubLocStaticLightVP = -1;

static Shader Shrub_GetShader(void)
{
    if (!s_shrubShaderLoaded)
    {
        s_shrubShaderLoaded = true;
        s_shrubShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/shrub_instanced.vs",
            "maps/toolkit/shaders/shrub_foliage.fs");
        if (s_shrubShader.id != 0 && s_shrubShader.id != rlGetShaderIdDefault())
        {
            s_shrubLocInstTex = GetShaderLocation(s_shrubShader, "u_shrubInstanceTex");
            s_shrubLocWorldOffset = GetShaderLocation(s_shrubShader, "u_worldOffset");
            s_shrubLocLightVP = GetShaderLocation(s_shrubShader, "u_lightVP");
            s_shrubLocStaticLightVP = GetShaderLocation(s_shrubShader, "u_staticLightVP");
            VFXLight_RegisterShader(s_shrubShader);
            MapShadow_ConfigureShader(s_shrubShader);
            (void)Nature_GetCachedLocs(s_shrubShader);
        }
    }
    return s_shrubShader;
}

typedef struct
{
    Vector3 position;
    float boundRadius;
    float packedTexel0[4];
    float packedTexel1[4];
} ShrubInstanceRecord;

typedef struct
{
    ShrubInstanceRecord *records[MAP_SHRUB_ARCHETYPE_COUNT];
    float *uploadScratch[MAP_SHRUB_ARCHETYPE_COUNT][2];
    int texRows[MAP_SHRUB_ARCHETYPE_COUNT];
    bool prepared;
    Camera3D preparedCamera;
    Vector3 preparedOffset;
    int preparedQuality;
} ShrubInternalState;

static void Shrub_PackInstance(const MapShrubPlacement *p, ShrubInstanceRecord *out)
{
    Color tint = (p->foliageTint.a > 0) ? p->foliageTint : WHITE;
    int packedRGB = ((int)tint.r << 16) | ((int)tint.g << 8) | (int)tint.b;
    float scaleXZ = (p->scaleXZ > 0.1f) ? p->scaleXZ : 1.0f;
    float scaleY = (p->scaleY > 0.1f) ? p->scaleY : 1.0f;
    float phase = p->phase - floorf(p->phase);
    if (phase < 0.0f) phase += 1.0f;
    if (phase > 0.999f) phase = 0.999f;

    float dryQ = floorf(fminf(fmaxf(p->dryness, 0.0f), 1.0f) * 15.0f + 0.5f);
    float scorchQ = floorf(fminf(fmaxf(p->scorch, 0.0f), 1.0f) * 15.0f + 0.5f);
    float frostQ = floorf(fminf(fmaxf(p->frost, 0.0f), 1.0f) * 15.0f + 0.5f);
    float packedState = phase + dryQ * 16.0f + scorchQ * 256.0f + frostQ * 4096.0f;

    out->position = p->position;
    out->boundRadius = fmaxf(scaleXZ, scaleY) * 1.15f;
    out->packedTexel0[0] = p->position.x;
    out->packedTexel0[1] = p->position.y;
    out->packedTexel0[2] = p->position.z;
    out->packedTexel0[3] = p->rotationDeg * DEG2RAD;
    out->packedTexel1[0] = scaleXZ;
    out->packedTexel1[1] = scaleY;
    out->packedTexel1[2] = packedState;
    out->packedTexel1[3] = (float)packedRGB;
}

#define SHRUB_HASH_BUCKETS 2048

static inline unsigned int Shrub_CellHash(int gx, int gz)
{
    unsigned int h = ((unsigned int)gx * 73856093u) ^ ((unsigned int)gz * 19349663u);
    return h & (SHRUB_HASH_BUCKETS - 1u);
}

int MapFlora_GenerateShrubPlacements(
    MapShrubPlacement *outPlacements,
    const MapGroundSurface *ground,
    Vector3 groundCenter,
    MapShrubDistributionConfig config)
{
    if (!outPlacements || config.maxCount <= 0 || !ground)
        return 0;

    unsigned int rng = config.seed ? config.seed : 0x54B2A9u;
    unsigned int selectRng = rng ^ 0x9E3779B9u;
    float minSpacing = (config.minSpacing > 0.25f) ? config.minSpacing : 2.20f;
    float minSpacingSq = minSpacing * minSpacing;
    float invCellSize = 1.0f / minSpacing;
    int count = 0;
    unsigned int accepted = 0;

    float spanX = config.maxBounds.x - config.minBounds.x;
    float spanZ = config.maxBounds.y - config.minBounds.y;
    if (spanX <= 1.0f || spanZ <= 1.0f)
        return 0;

    int *hashHeads = (int *)MemAlloc(SHRUB_HASH_BUCKETS * sizeof(int));
    int *hashNext = (int *)MemAlloc((size_t)config.maxCount * sizeof(int));
    if (!hashHeads || !hashNext)
    {
        if (hashHeads) MemFree(hashHeads);
        if (hashNext) MemFree(hashNext);
        return 0;
    }
    for (int b = 0; b < SHRUB_HASH_BUCKETS; b++)
        hashHeads[b] = -1;

    bool useGrid = config.useStratifiedGrid ||
                   ((spanX * spanZ) > (float)config.maxCount * minSpacingSq * 2.5f);
    float gridStep = 1.0f;
    int cols = 1, rows = 1;
    int totalCandidates = config.maxCount * 28;
    if (useGrid)
    {
        float targetSamples = (float)config.maxCount * 8.0f;
        gridStep = fmaxf(minSpacing * 1.15f, sqrtf((spanX * spanZ) / fmaxf(targetSamples, 1.0f)));
        if (config.focalCount > 0 && gridStep > 10.0f)
            gridStep = 10.0f;
        cols = (int)ceilf(spanX / gridStep);
        rows = (int)ceilf(spanZ / gridStep);
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
        totalCandidates = cols * rows;
    }

    for (int iter = 0; iter < totalCandidates; iter++)
    {
        if (!useGrid && count >= config.maxCount)
            break;

        float x, z;
        if (useGrid)
        {
            int r = iter / cols;
            int c = iter - r * cols;
            x = config.minBounds.x + ((float)c + 0.12f + 0.76f * Nature_Random01(&rng)) * gridStep;
            z = config.minBounds.y + ((float)r + 0.12f + 0.76f * Nature_Random01(&rng)) * gridStep;
            if (x > config.maxBounds.x || z > config.maxBounds.y)
                continue;
        }
        else
        {
            x = config.minBounds.x + Nature_Random01(&rng) * spanX;
            z = config.minBounds.y + Nature_Random01(&rng) * spanZ;
        }

        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(ground, groundCenter, x, z, &pos, &normal))
            continue;
        if (normal.y < 0.68f || pos.y < -0.15f)
            continue;

        if (config.sampleWaterClearance)
        {
            float clearance = config.sampleWaterClearance(x, z, config.ctx);
            if (clearance < 0.35f)
                continue;
        }

        float rockShelter = 0.0f;
        if (config.rocks && config.rockCount > 0)
        {
            bool insideRock = false;
            for (int r = 0; r < config.rockCount; r++)
            {
                const MapRockItem *rk = &config.rocks[r];
                if (rk->archetype == MAP_ROCK_SCREE)
                    continue;
                float leeMax = rk->radiusScale + 4.5f;
                float dx = x - rk->position.x;
                if (dx > leeMax || dx < -leeMax)
                    continue;
                float dz = z - rk->position.z;
                if (dz > leeMax || dz < -leeMax)
                    continue;
                float d2 = dx * dx + dz * dz;
                float minClear = rk->radiusScale * 0.95f + 0.45f;
                if (d2 < minClear * minClear)
                {
                    insideRock = true;
                    break;
                }
                if (d2 < leeMax * leeMax)
                {
                    float w = 1.0f - (sqrtf(d2) - minClear) / fmaxf(leeMax - minClear, 0.5f);
                    if (w > rockShelter)
                        rockShelter = w;
                }
            }
            if (insideRock)
                continue;
        }

        float focalBoost = 1.0f;
        if (config.focalCount > 0)
        {
            float minFocalD2 = 1e20f;
            int fc = config.focalCount < 4 ? config.focalCount : 4;
            for (int f = 0; f < fc; f++)
            {
                float fdx = x - config.focalPoints[f].x;
                float fdz = z - config.focalPoints[f].y;
                float d2 = fdx * fdx + fdz * fdz;
                if (d2 < minFocalD2)
                    minFocalD2 = d2;
            }
            float coreR = (config.focalCoreRadius > 1.0f) ? config.focalCoreRadius : 55.0f;
            if (minFocalD2 > coreR * coreR)
                focalBoost = 0.28f;
            else
                focalBoost = 1.45f;
        }

        float density = (0.42f + rockShelter * 0.58f) * focalBoost;
        if (config.customDensityFn)
        {
            float custom = config.customDensityFn(x, z, config.ctx);
            if (custom <= 0.0f)
                continue;
            density *= custom;
        }
        if (Nature_Random01(&rng) > fminf(1.0f, density))
            continue;

        // O(1) 2D Spatial Hash check for minimum spacing
        int gx = (int)floorf(x * invCellSize);
        int gz = (int)floorf(z * invCellSize);
        bool tooClose = false;
        for (int oz = -1; oz <= 1 && !tooClose; oz++)
        {
            for (int ox = -1; ox <= 1 && !tooClose; ox++)
            {
                int idx = hashHeads[Shrub_CellHash(gx + ox, gz + oz)];
                while (idx >= 0)
                {
                    float dx = x - outPlacements[idx].position.x;
                    float dz = z - outPlacements[idx].position.z;
                    if (dx * dx + dz * dz < minSpacingSq)
                    {
                        tooClose = true;
                        break;
                    }
                    idx = hashNext[idx];
                }
            }
        }
        if (tooClose)
            continue;

        float roll = Nature_Random01(&rng);
        MapShrubArchetype arch = MAP_SHRUB_LUSH_AZALEA;
        float dry = 0.0f, scorch = 0.0f, frost = 0.0f;
        Color fTint = WHITE;

        if (config.volcanicWeight > 0.45f)
        {
            if (roll < 0.45f)
            {
                arch = MAP_SHRUB_GNARLED_DEADWOOD;
                scorch = 0.55f + 0.35f * Nature_Random01(&rng);
                dry = 0.90f;
            }
            else if (roll < 0.80f)
            {
                arch = MAP_SHRUB_DESERT_SAGE;
                dry = 0.85f;
                scorch = 0.22f * Nature_Random01(&rng);
                fTint = (Color){185, 178, 162, 255};
            }
            else
            {
                arch = MAP_SHRUB_XERIC_THORN;
                dry = 0.88f;
                scorch = 0.18f;
                fTint = (Color){176, 158, 122, 255};
            }
        }
        else if (config.alpineWeight > 0.45f)
        {
            arch = (roll < 0.78f) ? MAP_SHRUB_ALPINE_JUNIPER : MAP_SHRUB_GNARLED_DEADWOOD;
            frost = 0.68f + 0.30f * Nature_Random01(&rng);
            dry = 0.25f;
            fTint = (Color){210, 228, 238, 255};
        }
        else if (config.aridWeight > 0.45f)
        {
            dry = 0.78f + 0.20f * Nature_Random01(&rng);
            if (roll < 0.24f)
            {
                arch = MAP_SHRUB_PRICKLY_PEAR;
                dry = 0.52f + 0.25f * Nature_Random01(&rng);
                fTint = (Color){216, 232, 188, 255};
            }
            else if (roll < 0.50f)
            {
                arch = MAP_SHRUB_XERIC_THORN;
                fTint = (Color){218, 206, 156, 255};
            }
            else if (roll < 0.76f)
            {
                arch = MAP_SHRUB_DESERT_SAGE;
                fTint = (Color){222, 226, 210, 255};
            }
            else if (roll < 0.90f)
            {
                arch = MAP_SHRUB_ARID_AGAVE;
                fTint = (Color){192, 218, 174, 255};
            }
            else
            {
                arch = MAP_SHRUB_GNARLED_DEADWOOD;
                fTint = (Color){204, 188, 166, 255};
            }
        }
        else if (config.metallicWeight > 0.45f)
        {
            if (roll < 0.25f)
                arch = MAP_SHRUB_LUSH_AZALEA;
            else if (roll < 0.48f)
                arch = MAP_SHRUB_MEADOW_HAWTHORN;
            else if (roll < 0.68f)
                arch = MAP_SHRUB_MEADOW_DOG_ROSE;
            else if (roll < 0.85f)
                arch = MAP_SHRUB_XERIC_THORN;
            else
                arch = MAP_SHRUB_PRICKLY_PEAR;
            dry = 0.30f + 0.25f * Nature_Random01(&rng);
            fTint = (Color){218, 222, 192, 255};
        }
        else
        {
            if (roll < 0.18f)
                arch = MAP_SHRUB_LUSH_AZALEA;
            else if (roll < 0.36f)
                arch = MAP_SHRUB_MEADOW_HAWTHORN;
            else if (roll < 0.52f)
                arch = MAP_SHRUB_MEADOW_LILAC;
            else if (roll < 0.70f)
                arch = MAP_SHRUB_MEADOW_DOG_ROSE;
            else if (roll < 0.86f)
                arch = MAP_SHRUB_MEADOW_ELDERBERRY;
            else
                arch = MAP_SHRUB_MEADOW_ROWAN;
            fTint = (Color){226, 242, 212, 255};
        }

        float scXZ = 0.82f + 0.48f * Nature_Random01(&rng);
        float scY = 0.80f + 0.44f * Nature_Random01(&rng);
        MapShrubPlacement cand = {
            .position = pos,
            .scaleXZ = scXZ,
            .scaleY = scY,
            .rotationDeg = Nature_Random01(&rng) * 360.0f,
            .phase = Nature_Random01(&rng),
            .dryness = dry,
            .scorch = scorch,
            .frost = frost,
            .foliageTint = fTint,
            .accentTint = WHITE,
            .archetype = arch,
        };

        accepted++;
        if (count < config.maxCount)
        {
            int idx = count++;
            outPlacements[idx] = cand;
            unsigned int b = Shrub_CellHash(gx, gz);
            hashNext[idx] = hashHeads[b];
            hashHeads[b] = idx;
        }
        else
        {
            unsigned int slot = (unsigned int)(((uint64_t)Nature_NextRandom(&selectRng) * (uint64_t)accepted) >> 32);
            if (slot < (unsigned int)config.maxCount)
                outPlacements[slot] = cand;
        }
    }
    MemFree(hashHeads);
    MemFree(hashNext);
    return count;
}

float MapFlora_EvaluateShrubUnderstorey(
    float x, float z,
    const MapShrubPlacement *shrubs,
    int shrubCount)
{
    if (!shrubs || shrubCount <= 0)
        return 0.0f;
    float best = 0.0f;
    for (int i = 0; i < shrubCount; i++)
    {
        float dx = x - shrubs[i].position.x;
        float dz = z - shrubs[i].position.z;
        float d2 = dx * dx + dz * dz;
        float maxR = shrubs[i].scaleXZ * 1.45f + 0.85f;
        if (d2 < maxR * maxR)
        {
            float w = 1.0f - sqrtf(d2) / maxR;
            if (w > best)
                best = w;
        }
    }
    return best;
}

MapShrubSystem MapProp_CreateShrubSystem(
    const MapShrubPlacement *placements, int count,
    const MapGroundSurface *ground, Vector3 worldCenter,
    float drawDistance)
{
    MapShrubSystem sys = {0};
    if (!placements || count <= 0)
        return sys;

    sys.drawDistance = (drawDistance > 10.0f) ? drawDistance : 95.0f;
    sys.lodDistance = 28.0f;

    Shader sh = Shrub_GetShader();
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return sys;

    ShrubInternalState *state = (ShrubInternalState *)MemAlloc(sizeof(ShrubInternalState));
    if (!state)
        return sys;
    memset(state, 0, sizeof(*state));

    for (int a = 0; a < MAP_SHRUB_ARCHETYPE_COUNT; a++)
    {
        int typeCount = 0;
        for (int i = 0; i < count; i++)
        {
            if (placements[i].archetype == (MapShrubArchetype)a)
                typeCount++;
        }
        if (typeCount <= 0)
            continue;

        Mesh m0 = Shrub_BuildArchetypeMesh((MapShrubArchetype)a, 0);
        Mesh m1 = Shrub_BuildArchetypeMesh((MapShrubArchetype)a, 1);
        if (!m0.vertices || !m1.vertices)
        {
            if (m0.vertices) UnloadMesh(m0);
            if (m1.vertices) UnloadMesh(m1);
            continue;
        }
        sys.lod0Models[a] = LoadModelFromMesh(m0);
        sys.lod1Models[a] = LoadModelFromMesh(m1);
        if (sys.lod0Models[a].materialCount > 0)
            sys.lod0Models[a].materials[0].shader = sh;
        if (sys.lod1Models[a].materialCount > 0)
            sys.lod1Models[a].materials[0].shader = sh;
        sys.modelLoaded[a] = true;
        sys.totalCounts[a] = typeCount;

        int texels = typeCount * 2;
        int rows = (texels + 511) / 512;
        if (rows < 1) rows = 1;
        state->texRows[a] = rows;
        state->records[a] = (ShrubInstanceRecord *)MemAlloc((size_t)typeCount * sizeof(ShrubInstanceRecord));
        state->uploadScratch[a][0] = (float *)MemAlloc((size_t)(512 * rows * 4) * sizeof(float));
        state->uploadScratch[a][1] = (float *)MemAlloc((size_t)(512 * rows * 4) * sizeof(float));

        if (!state->records[a] || !state->uploadScratch[a][0] || !state->uploadScratch[a][1])
            continue;

        memset(state->uploadScratch[a][0], 0, (size_t)(512 * rows * 4) * sizeof(float));
        memset(state->uploadScratch[a][1], 0, (size_t)(512 * rows * 4) * sizeof(float));

        int k = 0;
        for (int i = 0; i < count; i++)
        {
            if (placements[i].archetype != (MapShrubArchetype)a)
                continue;
            MapShrubPlacement p = placements[i];
            if (ground && ground->ready)
            {
                Vector3 gp, gn;
                if (MapProp_SampleGroundSurface(ground, worldCenter, p.position.x, p.position.z, &gp, &gn))
                    p.position.y = gp.y - 0.03f;
            }
            Shrub_PackInstance(&p, &state->records[a][k]);
            memcpy(state->uploadScratch[a][0] + (size_t)k * 8, state->records[a][k].packedTexel0, 4 * sizeof(float));
            memcpy(state->uploadScratch[a][0] + (size_t)k * 8 + 4, state->records[a][k].packedTexel1, 4 * sizeof(float));
            k++;
        }

        Image img0 = {
            .data = state->uploadScratch[a][0],
            .width = 512,
            .height = rows,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
        };
        sys.instanceTextures[a][0] = LoadTextureFromImage(img0);
        sys.instanceTextures[a][1] = LoadTextureFromImage(img0);
        for (int lod = 0; lod < 2; lod++)
        {
            if (sys.instanceTextures[a][lod].id != 0)
            {
                SetTextureFilter(sys.instanceTextures[a][lod], TEXTURE_FILTER_POINT);
                SetTextureWrap(sys.instanceTextures[a][lod], TEXTURE_WRAP_CLAMP);
            }
        }
        sys.visibleCounts[a][0] = typeCount;
        sys.visibleCounts[a][1] = 0;
    }

    sys.internalState = state;
    sys.ready = true;
    return sys;
}

void MapProp_PrepareShrubSystem(MapShrubSystem *sys, Vector3 worldOffset)
{
    if (!sys || !sys->ready || !sys->internalState)
        return;

    ShrubInternalState *state = (ShrubInternalState *)sys->internalState;
    int quality = GfxQuality_Get();
    float rangeScale = (quality >= GFX_HIGH) ? 1.0f : ((quality == GFX_MED) ? 0.85f : 0.72f);
    float lodScale = (quality >= GFX_HIGH) ? 1.0f : ((quality == GFX_MED) ? 0.82f : 0.65f);

    if (state->prepared &&
        state->preparedQuality == quality &&
        Vector3DistanceSqr(state->preparedCamera.position, camera.position) < 0.0049f &&
        Vector3DistanceSqr(state->preparedCamera.target, camera.target) < 0.0049f &&
        Vector3DistanceSqr(state->preparedOffset, worldOffset) < 0.0001f &&
        fabsf(state->preparedCamera.fovy - camera.fovy) < 0.05f)
    {
        return;
    }

    Nature_RefreshFrustumCache();

    float maxDist = sys->drawDistance * rangeScale;
    float maxDistSq = maxDist * maxDist;
    float lodDist = sys->lodDistance * lodScale;
    float lodDistSq = lodDist * lodDist;

    for (int a = 0; a < MAP_SHRUB_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a] || sys->totalCounts[a] <= 0 || !state->records[a])
            continue;

        int vis0 = 0;
        int vis1 = 0;
        float *dst0 = state->uploadScratch[a][0];
        float *dst1 = state->uploadScratch[a][1];
        const ShrubInstanceRecord *recs = state->records[a];
        int total = sys->totalCounts[a];

        for (int i = 0; i < total; i++)
        {
            Vector3 worldPos = Vector3Add(recs[i].position, worldOffset);
            float dx = worldPos.x - camera.position.x;
            float dy = worldPos.y - camera.position.y;
            float dz = worldPos.z - camera.position.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > maxDistSq)
                continue;
            if (!Nature_IsChunkVisibleFast(worldPos, recs[i].boundRadius))
                continue;

            int lod = (distSq <= lodDistSq) ? 0 : 1;
            if (lod == 0)
            {
                memcpy(dst0 + (size_t)vis0 * 8, recs[i].packedTexel0, 4 * sizeof(float));
                memcpy(dst0 + (size_t)vis0 * 8 + 4, recs[i].packedTexel1, 4 * sizeof(float));
                vis0++;
            }
            else
            {
                memcpy(dst1 + (size_t)vis1 * 8, recs[i].packedTexel0, 4 * sizeof(float));
                memcpy(dst1 + (size_t)vis1 * 8 + 4, recs[i].packedTexel1, 4 * sizeof(float));
                vis1++;
            }
        }

        sys->visibleCounts[a][0] = vis0;
        sys->visibleCounts[a][1] = vis1;
        if (vis0 > 0 && sys->instanceTextures[a][0].id != 0)
            UpdateTexture(sys->instanceTextures[a][0], dst0);
        if (vis1 > 0 && sys->instanceTextures[a][1].id != 0)
            UpdateTexture(sys->instanceTextures[a][1], dst1);
    }

    state->prepared = true;
    state->preparedCamera = camera;
    state->preparedOffset = worldOffset;
    state->preparedQuality = quality;
}

void MapProp_DrawShrubSystem(const MapShrubSystem *sys, Vector3 worldOffset,
                             float time, Vector2 windDirection, float windStrength)
{
    if (!sys || !sys->ready)
        return;

    Shader sh = Shrub_GetShader();
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault())
        return;

    bool anyVisible = false;
    for (int a = 0; a < MAP_SHRUB_ARCHETYPE_COUNT; a++)
    {
        if (sys->modelLoaded[a] && (sys->visibleCounts[a][0] > 0 || sys->visibleCounts[a][1] > 0))
        {
            anyVisible = true;
            break;
        }
    }
    if (!anyVisible)
        return;

    Nature_BeginWindReceiverShader(sh);
    Nature_UpdateShader(sh, time, windDirection, windStrength,
                        false, 0.0f, NATURE_WIND_RESPONSE_FLOWER);
    if (s_shrubLocWorldOffset >= 0)
        SetShaderValue(sh, s_shrubLocWorldOffset, &worldOffset, SHADER_UNIFORM_VEC3);

    Matrix lightVP = EnvShadow_GetLightVP();
    Matrix staticLightVP = EnvShadow_GetStaticLightVP();
    if (s_shrubLocLightVP >= 0) SetShaderValueMatrix(sh, s_shrubLocLightVP, lightVP);
    if (s_shrubLocStaticLightVP >= 0) SetShaderValueMatrix(sh, s_shrubLocStaticLightVP, staticLightVP);
    MapShadow_UpdateShader(sh);

    Matrix mvp = MatrixMultiply(MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview()), rlGetMatrixProjection());
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MVP], mvp);

    if (s_shrubLocInstTex >= 0)
    {
        int unit0 = 0;
        SetShaderValue(sh, s_shrubLocInstTex, &unit0, SHADER_UNIFORM_INT);
    }

    rlDisableBackfaceCulling();
    for (int a = 0; a < MAP_SHRUB_ARCHETYPE_COUNT; a++)
    {
        if (!sys->modelLoaded[a])
            continue;
        for (int lod = 0; lod < 2; lod++)
        {
            int visCount = sys->visibleCounts[a][lod];
            if (visCount <= 0 || sys->instanceTextures[a][lod].id == 0)
                continue;
            const Model *mdl = (lod == 0) ? &sys->lod0Models[a] : &sys->lod1Models[a];
            const Mesh *mesh = &mdl->meshes[0];
            rlActiveTextureSlot(0);
            rlEnableTexture(sys->instanceTextures[a][lod].id);
            rlEnableVertexArray(mesh->vaoId);
            rlDrawVertexArrayInstanced(0, mesh->vertexCount, visCount);
            rlDisableVertexArray();
        }
    }
    rlEnableBackfaceCulling();
    Nature_EndWindReceiverShader();
}

void MapProp_DrawShrubShadowCasters(const MapShrubSystem *sys, Vector3 worldOffset,
                                    float time, Vector2 windDirection, float windStrength)
{
    (void)worldOffset;
    (void)time;
    (void)windDirection;
    (void)windStrength;
    if (!sys || !sys->ready || GfxQuality_Get() < GFX_HIGH)
        return;
    // Shrubs use ground root contact AO + hemisphere ambient; optional depth casters can reuse LOD1 here if needed.
}

void MapProp_UnloadShrubSystem(MapShrubSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    ShrubInternalState *state = (ShrubInternalState *)sys->internalState;
    for (int a = 0; a < MAP_SHRUB_ARCHETYPE_COUNT; a++)
    {
        if (sys->modelLoaded[a])
        {
            UnloadModel(sys->lod0Models[a]);
            UnloadModel(sys->lod1Models[a]);
            sys->modelLoaded[a] = false;
        }
        for (int lod = 0; lod < 2; lod++)
        {
            if (sys->instanceTextures[a][lod].id != 0)
            {
                UnloadTexture(sys->instanceTextures[a][lod]);
                sys->instanceTextures[a][lod] = (Texture2D){0};
            }
        }
        if (state)
        {
            if (state->records[a]) MemFree(state->records[a]);
            if (state->uploadScratch[a][0]) MemFree(state->uploadScratch[a][0]);
            if (state->uploadScratch[a][1]) MemFree(state->uploadScratch[a][1]);
        }
        sys->totalCounts[a] = 0;
        sys->visibleCounts[a][0] = 0;
        sys->visibleCounts[a][1] = 0;
    }
    if (state)
    {
        MemFree(state);
        sys->internalState = NULL;
    }
    sys->ready = false;
}

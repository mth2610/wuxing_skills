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
        return;
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

static void Tree_AddTaperedSegment(TreeMeshBuilder *b, Vector3 a, Vector3 bPt,
                                   float rA, float rB, int sides,
                                   Color colA, Color colB)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(bPt, a));
    Vector3 ref = (fabsf(dir.y) < 0.95f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(ref, dir));
    Vector3 fwd = Vector3CrossProduct(dir, right);

    for (int i = 0; i < sides; i++)
    {
        float a0 = ((float)i / (float)sides) * 2.0f * PI;
        float a1 = ((float)(i + 1) / (float)sides) * 2.0f * PI;
        Vector3 n0 = Vector3Add(Vector3Scale(right, cosf(a0)), Vector3Scale(fwd, sinf(a0)));
        Vector3 n1 = Vector3Add(Vector3Scale(right, cosf(a1)), Vector3Scale(fwd, sinf(a1)));

        Vector3 p00 = Vector3Add(a, Vector3Scale(n0, rA));
        Vector3 p01 = Vector3Add(a, Vector3Scale(n1, rA));
        Vector3 p10 = Vector3Add(bPt, Vector3Scale(n0, rB));
        Vector3 p11 = Vector3Add(bPt, Vector3Scale(n1, rB));

        Vector2 uvBark = {0.0f, 0.0f};
        TreeBuilder_PushTri(b, p00, n0, uvBark, colA, p10, n0, uvBark, colB, p11, n1, uvBark, colB);
        TreeBuilder_PushTri(b, p00, n0, uvBark, colA, p11, n1, uvBark, colB, p01, n1, uvBark, colA);
    }
}

// Adds a layered botanical foliage crown dome with spherical normals for soft Xianxia shading
static void Tree_AddCanopyCluster(TreeMeshBuilder *b, Vector3 center, float rx, float ry, float rz,
                                  Color baseCol, Color topCol)
{
    const int rings = 3;
    const int segs = 6;
    Vector3 prevRing[7];
    Vector3 prevNorm[7];
    for (int r = 0; r < rings; r++)
    {
        float phi0 = ((float)r / (float)rings) * PI - PI * 0.5f;
        float phi1 = ((float)(r + 1) / (float)rings) * PI - PI * 0.5f;
        float y0 = sinf(phi0), cr0 = cosf(phi0);
        float y1 = sinf(phi1), cr1 = cosf(phi1);
        Color c0 = (r == 0) ? baseCol : topCol;
        Color c1 = (r == rings - 1) ? topCol : baseCol;
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

            if (r > 0)
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p10, n10, uvFoliage, c1, p11, n11, uvFoliage, c1);
            if (r < rings - 1)
                TreeBuilder_PushTri(b, p00, n00, uvFoliage, c0, p11, n11, uvFoliage, c1, p01, n01, uvFoliage, c0);
        }
    }
    (void)prevRing;
    (void)prevNorm;
}

static Mesh Tree_BuildArchetypeMesh(MapTreeArchetype arch)
{
    TreeMeshBuilder b = {0};
    if (!TreeBuilder_Init(&b, 1200))
    {
        TreeBuilder_Free(&b);
        return (Mesh){0};
    }

    if (arch == MAP_TREE_ANCIENT_PINE)
    {
        Color bark0 = {62, 48, 38, 0};
        Color bark1 = {76, 58, 46, 95};
        Vector3 p0 = {0.0f, 0.0f, 0.0f};
        Vector3 p1 = {0.22f, 1.55f, 0.08f};
        Vector3 p2 = {-0.14f, 3.10f, 0.25f};
        Vector3 p3 = {0.18f, 4.40f, 0.10f};
        Tree_AddTaperedSegment(&b, p0, p1, 0.26f, 0.19f, 5, bark0, bark1);
        Tree_AddTaperedSegment(&b, p1, p2, 0.19f, 0.13f, 5, bark1, bark1);
        Tree_AddTaperedSegment(&b, p2, p3, 0.13f, 0.07f, 5, bark1, bark1);
        // Sculpted pine boughs
        Tree_AddTaperedSegment(&b, p1, (Vector3){1.20f, 2.25f, -0.35f}, 0.10f, 0.04f, 4, bark1, bark1);
        Tree_AddTaperedSegment(&b, p2, (Vector3){-1.15f, 3.65f, 0.55f}, 0.08f, 0.03f, 4, bark1, bark1);

        Color pineDeep = {32, 68, 36, 210};
        Color pineTop = {56, 102, 52, 240};
        Tree_AddCanopyCluster(&b, (Vector3){1.30f, 2.40f, -0.40f}, 1.25f, 0.42f, 1.15f, pineDeep, pineTop);
        Tree_AddCanopyCluster(&b, (Vector3){-1.20f, 3.80f, 0.58f}, 1.15f, 0.38f, 1.10f, pineDeep, pineTop);
        Tree_AddCanopyCluster(&b, (Vector3){0.20f, 4.60f, 0.12f}, 1.45f, 0.50f, 1.35f, pineDeep, pineTop);
    }
    else if (arch == MAP_TREE_BAMBOO)
    {
        Color culmBase = {72, 118, 52, 0};
        Color culmTop = {110, 162, 74, 210};
        Vector3 p0 = {0.0f, 0.0f, 0.0f};
        Vector3 p1 = {0.08f, 1.80f, 0.04f};
        Vector3 p2 = {0.22f, 3.60f, 0.12f};
        Vector3 p3 = {0.42f, 5.40f, 0.24f};
        Tree_AddTaperedSegment(&b, p0, p1, 0.065f, 0.055f, 4, culmBase, culmTop);
        Tree_AddTaperedSegment(&b, p1, p2, 0.055f, 0.042f, 4, culmTop, culmTop);
        Tree_AddTaperedSegment(&b, p2, p3, 0.042f, 0.024f, 4, culmTop, culmTop);

        Color leafBase = {88, 138, 62, 245};
        Color leafTip = {138, 186, 86, 255};
        Tree_AddCanopyCluster(&b, (Vector3){0.28f, 3.85f, 0.18f}, 0.72f, 0.28f, 0.68f, leafBase, leafTip);
        Tree_AddCanopyCluster(&b, (Vector3){0.48f, 5.20f, 0.28f}, 0.85f, 0.34f, 0.80f, leafBase, leafTip);
    }
    else if (arch == MAP_TREE_PEACH_BLOSSOM)
    {
        Color bark0 = {54, 38, 34, 0};
        Color bark1 = {68, 48, 42, 105};
        Vector3 p0 = {0.0f, 0.0f, 0.0f};
        Vector3 p1 = {0.15f, 1.35f, -0.05f};
        Vector3 bL = {-0.85f, 2.45f, 0.40f};
        Vector3 bR = {0.95f, 2.65f, -0.35f};
        Vector3 bC = {0.10f, 3.05f, 0.15f};
        Tree_AddTaperedSegment(&b, p0, p1, 0.21f, 0.14f, 5, bark0, bark1);
        Tree_AddTaperedSegment(&b, p1, bL, 0.10f, 0.04f, 4, bark1, bark1);
        Tree_AddTaperedSegment(&b, p1, bR, 0.10f, 0.04f, 4, bark1, bark1);
        Tree_AddTaperedSegment(&b, p1, bC, 0.11f, 0.05f, 4, bark1, bark1);

        Color petalDeep = {215, 118, 146, 235};
        Color petalLight = {255, 206, 218, 255};
        Tree_AddCanopyCluster(&b, (Vector3){-0.95f, 2.60f, 0.45f}, 1.05f, 0.62f, 1.00f, petalDeep, petalLight);
        Tree_AddCanopyCluster(&b, (Vector3){1.02f, 2.80f, -0.38f}, 1.12f, 0.65f, 1.05f, petalDeep, petalLight);
        Tree_AddCanopyCluster(&b, (Vector3){0.12f, 3.35f, 0.18f}, 1.30f, 0.72f, 1.25f, petalDeep, petalLight);
    }
    else // MAP_TREE_WEEPING_WILLOW
    {
        Color bark0 = {60, 48, 36, 0};
        Color bark1 = {78, 64, 48, 90};
        Vector3 p0 = {0.0f, 0.0f, 0.0f};
        Vector3 p1 = {0.18f, 1.75f, 0.12f};
        Vector3 p2 = {0.32f, 3.20f, 0.25f};
        Tree_AddTaperedSegment(&b, p0, p1, 0.24f, 0.16f, 5, bark0, bark1);
        Tree_AddTaperedSegment(&b, p1, p2, 0.16f, 0.09f, 5, bark1, bark1);

        Color willowBase = {66, 114, 54, 245};
        Color willowTop = {114, 158, 72, 255};
        Tree_AddCanopyCluster(&b, (Vector3){0.35f, 3.45f, 0.28f}, 1.65f, 1.15f, 1.65f, willowBase, willowTop);
        Tree_AddCanopyCluster(&b, (Vector3){-0.65f, 2.65f, -0.45f}, 1.10f, 1.10f, 1.05f, willowBase, willowTop);
        Tree_AddCanopyCluster(&b, (Vector3){0.95f, 2.75f, 0.65f}, 1.15f, 1.15f, 1.10f, willowBase, willowTop);
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
    EndShaderMode();
}

void MapProp_DrawTreeSystemShadowCasters(const MapTreeSystem *sys, Shader depthShader,
                                         float time, Vector2 windDirection, float windStrength)
{
    (void)sys; (void)depthShader; (void)time; (void)windDirection; (void)windStrength;
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

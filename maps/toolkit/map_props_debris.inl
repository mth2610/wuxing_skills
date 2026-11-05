// Generalized 3D Debris System (Fallen Leaves, Scree Pebbles, Twigs, Moss Patches)
// Included into map_props.c — not a standalone compilation unit.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Helper: build a procedural 3D leaf blade mesh (single-sided 6 triangles; rendered with backface culling off)
static Mesh Debris_BuildLeafMesh(void)
{
    Mesh mesh = Nature_AllocMesh(18); // 6 triangles * 3 vertices (no duplicate coplanar Z-fighting tris)
    if (!mesh.vertices)
        return mesh;

    Vector3 pts[7] = {
        {0.0f, 0.002f, -0.06f},     // 0: base stem
        {-0.038f, 0.008f, -0.015f}, // 1: left margin
        {0.038f, 0.008f, -0.015f},  // 2: right margin
        {0.0f, 0.014f, 0.005f},     // 3: raised center spine
        {-0.032f, 0.009f, 0.035f},  // 4: left upper
        {0.032f, 0.009f, 0.035f},   // 5: right upper
        {0.0f, 0.003f, 0.075f},     // 6: leaf apex tip
    };

    static const int triIndices[6][3] = {
        {0, 1, 3}, {0, 3, 2}, {1, 4, 3}, {2, 3, 5}, {4, 6, 3}, {3, 6, 5}};

    int cursor = 0;
    for (int t = 0; t < 6; t++)
    {
        int i0 = triIndices[t][0];
        int i1 = triIndices[t][1];
        int i2 = triIndices[t][2];
        Vector3 v0 = pts[i0], v1 = pts[i1], v2 = pts[i2];
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(v2, v0)));
        if (fn.y < 0.0f)
            fn = Vector3Negate(fn);

        Vector3 tri[3] = {v0, v1, v2};
        for (int k = 0; k < 3; k++)
        {
            // heightMask = 0.0f so nature_lit.vs skips per-vertex wind/interaction bending on grounded debris
            Nature_SetVertex(&mesh, cursor, tri[k], fn, 0.0f, 0.0f, WHITE);
            Nature_SetUv2(&mesh, cursor, tri[k].x * (1.0f / 0.04f), 0.5f);
            cursor++;
        }
    }
    return mesh;
}

// Helper: build a procedural 3D pebble stone mesh (closed 12-triangle convex hull)
static Mesh Debris_BuildPebbleMesh(void)
{
    Mesh mesh = Nature_AllocMesh(36); // 12 triangles * 3 vertices
    if (!mesh.vertices)
        return mesh;

    Vector3 apex = {0.0f, 0.024f, 0.0f};
    Vector3 base = {0.0f, 0.002f, 0.0f};
    Vector3 rim[6];
    for (int i = 0; i < 6; i++)
    {
        float a = (float)i * 1.04719755f;
        float r = 0.045f * (0.85f + 0.15f * sinf(a * 2.0f + 0.5f));
        float sinA, cosA;
        Nature_FastSinCos(a, &sinA, &cosA);
        rim[i] = (Vector3){cosA * r, 0.010f, sinA * r * 0.75f};
    }

    int cursor = 0;
    // Top dome (6 CCW triangles viewed from above)
    for (int i = 0; i < 6; i++)
    {
        int next = (i + 1) % 6;
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(rim[next], apex), Vector3Subtract(rim[i], apex)));
        if (fn.y < 0.0f)
            fn = Vector3Negate(fn);
        Vector3 tri[3] = {apex, rim[next], rim[i]};
        for (int k = 0; k < 3; k++)
        {
            Nature_SetVertex(&mesh, cursor, tri[k], fn, 0.0f, 0.0f, WHITE);
            Nature_SetUv2(&mesh, cursor, tri[k].x * 20.0f, 0.5f);
            cursor++;
        }
    }
    // Bottom skirt (6 triangles)
    for (int i = 0; i < 6; i++)
    {
        int next = (i + 1) % 6;
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(rim[i], base), Vector3Subtract(rim[next], base)));
        if (fn.y > 0.0f)
            fn = Vector3Negate(fn);
        Vector3 tri[3] = {base, rim[i], rim[next]};
        for (int k = 0; k < 3; k++)
        {
            Nature_SetVertex(&mesh, cursor, tri[k], fn, 0.0f, 0.0f, WHITE);
            Nature_SetUv2(&mesh, cursor, tri[k].x * 20.0f, 0.5f);
            cursor++;
        }
    }
    return mesh;
}

// Helper: build a procedural 3D twig / branch mesh (5 triangles; rendered with backface culling off)
static Mesh Debris_BuildTwigMesh(void)
{
    Mesh mesh = Nature_AllocMesh(15); // 5 triangles * 3 vertices (no duplicate coplanar underside tris)
    if (!mesh.vertices)
        return mesh;

    Vector3 p0 = {-0.09f, 0.003f, -0.015f};
    Vector3 p1 = {0.00f, 0.008f, 0.010f};
    Vector3 p2 = {0.08f, 0.004f, -0.010f};
    Vector3 pF = {0.04f, 0.009f, 0.045f};

    float w = 0.006f;
    Vector3 n01 = Vector3Normalize((Vector3){-(p1.z - p0.z), 0.0f, p1.x - p0.x});
    Vector3 n12 = Vector3Normalize((Vector3){-(p2.z - p1.z), 0.0f, p2.x - p1.x});

    Vector3 v0L = Vector3Add(p0, Vector3Scale(n01, w));
    Vector3 v0R = Vector3Subtract(p0, Vector3Scale(n01, w));
    Vector3 v1L = Vector3Add(p1, Vector3Scale(n01, w * 0.85f));
    Vector3 v1R = Vector3Subtract(p1, Vector3Scale(n01, w * 0.85f));
    Vector3 v2L = Vector3Add(p2, Vector3Scale(n12, w * 0.6f));
    Vector3 v2R = Vector3Subtract(p2, Vector3Scale(n12, w * 0.6f));
    Vector3 vFTip = pF;

    int cursor = 0;
    Vector3 up = {0.0f, 1.0f, 0.0f};
    const Vector3 tris[5][3] = {
        {v0L, v1L, v1R},
        {v0L, v1R, v0R},
        {v1L, v2L, v2R},
        {v1L, v2R, v1R},
        {v1L, vFTip, v2L}};

    for (int t = 0; t < 5; t++)
    {
        for (int k = 0; k < 3; k++)
        {
            Vector3 v = tris[t][k];
            Nature_SetVertex(&mesh, cursor, v, up, 0.0f, 0.0f, WHITE);
            Nature_SetUv2(&mesh, cursor, v.x * 10.0f, 0.5f);
            cursor++;
        }
    }
    return mesh;
}

// Helper: build a low-profile organic moss cushion mesh (6 triangles)
static Mesh Debris_BuildMossMesh(void)
{
    Mesh mesh = Nature_AllocMesh(18);
    if (!mesh.vertices)
        return mesh;

    Vector3 apex = {0.0f, 0.012f, 0.0f};
    Vector3 rim[6];
    for (int i = 0; i < 6; i++)
    {
        float a = (float)i * 1.04719755f;
        float r = 0.065f * (0.82f + 0.18f * cosf(a * 3.0f));
        float sinA, cosA;
        Nature_FastSinCos(a, &sinA, &cosA);
        rim[i] = (Vector3){cosA * r, 0.002f, sinA * r};
    }

    int cursor = 0;
    for (int i = 0; i < 6; i++)
    {
        int next = (i + 1) % 6;
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(rim[next], apex), Vector3Subtract(rim[i], apex)));
        if (fn.y < 0.0f)
            fn = Vector3Negate(fn);
        Vector3 tri[3] = {apex, rim[next], rim[i]};
        for (int k = 0; k < 3; k++)
        {
            Nature_SetVertex(&mesh, cursor, tri[k], fn, 0.0f, 0.0f, WHITE);
            Nature_SetUv2(&mesh, cursor, tri[k].x * 14.0f, 0.35f);
            cursor++;
        }
    }
    return mesh;
}

MapDebrisSystem MapProp_CreateDebrisSystem(
    const MapDebrisPlacement *placements, int count,
    const MapGroundSurface *ground, Vector3 worldCenter, float drawDistance)
{
    MapDebrisSystem debris = {0};
    if (!placements || count <= 0)
        return debris;

    debris.placementCount = count;
    debris.drawDistance = (drawDistance > 5.0f) ? drawDistance : 35.0f;
    debris.placements = (MapDebrisPlacement *)MemAlloc((size_t)count * sizeof(MapDebrisPlacement));
    if (!debris.placements)
        return debris;

    // Group placements by debris type during copy so drawing iterates contiguously per model
    int writeCursor = 0;
    for (int typePass = 0; typePass < MAP_DEBRIS_TYPE_COUNT; typePass++)
    {
        for (int i = 0; i < count; i++)
        {
            int itemType = (placements[i].type >= 0 && placements[i].type < MAP_DEBRIS_TYPE_COUNT)
                               ? (int)placements[i].type
                               : 0;
            if (itemType != typePass)
                continue;

            MapDebrisPlacement p = placements[i];
            p.type = (MapDebrisType)itemType;
            if (ground && ground->ready)
            {
                Vector3 pos, normal;
                if (MapProp_SampleGroundSurface(ground, worldCenter, p.position.x, p.position.z, &pos, &normal))
                {
                    p.position = pos;
                    p.position.y += 0.012f; // prevent z-fighting with terrain surface
                }
            }

            switch (p.type)
            {
            case MAP_DEBRIS_LEAF:
            {
                float hueVar = (float)((i * 37) % 10) * 0.1f;
                p.color = (Color){
                    (unsigned char)(190.0f + 35.0f * hueVar),
                    (unsigned char)(95.0f + 35.0f * hueVar),
                    (unsigned char)(30.0f + 15.0f * hueVar),
                    255};
                break;
            }
            case MAP_DEBRIS_PEBBLE:
            {
                float shade = (float)((i * 29) % 10) * 0.1f;
                unsigned char val = (unsigned char)(140.0f + 25.0f * shade);
                p.color = (Color){val, val, (unsigned char)(val - 4), 255};
                break;
            }
            case MAP_DEBRIS_TWIG:
            {
                float shade = (float)((i * 41) % 10) * 0.1f;
                p.color = (Color){
                    (unsigned char)(85.0f + 20.0f * shade),
                    (unsigned char)(54.0f + 15.0f * shade),
                    (unsigned char)(36.0f + 10.0f * shade),
                    255};
                break;
            }
            case MAP_DEBRIS_MOSS:
            default:
                p.color = (Color){68, 120, 46, 255};
                break;
            }

            debris.placements[writeCursor++] = p;
        }
    }
    debris.placementCount = writeCursor;

    Shader shader = Nature_GetShader(false);

    debris.models[MAP_DEBRIS_LEAF] = Nature_ModelFromMesh(Debris_BuildLeafMesh(), shader);
    debris.modelLoaded[MAP_DEBRIS_LEAF] = true;

    debris.models[MAP_DEBRIS_PEBBLE] = Nature_ModelFromMesh(Debris_BuildPebbleMesh(), shader);
    debris.modelLoaded[MAP_DEBRIS_PEBBLE] = true;

    debris.models[MAP_DEBRIS_TWIG] = Nature_ModelFromMesh(Debris_BuildTwigMesh(), shader);
    debris.modelLoaded[MAP_DEBRIS_TWIG] = true;

    if (MAP_DEBRIS_MOSS < MAP_DEBRIS_TYPE_COUNT)
    {
        debris.models[MAP_DEBRIS_MOSS] = Nature_ModelFromMesh(Debris_BuildMossMesh(), shader);
        debris.modelLoaded[MAP_DEBRIS_MOSS] = true;
    }

    debris.ready = true;
    return debris;
}

void MapProp_DrawDebrisSystem(const MapDebrisSystem *debris, Vector3 cameraPosition)
{
    if (!debris || !debris->ready || debris->placementCount <= 0 || !debris->placements)
        return;

    Shader shader = Nature_GetShader(false);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, (float)fmod(GetTime(), 3600.0), (Vector2){1.0f, 0.0f}, 0.0f,
                        false, 0.0f, NATURE_WIND_RESPONSE_STATIC);
    const NatureShaderLocs *locs = Nature_GetCachedLocs(shader);
    if (locs->viewPos >= 0)
    {
        SetShaderValue(shader, locs->viewPos, &cameraPosition, SHADER_UNIFORM_VEC3);
    }

    Nature_RefreshFrustumCache();

    float maxDist = debris->drawDistance;
    float maxDistSq = maxDist * maxDist;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    bool cullingDisabled = false;

    for (int i = 0; i < debris->placementCount; i++)
    {
        const MapDebrisPlacement *p = &debris->placements[i];
        if ((int)p->type < 0 || p->type >= MAP_DEBRIS_TYPE_COUNT || !debris->modelLoaded[p->type])
            continue;

        float dx = p->position.x - cameraPosition.x;
        float dy = p->position.y - cameraPosition.y;
        float dz = p->position.z - cameraPosition.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > maxDistSq)
            continue;
        if (!Nature_IsChunkVisible(p->position, 0.35f))
            continue;

        // Closed convex pebbles & moss domes can keep backface culling enabled; flat leaves/twigs disable it
        bool needTwoSided = (p->type == MAP_DEBRIS_LEAF || p->type == MAP_DEBRIS_TWIG);
        if (needTwoSided != cullingDisabled)
        {
            rlDrawRenderBatchActive();
            if (needTwoSided)
                rlDisableBackfaceCulling();
            else
                rlEnableBackfaceCulling();
            cullingDisabled = needTwoSided;
        }

        Model model = debris->models[p->type];
        float s = p->scale * ((p->type == MAP_DEBRIS_LEAF) ? 1.15f : 1.55f);
        Vector3 scale = {s, s, s};
        DrawModelEx(model, p->position, rotAxis, p->rotationDeg, scale, p->color);
    }

    if (cullingDisabled)
        rlEnableBackfaceCulling();
    Nature_EndWindReceiverShader();
}

void MapProp_DrawDebrisShadowCasters(const MapDebrisSystem *debris)
{
    if (!debris || !debris->ready)
        return;
}

void MapProp_UnloadDebrisSystem(MapDebrisSystem *debris)
{
    if (!debris || !debris->ready)
        return;
    for (int i = 0; i < MAP_DEBRIS_TYPE_COUNT; i++)
    {
        if (debris->modelLoaded[i])
        {
            UnloadModel(debris->models[i]);
            debris->modelLoaded[i] = false;
        }
    }
    if (debris->placements)
    {
        MemFree(debris->placements);
        debris->placements = NULL;
    }
    debris->placementCount = 0;
    debris->ready = false;
}
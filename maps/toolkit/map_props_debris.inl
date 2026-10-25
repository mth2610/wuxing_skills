// Generalized 3D Debris System (Fallen Leaves, Scree Pebbles, Twigs, Moss Patches)
// Included into map_props.c — not a standalone compilation unit.

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Helper: build a procedural 3D leaf blade mesh
static Mesh Debris_BuildLeafMesh(void)
{
    Mesh mesh = {0};
    mesh.triangleCount = 8;
    mesh.vertexCount = 24; // 8 triangles * 3 vertices

    mesh.vertices  = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals   = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.colors    = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    // Curved organic leaf: stem (0,0), mid-body, spine, curved tip
    Vector3 pts[7] = {
        {0.0f,    0.002f, -0.06f},  // 0: base stem
        {-0.038f, 0.008f, -0.015f}, // 1: left margin
        {0.038f,  0.008f, -0.015f}, // 2: right margin
        {0.0f,    0.014f,  0.005f}, // 3: raised center spine
        {-0.032f, 0.009f,  0.035f}, // 4: left upper
        {0.032f,  0.009f,  0.035f}, // 5: right upper
        {0.0f,    0.003f,  0.075f}, // 6: leaf apex tip
    };

    static const int triIndices[8][3] = {
        {0, 1, 3}, {0, 3, 2},
        {1, 4, 3}, {2, 3, 5},
        {4, 6, 3}, {3, 6, 5},
        // double-sided undersurface
        {0, 3, 1}, {0, 2, 3}
    };

    int cursor = 0;
    for (int t = 0; t < 8; t++) {
        int i0 = triIndices[t][0];
        int i1 = triIndices[t][1];
        int i2 = triIndices[t][2];
        Vector3 v0 = pts[i0], v1 = pts[i1], v2 = pts[i2];
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(v1, v0), Vector3Subtract(v2, v0)));
        if (fn.y < 0.0f && t < 6) fn = Vector3Negate(fn);

        Vector3 tri[3] = {v0, v1, v2};
        for (int k = 0; k < 3; k++) {
            mesh.vertices[cursor * 3 + 0] = tri[k].x;
            mesh.vertices[cursor * 3 + 1] = tri[k].y;
            mesh.vertices[cursor * 3 + 2] = tri[k].z;

            mesh.normals[cursor * 3 + 0] = fn.x;
            mesh.normals[cursor * 3 + 1] = fn.y;
            mesh.normals[cursor * 3 + 2] = fn.z;

            mesh.texcoords[cursor * 2 + 0] = tri[k].x / 0.04f;
            mesh.texcoords[cursor * 2 + 1] = 0.5f;

            mesh.colors[cursor * 4 + 0] = 255;
            mesh.colors[cursor * 4 + 1] = 255;
            mesh.colors[cursor * 4 + 2] = 255;
            mesh.colors[cursor * 4 + 3] = 255;
            cursor++;
        }
    }
    return mesh;
}

// Helper: build a procedural 3D pebble stone mesh
static Mesh Debris_BuildPebbleMesh(void)
{
    Mesh mesh = {0};
    // 8-facet rounded flattened stone
    mesh.triangleCount = 12;
    mesh.vertexCount = 36;

    mesh.vertices  = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals   = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.colors    = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    Vector3 apex = {0.0f, 0.024f, 0.0f};
    Vector3 base = {0.0f, 0.002f, 0.0f};
    Vector3 rim[6];
    for (int i = 0; i < 6; i++) {
        float a = (float)i * 1.04719755f;
        float r = 0.045f * (0.85f + 0.15f * sinf(a * 2.0f + 0.5f));
        rim[i] = (Vector3){cosf(a) * r, 0.010f, sinf(a) * r * 0.75f};
    }

    int cursor = 0;
    // Top dome (6 triangles)
    for (int i = 0; i < 6; i++) {
        int next = (i + 1) % 6;
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(rim[i], apex), Vector3Subtract(rim[next], apex)));
        Vector3 tri[3] = {apex, rim[i], rim[next]};
        for (int k = 0; k < 3; k++) {
            mesh.vertices[cursor * 3 + 0] = tri[k].x;
            mesh.vertices[cursor * 3 + 1] = tri[k].y;
            mesh.vertices[cursor * 3 + 2] = tri[k].z;
            mesh.normals[cursor * 3 + 0]  = fn.x;
            mesh.normals[cursor * 3 + 1]  = fn.y;
            mesh.normals[cursor * 3 + 2]  = fn.z;
            mesh.texcoords[cursor * 2 + 0] = tri[k].x / 0.05f;
            mesh.texcoords[cursor * 2 + 1] = 0.5f;
            mesh.colors[cursor * 4 + 0] = 255;
            mesh.colors[cursor * 4 + 1] = 255;
            mesh.colors[cursor * 4 + 2] = 255;
            mesh.colors[cursor * 4 + 3] = 255;
            cursor++;
        }
    }
    // Bottom skirt (6 triangles)
    for (int i = 0; i < 6; i++) {
        int next = (i + 1) % 6;
        Vector3 fn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(rim[next], base), Vector3Subtract(rim[i], base)));
        Vector3 tri[3] = {base, rim[next], rim[i]};
        for (int k = 0; k < 3; k++) {
            mesh.vertices[cursor * 3 + 0] = tri[k].x;
            mesh.vertices[cursor * 3 + 1] = tri[k].y;
            mesh.vertices[cursor * 3 + 2] = tri[k].z;
            mesh.normals[cursor * 3 + 0]  = fn.x;
            mesh.normals[cursor * 3 + 1]  = fn.y;
            mesh.normals[cursor * 3 + 2]  = fn.z;
            mesh.texcoords[cursor * 2 + 0] = tri[k].x / 0.05f;
            mesh.texcoords[cursor * 2 + 1] = 0.5f;
            mesh.colors[cursor * 4 + 0] = 255;
            mesh.colors[cursor * 4 + 1] = 255;
            mesh.colors[cursor * 4 + 2] = 255;
            mesh.colors[cursor * 4 + 3] = 255;
            cursor++;
        }
    }
    return mesh;
}

// Helper: build a procedural 3D twig / branch mesh
static Mesh Debris_BuildTwigMesh(void)
{
    Mesh mesh = {0};
    mesh.triangleCount = 10;
    mesh.vertexCount = 30;

    mesh.vertices  = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals   = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.colors    = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    // Main bent stick: p0 -> p1 -> p2, side twig: p1 -> pFork
    Vector3 p0 = {-0.09f, 0.003f, -0.015f};
    Vector3 p1 = { 0.00f, 0.008f,  0.010f};
    Vector3 p2 = { 0.08f, 0.004f, -0.010f};
    Vector3 pF = { 0.04f, 0.009f,  0.045f};

    float w = 0.006f;
    Vector3 n01 = Vector3Normalize((Vector3){-(p1.z - p0.z), 0, p1.x - p0.x});
    Vector3 n12 = Vector3Normalize((Vector3){-(p2.z - p1.z), 0, p2.x - p1.x});

    Vector3 v0L = Vector3Add(p0, Vector3Scale(n01, w));
    Vector3 v0R = Vector3Subtract(p0, Vector3Scale(n01, w));
    Vector3 v1L = Vector3Add(p1, Vector3Scale(n01, w * 0.85f));
    Vector3 v1R = Vector3Subtract(p1, Vector3Scale(n01, w * 0.85f));
    Vector3 v2L = Vector3Add(p2, Vector3Scale(n12, w * 0.6f));
    Vector3 v2R = Vector3Subtract(p2, Vector3Scale(n12, w * 0.6f));
    Vector3 vFTip = pF;

    int cursor = 0;
    #define ADD_TRI(va, vb, vc, nrm) do { \
        Vector3 _tri[3] = {va, vb, vc}; \
        for (int _k = 0; _k < 3; _k++) { \
            mesh.vertices[cursor * 3 + 0] = _tri[_k].x; \
            mesh.vertices[cursor * 3 + 1] = _tri[_k].y; \
            mesh.vertices[cursor * 3 + 2] = _tri[_k].z; \
            mesh.normals[cursor * 3 + 0]  = (nrm).x; \
            mesh.normals[cursor * 3 + 1]  = (nrm).y; \
            mesh.normals[cursor * 3 + 2]  = (nrm).z; \
            mesh.texcoords[cursor * 2 + 0] = _tri[_k].x / 0.1f; \
            mesh.texcoords[cursor * 2 + 1] = 0.5f; \
            mesh.colors[cursor * 4 + 0]   = 255; \
            mesh.colors[cursor * 4 + 1]   = 255; \
            mesh.colors[cursor * 4 + 2]   = 255; \
            mesh.colors[cursor * 4 + 3]   = 255; \
            cursor++; \
        } \
    } while(0)

    Vector3 up = {0.0f, 1.0f, 0.0f};
    ADD_TRI(v0L, v1L, v1R, up);
    ADD_TRI(v0L, v1R, v0R, up);
    ADD_TRI(v1L, v2L, v2R, up);
    ADD_TRI(v1L, v2R, v1R, up);
    ADD_TRI(v1L, vFTip, v2L, up);
    // Double sided
    ADD_TRI(v1R, v1L, v0L, Vector3Negate(up));
    ADD_TRI(v0R, v1R, v0L, Vector3Negate(up));
    ADD_TRI(v2R, v2L, v1L, Vector3Negate(up));
    ADD_TRI(v1R, v2R, v1L, Vector3Negate(up));
    ADD_TRI(v2L, vFTip, v1L, Vector3Negate(up));
    #undef ADD_TRI

    return mesh;
}

MapDebrisSystem MapProp_CreateDebrisSystem(
    const MapDebrisPlacement *placements, int count,
    const MapGroundSurface *ground, Vector3 worldCenter, float drawDistance)
{
    MapDebrisSystem debris = {0};
    if (!placements || count <= 0) return debris;

    debris.placementCount = count;
    debris.drawDistance = (drawDistance > 5.0f) ? drawDistance : 35.0f;
    debris.placements = (MapDebrisPlacement *)MemAlloc(count * sizeof(MapDebrisPlacement));
    if (!debris.placements) return debris;
    memcpy(debris.placements, placements, count * sizeof(MapDebrisPlacement));

    // Snap to rendered ground surface and assign harmonious botanical/stony tints
    for (int i = 0; i < count; i++) {
        MapDebrisPlacement *p = &debris.placements[i];
        if (ground && ground->ready) {
            Vector3 pos, normal;
            if (MapProp_SampleGroundSurface(ground, worldCenter, p->position.x, p->position.z, &pos, &normal)) {
                p->position = pos;
                p->position.y += 0.012f; // prevent z-fighting with terrain surface
            }
        }

        // Color variation based on type
        switch (p->type) {
            case MAP_DEBRIS_LEAF: {
                // Autumn golden-amber to russet-orange leaf decay
                float hueVar = (float)((i * 37) % 10) / 10.0f;
                p->color = (Color){
                    (unsigned char)(190 + 35 * hueVar),
                    (unsigned char)(95 + 35 * hueVar),
                    (unsigned char)(30 + 15 * hueVar),
                    255
                };
                break;
            }
            case MAP_DEBRIS_PEBBLE: {
                // Weathered river gravel / scree stone gray
                float shade = (float)((i * 29) % 10) / 10.0f;
                unsigned char val = (unsigned char)(140 + 25 * shade);
                p->color = (Color){val, val, (unsigned char)(val - 4), 255};
                break;
            }
            case MAP_DEBRIS_TWIG: {
                // Forest understory fallen twig / woody bark brown
                float shade = (float)((i * 41) % 10) / 10.0f;
                p->color = (Color){
                    (unsigned char)(85 + 20 * shade),
                    (unsigned char)(54 + 15 * shade),
                    (unsigned char)(36 + 10 * shade),
                    255
                };
                break;
            }
            case MAP_DEBRIS_MOSS: {
                p->color = (Color){68, 120, 46, 255};
                break;
            }
            default: break;
        }
    }

    // Build base archetype meshes
    Mesh leafMesh = Debris_BuildLeafMesh();
    Mesh pebbleMesh = Debris_BuildPebbleMesh();
    Mesh twigMesh = Debris_BuildTwigMesh();

    Shader shader = Nature_GetShader(true);

    debris.models[MAP_DEBRIS_LEAF] = Nature_ModelFromMesh(leafMesh, shader);
    debris.modelLoaded[MAP_DEBRIS_LEAF] = true;

    debris.models[MAP_DEBRIS_PEBBLE] = Nature_ModelFromMesh(pebbleMesh, shader);
    debris.modelLoaded[MAP_DEBRIS_PEBBLE] = true;

    debris.models[MAP_DEBRIS_TWIG] = Nature_ModelFromMesh(twigMesh, shader);
    debris.modelLoaded[MAP_DEBRIS_TWIG] = true;

    debris.ready = true;
    return debris;
}

void MapProp_DrawDebrisSystem(const MapDebrisSystem *debris, Vector3 cameraPosition)
{
    if (!debris || !debris->ready || debris->placementCount <= 0 || !debris->placements) return;

    Shader shader = Nature_GetShader(true);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, (float)GetTime(), (Vector2){1.0f, 0.0f}, 0.1f,
                        false, 0.5f, NATURE_WIND_RESPONSE_STATIC);
    static int s_cachedShaderId = -1;
    static int s_viewPosLoc = -1;
    if ((int)shader.id != s_cachedShaderId) {
        s_cachedShaderId = (int)shader.id;
        s_viewPosLoc = GetShaderLocation(shader, "u_viewPos");
    }
    if (s_viewPosLoc >= 0) {
        SetShaderValue(shader, s_viewPosLoc, &cameraPosition, SHADER_UNIFORM_VEC3);
    }

    rlDisableBackfaceCulling();

    float maxDist = debris->drawDistance;
    float maxDistSq = maxDist * maxDist;

    for (int i = 0; i < debris->placementCount; i++) {
        const MapDebrisPlacement *p = &debris->placements[i];
        if (p->type >= MAP_DEBRIS_TYPE_COUNT || !debris->modelLoaded[p->type]) continue;

        float dx = p->position.x - cameraPosition.x;
        float dz = p->position.z - cameraPosition.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > maxDistSq) continue;
        if (!Nature_IsChunkVisible(p->position, 0.35f)) continue;

        Model model = debris->models[p->type];
        float s = p->scale * ((p->type == MAP_DEBRIS_LEAF) ? 1.15f : 1.55f);
        Vector3 scale = {s, s, s};
        DrawModelEx(model, p->position, (Vector3){0.0f, 1.0f, 0.0f}, p->rotationDeg, scale, p->color);
    }

    rlEnableBackfaceCulling();
    Nature_EndWindReceiverShader();
}

void MapProp_DrawDebrisShadowCasters(const MapDebrisSystem *debris)
{
    if (!debris || !debris->ready) return;
}

void MapProp_UnloadDebrisSystem(MapDebrisSystem *debris)
{
    if (!debris || !debris->ready) return;
    for (int i = 0; i < MAP_DEBRIS_TYPE_COUNT; i++) {
        if (debris->modelLoaded[i]) {
            UnloadModel(debris->models[i]);
            debris->modelLoaded[i] = false;
        }
    }
    if (debris->placements) {
        MemFree(debris->placements);
        debris->placements = NULL;
    }
    debris->placementCount = 0;
    debris->ready = false;
}

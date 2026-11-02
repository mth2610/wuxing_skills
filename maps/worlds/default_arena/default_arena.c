#include "default_arena.h"
#include "maps/map_manager.h"
#include "environment/environment_system.h"
#include "environment/volumetric/volumetric_fog.h"
#include "maps/toolkit/ground_shadow.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const MapZone ARENA_ZONES[] = {
    { NAT_RIVER,       {  0.0f, 0.0f, -2.0f }, 3.0f },
    { NAT_FOREST,      { 14.0f, 0.0f, 10.0f }, 3.5f },
    { NAT_DESERT_ZONE, { -2.0f, 0.0f, 10.0f }, 3.0f },
};
#define ARENA_ZONE_COUNT (int)(sizeof(ARENA_ZONES) / sizeof(ARENA_ZONES[0]))

void InitDefaultArenaMap(void)
{
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);
    MapManager_SetZones(ARENA_ZONES, ARENA_ZONE_COUNT);
}

static inline Color ShadeArenaVertex(Color baseCol, float x, float z, bool waveActive)
{
    if (!waveActive) return baseCol;

    float eps = 0.30f;
    float hL = MapManager_GetGroundHeightAt(x - eps, z);
    float hR = MapManager_GetGroundHeightAt(x + eps, z);
    float hD = MapManager_GetGroundHeightAt(x, z - eps);
    float hU = MapManager_GetGroundHeightAt(x, z + eps);
    float dyX = (hR - hL) / (2.0f * eps);
    float dyZ = (hU - hD) / (2.0f * eps);

    float slopeSq = dyX * dyX + dyZ * dyZ;
    if (slopeSq < 1e-5f) return baseCol;

    float invLen = 1.0f / sqrtf(slopeSq + 1.0f);
    float nx = -dyX * invLen;
    float ny = invLen;
    float nz = -dyZ * invLen;

    Vector3 sunDir = Vector3Negate(Environment_GetSunDirection());
    float ndotl = nx * sunDir.x + ny * sunDir.y + nz * sunDir.z;

    float flatNdotL = fmaxf(sunDir.y, 0.2f);
    float ratio = (ndotl - flatNdotL) * 2.2f;
    float factor = 1.0f + ratio;
    if (factor < 0.35f) factor = 0.35f;
    if (factor > 2.20f) factor = 2.20f;

    int r = (int)(baseCol.r * factor);
    int g = (int)(baseCol.g * factor);
    int b = (int)(baseCol.b * factor);
    return (Color){
        (unsigned char)(r > 255 ? 255 : (r < 0 ? 0 : r)),
        (unsigned char)(g > 255 ? 255 : (g < 0 ? 0 : g)),
        (unsigned char)(b > 255 ? 255 : (b < 0 ? 0 : b)),
        baseCol.a
    };
}

// Radial-gradient ground disc, same self-shaded vertex-color technique as
// the floor plate below (lit materials read black-on-black in the night
// arena — see project memory). Opaque (alpha 255 rule).
static void DrawZoneDisc(Vector3 center, float radius, Color cCenter, Color cEdge)
{
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    GroundShadow_Begin(); // Real Shading P6 — receive the real shadow map (no-op if disabled)

    Vector4 od = {0}, wp = {0}, pr = {0};
    bool waveActive = MapManager_GetGroundWaveParams(&od, &wp, &pr);

    const int rings = 12;
    const int segments = 48;
    float r1 = radius * (1.0f / (float)rings);
    float t1 = 1.0f / (float)rings;
    Color cRing1 = ColorLerp(cCenter, cEdge, t1);
    float yCenter = center.y + 0.001f + MapManager_GetGroundHeightAt(center.x, center.z);
    Color colCenter = ShadeArenaVertex(cCenter, center.x, center.z, waveActive);

    rlCheckRenderBatchLimit(segments * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; i++)
    {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        float x1 = center.x + cosf(a1) * r1;
        float z1 = center.z + sinf(a1) * r1;
        float y1 = center.y + 0.001f + MapManager_GetGroundHeightAt(x1, z1);
        float x2 = center.x + cosf(a2) * r1;
        float z2 = center.z + sinf(a2) * r1;
        float y2 = center.y + 0.001f + MapManager_GetGroundHeightAt(x2, z2);

        Color col1 = ShadeArenaVertex(cRing1, x1, z1, waveActive);
        Color col2 = ShadeArenaVertex(cRing1, x2, z2, waveActive);

        rlColor4ub(colCenter.r, colCenter.g, colCenter.b, 255);
        rlVertex3f(center.x, yCenter, center.z);
        rlColor4ub(col2.r, col2.g, col2.b, 255);
        rlVertex3f(x2, y2, z2);
        rlColor4ub(col1.r, col1.g, col1.b, 255);
        rlVertex3f(x1, y1, z1);
    }
    rlEnd();

    for (int r = 1; r < rings; r++)
    {
        float rInner = radius * ((float)r / (float)rings);
        float rOuter = radius * ((float)(r + 1) / (float)rings);
        float tInner = (float)r / (float)rings;
        float tOuter = (float)(r + 1) / (float)rings;
        Color cIn  = ColorLerp(cCenter, cEdge, tInner);
        Color cOut = ColorLerp(cCenter, cEdge, tOuter);

        rlCheckRenderBatchLimit(segments * 6);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < segments; i++)
        {
            float a1 = ((float)i / segments) * 2.0f * PI;
            float a2 = ((float)(i + 1) / segments) * 2.0f * PI;

            float cosA1 = cosf(a1), sinA1 = sinf(a1);
            float cosA2 = cosf(a2), sinA2 = sinf(a2);

            float inX1 = center.x + cosA1 * rInner;
            float inZ1 = center.z + sinA1 * rInner;
            float inY1 = center.y + 0.001f + MapManager_GetGroundHeightAt(inX1, inZ1);

            float inX2 = center.x + cosA2 * rInner;
            float inZ2 = center.z + sinA2 * rInner;
            float inY2 = center.y + 0.001f + MapManager_GetGroundHeightAt(inX2, inZ2);

            float outX1 = center.x + cosA1 * rOuter;
            float outZ1 = center.z + sinA1 * rOuter;
            float outY1 = center.y + 0.001f + MapManager_GetGroundHeightAt(outX1, outZ1);

            float outX2 = center.x + cosA2 * rOuter;
            float outZ2 = center.z + sinA2 * rOuter;
            float outY2 = center.y + 0.001f + MapManager_GetGroundHeightAt(outX2, outZ2);

            Color colIn1 = ShadeArenaVertex(cIn, inX1, inZ1, waveActive);
            Color colIn2 = ShadeArenaVertex(cIn, inX2, inZ2, waveActive);
            Color colOut1 = ShadeArenaVertex(cOut, outX1, outZ1, waveActive);
            Color colOut2 = ShadeArenaVertex(cOut, outX2, outZ2, waveActive);

            rlColor4ub(colIn1.r, colIn1.g, colIn1.b, 255);
            rlVertex3f(inX1, inY1, inZ1);
            rlColor4ub(colOut2.r, colOut2.g, colOut2.b, 255);
            rlVertex3f(outX2, outY2, outZ2);
            rlColor4ub(colOut1.r, colOut1.g, colOut1.b, 255);
            rlVertex3f(outX1, outY1, outZ1);

            rlColor4ub(colIn1.r, colIn1.g, colIn1.b, 255);
            rlVertex3f(inX1, inY1, inZ1);
            rlColor4ub(colIn2.r, colIn2.g, colIn2.b, 255);
            rlVertex3f(inX2, inY2, inZ2);
            rlColor4ub(colOut2.r, colOut2.g, colOut2.b, 255);
            rlVertex3f(outX2, outY2, outZ2);
        }
        rlEnd();
    }

    GroundShadow_End();
    rlEnableBackfaceCulling();
}

void DrawDefaultArenaMap(void)
{
    // Draw the main floor plate with a radial lighting gradient
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    GroundShadow_Begin(); // Real Shading P6 — receive the real shadow map (no-op if disabled)

    Color cCenter = GetColor(0x3B3D4DFF); // Lighter blue-grey for center highlight (spotlight effect)
    Color cEdge = GetColor(0x131317FF);   // Darker tone at the edge of the arena
    float radius = 18.05f;
    Vector3 center = {6.0f, -0.0005f, 4.4f};

    Vector4 od = {0}, wp = {0}, pr = {0};
    bool waveActive = MapManager_GetGroundWaveParams(&od, &wp, &pr);

    const int rings = 36;
    const int segments = 96;

    // Center disc (fan from center to ring 1)
    float r1 = radius * (1.0f / (float)rings);
    float t1 = 1.0f / (float)rings;
    Color cRing1 = ColorLerp(cCenter, cEdge, t1);
    float yCenter = center.y + MapManager_GetGroundHeightAt(center.x, center.z);
    Color colCenter = ShadeArenaVertex(cCenter, center.x, center.z, waveActive);

    rlCheckRenderBatchLimit(segments * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; i++)
    {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;

        float x1 = center.x + cosf(a1) * r1;
        float z1 = center.z + sinf(a1) * r1;
        float y1 = center.y + MapManager_GetGroundHeightAt(x1, z1);

        float x2 = center.x + cosf(a2) * r1;
        float z2 = center.z + sinf(a2) * r1;
        float y2 = center.y + MapManager_GetGroundHeightAt(x2, z2);

        Color col1 = ShadeArenaVertex(cRing1, x1, z1, waveActive);
        Color col2 = ShadeArenaVertex(cRing1, x2, z2, waveActive);

        rlColor4ub(colCenter.r, colCenter.g, colCenter.b, colCenter.a);
        rlVertex3f(center.x, yCenter, center.z);

        rlColor4ub(col2.r, col2.g, col2.b, col2.a);
        rlVertex3f(x2, y2, z2);

        rlColor4ub(col1.r, col1.g, col1.b, col1.a);
        rlVertex3f(x1, y1, z1);
    }
    rlEnd();

    // Outer concentric rings (quads)
    for (int r = 1; r < rings; r++)
    {
        float rInner = radius * ((float)r / (float)rings);
        float rOuter = radius * ((float)(r + 1) / (float)rings);
        float tInner = (float)r / (float)rings;
        float tOuter = (float)(r + 1) / (float)rings;
        Color cIn  = ColorLerp(cCenter, cEdge, tInner);
        Color cOut = ColorLerp(cCenter, cEdge, tOuter);

        rlCheckRenderBatchLimit(segments * 6);
        rlBegin(RL_TRIANGLES);
        for (int i = 0; i < segments; i++)
        {
            float a1 = ((float)i / segments) * 2.0f * PI;
            float a2 = ((float)(i + 1) / segments) * 2.0f * PI;

            float cosA1 = cosf(a1), sinA1 = sinf(a1);
            float cosA2 = cosf(a2), sinA2 = sinf(a2);

            float inX1 = center.x + cosA1 * rInner;
            float inZ1 = center.z + sinA1 * rInner;
            float inY1 = center.y + MapManager_GetGroundHeightAt(inX1, inZ1);

            float inX2 = center.x + cosA2 * rInner;
            float inZ2 = center.z + sinA2 * rInner;
            float inY2 = center.y + MapManager_GetGroundHeightAt(inX2, inZ2);

            float outX1 = center.x + cosA1 * rOuter;
            float outZ1 = center.z + sinA1 * rOuter;
            float outY1 = center.y + MapManager_GetGroundHeightAt(outX1, outZ1);

            float outX2 = center.x + cosA2 * rOuter;
            float outZ2 = center.z + sinA2 * rOuter;
            float outY2 = center.y + MapManager_GetGroundHeightAt(outX2, outZ2);

            Color colIn1 = ShadeArenaVertex(cIn, inX1, inZ1, waveActive);
            Color colIn2 = ShadeArenaVertex(cIn, inX2, inZ2, waveActive);
            Color colOut1 = ShadeArenaVertex(cOut, outX1, outZ1, waveActive);
            Color colOut2 = ShadeArenaVertex(cOut, outX2, outZ2, waveActive);

            // Triangle 1: in1 -> out2 -> out1
            rlColor4ub(colIn1.r, colIn1.g, colIn1.b, colIn1.a);
            rlVertex3f(inX1, inY1, inZ1);

            rlColor4ub(colOut2.r, colOut2.g, colOut2.b, colOut2.a);
            rlVertex3f(outX2, outY2, outZ2);

            rlColor4ub(colOut1.r, colOut1.g, colOut1.b, colOut1.a);
            rlVertex3f(outX1, outY1, outZ1);

            // Triangle 2: in1 -> in2 -> out2
            rlColor4ub(colIn1.r, colIn1.g, colIn1.b, colIn1.a);
            rlVertex3f(inX1, inY1, inZ1);

            rlColor4ub(colIn2.r, colIn2.g, colIn2.b, colIn2.a);
            rlVertex3f(inX2, inY2, inZ2);

            rlColor4ub(colOut2.r, colOut2.g, colOut2.b, colOut2.a);
            rlVertex3f(outX2, outY2, outZ2);
        }
        rlEnd();
    }

    GroundShadow_End();
    rlEnableBackfaceCulling();

    // Draw grid
    rlBegin(RL_LINES);
    rlColor4ub(80, 80, 95, 100);
    int gridSlices = 60;
    float spacing = 60.0f;
    for (int i = -gridSlices; i <= gridSlices; i++)
    {
        rlVertex3f(center.x + i * spacing, 0.0f, center.z - gridSlices * spacing);
        rlVertex3f(center.x + i * spacing, 0.0f, center.z + gridSlices * spacing);

        rlVertex3f(center.x - gridSlices * spacing, 0.0f, center.z + i * spacing);
        rlVertex3f(center.x + gridSlices * spacing, 0.0f, center.z + i * spacing);
    }
    rlEnd();
}

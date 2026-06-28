#include "meadow_night.h"
#include "raylib.h"
#include "rlgl.h"
#include "../../environment/environment_system.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// To animate the lake
static float lakeTime = 0.0f;

void InitMeadowNightMap(void) {
    // Set night time lighting
    Environment_SetAmbientColor((Color){ 15, 20, 35, 255 }); // Deep dark blue
    Environment_SetSunColor((Color){ 40, 60, 100, 255 });    // Pale moonlight
    Environment_SetSunDirection((Vector3){ -0.5f, -0.6f, 0.8f }); // Moon shining from distance
    Environment_SetShadowColor((Color){ 5, 8, 15, 180 });

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){ 10, 15, 25, 255 }; // Match ambient
    fog.start = 800.0f;
    fog.end = 2200.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);
}

void DrawMeadowNightMap(void) {
    Vector3 center = { 600.0f, 0.0f, 440.0f };
    float radius = 1805.0f;
    float lakeRadius = 350.0f;

    // Draw Grass Base
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    Color cGrassCenter = GetColor(0x2d4c2bff); // Muted green
    Color cGrassEdge = GetColor(0x132314ff);   // Dark green at edges
    int segments = 64;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y - 0.1f, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y - 0.1f, center.z + sinf(a2) * radius };
        
        rlColor4ub(cGrassCenter.r, cGrassCenter.g, cGrassCenter.b, cGrassCenter.a);
        rlVertex3f(center.x, center.y - 0.1f, center.z);
        rlColor4ub(cGrassEdge.r, cGrassEdge.g, cGrassEdge.b, cGrassEdge.a);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cGrassEdge.r, cGrassEdge.g, cGrassEdge.b, cGrassEdge.a);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();

    // Draw Lake (slightly wavy cyan/blue)
    rlBegin(RL_TRIANGLES);
    Color cLakeCenter = GetColor(0x3a7c8aff); // Moon reflection color
    Color cLakeEdge = GetColor(0x1a3a4aff);
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        // animate lake edge slightly
        float wave1 = sinf(a1 * 8.0f + lakeTime) * 10.0f;
        float wave2 = sinf(a2 * 8.0f + lakeTime) * 10.0f;
        Vector3 p1 = { center.x + cosf(a1) * (lakeRadius + wave1), center.y, center.z + sinf(a1) * (lakeRadius + wave1) };
        Vector3 p2 = { center.x + cosf(a2) * (lakeRadius + wave2), center.y, center.z + sinf(a2) * (lakeRadius + wave2) };
        
        rlColor4ub(cLakeCenter.r, cLakeCenter.g, cLakeCenter.b, 255);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4ub(cLakeEdge.r, cLakeEdge.g, cLakeEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cLakeEdge.r, cLakeEdge.g, cLakeEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();

    // Draw scattered flowers / obstacles
    Vector3 flowerPositions[] = {
        { center.x + 400.0f, center.y, center.z + 200.0f },
        { center.x - 450.0f, center.y, center.z - 100.0f },
        { center.x + 100.0f, center.y, center.z - 450.0f },
        { center.x - 200.0f, center.y, center.z + 500.0f },
        { center.x + 500.0f, center.y, center.z - 300.0f }
    };

    for (int i = 0; i < 5; i++) {
        Environment_DrawSmartShadow(flowerPositions[i], ENV_SHAPE_CYLINDER, 15.0f, 30.0f);
        DrawCylinder(flowerPositions[i], 15.0f, 10.0f, 30.0f, 8, GetColor(0x8a3a5aff)); // Pink/Purple flower bush
        DrawCylinder((Vector3){flowerPositions[i].x, flowerPositions[i].y + 30.0f, flowerPositions[i].z}, 10.0f, 0.0f, 15.0f, 8, GetColor(0xaa5a7aff));
    }

    // Draw Distant Moon
    // Camera is at (x, y+500, z+400). It looks towards -Z. 
    // We place the moon deep in -Z, slightly offset in X so it's not dead center.
    Vector3 moonPos = { center.x - 500.0f, 250.0f, center.z - 1100.0f };
    
    // Temporarily disable depth testing for the moon so it acts like a background element
    // but we need to draw it after everything or before everything. Since it's far, drawing it here is fine.
    Color moonColor = GetColor(0xfcfadeff); // Pale yellow/white
    DrawSphere(moonPos, 120.0f, moonColor);
}

void UpdateMeadowNightMap(float dt) {
    lakeTime += dt * 2.0f;
}

void UnloadMeadowNightMap(void) {
    // Nothing to unload
}

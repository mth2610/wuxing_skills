#include "default_arena.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

void InitDefaultArenaMap(void) {
    // Map setup if any
}

void DrawDefaultArenaMap(void) {
    // Draw the main floor plate with a radial lighting gradient
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    rlBegin(RL_TRIANGLES);
    Color cCenter = GetColor(0x3B3D4DFF); // Lighter blue-grey for center highlight (spotlight effect)
    Color cEdge = GetColor(0x131317FF);   // Darker tone at the edge of the arena
    float radius = 1805.0f;
    Vector3 center = { 600.0f, -0.05f, 440.0f };
    int segments = 128;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y, center.z + sinf(a2) * radius };
        
        // Center vertex (lit/lighter)
        rlColor4ub(cCenter.r, cCenter.g, cCenter.b, cCenter.a);
        rlVertex3f(center.x, center.y, center.z);
        
        // Outer vertices (darker/vignette)
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
        rlVertex3f(p2.x, p2.y, p2.z);
        
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, cEdge.a);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();

    // Draw grid
    rlBegin(RL_LINES);
    rlColor4ub(80, 80, 95, 100);
    int gridSlices = 60;
    float spacing = 60.0f;
    for (int i = -gridSlices; i <= gridSlices; i++) {
        rlVertex3f(center.x + i*spacing, 0.0f, center.z - gridSlices*spacing);
        rlVertex3f(center.x + i*spacing, 0.0f, center.z + gridSlices*spacing);
        
        rlVertex3f(center.x - gridSlices*spacing, 0.0f, center.z + i*spacing);
        rlVertex3f(center.x + gridSlices*spacing, 0.0f, center.z + i*spacing);
    }
    rlEnd();
}

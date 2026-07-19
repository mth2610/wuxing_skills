#include "default_arena.h"
#include "core/map_manager.h"
#include "maps/toolkit/ground_shadow.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Virtual Trigger Zones (MODULES_ROADMAP.md Module 2) — pure data; the
// gameplay modifier rules live in game/. Each zone gets a matching visual
// cue in DrawDefaultArenaMap (No Tutorial — the player reads the ground).
static const MapZone ARENA_ZONES[] = {
    { NAT_RIVER,       {  0.0f, 0.0f, -2.0f }, 3.0f },
    { NAT_FOREST,      { 14.0f, 0.0f, 10.0f }, 3.5f },
    { NAT_DESERT_ZONE, { -2.0f, 0.0f, 10.0f }, 3.0f },
};
#define ARENA_ZONE_COUNT (int)(sizeof(ARENA_ZONES) / sizeof(ARENA_ZONES[0]))

void InitDefaultArenaMap(void) {
    MapManager_SetZones(ARENA_ZONES, ARENA_ZONE_COUNT);
}

// Radial-gradient ground disc, same self-shaded vertex-color technique as
// the floor plate below (lit materials read black-on-black in the night
// arena — see project memory). Opaque (alpha 255 rule).
static void DrawZoneDisc(Vector3 center, float radius, Color cCenter, Color cEdge) {
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    GroundShadow_Begin(); // Real Shading P6 — receive the real shadow map (no-op if disabled)
    rlBegin(RL_TRIANGLES);
    int segments = 48;
    float y = center.y + 0.001f; // just above the floor plate
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, y, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, y, center.z + sinf(a2) * radius };
        rlColor4ub(cCenter.r, cCenter.g, cCenter.b, 255);
        rlVertex3f(center.x, y, center.z);
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    GroundShadow_End();
    rlEnableBackfaceCulling();
}

void DrawDefaultArenaMap(void) {
    // Draw the main floor plate with a radial lighting gradient
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    GroundShadow_Begin(); // Real Shading P6 — receive the real shadow map (no-op if disabled)
    rlBegin(RL_TRIANGLES);
    Color cCenter = GetColor(0x3B3D4DFF); // Lighter blue-grey for center highlight (spotlight effect)
    Color cEdge = GetColor(0x131317FF);   // Darker tone at the edge of the arena
    float radius = 18.05f;
    Vector3 center = { 6.0f, -0.0005f, 4.4f };
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
    GroundShadow_End();
    rlEnableBackfaceCulling();

    // Nature-zone visual cues: muted gradient discs blending into the dark
    // floor — river (deep blue), forest (deep green), desert (dry ochre).
    Color floorDark = GetColor(0x131317FF);
    DrawZoneDisc(ARENA_ZONES[0].center, ARENA_ZONES[0].radius, GetColor(0x1E4A66FF), floorDark);
    DrawZoneDisc(ARENA_ZONES[1].center, ARENA_ZONES[1].radius, GetColor(0x1E4A32FF), floorDark);
    DrawZoneDisc(ARENA_ZONES[2].center, ARENA_ZONES[2].radius, GetColor(0x4A3A22FF), floorDark);

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

#include "core/map_manager.h"
#include <string.h>

#define MAX_MAPS 16

static MapDefinition s_maps[MAX_MAPS];
static int s_mapCount = 0;
static int s_activeMapIndex = 0;

// Virtual Trigger Zones of the ACTIVE map only — repopulated by the map's
// Init (via MapManager_SetZones), cleared on every switch/init/unload.
static MapZone s_zones[MAX_MAP_ZONES];
static int s_zoneCount = 0;

#include "core/maps_generated.h"

void MapManager_Init(void) {
    s_mapCount = 0;
    s_activeMapIndex = 0;
    s_zoneCount = 0;

    RegisterGeneratedMaps();

    // Default map: DEFAULT_ARENA. (Previously searched for "BAMBOO_VALLEY",
    // a map deleted in an earlier session — that left the default silently
    // falling back to whatever map the registry-generator scanned first,
    // which changes any time a new map/ directory is added. Pinning to a
    // map that actually exists keeps the default stable across additions.)
    if (s_mapCount > 0) {
        for (int i = 0; i < s_mapCount; i++) {
            if (strcmp(s_maps[i].name, "DEFAULT_ARENA") == 0) {
                s_activeMapIndex = i;
                break;
            }
        }
        
        if (s_maps[s_activeMapIndex].Init) {
            s_maps[s_activeMapIndex].Init();
        }
    }
}

void MapManager_Register(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void), void (*unload)(void)) {
    MapManager_RegisterEx(name, init, update, draw, unload, NULL);
}

void MapManager_RegisterEx(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void),
                           void (*unload)(void), float (*getGroundHeight)(float x, float z)) {
    if (s_mapCount >= MAX_MAPS) return;
    s_maps[s_mapCount++] = (MapDefinition){
        .name = name,
        .Init = init,
        .Update = update,
        .Draw = draw,
        .Unload = unload,
        .GetGroundHeight = getGroundHeight
    };
}

float MapManager_GetGroundHeightAt(float x, float z) {
    if (s_mapCount == 0) return 0.0f;
    float (*fn)(float, float) = s_maps[s_activeMapIndex].GetGroundHeight;
    return fn ? fn(x, z) : 0.0f;
}

void MapManager_Update(float dt) {
    if (s_mapCount == 0) return;
    if (s_maps[s_activeMapIndex].Update) {
        s_maps[s_activeMapIndex].Update(dt);
    }
}

void MapManager_DrawActive(void) {
    if (s_mapCount == 0) return;
    if (s_maps[s_activeMapIndex].Draw) {
        s_maps[s_activeMapIndex].Draw();
    }
}

void MapManager_Unload(void) {
    for (int i = 0; i < s_mapCount; i++) {
        if (s_maps[i].Unload) s_maps[i].Unload();
    }
    s_mapCount = 0;
    s_zoneCount = 0;
}

int MapManager_GetCount(void) {
    return s_mapCount;
}

const char* MapManager_GetName(int index) {
    if (index < 0 || index >= s_mapCount) return "Unknown";
    return s_maps[index].name;
}

int MapManager_GetActiveIndex(void) {
    return s_activeMapIndex;
}

void MapManager_SetActiveIndex(int index) {
    if (index >= 0 && index < s_mapCount) {
        s_activeMapIndex = index;
        // Clear the previous map's zones BEFORE Init so a zone-less map ends
        // up with zero zones instead of inheriting stale ones.
        s_zoneCount = 0;
        if (s_maps[s_activeMapIndex].Init) {
            s_maps[s_activeMapIndex].Init();
        }
    }
}

void MapManager_SetZones(const MapZone *zones, int count) {
    if (zones == NULL || count <= 0) { s_zoneCount = 0; return; }
    if (count > MAX_MAP_ZONES) count = MAX_MAP_ZONES;
    for (int i = 0; i < count; i++) s_zones[i] = zones[i];
    s_zoneCount = count;
}

int Map_GetZoneCount(void) {
    return s_zoneCount;
}

const MapZone *Map_GetZone(int index) {
    if (index < 0 || index >= s_zoneCount) return NULL;
    return &s_zones[index];
}

NatureZoneType Map_QueryZoneAt(Vector3 pos) {
    for (int i = 0; i < s_zoneCount; i++) {
        float dx = pos.x - s_zones[i].center.x;
        float dz = pos.z - s_zones[i].center.z;
        if (dx * dx + dz * dz <= s_zones[i].radius * s_zones[i].radius) {
            return s_zones[i].type;
        }
    }
    return NAT_NONE;
}

void MapManager_DebugDrawZones(void) {
    for (int i = 0; i < s_zoneCount; i++) {
        Color c;
        switch (s_zones[i].type) {
            case NAT_RIVER:       c = (Color){  60, 170, 230, 255 }; break;
            case NAT_FOREST:      c = (Color){  70, 200, 110, 255 }; break;
            case NAT_DESERT_ZONE: c = (Color){ 220, 170,  70, 255 }; break;
            default:              c = (Color){ 200, 200, 200, 255 }; break;
        }
        Vector3 p = s_zones[i].center;
        p.y += 0.02f; // lift off the floor plate to avoid z-fighting
        DrawCircle3D(p, s_zones[i].radius, (Vector3){ 1, 0, 0 }, 90.0f, c);
    }
}

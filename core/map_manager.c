#include "core/map_manager.h"
#include "environment/environment_system.h"
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
    Environment_SetSunIntensity(1.0f);

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
    MapManager_RegisterEx(name, init, update, draw, unload, NULL, NULL, NULL);
}

void MapManager_RegisterEx(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void),
                           void (*unload)(void), float (*getGroundHeight)(float x, float z),
                           MapGroundSurfaceSampleFn sampleGroundSurface,
                           void (*drawTransparent)(void)) {
    if (s_mapCount >= MAX_MAPS) return;
    s_maps[s_mapCount++] = (MapDefinition){
        .name = name,
        .Init = init,
        .Update = update,
        .Draw = draw,
        .Unload = unload,
        .GetGroundHeight = getGroundHeight,
        .SampleGroundSurface = sampleGroundSurface,
        .DrawTransparent = drawTransparent
    };
}

void MapManager_RegisterWaterHooks(const char *name,
                                   bool (*getWaterInfo)(float x, float z, float *outSurfaceY, float *outWaterDepth),
                                   void (*setWaterInteractor)(Vector3, Vector3, float),
                                   void (*addWaterRipple)(Vector3, float, float)) {
    for (int i = 0; i < s_mapCount; i++) {
        if (strcmp(s_maps[i].name, name) == 0) {
            s_maps[i].GetWaterInfo = getWaterInfo;
            s_maps[i].SetWaterInteractor = setWaterInteractor;
            s_maps[i].AddWaterRipple = addWaterRipple;
            return;
        }
    }
}

bool MapManager_GetWaterInfoAt(float x, float z, float *outSurfaceY, float *outWaterDepth) {
    if (s_mapCount == 0) return false;
    bool (*fn)(float, float, float*, float*) = s_maps[s_activeMapIndex].GetWaterInfo;
    return fn ? fn(x, z, outSurfaceY, outWaterDepth) : false;
}

void MapManager_SetWaterInteractor(Vector3 position, Vector3 velocity, float radius) {
    if (s_mapCount == 0) return;
    void (*fn)(Vector3, Vector3, float) = s_maps[s_activeMapIndex].SetWaterInteractor;
    if (fn) fn(position, velocity, radius);
}

void MapManager_AddWaterRipple(Vector3 position, float radius, float intensity) {
    if (s_mapCount == 0) return;
    void (*fn)(Vector3, float, float) = s_maps[s_activeMapIndex].AddWaterRipple;
    if (fn) fn(position, radius, intensity);
}

bool MapManager_SampleGroundSurfaceAt(float x, float z, Vector3 *outPosition, Vector3 *outNormal) {
    if (outPosition) *outPosition = (Vector3){x, MapManager_GetGroundHeightAt(x, z), z};
    if (outNormal) *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
    if (s_mapCount == 0) return false;
    MapGroundSurfaceSampleFn fn = s_maps[s_activeMapIndex].SampleGroundSurface;
    return fn ? fn(x, z, outPosition, outNormal) : false;
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

void MapManager_DrawTransparent(void) {
    if (s_mapCount == 0) return;
    if (s_maps[s_activeMapIndex].DrawTransparent) {
        s_maps[s_activeMapIndex].DrawTransparent();
    }
}

void MapManager_Unload(void) {
    for (int i = 0; i < s_mapCount; i++) {
        if (s_maps[i].Unload) s_maps[i].Unload();
    }
    s_mapCount = 0;
    s_zoneCount = 0;
    Environment_SetSunIntensity(1.0f);
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
        // Maps remain loaded across switches; reset opt-in direct radiance
        // before Init so legacy maps cannot inherit another map's HDR sun.
        Environment_SetSunIntensity(1.0f);
        if (s_maps[s_activeMapIndex].Init) {
            s_maps[s_activeMapIndex].Init();
        }
    }
}

Vector3 MapManager_GetActiveSpawnPoint(void) {
    if (s_mapCount == 0) return (Vector3){ 6.0f, 0.0f, 4.4f };
    const char *name = s_maps[s_activeMapIndex].name;
    Vector3 pt;
    if (strcmp(name, "SONG_QUAO") == 0) {
        // Song Quao island center is (94.15f, 150.0f). Spawn near the main meadow plateau.
        pt = (Vector3){ 94.15f, 0.0f, 150.0f };
    } else if (strcmp(name, "VERDANT_PATH") == 0) {
        // Verdant Path central meadow clearing / path fork
        pt = (Vector3){ 46.0f, 0.0f, 37.5f };
    } else {
        // DEFAULT_ARENA and default fallback
        pt = (Vector3){ 6.0f, 0.0f, 4.4f };
    }
    pt.y = MapManager_GetGroundHeightAt(pt.x, pt.z);
    return pt;
}

void MapManager_GetActiveBounds(Vector3 *outCenter, float *outRadius) {
    if (s_mapCount == 0) {
        if (outCenter) *outCenter = (Vector3){ 6.0f, 0.0f, 4.4f };
        if (outRadius) *outRadius = 18.0f;
        return;
    }
    const char *name = s_maps[s_activeMapIndex].name;
    if (strcmp(name, "SONG_QUAO") == 0) {
        if (outCenter) *outCenter = (Vector3){ 94.15f, 0.0f, 150.0f };
        if (outRadius) *outRadius = 135.0f;
    } else if (strcmp(name, "VERDANT_PATH") == 0) {
        if (outCenter) *outCenter = (Vector3){ 50.0f, 0.0f, 37.5f };
        if (outRadius) *outRadius = 34.0f;
    } else {
        if (outCenter) *outCenter = (Vector3){ 6.0f, 0.0f, 4.4f };
        if (outRadius) *outRadius = 18.0f;
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

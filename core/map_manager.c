#include "core/map_manager.h"
#include <string.h>

#define MAX_MAPS 16

static MapDefinition s_maps[MAX_MAPS];
static int s_mapCount = 0;
static int s_activeMapIndex = 0;

#include "core/maps_generated.h"

void MapManager_Init(void) {
    s_mapCount = 0;
    s_activeMapIndex = 0;
    
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
        if (s_maps[s_activeMapIndex].Init) {
            s_maps[s_activeMapIndex].Init();
        }
    }
}

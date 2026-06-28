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

    // Khởi tạo map mặc định
    if (s_mapCount > 0 && s_maps[0].Init) {
        s_maps[0].Init();
    }
}

void MapManager_Register(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void), void (*unload)(void)) {
    if (s_mapCount >= MAX_MAPS) return;
    s_maps[s_mapCount++] = (MapDefinition){
        .name = name,
        .Init = init,
        .Update = update,
        .Draw = draw,
        .Unload = unload
    };
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

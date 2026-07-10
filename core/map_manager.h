#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include "raylib.h"

typedef struct {
    const char* name;
    void (*Init)(void);
    void (*Update)(float dt);
    void (*Draw)(void);
    void (*Unload)(void);
    float (*GetGroundHeight)(float x, float z); // optional, NULL = flat (Y=0)
} MapDefinition;

void MapManager_Init(void);
void MapManager_Register(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void), void (*unload)(void));
// Same as MapManager_Register but with the optional GetGroundHeight(x,z)
// hook (returns absolute world Y at that XZ — see MapProp_SampleGroundHeight
// for the toolkit helper a heightmap map wraps to implement this).
// getGroundHeight may be NULL (flat Y=0, same as MapManager_Register).
void MapManager_RegisterEx(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void),
                           void (*unload)(void), float (*getGroundHeight)(float x, float z));
void MapManager_Update(float dt);
void MapManager_DrawActive(void);
void MapManager_Unload(void);

// Absolute world-space ground Y at (x,z) on the currently active map. 0.0f
// (this project's flat-ground convention) if the active map has no
// GetGroundHeight hook registered.
float MapManager_GetGroundHeightAt(float x, float z);

int MapManager_GetCount(void);
const char* MapManager_GetName(int index);
int MapManager_GetActiveIndex(void);
void MapManager_SetActiveIndex(int index);

#endif // MAP_MANAGER_H

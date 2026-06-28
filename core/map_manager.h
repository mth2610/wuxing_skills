#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include "raylib.h"

typedef struct {
    const char* name;
    void (*Init)(void);
    void (*Update)(float dt);
    void (*Draw)(void);
    void (*Unload)(void);
} MapDefinition;

void MapManager_Init(void);
void MapManager_Register(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void), void (*unload)(void));
void MapManager_Update(float dt);
void MapManager_DrawActive(void);
void MapManager_Unload(void);

int MapManager_GetCount(void);
const char* MapManager_GetName(int index);
int MapManager_GetActiveIndex(void);
void MapManager_SetActiveIndex(int index);

#endif // MAP_MANAGER_H

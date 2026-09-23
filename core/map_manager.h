#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include "raylib.h"

typedef bool (*MapGroundSurfaceSampleFn)(float x, float z, Vector3 *outPosition, Vector3 *outNormal);

typedef struct {
    const char* name;
    void (*Init)(void);
    void (*Update)(float dt);
    void (*Draw)(void);
    void (*Unload)(void);
    float (*GetGroundHeight)(float x, float z); // optional, NULL = flat (Y=0)
    MapGroundSurfaceSampleFn SampleGroundSurface; // optional exact mesh receiver
    void (*DrawTransparent)(void);              // optional transparent pass (water, glass)
    bool (*GetWaterInfo)(float x, float z, float *outSurfaceY, float *outWaterDepth); // optional shallow water info
    void (*SetWaterInteractor)(Vector3 position, Vector3 velocity, float radius);     // optional dynamic wake/ripples
    void (*AddWaterRipple)(Vector3 position, float radius, float intensity);          // optional expanding shockwave ring
} MapDefinition;

void MapManager_Init(void);
void MapManager_Register(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void), void (*unload)(void));
// Same as MapManager_Register but with optional GetGroundHeight(x,z), surface sampler,
// and transparent overlay pass (drawTransparent).
void MapManager_RegisterEx(const char* name, void (*init)(void), void (*update)(float), void (*draw)(void),
                           void (*unload)(void), float (*getGroundHeight)(float x, float z),
                           MapGroundSurfaceSampleFn sampleGroundSurface,
                           void (*drawTransparent)(void));
void MapManager_RegisterWaterHooks(const char *name,
                                   bool (*getWaterInfo)(float x, float z, float *outSurfaceY, float *outWaterDepth),
                                   void (*setWaterInteractor)(Vector3, Vector3, float),
                                   void (*addWaterRipple)(Vector3, float, float));
void MapManager_Update(float dt);
void MapManager_DrawActive(void);
void MapManager_DrawTransparent(void);
void MapManager_Unload(void);

// Absolute world-space ground Y at (x,z) on the currently active map. 0.0f
// (this project's flat-ground convention) if the active map has no
// GetGroundHeight hook registered.
float MapManager_GetGroundHeightAt(float x, float z);
bool MapManager_SampleGroundSurfaceAt(float x, float z, Vector3 *outPosition, Vector3 *outNormal);

// Queries the active map for shallow water at (x, z).
// Returns true if (x, z) is in a water body, writing the water surface Y to outSurfaceY
// and the physical water depth (surfaceY - bedY) to outWaterDepth.
bool MapManager_GetWaterInfoAt(float x, float z, float *outSurfaceY, float *outWaterDepth);

// Passes an interactor (e.g. player) to the active map's water system to generate dynamic wakes and ripples.
void MapManager_SetWaterInteractor(Vector3 position, Vector3 velocity, float radius);

// Adds an expanding circular ripple impulse (e.g. footstep, landing, jump splash) to the active map.
void MapManager_AddWaterRipple(Vector3 position, float radius, float intensity);

int MapManager_GetCount(void);
const char* MapManager_GetName(int index);
int MapManager_GetActiveIndex(void);
void MapManager_SetActiveIndex(int index);

// --- Virtual Trigger Zones (MODULES_ROADMAP.md Module 2) ---
// Map = pure data: a map declares WHERE its nature zones are; the gameplay
// modifier rules (Thủy -50% cooldown in a river, etc.) live in the consumer
// (game/entities), never here.
typedef enum { NAT_NONE = 0, NAT_RIVER, NAT_FOREST, NAT_DESERT_ZONE } NatureZoneType;

typedef struct {
    NatureZoneType type;
    Vector3 center;   // on the floor, y = 0
    float   radius;   // XZ-plane distance check, same convention as
                      // Entity_GetNearbyTargets / ring-out
} MapZone;

#define MAX_MAP_ZONES 16

// Called from a map's Init to declare its zones (copied into the manager).
// Zones are cleared automatically on map switch, so a map with no zones
// simply never calls this. count > MAX_MAP_ZONES is clamped.
void MapManager_SetZones(const MapZone *zones, int count);

// Zones of the currently active map.
int            Map_GetZoneCount(void);
const MapZone *Map_GetZone(int index);          // NULL if index out of range
NatureZoneType Map_QueryZoneAt(Vector3 pos);    // NAT_NONE outside every zone

// Debug visualization: one ground ring per zone, color-coded by type
// (river cyan-blue, forest green, desert ochre). Call inside BeginMode3D.
void MapManager_DebugDrawZones(void);

#endif // MAP_MANAGER_H

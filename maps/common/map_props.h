#ifndef MAP_PROPS_H
#define MAP_PROPS_H

#include "raylib.h"
#include <stdbool.h>

// Reusable map-building blocks, shared across every map/ so a new map is
// "call these with different numbers" instead of copy-pasting mesh/texture
// setup. Follows MAP_API.md's "load once (Init), draw many (Draw)"
// convention — every Create* does the one-time work, every Draw* is cheap
// to call every frame.
//
// Add new prop kinds here as maps need them (bush, flower patch, tree,
// water pool, ...) rather than inlining setup code in a map's own .c file.
//
// Ground/strip use raylib's plain default material (no custom shader) —
// deliberately the simplest possible textured-surface path. Custom lit
// materials (core/prop_lit.h, core/grass_material.h) are still available
// and fine for props that need them (see MapRockSet, which uses prop_lit);
// ground specifically defaults to plain texturing here after repeated
// visual issues with shaded ground (see CORE_ISSUES.md Item 38) — swap in
// a custom Material after MapProp_Create* returns if a specific map wants
// something fancier once that's sorted out.

// --- Ground plane -------------------------------------------------------

typedef struct
{
    Model model;
    bool ready;
} MapGroundSurface;

// width/depth in world meters. tileSize = meters per texture repeat
// (e.g. 1.5f means the diffuse texture repeats every 1.5m).
MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize, const char *splatMapPath, const char *grassTexPath, const char *pathTexPath);
void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter);
void MapProp_UnloadGround(MapGroundSurface *ground);

// --- Flat strip (stone path, road, bridge deck, ...) --------------------

typedef struct
{
    Model model;
    bool ready;
} MapStripSurface;

// length = extent along the strip's own long axis, width = across it.
// normalPath/roughnessPath: pass NULL for both to use a plain-textured
// strip; pass all 3 paths for a full prop_lit strip (matches MapRockSet).
MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath);
void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset);
void MapProp_UnloadStrip(MapStripSurface *strip);

// --- Rock props (one mesh/texture set, many placements) -----------------

typedef struct
{
    Model model;
    bool ready;
} MapRockSet;

typedef struct
{
    Vector3 position;  // world X/Z; Y ignored — rocks auto-sink for a half-buried look
    float radiusScale; // XZ scale of the unit rock sphere
    float heightScale; // Y scale — flatten for a boulder silhouette
    float rotationDeg;
} MapRockPlacement;

// diffusePath/normalPath/roughnessPath: pass NULL for normal/roughness to
// skip PBR shading and use a plain-textured rock instead (matches
// MapProp_CreateGround's simplicity). Pass all 3 for a full prop_lit rock.
MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath);
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count);
void MapProp_UnloadRocks(MapRockSet *rocks);

#endif // MAP_PROPS_H

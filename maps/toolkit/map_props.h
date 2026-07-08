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
// Ground uses a dedicated splatmap shader (maps/toolkit/shaders/ground_splat.fs,
// see MapProp_CreateGround) blending a grass + path texture by sampling a
// splatmap's red channel — WIP, tune splatMapPath/the shader together.
// Strip uses its own noise-feathered-edge shader (maps/toolkit/shaders/
// path_blend.fs, see MapProp_CreateStrip) — normalPath/roughnessPath are
// currently accepted but unused (kept in the signature for a future prop_lit
// strip variant). Rocks use prop_lit (maps/toolkit/prop_lit.h) when
// normal+roughness paths are given, otherwise a plain-textured material.
// maps/toolkit/grass_material.h is a shelved alternative ground material
// (CORE_ISSUES.md Item 38) — swap in a custom Material after MapProp_Create*
// returns if a map wants something different.
//
// Implementation is split one .inl per prop kind (map_props_ground.inl,
// map_props_strip.inl, map_props_rocks.inl), #include'd from map_props.c —
// add a new prop kind's own .inl there, not inline in map_props.c itself.

// --- Ground plane -------------------------------------------------------

typedef struct
{
    Model model;
    Vector3 drawOffset; // added to worldCenter in MapProp_DrawGround — lets
                        // Create* pick where the model's local origin sits
                        // without ever mutating the mesh's own vertex data
    bool ready;
} MapGroundSurface;

// width/depth in world meters. tileSize = meters per texture repeat
// (e.g. 1.5f means the diffuse texture repeats every 1.5m).
MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize, const char *splatMapPath, const char *grassTexPath, const char *pathTexPath);

// Sloped-island variant of MapProp_CreateGround, for the floating-island
// motif (MAP_API.md): heightmapPath is a grayscale image, WHITE = flat
// walkable plateau, BLACK = cliff edge. cliffDepth = how many meters the
// black edge sinks below the plateau (plateau is always at local Y=0,
// matching every other prop kind's ground-level convention) — keep this
// LESS than the y offset passed to MapProp_DrawCloudSea, so the cliff never
// pokes through the cloud plane below it. Same splatMapPath/grassTexPath/
// pathTexPath + shader as MapProp_CreateGround — only the mesh source
// differs. Draw/Unload with the same MapProp_DrawGround/UnloadGround.
MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                               float cliffDepth, float tileSize,
                                               const char *splatMapPath,
                                               const char *grassTexPath,
                                               const char *pathTexPath);
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
// drawShadow: pass false for large border/mountain-ring rocks — dozens of
// giant fake-shadow decals stacked/overlapping is real alpha overdraw
// (measured FPS cost, not theoretical) for a case where the shadow barely
// reads anyway. Pass true for normal scattered decorative rocks.
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow);
void MapProp_UnloadRocks(MapRockSet *rocks);

// Fills outPlacements[] (capacity maxCount) with a ring of giant rock
// placements around a mapWidth x mapDepth rectangle's border — the
// "floating island ringed by mountains" motif every map uses. Draw the
// result with the SAME MapProp_DrawRocks/MapRockSet used for scattered
// rocks (just much bigger radius/height scale) — no separate prop kind
// needed. seed makes the layout reproducible (same seed -> same ring).
// Returns maxCount (kept as an int return for a future early-out case).
int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                 float mapWidth, float mapDepth,
                                 float minRadiusScale, float maxRadiusScale,
                                 float minHeightScale, float maxHeightScale,
                                 unsigned int seed);

// --- Sea of clouds (floor of the void below a floating-island map) ------

typedef struct
{
    Model model;
    bool ready;
} MapCloudSea;

// width/depth in world meters (make this bigger than the map itself so it
// reads as an endless sea extending past the mountain ring). tileSize
// controls the noise frequency, not a texture repeat (this shader is
// texture-free, pure procedural FBM) — smaller tileSize = smaller/denser
// cloud puffs. Draw well below the ground (yOffset negative, e.g. -12.0f).
//
// Uses a discard-based opaque cutout, never partial alpha — see
// maps/toolkit/shaders/cloud_sea.fs and maps/CLAUDE.md's Alpha = 255 rule.
MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize);
void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset);
void MapProp_UnloadCloudSea(MapCloudSea *cloud);

#endif // MAP_PROPS_H

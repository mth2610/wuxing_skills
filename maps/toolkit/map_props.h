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
// enable tangent-space surface lighting when both are supplied. Rocks use
// prop_lit (maps/toolkit/prop_lit.h) when
// normal+roughness paths are given, otherwise a plain-textured material.
// maps/toolkit/grass_material.h is a shelved alternative ground material
// (CORE_ISSUES.md Item 38) — swap in a custom Material after MapProp_Create*
// returns if a map wants something different.
//
// Implementation is split one .inl per prop kind (map_props_ground.inl,
// map_props_strip.inl, map_props_rocks.inl), #include'd from map_props.c —
// add a new prop kind's own .inl there, not inline in map_props.c itself.

// --- Ground plane -------------------------------------------------------

// XZ bin grid over the ground mesh's triangles, so a height query touches the
// handful of triangles that can actually contain (x,z) instead of all of them.
// It is a pure ACCELERATOR: it indexes the same triangle array
// GetRayCollisionMesh would walk, and answers from the same vertex data, so it
// cannot reintroduce the class of bug the SampleGroundHeight comment below
// warns about. Nothing here is owned by the caller; MapProp_UnloadGround frees
// it. Built by MapProp_Create*; a surface whose grid failed to build silently
// falls back to the raycast.
typedef struct
{
    bool  built;
    int   nx, nz;                  // bins along local X / Z
    float minX, minZ;              // local-space origin of the grid
    float invCellX, invCellZ;      // 1 / bin size
    int   triCount;
    int  *cellStart;               // nx*nz + 1 CSR offsets
    int  *cellItems;               // triangle indices, binned
    const float *verts;            // borrowed from Mesh.vertices
    const unsigned short *indices; // borrowed from Mesh.indices, may be NULL
} MapGroundLookup;

typedef struct
{
    Model model;
    Vector3 drawOffset; // added to worldCenter in MapProp_DrawGround — lets
                        // Create* pick where the model's local origin sits
                        // without ever mutating the mesh's own vertex data
    MapGroundLookup lookup;
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
// Per-map biome grading. The tint is multiplied into both tiled ground
// textures through the material's standard colDiffuse uniform.
void MapProp_SetGroundTint(MapGroundSurface *ground, Color tint);
void MapProp_UnloadGround(MapGroundSurface *ground);

// Absolute world-space ground Y at (x,z) — for a flat MapProp_CreateGround
// surface this is just worldCenter.y; for a MapProp_CreateGroundHeightmap
// surface it raycasts straight down against the ACTUAL built mesh
// (GetRayCollisionMesh) instead of re-deriving height from the source
// heightmap image. An earlier version re-implemented GenMeshHeightmap's
// pixel->height mapping by hand (nearest-pixel, then bilinear) and both were
// wrong somewhere (grayscale formula / row-column convention / low heightmap
// resolution vs the smooth rendered surface — never fully pinned down) badly
// enough to bury ground-hugging effects (FISSURE) meters underground.
// Raycasting the real mesh can't have that class of bug — it reads the
// exact same triangles DrawModel renders, no formula to get wrong.
//
// COST, and why it is no longer a reason to ration the call (25/08/2026).
// `GetRayCollisionMesh` tests EVERY triangle of the mesh. On VERDANT_PATH's
// 7,938-triangle island that was enough to force every consumer to ration
// itself: vc_ground_wave.inl down from 455 samples/frame (which took the grass
// map to 13 fps) to 48, and vc_rune_circle.inl behind a cache on top of that.
//
// It now goes through MapGroundLookup, an XZ bin grid over the SAME triangle
// array — ~2 candidate triangles instead of 7,938. Measured at map load with
// WUXING_GROUND_LOOKUP_VERIFY=1, 4,096 probes over the island:
//     raycast   232-292 us/sample   (varies run to run)
//     grid         ~0.59 us/sample    (~400-500x, and that figure still
//                                      carries the verifier's own overhead)
//     agreement 4096/4096, 0 hit mismatches, max |dY| = 0.000204 m
// 0.2 mm is float rounding between barycentric interpolation and the raycast's
// Moller-Trumbore, not a difference in what was hit. The correctness argument
// above is untouched: the grid changes which triangles are TESTED, never how a
// height is derived. Set the env var to re-run the comparison whenever this
// file, or raylib's mesh generation, changes.
//
// The raycast remains as the fallback for any surface whose grid did not build.
// See CORE_API.md's DrawCoreGroundPatch for the GroundHeightSampleFn signature
// this is meant to be wrapped for.
float MapProp_SampleGroundHeight(const MapGroundSurface *ground, Vector3 worldCenter, float x, float z);

// Same real-mesh raycast as MapProp_SampleGroundHeight, but preserves the
// triangle position/normal for conformal VFX receivers. Returns false outside
// the mesh footprint; outputs then remain safe flat-ground defaults.
bool MapProp_SampleGroundSurface(const MapGroundSurface *ground, Vector3 worldCenter,
                                 float x, float z, Vector3 *outPosition, Vector3 *outNormal);

// --- Flat strip (stone path, road, bridge deck, ...) --------------------

typedef struct
{
    Model model;
    Vector2 tiling;
    bool useSurfaceMaps;
    bool ready;
} MapStripSurface;

// length = extent along the strip's own long axis, width = across it.
// normalPath/roughnessPath: pass both for lit stone/soil micro-surface detail;
// pass NULL for both for a cheaper diffuse-only strip.
MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath);
void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset);
// Rotated/scaled draw for composing curved roads from short overlapping
// segments. Rotation is around world Y; scale is local X/Y/Z.
void MapProp_DrawStripEx(const MapStripSurface *strip, Vector3 worldCenter, float yOffset,
                         float rotationDeg, Vector3 scale);
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

// --- Reusable natural surfaces -----------------------------------------

typedef struct
{
    Vector3 position;
    float radius;
    float height;
    float rotationDeg;
    float phase;
} MapMeadowPlacement;

typedef struct
{
    Color rootColor;
    Color tipColor;
    int bladesPerClump;
    int bladeSegments;
    float bladeWidthScale; // relative to placement.radius; <= 0 uses 0.24
    float chunkSize;       // world meters; <= 0 uses 12
    float lodDistance;     // near->simplified transition; <= 0 disables LOD
    float drawDistance;    // <= 0 draws all chunks
} MapMeadowStyle;

typedef struct
{
    Model nearModel;
    Model farModel;
    Vector3 center;
    bool ready;
} MapMeadowChunk;

typedef struct
{
    MapMeadowChunk *chunks;
    int chunkCount;
    float lodDistance;
    float drawDistance;
    bool ready;
} MapMeadowSurface;

typedef struct
{
    Vector2 minBounds;
    Vector2 maxBounds;
    float spacing;
    float jitter;
    float minRadius;
    float maxRadius;
    float minHeight;
    float maxHeight;
    float yOffset;
    unsigned int seed;
} MapMeadowDistribution;

// Return density in [0,1]. Returning zero is also the universal exclusion
// mechanism for roads, water, cliffs, gameplay clearings, or authored masks.
typedef float (*MapFoliageDensityFn)(float x, float z, void *userData);

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData);

MapMeadowSurface MapProp_CreateMeadow(const MapMeadowPlacement *placements, int count,
                                      MapMeadowStyle style);
void MapProp_DrawMeadow(const MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength);
void MapProp_UnloadMeadow(MapMeadowSurface *meadow);

typedef struct
{
    Vector3 position;
    float height;
    float bloomRadius;
    float rotationDeg;
    float phase;
    Color petalColor;
    unsigned char petalCount; // clamped to 4..6; zero defaults to 5
    float petalLengthScale;   // <= 0 defaults to 1
} MapFlowerPlacement;

typedef struct
{
    Model model;
    bool ready;
} MapFlowerField;

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor);
void MapProp_DrawFlowerField(const MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength);
void MapProp_UnloadFlowerField(MapFlowerField *field);

typedef struct
{
    Vector3 center;
    float radiusX;
    float radiusZ;
    float bankWidth;
    float waveHeight;
    float waveScale;
    float waveSpeed;
    float bankGroundY;   // absolute terrain height reached by the outer bank
    float detailScale;   // world-space water detail frequency; <= 0 uses default
    float detailStrength;// subtle texture-driven normal/color breakup
    int segments;
    int rings;
    unsigned int seed;
    Color deepColor;
    Color shallowColor;
    Color foamColor;
    Color bankInnerColor;
    Color bankOuterColor;
} MapWaterConfig;

typedef struct
{
    Model waterModel;
    Model bankModel;
    MapWaterConfig config;
    bool ready;
} MapWaterSurface;

MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config);
Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale);
void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time);
void MapProp_UnloadWaterSurface(MapWaterSurface *water);

#endif // MAP_PROPS_H

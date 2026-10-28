#ifndef MAP_PROPS_H
#define MAP_PROPS_H

#include "raylib.h"
#include "map_ecology.h"
#include <stdbool.h>
#include <math.h>

// Shared math helper for smooth transitions (GLSL smoothstep clamp)
static inline float MapMath_SmoothStep01(float edge0, float edge1, float x)
{
    float t = (x - edge0) / fmaxf(1e-5f, edge1 - edge0);
    t = fminf(1.0f, fmaxf(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

// Snaps a camera/actor target onto a regular world grid for stable dynamic shadow projections
static inline Vector3 MapProp_SnapShadowFocus(Vector3 target, float gridSize)
{
    float inv = 1.0f / (gridSize > 0.0f ? gridSize : 0.5f);
    return (Vector3){
        floorf(target.x * inv + 0.5f) * gridSize,
        0.0f,
        floorf(target.z * inv + 0.5f) * gridSize
    };
}

// Subtractive GPU profiling skip mask (WUXING_MAP_SKIP or WUXING_SONGQUAO_SKIP)
typedef enum {
    MAP_SKIP_GROUND         = 1 << 0,
    MAP_SKIP_ROCKS          = 1 << 1,
    MAP_SKIP_DEBRIS         = 1 << 2,
    MAP_SKIP_FLORA          = 1 << 3,
    MAP_SKIP_SKY            = 1 << 4,
    MAP_SKIP_CLOUDSEA       = 1 << 5,
    MAP_SKIP_WATER          = 1 << 6,
    MAP_SKIP_MIST           = 1 << 7,
    MAP_SKIP_SHADOWCASTERS  = 1 << 8,
    MAP_SKIP_FLUFF          = 1 << 9,
    MAP_SKIP_CLOUDSHADOW    = 1 << 10,
    MAP_SKIP_FOGVOLUMES     = 1 << 11,
    MAP_SKIP_GODRAYS        = 1 << 12,
    MAP_SKIP_GRADE          = 1 << 13,
} MapSkipMaskFlag;

unsigned int MapProp_GetSkipMask(void);

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
// Shared visual perimeter, independent of biome materials and collision.
// rect = world X/Z origin and width/depth; all distances are metres.
typedef struct {
    Vector4 rect;
    float cornerRadius, mistWidth, cloudLift, cloudBankWidth;
    float groundInset; // plateau-to-rectangle setback; shifts the mist inward
} MapIslandBoundary;
float MapProp_IslandBoundaryDistance(const MapIslandBoundary *boundary, Vector2 worldXZ);

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
    const MapEcology *ecology; // Borrowed shared habitat data; owner outlives ground.
    Vector2 tiling; // Per-surface texture repeats, uploaded in the active draw scope.
    Texture2D reliefTexture; // ResourceManager-owned optional R/G heights, B/A litter/moss.
    Texture2D debrisTexture; // Macro debris distribution map (R=litter, G=scree, B=twigs, A=moss).
    MapIslandBoundary boundary;
} MapGroundSurface;

void MapProp_SetGroundIslandBoundary(MapGroundSurface *ground, const MapIslandBoundary *boundary);

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
// RG8 high/low bytes encode normalized uint16 height; <=65535 grid nodes.
// Collision, shading and water bake share the same two triangles per cell.
MapGroundSurface MapProp_CreateGroundHeightfield16(const char *heightmapPath, float width, float depth,
                                                  float totalHeight, float cliffDepth, float tileSize,
                                                  const char *splatMapPath, const char *grassTexPath,
                                                  const char *pathTexPath);
// Universal heightmap loader: totalHeight = cliffDepth + elevationRange.
// Plateau/water level rests at its natural elevation while abyss sinks to -cliffDepth.
MapGroundSurface MapProp_CreateGroundHeightmapEx(const char *heightmapPath, float width, float depth,
                                                 float totalHeight, float cliffDepth, float tileSize,
                                                 const char *splatMapPath,
                                                 const char *grassTexPath,
                                                 const char *pathTexPath);
void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter);
// Geometry-only depth pass used by EnvShadow's cached static-map capture.
// Call only between EnvShadow_BeginStaticCapture/EndStaticCapture.
void MapProp_DrawGroundShadowCaster(MapGroundSurface *ground, Vector3 worldCenter,
                                    Shader depthShader);
// Per-map biome grading. The tint is multiplied into both tiled ground
// textures through the material's standard colDiffuse uniform.
void MapProp_SetGroundTint(MapGroundSurface *ground, Color tint);
void MapProp_SetGroundEcology(MapGroundSurface *ground, const MapEcology *ecology);
// Optional packed tangent-space normal (RGB) and roughness (A) textures.
void MapProp_SetGroundSurfaceMaps(MapGroundSurface *ground,
                                  const char *grassMaterialPath,
                                  const char *soilMaterialPath);
// Optional tileable relief: R substrate height, G soil height, B litter, A moss.
// Normal RGB / roughness A remain in the separate material maps.
void MapProp_SetGroundReliefMap(MapGroundSurface *ground, const char *reliefPath);
// Macro debris map (R=litter, G=scree, B=twigs, A=moss).
void MapProp_SetGroundDebrisMap(MapGroundSurface *ground, const char *debrisMapPath);
// Configure multi-layer habitat blending (path corridors, shoreline wetness, soil transitions)
void MapProp_SetGroundHabitat(MapGroundSurface *ground,
                              const Vector4 *pathSegments, int segmentCount,
                              Vector4 lakeParams);
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
    // Model-space bounding sphere of the generated mesh, baked once at load.
    // MapProp_DrawRockSystem culls each placement against the camera frustum
    // with it, so a rock is skipped only when the whole mesh is off-screen.
    Vector3 boundsCenter;
    float boundsRadius;
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
// Procedural stratified mountain crag / cliff mesh for border rings and horizons
MapRockSet MapProp_CreateMountainCrags(const char *diffusePath, const char *normalPath, const char *roughnessPath);
// Procedural water-smoothed riverbed / stepping stone mesh
MapRockSet MapProp_CreateRiverStones(const char *diffusePath, const char *normalPath, const char *roughnessPath);
// drawShadow: pass false for large border/mountain-ring rocks — dozens of
// giant fake-shadow decals stacked/overlapping is real alpha overdraw
// (measured FPS cost, not theoretical) for a case where the shadow barely
// reads anyway. Pass true for normal scattered decorative rocks.
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow);
// Geometry-only counterpart to MapProp_DrawRocks for a static shadow capture.
// It never emits fake blob shadows or updates lighting uniforms.
void MapProp_DrawRockShadowCasters(MapRockSet *rocks,
                                   const MapRockPlacement *placements, int count,
                                   Shader depthShader);
void MapProp_UnloadRocks(MapRockSet *rocks);

// --- Unified Rock & Crag System -----------------------------------------

typedef enum {
    MAP_ROCK_BOULDER = 0,     // Weathered rounded boulder (Granite Tor)
    MAP_ROCK_CRAG,            // Jagged karst/granite mountain crag/spire
    MAP_ROCK_RIVER_STONE,     // Smooth, flattened riverbed/shoreline stone
    MAP_ROCK_GRANITE,         // Chiseled Granite Tor / Massive blocky cleavage
    MAP_ROCK_BASALT,          // Hexagonal columnar basalt joints
    MAP_ROCK_SANDSTONE,       // Layered tabular sandstone strata
    MAP_ROCK_LIMESTONE,       // Dissolved karst pinnacle with vertical fluting
    MAP_ROCK_OBSIDIAN,        // Conchoidal razor-sharp volcanic glass shard
    MAP_ROCK_SLATE,           // Thin foliated tilted metamorphic plates
    MAP_ROCK_SCREE,           // Angular shattered talus gravel
    MAP_ROCK_ARCHETYPE_COUNT
} MapRockArchetype;

typedef struct {
    Vector3 position;
    float radiusScale;
    float heightScale;
    float rotationDeg;
    MapRockArchetype archetype;
} MapRockItem;

typedef struct {
    MapRockSet models[MAP_ROCK_ARCHETYPE_COUNT];
    MapRockSet boulders;     // backwards-compatibility alias to models[MAP_ROCK_BOULDER]
    MapRockSet crags;        // backwards-compatibility alias to models[MAP_ROCK_CRAG]
    MapRockSet riverStones;  // backwards-compatibility alias to models[MAP_ROCK_RIVER_STONE]
    MapRockItem *items;
    int count;
    int capacity;
    bool ready;
} MapRockSystem;

MapRockSystem MapProp_CreateRockSystem(const char *diffusePath, const char *normalPath, const char *roughnessPath, int capacity);
bool MapProp_AddRockItem(MapRockSystem *sys, Vector3 pos, float rScale, float hScale, float rotDeg, MapRockArchetype archetype);
int MapProp_AddRockCluster(MapRockSystem *sys, Vector3 center, MapRockArchetype archetype,
                           int count, float clusterRadius, float baseScale, unsigned int seed);
void MapProp_DrawRockSystem(const MapRockSystem *sys, bool drawShadows);
void MapProp_DrawRockSystemShadowCasters(MapRockSystem *sys, Shader depthShader);
void MapProp_UnloadRockSystem(MapRockSystem *sys);
void MapProp_PopulateRockItems(MapRockSystem *sys, const MapGroundSurface *ground,
                               Vector3 groundCenter, const MapRockItem *items, int itemCount);
void MapProp_AddCragPlacements(MapRockSystem *sys, const MapGroundSurface *ground,
                               Vector3 groundCenter, const Vector2 *points, int count,
                               float baseScale);
float MapProp_EvaluateRockSuppression(float x, float z, const MapRockItem *items, int itemCount);


// Fills outPlacements[] (capacity maxCount) with a ring of giant rock
// placements around a mapWidth x mapDepth rectangle's border — the
// legacy optional rock composition. Floating-plain boundaries use clouds. Draw the
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
    Vector2 tiling; // Texture repeats owned by this surface.
    Vector2 size;
    MapIslandBoundary boundary;
    Model mistModel;
    bool mistReady;
    Texture2D mistTexture; // Borrowed baked rim sprite; no emitter or simulation.
} MapCloudSea;

// Closed contours in map-local space, with the plateau on the left of each edge.
// Supports concave loops, disconnected islands and clockwise hole contours.
typedef struct { const Vector3 *points; int count; } MapBoundaryContour;
typedef struct {
    float innerWidth, outerWidth, heightOffset, outerDrop, opacity;
} MapBoundaryMistStyle;
// Copies/bakes once. Failure retains the previous mesh; zero contours clears it.
bool MapProp_SetCloudSeaBoundaryContours(MapCloudSea *cloud,
    const MapBoundaryContour *contours, int count, const MapBoundaryMistStyle *style);
// Extracts outer plateau contours from the actual indexed or unindexed terrain.
// contourHeight is relative to worldCenter.y; interior lake contours are excluded.
bool MapProp_SetCloudSeaGroundBoundary(MapCloudSea *cloud,
    const MapGroundSurface *ground, float contourHeight, const MapBoundaryMistStyle *style);

// width/depth in world meters (make this bigger than the map itself so it
// reads as an endless sea beneath the floating plain). tileSize
// controls the repeats of the tileable cloud-noise texture — smaller
// tileSize = smaller/denser
// cloud puffs. Draw well below the ground (yOffset negative, e.g. -12.0f).
//
// Uses a discard-based opaque cutout, never partial alpha — see
// maps/toolkit/shaders/cloud_sea.fs and maps/CLAUDE.md's Alpha = 255 rule.
MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize);
// Legacy rectangle descriptor only; the cloud sheet remains flat.
// New maps should bake terrain or explicit boundary contours.
void MapProp_SetCloudSeaIslandBoundary(MapCloudSea *cloud, const MapIslandBoundary *boundary);
void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset);
// Cached contour ribbon, one draw; call after opaque scenery in the transparent pass.
void MapProp_DrawIslandMist(const MapCloudSea *cloud, Vector3 worldCenter);
void MapProp_UnloadCloudSea(MapCloudSea *cloud);

// Opt-in camera-centred sky. Draw after opaque scenery, inside BeginMode3D.
// Uses resolved Environment lighting; tests at far depth without writing it.
typedef struct { Model model; Shader shader; int sunLoc, skyLoc, hazeLoc, colorLoc; bool ready; } MapSkyDome;
MapSkyDome MapProp_CreateSkyDome(void);
void MapProp_DrawSkyDome(const MapSkyDome *sky);
void MapProp_UnloadSkyDome(MapSkyDome *sky);

// --- Reusable natural surfaces -----------------------------------------

typedef struct
{
    Vector3 position;
    float radius;
    float height;
    float rotationDeg;
    float phase;
} MapMeadowPlacement;

typedef enum {
    MAP_MEADOW_GROWTH_AUTO = 0, // Legacy height-based grass/reed choice.
    MAP_MEADOW_GROWTH_GRASS,
    MAP_MEADOW_GROWTH_REED,
    MAP_MEADOW_GROWTH_PAMPAS
} MapMeadowGrowthForm;

// Generalized physiological / environmental grass blade states.
typedef enum {
    MAP_GRASS_STATE_LUSH = 0,     // Healthy green chlorophyll meadow
    MAP_GRASS_STATE_WITHERED = 1, // Yellowing / senescent grass (chlorophyll breakdown)
    MAP_GRASS_STATE_DRY = 2,      // Desiccated golden-straw grass
    MAP_GRASS_STATE_SCORCHED = 3  // Fire-charred carbon/ash stalks with optional embers
} MapGrassState;

// Continuous blendable grass condition parameters.
typedef struct {
    float dryness;        // [0..1]: 0.0 = lush green, 0.45 = withered yellow, 1.0 = dry straw
    float scorch;         // [0..1]: 0.0 = unburned, 1.0 = fire-charred black & ash
    float emberIntensity; // [0..2]: glowing smoldering ember strength on scorched grass
    float patchVariation; // [0..1]: organic patchy transition variance across the meadow
} MapGrassCondition;

typedef struct
{
    Color rootColor;
    Color tipColor;
    int bladesPerClump;
    int bladeSegments;
    float bladeWidthScale; // relative to placement.radius; <= 0 uses 0.24
    float chunkSize;       // world meters; <= 0 uses 12
    float lodDistance;     // near->simplified transition; <= 0 disables LOD
    float midLodDistance;  // near->mid transition; <= 0 defaults to 0.45 * lodDistance
    float drawDistance;    // <= 0 draws all chunks
    float shadowDistance;  // close contact-shadow range; <= 0 disables it
    const char *texturePath; // optional alpha-cutout blade texture
    float alphaCutoff;       // <= 0 uses 0.42
    bool hasPlumes;          // shore reeds: adds fluffy ivory plumes on top and arching side leaves
    float botanicalVariation; // 0 preserves authored shapes; [0,1] mixes patch-coherent arches/leaf accents.
    MapMeadowGrowthForm growthForm; // Explicit morphology independent of plant height.
    MapGrassState grassState;       // Preset physiological state (default: MAP_GRASS_STATE_LUSH)
    float dryness;                  // Optional explicit [0..1] dryness override when > 0
    float scorch;                   // Optional explicit [0..1] fire scorch override when > 0
} MapMeadowStyle;

typedef struct
{
    Model nearModel;
    Model midModel;
    Model farModel;
    Model shadowModel;
    Model realShadowModel; // sparse geometry LOD for the directional depth pass
    Vector3 center;
    float radius; // conservative chunk sphere used by view-frustum culling
    int lodLevel; // 0 = Near, 1 = Mid, 2 = Far
    bool farLod;  // persistent state for hysteresis; avoids boundary thrashing
    bool midReady;
    bool shadowReady;
    bool realShadowReady;
    bool visibleThisFrame;
    bool ready;
} MapMeadowChunk;

typedef struct
{
    MapMeadowChunk *chunks;
    int chunkCount;
    float lodDistance;
    float midLodDistance;
    float drawDistance;
    float shadowDistance;
    bool textured;
    float alphaCutoff;
    bool ready;
    void *parametric; // Private immutable tuft templates and blade parameter atlas.
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
float MapProp_GetMacroFlowAngle(float x, float z);
// Persistent mobile-safe interaction field shared by meadows and flowers.
// Begin scrolls/decays the 64x64 local field, Add stamps any number of actors,
// and End uploads it once. Strength is displacement in world metres.
void MapProp_BeginNatureInteraction(Vector3 focus, float dt);
void MapProp_AddNatureInteractor(Vector3 position, float radius, float strength);
// Sends the strongest Wind Vorticle through a direct uniform receiver, rasterizes
// the remaining sources into the already-open interaction field, and synchronizes
// macro direction/speed. Call once between Begin/End; grass and flowers share the
// same visible + dynamic-shadow deformation.
void MapProp_AddNatureWindVorticles(float time);
void MapProp_EndNatureInteraction(void);
void MapProp_ClearNatureInteraction(void);
// Generalized multi-state grass condition & localized fire burn-zone API.
// Transitions grass smoothly between Lush Green, Withered Yellow, Dry Straw, and
// Fire-Scorched Carbon/Ash with glowing embers in the shader without rebuilding VBOs.
void MapProp_SetGrassState(MapGrassState state, float emberIntensity);
void MapProp_SetGrassCondition(MapGrassCondition condition);
MapGrassCondition MapProp_GetGrassCondition(void);
void MapProp_ClearGrassBurnZones(void);
void MapProp_AddGrassBurnZone(Vector3 position, float radius, float scorchStrength);
typedef struct
{
    int meadowChunksTested;
    int meadowChunksVisible;
    int meadowFrustumCulled;
    int meadowDistanceCulled;
    int meadowNearDraws;
    int meadowMidDraws;
    int meadowFarDraws;
    int meadowShadowDraws;
    int meadowShadowDistanceCulled;
    int flowerFieldsTested;
    int flowerFieldsFrustumCulled;
    int flowerFieldsDistanceCulled;
    int flowerDraws;
    int flowerNearDraws;
    int flowerFarDraws;
    int flowerShadowDraws;
    int flowerShadowDistanceCulled;
} MapNatureRenderStats;

void MapProp_ResetNatureRenderStats(void);
MapNatureRenderStats MapProp_GetNatureRenderStats(void);
// Optional update/pre-render preparation after camera finalization, before any render pass.
// Stages compact visible IDs; Draw remains correct without preparation.
void MapProp_PrepareMeadow(MapMeadowSurface *meadow, Vector3 worldOffset);
void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength);
// HIGH-tier dynamic shadow-map submission using a stable sparse geometry LOD
// with the same blade silhouette/deformation. Call only from an EnvShadow map callback.
void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength);
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
    unsigned char bloomVariant; // atlas cell, wrapped by atlasColumns*atlasRows
    float petalLengthScale;   // <= 0 defaults to 1
} MapFlowerPlacement;

typedef struct
{
    Model model;
    Model farModel;
    Model shadowModel;
    bool textured;
    bool farReady;
    bool farLod; // persistent hysteresis state
    bool shadowReady;
    float alphaCutoff;
    Vector3 boundsCenter;
    float boundsRadius;
    float drawDistance;
    float lodDistance;
    float shadowDistance;
    bool ready;
} MapFlowerField;

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor,
                                         const char *petalTexturePath, float alphaCutoff,
                                         int atlasColumns, int atlasRows);
// Sets the field-level culling range in world metres. The default is 78 m;
// zero or a negative value disables distance culling.
void MapProp_SetFlowerFieldDrawDistance(MapFlowerField *field, float drawDistance);
// Sets the near-to-far geometry switch and contact-shadow range in world
// metres. Defaults are 34 m and 26 m; non-positive values disable each limit.
void MapProp_SetFlowerFieldLod(MapFlowerField *field, float lodDistance, float shadowDistance);
void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength);
// HIGH-tier alpha-clipped shadow-map submission of the full near flower mesh.
void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength);
void MapProp_UnloadFlowerField(MapFlowerField *field);

// --- Unified Flora System (Meadows, Reeds, Wildflower Colonies) ---------

typedef struct {
    Vector3 center;
    Vector2 radius;
    int flowerCount;
    int primarySpecies;       // 0..7 (daisy, buttercup, poppy, cornflower, rose, cosmos, lavender, primrose)
    unsigned char species[4]; // 4 signature & companion species
    Color primaryColor;
    Color accentColor;
    Color discalCenterColor;  // dome center color
} MapFloraFlowerCluster;

typedef struct {
    Color grassRootColor;
    Color grassTipColor;
    int bladesPerClump;       // Default: 6
    int bladeSegments;        // Default: 4 (smooth Bézier, no stick grass!)
    float bladeWidthScale;    // Default: 0.16
    float lodDistance;        // Default: 32.0m
    float midLodDistance;     // Optional mid-LOD distance (if <= 0, defaults to 0.45 * lodDistance)
    float drawDistance;       // Default: 60.0m
    float shadowDistance;     // Default: 16.0m (casts dynamic directional grass shadows within near range)
    MapGrassState grassState; // Default: MAP_GRASS_STATE_LUSH
    float dryness;            // Optional explicit [0..1] dryness override
    float scorch;             // Optional explicit [0..1] fire scorch override

    Color reedRootColor;
    Color reedTipColor;
    bool hasReedPlumes;       // Default: false (prevents flat white cards)
    float botanicalVariation; // Optional [0..1] habitat-based color variation (0 = 100% pure artist tip color)

    const MapMeadowPlacement *pampasPlacements;
    int pampasCount;
    Color pampasRootColor;
    Color pampasTipColor;
} MapFloraStyle;

typedef struct {
    MapMeadowSurface valleyMeadow;
    MapMeadowSurface reedMeadow;
    MapMeadowSurface pampasMeadow;
    int valleyGrassCount;
    int reedCount;
    int pampasCount;

    MapFlowerField *flowerFields;
    int flowerClusterCount;

    float time;
    Vector2 windDirection;
    float windStrength;
    bool ready;
} MapFloraSystem;

Color MapProp_GetFlowerSpeciesColor(int variant, bool accent);

typedef struct {
    Vector2 minBounds;
    Vector2 maxBounds;
    float spacing;             // <= 0 uses canonical 0.22m dense interlocking meadow carpet
    unsigned int seed;         // 0 uses 0x51a7c3u
    MapFoliageDensityFn densityFn;
    void *densityUserData;
    const MapEcology *ecology; // optional baked ecology for growth/coverage/habitat shaping
    const MapFloraFlowerCluster *flowerClusters;
    int flowerClusterCount;
    // Optional callback returning [0..1] edge height taper (e.g. near paths or shorelines); NULL = 1.0
    float (*edgeTaperFn)(float x, float z, void *userData);
    void *edgeTaperUserData;
} MapFloraMeadowConfig;

// Unified meadow tuft placement & biome shaping used across all maps
int MapFlora_GenerateMeadowPlacements(
    MapMeadowPlacement *outPlacements,
    int maxCount,
    const MapGroundSurface *ground,
    Vector3 groundCenter,
    MapFloraMeadowConfig config);

// Generalized procedural flower generator (100% procedural 3D geometric petals, no textures)
void MapFlora_GenerateFlowerPlacements(
    MapFlowerPlacement *outPlacements,
    int totalFlowerCount,
    const MapFloraFlowerCluster *clusters,
    int clusterCount,
    float (*sampleHeight)(float x, float z, void *ctx),
    bool (*isExcluded)(float x, float z, void *ctx),
    void *ctx,
    unsigned int seed);

float MapFlora_EvaluateFlowerUnderstorey(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount);

float MapFlora_EvaluateFlowerSuppression(
    float x, float z,
    const MapFloraFlowerCluster *clusters,
    int clusterCount);

struct MapDemIsland; // Forward declaration

typedef struct {
    Vector2 span;             // XZ half-extent for placement search (e.g. {mapWidth * 0.38f, mapDepth * 0.32f})
    float minWaterClearance;  // Default: 0.01f
    float maxWaterClearance;  // Default: 0.42f
    unsigned char minWetSplatAlpha; // Default: 95
    float maxWetClearance;    // Default: 0.85f
    float minSlopeNormalY;    // Default: 0.70f
    float minGroundY;         // Default: -2.40f
    float minClusterDistance; // Clustering spatial wave threshold, default: -0.15f
    float minRadius;          // Default: 0.24f
    float maxRadius;          // Default: 0.32f
    float minHeight;          // Default: 0.44f
    float maxHeight;          // Default: 0.62f
    unsigned int seed;
    const struct MapDemIsland *dem; // Optional DEM island helper
    float (*sampleWaterHeight)(float x, float z, void *ctx);
    Color (*sampleSplat)(float x, float z, void *ctx);
    void *ctx;
} MapFloraReedConfig;

int MapFlora_GenerateReedPlacements(
    MapMeadowPlacement *outPlacements, int maxCount,
    const MapGroundSurface *ground, Vector3 groundCenter,
    MapFloraReedConfig config);

typedef struct {
    Vector2 span;             // XZ half-extent
    float clearingRadius;     // Central clearing radius to keep free (e.g. 11.5f)
    float minSlopeNormalY;    // Default: 0.54f
    float minGroundY;         // Default: -1.20f
    float minWaterClearance;  // Default: 0.85f
    unsigned char maxRockSplatGreen; // Default: 190
    float driftThreshold;     // Default: 0.30f
    float minRadius;          // Default: 0.30f
    float maxRadius;          // Default: 0.40f
    float baseHeight;         // Default: 0.48f
    const MapFloraFlowerCluster *flowerClusters;
    int flowerClusterCount;
    unsigned int seed;
    const struct MapDemIsland *dem; // Optional DEM island helper
    float (*sampleWaterHeight)(float x, float z, void *ctx);
    Color (*sampleSplat)(float x, float z, void *ctx);
    void *ctx;
} MapFloraPampasConfig;

int MapFlora_GeneratePampasPlacements(
    MapMeadowPlacement *outPlacements, int maxCount,
    const MapGroundSurface *ground, Vector3 groundCenter,
    MapFloraPampasConfig config);

MapFloraSystem MapProp_CreateFloraSystem(
    const MapMeadowPlacement *grassPlacements, int grassCount,
    const MapMeadowPlacement *reedPlacements, int reedCount,
    const MapFlowerPlacement *flowerPlacements, const MapFloraFlowerCluster *clusters, int clusterCount,
    MapFloraStyle style);

void MapProp_UpdateFloraSystem(MapFloraSystem *flora, Vector3 cameraTarget, float dt);
void MapProp_DrawFloraSystem(MapFloraSystem *flora);
void MapProp_DrawFloraShadowCasters(MapFloraSystem *flora);
void MapProp_UnloadFloraSystem(MapFloraSystem *flora);

// --- Debris System (Fallen Leaves, Pebbles, Twigs, Moss Patches) ---------

typedef enum {
    MAP_DEBRIS_LEAF = 0,    // Curved 3D fallen autumn leaf (ginkgo / maple / dead leaves)
    MAP_DEBRIS_PEBBLE,      // Rounded river pebble / scree stone
    MAP_DEBRIS_TWIG,        // Fallen forest twig / woody branch
    MAP_DEBRIS_MOSS,        // Ground-hugging velvety moss clump
    MAP_DEBRIS_TYPE_COUNT
} MapDebrisType;

typedef struct {
    Vector3 position;
    float rotationDeg;
    float scale;
    Color color;
    unsigned char type;     // MapDebrisType
} MapDebrisPlacement;

typedef struct {
    Model models[MAP_DEBRIS_TYPE_COUNT];
    bool modelLoaded[MAP_DEBRIS_TYPE_COUNT];
    MapDebrisPlacement *placements;
    int placementCount;
    float drawDistance;     // Default: 32.0m
    bool ready;
} MapDebrisSystem;

MapDebrisSystem MapProp_CreateDebrisSystem(
    const MapDebrisPlacement *placements, int count,
    const MapGroundSurface *ground, Vector3 worldCenter, float drawDistance);
void MapProp_DrawDebrisSystem(const MapDebrisSystem *debris, Vector3 cameraPosition);
void MapProp_DrawDebrisShadowCasters(const MapDebrisSystem *debris);
void MapProp_UnloadDebrisSystem(MapDebrisSystem *debris);


typedef enum {
    WATER_ECO_ALPINE_STREAM = 0,   // Suối núi / đá vôi trong vắt: ngọc bích sáng, độ trong cao
    WATER_ECO_FOREST_SWAMP,        // Đầm lầy than bùn / vũng hoai mục: hổ phách / nâu đậm
    WATER_ECO_TROPICAL_SHALLOW,    // Thềm cát nhiệt đới / vịnh nông: xanh lơ nhạt, tương phản cát vàng
    WATER_ECO_STAGNANT_POND,       // Kênh rạch / vũng nước tù đọng: lục xỉn rêu phong
    WATER_ECO_CUSTOM               // Tự định nghĩa hệ số hấp thụ và tán xạ riêng
} MapWaterEcosystem;

typedef enum {
    WATER_SHAPE_RADIAL = 0,        // Hồ, ao, vụng nước tròn/elip có bờ uốn lượn ngẫu nhiên
    WATER_SHAPE_RECT,              // Vũng nước sân đình, bể chứa, rãnh nước chữ nhật
    WATER_SHAPE_STRIP,             // Dải suối uốn lượn / dòng chảy có hướng theo vector
    WATER_SHAPE_TERRAIN            // Baked world-space mesh, UV.x = vertical depth metres
} MapWaterShape;

typedef struct
{
    MapWaterShape shape;           // Kiểu hình thái mặt nước (mặc định: WATER_SHAPE_RADIAL)
    MapWaterEcosystem ecosystem;   // Preset sinh thái quang học Beer-Lambert

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

    // Các tham số chuyên sâu cho vùng nước nông (Độ sâu <= 1.3m):
    float maxDepth;            // Độ sâu quang học tối đa tính bằng mét (giới hạn <= 1.3m, mặc định 1.2m)
    Vector3 absorption;        // Hệ số hấp thụ Beer-Lambert beta_e(R, G, B) (đơn vị 1/m)
    Vector3 scatterColor;      // Sắc màu tán xạ ngược SSS Single Layer Water khi ngược nắng
    float scatterCoeff;        // Cường độ tán xạ ngược SSS (mặc định ~0.45)
    Vector2 flowVelocity;      // Vector định hướng và tốc độ dòng chảy (suối/kênh, mặc định (0, 0))
    float causticsStrength;    // Cường độ dải sáng tụ quang đáy nước (mặc định ~0.65)
    float causticsScale;       // Tần số không gian của mạng lưới tụ quang (mặc định ~1.2)
    float foamThreshold;       // Ngưỡng độ sâu xuất hiện bọt bờ (mặc định ~0.12m)
    float refractionStrength;  // Cường độ khúc xạ lệch tia màn hình (mặc định ~0.04)
} MapWaterConfig;

#define MAX_WATER_RIPPLES 4
#define MAX_WATER_OBSTACLES 4
#define WATER_FIELD_SIZE 128

typedef struct {
    Vector3 position;
    float spawnTime;
    float maxRadius;
    float amplitude;
    float speed;
    float wavelength;
    float decay;
    bool active;
} MapWaterRippleRing;

typedef struct
{
    Model waterModel;
    Model bankModel;
    Model bedModel;            // Nền hồ / đáy suối 3D lõm thật (bathymetric basin)
    MapWaterConfig config;
    Texture2D causticTex;      // Texture tụ quang ánh sáng (nếu có)
    Texture2D bedDiffuseTex;   // Texture đá cuội/cát đáy hồ

    // Dynamic Surface Interaction (Kelvin wake & ripples):
    Vector3 interactorPos;
    Vector3 interactorVel;
    float interactorRadius;
    float interactorSubmerged;

    // Expanding shockwave ripple rings:
    MapWaterRippleRing ripples[MAX_WATER_RIPPLES];
    int nextRipple;
    float lastTime;
    Vector4 obstacles[MAX_WATER_OBSTACLES]; // world X/Z, waterline radius, strength
    int obstacleCount;
    // Damped 2D shallow-water height field. The CPU advances it at a fixed
    // step; the texture carries slopes (R/G) and height (B) to the shaders.
    float *waveHeightField;
    float *waveNextField;
    float *waveVelocityField;
    unsigned char *wavePixels;
    Texture2D waveFieldTex;
    float waveStepRemainder;
    float waveWakeTimer;
    float waveLastImpulseTime;
    Vector3 waveGridCenter;
    float waveGridRadius;

    // Model-space bounding sphere of `waterModel`, baked at creation. The whole
    // surface is a single draw, so MapProp_DrawWaterOverlay skips it entirely
    // when this sphere is outside the camera frustum.
    Vector3 boundsCenter;
    float boundsRadius;

    bool ready;

    // Private exact-byte upload cache; allocated only with the wake texture.
    unsigned char *waveUploadedPixels;
    unsigned int waveUploadedTextureId;
} MapWaterSurface;

MapWaterConfig MapProp_DefaultWaterConfig(MapWaterEcosystem eco);
MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config);
// Offline terrain-clipped OBJ; UV.x is vertical water depth in metres.
// No analytical bed or wake grid; caller queries the companion water raster.
MapWaterSurface MapProp_CreateTerrainWater(const char *meshPath, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterStrip(Vector3 start, Vector3 end, float width, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterSpline(const Vector3 *points, const float *widths, int pointCount, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterPatch(Vector3 center, float width, float depth, MapWaterConfig config);
Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale);
// Truy vấn độ sâu vật lý của đáy hồ/suối tại tọa độ (x, z). Trả về true nếu nằm trong lòng nước,
// kèm theo độ cao Y tuyệt đối của đáy hồ và pháp tuyến nghiêng của sườn đáy.
bool MapProp_SampleWaterBed(const MapWaterSurface *water, float x, float z,
                            float *outBedHeight, Vector3 *outNormal);

// Cập nhật nguồn tương tác động (nhân vật, quái vật, v.v.) lên mặt nước để sinh sóng rẽ nước và bọt
void MapProp_SetWaterInteractor(MapWaterSurface *water, Vector3 position, Vector3 velocity,
                                float radius, float submerged);

// Kích hoạt một vòng sóng xung kích loang dần trên mặt nước (tiếp đất, bước mạnh, nổ, v.v.)
void MapProp_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity);
void MapProp_AddWaterObstacle(MapWaterSurface *water, Vector3 position, float radius, float strength);
void MapProp_UpdateWaterSurface(MapWaterSurface *water, float dt);

void MapProp_DrawWaterBed(const MapWaterSurface *water, float time);
void MapProp_DrawWaterOverlay(const MapWaterSurface *water, float time);
void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time);
void MapProp_UnloadWaterSurface(MapWaterSurface *water);

// --- Atmospheric Fluff / Floating Spores System -------------------------

typedef struct {
    Vector3 position;
    float phase;
    float size;
    float speed;
    float rot;
    float rotSpeed;
    float opacity;
} MapFluffParticle;

typedef struct {
    MapFluffParticle *particles;
    int count;
    Texture2D texture;
    bool ready;
} MapFluffSystem;

MapFluffSystem MapProp_CreateFluffSystem(int count, unsigned int seed);
void MapProp_UpdateFluffSystem(MapFluffSystem *fluff, Vector3 cameraTarget,
                               const MapGroundSurface *ground, Vector3 groundCenter,
                               float minGroundY, float dt);
void MapProp_DrawFluffSystem(const MapFluffSystem *fluff, Camera3D cam);
void MapProp_UnloadFluffSystem(MapFluffSystem *fluff);

// --- DEM-based Island System (sampling, heightfield water, abyss detection, and MapManager query hooks) ---

typedef struct MapDemIsland {
    float mapWidth;
    float mapDepth;
    float heightRange;
    float cliffDepth;
    Vector3 mapCenter;
    Image splatImage;
    bool splatLoaded;
    Image waterHeightImage;
    bool waterHeightLoaded;
    bool ready;
} MapDemIsland;

bool MapDem_InitIsland(MapDemIsland *dem,
                       float mapWidth, float mapDepth,
                       float heightRange, float cliffDepth,
                       const char *splatPath,
                       const char *waterHeightPath);

void MapDem_UnloadIsland(MapDemIsland *dem);

Color MapDem_SampleSplat(const MapDemIsland *dem, float x, float z);

float MapDem_SampleWaterHeight(const MapDemIsland *dem, float x, float z);

bool MapDem_IsAbyss(const MapDemIsland *dem, const MapGroundSurface *ground, float x, float z);

float MapDem_GetGroundHeight(const MapDemIsland *dem, const MapGroundSurface *ground,
                             float x, float z);

bool MapDem_SampleGroundSurface(const MapDemIsland *dem, const MapGroundSurface *ground,
                                float x, float z, Vector3 *outPosition, Vector3 *outNormal);

bool MapDem_GetWaterInfo(const MapDemIsland *dem, const MapGroundSurface *ground,
                         float x, float z, float *outSurfaceY, float *outWaterDepth);

void MapDem_SetWaterInteractor(const MapDemIsland *dem, const MapGroundSurface *ground,
                               MapWaterSurface *water, Vector3 position, Vector3 velocity, float radius);

void MapDem_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity);

#endif // MAP_PROPS_H

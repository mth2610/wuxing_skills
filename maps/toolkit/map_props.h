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
        floorf(target.z * inv + 0.5f) * gridSize};
}

// Subtractive GPU profiling skip mask (WUXING_MAP_SKIP or WUXING_SONGQUAO_SKIP)
typedef enum
{
    MAP_SKIP_GROUND = 1 << 0,
    MAP_SKIP_ROCKS = 1 << 1,
    MAP_SKIP_DEBRIS = 1 << 2,
    MAP_SKIP_FLORA = 1 << 3,
    MAP_SKIP_SKY = 1 << 4,
    MAP_SKIP_CLOUDSEA = 1 << 5,
    MAP_SKIP_WATER = 1 << 6,
    MAP_SKIP_MIST = 1 << 7,
    MAP_SKIP_SHADOWCASTERS = 1 << 8,
    MAP_SKIP_FLUFF = 1 << 9,
    MAP_SKIP_CLOUDSHADOW = 1 << 10,
    MAP_SKIP_FOGVOLUMES = 1 << 11,
    MAP_SKIP_GODRAYS = 1 << 12,
    MAP_SKIP_GRADE = 1 << 13,
    MAP_SKIP_CLIFF = 1 << 14,
} MapSkipMaskFlag;

unsigned int MapProp_GetSkipMask(void);

// --- Ground plane -------------------------------------------------------

typedef struct
{
    Vector4 rect;
    float cornerRadius, mistWidth, cloudLift, cloudBankWidth;
    float groundInset; // plateau-to-rectangle setback; shifts the mist inward
} MapIslandBoundary;
float MapProp_IslandBoundaryDistance(const MapIslandBoundary *boundary, Vector2 worldXZ);

typedef struct
{
    bool built;
    int nx, nz;               // bins along local X / Z
    float minX, minZ;         // local-space origin of the grid
    float invCellX, invCellZ; // 1 / bin size
    int triCount;
    int *cellStart;                // nx*nz + 1 CSR offsets
    int *cellItems;                // triangle indices, binned
    const float *verts;            // borrowed from Mesh.vertices
    const unsigned short *indices; // borrowed from Mesh.indices, may be NULL
} MapGroundLookup;

typedef struct
{
    Model model;
    Vector3 drawOffset;
    MapGroundLookup lookup;
    bool ready;
    const MapEcology *ecology;
    Vector2 tiling;
    Texture2D reliefTexture;
    Texture2D debrisTexture;
    Texture2D rockTexture;
    Texture2D rockNormalTexture;
    MapIslandBoundary boundary;
} MapGroundSurface;

void MapProp_SetGroundIslandBoundary(MapGroundSurface *ground, const MapIslandBoundary *boundary);
MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize, const char *splatMapPath, const char *grassTexPath, const char *pathTexPath);
MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                               float cliffDepth, float tileSize,
                                               const char *splatMapPath,
                                               const char *grassTexPath,
                                               const char *pathTexPath);
MapGroundSurface MapProp_CreateGroundHeightfield16(const char *heightmapPath, float width, float depth,
                                                   float totalHeight, float cliffDepth, float tileSize,
                                                   const char *splatMapPath, const char *grassTexPath,
                                                   const char *pathTexPath);
MapGroundSurface MapProp_CreateGroundHeightmapEx(const char *heightmapPath, float width, float depth,
                                                 float totalHeight, float cliffDepth, float tileSize,
                                                 const char *splatMapPath,
                                                 const char *grassTexPath,
                                                 const char *pathTexPath);
void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter);
void MapProp_DrawGroundShadowCaster(MapGroundSurface *ground, Vector3 worldCenter,
                                    Shader depthShader);
void MapProp_SetGroundTint(MapGroundSurface *ground, Color tint);
void MapProp_SetGroundEcology(MapGroundSurface *ground, const MapEcology *ecology);
void MapProp_SetGroundSurfaceMaps(MapGroundSurface *ground,
                                  const char *grassMaterialPath,
                                  const char *soilMaterialPath);
void MapProp_SetGroundReliefMap(MapGroundSurface *ground, const char *reliefPath);
void MapProp_SetGroundDebrisMap(MapGroundSurface *ground, const char *debrisMapPath);
void MapProp_SetGroundRockMaps(MapGroundSurface *ground, const char *diffusePath, const char *normalPath);
void MapProp_SetGroundHabitat(MapGroundSurface *ground,
                              const Vector4 *pathSegments, int segmentCount,
                              Vector4 lakeParams);
void MapProp_UnloadGround(MapGroundSurface *ground);
float MapProp_SampleGroundHeight(const MapGroundSurface *ground, Vector3 worldCenter, float x, float z);
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

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                    const char *diffusePath, const char *normalPath, const char *roughnessPath);
void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset);
void MapProp_DrawStripEx(const MapStripSurface *strip, Vector3 worldCenter, float yOffset,
                         float rotationDeg, Vector3 scale);
void MapProp_UnloadStrip(MapStripSurface *strip);

// --- Rock props (one mesh/texture set, many placements) -----------------

typedef struct
{
    Model model;
    bool ready;
    Vector3 boundsCenter;
    float boundsRadius;
} MapRockSet;

typedef struct
{
    Vector3 position;
    float radiusScale;
    float heightScale;
    float rotationDeg;
} MapRockPlacement;

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath);
MapRockSet MapProp_CreateMountainCrags(const char *diffusePath, const char *normalPath, const char *roughnessPath);
MapRockSet MapProp_CreateRiverStones(const char *diffusePath, const char *normalPath, const char *roughnessPath);
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow);
void MapProp_DrawRockShadowCasters(MapRockSet *rocks,
                                   const MapRockPlacement *placements, int count,
                                   Shader depthShader);
void MapProp_UnloadRocks(MapRockSet *rocks);

// --- Unified Rock & Crag System -----------------------------------------

typedef enum
{
    MAP_ROCK_BOULDER = 0,
    MAP_ROCK_CRAG,
    MAP_ROCK_RIVER_STONE,
    MAP_ROCK_GRANITE,
    MAP_ROCK_BASALT,
    MAP_ROCK_SANDSTONE,
    MAP_ROCK_LIMESTONE,
    MAP_ROCK_OBSIDIAN,
    MAP_ROCK_SLATE,
    MAP_ROCK_SCREE,
    MAP_ROCK_ARCHETYPE_COUNT
} MapRockArchetype;

typedef struct
{
    Vector3 position;
    float radiusScale;
    float heightScale;
    float rotationDeg;
    MapRockArchetype archetype;
} MapRockItem;

typedef struct
{
    MapRockSet models[MAP_ROCK_ARCHETYPE_COUNT];
    MapRockSet boulders;
    MapRockSet crags;
    MapRockSet riverStones;
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

int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                 float mapWidth, float mapDepth,
                                 float minRadiusScale, float maxRadiusScale,
                                 float minHeightScale, float maxHeightScale,
                                 unsigned int seed);

// --- Sea of clouds ------------------------------------------------------

typedef struct
{
    Model model;
    bool ready;
    Vector2 tiling;
    Vector2 size;
    MapIslandBoundary boundary;
    Model mistModel;
    bool mistReady;
    Texture2D mistTexture;
} MapCloudSea;

typedef struct
{
    const Vector3 *points;
    int count;
} MapBoundaryContour;
typedef struct
{
    float innerWidth, outerWidth, heightOffset, outerDrop, opacity;
} MapBoundaryMistStyle;

int MapProp_ExtractBoundaryLoop(const MapGroundSurface *ground, float height, Vector3 *outPoints, int maxPoints);
bool MapProp_SetCloudSeaBoundaryContours(MapCloudSea *cloud,
                                         const MapBoundaryContour *contours, int count, const MapBoundaryMistStyle *style);
bool MapProp_SetCloudSeaGroundBoundary(MapCloudSea *cloud,
                                       const MapGroundSurface *ground, float contourHeight, const MapBoundaryMistStyle *style);
MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize);
void MapProp_SetCloudSeaIslandBoundary(MapCloudSea *cloud, const MapIslandBoundary *boundary);
void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset);
void MapProp_DrawIslandMist(const MapCloudSea *cloud, Vector3 worldCenter);
void MapProp_UnloadCloudSea(MapCloudSea *cloud);

#define MAX_CLIFF_CHUNKS 32

typedef struct
{
    Model model;
    Vector3 center;
    float radius;
    bool ready;
} MapCliffChunk;

typedef struct
{
    MapCliffChunk chunks[MAX_CLIFF_CHUNKS];
    int chunkCount;
    Material material;
    Shader shader;
    bool ready;
} MapCliffSkirt;

MapCliffSkirt MapProp_CreateCliffSkirt(const MapGroundSurface *ground, Vector3 mapCenter, float sliceHeight, float bottomY);
void MapProp_DrawCliffSkirt(const MapCliffSkirt *cliff, Camera3D cam);
void MapProp_UnloadCliffSkirt(MapCliffSkirt *cliff);

typedef struct
{
    Model model;
    Shader shader;
    int sunLoc, skyLoc, hazeLoc, colorLoc;
    bool ready;
} MapSkyDome;
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

typedef enum
{
    MAP_MEADOW_GROWTH_AUTO = 0,
    MAP_MEADOW_GROWTH_GRASS,
    MAP_MEADOW_GROWTH_REED,
    MAP_MEADOW_GROWTH_PAMPAS
} MapMeadowGrowthForm;

typedef enum
{
    MAP_GRASS_STATE_LUSH = 0,
    MAP_GRASS_STATE_WITHERED = 1,
    MAP_GRASS_STATE_DRY = 2,
    MAP_GRASS_STATE_SCORCHED = 3
} MapGrassState;

typedef struct
{
    float dryness;
    float scorch;
    float emberIntensity;
    float patchVariation;
} MapGrassCondition;

typedef struct
{
    Color rootColor;
    Color tipColor;
    int bladesPerClump;
    int bladeSegments;
    float bladeWidthScale;
    float chunkSize;
    float lodDistance;
    float midLodDistance;
    float drawDistance;
    float shadowDistance;
    const char *texturePath;
    float alphaCutoff;
    bool hasPlumes;
    float botanicalVariation;
    MapMeadowGrowthForm growthForm;
    MapGrassState grassState;
    float dryness;
    float scorch;
} MapMeadowStyle;

typedef struct
{
    Model nearModel;
    Model midModel;
    Model farModel;
    Model shadowModel;
    Model realShadowModel;
    Vector3 center;
    float radius;
    int lodLevel;
    bool farLod;
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
    void *parametric;
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

typedef float (*MapFoliageDensityFn)(float x, float z, void *userData);

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData);

MapMeadowSurface MapProp_CreateMeadow(const MapMeadowPlacement *placements, int count,
                                      MapMeadowStyle style);
float MapProp_GetMacroFlowAngle(float x, float z);
void MapProp_BeginNatureInteraction(Vector3 focus, float dt);
void MapProp_AddNatureInteractor(Vector3 position, float radius, float strength);
void MapProp_AddNatureWindVorticles(float time);
void MapProp_EndNatureInteraction(void);
void MapProp_ClearNatureInteraction(void);
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
void MapProp_PrepareMeadow(MapMeadowSurface *meadow, Vector3 worldOffset);
void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength);
void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength);
void MapProp_UnloadMeadow(MapMeadowSurface *meadow);

// Danh mục 8 loài hoa dại Tiên Hiệp – Kiếm Hiệp
typedef enum
{
    MAP_FLOWER_SPECIES_MIXED = -1,     // Bách Hoa Cốc (Pha trộn tự nhiên theo quần thể)
    MAP_FLOWER_SPECIES_TUYET_LIEN = 0, // Bạch Cúc / Thiên Sơn Tuyết Liên
    MAP_FLOWER_SPECIES_KIM_CUC = 1,    // Hoàng Anh / Kim Cúc Cổ Phong
    MAP_FLOWER_SPECIES_BI_NGAN = 2,    // Chu Sa Hồng / Bỉ Ngạn Hoa
    MAP_FLOWER_SPECIES_U_LAN = 3,      // Lam Điệp / U Lan Linh Thảo
    MAP_FLOWER_SPECIES_DAO_THAO = 4,   // Đào Hoa / Vân Mộng Đào Thảo
    MAP_FLOWER_SPECIES_LINH_DANG = 5,  // Tử Mẫu / Tử Linh Đang
    MAP_FLOWER_SPECIES_HUAN_Y = 6,     // Huân Y Thảo / Tử Vân Anh
    MAP_FLOWER_SPECIES_NGOC_LAN = 7,   // Ngọc Lan / Linh Thảo
} MapFlowerSpeciesVariant;

typedef struct
{
    Vector3 position;
    float height;
    float bloomRadius;
    float rotationDeg;
    float phase;
    Color petalColor;
    unsigned char petalCount;
    unsigned char bloomVariant;
    float petalLengthScale;
} MapFlowerPlacement;

typedef struct
{
    Model model;
    Model farModel;
    Model shadowModel;
    bool textured;
    bool farReady;
    bool farLod;
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
void MapProp_SetFlowerFieldDrawDistance(MapFlowerField *field, float drawDistance);
void MapProp_SetFlowerFieldLod(MapFlowerField *field, float lodDistance, float shadowDistance);
void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength);
void MapProp_DrawFlowerFieldBatch(MapFlowerField *fields, int fieldCount, Vector3 worldOffset,
                                  float time, Vector2 windDirection, float windStrength);
void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength);
void MapProp_DrawFlowerFieldShadowCasterBatch(MapFlowerField *fields, int fieldCount,
                                              Vector3 worldOffset, float time,
                                              Vector2 windDirection, float windStrength);
void MapProp_UnloadFlowerField(MapFlowerField *field);

// Chế độ chọn loài hoa hiển thị trên toàn cánh đồng (-1 = Hỗn hợp tự nhiên, 0..7 = Đồng loạt 1 loài)
void MapProp_SetFlowerSpeciesOverride(int speciesVariant);
int MapProp_GetFlowerSpeciesOverride(void);

// --- Unified Flora System (Meadows, Reeds, Wildflower Colonies) ---------

typedef struct
{
    Vector3 center;
    Vector2 radius;
    int flowerCount;
    int primarySpecies;
    unsigned char species[4];
    Color primaryColor;
    Color accentColor;
    Color discalCenterColor;
} MapFloraFlowerCluster;

typedef struct
{
    Color grassRootColor;
    Color grassTipColor;
    int bladesPerClump;
    int bladeSegments;
    float bladeWidthScale;
    float lodDistance;
    float midLodDistance;
    float drawDistance;
    float shadowDistance;
    MapGrassState grassState;
    float dryness;
    float scorch;

    Color reedRootColor;
    Color reedTipColor;
    bool hasReedPlumes;
    float botanicalVariation;

    const MapMeadowPlacement *pampasPlacements;
    int pampasCount;
    Color pampasRootColor;
    Color pampasTipColor;
} MapFloraStyle;

typedef struct
{
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

typedef struct
{
    Vector2 minBounds;
    Vector2 maxBounds;
    float spacing;
    unsigned int seed;
    MapFoliageDensityFn densityFn;
    void *densityUserData;
    const MapEcology *ecology;
    const MapFloraFlowerCluster *flowerClusters;
    int flowerClusterCount;
    float (*edgeTaperFn)(float x, float z, void *userData);
    void *edgeTaperUserData;
} MapFloraMeadowConfig;

int MapFlora_GenerateMeadowPlacements(
    MapMeadowPlacement *outPlacements,
    int maxCount,
    const MapGroundSurface *ground,
    Vector3 groundCenter,
    MapFloraMeadowConfig config);

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

struct MapDemIsland;

typedef struct
{
    Vector2 span;
    float minWaterClearance;
    float maxWaterClearance;
    unsigned char minWetSplatAlpha;
    float maxWetClearance;
    float minSlopeNormalY;
    float minGroundY;
    float minClusterDistance;
    float minRadius;
    float maxRadius;
    float minHeight;
    float maxHeight;
    unsigned int seed;
    const struct MapDemIsland *dem;
    float (*sampleWaterHeight)(float x, float z, void *ctx);
    Color (*sampleSplat)(float x, float z, void *ctx);
    void *ctx;
} MapFloraReedConfig;

int MapFlora_GenerateReedPlacements(
    MapMeadowPlacement *outPlacements, int maxCount,
    const MapGroundSurface *ground, Vector3 groundCenter,
    MapFloraReedConfig config);

typedef struct
{
    Vector2 span;
    float clearingRadius;
    float minSlopeNormalY;
    float minGroundY;
    float minWaterClearance;
    unsigned char maxRockSplatGreen;
    float driftThreshold;
    float minRadius;
    float maxRadius;
    float baseHeight;
    const MapFloraFlowerCluster *flowerClusters;
    int flowerClusterCount;
    unsigned int seed;
    const struct MapDemIsland *dem;
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

// --- Debris System ------------------------------------------------------

typedef enum
{
    MAP_DEBRIS_LEAF = 0,
    MAP_DEBRIS_PEBBLE,
    MAP_DEBRIS_TWIG,
    MAP_DEBRIS_MOSS,
    MAP_DEBRIS_TYPE_COUNT
} MapDebrisType;

typedef struct
{
    Vector3 position;
    float rotationDeg;
    float scale;
    Color color;
    unsigned char type;
} MapDebrisPlacement;

typedef struct
{
    Model models[MAP_DEBRIS_TYPE_COUNT];
    bool modelLoaded[MAP_DEBRIS_TYPE_COUNT];
    MapDebrisPlacement *placements;
    int placementCount;
    float drawDistance;
    bool ready;
} MapDebrisSystem;

MapDebrisSystem MapProp_CreateDebrisSystem(
    const MapDebrisPlacement *placements, int count,
    const MapGroundSurface *ground, Vector3 worldCenter, float drawDistance);
void MapProp_DrawDebrisSystem(const MapDebrisSystem *debris, Vector3 cameraPosition);
void MapProp_DrawDebrisShadowCasters(const MapDebrisSystem *debris);
void MapProp_UnloadDebrisSystem(MapDebrisSystem *debris);

// --- Water System -------------------------------------------------------

typedef enum
{
    WATER_ECO_ALPINE_STREAM = 0,
    WATER_ECO_FOREST_SWAMP,
    WATER_ECO_TROPICAL_SHALLOW,
    WATER_ECO_STAGNANT_POND,
    WATER_ECO_CUSTOM
} MapWaterEcosystem;

typedef enum
{
    WATER_SHAPE_RADIAL = 0,
    WATER_SHAPE_RECT,
    WATER_SHAPE_STRIP,
    WATER_SHAPE_TERRAIN
} MapWaterShape;

typedef struct
{
    MapWaterShape shape;
    MapWaterEcosystem ecosystem;
    Vector3 center;
    float radiusX;
    float radiusZ;
    float bankWidth;
    float waveHeight;
    float waveScale;
    float waveSpeed;
    float bankGroundY;
    float detailScale;
    float detailStrength;
    int segments;
    int rings;
    unsigned int seed;
    Color deepColor;
    Color shallowColor;
    Color foamColor;
    Color bankInnerColor;
    Color bankOuterColor;
    float maxDepth;
    Vector3 absorption;
    Vector3 scatterColor;
    float scatterCoeff;
    Vector2 flowVelocity;
    float causticsStrength;
    float causticsScale;
    float foamThreshold;
    float refractionStrength;
} MapWaterConfig;

#define MAX_WATER_RIPPLES 4
#define MAX_WATER_OBSTACLES 4
#define WATER_FIELD_SIZE 128

typedef struct
{
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
    Model bedModel;
    MapWaterConfig config;
    Texture2D causticTex;
    Texture2D bedDiffuseTex;
    Vector3 interactorPos;
    Vector3 interactorVel;
    float interactorRadius;
    float interactorSubmerged;
    MapWaterRippleRing ripples[MAX_WATER_RIPPLES];
    int nextRipple;
    float lastTime;
    Vector4 obstacles[MAX_WATER_OBSTACLES];
    int obstacleCount;
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
    Vector3 boundsCenter;
    float boundsRadius;
    bool ready;
    unsigned char *waveUploadedPixels;
    unsigned int waveUploadedTextureId;
} MapWaterSurface;

MapWaterConfig MapProp_DefaultWaterConfig(MapWaterEcosystem eco);
MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config);
MapWaterSurface MapProp_CreateTerrainWater(const char *meshPath, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterStrip(Vector3 start, Vector3 end, float width, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterSpline(const Vector3 *points, const float *widths, int pointCount, MapWaterConfig config);
MapWaterSurface MapProp_CreateWaterPatch(Vector3 center, float width, float depth, MapWaterConfig config);
Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale);
bool MapProp_SampleWaterBed(const MapWaterSurface *water, float x, float z,
                            float *outBedHeight, Vector3 *outNormal);
void MapProp_SetWaterInteractor(MapWaterSurface *water, Vector3 position, Vector3 velocity,
                                float radius, float submerged);
void MapProp_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity);
void MapProp_AddWaterObstacle(MapWaterSurface *water, Vector3 position, float radius, float strength);
void MapProp_UpdateWaterSurface(MapWaterSurface *water, float dt);
void MapProp_DrawWaterBed(const MapWaterSurface *water, float time);
void MapProp_DrawWaterOverlay(const MapWaterSurface *water, float time);
void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time);
void MapProp_UnloadWaterSurface(MapWaterSurface *water);

// --- Atmospheric Fluff / Floating Spores System -------------------------

typedef struct
{
    Vector3 position;
    float phase;
    float size;
    float speed;
    float rot;
    float rotSpeed;
    float opacity;
} MapFluffParticle;

typedef struct
{
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

// --- DEM-based Island System --------------------------------------------

typedef struct MapDemIsland
{
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
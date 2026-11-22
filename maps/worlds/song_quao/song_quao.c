#include "song_quao.h"
#include "assets/maps/song_quao/song_quao_generated_meta.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "post_process/camera_context.h"
#include "post_process/color_grade_lut.h"
#include "post_process/post_fx.h"
#include "maps/map_manager.h"
#include "core/granular/granular_sim.h"
#include "core/resource_manager.h"
#include "environment/volumetric/volumetric_fog.h"
#include "environment/wind/wind_system.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static float s_scaleXZ = 1.0f;
static float s_scaleY = 1.0f;
static float s_mapWidth = SONG_QUAO_MAP_WIDTH;
static float s_mapDepth = SONG_QUAO_MAP_DEPTH;
static float s_heightRange = SONG_QUAO_HEIGHT_RANGE;

#define MAP_WIDTH s_mapWidth
#define MAP_DEPTH s_mapDepth
#define CLIFF_DEPTH (SONG_QUAO_CLIFF_DEPTH * s_scaleY)
#define CLOUD_SEA_Y (-5.20f * s_scaleY)

// Broad valley meadow coverage across the celestial island plateau and rolling hills
// Sized above total accepted plateau tufts (~58k) so reservoir sampling never evicts foreground tufts (LANDMINES.md §135)
#define GRASS_TUFT_CAPACITY 90000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 800
#define PAMPAS_COUNT 420

// Safe elevation floor above the sheer outer cliff abyss (-CLIFF_DEPTH)
#define PLAYABLE_BASIN_MIN_Y (-2.40f * s_scaleY)
#define PLAYABLE_BASIN_FADE_Y (-1.65f * s_scaleY)

// Plateau grass elevation & slope thresholds aligned with ground_splat.fs wCliff
#define GRASS_CLIFF_MIN_Y (-0.22f * s_scaleY)
#define GRASS_CLIFF_FADE_Y (-0.05f * s_scaleY)
#define GRASS_MIN_NORMAL_Y 0.76f
#define GRASS_FULL_NORMAL_Y 0.86f

// Shadow focus quantization & hysteresis to prevent boundary flip-flop shimmering
#define SHADOW_SNAP_STEP 2.0f
#define SHADOW_HYSTERESIS_XZ 1.50f
#define SHADOW_HYSTERESIS_Y 0.50f
#define SHADOW_FOCUS_RADIUS 18.0f

static Vector3 kMapCenter = {SONG_QUAO_MAP_WIDTH * 0.5f, 0.0f, SONG_QUAO_MAP_DEPTH * 0.5f};

static MapZone s_songQuaoZones[3];
#define SONG_QUAO_ZONE_COUNT 3

static MapRockItem s_scaledRocks[SONG_QUAO_ROCK_ITEM_COUNT];
static MapFloraFlowerCluster s_scaledFlowerClusters[SONG_QUAO_FLOWER_CLUSTER_COUNT];
static Vector3 s_floraWindowCenter = {0};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};

// --- Map Scenery & Scaffolding ---
static MapDemIsland s_dem;
static MapGroundSurface s_ground;
static MapRockSystem s_rocks;
static MapFloraSystem s_flora;
static MapSkyDome s_sky;
static MapCloudSea s_cloudSea;
static MapCliffSkirt s_cliff;
static MapWaterSurface s_river;
static MapEcology s_habitat;
static MapFluffSystem s_fluff;
static MapTreeSystem s_trees;
static WindMacroConfig s_prevWindMacro = {0};

static MapMeadowPlacement *s_grassPlacements = NULL;
static MapFlowerPlacement *s_flowerPlacements = NULL;
static MapMeadowPlacement *s_reedPlacements = NULL;
static MapMeadowPlacement *s_pampasPlacements = NULL;
static int s_grassCount = 0;
static int s_reedCount = 0;
static int s_pampasCount = 0;
static int s_flowerCount = 0;

static Vector3 s_lastShadowFocus = {-9999.0f, 0.0f, -9999.0f};
static bool s_shadowWasEnabled = false;
static bool s_ready = false;
static unsigned int s_skipMask = 0;
static Vector3 s_playerPos = {0};
static Vector3 s_playerVel = {0};
static float s_playerRadius = 0.0f;
static bool s_hasPlayerInteractor = false;

#define SHORE_FIELD_MAX_PIXELS (512 * 768)
static unsigned char s_shoreField[SHORE_FIELD_MAX_PIXELS];
static int s_shoreFieldW = 0;
static int s_shoreFieldH = 0;

static float SongQuaoSampleSignedShore(float x, float z)
{
    if (s_shoreFieldW <= 1 || s_shoreFieldH <= 1)
        return 2.4f;
    float u = x * (1.0f / MAP_WIDTH);
    float v = z * (1.0f / MAP_DEPTH);
    u = (u > 0.0f) ? ((u < 1.0f) ? u : 1.0f) : 0.0f;
    v = (v > 0.0f) ? ((v < 1.0f) ? v : 1.0f) : 0.0f;
    float fx = u * (float)(s_shoreFieldW - 1);
    float fy = v * (float)(s_shoreFieldH - 1);
    int x0 = (int)fx;
    int y0 = (int)fy;
    int x1 = (x0 + 1 < s_shoreFieldW) ? (x0 + 1) : x0;
    int y1 = (y0 + 1 < s_shoreFieldH) ? (y0 + 1) : y0;
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    float s00 = (float)s_shoreField[y0 * s_shoreFieldW + x0];
    float s10 = (float)s_shoreField[y0 * s_shoreFieldW + x1];
    float s01 = (float)s_shoreField[y1 * s_shoreFieldW + x0];
    float s11 = (float)s_shoreField[y1 * s_shoreFieldW + x1];
    float s0 = s00 + (s10 - s00) * tx;
    float s1 = s01 + (s11 - s01) * tx;
    float encShore = (s0 + (s1 - s0) * ty) * (1.0f / 255.0f);
    return encShore * 5.5f - 3.1f;
}

// --- Map-Specific Flora Biome Shaping ---

static float SongQuaoGrassDensity(float x, float z, void *userData)
{
    (void)userData;

    Vector3 pos, normal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &pos, &normal))
        return 0.0f;

    // [VISUAL FIX]: Stop grass cleanly at the plateau rim and steep cliff scarps
    // so tufts never spill down the vertical triplanar rock walls.
    if (pos.y < GRASS_CLIFF_MIN_Y || normal.y < GRASS_MIN_NORMAL_Y)
        return 0.0f;

    // 1. Lake water absolute clearance (radial basin)
    const float invLakeRadX = 1.0f / (SONG_QUAO_LAKE_RADIUS_X * s_scaleXZ);
    const float invLakeRadZ = 1.0f / (SONG_QUAO_LAKE_RADIUS_Z * s_scaleXZ);
    float dxLake = (x - SONG_QUAO_LAKE_CENTER_X * s_scaleXZ) * invLakeRadX;
    float dzLake = (z - SONG_QUAO_LAKE_CENTER_Z * s_scaleXZ) * invLakeRadZ;
    if (dxLake * dxLake + dzLake * dzLake <= (1.22f * 1.22f) && pos.y <= SONG_QUAO_LAKE_WATER_Y * s_scaleY + 0.04f)
        return 0.0f;

    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    // Gate scree/cliff rejection strictly by actual steep slope (outer rim vs interior grassy banks)
    float geomSlope = 1.0f - normal.y;
    float rimSlopeFactor = 1.0f - MapMath_SmoothStep01(-1.10f, 0.15f, pos.y);
    float slopeScreeGate = fmaxf(MapMath_SmoothStep01(0.44f, 0.58f, geomSlope),
                                 rimSlopeFactor * MapMath_SmoothStep01(0.24f, 0.38f, geomSlope));
    float rockScree = ((float)splat.g * (1.0f / 255.0f)) * slopeScreeGate;
    if (rockScree > 0.72f)
        return 0.0f;

    // Exact metric signed shoreline distance matching ground_splat.fs:
    // Multi-octave organic bank setback (macro meanders + medium scallops + micro promontories)
    // so grass clusters into natural rosettes instead of an artificial parallel wall.
    float signedShore = SongQuaoSampleSignedShore(x, z);
    float bankNoiseMacro = sinf(x * 0.28f + z * 0.22f) * 0.28f + cosf(z * 0.48f - x * 0.36f) * 0.18f;
    float bankNoiseMed   = sinf(x * 1.15f - z * 0.88f) * 0.12f + cosf(z * 1.55f + x * 1.25f) * 0.08f;
    float bankNoiseMicro = sinf(x * 3.40f + z * 2.80f) * 0.05f + cosf(z * 4.60f - x * 3.80f) * 0.03f;
    float rosetteGrain   = sinf(x * 9.2f + z * 6.8f) * cosf(z * 8.4f - x * 10.1f);
    float rosetteJut     = sinf(x * 1.85f + 1.2f) * cosf(z * 1.75f - 0.8f);

    // Dynamic bank setback (GPU Gems 3 Continuous Riverbank Ecology):
    // Reeds & riverbank sedges grow right down into the waterline (-0.04m .. 0.02m);
    // Gravel bars & pebble beaches transition smoothly without harsh empty trenches
    float bankThreshold = -0.02f - rosetteJut * 0.03f + rosetteGrain * 0.015f;
    float varShore = signedShore + bankNoiseMacro + bankNoiseMed + bankNoiseMicro;
    if (varShore < bankThreshold)
        return 0.0f;
    float shoreFade = MapMath_SmoothStep01(bankThreshold, bankThreshold + 0.24f, varShore);

    float rockSuppression = MapProp_EvaluateRockSuppression(
        x, z, s_scaledRocks, SONG_QUAO_ROCK_ITEM_COUNT);
    if (rockSuppression <= 0.001f)
        return 0.0f;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, s_scaledFlowerClusters, SONG_QUAO_FLOWER_CLUSTER_COUNT);
    if (flowerSuppression <= 0.001f)
        return 0.0f;

    float heightFade = MapMath_SmoothStep01(GRASS_CLIFF_MIN_Y, GRASS_CLIFF_FADE_Y, pos.y);
    float slopeFade = MapMath_SmoothStep01(GRASS_MIN_NORMAL_Y, GRASS_FULL_NORMAL_Y, normal.y);
    float rockFade = 1.0f - MapMath_SmoothStep01(0.45f, 0.70f, rockScree);

    // Coherent macro meadow swells: multi-scale colonies (lush core 0.70..1.0, transition turf 0.50..0.70)
    float warpedX = x + sinf(z * 0.12f) * 2.8f;
    float warpedZ = z + cosf(x * 0.11f) * 2.4f;
    float macroField = sinf(warpedX * 0.072f + warpedZ * 0.046f) * 0.55f +
                       sinf(warpedZ * 0.105f - warpedX * 0.038f + 1.2f) * 0.30f +
                       sinf(warpedX * 0.18f + warpedZ * 0.14f) * 0.15f;

    float dxBasin = x - 95.0f;
    float dzBasin = z - 150.0f;
    float distBasin = sqrtf(dxBasin * dxBasin + dzBasin * dzBasin);
    float outerHillBlend = MapMath_SmoothStep01(34.0f, 58.0f, distBasin);

    // Continuous, lush turf colony drifts without artificial bald clearing holes
    float patch = MapMath_SmoothStep01(-0.35f, 0.45f, macroField);
    float colonyDensity = 0.72f + 0.28f * patch;
    float outerHillFactor = 1.0f - outerHillBlend * 0.18f;
    float macro = colonyDensity * outerHillFactor;

    float density = macro * heightFade * slopeFade * shoreFade *
                    rockFade * flowerSuppression * rockSuppression;
    if (s_scaleXZ > 1.5f)
    {
        float fdx = x - s_floraWindowCenter.x;
        float fdz = z - s_floraWindowCenter.z;
        float fdist = sqrtf(fdx * fdx + fdz * fdz);
        float radialGrade = 1.0f - MapMath_SmoothStep01(68.0f, 155.0f, fdist) * 0.64f;
        density *= radialGrade;
    }
    return (density > 0.0f) ? ((density < 1.0f) ? density : 1.0f) : 0.0f;
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    Vector3 pos, normal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, cx, cz, &pos, &normal))
        return 0.0f;

    if (pos.y <= GRASS_CLIFF_MIN_Y || normal.y <= GRASS_MIN_NORMAL_Y)
        return 0.0f;

    // 4-direction cliff-drop setback so plateau rim grass blades never overhang the abyss lip.
    static const float kDirX[4] = {1.0f, -1.0f, 0.0f, 0.0f};
    static const float kDirZ[4] = {0.0f, 0.0f, 1.0f, -1.0f};
    float rimTaper = 1.0f;
    for (int d = 0; d < 4; d++)
    {
        Vector3 pNear, nNear;
        bool okNear = MapProp_SampleGroundSurface(&s_ground, kMapCenter, cx + kDirX[d] * 0.42f, cz + kDirZ[d] * 0.42f,
                                                  &pNear, &nNear);
        if (!okNear || pNear.y <= GRASS_CLIFF_MIN_Y || (pos.y - pNear.y > 0.40f && (pNear.y < -0.30f || nNear.y < 0.60f)))
            return 0.0f;

        Vector3 pFar, nFar;
        bool okFar = MapProp_SampleGroundSurface(&s_ground, kMapCenter, cx + kDirX[d] * 0.78f, cz + kDirZ[d] * 0.78f,
                                                 &pFar, &nFar);
        if (!okFar || pFar.y <= GRASS_CLIFF_MIN_Y || (pos.y - pFar.y > 0.55f && (pFar.y < -0.35f || nFar.y < 0.62f)))
            rimTaper = 0.48f;
    }

    float signedShore = SongQuaoSampleSignedShore(cx, cz);
    float shoreTaper = (signedShore >= 1.15f)
                           ? 1.0f
                           : (0.12f + 0.88f * MapMath_SmoothStep01(0.05f, 1.15f, signedShore));
    float cliffTaper = (pos.y >= -0.20f)
                           ? 1.0f
                           : (0.35f + 0.65f * MapMath_SmoothStep01(GRASS_CLIFF_MIN_Y, -0.20f, pos.y));
    float slopeTaper = (normal.y >= 0.72f)
                           ? 1.0f
                           : (0.68f + 0.32f * MapMath_SmoothStep01(GRASS_MIN_NORMAL_Y, 0.72f, normal.y));
    float rockSupp = MapProp_EvaluateRockSuppression(
        cx, cz, s_scaledRocks, SONG_QUAO_ROCK_ITEM_COUNT);
    float rockTaper = 0.20f + 0.80f * rockSupp;
    return shoreTaper * cliffTaper * slopeTaper * rockTaper * rimTaper;
}

// Terrain-water SWE sources: real DEM bed and the baked steady water-surface elevation.
static float SongQuaoWaterBed(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static float SongQuaoWaterSurface(float x, float z, void *ctx)
{
    (void)ctx;
    float surfaceY = MapDem_SampleWaterHeight(&s_dem, x, z);
    return (surfaceY <= -s_dem.cliffDepth + 0.05f) ? -1e9f : surfaceY;
}

static float SongQuaoSampleGroundHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static bool SongQuaoIsFlowerExcluded(float x, float z, void *ctx)
{
    (void)ctx;
    Vector3 pos, normal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &pos, &normal))
        return true;
    if (pos.y < -0.65f || normal.y < 0.62f)
        return true;
    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    float geomSlope = 1.0f - normal.y;
    float rockScree = ((float)splat.g * (1.0f / 255.0f)) * MapMath_SmoothStep01(0.38f, 0.52f, geomSlope);
    if (rockScree > 0.65f || splat.b > 110)
        return true;
    if (SongQuaoSampleSignedShore(x, z) < 0.20f)
        return true;
    if (MapProp_EvaluateRockSuppression(x, z, s_scaledRocks, SONG_QUAO_ROCK_ITEM_COUNT) < 0.65f)
        return true;
    return false;
}

static void FreeFloraScratch(void)
{
    if (s_grassPlacements) { MemFree(s_grassPlacements); s_grassPlacements = NULL; }
    if (s_flowerPlacements) { MemFree(s_flowerPlacements); s_flowerPlacements = NULL; }
    if (s_reedPlacements) { MemFree(s_reedPlacements); s_reedPlacements = NULL; }
    if (s_pampasPlacements) { MemFree(s_pampasPlacements); s_pampasPlacements = NULL; }
}

static void BuildFloraPlacements(void)
{
    FreeFloraScratch();
    int reedCap = (s_scaleXZ > 1.5f) ? 2200 : REED_COUNT;
    int pampasCap = (s_scaleXZ > 1.5f) ? 1200 : PAMPAS_COUNT;
    s_grassPlacements = (MapMeadowPlacement *)MemAlloc((size_t)GRASS_TUFT_CAPACITY * sizeof(MapMeadowPlacement));
    s_flowerPlacements = (MapFlowerPlacement *)MemAlloc((size_t)FLOWER_COUNT * sizeof(MapFlowerPlacement));
    s_reedPlacements = (MapMeadowPlacement *)MemAlloc((size_t)reedCap * sizeof(MapMeadowPlacement));
    s_pampasPlacements = (MapMeadowPlacement *)MemAlloc((size_t)pampasCap * sizeof(MapMeadowPlacement));
    if (!s_grassPlacements || !s_flowerPlacements || !s_reedPlacements || !s_pampasPlacements)
    {
        FreeFloraScratch();
        return;
    }
    Vector2 minB = {10.0f, 8.0f};
    Vector2 maxB = {MAP_WIDTH - 10.0f, MAP_DEPTH - 8.0f};
    float spacing = 0.30f;
    if (s_scaleXZ > 1.5f)
    {
        float halfWin = 160.0f;
        minB.x = fmaxf(10.0f, s_floraWindowCenter.x - halfWin);
        minB.y = fmaxf(8.0f, s_floraWindowCenter.z - halfWin);
        maxB.x = fminf(MAP_WIDTH - 10.0f, s_floraWindowCenter.x + halfWin);
        maxB.y = fminf(MAP_DEPTH - 8.0f, s_floraWindowCenter.z + halfWin);
        spacing = 0.52f;
    }
    // 1. Lush interlocking meadow across the floating island plateau
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = minB,
            .maxBounds = maxB,
            .spacing = spacing,
            .seed = 0x51a7c3u,
            .densityFn = SongQuaoGrassDensity,
            .ecology = &s_habitat,
            .flowerClusters = s_scaledFlowerClusters,
            .flowerClusterCount = SONG_QUAO_FLOWER_CLUSTER_COUNT,
            .edgeTaperFn = SongQuaoEdgeTaper,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

    // 2. Reeds along riverbanks & wetland margins inside the valley basin
    float spanX = (s_scaleXZ > 1.5f) ? 180.0f : (MAP_WIDTH * 0.38f);
    float spanZ = (s_scaleXZ > 1.5f) ? 180.0f : (MAP_DEPTH * 0.46f);
    s_reedCount = MapFlora_GenerateReedPlacements(
        s_reedPlacements, reedCap, &s_ground, kMapCenter,
        (MapFloraReedConfig){
            .span = {spanX, spanZ},
            .dem = &s_dem,
            .minWaterClearance = 0.005f,
            .maxWaterClearance = 0.34f,
            .minWetSplatAlpha = 82,
            .maxWetClearance = 0.46f,
            .minClusterDistance = 0.085f * s_scaleXZ,
            .seed = 0x9371a5u,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_REEDS: placements=%d capacity=%d",
             s_reedCount, reedCap);

    // 3. Clustered Hillside Silvergrass / Pampas Drifts (Ghost of Tsushima style)
    s_pampasCount = MapFlora_GeneratePampasPlacements(
        s_pampasPlacements, pampasCap, &s_ground, kMapCenter,
        (MapFloraPampasConfig){
            .span = {spanX, spanZ},
            .clearingRadius = 11.5f * s_scaleXZ,
            .dem = &s_dem,
            .flowerClusters = s_scaledFlowerClusters,
            .flowerClusterCount = SONG_QUAO_FLOWER_CLUSTER_COUNT,
            .seed = 0x9371a5u,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_PAMPAS: placements=%d capacity=%d",
             s_pampasCount, pampasCap);

    // 4. Multi-species Wildflower Colonies
    MapFlora_GenerateFlowerPlacements(
        s_flowerPlacements, FLOWER_COUNT,
        s_scaledFlowerClusters, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        SongQuaoSampleGroundHeight, SongQuaoIsFlowerExcluded, NULL, 0x9371a5u);
    s_flowerCount = FLOWER_COUNT;
}

static void BuildRocks(void)
{
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rocks/granite_diffuse.png",
        "assets/textures/rocks/granite_normal.png",
        "assets/textures/rocks/granite_roughness.png",
        SONG_QUAO_ROCK_ITEM_COUNT);

    for (int i = 0; i < SONG_QUAO_ROCK_ITEM_COUNT; i++)
    {
        s_scaledRocks[i] = SONG_QUAO_ROCK_ITEMS[i];
        s_scaledRocks[i].position.x *= s_scaleXZ;
        s_scaledRocks[i].position.z *= s_scaleXZ;
        s_scaledRocks[i].radiusScale *= (1.0f + 0.55f * (s_scaleXZ - 1.0f));
        s_scaledRocks[i].heightScale *= (1.0f + 0.25f * (s_scaleY - 1.0f));
    }

    MapProp_PopulateRockItems(&s_rocks, &s_ground, kMapCenter,
                              s_scaledRocks, SONG_QUAO_ROCK_ITEM_COUNT);
}

static void UpdateSongQuaoWaterObstacles(Vector3 focusPos)
{
    s_river.obstacleCount = 0;
    Vector2 registered[MAX_WATER_OBSTACLES];
    int regCount = 0;

    for (int i = 0; i < s_rocks.count && regCount < MAX_WATER_OBSTACLES; i++)
    {
        const MapRockItem *rock = &s_rocks.items[i];
        if (rock->radiusScale < 0.35f)
            continue;
        float dx = rock->position.x - focusPos.x;
        float dz = rock->position.z - focusPos.z;
        if (dx * dx + dz * dz > (48.0f * 48.0f))
            continue;

        bool tooClose = false;
        for (int r = 0; r < regCount; r++)
        {
            float sepX = rock->position.x - registered[r].x;
            float sepZ = rock->position.z - registered[r].y;
            if (sepX * sepX + sepZ * sepZ < (1.2f * 1.2f))
            {
                tooClose = true;
                break;
            }
        }
        if (tooClose)
            continue;

        float surfaceY = 0.0f, waterDepth = 0.0f;
        if (GetWaterInfoSongQuaoMap(rock->position.x, rock->position.z, &surfaceY, &waterDepth) &&
            waterDepth > 0.02f)
        {
            MapProp_AddWaterObstacle(&s_river, rock->position, rock->radiusScale * 0.95f, 0.70f);
            registered[regCount++] = (Vector2){rock->position.x, rock->position.z};
        }
    }
}

static void SpawnSongQuaoMistVolumes(void)
{
    FogVolume_ClearAll();

    // 1. Upper mountain river cascade mist (hovering along the rocky torrent)
    LocalFogVolume upperGorgeMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {104.0f, 6.35f, 75.0f},
        .extents = {14.0f, 0.65f, 22.0f},
        .color = {244, 238, 228, 255},
        .density = 0.048f,
        .edgeSoftness = 0.92f,
        .driftVelocity = {0.015f, 0.0f, 0.02f},
        .active = true};
    FogVolume_Create(&upperGorgeMist);

    // 2. Lower river bend valley mist
    LocalFogVolume riverValleyMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {114.0f, 0.45f, 135.0f},
        .extents = {22.0f, 0.55f, 18.0f},
        .color = {244, 238, 228, 255},
        .density = 0.052f,
        .edgeSoftness = 0.92f,
        .driftVelocity = {0.02f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&riverValleyMist);

    // 3. Poetic celestial lake basin mist
    LocalFogVolume lakeBasinMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {SONG_QUAO_LAKE_CENTER_X, 0.28f, SONG_QUAO_LAKE_CENTER_Z},
        .extents = {SONG_QUAO_LAKE_RADIUS_X * 1.45f, 0.48f, SONG_QUAO_LAKE_RADIUS_Z * 1.45f},
        .color = {242, 236, 226, 255},
        .density = 0.056f,
        .edgeSoftness = 0.92f,
        .driftVelocity = {0.02f, 0.0f, 0.015f},
        .active = true};
    FogVolume_Create(&lakeBasinMist);

    // 4. Central plateau glade mist
    LocalFogVolume centralMeadowMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {94.0f, 0.42f, 150.0f},
        .extents = {16.0f, 0.45f, 12.0f},
        .color = {246, 240, 230, 255},
        .density = 0.042f,
        .edgeSoftness = 0.92f,
        .driftVelocity = {0.015f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&centralMeadowMist);

    // 5. Cloud waterfall abyss outlet plume
    LocalFogVolume outletWaterfallMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {SONG_QUAO_OUTLET_X, -0.40f, SONG_QUAO_OUTLET_Z - 1.5f},
        .extents = {16.0f, 1.65f, 14.0f},
        .color = {244, 238, 228, 255},
        .density = 0.13f,
        .edgeSoftness = 0.92f,
        .driftVelocity = {0.0f, -0.02f, 0.04f},
        .active = true};
    FogVolume_Create(&outletWaterfallMist);
}

static void DrawSongQuaoShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    if (!(s_skipMask & MAP_SKIP_SHADOWCASTERS) && !(s_skipMask & MAP_SKIP_FLORA))
    {
        MapProp_DrawFloraShadowCasters(&s_flora);
    }
}

static void CaptureSongQuaoStaticShadows(void)
{
    float staticRadius = (s_scaleXZ > 1.5f) ? 140.0f : 96.0f;
    Vector3 center = (s_scaleXZ > 1.5f) ? s_staticShadowCenter : kMapCenter;
    MapDem_CaptureStaticShadows(&s_rocks, center, staticRadius, s_skipMask);
}

void InitSongQuaoMap(void)
{
    s_scaleXZ = MapManager_GetWorldScaleXZ();
    s_scaleY  = MapManager_GetWorldScaleY();
    s_mapWidth = SONG_QUAO_MAP_WIDTH * s_scaleXZ;
    s_mapDepth = SONG_QUAO_MAP_DEPTH * s_scaleXZ;
    s_heightRange = SONG_QUAO_HEIGHT_RANGE * s_scaleY;
    kMapCenter = (Vector3){s_mapWidth * 0.5f, 0.0f, s_mapDepth * 0.5f};
    s_floraWindowCenter = kMapCenter;
    s_staticShadowCenter = kMapCenter;

    MapManager_SetActiveMetadata(&(MapSpatialMetadata){
        .spawnPoint = {94.15f * s_scaleXZ, 0.0f, 150.0f * s_scaleXZ},
        .arenaCenter = {94.15f * s_scaleXZ, 0.0f, 150.0f * s_scaleXZ},
        .arenaRadius = 135.0f * s_scaleXZ,
        .hasPlayableRect = true,
        .minX = 6.0f * s_scaleXZ, .maxX = 182.0f * s_scaleXZ,
        .minZ = 6.0f * s_scaleXZ, .maxZ = 294.0f * s_scaleXZ,
    });

    s_songQuaoZones[0] = (MapZone){NAT_RIVER, {SONG_QUAO_LAKE_CENTER_X * s_scaleXZ, 0.0f, SONG_QUAO_LAKE_CENTER_Z * s_scaleXZ}, SONG_QUAO_LAKE_RADIUS_X * s_scaleXZ};
    s_songQuaoZones[1] = (MapZone){NAT_FOREST, {94.15f * s_scaleXZ, 0.0f, 150.0f * s_scaleXZ}, 35.0f * s_scaleXZ};
    s_songQuaoZones[2] = (MapZone){NAT_FOREST, {110.0f * s_scaleXZ, 0.0f, 100.0f * s_scaleXZ}, 25.0f * s_scaleXZ};

    for (int i = 0; i < SONG_QUAO_FLOWER_CLUSTER_COUNT; i++)
    {
        s_scaledFlowerClusters[i] = SONG_QUAO_FLOWER_CLUSTERS[i];
        s_scaledFlowerClusters[i].center.x *= s_scaleXZ;
        s_scaledFlowerClusters[i].center.z *= s_scaleXZ;
        s_scaledFlowerClusters[i].radius.x *= (1.0f + 0.22f * (s_scaleXZ - 1.0f));
        s_scaledFlowerClusters[i].radius.y *= (1.0f + 0.22f * (s_scaleXZ - 1.0f));
    }
    // Apply dedicated highland valley wind profile
    s_prevWindMacro = Wind_GetMacro();
    WindMacroConfig sqWind = {
        .baseDirection = {3.20f, 0.0f, 1.65f},
        .gustAmplitude = 1.45f,
        .noiseScale = 0.058f,
        .noiseSpeed = 1.65f,
        .terrainLiftK = 1.45f,
        .heightGradientK = 0.72f,
    };
    Wind_SetMacro(&sqWind);

    // Enable s_cliff skirt: sculpted 17-row 3D geological cliff mesh with
    // overhanging turf lip, eroded sub-soil band, and inward floating keel.
    s_skipMask = MapProp_GetSkipMask();
    if (s_skipMask & MAP_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else
        ColorGradeLut_Load("assets/maps/song_quao/song_quao_grade.png");

    // Ghost of Tsushima / Where Winds Meet dynamic celestial floating island ToD curve
    static const EnvLightingPreset kSongQuaoToD[5] = {
        // 06:00 Misty Dawn: soft rose-gold sun, cool azure-violet ambient
        {
            .ambientColor = {80, 95, 130, 255},
            .sunColor = {255, 185, 140, 255},
            .sunDirection = {0.85f, -0.22f, -0.46f},
            .shadowColor = {40, 52, 75, 130},
            .fog = {.enabled = true, .color = {230, 220, 215, 255}, .start = 15.0f, .end = 150.0f, .density = 0.012f},
            .atmosphere = {
                .color = {230, 220, 215, 255}, .start = 15.0f, .end = 150.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0095f, 0.0165f, 0.0310f}, .mieScattering = 0.0055f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 2.4f},
                .density = {.baseDensity = 0.012f, .heightFalloff = 0.28f, .baseAltitude = -1.5f, .enableSigmoidLayer = true, .layerAltitude = 2.4f, .layerThickness = 3.4f, .layerDensity = 0.006f}
            }
        },
        // 12:00 High Celestial Noon: brilliant white-gold overhead sun, lush sky ambient
        {
            .ambientColor = {110, 130, 155, 255},
            .sunColor = {255, 246, 220, 255},
            .sunDirection = {0.20f, -0.95f, -0.22f},
            .shadowColor = {48, 65, 88, 150},
            .fog = {.enabled = true, .color = {225, 235, 245, 255}, .start = 22.0f, .end = 180.0f, .density = 0.008f},
            .atmosphere = {
                .color = {225, 235, 245, 255}, .start = 22.0f, .end = 180.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0080f, 0.0140f, 0.0260f}, .mieScattering = 0.0035f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.009f, .heightFalloff = 0.24f, .baseAltitude = -1.5f, .enableSigmoidLayer = true, .layerAltitude = 2.4f, .layerThickness = 3.4f, .layerDensity = 0.004f}
            }
        },
        // 16:30 Golden Hour (Primary Artistic Target - Raking Warm Sun & Jade Shadows)
        {
            .ambientColor = {96, 122, 156, 255},
            .sunColor = {255, 202, 138, 255},
            .sunDirection = {0.58f, -0.285f, -0.76f},
            .shadowColor = {40, 58, 78, 140},
            .fog = {.enabled = true, .color = {236, 228, 218, 255}, .start = 18.0f, .end = 165.0f, .density = 0.0112f},
            .atmosphere = {
                .color = {236, 228, 218, 255}, .start = 18.0f, .end = 165.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0088f, 0.0152f, 0.0285f}, .mieScattering = 0.0042f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.25f},
                .density = {.baseDensity = 0.0112f, .heightFalloff = 0.26f, .baseAltitude = -1.5f, .enableSigmoidLayer = true, .layerAltitude = 2.4f, .layerThickness = 3.4f, .layerDensity = 0.0052f}
            }
        },
        // 18:30 Crimson Sunset: deep orange-red horizon sun, slate-blue shadows
        {
            .ambientColor = {65, 80, 115, 255},
            .sunColor = {255, 145, 85, 255},
            .sunDirection = {-0.88f, -0.18f, -0.44f},
            .shadowColor = {35, 45, 68, 140},
            .fog = {.enabled = true, .color = {245, 195, 165, 255}, .start = 14.0f, .end = 140.0f, .density = 0.014f},
            .atmosphere = {
                .color = {245, 195, 165, 255}, .start = 14.0f, .end = 140.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0120f, 0.0185f, 0.0350f}, .mieScattering = 0.0068f, .mieAnisotropy = 0.85f, .multipleScatteringAmp = 2.6f},
                .density = {.baseDensity = 0.014f, .heightFalloff = 0.30f, .baseAltitude = -1.5f, .enableSigmoidLayer = true, .layerAltitude = 2.4f, .layerThickness = 3.4f, .layerDensity = 0.007f}
            }
        },
        // 22:00 Midnight Moonlight: crisp silver-cyan lunar key light, deep indigo shadows & mist
        {
            .ambientColor = {34, 52, 92, 255},
            .sunColor = {192, 218, 255, 255},
            .sunDirection = {0.48f, -0.46f, -0.74f},
            .shadowColor = {8, 14, 32, 200},
            .fog = {.enabled = true, .color = {20, 30, 62, 255}, .start = 16.0f, .end = 155.0f, .density = 0.0150f},
            .atmosphere = {
                .color = {20, 30, 62, 255}, .start = 16.0f, .end = 155.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0045f, 0.0085f, 0.0220f}, .mieScattering = 0.0048f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 1.85f},
                .density = {.baseDensity = 0.0135f, .heightFalloff = 0.28f, .baseAltitude = -1.5f, .enableSigmoidLayer = true, .layerAltitude = 2.4f, .layerThickness = 3.4f, .layerDensity = 0.0060f}
            }
        }
    };
    static const float kSongQuaoTimes[5] = {0.25f, 0.50f, 0.6875f, 0.7708f, 0.92f};
    Environment_SetTimeOfDayPresets(kSongQuaoToD, kSongQuaoTimes, 5);
    Environment_SetTimeOfDay(0.6875f); // 16:30 default
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){82, 110, 148, 255});
    Environment_SetSunColor((Color){255, 204, 142, 255});
    Environment_SetSunIntensity(2.35f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.58f, -0.285f, -0.76f}));
    Environment_SetShadowColor((Color){32, 48, 72, 158});
    if (Environment_IsNightMode())
        Environment_ApplyMoonlightNight();

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.04f,
        .contrast = 1.12f,
        .saturation = 1.10f,
        .bloomIntensity = 0.20f,
        .shadowTint = {0.92f, 0.96f, 1.05f},    // Cool jade-slate shadows
        .highlightTint = {1.06f, 1.01f, 0.91f}, // Warm golden amber sun highlights
        .bloomThreshold = 1.06f,                // Catches water glints, cloud crests & grass SSS
        .bloomKnee = 0.42f,
        .hueRestore = 0.65f,                    // Filmic hue restore: prevents neon lime skew under ACES
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);

    AtmosphereProfile atmos = {
        .color = {236, 228, 218, 255},
        .start = 18.0f,
        .end = 165.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0088f, 0.0152f, 0.0285f},
            .mieScattering = 0.0042f,
            .mieAnisotropy = 0.80f,
            .multipleScatteringAmp = 2.25f},
        .density = {
            .baseDensity = 0.0112f,
            .heightFalloff = 0.26f,
            .baseAltitude = -1.5f,
            .enableSigmoidLayer = true,
            .layerAltitude = 2.4f,
            .layerThickness = 3.4f,
            .layerDensity = 0.0052f}};
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity((s_skipMask & MAP_SKIP_GODRAYS) ? 0.0f : 0.85f);
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.42f,
        .worldSize = 135.0f,
        .planeHeight = 80.0f,
        .coverage = 0.42f,
        .softness = 0.28f,
        .windSpeedScale = 0.75f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);
    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnSongQuaoMistVolumes();

    MapProp_SetGroundBiomeProfile(NULL);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetGrassState(MAP_GRASS_STATE_LUSH, 0.0f);

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        if (s_scaleXZ > 1.5f)
    {
        float sdx = camera.target.x - s_staticShadowCenter.x;
        float sdz = camera.target.z - s_staticShadowCenter.z;
        if (sdx * sdx + sdz * sdz > 70.0f * 70.0f)
        {
            s_staticShadowCenter = camera.target;
            EnvShadow_InvalidateStaticCache();
        }
    }
    CaptureSongQuaoStaticShadows();
        MapManager_SetZones(s_songQuaoZones, SONG_QUAO_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = kMapCenter;
    EnvShadow_SetFocus(kMapCenter, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/song_quao/song_quao_ecology_splat.png",
                      "assets/maps/song_quao/song_quao_water_height.png");

    s_ground = MapProp_CreateGroundHeightfield16(
        "assets/maps/song_quao/song_quao_heightfield.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 3.6f,
        "assets/maps/song_quao/song_quao_ecology_splat.png",
        "assets/textures/verdant_meadow_substrate_diffuse.png",
        "assets/textures/dirt_diffuse.png");

    MapEcology_LoadBakedAssets(&s_habitat,
                               "assets/maps/song_quao/song_quao_habitat.png",
                               "assets/maps/song_quao/song_quao_habitat_distance.png",
                               (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH},
                               s_shoreField, SHORE_FIELD_MAX_PIXELS,
                               &s_shoreFieldW, &s_shoreFieldH);
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    // Rich forest-moss substrate tint harmonized with grass roots & distant canopy
    MapProp_SetGroundTint(&s_ground, (Color){42, 58, 28, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/verdant_meadow_substrate_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rock_diffuse.png",
                              "assets/textures/rock_normal.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(1400.0f * s_scaleXZ, 1400.0f * s_scaleXZ, 65.0f * s_scaleXZ);
    MapProp_SetCloudSeaGroundBoundary(
        &s_cloudSea, &s_ground, -4.85f * s_scaleY,
        &(MapBoundaryMistStyle){
            .innerWidth = 1.2f * s_scaleXZ,
            .outerWidth = 3.6f * s_scaleXZ,
            .heightOffset = 0.22f * s_scaleY,
            .outerDrop = 0.42f * s_scaleY,
            .opacity = 0.32f,
        });
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    // [VISUAL AAA WATER]: Nước trong ngọc bích, phản chiếu vòm trời và đáy cát ven bờ
    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .center = {kMapCenter.x, 0.0f, kMapCenter.z},
                                             .radiusX = MAP_WIDTH * 0.5f,
                                             .radiusZ = MAP_DEPTH * 0.5f,
                                             .waveHeight = 0.012f,
                                             .waveScale = 1.15f,
                                             .waveSpeed = 0.58f,
                                             .flowVelocity = {0.24f, 0.35f},
                                             .detailScale = 0.18f,
                                             .detailStrength = 0.12f,
                                             .maxDepth = 1.70f,
                                             .absorption = {0.52f, 0.14f, 0.06f},
                                             .scatterColor = {0.08f, 0.38f, 0.36f},
                                             .scatterCoeff = 0.28f,
                                             .causticsStrength = 0.38f,
                                             .causticsScale = 2.10f,
                                             .foamThreshold = 0.095f,
                                             .deepColor = {8, 30, 44, 238},
                                             .shallowColor = {34, 88, 92, 96},
                                             .foamColor = {245, 250, 255, 220},
                                         });

    if (s_river.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_river.waterModel.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_river.boundsCenter = (Vector3){
            s_river.boundsCenter.x * s_scaleXZ,
            s_river.boundsCenter.y * s_scaleY,
            s_river.boundsCenter.z * s_scaleXZ
        };
        s_river.boundsRadius *= s_scaleXZ;
    }

    MapProp_SetWaterTerrainSource(&s_river, SongQuaoWaterBed, SongQuaoWaterSurface, NULL);

    BuildRocks();
    UpdateSongQuaoWaterObstacles(kMapCenter);

    // Draw small 3D pebbles/twigs around camera focus (< 28m)
    // Supplement static debris with dynamic riverbank pebbles and river chips along the stream banks
    int totalDebrisCapacity = SONG_QUAO_DEBRIS_ITEM_COUNT + 1600;
    MapDebrisPlacement *allDebris = (MapDebrisPlacement *)MemAlloc((size_t)totalDebrisCapacity * sizeof(MapDebrisPlacement));
    if (allDebris)
    {
        memcpy(allDebris, SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT * sizeof(MapDebrisPlacement));
        int debrisCount = SONG_QUAO_DEBRIS_ITEM_COUNT;

        for (float rz = 20.0f; rz < 270.0f; rz += 0.82f)
        {
            for (float rx = 30.0f; rx < 170.0f; rx += 0.82f)
            {
                if (debrisCount >= totalDebrisCapacity)
                    break;
                float jx = rx + sinf(rz * 3.7f + rx * 1.9f) * 0.38f;
                float jz = rz + cosf(rx * 2.3f - rz * 2.1f) * 0.38f;
                float signedShore = SongQuaoSampleSignedShore(jx, jz);
                if (signedShore >= -0.12f && signedShore <= 0.55f)
                {
                    Vector3 gPos, gNorm;
                    if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, jx, jz, &gPos, &gNorm))
                    {
                        if (gNorm.y >= 0.62f && gPos.y >= -0.85f)
                        {
                            float pScale = 0.65f + 0.60f * (0.5f + 0.5f * sinf(jx * 7.1f + jz * 5.3f));
                            float pRot = (float)fmodf((jx * 137.0f + jz * 83.0f), 360.0f);
                            allDebris[debrisCount++] = (MapDebrisPlacement){
                                .position = gPos,
                                .rotationDeg = pRot,
                                .scale = pScale,
                                .color = (Color){255, 255, 255, 255},
                                .type = MAP_DEBRIS_PEBBLE};
                        }
                    }
                }
            }
        }
        TraceLog(LOG_INFO, "SONG_QUAO_RIVER_PEBBLES: added=%d total=%d capacity=%d",
                 debrisCount - SONG_QUAO_DEBRIS_ITEM_COUNT, debrisCount, totalDebrisCapacity);

        int grainCap = debrisCount < GRANULAR_MAP_BED_MAX ? debrisCount : GRANULAR_MAP_BED_MAX;
        GranularGrainState *bedGrains = (GranularGrainState *)MemAlloc((size_t)grainCap * sizeof(GranularGrainState));
        if (bedGrains)
        {
            for (int i = 0; i < grainCap; i++)
            {
                const MapDebrisPlacement *d = &allDebris[i];
                Vector3 pos = d->position;
                Vector3 norm = {0.0f, 1.0f, 0.0f};
                if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &pos, &norm))
                {
                    pos.y += 0.014f;
                }
                uint32_t mId = GRANULAR_MAT_COARSE_GRAVEL;
                if (d->type == MAP_DEBRIS_LEAF)
                {
                    mId = ((i % 3) == 0) ? GRANULAR_MAT_FRESH_LEAF : GRANULAR_MAT_DRY_LEAF;
                }
                else if (d->type == MAP_DEBRIS_TWIG)
                {
                    mId = GRANULAR_MAT_DRY_TWIG;
                }
                float seed = fmodf(fabsf(d->rotationDeg) / 360.0f + (float)(i % 97) * 0.0103f, 1.0f);
                bedGrains[i] = (GranularGrainState){
                    .position = pos,
                    .velocity = {0.0f, 0.0f, 0.0f},
                    .radius = fmaxf(0.018f, 0.024f * d->scale),
                    .lifetime = 999.0f,
                    .seed = seed,
                    .materialId = mId
                };
            }
#ifndef __ANDROID__
            GranularSim_SeedMapBedFromGrains(bedGrains, grainCap);
#endif
            MemFree(bedGrains);
        }
        MemFree(allDebris);
    }
    else
    {
#ifndef __ANDROID__
        GranularSim_SeedMapBed(kMapCenter, 32.0f, 1500, NULL, 0, 101u);
#endif
    }

    BuildFloraPlacements();

    // [VISUAL AAA & 60+ FPS FLORA]:
    // - Bản lá thanh mảnh tự nhiên (bladeWidthScale = 0.065f ~ 2.1cm) đan kết rosettes mềm mại
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, s_scaledFlowerClusters, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {35, 54, 20, 255},  // Deep velvety moss-jade root matching ground_splat.fs
            .grassTipColor = {102, 142, 50, 255}, // Natural sunlit olive-jade tips
            .bladesPerClump = 6,
            .bladeSegments = 4,
            .bladeWidthScale = 0.060f,
            .lodDistance = 36.0f,
            .midLodDistance = 24.0f,
            .drawDistance = 122.0f,
            .shadowDistance = (s_scaleXZ > 1.5f) ? -1.0f : 24.0f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {22, 36, 16, 255},
            .reedTipColor = {104, 138, 58, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 1.0f,
            .pampasPlacements = s_pampasPlacements,
            .pampasCount = s_pampasCount,
            .pampasRootColor = {28, 38, 18, 255},
            .pampasTipColor = {176, 178, 122, 255},
            .enableCoLauPlumes = true,
            .coLauOnReeds = true,
            .coLauPlumeDensity = 0.92f,
            .coLauPlumeTint = {238, 239, 233, 255}, // Ethereal silver-white (Bạch Miên Cỏ Lau)
        });
    FreeFloraScratch();

    EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
    if (s_scaleXZ > 1.5f)
    {
        float sdx = camera.target.x - s_staticShadowCenter.x;
        float sdz = camera.target.z - s_staticShadowCenter.z;
        if (sdx * sdx + sdz * sdz > 70.0f * 70.0f)
        {
            s_staticShadowCenter = camera.target;
            EnvShadow_InvalidateStaticCache();
        }
    }
    CaptureSongQuaoStaticShadows();
    MapManager_SetZones(s_songQuaoZones, SONG_QUAO_ZONE_COUNT);
    extern void ResolveCollisionSongQuaoMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("SONG_QUAO", ResolveCollisionSongQuaoMap);

    s_fluff = MapProp_CreateFluffSystem(64, 0x6a1f29u);

    // Scenic Xianxia trees & bamboo groves along valley ridges and riverbanks
    {
        static const MapTreePlacement kSongQuaoTrees[] = {
            // Peach Blossom grove near central clearing & upper path
            {.position = {76.0f, 0.0f, 134.0f}, .scaleXZ = 1.15f, .scaleY = 1.10f, .rotationDeg = 25.0f, .phase = 0.12f, .canopyTint = {255, 224, 232, 255}, .archetype = MAP_TREE_PEACH_BLOSSOM},
            {.position = {112.0f, 0.0f, 162.0f}, .scaleXZ = 1.25f, .scaleY = 1.18f, .rotationDeg = 140.0f, .phase = 0.41f, .canopyTint = {255, 214, 226, 255}, .archetype = MAP_TREE_PEACH_BLOSSOM},
            {.position = {82.0f, 0.0f, 168.0f}, .scaleXZ = 1.05f, .scaleY = 1.00f, .rotationDeg = 275.0f, .phase = 0.73f, .canopyTint = {255, 232, 238, 255}, .archetype = MAP_TREE_PEACH_BLOSSOM},
            // Weeping Willows leaning over lake & river bends
            {.position = {SONG_QUAO_LAKE_CENTER_X - 16.0f, 0.0f, SONG_QUAO_LAKE_CENTER_Z + 9.0f}, .scaleXZ = 1.20f, .scaleY = 1.15f, .rotationDeg = 45.0f, .phase = 0.22f, .canopyTint = {225, 245, 210, 255}, .archetype = MAP_TREE_WEEPING_WILLOW},
            {.position = {SONG_QUAO_LAKE_CENTER_X + 18.0f, 0.0f, SONG_QUAO_LAKE_CENTER_Z - 11.0f}, .scaleXZ = 1.10f, .scaleY = 1.10f, .rotationDeg = 200.0f, .phase = 0.58f, .canopyTint = {215, 240, 200, 255}, .archetype = MAP_TREE_WEEPING_WILLOW},
            // Ancient Pines on outer ridges
            {.position = {58.0f, 0.0f, 118.0f}, .scaleXZ = 1.35f, .scaleY = 1.30f, .rotationDeg = 60.0f, .phase = 0.15f, .canopyTint = {230, 245, 225, 255}, .archetype = MAP_TREE_ANCIENT_PINE},
            {.position = {134.0f, 0.0f, 112.0f}, .scaleXZ = 1.40f, .scaleY = 1.35f, .rotationDeg = 185.0f, .phase = 0.67f, .canopyTint = {225, 240, 220, 255}, .archetype = MAP_TREE_ANCIENT_PINE},
            {.position = {62.0f, 0.0f, 195.0f}, .scaleXZ = 1.25f, .scaleY = 1.20f, .rotationDeg = 310.0f, .phase = 0.89f, .canopyTint = {235, 248, 228, 255}, .archetype = MAP_TREE_ANCIENT_PINE},
            {.position = {128.0f, 0.0f, 205.0f}, .scaleXZ = 1.30f, .scaleY = 1.28f, .rotationDeg = 95.0f, .phase = 0.34f, .canopyTint = {228, 242, 222, 255}, .archetype = MAP_TREE_ANCIENT_PINE},
            // Jade Bamboo clusters
            {.position = {68.0f, 0.0f, 146.0f}, .scaleXZ = 1.00f, .scaleY = 1.10f, .rotationDeg = 15.0f, .phase = 0.05f, .canopyTint = {232, 250, 220, 255}, .archetype = MAP_TREE_BAMBOO},
            {.position = {69.5f, 0.0f, 147.8f}, .scaleXZ = 0.90f, .scaleY = 0.98f, .rotationDeg = 110.0f, .phase = 0.28f, .canopyTint = {226, 245, 214, 255}, .archetype = MAP_TREE_BAMBOO},
            {.position = {67.2f, 0.0f, 148.5f}, .scaleXZ = 1.08f, .scaleY = 1.18f, .rotationDeg = 225.0f, .phase = 0.51f, .canopyTint = {238, 252, 225, 255}, .archetype = MAP_TREE_BAMBOO},
            {.position = {122.0f, 0.0f, 142.0f}, .scaleXZ = 1.05f, .scaleY = 1.12f, .rotationDeg = 40.0f, .phase = 0.19f, .canopyTint = {232, 250, 220, 255}, .archetype = MAP_TREE_BAMBOO},
            {.position = {123.4f, 0.0f, 140.6f}, .scaleXZ = 0.92f, .scaleY = 1.02f, .rotationDeg = 165.0f, .phase = 0.63f, .canopyTint = {226, 245, 214, 255}, .archetype = MAP_TREE_BAMBOO},
        };
        MapTreePlacement scaledTrees[sizeof(kSongQuaoTrees) / sizeof(kSongQuaoTrees[0])];
        int treeCount = (int)(sizeof(kSongQuaoTrees) / sizeof(kSongQuaoTrees[0]));
        for (int i = 0; i < treeCount; i++)
        {
            scaledTrees[i] = kSongQuaoTrees[i];
            scaledTrees[i].position.x *= s_scaleXZ;
            scaledTrees[i].position.z *= s_scaleXZ;
        }
        s_trees = MapProp_CreateTreeSystem(scaledTrees, treeCount, &s_ground, kMapCenter, fminf(120.0f * s_scaleXZ, 185.0f));
    }
    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready)
        return;

    // Direct player grass parting: forward player position into the 64x64 nature interaction field
    MapProp_BeginNatureInteraction(camera.target, dt);
    MapProp_AddNatureWindVorticles(s_flora.time);
    if (s_hasPlayerInteractor && s_playerRadius > 0.01f)
    {
        MapProp_AddNatureInteractor(s_playerPos, 0.85f, 1.0f);
    }
    MapProp_EndNatureInteraction();

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    UpdateSongQuaoWaterObstacles(camera.target);
    // Rain mode (] cycles dry -> drizzle -> heavy -> storm): drives SWE rain/runoff + streaks
    if (IsKeyPressed(KEY_RIGHT_BRACKET))
    {
        static const float kRainSteps[] = {0.0f, 0.5f, 1.0f, 2.0f};
        int cur = 0;
        for (int i = 0; i < 4; i++)
            if (fabsf(MapProp_GetRainTarget() - kRainSteps[i]) < 0.01f)
                cur = i;
        MapProp_SetRain(kRainSteps[(cur + 1) % 4]);
    }
    MapProp_UpdateRain(dt);
    MapProp_UpdateWaterSurface(&s_river, dt);
    if (!(s_skipMask & MAP_SKIP_FLUFF))
        MapProp_UpdateFluffSystem(&s_fluff, camera.target, &s_ground, kMapCenter, PLAYABLE_BASIN_MIN_Y, dt);

    MapDem_UpdateShadowFocusAndStaticCache(
        camera.target, &s_lastShadowFocus, &s_staticShadowCenter,
        SHADOW_FOCUS_RADIUS, SHADOW_SNAP_STEP, SHADOW_HYSTERESIS_XZ, SHADOW_HYSTERESIS_Y,
        s_scaleXZ, kMapCenter, (s_scaleXZ > 1.5f) ? 140.0f : 96.0f, &s_rocks, s_skipMask);
}

void DrawSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    // Thứ tự chuẩn Early-Z / TBDR HSR:
    // Vẽ Đá & Địa hình mặt đất trước để Depth Buffer cắt bỏ toàn bộ cỏ/hoa bị khuất sau đồi.
    if (!(s_skipMask & MAP_SKIP_ROCKS))
        MapProp_DrawRockSystem(&s_rocks, false);
    if (!(s_skipMask & MAP_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        MapProp_DrawCliffSkirt(&s_cliff, camera);
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);
    if (s_trees.ready)
        MapProp_DrawTreeSystem(&s_trees, s_flora.time, s_flora.windDirection, s_flora.windStrength);

    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_WATER))
        MapProp_DrawWaterOverlay(&s_river, (float)fmod(GetTime(), 3600.0));
    if (!(s_skipMask & MAP_SKIP_FLUFF))
        MapProp_DrawFluffSystem(&s_fluff, camera);
    MapProp_DrawRain(camera.target, (float)fmod(GetTime(), 3600.0));
}

void UnloadSongQuaoMap(void)
{
    Wind_SetMacro(&s_prevWindMacro);
    ColorGradeLut_UseNeutral();
    PostFX_ClearMapProfile();
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    Environment_SetSunIntensity(1.0f);
    if (!s_ready)
        return;

    MapProp_UnloadFluffSystem(&s_fluff);
    MapProp_UnloadTreeSystem(&s_trees);
    FreeFloraScratch();

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_river);
    MapProp_UnloadFloraSystem(&s_flora);
    GranularSim_ClearMapBed();
    MapProp_UnloadRockSystem(&s_rocks);
    if (s_cliff.ready)
        MapProp_UnloadCliffSkirt(&s_cliff);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);

    MapDem_UnloadIsland(&s_dem);
    if (s_habitat.distanceTexture.id > 0)
        UnloadTexture(s_habitat.distanceTexture);
    s_habitat = (MapEcology){0};

    s_grassCount = 0;
    s_reedCount = 0;
    s_pampasCount = 0;
    s_flowerCount = 0;
    s_shoreFieldW = 0;
    s_shoreFieldH = 0;

#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_lastShadowFocus = (Vector3){-9999.0f, 0.0f, -9999.0f};
    s_playerPos = (Vector3){0};
    s_playerVel = (Vector3){0};
    s_playerRadius = 0.0f;
    s_hasPlayerInteractor = false;
    s_ready = false;
}

float GetGroundHeightSongQuaoMap(float x, float z)
{
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

bool SampleGroundSurfaceSongQuaoMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    return MapDem_SampleGroundSurface(&s_dem, &s_ground, x, z, outPosition, outNormal);
}

bool GetWaterInfoSongQuaoMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, outSurfaceY, outWaterDepth);
}

void SetWaterInteractorSongQuaoMap(Vector3 position, Vector3 velocity, float radius)
{
    s_playerPos = position;
    s_playerVel = velocity;
    s_playerRadius = radius;
    s_hasPlayerInteractor = (radius > 0.05f);
    MapDem_SetWaterInteractor(&s_dem, &s_ground, &s_river, position, velocity, radius);
}

void SetRainSongQuaoMap(float intensity)
{
    MapProp_SetRain(intensity);
}

void AddWaterRippleSongQuaoMap(Vector3 position, float radius, float intensity)
{
    MapDem_AddWaterRipple(&s_river, position, radius, intensity);
}

void ResolveCollisionSongQuaoMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY)
{
    if (!s_ready)
        return;
    MapProp_ResolveRockSystemCollision(&s_rocks, pos, agentRadius, stepHeight, inOutGroundY);
}

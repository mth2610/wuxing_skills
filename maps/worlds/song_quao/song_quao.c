#include "song_quao.h"
#include "assets/maps/song_quao/song_quao_generated_meta.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "post_process/camera_context.h"
#include "post_process/color_grade_lut.h"
#include "maps/map_manager.h"
#include "core/resource_manager.h"
#include "environment/volumetric/volumetric_fog.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MAP_WIDTH SONG_QUAO_MAP_WIDTH
#define MAP_DEPTH SONG_QUAO_MAP_DEPTH
#define CLIFF_DEPTH SONG_QUAO_CLIFF_DEPTH
#define CLOUD_SEA_Y -5.20f

// Broad valley meadow coverage across the celestial island plateau and rolling hills
#define GRASS_TUFT_CAPACITY 98000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 800
#define PAMPAS_COUNT 420

// Safe elevation floor above the sheer outer cliff abyss (-CLIFF_DEPTH)
#define PLAYABLE_BASIN_MIN_Y (-2.40f)
#define PLAYABLE_BASIN_FADE_Y (-1.65f)

// Plateau grass elevation & slope thresholds aligned with ground_splat.fs wCliff
#define GRASS_CLIFF_MIN_Y (-0.95f)
#define GRASS_CLIFF_FADE_Y (-0.35f)
#define GRASS_MIN_NORMAL_Y 0.73f
#define GRASS_FULL_NORMAL_Y 0.86f

// Shadow focus quantization & hysteresis to prevent boundary flip-flop shimmering
#define SHADOW_SNAP_STEP 1.0f
#define SHADOW_HYSTERESIS_XZ 0.65f
#define SHADOW_HYSTERESIS_Y 0.35f
#define SHADOW_FOCUS_RADIUS 18.0f

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

static const MapZone SONG_QUAO_ZONES[] = {
    {NAT_RIVER, {SONG_QUAO_LAKE_CENTER_X, 0.0f, SONG_QUAO_LAKE_CENTER_Z}, SONG_QUAO_LAKE_RADIUS_X},
    {NAT_FOREST, {94.15f, 0.0f, 150.0f}, 35.0f}, // Central basin plateau meadow
    {NAT_FOREST, {110.0f, 0.0f, 100.0f}, 25.0f}, // North valley forest
};
#define SONG_QUAO_ZONE_COUNT (int)(sizeof(SONG_QUAO_ZONES) / sizeof(SONG_QUAO_ZONES[0]))

// --- Map Scenery & Scaffolding ---
static MapDemIsland s_dem;
static MapGroundSurface s_ground;
static MapRockSystem s_rocks;
static MapDebrisSystem s_debris;
static MapFloraSystem s_flora;
static MapSkyDome s_sky;
static MapCloudSea s_cloudSea;
static MapCliffSkirt s_cliff;
static MapWaterSurface s_river;
static MapEcology s_habitat;
static MapFluffSystem s_fluff;

static MapMeadowPlacement s_grassPlacements[GRASS_TUFT_CAPACITY];
static MapFlowerPlacement s_flowerPlacements[FLOWER_COUNT];
static MapMeadowPlacement s_reedPlacements[REED_COUNT];
static MapMeadowPlacement s_pampasPlacements[PAMPAS_COUNT];
static int s_grassCount = 0;
static int s_reedCount = 0;
static int s_pampasCount = 0;
static int s_flowerCount = 0;

static Vector3 s_lastShadowFocus = {-9999.0f, 0.0f, -9999.0f};
static bool s_shadowWasEnabled = false;
static bool s_ready = false;
static unsigned int s_skipMask = 0;

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
    const float invLakeRadX = 1.0f / SONG_QUAO_LAKE_RADIUS_X;
    const float invLakeRadZ = 1.0f / SONG_QUAO_LAKE_RADIUS_Z;
    float dxLake = (x - SONG_QUAO_LAKE_CENTER_X) * invLakeRadX;
    float dzLake = (z - SONG_QUAO_LAKE_CENTER_Z) * invLakeRadZ;
    if (dxLake * dxLake + dzLake * dzLake <= (1.22f * 1.22f) && pos.y <= SONG_QUAO_LAKE_WATER_Y + 0.06f)
        return 0.0f;

    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    // Keep river sand beaches, alluvial gravel beds & rocky cliff rims clean of grass
    if (splat.a > 76 || splat.g > 175)
        return 0.0f;

    float geomSlope = 1.0f - normal.y;
    float rockScree = ((float)splat.g * (1.0f / 255.0f)) * MapMath_SmoothStep01(0.14f, 0.30f, geomSlope);
    if (rockScree > 0.65f)
        return 0.0f;

    // Natural, organic shoreline clumping: lush grass pockets reach the bank in cohesive coves
    float surfaceY = MapDem_SampleWaterHeight(&s_dem, x, z);
    float shoreFade = 1.0f;
    if (surfaceY > -CLIFF_DEPTH + 0.5f)
    {
        float waterClearance = pos.y - surfaceY;
        if (waterClearance < 0.075f)
            return 0.0f;

        if (waterClearance < 0.24f)
        {
            float coveNoise = sinf(x * 0.16f + z * 0.12f) * 0.55f +
                              cosf(z * 0.25f - x * 0.18f + 0.8f) * 0.45f;
            float minClearance = (coveNoise > 0.15f) ? 0.075f : (0.115f - coveNoise * 0.030f);
            if (waterClearance < minClearance)
                return 0.0f;
            shoreFade = MapMath_SmoothStep01(minClearance, minClearance + 0.08f, waterClearance);
        }
    }

    float rockSuppression = MapProp_EvaluateRockSuppression(
        x, z, SONG_QUAO_ROCK_ITEMS, SONG_QUAO_ROCK_ITEM_COUNT);
    if (rockSuppression <= 0.001f)
        return 0.0f;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);
    if (flowerSuppression <= 0.001f)
        return 0.0f;

    float heightFade = MapMath_SmoothStep01(GRASS_CLIFF_MIN_Y, GRASS_CLIFF_FADE_Y, pos.y);
    float slopeFade = MapMath_SmoothStep01(GRASS_MIN_NORMAL_Y, GRASS_FULL_NORMAL_Y, normal.y);
    float sandFade = 1.0f - MapMath_SmoothStep01(42.0f, 76.0f, (float)splat.a);
    float rockFade = 1.0f - MapMath_SmoothStep01(0.35f, 0.65f, rockScree);

    // Coherent macro meadow swells
    float warpedX = x + sinf(z * 0.12f) * 2.6f;
    float macroField = sinf(warpedX * 0.075f + z * 0.048f) * 0.55f +
                       sinf(z * 0.11f - x * 0.042f + 1.2f) * 0.30f +
                       sinf(x * 0.19f + z * 0.15f) * 0.15f;
    float patch = MapMath_SmoothStep01(-0.35f, 0.35f, macroField);
    float macro = 0.74f + 0.26f * patch;

    float density = macro * heightFade * slopeFade * shoreFade * sandFade *
                    rockFade * flowerSuppression * rockSuppression;
    return (density > 0.0f) ? ((density < 1.0f) ? density : 1.0f) : 0.0f;
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    Vector3 pos, normal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, cx, cz, &pos, &normal))
        return 0.5f;

    float waterClearance = pos.y - MapDem_SampleWaterHeight(&s_dem, cx, cz);
    // Graceful shoreline & cliff-rim taper: peripheral tufts bow down into low cushions
    float shoreTaper = (waterClearance >= 0.34f)
                           ? 1.0f
                           : (0.28f + 0.72f * MapMath_SmoothStep01(0.06f, 0.34f, waterClearance));
    float cliffTaper = (pos.y >= -0.20f)
                           ? 1.0f
                           : (0.35f + 0.65f * MapMath_SmoothStep01(GRASS_CLIFF_MIN_Y, -0.20f, pos.y));
    float slopeTaper = (normal.y >= 0.88f)
                           ? 1.0f
                           : (0.45f + 0.55f * MapMath_SmoothStep01(GRASS_MIN_NORMAL_Y, 0.88f, normal.y));
    return shoreTaper * cliffTaper * slopeTaper;
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
    if (pos.y < -0.65f || normal.y < 0.75f)
        return true;
    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    if (splat.g > 165 || splat.b > 95 || splat.a > 72)
        return true;
    if (MapDem_SampleWaterHeight(&s_dem, x, z) >= pos.y - 0.08f)
        return true;
    if (MapProp_EvaluateRockSuppression(x, z, SONG_QUAO_ROCK_ITEMS, SONG_QUAO_ROCK_ITEM_COUNT) < 0.65f)
        return true;
    return false;
}

static void BuildFloraPlacements(void)
{
    // 1. Lush interlocking meadow across the floating island plateau
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = {10.0f, 8.0f},
            .maxBounds = {MAP_WIDTH - 10.0f, MAP_DEPTH - 8.0f},
            .spacing = 0.33f,
            .seed = 0x51a7c3u,
            .densityFn = SongQuaoGrassDensity,
            .ecology = &s_habitat,
            .flowerClusters = SONG_QUAO_FLOWER_CLUSTERS,
            .flowerClusterCount = SONG_QUAO_FLOWER_CLUSTER_COUNT,
            .edgeTaperFn = SongQuaoEdgeTaper,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

    // 2. Reeds along riverbanks & wetland margins inside the valley basin
    float spanX = MAP_WIDTH * 0.38f;
    float spanZ = MAP_DEPTH * 0.46f;
    s_reedCount = MapFlora_GenerateReedPlacements(
        s_reedPlacements, REED_COUNT, &s_ground, kMapCenter,
        (MapFloraReedConfig){
            .span = {spanX, spanZ},
            .dem = &s_dem,
            .minWaterClearance = 0.006f,
            .maxWaterClearance = 0.22f,
            .minWetSplatAlpha = 95,
            .maxWetClearance = 0.34f,
            .minClusterDistance = 0.10f,
            .seed = 0x9371a5u,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_REEDS: placements=%d capacity=%d",
             s_reedCount, REED_COUNT);

    // 3. Clustered Hillside Silvergrass / Pampas Drifts (Ghost of Tsushima style)
    s_pampasCount = MapFlora_GeneratePampasPlacements(
        s_pampasPlacements, PAMPAS_COUNT, &s_ground, kMapCenter,
        (MapFloraPampasConfig){
            .span = {spanX, spanZ},
            .clearingRadius = 11.5f,
            .dem = &s_dem,
            .flowerClusters = SONG_QUAO_FLOWER_CLUSTERS,
            .flowerClusterCount = SONG_QUAO_FLOWER_CLUSTER_COUNT,
            .seed = 0x9371a5u,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_PAMPAS: placements=%d capacity=%d",
             s_pampasCount, PAMPAS_COUNT);

    // 4. Multi-species Wildflower Colonies
    MapFlora_GenerateFlowerPlacements(
        s_flowerPlacements, FLOWER_COUNT,
        SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
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

    MapProp_PopulateRockItems(&s_rocks, &s_ground, kMapCenter,
                              SONG_QUAO_ROCK_ITEMS, SONG_QUAO_ROCK_ITEM_COUNT);
}

static void RegisterSongQuaoWaterObstacles(void)
{
    for (int i = 0; i < s_rocks.count; i++)
    {
        const MapRockItem *rock = &s_rocks.items[i];
        float surfaceY = 0.0f, waterDepth = 0.0f;
        if (GetWaterInfoSongQuaoMap(rock->position.x, rock->position.z, &surfaceY, &waterDepth) &&
            waterDepth > 0.03f)
        {
            MapProp_AddWaterObstacle(&s_river, rock->position, rock->radiusScale * 1.18f, 1.0f);
        }
    }
}

static void SpawnSongQuaoMistVolumes(void)
{
    FogVolume_ClearAll();

    LocalFogVolume riverValleyMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {115.0f, 0.38f, 130.0f},
        .extents = {18.0f, 0.48f, 14.0f},
        .color = {232, 244, 252, 255},
        .density = 0.065f,
        .edgeSoftness = 0.90f,
        .driftVelocity = {0.02f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&riverValleyMist);

    LocalFogVolume lakeBasinMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {SONG_QUAO_LAKE_CENTER_X, 0.30f, SONG_QUAO_LAKE_CENTER_Z},
        .extents = {SONG_QUAO_LAKE_RADIUS_X * 1.35f, 0.44f, SONG_QUAO_LAKE_RADIUS_Z * 1.35f},
        .color = {228, 242, 252, 255},
        .density = 0.075f,
        .edgeSoftness = 0.90f,
        .driftVelocity = {0.025f, 0.0f, 0.015f},
        .active = true};
    FogVolume_Create(&lakeBasinMist);

    LocalFogVolume centralMeadowMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {94.0f, 0.42f, 150.0f},
        .extents = {15.0f, 0.45f, 11.5f},
        .color = {236, 246, 252, 255},
        .density = 0.060f,
        .edgeSoftness = 0.90f,
        .driftVelocity = {0.02f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&centralMeadowMist);

    LocalFogVolume outletWaterfallMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {SONG_QUAO_OUTLET_X, -0.35f, SONG_QUAO_OUTLET_Z - 1.5f},
        .extents = {14.5f, 1.45f, 12.0f},
        .color = {234, 244, 252, 255},
        .density = 0.16f,
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
    if (!EnvShadow_NeedsStaticCapture())
        return;
    if ((s_skipMask & MAP_SKIP_SHADOWCASTERS) || (s_skipMask & MAP_SKIP_ROCKS))
        return;
    float staticRadius = fmaxf(MAP_WIDTH, MAP_DEPTH) * 0.52f;
    EnvShadow_BeginStaticCapture(kMapCenter, staticRadius);
    if (!EnvShadow_IsCapturing())
        return;
    Shader depthShader = EnvShadow_GetDepthShader();
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
    EnvShadow_EndStaticCapture();
}

void InitSongQuaoMap(void)
{
    // Skip disjointed s_cliff skirt: s_ground's unified triplanar granite terrain
    // renders the seamless floating island directly into the cloud sea.
    s_skipMask = MapProp_GetSkipMask() | MAP_SKIP_CLIFF;
    if (s_skipMask & MAP_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else
        ColorGradeLut_Load("assets/maps/song_quao/song_quao_grade.png");

    Environment_SetTimeOfDaySpeed(0.0f);
    // Ghost of Tsushima / Where Winds Meet cinematic golden-hour lighting:
    Environment_SetAmbientColor((Color){76, 104, 138, 255});
    Environment_SetSunColor((Color){255, 218, 162, 255});
    Environment_SetSunIntensity(1.85f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.46f, -0.38f, -0.80f}));
    Environment_SetShadowColor((Color){32, 46, 64, 98});

    AtmosphereProfile atmos = {
        .color = {226, 236, 246, 255},
        .start = 30.0f,
        .end = 220.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0082f, 0.0142f, 0.0275f},
            .mieScattering = 0.0032f,
            .mieAnisotropy = 0.80f,
            .multipleScatteringAmp = 2.25f},
        .density = {.baseDensity = 0.0072f, .heightFalloff = 0.36f, .baseAltitude = -1.5f, .enableSigmoidLayer = false}};
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity((s_skipMask & MAP_SKIP_GODRAYS) ? 0.0f : 1.35f);
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.16f,
        .worldSize = 105.0f,
        .planeHeight = 80.0f,
        .coverage = 0.46f,
        .softness = 0.24f,
        .windSpeedScale = 0.45f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);
    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnSongQuaoMistVolumes();

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        CaptureSongQuaoStaticShadows();
        MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = kMapCenter;
    EnvShadow_SetFocus(kMapCenter, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, SONG_QUAO_HEIGHT_RANGE, CLIFF_DEPTH,
                      "assets/maps/song_quao/song_quao_ecology_splat.png",
                      "assets/maps/song_quao/song_quao_water_height.png");

    s_ground = MapProp_CreateGroundHeightfield16(
        "assets/maps/song_quao/song_quao_heightfield.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + SONG_QUAO_HEIGHT_RANGE, CLIFF_DEPTH, 3.6f,
        "assets/maps/song_quao/song_quao_ecology_splat.png",
        "assets/textures/verdant_meadow_substrate_diffuse.png",
        "assets/textures/dirt_diffuse.png");

    s_habitat.habitatTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/song_quao/song_quao_habitat.png", true,
        TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP);
    {
        Image distImg = LoadImage("assets/maps/song_quao/song_quao_habitat_distance.png");
        if (distImg.data != NULL)
        {
            ImageFormat(&distImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            unsigned char *bytes = (unsigned char *)distImg.data;
            int totalPx = distImg.width * distImg.height;

            // Pre-folded affine constants for 16-bit big-endian distance field decoding
            const float kPathScale = (32.0f / 65535.0f) * (1.0f / 5.1f);
            const float kPathBias = (-14.35f) * (1.0f / 5.1f);
            const float kShoreScale = (32.0f / 65535.0f) * (1.0f / 5.5f);
            const float kShoreBias = (-12.90f) * (1.0f / 5.5f);

            for (int i = 0; i < totalPx; i++)
            {
                unsigned char *p = &bytes[i * 4];
                float rawPath = (float)(((unsigned int)p[0] << 8) | (unsigned int)p[1]);
                float rawShore = (float)(((unsigned int)p[2] << 8) | (unsigned int)p[3]);

                float encPath = rawPath * kPathScale + kPathBias;
                float encShore = rawShore * kShoreScale + kShoreBias;

                encPath = (encPath > 0.0f) ? ((encPath < 1.0f) ? encPath : 1.0f) : 0.0f;
                encShore = (encShore > 0.0f) ? ((encShore < 1.0f) ? encShore : 1.0f) : 0.0f;

                p[0] = (unsigned char)(encPath * 255.0f + 0.5f);
                p[1] = (unsigned char)(encShore * 255.0f + 0.5f);
                p[2] = 0;
                p[3] = 255;
            }
            s_habitat.distanceTexture = LoadTextureFromImage(distImg);
            UnloadImage(distImg);
            if (s_habitat.distanceTexture.id > 0)
            {
                SetTextureFilter(s_habitat.distanceTexture, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(s_habitat.distanceTexture, TEXTURE_WRAP_CLAMP);
            }
        }
    }
    s_habitat.rect = (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH};
    s_habitat.ready = s_habitat.habitatTexture.id > 0 && s_habitat.distanceTexture.id > 0;
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    // Rich forest-moss substrate tint harmonized with grass roots & distant canopy
    MapProp_SetGroundTint(&s_ground, (Color){42, 58, 28, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/verdant_meadow_substrate_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rocks/scree_diffuse.png",
                              "assets/textures/rocks/scree_normal.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(1400.0f, 1400.0f, 65.0f);
    MapProp_SetCloudSeaGroundBoundary(
        &s_cloudSea, &s_ground, -4.85f,
        &(MapBoundaryMistStyle){
            .innerWidth = 1.2f,
            .outerWidth = 3.6f,
            .heightOffset = 0.22f,
            .outerDrop = 0.42f,
            .opacity = 0.32f,
        });
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -1.35f, -26.0f);

    BuildRocks();

    // Cull small 3D pebbles/twigs at 14m
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 14.0f);

    BuildFloraPlacements();

    // [VISUAL AAA & 60+ FPS FLORA]:
    // - Bản lá rộng 0.138f với thân lá đầy đặn giúp thảm cỏ đan kín mượt mà như nhung,
    //   xóa hoàn toàn cảm giác "gai nhọn" và hòa nhập liền mạch với nền địa hình ở cự ly 40m.
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {32, 52, 22, 255},  // Sắc xanh rêu rừng sâu ăn khớp nền đất
            .grassTipColor = {124, 178, 66, 255}, // Sắc xanh ngọc bích bắt nắng ngược sáng
            .bladesPerClump = 5,
            .bladeSegments = 3,
            .bladeWidthScale = 0.138f,
            .lodDistance = 21.0f,
            .midLodDistance = 11.0f,
            .drawDistance = 40.0f,
            .shadowDistance = 7.5f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {30, 48, 22, 255},
            .reedTipColor = {112, 162, 64, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 1.0f,
            .pampasPlacements = s_pampasPlacements,
            .pampasCount = s_pampasCount,
            .pampasRootColor = {38, 54, 26, 255},
            .pampasTipColor = {152, 180, 94, 255},
        });

    // [VISUAL AAA WATER]: Nước nông ven bờ trong vắt (alpha 72) nhìn xuyên thấu đáy cát vàng,
    // chuyển dần theo định luật Beer-Lambert sang sắc xanh ngọc lục bảo sâu thẳm ở lòng sông.
    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .center = {kMapCenter.x, 0.0f, kMapCenter.z},
                                             .radiusX = MAP_WIDTH * 0.5f,
                                             .radiusZ = MAP_DEPTH * 0.5f,
                                             .waveHeight = 0.011f,
                                             .waveScale = 1.55f,
                                             .waveSpeed = 0.62f,
                                             .flowVelocity = {0.22f, 0.34f},
                                             .detailScale = 0.32f,
                                             .detailStrength = 0.26f,
                                             .maxDepth = 1.45f,
                                             .absorption = {0.48f, 0.14f, 0.05f},
                                             .scatterColor = {0.14f, 0.58f, 0.56f},
                                             .scatterCoeff = 0.26f,
                                             .causticsStrength = 0.52f,
                                             .causticsScale = 3.60f,
                                             .foamThreshold = 0.085f,
                                             .deepColor = {16, 62, 78, 215},
                                             .shallowColor = {64, 148, 138, 72},
                                             .foamColor = {244, 248, 250, 215},
                                         });
    RegisterSongQuaoWaterObstacles();

    EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
    CaptureSongQuaoStaticShadows();
    MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);

    s_fluff = MapProp_CreateFluffSystem(64, 0x6a1f29u);
    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready)
        return;
    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_river, dt);
    if (!(s_skipMask & MAP_SKIP_FLUFF))
        MapProp_UpdateFluffSystem(&s_fluff, camera.target, &s_ground, kMapCenter, PLAYABLE_BASIN_MIN_Y, dt);

    // Áp dụng vùng trễ (hysteresis) trước khi snap tâm bóng đổ theo bước 1.0m
    // để chống rung răng cưa bóng (shadow shimmering) khi camera dao động nhẹ.
    float rawDx = fabsf(camera.target.x - s_lastShadowFocus.x);
    float rawDy = fabsf(camera.target.y - s_lastShadowFocus.y);
    float rawDz = fabsf(camera.target.z - s_lastShadowFocus.z);
    if (rawDx > SHADOW_HYSTERESIS_XZ || rawDz > SHADOW_HYSTERESIS_XZ || rawDy > SHADOW_HYSTERESIS_Y)
    {
        Vector3 focus = MapProp_SnapShadowFocus(camera.target, SHADOW_SNAP_STEP);
        if (fabsf(focus.x - s_lastShadowFocus.x) > 0.01f ||
            fabsf(focus.y - s_lastShadowFocus.y) > 0.01f ||
            fabsf(focus.z - s_lastShadowFocus.z) > 0.01f)
        {
            s_lastShadowFocus = focus;
            EnvShadow_SetFocus(focus, SHADOW_FOCUS_RADIUS);
        }
    }
    CaptureSongQuaoStaticShadows();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    bool useFakeRockShadows = !EnvShadow_IsEnabled();
    if (!useFakeRockShadows)
    {
        // Thứ tự chuẩn Early-Z / TBDR HSR:
        // Vẽ Đá & Địa hình mặt đất trước để Depth Buffer cắt bỏ toàn bộ cỏ/hoa bị khuất sau đồi.
        if (!(s_skipMask & MAP_SKIP_ROCKS))
            MapProp_DrawRockSystem(&s_rocks, false);
        if (!(s_skipMask & MAP_SKIP_GROUND))
            MapProp_DrawGround(&s_ground, kMapCenter);
        if (!(s_skipMask & MAP_SKIP_CLIFF))
            MapProp_DrawCliffSkirt(&s_cliff, camera);
        if (!(s_skipMask & MAP_SKIP_DEBRIS))
            MapProp_DrawDebrisSystem(&s_debris, camera.position);
        if (!(s_skipMask & MAP_SKIP_FLORA))
            MapProp_DrawFloraSystem(&s_flora);
    }
    else
    {
        if (!(s_skipMask & MAP_SKIP_GROUND))
            MapProp_DrawGround(&s_ground, kMapCenter);
        if (!(s_skipMask & MAP_SKIP_CLIFF))
            MapProp_DrawCliffSkirt(&s_cliff, camera);
        if (!(s_skipMask & MAP_SKIP_ROCKS))
            MapProp_DrawRockSystem(&s_rocks, true);
        if (!(s_skipMask & MAP_SKIP_DEBRIS))
            MapProp_DrawDebrisSystem(&s_debris, camera.position);
        if (!(s_skipMask & MAP_SKIP_FLORA))
            MapProp_DrawFloraSystem(&s_flora);
    }

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
}

void UnloadSongQuaoMap(void)
{
    ColorGradeLut_UseNeutral();
    Environment_SetSunIntensity(1.0f);
    if (!s_ready)
        return;

    MapProp_UnloadFluffSystem(&s_fluff);

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_river);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
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

#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_lastShadowFocus = (Vector3){-9999.0f, 0.0f, -9999.0f};
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
    MapDem_SetWaterInteractor(&s_dem, &s_ground, &s_river, position, velocity, radius);
}

void AddWaterRippleSongQuaoMap(Vector3 position, float radius, float intensity)
{
    MapDem_AddWaterRipple(&s_river, position, radius, intensity);
}
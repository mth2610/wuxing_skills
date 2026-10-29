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
#define CLOUD_SEA_Y -4.20f

// Broad valley meadow coverage across the celestial island plateau and rolling hills
#define GRASS_TUFT_CAPACITY 98000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 800
#define PAMPAS_COUNT 420

// Safe elevation floor above the sheer outer cliff abyss (-CLIFF_DEPTH)
#define PLAYABLE_BASIN_MIN_Y (-2.40f)
#define PLAYABLE_BASIN_FADE_Y (-1.65f)

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

    // Organic boundary at island cliff drop-off into cloud sea
    if (pos.y < PLAYABLE_BASIN_MIN_Y || normal.y < 0.38f)
        return 0.0f;
    float heightFade = MapMath_SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y, pos.y);
    float slopeFade = MapMath_SmoothStep01(0.38f, 0.56f, normal.y);

    // Smooth, organic shoreline transition with micro-meander noise
    float surfaceY = MapDem_SampleWaterHeight(&s_dem, x, z);
    float waterClearance = pos.y - surfaceY;
    float shoreMeander = sinf(x * 1.6f + z * 1.2f) * 0.035f + cosf(x * 3.2f - z * 2.6f) * 0.018f;
    float minClearance = 0.015f + shoreMeander;
    if (waterClearance < minClearance)
        return 0.0f;
    float shoreFade = MapMath_SmoothStep01(minClearance, 0.20f + shoreMeander, waterClearance);

    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    // Gate rockScree by actual geometric slope (1.0 - normal.y), matching ground_splat.fs wCliff
    float geomSlope = 1.0f - normal.y;
    float rockScree = ((float)splat.g / 255.0f) * MapMath_SmoothStep01(0.18f, 0.45f, geomSlope);
    if (rockScree > 0.88f)
        return 0.0f;
    float rockFade = 1.0f - MapMath_SmoothStep01(0.65f, 0.88f, rockScree);

    // Coherent macro meadow swells with organic breathing room
    float warpedX = x + sinf(z * 0.12f) * 2.6f;
    float macroField = sinf(warpedX * 0.075f + z * 0.048f) * 0.55f +
                       sinf(z * 0.11f - x * 0.042f + 1.2f) * 0.30f +
                       sinf(x * 0.19f + z * 0.15f) * 0.15f;
    float patch = MapMath_SmoothStep01(-0.35f, 0.35f, macroField);
    float macro = 0.68f + 0.32f * patch;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);

    float rockSuppression = MapProp_EvaluateRockSuppression(
        x, z, SONG_QUAO_ROCK_ITEMS, SONG_QUAO_ROCK_ITEM_COUNT);

    return fmaxf(0.0f, fminf(1.0f, macro * heightFade * slopeFade * shoreFade * rockFade * flowerSuppression * rockSuppression));
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, cx, cz);
    float waterClearance = gy - MapDem_SampleWaterHeight(&s_dem, cx, cz);
    // Graceful shoreline taper: peripheral tufts bow down to ~28% height near water shingle
    float shoreTaper = 0.28f + 0.72f * MapMath_SmoothStep01(0.01f, 0.45f, waterClearance);
    float cliffTaper = 0.58f + 0.42f * MapMath_SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y + 0.40f, gy);
    return shoreTaper * cliffTaper;
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
    if (pos.y < PLAYABLE_BASIN_FADE_Y || normal.y < 0.65f)
        return true;
    if (MapDem_SampleWaterHeight(&s_dem, x, z) >= pos.y - 0.14f)
        return true;
    Color splat = MapDem_SampleSplat(&s_dem, x, z);
    if (splat.g > 205)
        return true;
    if (MapProp_EvaluateRockSuppression(x, z, SONG_QUAO_ROCK_ITEMS, SONG_QUAO_ROCK_ITEM_COUNT) < 0.65f)
        return true;
    return false;
}

static void BuildFloraPlacements(void)
{
    // 1. Lush interlocking meadow across the whole floating island
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = {10.0f, 8.0f},
            .maxBounds = {MAP_WIDTH - 10.0f, MAP_DEPTH - 8.0f},
            .spacing = 0.34f,
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
    float spanZ = MAP_DEPTH * 0.32f;
    s_reedCount = MapFlora_GenerateReedPlacements(
        s_reedPlacements, REED_COUNT, &s_ground, kMapCenter,
        (MapFloraReedConfig){
            .span = {spanX, spanZ},
            .dem = &s_dem,
            .minWaterClearance = 0.01f,
            .maxWaterClearance = 0.16f,
            .minWetSplatAlpha = 140,
            .maxWetClearance = 0.20f,
            .minClusterDistance = 0.42f,
            .seed = 0x9371a5u,
        });

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
    s_skipMask = MapProp_GetSkipMask();
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
        EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif

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
    s_habitat.distanceTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/song_quao/song_quao_habitat_distance.png", false,
        TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_CLAMP);
    s_habitat.rect = (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH};
    s_habitat.ready = s_habitat.habitatTexture.id > 0 && s_habitat.distanceTexture.id > 0;
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    // Deep forest-moss substrate tint harmonized with grass roots to eliminate the "plastic carpet" gap
    MapProp_SetGroundTint(&s_ground, (Color){38, 54, 26, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/verdant_meadow_substrate_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(1400.0f, 1400.0f, 65.0f);
    MapBoundaryMistStyle rim = {4.2f, 20.0f, 0.58f, 1.85f, 0.62f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -2.55f, &rim))
        TraceLog(LOG_WARNING, "Song Quao: terrain boundary mist bake failed");

    BuildRocks();

    // Cull small 3D pebbles/twigs at 14m
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 14.0f);

    BuildFloraPlacements();

    // [TỐI ƯU FLORA 60+ FPS]:
    // - Dùng 5 nhánh cỏ/bụi x 3 đoạn uốn cong với bản lá rộng 0.124f giúp giữ nguyên độ dày
    //   của thảm cỏ 98.000 bụi nhưng giảm ~37% đỉnh/tam giác ở tầng cận cảnh (triệt tiêu Quad Overdraw).
    // - Phân bổ khoảng cách 3 tầng LOD (11.5m / 22.0m / 86.0m) mượt mà và kích hoạt cỏ lau sườn đồi.
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {30, 50, 22, 255},  // Sắc xanh rêu rừng sâu ăn khớp nền đất
            .grassTipColor = {116, 172, 64, 255}, // Sắc xanh ngọc bích bắt nắng ngược sáng
            .bladesPerClump = 5,
            .bladeSegments = 3,
            .bladeWidthScale = 0.124f,
            .lodDistance = 22.0f,
            .midLodDistance = 11.5f,
            .drawDistance = 86.0f,
            .shadowDistance = 13.0f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {30, 48, 22, 255},
            .reedTipColor = {108, 156, 62, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 1.0f,
            .pampasPlacements = s_pampasPlacements,
            .pampasCount = s_pampasCount,
            .pampasRootColor = {36, 52, 24, 255},
            .pampasTipColor = {144, 174, 88, 255},
        });

    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .center = {kMapCenter.x, 0.0f, kMapCenter.z},
                                             .radiusX = MAP_WIDTH * 0.5f,
                                             .radiusZ = MAP_DEPTH * 0.5f,
                                             .waveHeight = 0.016f,
                                             .waveScale = 2.40f,
                                             .waveSpeed = 0.65f,
                                             .flowVelocity = {0.24f, 0.38f},
                                             .detailScale = 0.22f,
                                             .detailStrength = 0.22f,
                                             .maxDepth = 1.65f,
                                             .absorption = {0.52f, 0.22f, 0.08f},
                                             .scatterColor = {0.10f, 0.46f, 0.44f},
                                             .scatterCoeff = 0.32f,
                                             .causticsStrength = 0.20f,
                                             .causticsScale = 3.40f,
                                             .foamThreshold = 0.075f,
                                             .deepColor = {18, 56, 68, 205},
                                             .shallowColor = {76, 156, 148, 42},
                                             .foamColor = {238, 246, 248, 180},
                                         });

    s_lastShadowFocus = kMapCenter;
    EnvShadow_SetFocus(kMapCenter, 10.8f);
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

    // [TỐI ƯU SHADOW]: Snap tâm bóng đổ theo bước 1.0m để tránh cập nhật ma trận bóng liên tục
    // khi camera chỉ nhích nhẹ, giữ ổn định cả hiệu năng lẫn chống rung răng cưa bóng (shadow shimmering).
    Vector3 focus = MapProp_SnapShadowFocus(camera.target, 1.0f);
    if (fabsf(focus.x - s_lastShadowFocus.x) > 0.01f || fabsf(focus.z - s_lastShadowFocus.z) > 0.01f)
    {
        s_lastShadowFocus = focus;
        EnvShadow_SetFocus(focus, 10.8f);
    }
    CaptureSongQuaoStaticShadows();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    if (!(s_skipMask & MAP_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);

    bool useFakeRockShadows = !EnvShadow_IsEnabled();
    if (!(s_skipMask & MAP_SKIP_ROCKS))
        MapProp_DrawRockSystem(&s_rocks, useFakeRockShadows);

    if (!(s_skipMask & MAP_SKIP_DEBRIS))
        MapProp_DrawDebrisSystem(&s_debris, camera.position);

    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);

    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_WATER))
        MapProp_DrawWaterOverlay(&s_river, GetTime());
    if (!(s_skipMask & MAP_SKIP_MIST))
        MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
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
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);

    MapDem_UnloadIsland(&s_dem);
    s_habitat = (MapEcology){0};

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
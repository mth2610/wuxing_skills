#include "song_quao.h"
#include "assets/maps/song_quao/song_quao_generated_meta.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "core/camera_context.h"
#include "core/map_manager.h"
#include "core/resource_manager.h"
#include "core/volumetric/volumetric_fog.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define MAP_WIDTH SONG_QUAO_MAP_WIDTH
#define MAP_DEPTH SONG_QUAO_MAP_DEPTH
#define CLIFF_DEPTH SONG_QUAO_CLIFF_DEPTH
#define CLOUD_SEA_Y -4.20f
#define GRASS_TUFT_CAPACITY 70000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 650

// Safe elevation floor above the sheer outer cliff abyss (-CLIFF_DEPTH)
// Prevents cutting off grass in natural river basins and low swales (y in [-2.2m, 0.0m])
#define PLAYABLE_BASIN_MIN_Y (-2.40f)
#define PLAYABLE_BASIN_FADE_Y (-1.65f)

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

static const MapZone SONG_QUAO_ZONES[] = {
    {NAT_RIVER, {SONG_QUAO_LAKE_CENTER_X, 0.0f, SONG_QUAO_LAKE_CENTER_Z}, SONG_QUAO_LAKE_RADIUS_X},
    {NAT_FOREST, {94.15f, 0.0f, 150.0f}, 35.0f}, // Central basin plateau meadow
    {NAT_FOREST, {110.0f, 0.0f, 100.0f}, 25.0f}, // North valley forest
};
#define SONG_QUAO_ZONE_COUNT (int)(sizeof(SONG_QUAO_ZONES) / sizeof(SONG_QUAO_ZONES[0]))

static MapGroundSurface s_ground;
static MapRockSystem s_rocks;
static MapDebrisSystem s_debris;
static MapFloraSystem s_flora;
static MapSkyDome s_sky;
static MapCloudSea s_cloudSea;
static MapWaterSurface s_river;
static Image s_waterHeight;
static MapEcology s_habitat;

static MapMeadowPlacement s_grassPlacements[GRASS_TUFT_CAPACITY];
static MapFlowerPlacement s_flowerPlacements[FLOWER_COUNT];
static MapMeadowPlacement s_reedPlacements[REED_COUNT];
static int s_grassCount = 0;
static int s_reedCount = 0;
static int s_flowerCount = 0;

static bool s_shadowWasEnabled = false;
static bool s_ready = false;

// Ecology texture lookup helper
static Image s_splatImage;
static bool s_splatLoaded = false;

static float SmoothStep01(float edge0, float edge1, float x)
{
    float t = (x - edge0) / fmaxf(1e-5f, edge1 - edge0);
    t = fminf(1.0f, fmaxf(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

static Color SampleSplat(float x, float z)
{
    if (!s_splatLoaded || s_splatImage.width <= 0 || s_splatImage.height <= 0)
        return (Color){255, 0, 0, 0};
    int tx = (int)((x / MAP_WIDTH) * (float)(s_splatImage.width - 1) + 0.5f);
    int tz = (int)((z / MAP_DEPTH) * (float)(s_splatImage.height - 1) + 0.5f);
    if (tx < 0)
        tx = 0;
    else if (tx >= s_splatImage.width)
        tx = s_splatImage.width - 1;
    if (tz < 0)
        tz = 0;
    else if (tz >= s_splatImage.height)
        tz = s_splatImage.height - 1;
    return GetImageColor(s_splatImage, tx, tz);
}

static float SampleWaterHeight(float x, float z)
{
    if (!s_waterHeight.data || s_waterHeight.width < 2 || s_waterHeight.height < 2 ||
        x < 0.0f || z < 0.0f || x > MAP_WIDTH || z > MAP_DEPTH)
        return -CLIFF_DEPTH;

    float fx = (x / MAP_WIDTH) * (float)(s_waterHeight.width - 1);
    float fz = (z / MAP_DEPTH) * (float)(s_waterHeight.height - 1);
    int ix = (int)fminf(floorf(fx), (float)(s_waterHeight.width - 2));
    int iz = (int)fminf(floorf(fz), (float)(s_waterHeight.height - 2));
    if (ix < 0)
        ix = 0;
    if (iz < 0)
        iz = 0;
    float tx = fx - (float)ix;
    float tz = fz - (float)iz;

    Color c00 = GetImageColor(s_waterHeight, ix, iz);
    Color c10 = GetImageColor(s_waterHeight, ix + 1, iz);
    Color c01 = GetImageColor(s_waterHeight, ix, iz + 1);
    Color c11 = GetImageColor(s_waterHeight, ix + 1, iz + 1);

    // Quick dry-cell rejection when all 4 surrounding texels have zero wet mask (B == 0)
    if ((c00.b | c10.b | c01.b | c11.b) == 0)
        return -CLIFF_DEPTH;

    float h00 = (float)(c00.r * 256 + c00.g);
    float h10 = (float)(c10.r * 256 + c10.g);
    float h01 = (float)(c01.r * 256 + c01.g);
    float h11 = (float)(c11.r * 256 + c11.g);
    float code = (tx + tz <= 1.0f)
                     ? (h00 + tx * (h10 - h00) + tz * (h01 - h00))
                     : (h11 + (1.0f - tx) * (h01 - h11) + (1.0f - tz) * (h10 - h11));
    return code * ((CLIFF_DEPTH + SONG_QUAO_HEIGHT_RANGE) / 65535.0f) - CLIFF_DEPTH;
}

static float SongQuaoGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    Vector3 pos, normal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &pos, &normal))
        return 0.0f;

    // Smoothly exclude only deep outer cliff drop-offs and sheer vertical rock walls,
    // preserving lush turf across low-lying riverbanks, gentle slopes and valley swales.
    if (pos.y < PLAYABLE_BASIN_MIN_Y || normal.y < 0.65f)
        return 0.0f;
    float heightFade = SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y, pos.y);
    float slopeFade = SmoothStep01(0.65f, 0.82f, normal.y);

    // Smooth shoreline transition: prevent grass from spawning underwater while
    // feathering density naturally right up to the water's edge.
    float surfaceY = SampleWaterHeight(x, z);
    float waterClearance = pos.y - surfaceY;
    if (waterClearance < 0.02f)
        return 0.0f;
    float shoreFade = SmoothStep01(0.02f, 0.22f, waterClearance);

    Color splat = SampleSplat(x, z);
    float rockScree = (float)splat.g / 255.0f;
    if (rockScree > 0.90f)
        return 0.0f;
    float rockFade = 1.0f - SmoothStep01(0.65f, 0.90f, rockScree);

    // Island boundary fade: gentle taper towards the outer cliff edge (margin 8m)
    float edgeDistX = fminf(x - 8.0f, (MAP_WIDTH - 8.0f) - x);
    float edgeDistZ = fminf(z - 8.0f, (MAP_DEPTH - 8.0f) - z);
    float edgeDist = fminf(edgeDistX, edgeDistZ);
    if (edgeDist <= 0.0f)
        return 0.0f;
    float edgeFade = SmoothStep01(0.0f, 6.0f, edgeDist);

    float warpedX = x + sinf(z * 0.17f) * 2.1f;
    float macroField = sinf(warpedX * 0.15f + z * 0.08f) * 0.55f + sinf(z * 0.20f - x * 0.06f + 1.4f) * 0.30f + sinf(x * 0.33f + z * 0.29f) * 0.15f;
    float patch = SmoothStep01(-0.25f, 0.45f, macroField);
    float macro = 0.78f + 0.22f * patch;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);

    return fmaxf(0.0f, fminf(1.0f, macro * heightFade * slopeFade * shoreFade * edgeFade * rockFade * flowerSuppression));
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, cx, cz);
    float waterClearance = gy - SampleWaterHeight(cx, cz);
    float shoreTaper = 0.38f + 0.62f * SmoothStep01(-0.01f, 0.42f, waterClearance);
    float cliffTaper = 0.50f + 0.50f * SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y + 0.4f, gy);
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
    // Allow wildflowers in gentle basins, exclude only steep slopes, cliffs, and wet banks
    if (pos.y < PLAYABLE_BASIN_FADE_Y || normal.y < 0.62f)
        return true;
    if (SampleWaterHeight(x, z) >= pos.y - 0.08f)
        return true;
    Color splat = SampleSplat(x, z);
    if (splat.g > 215)
        return true; // Excluded on rocky cliff/scree faces
    return false;
}

static float Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)*rng / 4294967296.0f;
}

static void BuildFloraPlacements(void)
{
    // 1. Unified meadow grass generation across the central basin & valley terraces
    // Optimized 0.255m spacing with fuller blade width reduces vertex pressure by ~30%
    // while eliminating sub-pixel mid-range shimmering and keeping lush coverage.
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = {8.0f, 8.0f},
            .maxBounds = {MAP_WIDTH - 8.0f, MAP_DEPTH - 8.0f},
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

    // 2. Reeds along riverbanks & wetland margins (clustered naturally near water edges)
    unsigned int rng = 0x9371a5u;
    s_reedCount = 0;
    for (int attempts = 0; attempts < 4500 && s_reedCount < REED_COUNT; attempts++)
    {
        float rx = 10.0f + Random01(&rng) * (MAP_WIDTH - 20.0f);
        float rz = 10.0f + Random01(&rng) * (MAP_DEPTH - 20.0f);
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal))
            continue;
        if (normal.y < 0.68f || pos.y < PLAYABLE_BASIN_MIN_Y)
            continue;

        float waterY = SampleWaterHeight(rx, rz);
        float clearance = pos.y - waterY;
        Color splat = SampleSplat(rx, rz);

        // Place reeds in moist riparian zones right along the water margin or wetland splat
        bool nearShore = (waterY > -CLIFF_DEPTH + 0.5f) && (clearance >= -0.01f && clearance <= 0.55f);
        bool wetMargin = (splat.a > 85) && (clearance >= 0.0f && clearance <= 1.25f);
        if (nearShore || wetMargin)
        {
            float rad = 0.18f + 0.08f * Random01(&rng);
            float h = 0.82f + 0.46f * Random01(&rng);
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = Random01(&rng) * 360.0f,
                .phase = Random01(&rng)};
        }
    }

    // 3. Multi-species Wildflower Colonies: 100% Procedural 3D Flowers (No texture atlas)
    MapFlora_GenerateFlowerPlacements(
        s_flowerPlacements, FLOWER_COUNT,
        SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        SongQuaoSampleGroundHeight, SongQuaoIsFlowerExcluded, NULL, 0x9371a5u);
    s_flowerCount = FLOWER_COUNT;
}

static void BuildRocks(void)
{
    static const Vector2 kDistantCrags[] = {
        {120.0f, 25.0f},
        {105.0f, 40.0f},
        {80.0f, 30.0f},
        {35.0f, 280.0f},
        {55.0f, 270.0f},
        {155.0f, 265.0f},
    };
    int distantCragCount = (int)(sizeof(kDistantCrags) / sizeof(kDistantCrags[0]));

    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png",
        "assets/textures/rock_roughness.png",
        SONG_QUAO_ROCK_ITEM_COUNT + distantCragCount);

    for (int i = 0; i < SONG_QUAO_ROCK_ITEM_COUNT; i++)
    {
        const MapRockItem *item = &SONG_QUAO_ROCK_ITEMS[i];
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, item->position.x, item->position.z, &pos, &normal))
        {
            pos = item->position;
        }
        MapProp_AddRockItem(&s_rocks, pos, item->radiusScale, item->heightScale, item->rotationDeg, item->archetype);
    }

    for (int i = 0; i < distantCragCount; i++)
    {
        Vector3 pos, normal;
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, kDistantCrags[i].x, kDistantCrags[i].y, &pos, &normal))
        {
            float rot = (float)(i * 67 % 360);
            float scale = 1.35f + 0.35f * (float)(i % 3);
            MapProp_AddRockItem(&s_rocks, pos, scale, scale * 1.15f, rot, MAP_ROCK_CRAG);
        }
    }
}

static void DrawSongQuaoShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    // Dynamic shadow pass only renders animated flora near the camera focus.
    // Static rocks are already baked into the static shadow map via CaptureSongQuaoStaticShadows().
    MapProp_DrawFloraShadowCasters(&s_flora);
}

static void CaptureSongQuaoStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
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
    Environment_SetTimeOfDaySpeed(0.0f);
    // Balanced cool sky hemispheric fill & warm golden morning sunlight
    Environment_SetAmbientColor((Color){96, 116, 142, 255});
    Environment_SetSunColor((Color){255, 228, 192, 255});
    Environment_SetSunIntensity(2.35f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){-0.26f, -0.46f, 0.85f}));
    Environment_SetShadowColor((Color){26, 36, 48, 115});

    AtmosphereProfile atmos = {
        .color = {216, 232, 246, 255},
        .start = 42.0f,
        .end = 260.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0072f, 0.0126f, 0.0248f},
            .mieScattering = 0.0020f,
            .mieAnisotropy = 0.72f,
            .multipleScatteringAmp = 1.85f},
        .density = {.baseDensity = 0.0058f, .heightFalloff = 0.26f, .baseAltitude = -1.5f, .enableSigmoidLayer = false}};
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(0.80f);
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.11f,
        .worldSize = 104.0f,
        .planeHeight = 80.0f,
        .coverage = 0.46f,
        .softness = 0.18f,
        .windSpeedScale = 0.50f,
    };
    Environment_SetCloudShadowConfig(&cloudConfig);

    if (s_ready)
        return;

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif

    // Load splat & water-height images for CPU placement & runtime queries
    s_splatImage = LoadImage("assets/maps/song_quao/song_quao_ecology_splat.png");
    s_splatLoaded = (s_splatImage.data != NULL);

    s_waterHeight = LoadImage("assets/maps/song_quao/song_quao_water_height.png");

    // 16-bit heightfield terrain
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

    // Harmonized warm moss-earth tint so ground substrate blends seamlessly with 3D grass roots
    MapProp_SetGroundTint(&s_ground, (Color){80, 106, 54, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/verdant_meadow_substrate_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");

    // Sky dome & Cloud Sea
    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    MapBoundaryMistStyle rim = {2.4f, 10.5f, 0.45f, 1.9f, 0.50f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -3.85f, &rim))
        TraceLog(LOG_WARNING, "Song Quao: terrain boundary mist bake failed");

    // Build Rocks (Mountain crags, talus clusters, river stones)
    BuildRocks();

    // Build Debris System (Fallen leaves, scree pebbles, twigs)
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 36.0f);

    // Build Flora (Lush valley grass, reeds, wildflower colonies)
    BuildFloraPlacements();
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {38, 64, 28, 255},
            .grassTipColor = {122, 178, 62, 255},
            .bladesPerClump = 6,
            .bladeSegments = 4,
            .bladeWidthScale = 0.245f,
            .lodDistance = 28.0f,
            .drawDistance = 64.0f,
            .shadowDistance = 20.0f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {30, 50, 24, 255},
            .reedTipColor = {132, 172, 78, 255},
            .hasReedPlumes = false,
        });

    // Terrain-clipped river & reservoir surface with refined Beer-Lambert depth optical profile
    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .radiusX = MAP_WIDTH * 0.5f,
                                             .radiusZ = MAP_DEPTH * 0.5f,
                                             .waveHeight = 0.018f,
                                             .waveScale = 1.25f,
                                             .waveSpeed = 0.62f,
                                             .detailScale = 0.11f,
                                             .detailStrength = 0.22f,
                                             .maxDepth = 1.20f,
                                             .absorption = {0.72f, 0.28f, 0.18f},
                                             .scatterColor = {0.10f, 0.24f, 0.20f},
                                             .scatterCoeff = 0.16f,
                                             .causticsStrength = 0.38f,
                                             .causticsScale = 1.45f,
                                             .foamThreshold = 0.135f,
                                             .deepColor = {12, 42, 44, 255},
                                             .shallowColor = {48, 94, 76, 255},
                                             .foamColor = {226, 236, 230, 255},
                                         });

    EnvShadow_SetFocus(kMapCenter, 18.0f);
    EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
    CaptureSongQuaoStaticShadows();
    MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);

    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready)
        return;
    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_river, dt);

    Vector3 focus = {camera.target.x, 0.0f, camera.target.z};
    EnvShadow_SetFocus(focus, 18.0f);
    CaptureSongQuaoStaticShadows();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();
    MapProp_DrawGround(&s_ground, kMapCenter);

    // Avoid stacking fake smart shadows when real environment shadow mapping is active
    bool useFakeRockShadows = !EnvShadow_IsEnabled();
    MapProp_DrawRockSystem(&s_rocks, useFakeRockShadows);

    MapProp_DrawDebrisSystem(&s_debris, camera.position);
    MapProp_DrawFloraSystem(&s_flora);
    MapProp_DrawSkyDome(&s_sky);
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_DrawWaterOverlay(&s_river, GetTime());
    MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
}

void UnloadSongQuaoMap(void)
{
    Environment_SetSunIntensity(1.0f);
    if (!s_ready)
        return;
    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);

    // Release nature interaction displacement texture to prevent trail leaks into next map
    MapProp_ClearNatureInteraction();

    // Detach borrowed ecology pointer before unloading ground
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_river);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
    MapProp_UnloadRockSystem(&s_rocks);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);

    if (s_waterHeight.data)
    {
        UnloadImage(s_waterHeight);
        s_waterHeight = (Image){0};
    }
    s_habitat = (MapEcology){0};
    if (s_splatLoaded)
    {
        UnloadImage(s_splatImage);
        s_splatImage = (Image){0};
        s_splatLoaded = false;
    }
#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_ready = false;
}

float GetGroundHeightSongQuaoMap(float x, float z)
{
    if (x < 1.0f || x > MAP_WIDTH - 1.0f || z < 1.0f || z > MAP_DEPTH - 1.0f)
        return -100.0f;
    Color splat = SampleSplat(x, z);
    if (splat.g == 255 && splat.r == 0 && splat.b == 0 && splat.a == 0)
    {
        float rawY = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
        if (rawY <= -CLIFF_DEPTH + 0.15f)
            return -100.0f;
    }
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

bool SampleGroundSurfaceSongQuaoMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (x < 1.0f || x > MAP_WIDTH - 1.0f || z < 1.0f || z > MAP_DEPTH - 1.0f)
    {
        if (outPosition)
            *outPosition = (Vector3){x, -100.0f, z};
        if (outNormal)
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        return false;
    }
    Color splat = SampleSplat(x, z);
    if (splat.g == 255 && splat.r == 0 && splat.b == 0 && splat.a == 0)
    {
        float rawY = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
        if (rawY <= -CLIFF_DEPTH + 0.15f)
        {
            if (outPosition)
                *outPosition = (Vector3){x, -100.0f, z};
            if (outNormal)
                *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
            return false;
        }
    }
    return MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, outPosition, outNormal);
}

bool GetWaterInfoSongQuaoMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready || x < 0.0f || z < 0.0f || x > MAP_WIDTH || z > MAP_DEPTH)
        return false;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    float surfaceY = SampleWaterHeight(x, z);
    if (surfaceY <= gy + 0.005f)
        return false;
    if (outSurfaceY)
        *outSurfaceY = surfaceY;
    if (outWaterDepth)
        *outWaterDepth = surfaceY - gy;
    return true;
}

void SetWaterInteractorSongQuaoMap(Vector3 position, Vector3 velocity, float radius)
{
    MapProp_SetWaterInteractor(&s_river, position, velocity, radius, 0.0f);
}

void AddWaterRippleSongQuaoMap(Vector3 position, float radius, float intensity)
{
    MapProp_AddWaterRipple(&s_river, position, radius, intensity);
}
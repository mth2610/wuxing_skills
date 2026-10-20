#include "song_quao.h"
#include "assets/maps/song_quao/song_quao_generated_meta.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "core/camera_context.h"
#include "core/color_grade_lut.h"
#include "core/map_manager.h"
#include "core/resource_manager.h"
#include "core/volumetric/volumetric_fog.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define MAP_WIDTH SONG_QUAO_MAP_WIDTH
#define MAP_DEPTH SONG_QUAO_MAP_DEPTH
#define CLIFF_DEPTH SONG_QUAO_CLIFF_DEPTH
#define CLOUD_SEA_Y -4.20f
#define GRASS_TUFT_CAPACITY 64000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 560
#define PAMPAS_COUNT 2400

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
static MapMeadowPlacement s_pampasPlacements[PAMPAS_COUNT];
static int s_grassCount = 0;
static int s_reedCount = 0;
static int s_pampasCount = 0;
static int s_flowerCount = 0;

static bool s_shadowWasEnabled = false;
static bool s_ready = false;

// Subtractive GPU profiling skip mask (WUXING_SONGQUAO_SKIP)
enum {
    SQ_SKIP_GROUND        = 1 << 0,
    SQ_SKIP_ROCKS         = 1 << 1,
    SQ_SKIP_DEBRIS        = 1 << 2,
    SQ_SKIP_FLORA         = 1 << 3,
    SQ_SKIP_SKY           = 1 << 4,
    SQ_SKIP_CLOUDSEA      = 1 << 5,
    SQ_SKIP_WATER         = 1 << 6,
    SQ_SKIP_MIST          = 1 << 7,
    SQ_SKIP_SHADOWCASTERS = 1 << 8,
};
static unsigned int s_skipMask = 0;

static void ParseSkipMask(void)
{
    s_skipMask = 0;
    const char *env = getenv("WUXING_SONGQUAO_SKIP");
    if (!env || !*env)
        return;
    if (strstr(env, "ground")) s_skipMask |= SQ_SKIP_GROUND;
    if (strstr(env, "rocks")) s_skipMask |= SQ_SKIP_ROCKS;
    if (strstr(env, "debris")) s_skipMask |= SQ_SKIP_DEBRIS;
    if (strstr(env, "flora") || strstr(env, "grass")) s_skipMask |= SQ_SKIP_FLORA;
    if (strstr(env, "sky")) s_skipMask |= SQ_SKIP_SKY;
    if (strstr(env, "cloudsea")) s_skipMask |= SQ_SKIP_CLOUDSEA;
    if (strstr(env, "water")) s_skipMask |= SQ_SKIP_WATER;
    if (strstr(env, "mist")) s_skipMask |= SQ_SKIP_MIST;
    if (strstr(env, "shadowcasters") || strstr(env, "casters")) s_skipMask |= SQ_SKIP_SHADOWCASTERS;
}

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

    // Allow grass to grow naturally on rolling hills and riverbanks (normal.y >= 0.42),
    // excluding only sheer vertical cliff walls and deep abyss drop-offs.
    if (pos.y < PLAYABLE_BASIN_MIN_Y || normal.y < 0.42f)
        return 0.0f;
    float heightFade = SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y, pos.y);
    float slopeFade = SmoothStep01(0.42f, 0.68f, normal.y);

    // Smooth shoreline transition right up to the water's edge
    float surfaceY = SampleWaterHeight(x, z);
    float waterClearance = pos.y - surfaceY;
    if (waterClearance < -0.01f)
        return 0.0f;
    float shoreFade = SmoothStep01(-0.01f, 0.18f, waterClearance);

    Color splat = SampleSplat(x, z);
    float rockScree = (float)splat.g / 255.0f;
    if (rockScree > 0.90f)
        return 0.0f;
    float rockFade = 1.0f - SmoothStep01(0.65f, 0.90f, rockScree);

    // Focus grass budget inside the main valley & river basin so reservoir sampling
    // never thins out the playable foreground meadow.
    float nx = (x - kMapCenter.x) / (MAP_WIDTH * 0.28f);
    float nz = (z - kMapCenter.z) / (MAP_DEPTH * 0.28f);
    float basinDist = nx * nx + nz * nz;
    if (basinDist >= 1.0f)
        return 0.0f;
    float edgeFade = 1.0f - SmoothStep01(0.74f, 1.0f, basinDist);

    // Painterly macro meadow rhythm (billowing dense drifts + softer breathing swales)
    float warpedX = x + sinf(z * 0.14f) * 2.4f;
    float macroField = sinf(warpedX * 0.13f + z * 0.07f) * 0.52f +
                       sinf(z * 0.19f - x * 0.06f + 1.4f) * 0.32f +
                       sinf(x * 0.31f + z * 0.27f) * 0.16f;
    float patch = SmoothStep01(-0.32f, 0.42f, macroField);
    float macro = 0.68f + 0.32f * patch;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);

    return fmaxf(0.0f, fminf(1.0f, macro * heightFade * slopeFade * shoreFade * edgeFade * rockFade * flowerSuppression));
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, cx, cz);
    float waterClearance = gy - SampleWaterHeight(cx, cz);
    float shoreTaper = 0.48f + 0.52f * SmoothStep01(-0.01f, 0.42f, waterClearance);
    float cliffTaper = 0.55f + 0.45f * SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y + 0.35f, gy);
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
    if (pos.y < PLAYABLE_BASIN_FADE_Y || normal.y < 0.62f)
        return true;
    if (SampleWaterHeight(x, z) >= pos.y - 0.10f)
        return true;
    Color splat = SampleSplat(x, z);
    if (splat.g > 210)
        return true;
    return false;
}

static float Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)*rng / 4294967296.0f;
}

static void BuildFloraPlacements(void)
{
    // 1. Lush interlocking valley meadow:
    // Focused in central playable valley with dense 0.255m spacing for a continuous, velvety carpet
    float spanX = MAP_WIDTH * 0.28f;
    float spanZ = MAP_DEPTH * 0.28f;
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = {kMapCenter.x - spanX, kMapCenter.z - spanZ},
            .maxBounds = {kMapCenter.x + spanX, kMapCenter.z + spanZ},
            .spacing = 0.255f,
            .seed = 0x51a7c3u,
            .densityFn = SongQuaoGrassDensity,
            .ecology = &s_habitat,
            .flowerClusters = SONG_QUAO_FLOWER_CLUSTERS,
            .flowerClusterCount = SONG_QUAO_FLOWER_CLUSTER_COUNT,
            .edgeTaperFn = SongQuaoEdgeTaper,
        });
    TraceLog(LOG_INFO, "SONG_QUAO_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

    // 2. Reeds along riverbanks & wetland margins
    unsigned int rng = 0x9371a5u;
    s_reedCount = 0;
    for (int attempts = 0; attempts < 4200 && s_reedCount < REED_COUNT; attempts++)
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

        bool nearShore = (waterY > -CLIFF_DEPTH + 0.5f) && (clearance >= -0.01f && clearance <= 0.55f);
        bool wetMargin = (splat.a > 85) && (clearance >= 0.0f && clearance <= 1.25f);
        if (nearShore || wetMargin)
        {
            float rad = 0.17f + 0.07f * Random01(&rng);
            float h = 0.80f + 0.44f * Random01(&rng);
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = Random01(&rng) * 360.0f,
                .phase = Random01(&rng)};
        }
    }

    // 3. Hillside Japanese Pampas / Chè Cỏ Tranh Plumes (Ghost of Tsushima Aesthetic)
    s_pampasCount = 0;
    for (int attempts = 0; attempts < 6000 && s_pampasCount < PAMPAS_COUNT; attempts++)
    {
        float rx = 15.0f + Random01(&rng) * (MAP_WIDTH - 30.0f);
        float rz = 15.0f + Random01(&rng) * (MAP_DEPTH - 30.0f);
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal))
            continue;
        // Pampas colonizes rolling hill flanks and ridges above the valley floor:
        if (normal.y < 0.52f || normal.y > 0.92f || pos.y < PLAYABLE_BASIN_FADE_Y + 0.65f)
            continue;

        float waterY = SampleWaterHeight(rx, rz);
        if (pos.y - waterY < 1.2f)
            continue;

        Color splat = SampleSplat(rx, rz);
        if (splat.g > 200)
            continue;

        float rad = 0.24f + 0.08f * Random01(&rng);
        float h = 1.25f + 0.55f * Random01(&rng);
        s_pampasPlacements[s_pampasCount++] = (MapMeadowPlacement){
            .position = pos,
            .radius = rad,
            .height = h,
            .rotationDeg = Random01(&rng) * 360.0f,
            .phase = Random01(&rng)};
    }
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
    static const Vector2 kDistantCrags[] = {
        {120.0f, 25.0f},
        {105.0f, 40.0f},
        {80.0f, 30.0f},
        {35.0f, 280.0f},
        {55.0f, 270.0f},
        {155.0f, 265.0f},
        {20.0f, 160.0f},
        {175.0f, 140.0f},
        {45.0f, 90.0f},
        {165.0f, 80.0f},
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
        // Bury rock base 22-30% into the terrain so rocks emerge organically from the earth
        float sinkOffset = (item->archetype == MAP_ROCK_RIVER_STONE)
            ? (item->heightScale * 0.20f)
            : (item->heightScale * 0.28f);
        pos.y -= sinkOffset;
        MapProp_AddRockItem(&s_rocks, pos, item->radiusScale, item->heightScale, item->rotationDeg, item->archetype);
    }

    for (int i = 0; i < distantCragCount; i++)
    {
        Vector3 pos, normal;
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, kDistantCrags[i].x, kDistantCrags[i].y, &pos, &normal))
        {
            float rot = (float)(i * 67 % 360);
            float scale = 1.45f + 0.35f * (float)(i % 3);
            MapProp_AddRockItem(&s_rocks, pos, scale, scale * 1.25f, rot, MAP_ROCK_CRAG);
        }
    }
}

static void SpawnSongQuaoMistVolumes(void)
{
    FogVolume_ClearAll();

    LocalFogVolume riverValleyMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {115.0f, 0.45f, 130.0f},
        .extents = {16.0f, 0.55f, 12.0f},
        .color = {236, 246, 255, 255},
        .density = 0.08f,
        .edgeSoftness = 0.85f,
        .driftVelocity = {0.02f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&riverValleyMist);

    LocalFogVolume lakeBasinMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {SONG_QUAO_LAKE_CENTER_X, 0.35f, SONG_QUAO_LAKE_CENTER_Z},
        .extents = {SONG_QUAO_LAKE_RADIUS_X * 1.35f, 0.48f, SONG_QUAO_LAKE_RADIUS_Z * 1.35f},
        .color = {232, 245, 255, 255},
        .density = 0.09f,
        .edgeSoftness = 0.85f,
        .driftVelocity = {0.025f, 0.0f, 0.015f},
        .active = true};
    FogVolume_Create(&lakeBasinMist);

    LocalFogVolume centralMeadowMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {94.0f, 0.50f, 150.0f},
        .extents = {14.0f, 0.50f, 10.5f},
        .color = {238, 248, 255, 255},
        .density = 0.075f,
        .edgeSoftness = 0.85f,
        .driftVelocity = {0.02f, 0.0f, 0.01f},
        .active = true};
    FogVolume_Create(&centralMeadowMist);
}

static void DrawSongQuaoShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    if (!(s_skipMask & SQ_SKIP_SHADOWCASTERS) && !(s_skipMask & SQ_SKIP_FLORA))
    {
        MapProp_DrawFloraShadowCasters(&s_flora);
    }
}

static void CaptureSongQuaoStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
        return;
    if ((s_skipMask & SQ_SKIP_SHADOWCASTERS) || (s_skipMask & SQ_SKIP_ROCKS))
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
    ParseSkipMask();
    ColorGradeLut_Load("assets/maps/song_quao/song_quao_grade.png");

    Environment_SetTimeOfDaySpeed(0.0f);
    // Ghost of Tsushima golden-hour lighting: warm sun, cool sky ambient, back-lighting
    Environment_SetAmbientColor((Color){86, 110, 146, 255});
    Environment_SetSunColor((Color){255, 222, 172, 255});
    Environment_SetSunIntensity(2.15f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.42f, -0.38f, -0.82f}));
    Environment_SetShadowColor((Color){36, 48, 68, 90});

    AtmosphereProfile atmos = {
        .color = {228, 238, 248, 255},
        .start = 35.0f,
        .end = 250.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0078f, 0.0135f, 0.0265f},
            .mieScattering = 0.0028f,
            .mieAnisotropy = 0.78f,
            .multipleScatteringAmp = 2.15f},
        .density = {.baseDensity = 0.0065f, .heightFalloff = 0.35f, .baseAltitude = -1.5f, .enableSigmoidLayer = false}};
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(1.45f);
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.14f,
        .worldSize = 96.0f,
        .planeHeight = 80.0f,
        .coverage = 0.48f,
        .softness = 0.20f,
        .windSpeedScale = 0.50f,
    };
    Environment_SetCloudShadowConfig(&cloudConfig);
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

    s_splatImage = LoadImage("assets/maps/song_quao/song_quao_ecology_splat.png");
    s_splatLoaded = (s_splatImage.data != NULL);

    s_waterHeight = LoadImage("assets/maps/song_quao/song_quao_water_height.png");

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

    // Rich olive-moss earth substrate tint so exposed ground contrasts warmly against sunlit grass tips
    MapProp_SetGroundTint(&s_ground, (Color){46, 58, 34, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/verdant_meadow_substrate_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    MapBoundaryMistStyle rim = {2.4f, 10.5f, 0.45f, 1.9f, 0.50f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -3.85f, &rim))
        TraceLog(LOG_WARNING, "Song Quao: terrain boundary mist bake failed");

    BuildRocks();

    // Cull small 3D pebbles/twigs at 22m (invisible inside grass beyond 20m, saving draw calls)
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 22.0f);

    // Build Flora: slender tapered blades (0.102 width scale) + warm golden-emerald tip gradient + 60 FPS LODs
    BuildFloraPlacements();
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {24, 38, 16, 255},
            .grassTipColor = {128, 172, 62, 255},
            .bladesPerClump = 6,
            .bladeSegments = 4,
            .bladeWidthScale = 0.112f,
            .lodDistance = 24.0f,
            .midLodDistance = 12.0f,
            .drawDistance = 34.0f,
            .shadowDistance = 14.5f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {32, 46, 22, 255},
            .reedTipColor = {118, 162, 68, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 1.0f,
            .pampasPlacements = s_pampasPlacements,
            .pampasCount = s_pampasCount,
            .pampasRootColor = {44, 52, 28, 255},
            .pampasTipColor = {246, 238, 218, 255},
        });

    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .radiusX = MAP_WIDTH * 0.5f,
                                             .radiusZ = MAP_DEPTH * 0.5f,
                                             .waveHeight = 0.016f,
                                             .waveScale = 1.65f,
                                             .waveSpeed = 0.85f,
                                             .flowVelocity = {0.24f, 0.38f},
                                             .detailScale = 0.18f,
                                             .detailStrength = 0.28f,
                                             .maxDepth = 1.35f,
                                             .absorption = {0.60f, 0.18f, 0.08f},
                                             .scatterColor = {0.16f, 0.54f, 0.48f},
                                             .scatterCoeff = 0.35f,
                                             .causticsStrength = 0.65f,
                                             .causticsScale = 1.45f,
                                             .foamThreshold = 0.105f,
                                             .deepColor = {10, 32, 44, 255},
                                             .shallowColor = {68, 162, 142, 160},
                                             .foamColor = {240, 248, 244, 210},
                                         });

    EnvShadow_SetFocus(kMapCenter, 12.5f);
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

    // Snap dynamic shadow focus to 0.25m steps and tighten radius to 12.5m for higher texel density & fewer casters
    Vector3 focus = {
        floorf(camera.target.x * 4.0f + 0.5f) * 0.25f,
        0.0f,
        floorf(camera.target.z * 4.0f + 0.5f) * 0.25f};
    EnvShadow_SetFocus(focus, 12.5f);
    CaptureSongQuaoStaticShadows();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    if (!(s_skipMask & SQ_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);

    bool useFakeRockShadows = !EnvShadow_IsEnabled();
    if (!(s_skipMask & SQ_SKIP_ROCKS))
        MapProp_DrawRockSystem(&s_rocks, useFakeRockShadows);

    if (!(s_skipMask & SQ_SKIP_DEBRIS))
        MapProp_DrawDebrisSystem(&s_debris, camera.position);
    if (!(s_skipMask & SQ_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);
    if (!(s_skipMask & SQ_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
    if (!(s_skipMask & SQ_SKIP_CLOUDSEA))
        MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & SQ_SKIP_WATER))
        MapProp_DrawWaterOverlay(&s_river, GetTime());
    if (!(s_skipMask & SQ_SKIP_MIST))
        MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
}

void UnloadSongQuaoMap(void)
{
    ColorGradeLut_UseNeutral();
    Environment_SetSunIntensity(1.0f);
    if (!s_ready)
        return;
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
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

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define MAP_WIDTH SONG_QUAO_MAP_WIDTH
#define MAP_DEPTH SONG_QUAO_MAP_DEPTH
#define CLIFF_DEPTH SONG_QUAO_CLIFF_DEPTH
#define CLOUD_SEA_Y -4.20f

// Broad valley meadow coverage across the celestial island plateau and rolling hills
#define GRASS_TUFT_CAPACITY 98000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 340
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

static Vector3 s_lastShadowFocus = {-9999.0f, 0.0f, -9999.0f};
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

// Subtractive GPU profiling skip mask (WUXING_SONGQUAO_SKIP)
enum
{
    SQ_SKIP_GROUND = 1 << 0,
    SQ_SKIP_ROCKS = 1 << 1,
    SQ_SKIP_DEBRIS = 1 << 2,
    SQ_SKIP_FLORA = 1 << 3,
    SQ_SKIP_SKY = 1 << 4,
    SQ_SKIP_CLOUDSEA = 1 << 5,
    SQ_SKIP_WATER = 1 << 6,
    SQ_SKIP_MIST = 1 << 7,
    SQ_SKIP_SHADOWCASTERS = 1 << 8,
    SQ_SKIP_FLUFF = 1 << 9,
    SQ_SKIP_CLOUDSHADOW = 1 << 10,
    SQ_SKIP_FOGVOLUMES = 1 << 11,
    SQ_SKIP_GODRAYS = 1 << 12,
    SQ_SKIP_GRADE = 1 << 13,
};
static unsigned int s_skipMask = 0;

static void ParseSkipMask(void)
{
    s_skipMask = 0;
    const char *env = getenv("WUXING_SONGQUAO_SKIP");
    if (!env || !*env)
        return;
    if (strstr(env, "ground"))
        s_skipMask |= SQ_SKIP_GROUND;
    if (strstr(env, "rocks"))
        s_skipMask |= SQ_SKIP_ROCKS;
    if (strstr(env, "debris"))
        s_skipMask |= SQ_SKIP_DEBRIS;
    if (strstr(env, "flora") || strstr(env, "grass"))
        s_skipMask |= SQ_SKIP_FLORA;
    if (strstr(env, "sky"))
        s_skipMask |= SQ_SKIP_SKY;
    if (strstr(env, "cloudsea"))
        s_skipMask |= SQ_SKIP_CLOUDSEA;
    if (strstr(env, "water"))
        s_skipMask |= SQ_SKIP_WATER;
    if (strstr(env, "mist"))
        s_skipMask |= SQ_SKIP_MIST;
    if (strstr(env, "shadowcasters") || strstr(env, "casters"))
        s_skipMask |= SQ_SKIP_SHADOWCASTERS;
    if (strstr(env, "fluff"))
        s_skipMask |= SQ_SKIP_FLUFF;
    if (strstr(env, "cloudshadow"))
        s_skipMask |= SQ_SKIP_CLOUDSHADOW;
    if (strstr(env, "fogvolumes"))
        s_skipMask |= SQ_SKIP_FOGVOLUMES;
    if (strstr(env, "godrays"))
        s_skipMask |= SQ_SKIP_GODRAYS;
    if (strstr(env, "grade"))
        s_skipMask |= SQ_SKIP_GRADE;
}

// Ecology texture lookup helper
static Image s_splatImage;
static bool s_splatLoaded = false;

static inline float SmoothStep01(float edge0, float edge1, float x)
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

    // Organic boundary at island cliff drop-off into cloud sea
    if (pos.y < PLAYABLE_BASIN_MIN_Y || normal.y < 0.38f)
        return 0.0f;
    float heightFade = SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y, pos.y);
    float slopeFade = SmoothStep01(0.38f, 0.56f, normal.y);

    // Smooth shoreline transition right up to the water's edge
    float surfaceY = SampleWaterHeight(x, z);
    float waterClearance = pos.y - surfaceY;
    if (waterClearance < 0.02f)
        return 0.0f;
    float shoreFade = SmoothStep01(0.02f, 0.24f, waterClearance);

    Color splat = SampleSplat(x, z);
    // Gate rockScree by actual geometric slope (1.0 - normal.y), matching ground_splat.fs wCliff
    // so gentle grassy hills never get bare contour strips carved out of them.
    float geomSlope = 1.0f - normal.y;
    float rockScree = ((float)splat.g / 255.0f) * SmoothStep01(0.18f, 0.45f, geomSlope);
    if (rockScree > 0.88f)
        return 0.0f;
    float rockFade = 1.0f - SmoothStep01(0.65f, 0.88f, rockScree);

    // Coherent macro meadow swells with organic breathing room
    float warpedX = x + sinf(z * 0.12f) * 2.6f;
    float macroField = sinf(warpedX * 0.075f + z * 0.048f) * 0.55f +
                       sinf(z * 0.11f - x * 0.042f + 1.2f) * 0.30f +
                       sinf(x * 0.19f + z * 0.15f) * 0.15f;
    float patch = SmoothStep01(-0.35f, 0.35f, macroField);
    float macro = 0.68f + 0.32f * patch;

    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(
        x, z, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);

    return fmaxf(0.0f, fminf(1.0f, macro * heightFade * slopeFade * shoreFade * rockFade * flowerSuppression));
}

static float SongQuaoEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, cx, cz);
    float waterClearance = gy - SampleWaterHeight(cx, cz);
    // Gentle shoreline & cliff height taper so peripheral tufts hug the earth
    float shoreTaper = 0.54f + 0.46f * SmoothStep01(0.0f, 0.45f, waterClearance);
    float cliffTaper = 0.58f + 0.42f * SmoothStep01(PLAYABLE_BASIN_MIN_Y, PLAYABLE_BASIN_FADE_Y + 0.40f, gy);
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
    if (SampleWaterHeight(x, z) >= pos.y - 0.14f)
        return true;
    Color splat = SampleSplat(x, z);
    if (splat.g > 205)
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
    // 1. Lush interlocking meadow across the whole floating island (central plateau, northern peak & southern ridge)
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
    unsigned int rng = 0x9371a5u;
    s_reedCount = 0;
    for (int attempts = 0; attempts < 3600 && s_reedCount < REED_COUNT; attempts++)
    {
        float rx = kMapCenter.x + (Random01(&rng) * 2.0f - 1.0f) * spanX * 0.92f;
        float rz = kMapCenter.z + (Random01(&rng) * 2.0f - 1.0f) * spanZ * 0.92f;
        float rnx = (rx - kMapCenter.x) / (spanX * 0.92f);
        float rnz = (rz - kMapCenter.z) / (spanZ * 0.92f);
        if (rnx * rnx + rnz * rnz > 1.0f)
            continue;
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal))
            continue;
        if (normal.y < 0.70f || pos.y < PLAYABLE_BASIN_MIN_Y)
            continue;

        float waterY = SampleWaterHeight(rx, rz);
        float clearance = pos.y - waterY;
        Color splat = SampleSplat(rx, rz);

        bool nearShore = (waterY > -CLIFF_DEPTH + 0.5f) && (clearance >= 0.01f && clearance <= 0.42f);
        bool wetMargin = (splat.a > 95) && (clearance >= 0.02f && clearance <= 0.85f);
        if (nearShore || wetMargin)
        {
            // Cluster reeds slightly using spatial wave so they form natural waterside beds
            float reedCluster = sinf(rx * 0.35f + rz * 0.28f) * cosf(rz * 0.42f - rx * 0.19f);
            if (reedCluster < -0.15f)
                continue;

            float rad = 0.24f + 0.08f * Random01(&rng);
            float h = 0.44f + 0.18f * Random01(&rng);
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = Random01(&rng) * 360.0f,
                .phase = Random01(&rng)};
        }
    }

    // 3. Clustered Hillside Silvergrass / Pampas Drifts (Ghost of Tsushima style):
    // Grouped into organic wind-swept colonies inside the valley meadow so isolated tufts never speck bare distant hills.
    s_pampasCount = 0;
    for (int attempts = 0; attempts < 5200 && s_pampasCount < PAMPAS_COUNT; attempts++)
    {
        float rx = kMapCenter.x + (Random01(&rng) * 2.0f - 1.0f) * spanX * 0.86f;
        float rz = kMapCenter.z + (Random01(&rng) * 2.0f - 1.0f) * spanZ * 0.86f;
        float pnx = (rx - kMapCenter.x) / (spanX * 0.86f);
        float pnz = (rz - kMapCenter.z) / (spanZ * 0.86f);
        if (pnx * pnx + pnz * pnz > 1.0f)
            continue;

        // Keep central hero clearing free of tall pampas spikes at close zoom
        float cdx = rx - kMapCenter.x;
        float cdz = rz - kMapCenter.z;
        if (cdx * cdx + cdz * cdz < 11.5f * 11.5f)
            continue;

        // Coherent colony mask: only spawn inside natural hillside drifts
        float driftMask = sinf(rx * 0.17f + sinf(rz * 0.09f) * 1.8f) *
                          cosf(rz * 0.15f - rx * 0.08f);
        if (driftMask < 0.30f)
            continue;

        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal))
            continue;
        if (normal.y < 0.54f || pos.y < PLAYABLE_BASIN_FADE_Y + 0.45f)
            continue;

        float waterY = SampleWaterHeight(rx, rz);
        if (pos.y - waterY < 0.85f)
            continue;

        Color splat = SampleSplat(rx, rz);
        if (splat.g > 190)
            continue;

        // Avoid spawning tall stalks directly inside wildflower beds
        float flowerSupp = MapFlora_EvaluateFlowerSuppression(
            rx, rz, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);
        if (flowerSupp < 0.75f)
            continue;

        // Graceful arching proportions (0.46m - 0.64m) with wide base radius so blades arch outward
        float colonyWeight = SmoothStep01(0.30f, 0.75f, driftMask);
        float rad = 0.30f + 0.10f * Random01(&rng);
        float h = (0.48f + 0.14f * colonyWeight) * (0.90f + 0.18f * Random01(&rng));
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
        float sinkOffset = (item->archetype == MAP_ROCK_RIVER_STONE)
                               ? (item->heightScale * 0.44f)
                               : (item->heightScale * 0.32f);
        float heightScale = (item->archetype == MAP_ROCK_RIVER_STONE)
                                ? (item->heightScale * 0.68f)
                                : item->heightScale;
        pos.y -= sinkOffset;
        MapProp_AddRockItem(&s_rocks, pos, item->radiusScale, heightScale, item->rotationDeg, item->archetype);
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

// Batched single-draw-call dandelion/pollen spores (replaces 140 individual DrawBillboardPro calls)
#define AMBIENT_FLUFF_COUNT 64

typedef struct AmbientFluffParticle
{
    Vector3 position;
    float phase;
    float size;
    float speed;
    float rot;
    float rotSpeed;
    float opacity;
} AmbientFluffParticle;

static AmbientFluffParticle s_fluffParticles[AMBIENT_FLUFF_COUNT];
static Texture2D s_fluffTexture = {0};
static bool s_fluffInitialized = false;

static void InitAmbientFluff(void)
{
    if (s_fluffTexture.id == 0)
    {
        Image img = GenImageColor(16, 16, (Color){0, 0, 0, 0});
        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x++)
            {
                float dx = (x - 7.5f) / 7.5f;
                float dy = (y - 7.5f) / 7.5f;
                float r2 = dx * dx + dy * dy;
                if (r2 < 1.0f)
                {
                    float a = powf(1.0f - sqrtf(r2), 1.7f);
                    ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(a * 255.0f)});
                }
            }
        }
        s_fluffTexture = LoadTextureFromImage(img);
        SetTextureFilter(s_fluffTexture, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }

    unsigned int rng = 0x6a1f29u;
    for (int i = 0; i < AMBIENT_FLUFF_COUNT; i++)
    {
        float theta = Random01(&rng) * 2.0f * PI;
        float dist = 2.0f + sqrtf(Random01(&rng)) * 14.5f;
        float h = 0.45f + Random01(&rng) * 3.8f;
        s_fluffParticles[i].position = (Vector3){
            kMapCenter.x + cosf(theta) * dist,
            h,
            kMapCenter.z + sinf(theta) * dist};
        s_fluffParticles[i].phase = Random01(&rng) * 2.0f * PI;
        s_fluffParticles[i].size = 0.034f + 0.026f * Random01(&rng);
        s_fluffParticles[i].speed = 0.75f + 0.40f * Random01(&rng);
        s_fluffParticles[i].rot = Random01(&rng) * 2.0f * PI;
        s_fluffParticles[i].rotSpeed = (Random01(&rng) - 0.5f) * 1.1f;
        s_fluffParticles[i].opacity = 0.42f + 0.45f * Random01(&rng);
    }
    s_fluffInitialized = true;
}

static void UpdateAmbientFluff(Vector3 cameraTarget, float dt)
{
    if (!s_fluffInitialized)
        return;
    Vector3 windDir = {0.78f, -0.04f, 0.62f};
    float time = (float)GetTime();

    for (int i = 0; i < AMBIENT_FLUFF_COUNT; i++)
    {
        AmbientFluffParticle *p = &s_fluffParticles[i];
        float wave = sinf(time * 1.35f + p->phase);
        float lift = cosf(time * 0.85f + p->phase * 1.5f);

        p->position.x += (windDir.x * p->speed + wave * 0.11f) * dt;
        p->position.y += (windDir.y * p->speed + lift * 0.07f) * dt;
        p->position.z += (windDir.z * p->speed - wave * 0.09f) * dt;
        p->rot += p->rotSpeed * dt;

        float dx = p->position.x - cameraTarget.x;
        float dz = p->position.z - cameraTarget.z;
        float distSq = dx * dx + dz * dz;

        if (distSq > 256.0f || p->position.y < PLAYABLE_BASIN_MIN_Y)
        {
            float angle = (atan2f(windDir.z, windDir.x) + PI) + ((float)(i % 11) - 5.0f) * 0.22f;
            float spawnDist = 14.5f + (float)(i % 7) * 0.25f;
            p->position.x = cameraTarget.x + cosf(angle) * spawnDist;
            p->position.z = cameraTarget.z + sinf(angle) * spawnDist;
            float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, p->position.x, p->position.z);
            p->position.y = fmaxf(gy + 0.5f, PLAYABLE_BASIN_FADE_Y + 0.4f) + ((float)(i % 5)) * 0.65f;
        }
    }
}

static void DrawAmbientFluff(void)
{
    if (!s_fluffInitialized || s_fluffTexture.id == 0)
        return;

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    Color sunColor = Environment_GetSunColor();
    unsigned char baseR = (unsigned char)fminf(255.0f, 248.0f * (sunColor.r / 255.0f) * 1.08f);
    unsigned char baseG = (unsigned char)fminf(255.0f, 240.0f * (sunColor.g / 255.0f) * 1.08f);
    unsigned char baseB = (unsigned char)fminf(255.0f, 212.0f * (sunColor.b / 255.0f) * 1.02f);

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    rlSetTexture(s_fluffTexture.id);
    rlBegin(RL_QUADS);

    for (int i = 0; i < AMBIENT_FLUFF_COUNT; i++)
    {
        const AmbientFluffParticle *p = &s_fluffParticles[i];
        float dx = p->position.x - camera.position.x;
        float dy = p->position.y - camera.position.y;
        float dz = p->position.z - camera.position.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > 256.0f || distSq < 0.36f)
            continue;

        float camDist = sqrtf(distSq);
        float fade = p->opacity * SmoothStep01(0.6f, 1.8f, camDist) * (1.0f - SmoothStep01(12.0f, 16.0f, camDist));
        unsigned char alpha = (unsigned char)(255.0f * fade);
        if (alpha < 4)
            continue;

        float halfSize = p->size * 0.5f;
        float c = cosf(p->rot) * halfSize;
        float s = sinf(p->rot) * halfSize;

        Vector3 rx = {right.x * c - up.x * s, right.y * c - up.y * s, right.z * c - up.z * s};
        Vector3 ry = {right.x * s + up.x * c, right.y * s + up.y * c, right.z * s + up.z * c};

        Vector3 v0 = {p->position.x - rx.x - ry.x, p->position.y - rx.y - ry.y, p->position.z - rx.z - ry.z};
        Vector3 v1 = {p->position.x + rx.x - ry.x, p->position.y + rx.y - ry.y, p->position.z + rx.z - ry.z};
        Vector3 v2 = {p->position.x + rx.x + ry.x, p->position.y + rx.y + ry.y, p->position.z + rx.z + ry.z};
        Vector3 v3 = {p->position.x - rx.x + ry.x, p->position.y - rx.y + ry.y, p->position.z - rx.z + ry.z};

        rlColor4ub(baseR, baseG, baseB, alpha);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(v0.x, v0.y, v0.z);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }

    rlEnd();
    rlSetTexture(0);

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
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
    if (s_skipMask & SQ_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else
        ColorGradeLut_Load("assets/maps/song_quao/song_quao_grade.png");

    Environment_SetTimeOfDaySpeed(0.0f);
    // Ghost of Tsushima / Where Winds Meet cinematic golden-hour lighting:
    // Rich cerulean-slate sky fill so shadowed grass retains lush depth instead of turning muddy black
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
    VolumetricFog_SetGodRayIntensity((s_skipMask & SQ_SKIP_GODRAYS) ? 0.0f : 1.35f);
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
    Environment_SetCloudShadowConfig((s_skipMask & SQ_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);
    if (s_skipMask & SQ_SKIP_FOGVOLUMES)
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

    // Cull small 3D pebbles/twigs at 14m (saves prop_lit draw calls when hidden inside dense turf)
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 14.0f);

    // Build Flora with Ghost of Tsushima slender curved Bezier blades (0.112 width scale),
    // 6 blades distributed across each tuft disk, Voronoi patch clumping, and seamless 2-segment curved LODs
    BuildFloraPlacements();
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {30, 50, 22, 255},  // Sắc xanh rêu rừng sâu tạo độ tương phản tự nhiên ở gốc cỏ
            .grassTipColor = {112, 166, 62, 255}, // Sắc xanh ngọc bích Tsushima thanh tao, không bị bạc màu dưới nắng trưa
            .bladesPerClump = 6,
            .bladeSegments = 4,
            .bladeWidthScale = 0.108f, // Lá cỏ thanh mảnh, uốn cong theo luồng gió thay vì bản rộng cứng
            .lodDistance = 24.0f,
            .midLodDistance = 12.0f,   // 3-tier Bezier LOD: Near (6b x 4s) -> Mid (3b x 2s) -> Far (2b x 2s)
            .drawDistance = 88.0f,     // Tầm nhìn bao quát trọn vẹn toàn bộ các rặng đồi của hòn đảo thiên giới
            .shadowDistance = 14.0f,   // Khôi phục bóng đổ thực (real shadow casters) cho thảm cỏ và khóm hoa
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {30, 48, 22, 255},
            .reedTipColor = {108, 156, 62, 255},
            .hasReedPlumes = false, // Eliminates stiff white card artifacts on reeds (API.md §1b)
            .botanicalVariation = 1.0f,
            .pampasPlacements = NULL,
            .pampasCount = 0,
            .pampasRootColor = {36, 52, 24, 255},
            .pampasTipColor = {138, 168, 84, 255},
        });

    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
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

    InitAmbientFluff();
    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready)
        return;
    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_river, dt);
    if (!(s_skipMask & SQ_SKIP_FLUFF))
        UpdateAmbientFluff(camera.target, dt);

    // Snap dynamic shadow focus to 0.5m grid and update only when focus cell shifts
    Vector3 focus = {
        floorf(camera.target.x * 2.0f + 0.5f) * 0.5f,
        0.0f,
        floorf(camera.target.z * 2.0f + 0.5f) * 0.5f};
    if (fabsf(focus.x - s_lastShadowFocus.x) > 0.01f || fabsf(focus.z - s_lastShadowFocus.z) > 0.01f)
    {
        s_lastShadowFocus = focus;
        EnvShadow_SetFocus(focus, 8.8f);
    }
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
    if (!(s_skipMask & SQ_SKIP_FLUFF))
        DrawAmbientFluff();
}

void UnloadSongQuaoMap(void)
{
    ColorGradeLut_UseNeutral();
    Environment_SetSunIntensity(1.0f);
    if (!s_ready)
        return;
    if (s_fluffTexture.id > 0)
    {
        UnloadTexture(s_fluffTexture);
        s_fluffTexture = (Texture2D){0};
    }
    s_fluffInitialized = false;

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
    s_lastShadowFocus = (Vector3){-9999.0f, 0.0f, -9999.0f};
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
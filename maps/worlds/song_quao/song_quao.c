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
#define CLOUD_SEA_Y -7.35f
#define GRASS_TUFT_CAPACITY 65000
#define FLOWER_COUNT SONG_QUAO_TOTAL_FLOWERS
#define REED_COUNT 650

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};
static const Vector3 kOutletPos = {SONG_QUAO_OUTLET_X, 0.0f, SONG_QUAO_OUTLET_Z};
static const Vector3 kPeakPos   = {SONG_QUAO_PEAK_X, SONG_QUAO_HEIGHT_RANGE, SONG_QUAO_PEAK_Z};
static const Vector3 kLakePos   = {SONG_QUAO_LAKE_CENTER_X, SONG_QUAO_LAKE_WATER_Y, SONG_QUAO_LAKE_CENTER_Z};

static const MapZone SONG_QUAO_ZONES[] = {
    {NAT_RIVER,  {SONG_QUAO_LAKE_CENTER_X, 0.0f, SONG_QUAO_LAKE_CENTER_Z}, SONG_QUAO_LAKE_RADIUS_X},
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

static Color SampleSplat(float x, float z)
{
    if (!s_splatLoaded) return (Color){255, 0, 0, 0};
    int tx = (int)((x / MAP_WIDTH) * (float)(s_splatImage.width - 1) + 0.5f);
    int tz = (int)((z / MAP_DEPTH) * (float)(s_splatImage.height - 1) + 0.5f);
    if (tx < 0) tx = 0; if (tx >= s_splatImage.width) tx = s_splatImage.width - 1;
    if (tz < 0) tz = 0; if (tz >= s_splatImage.height) tz = s_splatImage.height - 1;
    return GetImageColor(s_splatImage, tx, tz);
}

static float SampleWaterHeight(float x, float z)
{
    if (!s_waterHeight.data || x < 0.0f || z < 0.0f || x > MAP_WIDTH || z > MAP_DEPTH)
        return -CLIFF_DEPTH;
    float fx = x / MAP_WIDTH * (float)(s_waterHeight.width - 1);
    float fz = z / MAP_DEPTH * (float)(s_waterHeight.height - 1);
    int ix = (int)fminf(floorf(fx), (float)(s_waterHeight.width - 2));
    int iz = (int)fminf(floorf(fz), (float)(s_waterHeight.height - 2));
    float tx = fx - (float)ix, tz = fz - (float)iz;
    Color c00 = GetImageColor(s_waterHeight, ix, iz);
    Color c10 = GetImageColor(s_waterHeight, ix + 1, iz);
    Color c01 = GetImageColor(s_waterHeight, ix, iz + 1);
    Color c11 = GetImageColor(s_waterHeight, ix + 1, iz + 1);
    float h00 = (float)(c00.r * 256 + c00.g);
    float h10 = (float)(c10.r * 256 + c10.g);
    float h01 = (float)(c01.r * 256 + c01.g);
    float h11 = (float)(c11.r * 256 + c11.g);
    float code = tx + tz <= 1.0f ? h00 + tx * (h10 - h00) + tz * (h01 - h00)
        : h11 + (1.0f - tx) * (h01 - h11) + (1.0f - tz) * (h10 - h11);
    return code * ((CLIFF_DEPTH + SONG_QUAO_HEIGHT_RANGE) / 65535.0f) - CLIFF_DEPTH;
}

static float SongQuaoGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    if (gy < -0.25f || SampleWaterHeight(x, z) > gy - 0.025f) return 0.0f;
    return (float)SampleSplat(x, z).r / 255.0f;
}

static float SongQuaoSampleGroundHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static bool SongQuaoIsFlowerExcluded(float x, float z, void *ctx)
{
    (void)ctx;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    if (gy < -0.2f || SampleWaterHeight(x, z) >= gy - 0.02f) return true;
    Color splat = SampleSplat(x, z);
    if (splat.g > 180) return true; // excluded on steep rocky cliff faces
    return false;
}

static float Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)*rng / 4294967296.0f;
}

static void BuildFloraPlacements(void)
{
    // 1. Generate dense meadow grass in valleys & gentle terraces
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapMeadowDistribution){
            .minBounds = {6.0f, 6.0f}, .maxBounds = {MAP_WIDTH - 6.0f, MAP_DEPTH - 6.0f},
            .spacing = 0.42f, .jitter = 0.85f,
            .minRadius = 0.28f, .maxRadius = 0.40f,
            .minHeight = 0.45f, .maxHeight = 0.72f,
            .yOffset = 0.025f, .seed = 0x51a7c3u,
        }, SongQuaoGrassDensity, NULL);

    for (int i = 0; i < s_grassCount; i++) {
        MapMeadowPlacement *clump = &s_grassPlacements[i];
        Color splat = SampleSplat(clump->position.x, clump->position.z);
        float vigor = (float)splat.r / 255.0f;
        clump->height *= 0.80f + 0.35f * vigor;
        // Suppress meadow canopy inside wildflower colonies so blooms emerge cleanly
        float understorey = MapFlora_EvaluateFlowerUnderstorey(
            clump->position.x, clump->position.z,
            SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT);
        clump->height *= understorey;
    }

    // 2. Reeds along riverbanks & wetland margins
    unsigned int rng = 0x9371a5u;
    s_reedCount = 0;
    for (int attempts = 0; attempts < 3500 && s_reedCount < REED_COUNT; attempts++) {
        float rx = 8.0f + Random01(&rng) * (MAP_WIDTH - 16.0f);
        float rz = 8.0f + Random01(&rng) * (MAP_DEPTH - 16.0f);
        Color splat = SampleSplat(rx, rz);
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal)) continue;
        if (normal.y < 0.70f || pos.y < -CLIFF_DEPTH + 0.45f) continue;

        float waterY = SampleWaterHeight(rx, rz);
        if (waterY >= pos.y - 0.02f) continue; // must be above water
        if (splat.a > 70) {
            float rad = 0.18f + 0.08f * Random01(&rng);
            float h = 0.85f + 0.50f * Random01(&rng);
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = Random01(&rng) * 360.0f,
                .phase = Random01(&rng)
            };
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
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png",
        "assets/textures/rock_roughness.png",
        SONG_QUAO_ROCK_ITEM_COUNT);

    for (int i = 0; i < SONG_QUAO_ROCK_ITEM_COUNT; i++) {
        const MapRockItem *item = &SONG_QUAO_ROCK_ITEMS[i];
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, item->position.x, item->position.z, &pos, &normal)) {
            pos = item->position;
        }
        MapProp_AddRockItem(&s_rocks, pos, item->radiusScale, item->heightScale, item->rotationDeg, item->archetype);
    }
}

static void DrawSongQuaoShadowCasters(Shader depthShader, void *userData)
{
    (void)userData;
    MapProp_DrawFloraShadowCasters(&s_flora);
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
}

static void CaptureSongQuaoStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture()) return;
    EnvShadow_BeginStaticCapture(kMapCenter, 64.0f);
    if (!EnvShadow_IsCapturing()) return;
    Shader depthShader = EnvShadow_GetDepthShader();
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
    EnvShadow_EndStaticCapture();
}

void InitSongQuaoMap(void)
{
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){88, 102, 128, 255}); // Cool sky fill preserves shaded canopy volume
    Environment_SetSunColor((Color){255, 224, 185, 255});
    Environment_SetSunIntensity(2.2f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){-0.25f, -0.45f, 0.85f}));
    Environment_SetShadowColor((Color){28, 36, 48, 120});

    AtmosphereProfile atmos = {
        .color = {225, 235, 248, 255},
        .start = 30.0f,
        .end = 220.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0076224f, 0.012935f, 0.024845f},
            .mieScattering = 0.0024f,
            .mieAnisotropy = 0.66f,
            .multipleScatteringAmp = 1.7f
        },
        .density = {
            .baseDensity = 0.008f,
            .heightFalloff = 0.20f,
            .baseAltitude = 0.0f,
            .enableSigmoidLayer = false
        }
    };
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(0.75f);
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true, .strength = 0.12f, .worldSize = 96.0f,
        .planeHeight = 80.0f, .coverage = 0.48f, .softness = 0.16f, .windSpeedScale = 0.55f,
    };
    Environment_SetCloudShadowConfig(&cloudConfig);

    if (s_ready) return;

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif

    // Load splat image for sampling
    s_splatImage = LoadImage("assets/maps/song_quao/song_quao_ecology_splat.png");
    s_splatLoaded = (s_splatImage.data != NULL);

    s_waterHeight = LoadImage("assets/maps/song_quao/song_quao_water_height.png");

    // Heightmap terrain using universal heightmap loader
    s_ground = MapProp_CreateGroundHeightfield16(
        "assets/maps/song_quao/song_quao_heightfield.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + SONG_QUAO_HEIGHT_RANGE, CLIFF_DEPTH, 3.6f, "assets/maps/song_quao/song_quao_ecology_splat.png",
        "assets/textures/verdant_meadow_substrate_diffuse.png", "assets/textures/dirt_diffuse.png");
    s_habitat.habitatTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/song_quao/song_quao_habitat.png", true,
        TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP);
    s_habitat.distanceTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/song_quao/song_quao_habitat_distance.png", false,
        TEXTURE_FILTER_BILINEAR, TEXTURE_WRAP_CLAMP);
    s_habitat.rect = (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH};
    s_habitat.ready = s_habitat.habitatTexture.id > 0 && s_habitat.distanceTexture.id > 0;
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    MapProp_SetGroundTint(&s_ground, (Color){76, 96, 52, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
        "assets/textures/verdant_meadow_substrate_material.png",
        "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/song_quao/song_quao_debris_map.png");

    // Sky dome & Cloud Sea
    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    MapBoundaryMistStyle rim = {2.2f, 9.5f, 0.40f, 1.8f, 0.55f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -6.85f, &rim))
        TraceLog(LOG_WARNING, "Song Quao: terrain boundary mist bake failed");

    // Build Rocks (Mountain crags, talus clusters, river stones)
    BuildRocks();

    // Build Debris System (Fallen leaves, scree pebbles, twigs)
    s_debris = MapProp_CreateDebrisSystem(
        SONG_QUAO_DEBRIS_ITEMS, SONG_QUAO_DEBRIS_ITEM_COUNT,
        &s_ground, kMapCenter, 42.0f);

    // Build Flora (Lush valley grass, reeds, wildflower colonies)
    BuildFloraPlacements();
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, SONG_QUAO_FLOWER_CLUSTERS, SONG_QUAO_FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {18, 34, 16, 255},
            .grassTipColor  = {136, 186, 54, 255},
            .bladesPerClump = 6,
            .bladeSegments  = 4,
            .bladeWidthScale = 0.16f,
            .lodDistance = 32.0f,
            .drawDistance = 60.0f,
            .shadowDistance = 16.0f,
            .reedRootColor = {26, 42, 20, 255},
            .reedTipColor  = {136, 172, 82, 255},
            .hasReedPlumes = false,
        });

    // Terrain-clipped river & reservoir surface
    s_river = MapProp_CreateTerrainWater("assets/maps/song_quao/song_quao_water.obj",
        (MapWaterConfig){
            .ecosystem = WATER_ECO_CUSTOM,
            .radiusX = MAP_WIDTH * 0.5f, .radiusZ = MAP_DEPTH * 0.5f,
            .waveHeight = 0.024f, .waveScale = 1.15f, .waveSpeed = 0.65f,
            .detailScale = 0.085f, .detailStrength = 0.16f,
            .maxDepth = 1.5f, .absorption = {0.65f, 0.30f, 0.16f},
            .scatterColor = {0.13f, 0.24f, 0.18f}, .scatterCoeff = 0.24f,
            .causticsStrength = 0.32f, .causticsScale = 1.35f,
            .foamThreshold = 0.08f,
            .deepColor = {18, 48, 46, 255}, .shallowColor = {76, 100, 72, 255},
            .foamColor = {215, 227, 216, 255},
        });

    EnvShadow_SetFocus(kMapCenter, 20.0f);
    EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
    MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);

    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready) return;
    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_river, dt);

    Vector3 focus = {camera.target.x, 0.0f, camera.target.z};
    EnvShadow_SetFocus(focus, 20.0f);
    CaptureSongQuaoStaticShadows();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready) return;
    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawRockSystem(&s_rocks, true);
    MapProp_DrawDebrisSystem(&s_debris, camera.position);
    MapProp_DrawFloraSystem(&s_flora);
    MapProp_DrawSkyDome(&s_sky);
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready) return;
    MapProp_DrawWaterOverlay(&s_river, GetTime());
    MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
}

void UnloadSongQuaoMap(void)
{
    Environment_SetSunIntensity(1.0f);
    if (!s_ready) return;
    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);

    MapProp_UnloadWaterSurface(&s_river);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
    MapProp_UnloadRockSystem(&s_rocks);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);

    if (s_waterHeight.data) { UnloadImage(s_waterHeight); s_waterHeight = (Image){0}; }
    s_habitat = (MapEcology){0};
    if (s_splatLoaded) {
        UnloadImage(s_splatImage);
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
    if (splat.g == 255 && splat.r == 0 && splat.b == 0 && splat.a == 0) {
        float rawY = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
        if (rawY <= -CLIFF_DEPTH + 0.15f)
            return -100.0f;
    }
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

bool SampleGroundSurfaceSongQuaoMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (x < 1.0f || x > MAP_WIDTH - 1.0f || z < 1.0f || z > MAP_DEPTH - 1.0f) {
        if (outPosition) *outPosition = (Vector3){x, -100.0f, z};
        if (outNormal) *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        return false;
    }
    Color splat = SampleSplat(x, z);
    if (splat.g == 255 && splat.r == 0 && splat.b == 0 && splat.a == 0) {
        float rawY = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
        if (rawY <= -CLIFF_DEPTH + 0.15f) {
            if (outPosition) *outPosition = (Vector3){x, -100.0f, z};
            if (outNormal) *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
            return false;
        }
    }
    return MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, outPosition, outNormal);
}

bool GetWaterInfoSongQuaoMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready || x < 0 || z < 0 || x > MAP_WIDTH || z > MAP_DEPTH) return false;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    float surfaceY = SampleWaterHeight(x, z);
    if (surfaceY <= gy + 0.005f) return false;
    if (outSurfaceY) *outSurfaceY = surfaceY;
    if (outWaterDepth) *outWaterDepth = surfaceY - gy;
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

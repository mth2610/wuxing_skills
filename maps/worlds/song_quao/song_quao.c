#include "song_quao.h"
#include "assets/maps/song_quao/song_quao_generated_meta.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "core/camera_context.h"
#include "core/map_manager.h"
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
#define CLOUD_SEA_Y -18.0f
#define ROCK_COUNT 24
#define GRASS_TUFT_CAPACITY 65000
#define FLOWER_COUNT 600
#define REED_COUNT 600

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
static MapRockSet s_rocks;
static MapSkyDome s_sky;
static MapRockPlacement s_rockPlacements[ROCK_COUNT];
static MapCloudSea s_cloudSea;
static MapMeadowPlacement s_grassPlacements[GRASS_TUFT_CAPACITY];
static MapFlowerPlacement s_flowerPlacements[FLOWER_COUNT];
static MapMeadowPlacement s_reedPlacements[REED_COUNT];
static MapMeadowSurface s_meadow;
static MapMeadowSurface s_reedMeadow;
static MapFlowerField s_flowerField;
static MapWaterSurface s_river;

static int s_grassCount = 0;
static int s_reedCount = 0;
static int s_flowerCount = 0;
static bool s_shadowWasEnabled = false;
static float s_time = 0.0f;
static bool s_ready = false;

// Ecology texture lookup helper
static Image s_splatImage;
static bool s_splatLoaded = false;

static Color SampleSplat(float x, float z)
{
    if (!s_splatLoaded) return (Color){255, 0, 0, 0};
    int tx = (int)((x / MAP_WIDTH) * (float)s_splatImage.width);
    int tz = (int)((z / MAP_DEPTH) * (float)s_splatImage.height);
    if (tx < 0) tx = 0; if (tx >= s_splatImage.width) tx = s_splatImage.width - 1;
    if (tz < 0) tz = 0; if (tz >= s_splatImage.height) tz = s_splatImage.height - 1;
    return GetImageColor(s_splatImage, tx, tz);
}

static float SongQuaoGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    Color splat = SampleSplat(x, z);
    // Grass probability from channel R, suppressed if rock or river
    float grass = (float)splat.r / 255.0f;
    float rock = (float)splat.g / 255.0f;
    float water = (float)splat.a / 255.0f;
    float density = grass * (1.0f - rock) * (1.0f - water * 0.85f);
    return fmaxf(0.0f, fminf(1.0f, density));
}

static void BuildMeadowLayout(void)
{
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapMeadowDistribution){
            .minBounds = {6.0f, 6.0f}, .maxBounds = {MAP_WIDTH - 6.0f, MAP_DEPTH - 6.0f},
            .spacing = 0.46f, .jitter = 0.85f,
            .minRadius = 0.26f, .maxRadius = 0.38f,
            .minHeight = 0.45f, .maxHeight = 0.70f,
            .yOffset = 0.025f, .seed = 0x51a7c3u,
        }, SongQuaoGrassDensity, NULL);

    TraceLog(LOG_INFO, "SONG_QUAO_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

    for (int i = 0; i < s_grassCount; i++) {
        MapMeadowPlacement *clump = &s_grassPlacements[i];
        float cx = clump->position.x;
        float cz = clump->position.z;
        float hash = sinf((float)(i * 47)) * 43758.5453f;
        clump->rotationDeg = (hash - floorf(hash)) * 360.0f;
        clump->height = 0.45f + 0.25f * sinf(cx * 0.05f + cz * 0.05f);
        clump->radius = 0.24f + 0.06f * cosf(cx * 0.08f - cz * 0.06f);
    }
}

static float Random01(unsigned int *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    return (float)*rng / 4294967296.0f;
}

static void BuildReedsAndFlowers(void)
{
    unsigned int rng = 0x9371a5u;
    s_reedCount = 0;
    s_flowerCount = 0;

    // Scan for riverbanks (reeds) and sunny knolls (flowers)
    for (int attempts = 0; attempts < 3000 && (s_reedCount < REED_COUNT || s_flowerCount < FLOWER_COUNT); attempts++) {
        float rx = 8.0f + Random01(&rng) * (MAP_WIDTH - 16.0f);
        float rz = 8.0f + Random01(&rng) * (MAP_DEPTH - 16.0f);
        Color splat = SampleSplat(rx, rz);
        Vector3 pos, normal;
        if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &pos, &normal)) continue;
        if (normal.y < 0.70f || pos.y < -1.0f) continue;

        // Reeds in wetland / stream banks (Channel A > 80)
        if (splat.a > 80 && s_reedCount < REED_COUNT) {
            float rad = 0.16f + 0.08f * Random01(&rng);
            float h = 0.75f + 0.45f * Random01(&rng);
            float rot = Random01(&rng) * 360.0f;
            float ph = Random01(&rng);
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = pos,
                .radius = rad,
                .height = h,
                .rotationDeg = rot,
                .phase = ph
            };
        }
        // Wildflowers in sunny knolls (Channel B > 60)
        if (splat.b > 60 && s_flowerCount < FLOWER_COUNT) {
            float h = 0.25f + 0.15f * Random01(&rng);
            float bloomR = 0.12f + 0.06f * Random01(&rng);
            float rot = Random01(&rng) * 360.0f;
            float ph = Random01(&rng);
            unsigned char tint = (unsigned char)(180 + (int)(60.0f * Random01(&rng)));
            s_flowerPlacements[s_flowerCount++] = (MapFlowerPlacement){
                .position = pos,
                .height = h,
                .bloomRadius = bloomR,
                .rotationDeg = rot,
                .phase = ph,
                .petalColor = (Color){235, tint, 60, 255},
                .petalCount = 5,
                .petalLengthScale = 1.0f
            };
        }
    }
}

static void BuildRockPlacements(void)
{
    unsigned int rng = 0x826412u;
    for (int i = 0; i < ROCK_COUNT; i++) {
        float rx = 10.0f + Random01(&rng) * (MAP_WIDTH - 20.0f);
        float rz = 10.0f + Random01(&rng) * (MAP_DEPTH - 20.0f);
        float rScale = 0.75f + 1.25f * Random01(&rng);
        float hScale = 0.60f + 0.80f * Random01(&rng);
        float rot = Random01(&rng) * 360.0f;
        float gy = GetGroundHeightSongQuaoMap(rx, rz);
        s_rockPlacements[i] = (MapRockPlacement){
            .position = {rx, gy, rz},
            .radiusScale = rScale,
            .heightScale = hScale,
            .rotationDeg = rot
        };
    }
}

static void DrawSongQuaoShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    Vector3 offset = {0};
    Vector2 wind = {0.86f, 0.51f};
    MapProp_DrawMeadowShadowCasters(&s_meadow, offset, s_time, wind, 0.035f);
    MapProp_DrawMeadowShadowCasters(&s_reedMeadow, offset, s_time, wind, 0.11f);
    MapProp_DrawFlowerFieldShadowCaster(&s_flowerField, offset, s_time, wind, 0.032f);
}

static void CaptureSongQuaoStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
        return;
    EnvShadow_BeginStaticCapture(kMapCenter, 64.0f);
    if (!EnvShadow_IsCapturing())
        return;
    Shader depthShader = EnvShadow_GetDepthShader();
    MapProp_DrawRockShadowCasters(&s_rocks, s_rockPlacements, ROCK_COUNT, depthShader);
    EnvShadow_EndStaticCapture();
}

void InitSongQuaoMap(void)
{
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){88, 102, 128, 255}); // Cool sky fill preserves shaded canopy volume
    Environment_SetSunColor((Color){255, 175, 95, 255});
    Environment_SetSunIntensity(2.8f);
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

    // Heightmap terrain using identical standard 3-layer materials and relief as Verdant Path
    s_ground = MapProp_CreateGroundHeightmap(
        "assets/maps/song_quao/song_quao_island_heightmap.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH, 3.6f, "assets/maps/song_quao/song_quao_ecology_splat.png",
        "assets/textures/verdant_meadow_substrate_diffuse.png", "assets/textures/dirt_diffuse.png");
    MapProp_SetGroundTint(&s_ground, (Color){62, 88, 45, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
        "assets/textures/verdant_meadow_substrate_material.png",
        "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");

    // Sample ground height inside lake basin floor to position the water surface naturally
    float lakeGroundY = MapProp_SampleGroundHeight(&s_ground, kMapCenter, SONG_QUAO_LAKE_CENTER_X, SONG_QUAO_LAKE_CENTER_Z);
    float lakeWaterY = lakeGroundY + 0.55f;

    // Configure lake basin habitat blending for ground shader
    Vector4 lakeParams = {SONG_QUAO_LAKE_CENTER_X, SONG_QUAO_LAKE_CENTER_Z,
                          SONG_QUAO_LAKE_RADIUS_X, SONG_QUAO_LAKE_RADIUS_Z};
    MapProp_SetGroundHabitat(&s_ground, NULL, 0, lakeParams);

    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
                                  "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png");
    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    MapBoundaryMistStyle rim = {1.2f, 4.0f, 0.5f, 2.0f, 0.35f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -1.25f, &rim))
        TraceLog(LOG_WARNING, "Song Quao: terrain boundary mist bake failed");

    BuildMeadowLayout();
    s_meadow = MapProp_CreateMeadow(s_grassPlacements, s_grassCount,
        (MapMeadowStyle){
            .rootColor = {18, 34, 16, 255}, .tipColor = {136, 186, 54, 255},
            .bladesPerClump = 6, .bladeSegments = 3, .bladeWidthScale = 0.17f,
            .chunkSize = 12.0f, .lodDistance = 32.0f, .midLodDistance = 16.0f, .drawDistance = 58.0f,
            .shadowDistance = 14.0f,
            .texturePath = NULL,
            .botanicalVariation = 1.0f,
            .growthForm = MAP_MEADOW_GROWTH_GRASS,
        });

    BuildReedsAndFlowers();
    if (s_reedCount > 0) {
        s_reedMeadow = MapProp_CreateMeadow(s_reedPlacements, s_reedCount,
            (MapMeadowStyle){
                .rootColor = {26, 42, 20, 255}, .tipColor = {136, 172, 82, 255},
                .bladesPerClump = 7, .bladeSegments = 3, .bladeWidthScale = 0.14f,
                .chunkSize = 18.0f, .lodDistance = 36.0f, .midLodDistance = 18.0f, .drawDistance = 65.0f,
                .shadowDistance = 12.0f,
                .texturePath = NULL,
                .hasPlumes = false,
                .growthForm = MAP_MEADOW_GROWTH_REED,
            });
    }

    if (s_flowerCount > 0) {
        s_flowerField = MapProp_CreateFlowerField(
            s_flowerPlacements, s_flowerCount,
            (Color){91, 120, 65, 255}, (Color){218, 185, 65, 255},
            NULL, 0.0f, 1, 1);
        MapProp_SetFlowerFieldDrawDistance(&s_flowerField, 75.0f);
    }

    BuildRockPlacements();

    // Song Quao Reservoir water surface in lake basin
    Vector3 lakePos = {SONG_QUAO_LAKE_CENTER_X, lakeWaterY, SONG_QUAO_LAKE_CENTER_Z};
    s_river = MapProp_CreateWaterSurface((MapWaterConfig){
        .shape = WATER_SHAPE_RADIAL,
        .ecosystem = WATER_ECO_ALPINE_STREAM,
        .center = lakePos,
        .radiusX = SONG_QUAO_LAKE_RADIUS_X, .radiusZ = SONG_QUAO_LAKE_RADIUS_Z,
        .bankWidth = 0.85f,
        .bankGroundY = lakeGroundY,
        .waveHeight = 0.038f, .waveScale = 1.15f, .waveSpeed = 0.65f,
        .detailScale = 0.085f, .detailStrength = 0.22f,
        .maxDepth = 1.35f,
        .absorption = {0.65f, 0.18f, 0.04f},
        .scatterColor = {0.15f, 0.65f, 0.70f},
        .scatterCoeff = 0.42f,
        .causticsStrength = 0.85f, .causticsScale = 1.35f,
        .foamThreshold = 0.15f,
        .segments = 128, .rings = 32, .seed = 7491u,
        .deepColor = {10, 42, 62, 255}, .shallowColor = {42, 138, 122, 235},
        .foamColor = {230, 245, 238, 220},
        .bankInnerColor = {62, 58, 44, 255}, .bankOuterColor = {55, 75, 42, 255},
    });

    for (int i = 0; i < ROCK_COUNT; i++) {
        float gy = GetGroundHeightSongQuaoMap(s_rockPlacements[i].position.x, s_rockPlacements[i].position.z);
        if (s_rockPlacements[i].position.y < lakeWaterY) {
            MapProp_AddWaterObstacle(&s_river, s_rockPlacements[i].position,
                                     s_rockPlacements[i].radiusScale * 0.92f, 0.75f);
        }
    }

    EnvShadow_SetFocus(kMapCenter, 20.0f);
    EnvShadow_SetMapCasterCallback(DrawSongQuaoShadowCasters, NULL);
    MapManager_SetZones(SONG_QUAO_ZONES, SONG_QUAO_ZONE_COUNT);

    s_time = 0.0f;
    s_ready = true;
}

void UpdateSongQuaoMap(float dt)
{
    if (!s_ready) return;
    s_time += dt;
    MapProp_PrepareMeadow(&s_meadow, (Vector3){0});
    if (s_reedCount > 0) MapProp_PrepareMeadow(&s_reedMeadow, (Vector3){0});
    MapProp_UpdateWaterSurface(&s_river, dt);

    Vector3 focus = {camera.target.x, 0.0f, camera.target.z};
    EnvShadow_SetFocus(focus, 20.0f);
    CaptureSongQuaoStaticShadows();

    MapProp_BeginNatureInteraction(camera.target, dt);
    MapProp_AddNatureInteractor(camera.target, 1.25f, 0.34f);
    MapProp_AddNatureWindVorticles(s_time);
    MapProp_EndNatureInteraction();
}

void DrawSongQuaoMap(void)
{
    if (!s_ready) return;
    PropLit_UpdateLighting();
    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawRocks(&s_rocks, s_rockPlacements, ROCK_COUNT, true);
    MapProp_DrawMeadow(&s_meadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.035f);
    MapProp_DrawSkyDome(&s_sky);
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    if (s_reedCount > 0)
        MapProp_DrawMeadow(&s_reedMeadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.11f);
    if (s_flowerCount > 0)
        MapProp_DrawFlowerField(&s_flowerField, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.032f);
}

void DrawTransparentSongQuaoMap(void)
{
    if (!s_ready) return;
    MapProp_DrawWaterOverlay(&s_river, s_time);
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
    if (s_flowerCount > 0) MapProp_UnloadFlowerField(&s_flowerField);
    if (s_reedCount > 0) MapProp_UnloadMeadow(&s_reedMeadow);
    MapProp_UnloadMeadow(&s_meadow);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadRocks(&s_rocks);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);
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
            return -100.0f; // Abyss void falloff outside floating island
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
    float surfaceY = s_river.config.center.y;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    if (gy < surfaceY) {
        if (outSurfaceY) *outSurfaceY = surfaceY;
        if (outWaterDepth) *outWaterDepth = surfaceY - gy;
        return true;
    }
    return false;
}

void SetWaterInteractorSongQuaoMap(Vector3 position, Vector3 velocity, float radius)
{
    MapProp_SetWaterInteractor(&s_river, position, velocity, radius, 0.0f);
}

void AddWaterRippleSongQuaoMap(Vector3 position, float radius, float intensity)
{
    MapProp_AddWaterRipple(&s_river, position, radius, intensity);
}

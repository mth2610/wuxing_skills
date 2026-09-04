#include "verdant_path.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "core/camera_context.h"
#include "core/map_manager.h"
#include "raylib.h"

#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
#define CLIFF_DEPTH 8.0f
#define CLOUD_SEA_Y -12.0f
#define PATH_UNIT_LENGTH 11.0f
#define PATH_WIDTH 3.6f
#define MOUNTAIN_RING_WIDTH 82.0f
#define MOUNTAIN_RING_DEPTH 58.0f
#define MOUNTAIN_ROCK_COUNT 40
#define ROCK_COUNT 10
#define GRASS_TUFT_CAPACITY 6400
#define FLOWER_CLUSTER_COUNT 3
#define FLOWERS_PER_CLUSTER 800
#define FLOWER_COUNT (FLOWER_CLUSTER_COUNT * FLOWERS_PER_CLUSTER)
#define REED_COUNT 180

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};
static const Vector3 kLakeCenter = {63.0f, 0.0f, 25.5f};
static const float kLakeRadiusX = 10.5f;
static const float kLakeRadiusZ = 7.4f;

static const MapZone ISLAND_ZONES[] = {
    {NAT_RIVER,  {63.0f, 0.0f, 25.5f}, 8.0f},
    {NAT_FOREST, {27.0f, 0.0f, 20.0f}, 8.5f},
    {NAT_FOREST, {76.0f, 0.0f, 52.0f}, 8.0f},
};
#define ISLAND_ZONE_COUNT (int)(sizeof(ISLAND_ZONES) / sizeof(ISLAND_ZONES[0]))

static const Vector3 kMainPath[] = {
    {10.0f, 0.0f, 39.0f}, {20.0f, 0.0f, 36.5f}, {30.0f, 0.0f, 38.5f},
    {40.0f, 0.0f, 35.2f}, {49.5f, 0.0f, 38.2f}, {59.0f, 0.0f, 41.5f},
    {69.0f, 0.0f, 43.5f}, {79.0f, 0.0f, 40.5f}, {90.0f, 0.0f, 37.0f},
};
#define MAIN_PATH_POINT_COUNT (int)(sizeof(kMainPath) / sizeof(kMainPath[0]))

static const Vector3 kLakePath[] = {
    {44.0f, 0.0f, 36.0f}, {48.0f, 0.0f, 31.0f}, {52.5f, 0.0f, 27.5f},
};
#define LAKE_PATH_POINT_COUNT (int)(sizeof(kLakePath) / sizeof(kLakePath[0]))

static const MapRockPlacement kRocks[ROCK_COUNT] = {
    {{14.0f, 0.0f, 13.0f}, 0.65f, 0.48f, 18.0f},
    {{22.0f, 0.0f, 58.0f}, 0.90f, 0.62f, 92.0f},
    {{39.0f, 0.0f, 61.0f}, 0.55f, 0.42f, 205.0f},
    {{84.0f, 0.0f, 57.0f}, 1.05f, 0.72f, 63.0f},
    {{82.0f, 0.0f, 17.0f}, 0.70f, 0.48f, 318.0f},
    {{52.0f, 0.0f, 18.0f}, 0.48f, 0.36f, 148.0f},
    {{74.0f, 0.0f, 23.0f}, 0.62f, 0.40f, 41.0f},
    {{57.0f, 0.0f, 33.5f}, 0.52f, 0.34f, 260.0f},
    {{67.0f, 0.0f, 34.0f}, 0.42f, 0.30f, 112.0f},
    {{76.0f, 0.0f, 29.0f}, 0.58f, 0.38f, 226.0f},
};

static MapGroundSurface s_ground;
static MapStripSurface s_path;
static MapRockSet s_rocks;
static MapRockSet s_mountainRockSet;
static MapCloudSea s_cloudSea;
static MapRockPlacement s_mountainRocks[MOUNTAIN_ROCK_COUNT];
static MapMeadowPlacement s_grassPlacements[GRASS_TUFT_CAPACITY];
static MapFlowerPlacement s_flowerPlacements[FLOWER_COUNT];
static MapMeadowPlacement s_reedPlacements[REED_COUNT];
static MapMeadowSurface s_meadow;
static MapMeadowSurface s_reedMeadow;
static MapFlowerField s_flowerFields[FLOWER_CLUSTER_COUNT];
static MapWaterSurface s_lake;
static int s_grassCount = 0;
static bool s_shadowWasEnabled = false;
static float s_time = 0.0f;
static bool s_ready = false;

static unsigned int NextRandom(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float Random01(unsigned int *state)
{
    return (float)(NextRandom(state) & 0x00ffffffu) / 16777215.0f;
}

static float RandomRange(unsigned int *state, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * Random01(state);
}

static bool IsInsideLake(float x, float z, float margin)
{
    float dx = (x - kLakeCenter.x) / (kLakeRadiusX + margin);
    float dz = (z - kLakeCenter.z) / (kLakeRadiusZ + margin);
    return dx * dx + dz * dz < 1.0f;
}

static float DistanceToSegmentXZ(float x, float z, Vector3 a, Vector3 b)
{
    float vx = b.x - a.x;
    float vz = b.z - a.z;
    float wx = x - a.x;
    float wz = z - a.z;
    float lengthSquared = vx * vx + vz * vz;
    float t = (lengthSquared > 0.0001f) ? (wx * vx + wz * vz) / lengthSquared : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float dx = x - (a.x + vx * t);
    float dz = z - (a.z + vz * t);
    return sqrtf(dx * dx + dz * dz);
}

static bool IsNearPath(float x, float z, float margin)
{
    for (int i = 0; i < MAIN_PATH_POINT_COUNT - 1; i++) {
        if (DistanceToSegmentXZ(x, z, kMainPath[i], kMainPath[i + 1]) < margin)
            return true;
    }
    for (int i = 0; i < LAKE_PATH_POINT_COUNT - 1; i++) {
        if (DistanceToSegmentXZ(x, z, kLakePath[i], kLakePath[i + 1]) < margin)
            return true;
    }
    return false;
}

static Color FlowerSpeciesColor(int variant, bool accent)
{
    static const Color primary[8] = {
        {242, 235, 211, 255}, {240, 205, 104, 255}, {232, 126, 99, 255},
        {132, 180, 225, 255}, {215, 178, 205, 255}, {232, 187, 204, 255},
        {177, 160, 218, 255}, {228, 219, 183, 255},
    };
    static const Color secondary[8] = {
        {255, 246, 216, 255}, {226, 179, 72, 255}, {215, 91, 76, 255},
        {105, 154, 211, 255}, {192, 145, 187, 255}, {218, 155, 182, 255},
        {151, 130, 202, 255}, {245, 228, 167, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

static float VerdantGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    if (IsInsideLake(x, z, 1.35f) || IsNearPath(x, z, 2.05f))
        return 0.0f;
    float nx = (x - kMapCenter.x) / 43.0f;
    float nz = (z - kMapCenter.z) / 29.5f;
    float edge = nx * nx + nz * nz;
    if (edge >= 1.0f)
        return 0.0f;
    float edgeFade = 1.0f - fmaxf(0.0f, (edge - 0.72f) / 0.28f);
    float macro = 0.76f + sinf(x * 0.19f + z * 0.11f) * 0.11f
                         + sinf(x * -0.07f + z * 0.23f) * 0.08f;
    return fmaxf(0.0f, fminf(1.0f, macro * edgeFade));
}

static void BuildMeadowLayout(void)
{
    unsigned int rng = 0x51a7c3u;
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapMeadowDistribution){
            .minBounds = {7.0f, 6.0f}, .maxBounds = {93.0f, 69.0f},
            .spacing = 0.78f, .jitter = 0.86f,
            .minRadius = 0.07f, .maxRadius = 0.135f,
            .minHeight = 0.12f, .maxHeight = 0.29f,
            .yOffset = 0.035f, .seed = 0x51a7c3u,
        }, VerdantGrassDensity, NULL);

    const Vector3 centers[FLOWER_CLUSTER_COUNT] = {
        {27.0f, 0.0f, 20.0f}, {29.0f, 0.0f, 54.0f}, {77.0f, 0.0f, 53.0f},
    };
    const Vector3 radii[FLOWER_CLUSTER_COUNT] = {
        {11.5f, 0.0f, 8.0f}, {13.0f, 0.0f, 7.0f}, {10.5f, 0.0f, 8.5f},
    };
    for (int i = 0; i < FLOWER_COUNT; i++) {
        // Keep each authored meadow contiguous so it can become an independent
        // GPU submission and be culled without touching the other clusters.
        int cluster = i / FLOWERS_PER_CLUSTER;
        float x = centers[cluster].x;
        float z = centers[cluster].z;
        for (int attempt = 0; attempt < 16; attempt++) {
            float angle = RandomRange(&rng, 0.0f, 2.0f * PI);
            float radius = powf(Random01(&rng), 1.30f);
            x = centers[cluster].x + cosf(angle) * radii[cluster].x * radius;
            z = centers[cluster].z + sinf(angle) * radii[cluster].z * radius;
            if (!IsInsideLake(x, z, 0.85f) && !IsNearPath(x, z, 1.92f))
                break;
        }
        s_flowerPlacements[i].position = (Vector3){x, 0.014f, z};
        s_flowerPlacements[i].rotationDeg = RandomRange(&rng, 0.0f, 360.0f);
        s_flowerPlacements[i].phase = Random01(&rng);
        static const unsigned char speciesByCluster[FLOWER_CLUSTER_COUNT][4] = {
            {0, 5, 7, 6}, // pale daisy/cosmos meadow
            {1, 0, 2, 7}, // warm buttercup/poppy meadow
            {3, 6, 4, 0}, // cool cornflower/aster meadow
        };
        float speciesRoll = Random01(&rng);
        int speciesSlot = speciesRoll < 0.56f ? 0
                        : speciesRoll < 0.80f ? 1
                        : speciesRoll < 0.94f ? 2 : 3;
        int variant = speciesByCluster[cluster][speciesSlot];
        bool tallAccent = variant == 2 || variant == 4 || variant == 6;
        s_flowerPlacements[i].height = tallAccent
            ? RandomRange(&rng, 0.20f, 0.35f)
            : RandomRange(&rng, 0.085f, 0.22f);
        s_flowerPlacements[i].bloomRadius = tallAccent
            ? RandomRange(&rng, 0.040f, 0.068f)
            : RandomRange(&rng, 0.028f, 0.057f);
        s_flowerPlacements[i].petalColor = FlowerSpeciesColor(
            variant, Random01(&rng) > 0.84f);
        s_flowerPlacements[i].petalCount = (unsigned char)(4 + (variant % 3));
        s_flowerPlacements[i].bloomVariant = (unsigned char)variant;
        s_flowerPlacements[i].petalLengthScale = RandomRange(&rng, 0.86f, 1.10f);
    }

    for (int i = 0; i < REED_COUNT; i++) {
        float angle;
        float habitat;
        do {
            angle = RandomRange(&rng, 0.0f, 2.0f * PI);
            habitat = 0.5f + 0.5f * sinf(angle * 3.0f + 0.8f);
            habitat *= habitat;
        } while (Random01(&rng) > 0.18f + habitat * 0.72f);
        float rim = RandomRange(&rng, 0.985f, 1.085f);
        s_reedPlacements[i].position = MapProp_GetWaterEdgePoint(&s_lake, angle, rim);
        s_reedPlacements[i].position.y -= 0.03f;
        s_reedPlacements[i].radius = RandomRange(&rng, 0.08f, 0.14f);
        s_reedPlacements[i].height = RandomRange(&rng, 0.38f, 0.82f);
        s_reedPlacements[i].rotationDeg = angle * 180.0f / PI;
        s_reedPlacements[i].phase = Random01(&rng);
    }
}

static void DrawPathChain(const Vector3 *points, int count, float widthScale)
{
    for (int i = 0; i < count - 1; i++) {
        float dx = points[i + 1].x - points[i].x;
        float dz = points[i + 1].z - points[i].z;
        float length = sqrtf(dx * dx + dz * dz) + 0.75f;
        Vector3 midpoint = {
            (points[i].x + points[i + 1].x) * 0.5f,
            0.0f,
            (points[i].z + points[i + 1].z) * 0.5f,
        };
        float rotation = atan2f(dz, dx) * 180.0f / PI;
        MapProp_DrawStripEx(&s_path, midpoint, 0.035f, -rotation,
                            (Vector3){length / PATH_UNIT_LENGTH, 1.0f, widthScale});
    }
}

void InitVerdantPathMap(void)
{
    if (s_ready)
        return;

    Environment_SetTimeOfDaySpeed(0.0f);
    // Warm sunset key plus cool sky fill: enough energy for flower colour,
    // with a lower light angle that makes terrain and vegetation shadows read.
    Environment_SetAmbientColor((Color){88, 94, 118, 255});
    Environment_SetSunColor((Color){255, 194, 132, 255});
    Environment_SetSunDirection((Vector3){0.58f, -0.50f, -0.64f});
    Environment_SetShadowColor((Color){30, 37, 58, 148});
    Environment_SetFogConfig((EnvFogConfig){
        .color = {96, 86, 102, 255}, .start = 68.0f, .end = 152.0f,
        .density = 0.78f, .enabled = true,
    });

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif

    s_ground = MapProp_CreateGroundHeightmap(
        "assets/heightmaps/verdant_path_island.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH, 8.0f, "assets/textures/grass_ground_diffuse.png",
        "assets/textures/grass_ground_diffuse.png", "assets/textures/dirt_diffuse.png");
    MapProp_SetGroundTint(&s_ground, (Color){168, 181, 157, 255});
    s_path = MapProp_CreateStrip(PATH_UNIT_LENGTH, PATH_WIDTH, 1.8f,
        "assets/textures/stone_path_diffuse.png",
        "assets/textures/stone_path_normal.png",
        "assets/textures/stone_path_roughness.png");
    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png");
    // Border rocks must participate in the same lighting response as nearby
    // rocks; an unlit fallback turns the skyline into a flat white cut-out.
    s_mountainRockSet = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png");
    MapProp_GenerateMountainRing(s_mountainRocks, MOUNTAIN_ROCK_COUNT,
        MOUNTAIN_RING_WIDTH, MOUNTAIN_RING_DEPTH, 2.6f, 5.4f, 0.9f, 2.2f, 1337u);
    {
        float offsetX = (MAP_WIDTH - MOUNTAIN_RING_WIDTH) * 0.5f;
        float offsetZ = (MAP_DEPTH - MOUNTAIN_RING_DEPTH) * 0.5f;
        for (int i = 0; i < MOUNTAIN_ROCK_COUNT; i++) {
            s_mountainRocks[i].position.x += offsetX;
            s_mountainRocks[i].position.z += offsetZ;
        }
    }
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    s_lake = MapProp_CreateWaterSurface((MapWaterConfig){
        .center = {63.0f, 0.075f, 25.5f},
        .radiusX = kLakeRadiusX, .radiusZ = kLakeRadiusZ, .bankWidth = 0.56f,
        .waveHeight = 0.042f, .waveScale = 0.96f, .waveSpeed = 0.72f,
        .bankGroundY = 0.008f, .detailScale = 0.075f, .detailStrength = 0.17f,
        .segments = 112, .rings = 14, .seed = 9173u,
        .deepColor = {10, 31, 39, 255}, .shallowColor = {45, 79, 74, 255},
        .foamColor = {112, 132, 116, 255},
        .bankInnerColor = {52, 58, 43, 255}, .bankOuterColor = {65, 84, 51, 255},
    });
    BuildMeadowLayout();
    s_meadow = MapProp_CreateMeadow(s_grassPlacements, s_grassCount,
        (MapMeadowStyle){
            .rootColor = {70, 91, 50, 255}, .tipColor = {122, 143, 82, 255},
            .bladesPerClump = 5, .bladeSegments = 2, .bladeWidthScale = 0.25f,
            .chunkSize = 12.0f, .lodDistance = 30.0f, .drawDistance = 78.0f,
            .texturePath = NULL,
        });
    s_reedMeadow = MapProp_CreateMeadow(s_reedPlacements, REED_COUNT,
        (MapMeadowStyle){
            .rootColor = {55, 73, 34, 255}, .tipColor = {128, 139, 66, 255},
            .bladesPerClump = 5, .bladeSegments = 3, .bladeWidthScale = 0.23f,
            .chunkSize = 18.0f, .lodDistance = 34.0f, .drawDistance = 72.0f,
            .texturePath = NULL,
        });
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        s_flowerFields[cluster] = MapProp_CreateFlowerField(
            &s_flowerPlacements[cluster * FLOWERS_PER_CLUSTER], FLOWERS_PER_CLUSTER,
            (Color){91, 120, 65, 255}, (Color){199, 157, 69, 255},
            "maps/toolkit/textures/wildflower_bloom_atlas_v2.png", 0.36f, 4, 2);
        MapProp_SetFlowerFieldDrawDistance(&s_flowerFields[cluster], 78.0f);
    }
    // World-fixed layer: terrain relief and static rocks are captured once.
    // Characters remain in the smaller camera-following map updated each frame.
    EnvShadow_BeginStaticCapture(kMapCenter, 64.0f);
    if (EnvShadow_IsCapturing()) {
        Shader depthShader = EnvShadow_GetDepthShader();
        MapProp_DrawGroundShadowCaster(&s_ground, kMapCenter, depthShader);
        MapProp_DrawRockShadowCasters(&s_mountainRockSet, s_mountainRocks,
                                      MOUNTAIN_ROCK_COUNT, depthShader);
        MapProp_DrawRockShadowCasters(&s_rocks, kRocks, ROCK_COUNT, depthShader);
        EnvShadow_EndStaticCapture();
    }
    MapManager_SetZones(ISLAND_ZONES, ISLAND_ZONE_COUNT);
    s_time = 0.0f;
    s_ready = true;
}

void UpdateVerdantPathMap(float dt)
{
    if (s_ready) {
        s_time += dt;
        Vector3 view = {
            camera.target.x - camera.position.x,
            0.0f,
            camera.target.z - camera.position.z,
        };
        float viewLength = sqrtf(view.x * view.x + view.z * view.z);
        if (viewLength > 0.001f) {
            view.x /= viewLength;
            view.z /= viewLength;
        }
        Vector3 focus = {
            camera.position.x + view.x * 10.0f,
            0.0f,
            camera.position.z + view.z * 10.0f,
        };
        EnvShadow_SetFocus(focus, 30.0f);
        MapProp_BeginNatureInteraction(camera.target, dt);
        MapProp_AddNatureInteractor(camera.target, 1.25f, 0.34f);
        MapProp_EndNatureInteraction();
    }
}

float GetGroundHeightVerdantPathMap(float x, float z)
{
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

bool SampleGroundSurfaceVerdantPathMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    return MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, outPosition, outNormal);
}

void DrawVerdantPathMap(void)
{
    if (!s_ready)
        return;

    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    MapProp_DrawGround(&s_ground, kMapCenter);
    DrawPathChain(kMainPath, MAIN_PATH_POINT_COUNT, 1.0f);
    DrawPathChain(kLakePath, LAKE_PATH_POINT_COUNT, 0.72f);
    MapProp_DrawWaterSurface(&s_lake, s_time);
    MapProp_DrawRocks(&s_mountainRockSet, s_mountainRocks, MOUNTAIN_ROCK_COUNT, false);
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT, true);
    MapProp_DrawMeadow(&s_meadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.035f);
    MapProp_DrawMeadow(&s_reedMeadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.11f);
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        MapProp_DrawFlowerField(&s_flowerFields[cluster], (Vector3){0}, s_time,
                                (Vector2){0.86f, 0.51f}, 0.032f);
    }
}

void UnloadVerdantPathMap(void)
{
    if (!s_ready)
        return;
    EnvShadow_InvalidateStaticCache();
    MapProp_UnloadWaterSurface(&s_lake);
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++)
        MapProp_UnloadFlowerField(&s_flowerFields[cluster]);
    MapProp_UnloadMeadow(&s_reedMeadow);
    MapProp_UnloadMeadow(&s_meadow);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadRocks(&s_mountainRockSet);
    MapProp_UnloadRocks(&s_rocks);
    MapProp_UnloadStrip(&s_path);
    MapProp_UnloadGround(&s_ground);
    MapProp_ClearNatureInteraction();
#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_ready = false;
}

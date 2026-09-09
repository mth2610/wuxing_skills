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
#define GRASS_TUFT_CAPACITY 65000
#define FLOWER_CLUSTER_COUNT 3
#define FLOWERS_PER_CLUSTER 120
#define FLOWER_COUNT (FLOWER_CLUSTER_COUNT * FLOWERS_PER_CLUSTER)
#define REED_COUNT 480

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

static float DistanceToPaths(float x, float z)
{
    float minDist = 9999.0f;
    for (int i = 0; i < MAIN_PATH_POINT_COUNT - 1; i++) {
        float d = DistanceToSegmentXZ(x, z, kMainPath[i], kMainPath[i + 1]);
        if (d < minDist) minDist = d;
    }
    for (int i = 0; i < LAKE_PATH_POINT_COUNT - 1; i++) {
        float d = DistanceToSegmentXZ(x, z, kLakePath[i], kLakePath[i + 1]);
        if (d < minDist) minDist = d;
    }
    return minDist;
}

static bool IsNearPath(float x, float z, float margin)
{
    return DistanceToPaths(x, z) < margin;
}

static Color FlowerSpeciesColor(int variant, bool accent)
{
    static const Color primary[8] = {
        {252, 250, 242, 255}, // 0: ivory white daisy
        {255, 212, 48, 255},  // 1: warm golden buttercup
        {232, 54, 42, 255},   // 2: vermilion crimson poppy
        {88, 148, 242, 255},  // 3: heavenly sapphire cornflower
        {248, 142, 175, 255}, // 4: wild peach-rose pink
        {208, 88, 156, 255},  // 5: orchid magenta cosmos
        {158, 128, 232, 255}, // 6: gentle lavender violet
        {255, 240, 168, 255}, // 7: pale primrose cream
    };
    static const Color secondary[8] = {
        {255, 246, 228, 255}, {255, 185, 28, 255},  {198, 32, 36, 255},
        {64, 118, 224, 255},  {242, 112, 148, 255}, {178, 52, 126, 255},
        {134, 98, 216, 255},  {255, 220, 130, 255},
    };
    variant &= 7;
    return accent ? secondary[variant] : primary[variant];
}

static float VerdantGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    if (IsInsideLake(x, z, 1.25f))
        return 0.0f;
    float distPath = DistanceToPaths(x, z);
    if (distPath < 1.15f)
        return 0.0f;
    float pathFade = fminf(1.0f, (distPath - 1.15f) / 1.10f); // smooth organic border along stone path

    float nx = (x - kMapCenter.x) / 43.0f;
    float nz = (z - kMapCenter.z) / 29.5f;
    float edge = nx * nx + nz * nz;
    if (edge >= 1.0f)
        return 0.0f;
    float edgeFade = 1.0f - fmaxf(0.0f, (edge - 0.72f) / 0.28f);

    // Multi-harmonic organic patch noise (Ghost of Tsushima natural lawn layering)
    float carpetNoise = sinf(x * 0.14f + z * 0.09f) * 0.45f
                      + sinf(x * -0.08f + z * 0.21f + 1.3f) * 0.35f
                      + sinf(x * 0.32f - z * 0.28f + 2.7f) * 0.20f;
    float macro = 0.68f + carpetNoise * 0.48f;

    // Suppress grass in flower clusters: flowers need clear open ground to bloom cleanly
    const Vector3 flowerCenters[FLOWER_CLUSTER_COUNT] = {
        {27.0f, 0.0f, 20.0f}, {29.0f, 0.0f, 54.0f}, {77.0f, 0.0f, 53.0f},
    };
    const Vector3 flowerRadii[FLOWER_CLUSTER_COUNT] = {
        {11.5f, 0.0f, 8.0f}, {13.0f, 0.0f, 7.0f}, {10.5f, 0.0f, 8.5f},
    };
    float flowerSuppression = 1.0f;
    for (int c = 0; c < FLOWER_CLUSTER_COUNT; c++) {
        float dx = (x - flowerCenters[c].x) / flowerRadii[c].x;
        float dz = (z - flowerCenters[c].z) / flowerRadii[c].z;
        float d2 = dx * dx + dz * dz;
        if (d2 < 1.25f) {
            // Core flower zone (d2 < 0.65) has 0 grass; gently fades in toward border
            float localFactor = fmaxf(0.0f, (d2 - 0.65f) / 0.60f);
            if (localFactor < flowerSuppression) {
                flowerSuppression = localFactor;
            }
        }
    }
    macro *= flowerSuppression;

    return fmaxf(0.0f, fminf(1.0f, macro * edgeFade * pathFade));
}

static void BuildMeadowLayout(void)
{
    unsigned int rng = 0x51a7c3u;
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapMeadowDistribution){
            .minBounds = {7.0f, 6.0f}, .maxBounds = {93.0f, 69.0f},
            .spacing = 0.18f, .jitter = 0.65f,
            .minRadius = 0.22f, .maxRadius = 0.28f,
            .minHeight = 0.22f, .maxHeight = 0.38f,
            .yOffset = 0.025f, .seed = 0x51a7c3u,
        }, VerdantGrassDensity, NULL);

    // Ghost of Tsushima / AAA Reference: Structured procedural variation with macro flow field
    for (int i = 0; i < s_grassCount; i++) {
        MapMeadowPlacement *clump = &s_grassPlacements[i];
        float cx = clump->position.x;
        float cz = clump->position.z;

        // Multi-frequency wind noise & directional flow:
        // 1. Broad landscape waves (macro scale ~16m)
        float wave1 = sinf(cx * 0.07f + cz * 0.05f) * 0.60f
                    + sinf(cx * -0.05f + cz * 0.11f + 1.2f) * 0.40f;
        // 2. Medium organic turbulence / Voronoi eddies (~4.5m)
        float eddy = sinf(cx * 0.22f - cz * 0.18f + 0.8f) * 0.55f
                   + sinf(cx * 0.14f + cz * 0.26f + 2.3f) * 0.45f;
        // 3. Micro gust ripple (~1.8m)
        float micro = sinf(cx * 0.55f + cz * 0.45f + 3.1f) * 0.5f
                    + sinf(cx * -0.42f + cz * 0.62f) * 0.5f;

        float flowAngle = 45.0f + wave1 * 32.0f + eddy * 24.0f + micro * 16.0f;

        // 4. Clump-level Yaw Jitter (AAA standard: organic diversity)
        float hash = sinf((float)(i * 47)) * 43758.5453f;
        hash -= floorf(hash);
        clump->rotationDeg = flowAngle + (hash - 0.5f) * 54.0f;

        // Cellular noise field for macro-biomes (scale ~7 meters)
        float cell1 = sinf(cx * 0.15f + cz * 0.10f) * 0.5f + 0.5f;
        float cell2 = sinf(cx * -0.11f + cz * 0.20f + 1.8f) * 0.5f + 0.5f;
        float biome = cell1 * 0.6f + cell2 * 0.4f;

        if (biome > 0.60f) {
            // Biome 1: Tall Deep Meadow (long sweeping weeping ribbons, height < 0.40m)
            float t = (biome - 0.60f) / 0.40f;
            clump->height = 0.30f + t * 0.08f;
            clump->radius = 0.25f + t * 0.03f;
        } else if (biome < 0.35f) {
            // Biome 2: Meadow clearing (dense arching grass, height ~0.20 - 0.26m)
            float t = biome / 0.35f;
            clump->height = 0.20f + t * 0.06f;
            clump->radius = 0.20f + t * 0.03f;
        } else {
            // Biome 3: Wild flowing grass (height ~0.24 - 0.32m)
            float t = (biome - 0.35f) / 0.25f;
            clump->height = 0.24f + t * 0.08f;
            clump->radius = 0.22f + t * 0.04f;
        }
    }

    const Vector3 centers[FLOWER_CLUSTER_COUNT] = {
        {27.0f, 0.0f, 20.0f}, {29.0f, 0.0f, 54.0f}, {77.0f, 0.0f, 53.0f},
    };
    const Vector3 radii[FLOWER_CLUSTER_COUNT] = {
        {11.5f, 0.0f, 8.0f}, {13.0f, 0.0f, 7.0f}, {10.5f, 0.0f, 8.5f},
    };
    static const Vector2 patchOffsets[4] = {
        {-0.42f, -0.18f}, {0.18f, -0.31f}, {0.38f, 0.20f}, {-0.12f, 0.36f},
    };
    for (int i = 0; i < FLOWER_COUNT; i++) {
        int cluster = i / FLOWERS_PER_CLUSTER;
        float x = centers[cluster].x;
        float z = centers[cluster].z;
        for (int attempt = 0; attempt < 24; attempt++) {
            float patchRoll = Random01(&rng);
            int patch = patchRoll < 0.32f ? 0 : patchRoll < 0.59f ? 1
                      : patchRoll < 0.82f ? 2 : 3;
            float angle = RandomRange(&rng, 0.0f, 2.0f * PI);
            float radius = sqrtf(Random01(&rng));
            float patchRadius = 0.31f + 0.08f * (float)((patch + cluster) & 1);
            x = centers[cluster].x + patchOffsets[patch].x * radii[cluster].x
              + cosf(angle) * radii[cluster].x * patchRadius * radius;
            z = centers[cluster].z + patchOffsets[patch].y * radii[cluster].z
              + sinf(angle) * radii[cluster].z * patchRadius * radius;
            if (IsInsideLake(x, z, 0.85f) || DistanceToPaths(x, z) < 1.40f)
                continue;

            // Blue noise / Poisson minimum distance enforcement: avoid intersecting flowers
            bool tooClose = false;
            for (int prev = cluster * FLOWERS_PER_CLUSTER; prev < i; prev++) {
                float pdx = x - s_flowerPlacements[prev].position.x;
                float pdz = z - s_flowerPlacements[prev].position.z;
                if (pdx * pdx + pdz * pdz < 0.065f * 0.065f) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose || attempt >= 23)
                break;
        }
        s_flowerPlacements[i].position = (Vector3){x, 0.014f, z};
        s_flowerPlacements[i].rotationDeg = RandomRange(&rng, 0.0f, 360.0f);
        s_flowerPlacements[i].phase = Random01(&rng);

        // 2D Spatial Cellular Bio-Drift (Ghost of Tsushima Voronoi field)
        // Flowers cluster by spatial cell so species form cohesive sweeping waves/drifts
        float cellVal = sinf(x * 0.22f + z * 0.15f) * 0.5f +
                        sinf(x * -0.14f + z * 0.26f + 1.2f) * 0.5f;
        cellVal += (Random01(&rng) - 0.5f) * 0.28f; // organic boundary jitter

        static const unsigned char speciesByCluster[FLOWER_CLUSTER_COUNT][4] = {
            {0, 4, 5, 6}, // Ivory Daisy drift -> Peach Blossom -> Orchid Cosmos -> Lavender
            {1, 2, 7, 0}, // Golden Buttercup drift -> Scarlet Poppy -> Primrose -> Daisy
            {3, 6, 4, 1}, // Sapphire Cornflower drift -> Lavender -> Wild Rose -> Buttercup
        };
        int speciesSlot = (cellVal < -0.25f) ? 0
                        : (cellVal < 0.28f)  ? 1
                        : (cellVal < 0.72f)  ? 2 : 3;
        int variant = speciesByCluster[cluster][speciesSlot];
        bool tallAccent = variant == 2 || variant == 4 || variant == 6;

        float driftHeightBase = tallAccent ? 0.32f : 0.20f;
        s_flowerPlacements[i].height = driftHeightBase + RandomRange(&rng, -0.05f, 0.07f);
        s_flowerPlacements[i].bloomRadius = (tallAccent ? 0.110f : 0.082f) * RandomRange(&rng, 0.90f, 1.15f);
        s_flowerPlacements[i].petalColor = FlowerSpeciesColor(
            variant, Random01(&rng) > 0.85f);
        s_flowerPlacements[i].petalCount = (unsigned char)(4 + (variant % 3));
        s_flowerPlacements[i].bloomVariant = (unsigned char)variant;
        s_flowerPlacements[i].petalLengthScale = RandomRange(&rng, 0.90f, 1.12f);
    }

    // Wetland Reeds organized into 3 natural thicket bays
    int validReeds = 0;
    for (int attempt = 0; attempt < REED_COUNT * 6 && validReeds < REED_COUNT; attempt++) {
        float angle = RandomRange(&rng, 0.0f, 2.0f * PI);
        // 3 major wetland thicket bays: Northwest, South-southeast, Northeast
        float bay1 = expf(-powf((angle - 3.1f) / 0.65f, 2.0f));
        float bay2 = expf(-powf((angle - 5.2f) / 0.60f, 2.0f));
        float bay3 = expf(-powf((angle - 1.0f) / 0.55f, 2.0f));
        float bayDensity = bay1 + bay2 + bay3;

        if (Random01(&rng) > 0.12f + bayDensity * 0.86f)
            continue;

        float rim = RandomRange(&rng, 0.94f, 1.20f);
        Vector3 pos = MapProp_GetWaterEdgePoint(&s_lake, angle, rim);
        if (DistanceToPaths(pos.x, pos.z) < 1.70f)
            continue;

        s_reedPlacements[validReeds].position = pos;
        s_reedPlacements[validReeds].position.y -= 0.035f;
        float coreBonus = bayDensity * (rim < 1.05f ? 0.35f : 0.15f);
        s_reedPlacements[validReeds].radius = RandomRange(&rng, 0.12f, 0.20f);
        s_reedPlacements[validReeds].height = RandomRange(&rng, 1.05f, 1.70f) + coreBonus;
        s_reedPlacements[validReeds].rotationDeg = angle * 180.0f / PI + RandomRange(&rng, -25.0f, 25.0f);
        s_reedPlacements[validReeds].phase = Random01(&rng);
        validReeds++;
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

static void DrawVerdantShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    Vector3 offset = {0};
    Vector2 wind = {0.86f, 0.51f};
    // High-efficiency shadow caster: s_meadow casts real, dynamic, wind-swaying
    // blade silhouettes into the shadow map within shadowDistance.
    MapProp_DrawMeadowShadowCasters(&s_meadow, offset, s_time, wind, 0.035f);
    MapProp_DrawMeadowShadowCasters(&s_reedMeadow, offset, s_time, wind, 0.11f);
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        MapProp_DrawFlowerFieldShadowCaster(&s_flowerFields[cluster], offset,
                                             s_time, wind, 0.032f);
    }
}

static void CaptureVerdantStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
        return;
    EnvShadow_BeginStaticCapture(kMapCenter, 64.0f);
    if (!EnvShadow_IsCapturing())
        return;
    Shader depthShader = EnvShadow_GetDepthShader();
    // Do not capture the heightmap against itself. At this low sunset angle,
    // receiver/caster depth quantization turns its shallow slopes into long
    // parallel acne bands across the whole meadow. Terrain still receives
    // static rock shadows, dynamic vegetation shadows, and its own normal-based
    // lighting; only unstable terrain self-shadowing is omitted.
    MapProp_DrawRockShadowCasters(&s_mountainRockSet, s_mountainRocks,
                                  MOUNTAIN_ROCK_COUNT, depthShader);
    MapProp_DrawRockShadowCasters(&s_rocks, kRocks, ROCK_COUNT, depthShader);
    EnvShadow_EndStaticCapture();
}

static void ApplyVerdantEnvironment(void)
{
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){124, 146, 172, 255});
    Environment_SetSunColor((Color){255, 242, 210, 255});
    Environment_SetSunDirection((Vector3){0.45f, -0.74f, -0.50f});
    Environment_SetShadowColor((Color){28, 36, 48, 120});
    Environment_SetFogConfig((EnvFogConfig){
        .color = {148, 168, 186, 255}, .start = 55.0f, .end = 155.0f,
        .density = 0.0035f, .enabled = true,
    });
}

static void ApplyHabitatToGround(void)
{
    Vector4 segs[16];
    int segCount = 0;
    for (int i = 0; i < MAIN_PATH_POINT_COUNT - 1 && segCount < 16; i++) {
        segs[segCount++] = (Vector4){kMainPath[i].x, kMainPath[i].z, kMainPath[i + 1].x, kMainPath[i + 1].z};
    }
    for (int i = 0; i < LAKE_PATH_POINT_COUNT - 1 && segCount < 16; i++) {
        segs[segCount++] = (Vector4){kLakePath[i].x, kLakePath[i].z, kLakePath[i + 1].x, kLakePath[i + 1].z};
    }
    Vector4 lakeParams = {kLakeCenter.x, kLakeCenter.z, kLakeRadiusX, kLakeRadiusZ};
    MapProp_SetGroundHabitat(&s_ground, segs, segCount, lakeParams);
}

void InitVerdantPathMap(void)
{
    // Map activation also calls Init for already-loaded worlds. Restore all
    // global environment state before the resource guard so another map cannot
    // leave Verdant using stale light/fog values.
    ApplyVerdantEnvironment();
    if (s_ready) {
        ApplyHabitatToGround();
        EnvShadow_SetMapCasterCallback(DrawVerdantShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        MapManager_SetZones(ISLAND_ZONES, ISLAND_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif

    s_ground = MapProp_CreateGroundHeightmap(
        "assets/heightmaps/verdant_path_island.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH, 3.6f, "assets/textures/grass_ground_diffuse.png",
        "assets/textures/grass_ground_diffuse.png", "assets/textures/dirt_diffuse.png");
    ApplyHabitatToGround();
    // Shader consumes normalized linear values; calibrated natural botanical meadow tint
    MapProp_SetGroundTint(&s_ground, (Color){62, 88, 45, 255});
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
            .rootColor = {26, 44, 16, 255}, .tipColor = {138, 206, 50, 255},
            .bladesPerClump = 4, .bladeSegments = 4, .bladeWidthScale = 0.075f,
            .chunkSize = 12.0f, .lodDistance = 18.0f, .drawDistance = 42.0f,
            .shadowDistance = 10.0f,
            .texturePath = NULL,
        });
    s_reedMeadow = MapProp_CreateMeadow(s_reedPlacements, REED_COUNT,
        (MapMeadowStyle){
            .rootColor = {24, 38, 16, 255}, .tipColor = {160, 194, 68, 255},
            .bladesPerClump = 7, .bladeSegments = 4, .bladeWidthScale = 0.14f,
            .chunkSize = 18.0f, .lodDistance = 36.0f, .drawDistance = 76.0f,
            .shadowDistance = 24.0f,
            .texturePath = NULL,
            .hasPlumes = false,
        });
    static const Color clusterCenters[FLOWER_CLUSTER_COUNT] = {
        {218, 185, 65, 255},  // Cluster 0: pale daisy golden center
        {112, 70, 38, 255},   // Cluster 1: poppy/buttercup warm deep amber
        {78, 70, 125, 255},   // Cluster 2: cornflower violet-indigo core
    };
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        s_flowerFields[cluster] = MapProp_CreateFlowerField(
            &s_flowerPlacements[cluster * FLOWERS_PER_CLUSTER], FLOWERS_PER_CLUSTER,
            (Color){91, 120, 65, 255}, clusterCenters[cluster],
            NULL, 0.0f, 1, 1);
        MapProp_SetFlowerFieldDrawDistance(&s_flowerFields[cluster], 78.0f);
        MapProp_SetFlowerFieldLod(&s_flowerFields[cluster], 34.0f, 30.0f);
    }
    EnvShadow_SetMapCasterCallback(DrawVerdantShadowCasters, NULL);
    // World-fixed terrain/rocks are captured now when enabled, or lazily after
    // a runtime toggle. Dynamic vegetation/characters use the near cascade.
    CaptureVerdantStaticShadows();
    MapManager_SetZones(ISLAND_ZONES, ISLAND_ZONE_COUNT);
    s_time = 0.0f;
    s_ready = true;
}

void UpdateVerdantPathMap(float dt)
{
    if (s_ready) {
        s_time += dt;
        Vector3 focus = {camera.target.x, 0.0f, camera.target.z};
        // Dynamic vegetation/character shadows are the near cascade. Static
        // terrain and rocks remain covered by the world-fixed cache. Centering
        // the 40 m box on the actual viewed/gameplay target keeps orbit and
        // top-down camera zoom from pushing visible vegetation into the edge
        // fade; Environment performs the light-space texel snapping.
        EnvShadow_SetFocus(focus, 20.0f);
        CaptureVerdantStaticShadows();
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
    EnvShadow_SetMapCasterCallback(NULL, NULL);
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

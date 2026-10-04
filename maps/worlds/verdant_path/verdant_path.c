#include "verdant_path.h"

#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/map_ecology.h"
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

#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
#define CLIFF_DEPTH 8.0f
#define CLOUD_SEA_Y -12.0f
#define ROCK_COUNT 13
#define HORIZON_CRAG_COUNT 16
#define GRASS_TUFT_CAPACITY 65000
#define FLOWER_CLUSTER_COUNT 6
#define FLOWERS_PER_CLUSTER 200
#define FLOWER_COUNT (FLOWER_CLUSTER_COUNT * FLOWERS_PER_CLUSTER)
#define REED_COUNT 480
// Scale both horizontal axes by sqrt(2/3) to retain two-thirds of mist area.
#define VERDANT_MIST_RADIUS_SCALE 0.81649658f

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};
static const Vector3 kLakeCenter = {63.0f, 0.0f, 25.5f};
static const float kLakeRadiusX = 10.5f;
static const float kLakeRadiusZ = 7.4f;

static const Vector3 kFlowerCenters[FLOWER_CLUSTER_COUNT] = {
    {27.0f, 0.0f, 20.0f}, // Cluster 0: Northwest meadow clearing
    {28.0f, 0.0f, 52.0f}, // Cluster 1: South meadow & main overview view
    {64.0f, 0.0f, 36.5f}, // Cluster 2: South lake bank
    {48.0f, 0.0f, 29.0f}, // Cluster 3: Fork to lake path
    {75.0f, 0.0f, 50.0f}, // Cluster 4: Southeast sunny knoll
    {54.0f, 0.0f, 17.5f}, // Cluster 5: North lake bank wildflowers
};

static const Vector3 kFlowerRadii[FLOWER_CLUSTER_COUNT] = {
    {14.0f, 0.0f, 10.0f},
    {18.0f, 0.0f, 15.0f},
    {8.5f,  0.0f, 6.0f},
    {10.5f, 0.0f, 7.5f},
    {15.0f, 0.0f, 11.5f},
    {12.0f, 0.0f, 8.5f},
};

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
    // Three low stones breach the surface; their contact rings are shaded in water_surface.fs.
    {{59.0f, 0.0f, 23.0f}, 0.66f, 0.52f, 29.0f},
    {{65.7f, 0.0f, 29.0f}, 0.48f, 0.38f, 114.0f},
    {{69.5f, 0.0f, 22.0f}, 0.73f, 0.57f, 241.0f},
};

static MapGroundSurface s_ground;
static MapEcology s_ecology;
static MapRockSet s_rocks;
static MapRockSet s_horizonCrags;
static MapRockPlacement s_horizonPlacements[HORIZON_CRAG_COUNT];
static MapSkyDome s_sky;
static MapRockPlacement s_rockPlacements[ROCK_COUNT];
static MapCloudSea s_cloudSea;
static Texture2D s_farSunbeamTexture = {0};
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

static void CreateFarSunbeamTexture(void)
{
    Image image = GenImageColor(64, 128, BLANK);
    for (int y = 0; y < image.height; y++) {
        float v = ((float)y + 0.5f) / (float)image.height;
        float endFade = sinf(PI * v);
        endFade *= endFade;
        for (int x = 0; x < image.width; x++) {
            float u = (((float)x + 0.5f) / (float)image.width) * 2.0f - 1.0f;
            float widthFade = expf(-5.5f * u * u);
            unsigned char alpha = (unsigned char)(82.0f * widthFade * endFade);
            ImageDrawPixel(&image, x, y, (Color){255, 255, 255, alpha});
        }
    }
    s_farSunbeamTexture = LoadTextureFromImage(image);
    SetTextureFilter(s_farSunbeamTexture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(image);
}

static void DrawFarSunbeams(void)
{
    if (s_farSunbeamTexture.id == 0) return;
    const Vector3 positions[] = {
        {55.5f, 4.4f, 17.5f},
        {61.5f, 4.0f, 18.5f},
        {69.0f, 4.7f, 18.0f},
    };
    const float sizes[] = {8.8f, 7.4f, 9.4f};
    Vector3 beamUp = Vector3Negate(Vector3Normalize(Environment_GetSunDirection()));
    Color sunlight = Environment_GetSunColor();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 3; i++) {
        float distance = Vector3Distance(camera.target, positions[i]);
        float farFade = Clamp((distance - 8.0f) / 9.0f, 0.0f, 1.0f);
        if (farFade <= 0.001f) continue;
        Color tint = sunlight;
        tint.a = (unsigned char)(255.0f * farFade);
        Vector2 size = {sizes[i] * 0.22f, sizes[i]};
        DrawBillboardPro(camera, s_farSunbeamTexture,
                         (Rectangle){0, 0, 64, 128}, positions[i], beamUp,
                         size, (Vector2){size.x * 0.5f, size.y * 0.5f}, 0.0f, tint);
    }
    EndBlendMode();
    rlEnableDepthMask();
}

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

static float VerdantGrassDensitySource(float x, float z, void *userData)
{
    (void)userData;
    if (IsInsideLake(x, z, 0.48f))
        return 0.0f;
    float distPath = DistanceToPaths(x, z);
    if (distPath < 0.95f)
        return 0.0f;
    float pathFade = fminf(1.0f, (distPath - 0.95f) / 0.65f); // grass returns gradually at the dirt path edge

    float nx = (x - kMapCenter.x) / 43.0f;
    float nz = (z - kMapCenter.z) / 29.5f;
    float edge = nx * nx + nz * nz;
    if (edge >= 1.0f)
        return 0.0f;
    float edgeFade = 1.0f - fmaxf(0.0f, (edge - 0.72f) / 0.28f);

    // Broad drifting meadows alternate dense tussocks with shorter open turf.
    float warpedX = x + sinf(z * 0.17f) * 2.1f;
    float macroField = sinf(warpedX * 0.15f + z * 0.08f) * 0.55f
                     + sinf(z * 0.20f - x * 0.06f + 1.4f) * 0.30f
                     + sinf(x * 0.33f + z * 0.29f) * 0.15f;
    float patch = fminf(1.0f, fmaxf(0.0f, (macroField + 0.25f) / 0.70f));
    patch = patch * patch * (3.0f - 2.0f * patch);
    float macro = 0.58f + 0.42f * patch;

    // Flowers grow through a shorter, sparser meadow underlayer. A zero-density
    // clearing exposes a large dark disc at gameplay distance and makes the
    // flower clusters look planted on bare soil instead of part of the meadow.
    float flowerSuppression = 1.0f;
    for (int c = 0; c < FLOWER_CLUSTER_COUNT; c++) {
        float dx = (x - kFlowerCenters[c].x) / kFlowerRadii[c].x;
        float dz = (z - kFlowerCenters[c].z) / kFlowerRadii[c].z;
        float d2 = dx * dx + dz * dz;
        if (d2 < 1.25f) {
            // Keep enough cover to connect the flowers to the surrounding turf.
            // The broad fade still gives blooms room to read individually.
            float localFactor = fmaxf(0.72f, (d2 - 0.55f) / 0.70f);
            if (localFactor < flowerSuppression) {
                flowerSuppression = localFactor;
            }
        }
    }
    macro *= flowerSuppression;

    return fmaxf(0.0f, fminf(1.0f, macro * edgeFade * pathFade));
}

static float VerdantGrassDensity(float x, float z, void *userData)
{
    (void)userData;
    return s_ecology.ready ? MapEcology_Sample(&s_ecology, x, z).coverage
                           : VerdantGrassDensitySource(x, z, NULL);
}

static bool VerdantEcologyEligible(float x, float z, void *userData)
{
    (void)userData;
    Vector3 position, normal;
    return MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &position, &normal)
        && normal.y > 0.65f && position.y > -1.0f;
}

static void BuildMeadowLayout(void)
{
    unsigned int rng = 0x51a7c3u;
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapMeadowDistribution){
            .minBounds = {7.0f, 6.0f}, .maxBounds = {93.0f, 69.0f},
            .spacing = 0.22f, .jitter = 0.90f,
            .minRadius = 0.22f, .maxRadius = 0.28f,
            .minHeight = 0.22f, .maxHeight = 0.38f,
            .yOffset = 0.025f, .seed = 0x51a7c3u,
        }, VerdantGrassDensity, NULL);
    TraceLog(LOG_INFO, "VERDANT_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

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
        clump->rotationDeg = flowAngle + (hash - 0.5f) * 240.0f;
        float localHeight = clump->height; // keep the placement jitter after biome shaping
        float localRadius = clump->radius;

        // Cellular noise field for macro-biomes (scale ~7 meters)
        float cell1 = sinf(cx * 0.15f + cz * 0.10f) * 0.5f + 0.5f;
        float cell2 = sinf(cx * -0.11f + cz * 0.20f + 1.8f) * 0.5f + 0.5f;
        float biome = s_ecology.ready ? MapEcology_Sample(&s_ecology, cx, cz).habitat
                                      : cell1 * 0.6f + cell2 * 0.4f;

        if (biome > 0.60f) {
            // Biome 1: Tall Deep Meadow (long sweeping weeping ribbons, height ~0.36 - 0.42m)
            float t = (biome - 0.60f) / 0.40f;
            clump->height = 0.36f + t * 0.06f;
            clump->radius = 0.26f + t * 0.04f;
        } else if (biome < 0.35f) {
            // Biome 2: Meadow clearing (dense arching grass, height ~0.30 - 0.34m)
            float t = biome / 0.35f;
            clump->height = 0.30f + t * 0.04f;
            clump->radius = 0.23f + t * 0.03f;
        } else {
            // Biome 3: Wild flowing grass (height ~0.33 - 0.37m)
            float t = (biome - 0.35f) / 0.25f;
            clump->height = 0.33f + t * 0.04f;
            clump->radius = 0.24f + t * 0.03f;
        }
        if (s_ecology.ready)
            clump->height = 0.30f + MapEcology_Sample(&s_ecology, cx, cz).growth * 0.12f;
        clump->height *= 0.82f + 0.34f * (localHeight - 0.22f) / 0.16f;
        clump->radius *= 0.91f + 0.18f * (localRadius - 0.22f) / 0.06f;

        // 5. Pathway Edge Trampled Turf (AAA organic transition)
        float distPath = DistanceToPaths(cx, cz);
        float pathEdgeT = (distPath - 0.95f) / 1.15f;
        pathEdgeT = fmaxf(0.0f, fminf(1.0f, pathEdgeT));
        float pathTaper = 0.45f + 0.55f * (pathEdgeT * pathEdgeT * (3.0f - 2.0f * pathEdgeT));
        clump->height *= pathTaper;
        clump->radius *= (1.15f - 0.15f * pathTaper);
    }

    static const Vector2 patchOffsets[4] = {
        {-0.42f, -0.18f}, {0.18f, -0.31f}, {0.38f, 0.20f}, {-0.12f, 0.36f},
    };
    for (int i = 0; i < FLOWER_COUNT; i++) {
        int cluster = i / FLOWERS_PER_CLUSTER;
        float x = kFlowerCenters[cluster].x;
        float z = kFlowerCenters[cluster].z;
        for (int attempt = 0; attempt < 24; attempt++) {
            float patchRoll = Random01(&rng);
            float angle = RandomRange(&rng, 0.0f, 2.0f * PI);
            float radius = sqrtf(Random01(&rng));
            if (patchRoll < 0.78f) {
                // Dense sweeping drifts/patches (poppies, daisies, buttercups in natural floral beds)
                int patch = patchRoll < 0.28f ? 0 : patchRoll < 0.52f ? 1
                          : patchRoll < 0.70f ? 2 : 3;
                float patchRadius = 0.35f + 0.08f * (float)((patch + cluster) & 1);
                x = kFlowerCenters[cluster].x + patchOffsets[patch].x * kFlowerRadii[cluster].x
                  + cosf(angle) * kFlowerRadii[cluster].x * patchRadius * radius;
                z = kFlowerCenters[cluster].z + patchOffsets[patch].y * kFlowerRadii[cluster].z
                  + sinf(angle) * kFlowerRadii[cluster].z * patchRadius * radius;
            } else {
                // Natural stray wild blossoms scattered through the surrounding meadow
                x = kFlowerCenters[cluster].x + cosf(angle) * kFlowerRadii[cluster].x * (0.30f + 0.70f * radius);
                z = kFlowerCenters[cluster].z + sinf(angle) * kFlowerRadii[cluster].z * (0.30f + 0.70f * radius);
            }
            if (IsInsideLake(x, z, 0.85f) || DistanceToPaths(x, z) < 1.30f)
                continue;

            // Blue noise / Poisson minimum distance enforcement: avoid intersecting flowers
            bool tooClose = false;
            for (int prev = cluster * FLOWERS_PER_CLUSTER; prev < i; prev++) {
                float pdx = x - s_flowerPlacements[prev].position.x;
                float pdz = z - s_flowerPlacements[prev].position.z;
                if (pdx * pdx + pdz * pdz < 0.055f * 0.055f) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose || attempt >= 23)
                break;
        }
        float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
        s_flowerPlacements[i].position = (Vector3){x, gy + 0.014f, z};
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
            {0, 7, 4, 1}, // South lake bank: ivory, primrose, peach and warm buttercup
            {3, 6, 4, 1}, // Sapphire Cornflower drift -> Lavender -> Wild Rose -> Buttercup
            {1, 0, 7, 4}, // Southeast knoll: buttercup, daisy, primrose, peach
            {0, 3, 6, 5}, // North bank: daisy, cornflower, lavender, cosmos
        };
        int speciesSlot = (cellVal < -0.25f) ? 0
                        : (cellVal < 0.28f)  ? 1
                        : (cellVal < 0.72f)  ? 2 : 3;
        int variant = speciesByCluster[cluster][speciesSlot];
        bool tallAccent = variant == 2 || variant == 4 || variant == 6;

        float driftHeightBase = tallAccent ? 0.41f : 0.34f;
        s_flowerPlacements[i].height = driftHeightBase + RandomRange(&rng, -0.03f, 0.08f);
        s_flowerPlacements[i].bloomRadius = (tallAccent ? 0.125f : 0.096f) * RandomRange(&rng, 0.95f, 1.20f);
        s_flowerPlacements[i].petalColor = FlowerSpeciesColor(
            variant, Random01(&rng) > 0.85f);
        s_flowerPlacements[i].petalCount = (unsigned char)(4 + (variant % 3));
        s_flowerPlacements[i].bloomVariant = (unsigned char)variant;
        s_flowerPlacements[i].petalLengthScale = RandomRange(&rng, 0.92f, 1.15f);
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

        float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, pos.x, pos.z);
        s_reedPlacements[validReeds].position = pos;
        s_reedPlacements[validReeds].position.y = gy - 0.02f;
        float coreBonus = bayDensity * (rim < 1.05f ? 0.35f : 0.15f);
        s_reedPlacements[validReeds].radius = RandomRange(&rng, 0.12f, 0.20f);
        // Short peripheral growth softens the thicket edge; tall reeds stay in bays.
        float thicketCore = Clamp(bayDensity, 0.0f, 1.0f);
        float reedHeight = RandomRange(&rng, 0.45f, 0.90f) + thicketCore * 0.70f;
        s_reedPlacements[validReeds].height = reedHeight + coreBonus;
        s_reedPlacements[validReeds].rotationDeg = angle * 180.0f / PI + RandomRange(&rng, -25.0f, 25.0f);
        s_reedPlacements[validReeds].phase = Random01(&rng);
        validReeds++;
    }
}

static void DrawVerdantShadowCasters(Shader depthShader, void *userData)
{
    (void)depthShader;
    (void)userData;
    Vector3 offset = {0};
    Vector2 wind = {0.86f, 0.51f};
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
    MapProp_DrawRockShadowCasters(&s_rocks, s_rockPlacements, ROCK_COUNT, depthShader);
    EnvShadow_EndStaticCapture();
}

static void ApplyVerdantEnvironment(void)
{
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){142, 157, 170, 255}); // Neutral morning sky fill
    Environment_SetSunColor((Color){246, 232, 207, 255});     // Warm sunlight without bleaching stone
    // Sun rises in the East-North ahead (X > 0, Z < 0), sunlight travels towards West-South (X < 0, Z > 0)
    Environment_SetSunDirection(Vector3Normalize((Vector3){-0.50f, -0.45f, 0.55f}));
    Environment_SetShadowColor((Color){28, 36, 48, 120});

    // Keep the playable foreground clear; haze and sun shafts belong beyond it.
    AtmosphereProfile atmos = {
        .color = {205, 228, 250, 255},
        .start = 22.0f,
        .end = 95.0f,
        .enabled = true,
        .optics = {
            .rayleighLMS = {0.0076224f, 0.012935f, 0.024845f},
            .mieScattering = 0.0024f,
            .mieAnisotropy = 0.66f,
            .multipleScatteringAmp = 1.7f
        },
        .density = {
            .baseDensity = 0.025f,
            .heightFalloff = 0.20f,      // Distant haze reaches above grass at gameplay zoom
            .baseAltitude = 0.0f,
            .enableSigmoidLayer = false, // Disabled map-wide blanket; mist is strictly localized
            .layerAltitude = 0.35f,
            .layerThickness = 0.8f,
            .layerDensity = 0.0f
        }
    };
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(0.70f);
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
    if (!s_ecology.ready) {
        MapEcologyConfig ecologyConfig = {
            .rect = {0.0f, 0.0f, MAP_WIDTH, MAP_DEPTH}, .lake = lakeParams,
            .paths = segs, .pathCount = segCount, .roadHalfWidth = 0.95f,
        };
        if (!MapEcology_Bake(&s_ecology, &ecologyConfig, VerdantGrassDensitySource,
                             VerdantEcologyEligible, NULL))
            TraceLog(LOG_WARNING, "VERDANT: ecology bake failed; using analytic habitat");
    }
    MapProp_SetGroundEcology(&s_ground, s_ecology.ready ? &s_ecology : NULL);
}

static void SpawnVerdantMistVolumes(void)
{
    FogVolume_ClearAll();

    // 1. Đám sương mỏng lác đác trên vạt cỏ sát lối đi chính (sát đất ngọn cỏ Y=0.0 - 0.85m)
    LocalFogVolume meadowPathMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {36.0f, 0.40f, 31.0f},
        .extents = {5.0f * VERDANT_MIST_RADIUS_SCALE, 0.45f, 4.2f * VERDANT_MIST_RADIUS_SCALE},
        .color = {240, 248, 255, 255},
        .density = 0.09f,
        .edgeSoftness = 0.85f,
        .emissive = 0.0f,
        .driftVelocity = {0.03f, 0.0f, 0.015f},
        .lifetime = 0.0f, // permanent
        .maxLifetime = 0.0f,
        .active = true
    };
    FogVolume_Create(&meadowPathMist);

    // 2. Đám sương nhỏ mỏng manh sát mép nước bãi sậy bờ hồ (sát mặt nước Y=0.0 - 0.75m)
    LocalFogVolume lakeEdgeMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {53.0f, 0.35f, 27.5f},
        .extents = {5.5f * VERDANT_MIST_RADIUS_SCALE, 0.40f, 4.5f * VERDANT_MIST_RADIUS_SCALE},
        .color = {235, 246, 255, 255},
        .density = 0.08f,
        .edgeSoftness = 0.85f,
        .emissive = 0.0f,
        .driftVelocity = {0.04f, 0.0f, 0.02f},
        .lifetime = 0.0f,
        .maxLifetime = 0.0f,
        .active = true
    };
    FogVolume_Create(&lakeEdgeMist);

    // 3. Small mist pocket in the western flower hollow.
    LocalFogVolume westHollowMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {24.0f, 0.38f, 20.5f},
        .extents = {5.0f * VERDANT_MIST_RADIUS_SCALE, 0.42f, 4.2f * VERDANT_MIST_RADIUS_SCALE},
        .color = {235, 246, 255, 255},
        .density = 0.08f,
        .edgeSoftness = 0.85f,
        .emissive = 0.0f,
        .driftVelocity = {0.03f, 0.0f, 0.015f},
        .lifetime = 0.0f,
        .maxLifetime = 0.0f,
        .active = true
    };
    FogVolume_Create(&westHollowMist);

    // 4. Small mist pocket at the eastern meadow foot.
    LocalFogVolume eastMeadowMist = {
        .shape = FOG_SHAPE_CYLINDER,
        .position = {75.0f, 0.38f, 49.0f},
        .extents = {5.2f * VERDANT_MIST_RADIUS_SCALE, 0.42f, 4.4f * VERDANT_MIST_RADIUS_SCALE},
        .color = {238, 246, 255, 255},
        .density = 0.08f,
        .edgeSoftness = 0.85f,
        .emissive = 0.0f,
        .driftVelocity = {0.04f, 0.0f, 0.02f},
        .lifetime = 0.0f,
        .maxLifetime = 0.0f,
        .active = true
    };
    FogVolume_Create(&eastMeadowMist);
}

void InitVerdantPathMap(void)
{
    // Map activation also calls Init for already-loaded worlds. Restore all
    // global environment state before the resource guard so another map cannot
    // leave Verdant using stale light/fog values.
    ApplyVerdantEnvironment();
    VolumetricFog_SetDistantCoverage(4.0f / 9.0f);
    EnvCloudShadowConfig cloudConfig = {
        .enabled = true, .strength = 0.12f, .worldSize = 96.0f,
        .planeHeight = 80.0f, .coverage = 0.48f, .softness = 0.16f, .windSpeedScale = 0.55f,
    };
    Environment_SetCloudShadowConfig(&cloudConfig);
    SpawnVerdantMistVolumes();
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
        "assets/textures/verdant_meadow_substrate_diffuse.png", "assets/textures/dirt_diffuse.png");
    ApplyHabitatToGround();
    // Shader consumes normalized linear values; calibrated natural botanical meadow tint
    MapProp_SetGroundTint(&s_ground, (Color){62, 88, 45, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
        "assets/textures/verdant_meadow_substrate_material.png",
        "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png");
    s_sky = MapProp_CreateSkyDome();
    s_horizonCrags = MapProp_CreateMountainCrags("assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png");
    unsigned int horizonSeed = 0x4c39a1u;
    for (int i = 0; i < HORIZON_CRAG_COUNT; i++) {
        float angle = ((float)i + RandomRange(&horizonSeed, -0.28f, 0.28f)) *
                      (2.0f * PI / HORIZON_CRAG_COUNT);
        float radius = RandomRange(&horizonSeed, 0.96f, 1.12f);
        s_horizonPlacements[i] = (MapRockPlacement){
            .position = {kMapCenter.x + cosf(angle) * 72.0f * radius, -6.0f,
                         kMapCenter.z + sinf(angle) * 55.0f * radius},
            .radiusScale = RandomRange(&horizonSeed, 11.0f, 19.0f),
            .heightScale = RandomRange(&horizonSeed, 5.0f, 12.0f),
            .rotationDeg = RandomRange(&horizonSeed, 0.0f, 360.0f),
        };
    }
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    CreateFarSunbeamTexture();
    s_lake = MapProp_CreateWaterSurface((MapWaterConfig){
        .shape = WATER_SHAPE_RADIAL,
        .ecosystem = WATER_ECO_ALPINE_STREAM,
        .center = {63.0f, 0.075f, 25.5f},
        .radiusX = kLakeRadiusX, .radiusZ = kLakeRadiusZ, .bankWidth = 0.60f,
        .waveHeight = 0.038f, .waveScale = 1.15f, .waveSpeed = 0.65f,
        .bankGroundY = 0.008f, .detailScale = 0.085f, .detailStrength = 0.22f,
        .maxDepth = 1.15f,
        .absorption = {0.65f, 0.18f, 0.04f},
        .scatterColor = {0.15f, 0.65f, 0.70f},
        .scatterCoeff = 0.42f,
        .causticsStrength = 0.85f,
        .causticsScale = 1.35f,
        .foamThreshold = 0.15f,
        .segments = 128, .rings = 32, .seed = 9173u,
        .deepColor = {10, 42, 62, 255}, .shallowColor = {42, 138, 122, 235},
        .foamColor = {230, 245, 238, 220},
        .bankInnerColor = {62, 58, 44, 255}, .bankOuterColor = {55, 75, 42, 255},
    });
    for (int i = 0; i < ROCK_COUNT; i++) {
        s_rockPlacements[i] = kRocks[i];
        float gy = GetGroundHeightVerdantPathMap(kRocks[i].position.x, kRocks[i].position.z);
        if (i >= ROCK_COUNT - 3) {
            float waterSurfaceY = s_lake.config.center.y;
            float depth = fmaxf(0.0f, waterSurfaceY - gy);
            s_rockPlacements[i].position.y = gy + 0.10f;
            s_rockPlacements[i].heightScale = fmaxf(kRocks[i].heightScale, depth + 0.22f);
        } else {
            s_rockPlacements[i].position.y = gy;
        }
    }
    for (int i = ROCK_COUNT - 3; i < ROCK_COUNT; i++) {
        MapProp_AddWaterObstacle(&s_lake, s_rockPlacements[i].position,
                                 s_rockPlacements[i].radiusScale * 0.92f, 0.75f);
    }
    BuildMeadowLayout();
    s_meadow = MapProp_CreateMeadow(s_grassPlacements, s_grassCount,
        (MapMeadowStyle){
            .rootColor = {28, 48, 22, 255}, .tipColor = {114, 165, 64, 255},
            .bladesPerClump = 6, .bladeSegments = 4, .bladeWidthScale = 0.19f,
            .chunkSize = 12.0f, .lodDistance = 28.0f, .midLodDistance = 0.0f, .drawDistance = 50.0f,
            .shadowDistance = 14.0f,
            .texturePath = NULL,
            .botanicalVariation = 1.0f,
        });
    s_reedMeadow = MapProp_CreateMeadow(s_reedPlacements, REED_COUNT,
        (MapMeadowStyle){
            .rootColor = {26, 42, 20, 255}, .tipColor = {136, 172, 82, 255},
            .bladesPerClump = 7, .bladeSegments = 4, .bladeWidthScale = 0.14f,
            .chunkSize = 18.0f, .lodDistance = 36.0f, .drawDistance = 65.0f,
            .shadowDistance = 12.0f,
            .texturePath = NULL,
            .hasPlumes = false,
        });
    static const Color clusterCenters[FLOWER_CLUSTER_COUNT] = {
        {218, 185, 65, 255},  // Cluster 0: pale daisy golden center
        {112, 70, 38, 255},   // Cluster 1: poppy/buttercup warm deep amber
        {191, 159, 82, 255},  // Cluster 2: warm lake-bank flowers
        {78, 70, 125, 255},   // Cluster 3: cornflower violet-indigo core
        {120, 85, 45, 255},   // Cluster 4: sunny knoll golden amber
        {210, 175, 75, 255},  // Cluster 5: north bank wildflowers
    };
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        s_flowerFields[cluster] = MapProp_CreateFlowerField(
            &s_flowerPlacements[cluster * FLOWERS_PER_CLUSTER], FLOWERS_PER_CLUSTER,
            (Color){91, 120, 65, 255}, clusterCenters[cluster],
            NULL, 0.0f, 1, 1);
        MapProp_SetFlowerFieldDrawDistance(&s_flowerFields[cluster], 78.0f);
        MapProp_SetFlowerFieldLod(&s_flowerFields[cluster], 34.0f, 30.0f);
    }
    EnvShadow_SetFocus(kMapCenter, 20.0f);
    EnvShadow_SetMapCasterCallback(DrawVerdantShadowCasters, NULL);
    // World-fixed terrain/rocks are captured now when enabled, or lazily after
    // a runtime toggle. Dynamic vegetation/characters use the near cascade.
    CaptureVerdantStaticShadows();
    MapManager_SetZones(ISLAND_ZONES, ISLAND_ZONE_COUNT);
    MapManager_RegisterWaterHooks("VERDANT_PATH", GetWaterInfoVerdantPathMap,
                                  SetWaterInteractorVerdantPathMap, AddWaterRippleVerdantPathMap);
    s_time = 0.0f;
    s_ready = true;
}

void UpdateVerdantPathMap(float dt)
{
    if (s_ready) {
        static int profileEnabled = -1;
        static double profileMs[6] = {0};
        static int profileSamples = 0;
        if (profileEnabled < 0) {
            const char *profile = getenv("WUXING_MAP_UPDATE_PROFILE");
            profileEnabled = profile && profile[0] == '1';
        }
        double mark = profileEnabled ? GetTime() : 0.0;
        s_time += dt;
        // Stage grass visibility before any framebuffer capture or scene draw.
        // Late uploads split the scene pass and copy its full depth attachment.
        MapProp_PrepareMeadow(&s_meadow, (Vector3){0});
        MapProp_PrepareMeadow(&s_reedMeadow, (Vector3){0});
        MapProp_UpdateWaterSurface(&s_lake, dt);
        if (profileEnabled) {
            double now = GetTime(); profileMs[0] += (now - mark) * 1000.0; mark = now;
        }
        Vector3 focus = {camera.target.x, 0.0f, camera.target.z};
        // Dynamic vegetation/character shadows are the near cascade. Static
        // terrain and rocks remain covered by the world-fixed cache. Centering
        // the 40 m box on the actual viewed/gameplay target keeps orbit and
        // top-down camera zoom from pushing visible vegetation into the edge
        // fade; Environment performs the light-space texel snapping.
        EnvShadow_SetFocus(focus, 20.0f);
        CaptureVerdantStaticShadows();
        if (profileEnabled) {
            double now = GetTime(); profileMs[1] += (now - mark) * 1000.0; mark = now;
        }
        MapProp_BeginNatureInteraction(camera.target, dt);
        if (profileEnabled) {
            double now = GetTime(); profileMs[2] += (now - mark) * 1000.0; mark = now;
        }
        MapProp_AddNatureInteractor(camera.target, 1.25f, 0.34f);
        if (profileEnabled) {
            double now = GetTime(); profileMs[3] += (now - mark) * 1000.0; mark = now;
        }
        MapProp_AddNatureWindVorticles(s_time);
        if (profileEnabled) {
            double now = GetTime(); profileMs[4] += (now - mark) * 1000.0; mark = now;
        }
        MapProp_EndNatureInteraction();
        if (profileEnabled) {
            profileMs[5] += (GetTime() - mark) * 1000.0;
            if (++profileSamples == 60) {
                TraceLog(LOG_INFO, "MAP_UPDATE_PROFILE: samples=60 water_ms=%.3f static_ms=%.3f begin_ms=%.3f interactor_ms=%.3f wind_ms=%.3f interaction_upload_ms=%.3f",
                    profileMs[0] / 60.0, profileMs[1] / 60.0, profileMs[2] / 60.0,
                    profileMs[3] / 60.0, profileMs[4] / 60.0, profileMs[5] / 60.0);
                for (int i = 0; i < 6; i++) profileMs[i] = 0.0;
                profileSamples = 0;
            }
        }
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

bool GetWaterInfoVerdantPathMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    float surfaceY = s_lake.config.center.y;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
    if (gy < surfaceY) {
        if (outSurfaceY) *outSurfaceY = surfaceY;
        if (outWaterDepth) *outWaterDepth = surfaceY - gy;
        return true;
    }
    return false;
}

void SetWaterInteractorVerdantPathMap(Vector3 position, Vector3 velocity, float radius)
{
    float surfaceY = s_lake.config.center.y;
    float gy = MapProp_SampleGroundHeight(&s_ground, kMapCenter, position.x, position.z);
    if (gy < surfaceY) {
        float depth = surfaceY - gy;
        float submerged = 0.0f;
        if (position.y < surfaceY + 0.15f) {
            submerged = fminf(1.0f, fmaxf(0.0f, (surfaceY - position.y + 0.25f) / fmaxf(depth, 0.35f)));
        }
        MapProp_SetWaterInteractor(&s_lake, position, velocity, radius, submerged);
    } else {
        MapProp_SetWaterInteractor(&s_lake, position, velocity, radius, 0.0f);
    }
}

void AddWaterRippleVerdantPathMap(Vector3 position, float radius, float intensity)
{
    MapProp_AddWaterRipple(&s_lake, position, radius, intensity);
}

void DrawVerdantPathMap(void)
{
    if (!s_ready)
        return;

    extern void rlvkPassMark(const char *label) __attribute__((weak));
    #define V_MARK(lbl) do { if (rlvkPassMark) rlvkPassMark(lbl); } while(0)
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();
    MapProp_DrawGround(&s_ground, kMapCenter);
    V_MARK("op_terrain");
    MapProp_DrawRocks(&s_rocks, s_rockPlacements, ROCK_COUNT, true);
    MapProp_DrawRocks(&s_horizonCrags, s_horizonPlacements, HORIZON_CRAG_COUNT, false);
    V_MARK("op_rocks");
    MapProp_DrawMeadow(&s_meadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.035f);
    V_MARK("op_grass_early");
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    V_MARK("op_cloudsea");
    V_MARK("op_grass");
    MapProp_DrawMeadow(&s_reedMeadow, (Vector3){0}, s_time, (Vector2){0.86f, 0.51f}, 0.11f);
    V_MARK("op_reeds");
    // Lake bed is seamlessly integrated into the sunken ground mesh (op_terrain)
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++) {
        MapProp_DrawFlowerField(&s_flowerFields[cluster], (Vector3){0}, s_time,
                                (Vector2){0.86f, 0.51f}, 0.032f);
    }
    MapProp_DrawSkyDome(&s_sky);
    V_MARK("op_flowers");
}

void DrawTransparentVerdantPathMap(void)
{
    if (!s_ready)
        return;
    if (!getenv("WUXING_NO_WATER")) MapProp_DrawWaterOverlay(&s_lake, s_time);
    DrawFarSunbeams();
}

void UnloadVerdantPathMap(void)
{
    if (!s_ready)
        return;
    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    FogVolume_ClearAll();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);
    if (s_farSunbeamTexture.id != 0) {
        UnloadTexture(s_farSunbeamTexture);
        s_farSunbeamTexture = (Texture2D){0};
    }
    MapProp_UnloadWaterSurface(&s_lake);
    for (int cluster = 0; cluster < FLOWER_CLUSTER_COUNT; cluster++)
        MapProp_UnloadFlowerField(&s_flowerFields[cluster]);
    MapProp_UnloadMeadow(&s_reedMeadow);
    MapProp_UnloadMeadow(&s_meadow);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadRocks(&s_rocks);
    MapProp_UnloadRocks(&s_horizonCrags);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_SetGroundEcology(&s_ground, NULL);
    MapEcology_Unload(&s_ecology);
    MapProp_UnloadGround(&s_ground);
    MapProp_ClearNatureInteraction();
#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_ready = false;
}

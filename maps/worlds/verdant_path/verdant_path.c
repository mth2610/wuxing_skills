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
#define CLOUD_SEA_Y -3.5f
#define ROCK_COUNT 13
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

static const MapFloraFlowerCluster VERDANT_FLOWER_CLUSTERS[FLOWER_CLUSTER_COUNT] = {
    {{27.0f, 0.0f, 20.0f}, {14.0f, 10.0f}, FLOWERS_PER_CLUSTER, 0, {0, 4, 5, 6}, {0}, {0}, {218, 185, 65, 255}},
    {{28.0f, 0.0f, 52.0f}, {18.0f, 15.0f}, FLOWERS_PER_CLUSTER, 1, {1, 2, 7, 0}, {0}, {0}, {112, 70, 38, 255}},
    {{64.0f, 0.0f, 36.5f}, {8.5f,  6.0f},  FLOWERS_PER_CLUSTER, 0, {0, 7, 4, 1}, {0}, {0}, {191, 159, 82, 255}},
    {{48.0f, 0.0f, 29.0f}, {10.5f, 7.5f},  FLOWERS_PER_CLUSTER, 3, {3, 6, 4, 1}, {0}, {0}, {78, 70, 125, 255}},
    {{75.0f, 0.0f, 50.0f}, {15.0f, 11.5f}, FLOWERS_PER_CLUSTER, 1, {1, 0, 7, 4}, {0}, {0}, {120, 85, 45, 255}},
    {{54.0f, 0.0f, 17.5f}, {12.0f, 8.5f},  FLOWERS_PER_CLUSTER, 0, {0, 3, 6, 5}, {0}, {0}, {210, 175, 75, 255}},
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
static MapRockSystem s_rocks;
static MapSkyDome s_sky;
static MapRockPlacement s_rockPlacements[ROCK_COUNT];
static MapCloudSea s_cloudSea;
static Texture2D s_farSunbeamTexture = {0};
static MapMeadowPlacement s_grassPlacements[GRASS_TUFT_CAPACITY];
static MapFlowerPlacement s_flowerPlacements[FLOWER_COUNT];
static MapMeadowPlacement s_reedPlacements[REED_COUNT];
static MapFloraSystem s_flora;
static MapWaterSurface s_lake;
static int s_grassCount = 0;
static int s_reedCount = 0;
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

static float VerdantGrassDensitySource(float x, float z, void *userData)
{
    (void)userData;
    if (IsInsideLake(x, z, 0.48f))
        return 0.0f;
    float distPath = DistanceToPaths(x, z);
    float pathMeander = sinf(x * 0.85f + z * 1.15f) * 0.22f
                      + sinf(x * 2.30f - z * 1.70f) * 0.11f;
    float effectiveDist = distPath + pathMeander;
    if (effectiveDist < 0.95f)
        return 0.0f;
    float pathFade = fminf(1.0f, (effectiveDist - 0.95f) / 0.65f); // grass returns gradually at the dirt path edge

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
    float macro = 0.45f + 0.55f * patch;

    // Flowers grow through a shorter, sparser meadow underlayer.
    float flowerSuppression = MapFlora_EvaluateFlowerSuppression(x, z, VERDANT_FLOWER_CLUSTERS, FLOWER_CLUSTER_COUNT);
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

static float VerdantSampleFlowerHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static bool VerdantIsFlowerExcluded(float x, float z, void *ctx)
{
    (void)ctx;
    return IsInsideLake(x, z, 0.85f) || DistanceToPaths(x, z) < 1.30f;
}

static float VerdantEdgeTaper(float cx, float cz, void *userData)
{
    (void)userData;
    float pathMeander = sinf(cx * 0.85f + cz * 1.15f) * 0.22f
                      + sinf(cx * 2.30f - cz * 1.70f) * 0.11f;
    float distPath = DistanceToPaths(cx, cz) + pathMeander;
    float pathEdgeT = fmaxf(0.0f, fminf(1.0f, (distPath - 0.95f) / 1.15f));
    return 0.40f + 0.60f * (pathEdgeT * pathEdgeT * (3.0f - 2.0f * pathEdgeT));
}

static void BuildMeadowLayout(void)
{
    unsigned int rng = 0x51a7c3u;
    s_grassCount = MapFlora_GenerateMeadowPlacements(
        s_grassPlacements, GRASS_TUFT_CAPACITY, &s_ground, kMapCenter,
        (MapFloraMeadowConfig){
            .minBounds = {7.0f, 6.0f}, .maxBounds = {93.0f, 69.0f},
            .spacing = 0.22f, .seed = 0x51a7c3u,
            .densityFn = VerdantGrassDensity, .ecology = &s_ecology,
            .flowerClusters = VERDANT_FLOWER_CLUSTERS, .flowerClusterCount = FLOWER_CLUSTER_COUNT,
            .edgeTaperFn = VerdantEdgeTaper,
        });
    TraceLog(LOG_INFO, "VERDANT_MEADOW: placements=%d capacity=%d",
             s_grassCount, GRASS_TUFT_CAPACITY);

    // Generalized procedural 3D wildflower colony placement
    MapFlora_GenerateFlowerPlacements(
        s_flowerPlacements, FLOWER_COUNT,
        VERDANT_FLOWER_CLUSTERS, FLOWER_CLUSTER_COUNT,
        VerdantSampleFlowerHeight, VerdantIsFlowerExcluded, NULL,
        0x51a7c3u);

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
    s_reedCount = validReeds;
}

static void DrawVerdantShadowCasters(Shader depthShader, void *userData)
{
    (void)userData;
    MapProp_DrawFloraShadowCasters(&s_flora);
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
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
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
    EnvShadow_EndStaticCapture();
}

static void ApplyVerdantEnvironment(void)
{
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){92, 110, 136, 255}); // Cool sky fill preserves shaded canopy volume
    Environment_SetSunColor((Color){255, 224, 185, 255});
    Environment_SetSunIntensity(2.30f);
    // Sun rises in the East-North ahead (X > 0, Z < 0), sunlight travels towards West-South (X < 0, Z > 0)
    Environment_SetSunDirection(Vector3Normalize((Vector3){-0.24f, -0.44f, 0.86f}));
    Environment_SetShadowColor((Color){28, 36, 48, 120});

    // Keep the playable foreground clear; haze and sun shafts belong beyond it.
    AtmosphereProfile atmos = {
        .color = {235, 190, 151, 255},
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
            .baseDensity = 0.008f,
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
    VolumetricFog_SetDistantCoverage(2.0f / 3.0f);
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
    MapProp_SetGroundTint(&s_ground, (Color){68, 92, 48, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
        "assets/textures/verdant_meadow_substrate_material.png",
        "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    s_rocks = MapProp_CreateRockSystem("assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png", "assets/textures/rock_roughness.png", ROCK_COUNT);
    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    // Bake from the actual plateau contour; no rectangular wall or per-frame search.
    MapBoundaryMistStyle rim = {1.2f, 4.0f, 0.5f, 2.0f, 0.35f};
    // Extract the cliff contour below all meadow swales, above the cloud sea.
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -1.25f, &rim))
        TraceLog(LOG_WARNING, "Verdant Path: terrain boundary mist bake failed");
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
        MapRockArchetype archetype = (i >= ROCK_COUNT - 3) ? MAP_ROCK_RIVER_STONE
                                   : (kRocks[i].radiusScale >= 0.70f ? MAP_ROCK_CRAG
                                                                     : MAP_ROCK_BOULDER);
        if (i >= ROCK_COUNT - 3) {
            float waterSurfaceY = s_lake.config.center.y;
            float depth = fmaxf(0.0f, waterSurfaceY - gy);
            s_rockPlacements[i].position.y = gy + 0.10f;
            s_rockPlacements[i].heightScale = fmaxf(kRocks[i].heightScale, depth + 0.22f);
        } else {
            s_rockPlacements[i].position.y = gy;
        }
        MapProp_AddRockItem(&s_rocks, s_rockPlacements[i].position,
                            s_rockPlacements[i].radiusScale, s_rockPlacements[i].heightScale,
                            s_rockPlacements[i].rotationDeg, archetype);
    }
    for (int i = ROCK_COUNT - 3; i < ROCK_COUNT; i++) {
        MapProp_AddWaterObstacle(&s_lake, s_rockPlacements[i].position,
                                 s_rockPlacements[i].radiusScale * 0.92f, 0.75f);
    }
    BuildMeadowLayout();
    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, VERDANT_FLOWER_CLUSTERS, FLOWER_CLUSTER_COUNT,
        (MapFloraStyle){
            .grassRootColor = {34, 58, 24, 255},
            .grassTipColor  = {114, 172, 58, 255},
            .bladesPerClump = 6,
            .bladeSegments  = 4,
            .bladeWidthScale = 0.17f,
            .lodDistance = 34.0f,
            .drawDistance = 68.0f,
            .shadowDistance = 22.0f,
            .grassState = MAP_GRASS_STATE_LUSH,
            .reedRootColor = {28, 46, 22, 255},
            .reedTipColor  = {126, 168, 76, 255},
            .hasReedPlumes = false,
        });
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
        MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
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
    MapProp_DrawRockSystem(&s_rocks, true);
    V_MARK("op_rocks");
    MapProp_DrawFloraSystem(&s_flora);
    V_MARK("op_flora");
    MapProp_DrawSkyDome(&s_sky);
    if (!getenv("WUXING_NO_CLOUD_SEA")) MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    V_MARK("op_cloudsea");
}

void DrawTransparentVerdantPathMap(void)
{
    if (!s_ready)
        return;
    if (!getenv("WUXING_NO_WATER")) MapProp_DrawWaterOverlay(&s_lake, s_time);
    MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
    DrawFarSunbeams();
}

void UnloadVerdantPathMap(void)
{
    Environment_SetSunIntensity(1.0f);
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
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadRockSystem(&s_rocks);
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

#include "maps/worlds/mau_son/mau_son.h"
#include "assets/maps/mau_son/mau_son_generated_meta.h"
#include "maps/worlds/mau_son/mau_son_rocks_generated.h"
#include "core/gfx_quality.h"
#include "core/resource_manager.h"
#include "environment/atmosphere.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "environment/volumetric/volumetric_fog.h"
#include "environment/wind/wind_system.h"
#include "maps/map_manager.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/prop_lit.h"
#include "post_process/camera_context.h"
#include "post_process/color_grade_lut.h"
#include "post_process/post_fx.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static float s_scaleXZ = 1.0f;
static float s_scaleY = 1.0f;
static float s_mapWidth = MAU_SON_MAP_WIDTH;
static float s_mapDepth = MAU_SON_MAP_DEPTH;
static float s_heightRange = MAU_SON_HEIGHT_RANGE;

#define MAP_WIDTH s_mapWidth
#define MAP_DEPTH s_mapDepth
#define CLIFF_DEPTH (MAU_SON_CLIFF_DEPTH * s_scaleY)
#define CLOUD_SEA_Y (-4.80f * s_scaleY)
#define SHADOW_FOCUS_RADIUS 22.0f
#define SHADOW_SNAP_STEP 0.25f
#define SHADOW_HYSTERESIS_XZ 0.50f
#define SHADOW_HYSTERESIS_Y 0.50f

static Vector3 kMapCenter = {MAU_SON_MAP_WIDTH * 0.5f, 0.0f, MAU_SON_MAP_DEPTH * 0.5f};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};
static Vector3 s_floraWindowCenter = {MAU_SON_ARENA_X, 0.0f, MAU_SON_ARENA_Z};

static MapDemIsland s_dem = {0};
static MapGroundSurface s_ground = {0};
static MapEcology s_habitat = {0};
static MapSkyDome s_sky = {0};
static MapCloudSea s_cloudSea = {0};
static MapCliffSkirt s_cliff = {0};
static MapWaterSurface s_glacialWater = {0};
static MapIceSurface s_glacialIce = {0};
static MapRockSystem s_rocks = {0};
static MapDebrisSystem s_debris = {0};
static MapFloraSystem s_flora = {0};
static MapTreeSystem s_trees = {0};

static WindMacroConfig s_prevWindMacro = {0};
static AtmosphereMode s_prevAtmoMode = ATMO_MODE_MOONLIGHT_DUST;
static Vector3 s_lastShadowFocus = {0};
static unsigned int s_skipMask = 0;
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

static MapZone s_mauSonZones[3];
#define MAU_SON_ZONE_COUNT 3

static float MauSonWaterBed(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static float MauSonWaterSurface(float x, float z, void *ctx)
{
    (void)ctx;
    float surfaceY = MapDem_SampleWaterHeight(&s_dem, x, z);
    return (surfaceY <= -s_dem.cliffDepth + 0.05f) ? -1e9f : surfaceY;
}

static MapDemShadowCasterCtx s_mauSonShadowCtx = {0};

static void DrawMauSonShadowCasters(Shader depthShader, void *user)
{
    (void)user;
    s_mauSonShadowCtx.flora = &s_flora;
    s_mauSonShadowCtx.rocks = &s_rocks;
    s_mauSonShadowCtx.ores = NULL;
    s_mauSonShadowCtx.trees = &s_trees;
    s_mauSonShadowCtx.skipMask = s_skipMask;
    MapDem_DefaultShadowCasterCallback(depthShader, &s_mauSonShadowCtx);
}

static void CaptureMauSonStaticShadows(void)
{
    float radius = (s_scaleXZ > 1.5f) ? 140.0f : 155.0f;
    Vector3 center = (s_scaleXZ > 1.5f) ? s_staticShadowCenter : kMapCenter;
    MapDem_CaptureStaticShadows(&s_rocks, center, radius, s_skipMask);
}

static void SpawnMauSonFreezingMist(void)
{
    FogVolume_ClearAll();
    static const LocalFogVolume kLakeMist[] = {
        {.shape = FOG_SHAPE_CYLINDER, .position = {MAU_SON_LAKE_X, 0.55f, MAU_SON_LAKE_Z}, .extents = {20.0f, 1.05f, 18.0f}, .color = {205, 228, 248, 255}, .density = 0.16f, .edgeSoftness = 0.92f, .driftVelocity = {-0.38f, 0.0f, 0.24f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {MAU_SON_TARN_X, 0.60f, MAU_SON_TARN_Z}, .extents = {16.0f, 1.15f, 16.0f}, .color = {210, 232, 250, 255}, .density = 0.16f, .edgeSoftness = 0.92f, .driftVelocity = {-0.32f, 0.0f, 0.22f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {MAU_SON_ARENA_X, 0.50f, MAU_SON_ARENA_Z}, .extents = {15.0f, 0.70f, 15.0f}, .color = {220, 236, 252, 255}, .density = 0.08f, .edgeSoftness = 0.94f, .driftVelocity = {-0.45f, 0.0f, 0.28f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {MAU_SON_ARCH_X, 0.60f, MAU_SON_ARCH_Z}, .extents = {14.0f, 1.00f, 14.0f}, .color = {208, 226, 246, 255}, .density = 0.12f, .edgeSoftness = 0.92f, .driftVelocity = {-0.40f, 0.0f, 0.25f}, .active = true},
    };
    for (int i = 0; i < (int)(sizeof(kLakeMist) / sizeof(kLakeMist[0])); i++)
    {
        LocalFogVolume v = kLakeMist[i];
        v.position.x *= s_scaleXZ;
        v.position.z *= s_scaleXZ;
        v.position.y = MapDem_GetGroundHeight(&s_dem, &s_ground, v.position.x, v.position.z) + 0.65f * s_scaleY;
        v.extents.x *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        v.extents.y *= s_scaleY;
        v.extents.z *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        FogVolume_Create(&v);
    }
}

static float MauSonFrostSedgeDensity(float x, float z, void *user)
{
    (void)user;
    Vector3 groundPos, groundNormal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &groundPos, &groundNormal))
        return 0.0f;
    if (groundNormal.y < 0.74f || groundPos.y < 0.15f)
        return 0.0f;

    float waterY = 0.0f, waterDepth = 0.0f;
    if (MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &waterY, &waterDepth))
        return 0.0f;

    float patch = sinf(x * 0.15f + z * 0.11f) * cosf(z * 0.18f - x * 0.13f);
    float pocket = (patch > 0.34f) ? ((patch - 0.34f) * 0.85f) : 0.0f;
    return fminf(0.66f, pocket);
}

static void BuildMauSonRocks(void)
{
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rocks/slate_diffuse.png",
        "assets/textures/rocks/slate_normal.png",
        "assets/textures/rocks/slate_roughness.png",
        MAU_SON_ROCK_COUNT + 16);

    for (int i = 0; i < MAU_SON_ROCK_COUNT; i++)
    {
        const MapRockItem *item = &MAU_SON_ROCK_ITEMS[i];
        Vector3 pos = {item->position.x * s_scaleXZ, item->position.y * s_scaleY, item->position.z * s_scaleXZ};
        float rScale = item->radiusScale * (1.0f + 0.52f * (s_scaleXZ - 1.0f));
        float hScale = item->heightScale * (1.0f + 0.25f * (s_scaleY - 1.0f));
        Vector3 gPos = pos, gNorm = {0.0f, 1.0f, 0.0f};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &gPos, &gNorm))
        {
            if (i >= 2 && i <= 3 && item->position.y > gPos.y + 1.5f)
            {
                pos.y = gPos.y + (item->position.y - gPos.y) * s_scaleY;
            }
            else
            {
                float sink = (item->archetype == MAP_ROCK_SCREE) ? hScale * 0.18f : hScale * 0.22f;
                pos.y = gPos.y - sink;
            }
        }
        MapProp_AddRockItem(&s_rocks, pos, rScale, hScale, item->rotationDeg, item->archetype);
    }
}

#define MAU_SON_MAX_GRASS 36000
#define MAU_SON_MAX_REEDS 360
#define MAU_SON_MAX_FLOWERS 840
#define MAU_SON_MAX_SHRUBS 400
static MapMeadowPlacement s_grassPlacements[MAU_SON_MAX_GRASS];
static MapMeadowPlacement s_reedPlacements[MAU_SON_MAX_REEDS];
static int s_grassCount = 0;
static int s_reedCount = 0;
static MapFlowerPlacement s_flowerPlacements[MAU_SON_MAX_FLOWERS];
static int s_flowerCount = 0;
static MapShrubPlacement s_shrubPlacements[MAU_SON_MAX_SHRUBS];
static int s_shrubCount = 0;
static MapDebrisPlacement s_scaledDebris[MAU_SON_DEBRIS_COUNT > 0 ? MAU_SON_DEBRIS_COUNT : 1];

static float MauSonShrubWaterClearance(float x, float z, void *ctx)
{
    (void)ctx;
    float wY = 0.0f, wD = 0.0f;
    if (MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &wY, &wD))
        return 0.0f;
    return 1.0f;
}

static void BuildMauSonDebris(void)
{
    int count = MAU_SON_DEBRIS_COUNT;
    if (count <= 0)
        return;
    for (int i = 0; i < count; i++)
    {
        s_scaledDebris[i] = MAU_SON_DEBRIS_ITEMS[i];
        s_scaledDebris[i].position.x *= s_scaleXZ;
        s_scaledDebris[i].position.z *= s_scaleXZ;
        Vector3 gp = {0}, gn = {0};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, s_scaledDebris[i].position.x, s_scaledDebris[i].position.z, &gp, &gn))
            s_scaledDebris[i].position.y = gp.y;
        else
            s_scaledDebris[i].position.y *= s_scaleY;
        s_scaledDebris[i].scale *= (1.0f + 0.18f * (s_scaleXZ - 1.0f));
    }
    s_debris = MapProp_CreateDebrisSystem(s_scaledDebris, count, &s_ground, kMapCenter, fminf(36.0f * s_scaleXZ, 75.0f));
}

static float MauSonFlowerHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static bool MauSonFlowerExcluded(float x, float z, void *ctx)
{
    (void)ctx;
    Vector3 gp = {0}, gn = {0};
    float wy = 0.0f, wd = 0.0f;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &gp, &gn))
        return true;
    if (gn.y < 0.78f || gp.y < 0.20f)
        return true;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &wy, &wd);
}

static void BuildMauSonFlora(void)
{
    Vector2 minB = {14.0f * s_scaleXZ, 14.0f * s_scaleXZ};
    Vector2 maxB = {MAP_WIDTH - 14.0f * s_scaleXZ, MAP_DEPTH - 14.0f * s_scaleXZ};
#ifdef __ANDROID__
    const float grassSpacing = 1.35f;
    const int maxGrass = 2400;
#else
    float grassSpacing = (s_scaleXZ > 1.5f) ? 0.62f : 0.72f;
    const int maxGrass = MAU_SON_MAX_GRASS;
#endif
    float lakeCx = MAU_SON_LAKE_X * s_scaleXZ;
    float lakeCz = MAU_SON_LAKE_Z * s_scaleXZ;
    const float arenaCx = MAU_SON_ARENA_X * s_scaleXZ;
    const float arenaCz = MAU_SON_ARENA_Z * s_scaleXZ;

    MapMeadowDistribution dist = {
        .minBounds = minB,
        .maxBounds = maxB,
        .spacing = grassSpacing,
        .jitter = 0.28f,
        .minRadius = 0.16f,
        .maxRadius = 0.26f,
        .minHeight = 0.20f,
        .maxHeight = 0.36f,
        .yOffset = 0.0f,
        .seed = 0x50F8A2u,
        .focalPoints = {{arenaCx, arenaCz}, {lakeCx, lakeCz}},
        .focalCount = (s_scaleXZ > 1.5f) ? 2 : 0,
        .focalCoreRadius = 68.0f,
        .focalOuterRadius = 210.0f,
        .focalOuterFloor = 0.14f,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, maxGrass,
        &s_ground, kMapCenter,
        dist, MauSonFrostSedgeDensity, NULL);

    // Shoreline frost reeds + silver-white Cỏ Lau plumes around Thiên Trì Hàn Đàm
    s_reedCount = 0;
    float scanR = 26.0f * s_scaleXZ;
    unsigned int rRng = 0x731942u;
    for (int a = 0; a < 480 && s_reedCount < MAU_SON_MAX_REEDS; a++)
    {
        rRng = rRng * 1664525u + 1013904223u;
        float u0 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        rRng = rRng * 1664525u + 1013904223u;
        float u1 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        float ang = u0 * (2.0f * PI);
        float rad = (6.0f + u1 * scanR);
        float rx = lakeCx + cosf(ang) * rad;
        float rz = lakeCz + sinf(ang) * rad;
        float wY = 0.0f, wD = 0.0f;
        if (MapDem_GetWaterInfo(&s_dem, &s_ground, rx, rz, &wY, &wD))
            continue;
        bool nearWater = false;
        for (int k = 0; k < 4 && !nearWater; k++)
        {
            float ka = (float)k * (PI * 0.5f);
            if (MapDem_GetWaterInfo(&s_dem, &s_ground, rx + cosf(ka) * 1.6f, rz + sinf(ka) * 1.6f, &wY, &wD))
                nearWater = true;
        }
        if (!nearWater)
            continue;
        Vector3 gp, gn;
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &gp, &gn) && gn.y > 0.72f)
        {
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = gp,
                .radius = 0.24f + 0.06f * u0,
                .height = 0.88f + 0.34f * u1,
                .rotationDeg = u0 * 360.0f,
                .phase = u1,
            };
        }
    }

    s_shrubCount = MapFlora_GenerateShrubPlacements(
        s_shrubPlacements, &s_ground, kMapCenter,
        (MapShrubDistributionConfig){
            .minBounds = minB,
            .maxBounds = maxB,
            .maxCount = (s_scaleXZ > 1.5f) ? MAU_SON_MAX_SHRUBS : 110,
            .minSpacing = 2.90f,
            .seed = 0x50F8B3u,
            .alpineWeight = 1.0f,
            .rocks = s_rocks.items,
            .rockCount = s_rocks.count,
            .sampleWaterClearance = MauSonShrubWaterClearance,
            .useStratifiedGrid = (s_scaleXZ > 1.5f),
            .focalPoints = {{arenaCx, arenaCz}, {lakeCx, lakeCz}},
            .focalCount = (s_scaleXZ > 1.5f) ? 2 : 0,
            .focalCoreRadius = 65.0f,
        });

    // Colonies of Thiên Sơn Tuyết Liên, U Lan Linh Thảo & Kim Ngân Linh Thảo around the snowy saddle
    static MapFloraFlowerCluster s_flowerClusters[15];
    const int clusterCount = (s_scaleXZ > 1.5f) ? 15 : 7;
    const int flowersPerCluster = 55;
    if (s_scaleXZ > 1.5f)
    {
        static const int kAlpineSpecies[3] = {MAP_FLOWER_SPECIES_TUYET_LIEN, MAP_FLOWER_SPECIES_U_LAN, MAP_FLOWER_SPECIES_KIM_NGAN};
        MapFlora_GenerateOpenWorldFlowerClusters(
            s_flowerClusters, clusterCount, flowersPerCluster,
            (Vector2){arenaCx, arenaCz}, (Vector2){lakeCx, lakeCz},
            7.5f, 24.0f, 165.0f,
            kAlpineSpecies, 3, (Color){175, 232, 255, 255},
            MauSonFlowerHeight, MauSonFlowerExcluded, NULL, 0xA19E55u);
    }
    else
    {
        for (int i = 0; i < clusterCount; i++)
        {
            float ang = (float)i * (6.2831853f / (float)clusterCount) + 0.28f;
            float distR = 17.0f + (float)(i % 2) * 3.8f;
            float fx = arenaCx + cosf(ang) * distR;
            float fz = arenaCz + sinf(ang) * distR;
            float fy = MapDem_GetGroundHeight(&s_dem, &s_ground, fx, fz);
            int sp = (i % 3 == 1) ? MAP_FLOWER_SPECIES_U_LAN : ((i % 3 == 2) ? MAP_FLOWER_SPECIES_KIM_NGAN : MAP_FLOWER_SPECIES_TUYET_LIEN);
            s_flowerClusters[i] = (MapFloraFlowerCluster){
                .center = {fx, fy, fz},
                .radius = {3.2f, 2.8f},
                .flowerCount = flowersPerCluster,
                .primarySpecies = sp,
                .species = {(unsigned char)sp, (unsigned char)sp, MAP_FLOWER_SPECIES_TUYET_LIEN, MAP_FLOWER_SPECIES_KIM_NGAN},
                .primaryColor = MapProp_GetFlowerSpeciesColor(sp, false),
                .accentColor = MapProp_GetFlowerSpeciesColor(sp, true),
                .discalCenterColor = {175, 232, 255, 255},
            };
        }
    }
    s_flowerCount = clusterCount * flowersPerCluster;
    MapFlora_GenerateFlowerPlacements(s_flowerPlacements, s_flowerCount, s_flowerClusters, clusterCount,
                                      MauSonFlowerHeight, MauSonFlowerExcluded, NULL, 0xA19E55u);

    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, s_flowerClusters, clusterCount,
        (MapFloraStyle){
            .grassRootColor = {68, 84, 98, 255},
            .grassTipColor = {218, 232, 245, 255},
            .bladesPerClump = 5,
            .bladeSegments = 3,
            .bladeWidthScale = 0.125f,
            .lodDistance = 36.0f,
            .midLodDistance = 24.0f,
            .drawDistance = 120.0f,
            .shadowDistance = (s_scaleXZ > 1.5f) ? 18.0f : 22.0f,
            .grassState = MAP_GRASS_STATE_DRY,
            .dryness = 0.65f,
            .scorch = 0.0f,
            .reedRootColor = {62, 82, 96, 255},
            .reedTipColor = {206, 224, 238, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 0.85f,
            .enableCoLauPlumes = true,
            .coLauOnReeds = true,
            .coLauPlumeDensity = 0.85f,
            .coLauPlumeTint = {235, 244, 255, 255},
            .grassGrowthForm = MAP_MEADOW_GROWTH_ALPINE_MAT,
            .shrubPlacements = s_shrubPlacements,
            .shrubCount = s_shrubCount,
            .shrubDrawDistance = 95.0f,
        });
}

void InitMauSonMap(void)
{
    s_scaleXZ = MapManager_GetWorldScaleXZ();
    s_scaleY = MapManager_GetWorldScaleY();
    s_mapWidth = MAU_SON_MAP_WIDTH * s_scaleXZ;
    s_mapDepth = MAU_SON_MAP_DEPTH * s_scaleXZ;
    s_heightRange = MAU_SON_HEIGHT_RANGE * s_scaleY;
    kMapCenter = (Vector3){s_mapWidth * 0.5f, 0.0f, s_mapDepth * 0.5f};

    MapManager_SetActiveMetadata(&(MapSpatialMetadata){
        .spawnPoint = {135.0f * s_scaleXZ, 0.0f, 112.5f * s_scaleXZ},
        .arenaCenter = {135.0f * s_scaleXZ, 0.0f, 112.5f * s_scaleXZ},
        .arenaRadius = 140.0f * s_scaleXZ,
        .hasPlayableRect = true,
        .minX = 8.0f * s_scaleXZ, .maxX = 292.0f * s_scaleXZ,
        .minZ = 8.0f * s_scaleXZ, .maxZ = 217.0f * s_scaleXZ,
    });
    s_staticShadowCenter = kMapCenter;
    s_floraWindowCenter = (Vector3){MAU_SON_ARENA_X * s_scaleXZ, 0.0f, MAU_SON_ARENA_Z * s_scaleXZ};

    s_mauSonZones[0] = (MapZone){.center = {MAU_SON_LAKE_X * s_scaleXZ, 0.0f, MAU_SON_LAKE_Z * s_scaleXZ}, .radius = 30.0f * s_scaleXZ, .type = NAT_RIVER};
    s_mauSonZones[1] = (MapZone){.center = {MAU_SON_TARN_X * s_scaleXZ, 0.0f, MAU_SON_TARN_Z * s_scaleXZ}, .radius = 28.0f * s_scaleXZ, .type = NAT_RIVER};
    s_mauSonZones[2] = (MapZone){.center = {MAU_SON_ARENA_X * s_scaleXZ, 0.0f, MAU_SON_ARENA_Z * s_scaleXZ}, .radius = 24.0f * s_scaleXZ, .type = NAT_RIVER};

    s_prevWindMacro = Wind_GetMacro();
    WindMacroConfig monsoonWind = {
        .baseDirection = {-3.60f, 0.0f, 2.20f},
        .gustAmplitude = 1.95f,
        .noiseScale = 0.068f,
        .noiseSpeed = 2.05f,
        .terrainLiftK = 1.65f,
        .heightGradientK = 0.90f,
    };
    Wind_SetMacro(&monsoonWind);

    s_skipMask = MapProp_GetSkipMask();
    if (s_skipMask & MAP_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else if (!ColorGradeLut_Load("assets/maps/mau_son/mau_son_grade.png"))
        ColorGradeLut_UseNeutral();

    // 5 Alpine Time-of-Day Presets (Default: 07:30 Crisp Winter Alpine Dawn)
    static const EnvLightingPreset kMauSonToD[5] = {
        {
            .ambientColor = {86, 114, 152, 255},
            .sunColor = {255, 218, 186, 255},
            .sunDirection = {0.78f, -0.28f, -0.56f},
            .shadowColor = {42, 62, 98, 165},
            .fog = {.enabled = true, .color = {184, 206, 232, 255}, .start = 32.0f, .end = 225.0f, .density = 0.0046f},
            .atmosphere = {
                .color = {184, 206, 232, 255}, .start = 32.0f, .end = 225.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0085f, 0.0135f, 0.0260f}, .mieScattering = 0.0042f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.0046f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {104, 130, 166, 255},
            .sunColor = {246, 250, 255, 255},
            .sunDirection = {0.32f, -0.88f, -0.35f},
            .shadowColor = {48, 70, 110, 160},
            .fog = {.enabled = true, .color = {196, 218, 242, 255}, .start = 38.0f, .end = 245.0f, .density = 0.0038f},
            .atmosphere = {
                .color = {196, 218, 242, 255}, .start = 38.0f, .end = 245.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0078f, 0.0128f, 0.0250f}, .mieScattering = 0.0036f, .mieAnisotropy = 0.76f, .multipleScatteringAmp = 1.9f},
                .density = {.baseDensity = 0.0038f, .heightFalloff = 0.22f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {90, 108, 148, 255},
            .sunColor = {255, 198, 162, 255},
            .sunDirection = {0.68f, -0.34f, -0.65f},
            .shadowColor = {40, 56, 94, 170},
            .fog = {.enabled = true, .color = {188, 202, 228, 255}, .start = 30.0f, .end = 215.0f, .density = 0.0048f},
            .atmosphere = {
                .color = {188, 202, 228, 255}, .start = 30.0f, .end = 215.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0092f, 0.0138f, 0.0265f}, .mieScattering = 0.0048f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.1f},
                .density = {.baseDensity = 0.0048f, .heightFalloff = 0.26f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {70, 88, 128, 255},
            .sunColor = {255, 162, 134, 255},
            .sunDirection = {-0.84f, -0.22f, -0.49f},
            .shadowColor = {30, 44, 78, 175},
            .fog = {.enabled = true, .color = {148, 166, 202, 255}, .start = 26.0f, .end = 200.0f, .density = 0.0056f},
            .atmosphere = {
                .color = {148, 166, 202, 255}, .start = 26.0f, .end = 200.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0102f, 0.0142f, 0.0275f}, .mieScattering = 0.0055f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.15f},
                .density = {.baseDensity = 0.0056f, .heightFalloff = 0.26f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {28, 44, 82, 255},
            .sunColor = {186, 218, 255, 255},
            .sunDirection = {0.50f, -0.46f, -0.73f},
            .shadowColor = {10, 18, 38, 210},
            .fog = {.enabled = true, .color = {28, 44, 78, 255}, .start = 26.0f, .end = 200.0f, .density = 0.0060f},
            .atmosphere = {
                .color = {28, 44, 78, 255}, .start = 26.0f, .end = 200.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0050f, 0.0085f, 0.0205f}, .mieScattering = 0.0042f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 1.85f},
                .density = {.baseDensity = 0.0052f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        }
    };
    static const float kMauSonTimes[5] = {0.28f, 0.50f, 0.68f, 0.78f, 0.92f};
    Environment_SetTimeOfDayPresets(kMauSonToD, kMauSonTimes, 5);
    Environment_SetTimeOfDay(0.28f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){86, 114, 152, 255});
    Environment_SetSunColor((Color){255, 218, 186, 255});
    Environment_SetSunIntensity(1.65f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.78f, -0.28f, -0.56f}));
    Environment_SetShadowColor((Color){42, 62, 98, 165});
    if (Environment_IsNightMode())
        Environment_ApplyMoonlightNight();

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 0.98f,
        .contrast = 1.08f,
        .saturation = 1.06f,
        .bloomIntensity = 0.22f,
        .shadowTint = {0.82f, 0.92f, 1.12f},
        .highlightTint = {1.02f, 1.01f, 0.98f},
        .bloomThreshold = 1.12f,
        .bloomKnee = 0.42f,
        .hueRestore = 0.62f,
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);

    AtmosphereProfile atmos = kMauSonToD[0].atmosphere;
    atmos.enabled = false;
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(1.05f);
    VolumetricFog_SetDistantCoverage(0.58f);
    s_prevAtmoMode = Atmosphere_GetMode();
    Atmosphere_SetMode(ATMO_MODE_SPIRIT_SPARKS);
    Atmosphere_Configure(s_floraWindowCenter, (Vector3){32.0f, 4.5f, 32.0f}, 340, (Color){185, 228, 255, 255});
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.24f,
        .worldSize = 160.0f,
        .planeHeight = 88.0f,
        .coverage = 0.38f,
        .softness = 0.34f,
        .windSpeedScale = 0.85f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);

    // Activate Alpine Snow & Glacial Water-Element Biome Shaders
    MapGroundBiomeProfile alpineBiome = {
        .aridFactor = 0.0f,
        .strataStrength = 0.78f,
        .grusColor = {0.58f, 0.46f, 0.34f},
        .hardpanColor = {0.42f, 0.31f, 0.21f},
        .lichenColor = {0.50f, 0.40f, 0.19f},
        .volcanicFactor = 0.0f,
        .magmaGlowStrength = 0.0f,
        .ashColor = {0.16f, 0.15f, 0.16f},
        .scoriaColor = {0.34f, 0.14f, 0.10f},
        .sulfurColor = {0.62f, 0.50f, 0.16f},
        .alpineFactor = 1.0f,
        .snowSparkleStrength = 1.25f,
        .snowColor = {0.88f, 0.92f, 0.97f},
        .permafrostColor = {0.22f, 0.26f, 0.32f},
        .glacialIceColor = {0.36f, 0.72f, 0.86f},
    };
    MapProp_SetGroundBiomeProfile(&alpineBiome);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetRockVolcanicMode(0.0f);
    MapProp_SetRockAlpineMode(1.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(0.0f);
    MapProp_SetCliffAlpineMode(1.0f);
    MapProp_SetGrassState(MAP_GRASS_STATE_DRY, 0.0f);
    MapProp_SetSnow(0.85f);

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawMauSonShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        CaptureMauSonStaticShadows();
        if (!(s_skipMask & MAP_SKIP_FOGVOLUMES))
            SpawnMauSonFreezingMist();
        MapManager_SetZones(s_mauSonZones, MAU_SON_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = camera.target;
    EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/mau_son/mau_son_ecology_splat.png",
                      "assets/maps/mau_son/mau_son_water_height.png");

    const char *hfPath = (s_scaleXZ > 1.5f && FileExists("assets/maps/mau_son/mau_son_heightfield_ow.png"))
                             ? "assets/maps/mau_son/mau_son_heightfield_ow.png"
                             : "assets/maps/mau_son/mau_son_heightfield.png";
    s_ground = MapProp_CreateGroundHeightfield16(
        hfPath, MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 36.0f,
        "assets/maps/mau_son/mau_son_ecology_splat.png",
        "assets/maps/mau_son/textures/alpine_snow_diffuse.png",
        "assets/maps/mau_son/textures/frozen_tundra_diffuse.png");

    MapEcology_LoadBakedAssets(&s_habitat,
                               "assets/maps/mau_son/mau_son_habitat.png",
                               "assets/maps/mau_son/mau_son_habitat_distance.png",
                               (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH},
                               NULL, 0, NULL, NULL);
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    MapProp_SetGroundTint(&s_ground, (Color){234, 240, 248, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/maps/mau_son/textures/alpine_snow_material.png",
                                 "assets/maps/mau_son/textures/frozen_tundra_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/maps/mau_son/mau_son_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/mau_son/mau_son_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rocks/slate_diffuse.png",
                              "assets/textures/rocks/slate_normal.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 320.0f, MAP_DEPTH + 320.0f, 52.0f);
    MapBoundaryMistStyle rimStyle = {1.4f, 4.4f, 0.5f, 2.2f, 0.38f};
    MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -0.35f * s_scaleY, &rimStyle);

    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    // Conformal Freezing Glacial Lakes & Cascading Streams
    s_glacialWater = MapProp_CreateTerrainWater(
        "assets/maps/mau_son/mau_son_water.obj",
        (MapWaterConfig){
            .ecosystem = WATER_ECO_ALPINE_STREAM,
            .center = {MAU_SON_LAKE_X * s_scaleXZ, 0.0f, MAU_SON_LAKE_Z * s_scaleXZ},
            .radiusX = MAU_SON_LAKE_RX * s_scaleXZ,
            .radiusZ = MAU_SON_LAKE_RZ * s_scaleXZ,
            .waveHeight = 0.012f,
            .waveScale = 1.25f,
            .waveSpeed = 0.55f,
            .flowVelocity = {0.16f, 0.14f},
            .detailScale = 0.22f,
            .detailStrength = 0.16f,
            .maxDepth = 1.85f,
            .absorption = {0.48f, 0.16f, 0.05f},
            .scatterColor = {0.12f, 0.54f, 0.68f},
            .scatterCoeff = 0.42f,
            .causticsStrength = 0.44f,
            .causticsScale = 2.10f,
            .foamThreshold = 0.12f,
            .deepColor = {8, 34, 64, 255},
            .shallowColor = {38, 156, 184, 240},
            .foamColor = {236, 246, 255, 245},
        });
    if (s_glacialWater.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_glacialWater.waterModel.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_glacialWater.boundsCenter = (Vector3){
            s_glacialWater.boundsCenter.x * s_scaleXZ,
            s_glacialWater.boundsCenter.y * s_scaleY,
            s_glacialWater.boundsCenter.z * s_scaleXZ};
        s_glacialWater.boundsRadius *= s_scaleXZ;
    }
    MapProp_SetWaterTerrainSource(&s_glacialWater, MauSonWaterBed, MauSonWaterSurface, NULL);

    // Conformal Parallax-Cracked Glacial Ice Shelves & Floating Voronoi Ice Floes
    s_glacialIce = MapProp_CreateTerrainIce(
        "assets/maps/mau_son/mau_son_water.obj",
        (MapIceConfig){
            .center = {MAU_SON_LAKE_X * s_scaleXZ, 0.0f, MAU_SON_LAKE_Z * s_scaleXZ},
            .radiusX = MAU_SON_LAKE_RX * s_scaleXZ,
            .radiusZ = MAU_SON_LAKE_RZ * s_scaleXZ,
            .driftSpeed = 0.07f,
            .driftDirection = {0.65f, 0.55f},
            .floeCoverage = 0.34f,
            .crackIntensity = 1.35f,
            .snowCrustAmount = 0.68f,
            .deepIceColor = {18, 72, 118, 255},
            .shallowIceColor = {126, 204, 228, 255},
            .snowRimeColor = {236, 244, 252, 255},
            .flowMapPath = "assets/maps/mau_son/mau_son_ice_flow.png",
            .mapWidth = MAP_WIDTH,
            .mapDepth = MAP_DEPTH,
        });
    if (s_glacialIce.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_glacialIce.model.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_glacialIce.boundsCenter = (Vector3){
            s_glacialIce.boundsCenter.x * s_scaleXZ,
            s_glacialIce.boundsCenter.y * s_scaleY,
            s_glacialIce.boundsCenter.z * s_scaleXZ};
        s_glacialIce.boundsRadius *= s_scaleXZ;
    }

    BuildMauSonRocks();
    BuildMauSonDebris();
    BuildMauSonFlora();

    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnMauSonFreezingMist();

    s_ready = true;
    EnvShadow_SetMapCasterCallback(DrawMauSonShadowCasters, NULL);
    EnvShadow_InvalidateStaticCache();
    if (s_scaleXZ > 1.5f)
    {
        s_staticShadowCenter = s_floraWindowCenter;
        EnvShadow_InvalidateStaticCache();
    }

    // Alpine & boreal snow-laden tree grove framing spawn, frozen lake, and mountain ridges (both 1x and Open World)
    {
        float spX = 135.0f * s_scaleXZ;
        float spZ = 112.5f * s_scaleXZ;
        float lkX = MAU_SON_LAKE_X * s_scaleXZ;
        float lkZ = MAU_SON_LAKE_Z * s_scaleXZ;
        const MapTreePlacement kMauSonTrees[] = {
            // Immediate foreground ring around spawn (14m - 36m in physical meters)
            {.position = {spX - 18.0f, 0.0f, spZ - 14.0f}, .scaleXZ = 1.02f, .scaleY = 1.05f, .rotationDeg = 18.0f, .phase = 0.12f, .canopyTint = {232, 242, 252, 255}, .archetype = MAP_TREE_ALPINE_SNOW_SPRUCE},
            {.position = {spX + 20.0f, 0.0f, spZ - 16.0f}, .scaleXZ = 1.08f, .scaleY = 1.12f, .rotationDeg = 85.0f, .phase = 0.34f, .canopyTint = {228, 240, 250, 255}, .archetype = MAP_TREE_COLUMNAR_TAIGA_SPRUCE},
            {.position = {spX - 24.0f, 0.0f, spZ + 14.0f}, .scaleXZ = 1.04f, .scaleY = 1.06f, .rotationDeg = 142.0f, .phase = 0.56f, .canopyTint = {235, 244, 254, 255}, .archetype = MAP_TREE_OLD_GROWTH_FIR},
            {.position = {spX + 18.0f, 0.0f, spZ + 20.0f}, .scaleXZ = 0.96f, .scaleY = 1.00f, .rotationDeg = 215.0f, .phase = 0.78f, .canopyTint = {238, 246, 255, 255}, .archetype = MAP_TREE_FROSTED_BIRCH},
            {.position = {spX - 12.0f, 0.0f, spZ - 24.0f}, .scaleXZ = 1.10f, .scaleY = 1.08f, .rotationDeg = 278.0f, .phase = 0.21f, .canopyTint = {230, 240, 250, 255}, .archetype = MAP_TREE_GNARLED_MOUNTAIN_PINE},
            {.position = {spX + 26.0f, 0.0f, spZ + 4.0f}, .scaleXZ = 1.02f, .scaleY = 1.02f, .rotationDeg = 310.0f, .phase = 0.44f, .canopyTint = {226, 236, 246, 255}, .archetype = MAP_TREE_FROZEN_DEADWOOD},
            // Mid-ring around spawn (38m - 62m)
            {.position = {spX - 42.0f, 0.0f, spZ - 34.0f}, .scaleXZ = 1.18f, .scaleY = 1.22f, .rotationDeg = 24.0f, .phase = 0.19f, .canopyTint = {232, 242, 252, 255}, .archetype = MAP_TREE_ALPINE_SNOW_SPRUCE},
            {.position = {spX + 40.0f, 0.0f, spZ - 36.0f}, .scaleXZ = 1.15f, .scaleY = 1.20f, .rotationDeg = 112.0f, .phase = 0.51f, .canopyTint = {230, 242, 252, 255}, .archetype = MAP_TREE_COLUMNAR_TAIGA_SPRUCE},
            {.position = {spX - 44.0f, 0.0f, spZ + 32.0f}, .scaleXZ = 1.22f, .scaleY = 1.18f, .rotationDeg = 188.0f, .phase = 0.83f, .canopyTint = {228, 240, 250, 255}, .archetype = MAP_TREE_OLD_GROWTH_FIR},
            {.position = {spX + 42.0f, 0.0f, spZ + 34.0f}, .scaleXZ = 1.10f, .scaleY = 1.12f, .rotationDeg = 295.0f, .phase = 0.42f, .canopyTint = {236, 246, 255, 255}, .archetype = MAP_TREE_FROSTED_BIRCH},
            // Lake & ridge boreal stands (physical meters around lake)
            {.position = {lkX - 24.0f, 0.0f, lkZ - 14.0f}, .scaleXZ = 1.15f, .scaleY = 1.18f, .rotationDeg = 42.0f, .phase = 0.15f, .canopyTint = {232, 242, 252, 255}, .archetype = MAP_TREE_ALPINE_SNOW_SPRUCE},
            {.position = {lkX + 22.0f, 0.0f, lkZ - 14.0f}, .scaleXZ = 1.06f, .scaleY = 1.10f, .rotationDeg = 128.0f, .phase = 0.39f, .canopyTint = {236, 246, 255, 255}, .archetype = MAP_TREE_FROSTED_BIRCH},
            {.position = {lkX - 20.0f, 0.0f, lkZ + 18.0f}, .scaleXZ = 1.18f, .scaleY = 1.20f, .rotationDeg = 195.0f, .phase = 0.63f, .canopyTint = {228, 240, 250, 255}, .archetype = MAP_TREE_OLD_GROWTH_FIR},
            {.position = {lkX + 26.0f, 0.0f, lkZ + 16.0f}, .scaleXZ = 1.08f, .scaleY = 1.15f, .rotationDeg = 255.0f, .phase = 0.87f, .canopyTint = {230, 242, 252, 255}, .archetype = MAP_TREE_COLUMNAR_TAIGA_SPRUCE},
            {.position = {lkX - 32.0f, 0.0f, lkZ + 4.0f}, .scaleXZ = 1.12f, .scaleY = 1.08f, .rotationDeg = 330.0f, .phase = 0.28f, .canopyTint = {232, 242, 252, 255}, .archetype = MAP_TREE_GNARLED_MOUNTAIN_PINE},
            {.position = {lkX + 12.0f, 0.0f, lkZ + 26.0f}, .scaleXZ = 1.00f, .scaleY = 1.00f, .rotationDeg = 64.0f, .phase = 0.72f, .canopyTint = {224, 234, 246, 255}, .archetype = MAP_TREE_FROZEN_DEADWOOD},
        };
        int treeCount = (int)(sizeof(kMauSonTrees) / sizeof(kMauSonTrees[0]));
        s_trees = MapProp_CreateTreeSystem(kMauSonTrees, treeCount, &s_ground, kMapCenter, fminf(135.0f * s_scaleXZ, 195.0f));
        MapProp_SetTreeSnowCoverage(&s_trees, 0.70f);
    }
    CaptureMauSonStaticShadows();
    MapManager_SetZones(s_mauSonZones, MAU_SON_ZONE_COUNT);
    extern void ResolveCollisionMauSonMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("MAU_SON", ResolveCollisionMauSonMap);
}

void DrawMauSonMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    if (!(s_skipMask & MAP_SKIP_ROCKS))
    {
        MapProp_DrawRockSystem(&s_rocks, false);
        MapProp_DrawDebrisSystem(&s_debris, camera.position);
    }
    if (!(s_skipMask & MAP_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        MapProp_DrawCliffSkirt(&s_cliff, camera);
    if (!(s_skipMask & MAP_SKIP_WATER))
    {
        float t = (float)fmod(GetTime(), 3600.0);
        MapProp_DrawIceSurface(&s_glacialIce, t);
    }
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);
    if (s_trees.ready)
        MapProp_DrawTreeSystem(&s_trees, s_flora.time, s_flora.windDirection, s_flora.windStrength);
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentMauSonMap(void)
{
    if (!s_ready)
        return;
    float t = (float)fmod(GetTime(), 3600.0);
    if (!(s_skipMask & MAP_SKIP_MIST))
        MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_WATER))
        MapProp_DrawWaterOverlay(&s_glacialWater, t);
    if (!(s_skipMask & MAP_SKIP_FLUFF))
        MapProp_DrawSnow(camera.target, camera, t);
}

void UpdateMauSonMap(float dt)
{
    if (!s_ready)
        return;

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    if (s_trees.ready)
        MapProp_PrepareTreeSystem(&s_trees, camera);
    MapProp_UpdateWaterSurface(&s_glacialWater, dt);
    MapProp_UpdateSnow(dt);

    MapDem_UpdateShadowFocusAndStaticCache(
        camera.target, &s_lastShadowFocus, &s_staticShadowCenter,
        SHADOW_FOCUS_RADIUS, SHADOW_SNAP_STEP, SHADOW_HYSTERESIS_XZ, SHADOW_HYSTERESIS_Y,
        s_scaleXZ, kMapCenter, (s_scaleXZ > 1.5f) ? 140.0f : 155.0f, &s_rocks, s_skipMask);
}

void UnloadMauSonMap(void)
{
    Wind_SetMacro(&s_prevWindMacro);
    Atmosphere_SetMode(s_prevAtmoMode);
    ColorGradeLut_UseNeutral();
    PostFX_ClearMapProfile();
    Environment_SetSunIntensity(1.0f);
    MapProp_SetGroundBiomeProfile(NULL);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetRockVolcanicMode(0.0f);
    MapProp_SetRockAlpineMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(0.0f);
    MapProp_SetCliffAlpineMode(0.0f);
    MapProp_SetSnow(0.0f);

    if (!s_ready)
        return;

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadIceSurface(&s_glacialIce);
    MapProp_UnloadWaterSurface(&s_glacialWater);
    MapProp_UnloadTreeSystem(&s_trees);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
    MapProp_UnloadRockSystem(&s_rocks);
    if (s_cliff.ready)
        MapProp_UnloadCliffSkirt(&s_cliff);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    MapProp_UnloadGround(&s_ground);

    MapDem_UnloadIsland(&s_dem);
    if (s_habitat.distanceTexture.id > 0)
        UnloadTexture(s_habitat.distanceTexture);
    s_habitat = (MapEcology){0};

#if !defined(__ANDROID__)
    EnvShadow_SetEnabled(s_shadowWasEnabled);
#endif
    s_ready = false;
}

float GetGroundHeightMauSonMap(float x, float z)
{
    if (!s_ready)
        return 0.0f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

bool SampleGroundSurfaceMauSonMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (!s_ready)
        return false;
    return MapDem_SampleGroundSurface(&s_dem, &s_ground, x, z, outPosition, outNormal);
}

bool GetWaterInfoMauSonMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready)
        return false;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, outSurfaceY, outWaterDepth);
}

void SetWaterInteractorMauSonMap(Vector3 position, Vector3 velocity, float radius)
{
    if (!s_ready)
        return;
    MapDem_SetWaterInteractor(&s_dem, &s_ground, &s_glacialWater, position, velocity, radius);
}

void AddWaterRippleMauSonMap(Vector3 position, float radius, float intensity)
{
    if (!s_ready)
        return;
    MapDem_AddWaterRipple(&s_glacialWater, position, radius, intensity);
}

void ResolveCollisionMauSonMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY)
{
    if (!s_ready)
        return;
    MapProp_ResolveRockSystemCollision(&s_rocks, pos, agentRadius, stepHeight, inOutGroundY);
}

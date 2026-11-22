#include "maps/worlds/phu_quoc/phu_quoc.h"
#include "assets/maps/phu_quoc/phu_quoc_generated_meta.h"
#include "maps/worlds/phu_quoc/phu_quoc_rocks_generated.h"
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
static float s_mapWidth = PHU_QUOC_MAP_WIDTH;
static float s_mapDepth = PHU_QUOC_MAP_DEPTH;
static float s_heightRange = PHU_QUOC_HEIGHT_RANGE;

#define MAP_WIDTH s_mapWidth
#define MAP_DEPTH s_mapDepth
#define CLIFF_DEPTH (PHU_QUOC_CLIFF_DEPTH * s_scaleY)
#define PHU_QUOC_SEA_Y -1.25f
#define SHADOW_FOCUS_RADIUS 22.0f
#define SHADOW_SNAP_STEP 0.25f
#define SHADOW_HYSTERESIS_XZ 0.50f
#define SHADOW_HYSTERESIS_Y 0.50f

static Vector3 kMapCenter = {PHU_QUOC_MAP_WIDTH * 0.5f, 0.0f, PHU_QUOC_MAP_DEPTH * 0.5f};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};
static Vector3 s_floraWindowCenter = {PHU_QUOC_ARENA_X, 0.0f, PHU_QUOC_ARENA_Z};

static MapDemIsland s_dem = {0};
static MapGroundSurface s_ground = {0};
static MapEcology s_habitat = {0};
static MapSkyDome s_sky = {0};
static MapCliffSkirt s_cliff = {0};
static MapWaterSurface s_coastalSea = {0};
static MapWaterSurface s_mineralWater = {0};
static MapRockSystem s_rocks = {0};
static MapOreSystem s_ores = {0};
static MapDebrisSystem s_debris = {0};
static MapFloraSystem s_flora = {0};

static WindMacroConfig s_prevWindMacro = {0};
static AtmosphereMode s_prevAtmoMode = ATMO_MODE_MOONLIGHT_DUST;
static Vector3 s_lastShadowFocus = {0};
static unsigned int s_skipMask = 0;
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

static MapZone s_phuQuocZones[3];
#define PHU_QUOC_ZONE_COUNT 3

static float PhuQuocMineralBed(float x, float z, void *ctx)
{
    (void)ctx;
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

static float PhuQuocMineralSurface(float x, float z, void *ctx)
{
    (void)ctx;
    float surfaceY = MapDem_SampleWaterHeight(&s_dem, x, z);
    return (surfaceY <= -s_dem.cliffDepth + 0.05f) ? -1e9f : surfaceY;
}

static float PhuQuocCoastalBed(float x, float z, void *user)
{
    (void)user;
    if (x < 0.0f || x > MAP_WIDTH || z < 0.0f || z > MAP_DEPTH)
        return -4.8f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static float PhuQuocCoastalSurface(float x, float z, void *user)
{
    (void)x;
    (void)z;
    (void)user;
    return PHU_QUOC_SEA_Y * s_scaleY;
}

static MapDemShadowCasterCtx s_phuQuocShadowCtx = {0};

static void DrawPhuQuocShadowCasters(Shader depthShader, void *user)
{
    (void)user;
    s_phuQuocShadowCtx.flora = &s_flora;
    s_phuQuocShadowCtx.rocks = &s_rocks;
    s_phuQuocShadowCtx.ores = s_ores.ready ? &s_ores : NULL;
    s_phuQuocShadowCtx.skipMask = s_skipMask;
    MapDem_DefaultShadowCasterCallback(depthShader, &s_phuQuocShadowCtx);
}

static void CapturePhuQuocStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
        return;
    if ((s_skipMask & MAP_SKIP_SHADOWCASTERS) || (s_skipMask & MAP_SKIP_ROCKS))
        return;
    float radius = (s_scaleXZ > 1.5f) ? 140.0f : 155.0f;
    Vector3 center = (s_scaleXZ > 1.5f) ? s_staticShadowCenter : kMapCenter;
    EnvShadow_BeginStaticCapture(center, radius);
    if (!EnvShadow_IsCapturing())
        return;
    Shader depthShader = EnvShadow_GetDepthShader();
    MapProp_DrawRockSystemShadowCasters(&s_rocks, depthShader);
    MapProp_DrawOreShadowCasters(&s_ores, depthShader);
    EnvShadow_EndStaticCapture();
}

static void SpawnPhuQuocMineralMist(void)
{
    FogVolume_ClearAll();
    static const LocalFogVolume kMistVolumes[] = {
        {.shape = FOG_SHAPE_CYLINDER, .position = {PHU_QUOC_LAKE_X, 0.55f, PHU_QUOC_LAKE_Z}, .extents = {18.0f, 0.90f, 16.0f}, .color = {224, 218, 198, 255}, .density = 0.14f, .edgeSoftness = 0.92f, .driftVelocity = {0.35f, 0.0f, 0.22f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {PHU_QUOC_TARN_X, 0.60f, PHU_QUOC_TARN_Z}, .extents = {14.0f, 1.00f, 14.0f}, .color = {216, 224, 236, 255}, .density = 0.14f, .edgeSoftness = 0.92f, .driftVelocity = {0.30f, 0.0f, 0.20f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {PHU_QUOC_PORTAL_X, 0.60f, PHU_QUOC_PORTAL_Z}, .extents = {13.0f, 0.85f, 13.0f}, .color = {232, 222, 192, 255}, .density = 0.11f, .edgeSoftness = 0.94f, .driftVelocity = {0.38f, 0.0f, 0.24f}, .active = true},
    };
    for (int i = 0; i < (int)(sizeof(kMistVolumes) / sizeof(kMistVolumes[0])); i++)
    {
        LocalFogVolume v = kMistVolumes[i];
        v.position.x *= s_scaleXZ;
        v.position.z *= s_scaleXZ;
        v.position.y = MapDem_GetGroundHeight(&s_dem, &s_ground, v.position.x, v.position.z) + 0.60f * s_scaleY;
        v.extents.x *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        v.extents.y *= s_scaleY;
        v.extents.z *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        FogVolume_Create(&v);
    }
}

static float PhuQuocMineralGrassDensity(float x, float z, void *user)
{
    (void)user;
    Vector3 groundPos, groundNormal;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &groundPos, &groundNormal))
        return 0.0f;
    if (groundNormal.y < 0.72f || groundPos.y < 0.15f)
        return 0.0f;

    float waterY = 0.0f, waterDepth = 0.0f;
    if (MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &waterY, &waterDepth))
        return 0.0f;

    float patch = sinf(x * 0.15f + z * 0.11f) * cosf(z * 0.18f - x * 0.13f);
    float pocket = (patch > 0.30f) ? ((patch - 0.30f) * 0.90f) : 0.0f;
    return fminf(0.72f, pocket);
}

static void BuildPhuQuocRocksAndOres(void)
{
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rocks/slate_diffuse.png",
        "assets/textures/rocks/slate_normal.png",
        "assets/textures/rocks/slate_roughness.png",
        PHU_QUOC_ROCK_COUNT + 16);

    for (int i = 0; i < PHU_QUOC_ROCK_COUNT; i++)
    {
        const MapRockItem *item = &PHU_QUOC_ROCK_ITEMS[i];
        Vector3 pos = {item->position.x * s_scaleXZ, item->position.y * s_scaleY, item->position.z * s_scaleXZ};
        float rScale = item->radiusScale * (1.0f + 0.35f * (s_scaleXZ - 1.0f));
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

    static MapOrePlacement s_scaledOres[PHU_QUOC_ORE_COUNT > 0 ? PHU_QUOC_ORE_COUNT : 1];
    for (int i = 0; i < PHU_QUOC_ORE_COUNT; i++)
    {
        s_scaledOres[i] = PHU_QUOC_ORE_ITEMS[i];
        s_scaledOres[i].position.x *= s_scaleXZ;
        s_scaledOres[i].position.z *= s_scaleXZ;
        s_scaledOres[i].radiusScale *= (1.0f + 0.30f * (s_scaleXZ - 1.0f));
        s_scaledOres[i].heightScale *= (1.0f + 0.22f * (s_scaleY - 1.0f));
    }
    s_ores = MapProp_CreateOreSystem(s_scaledOres, PHU_QUOC_ORE_COUNT, &s_ground, kMapCenter);
}

#define PHU_QUOC_MAX_GRASS 9500
#define PHU_QUOC_MAX_REEDS 140
#define PHU_QUOC_MAX_FLOWERS 380
static MapMeadowPlacement s_grassPlacements[PHU_QUOC_MAX_GRASS];
static MapMeadowPlacement s_reedPlacements[PHU_QUOC_MAX_REEDS];
static int s_grassCount = 0;
static int s_reedCount = 0;
static MapFlowerPlacement s_flowerPlacements[PHU_QUOC_MAX_FLOWERS];
static int s_flowerCount = 0;
static MapDebrisPlacement s_scaledDebris[PHU_QUOC_DEBRIS_COUNT > 0 ? PHU_QUOC_DEBRIS_COUNT : 1];

static void BuildPhuQuocDebris(void)
{
    int count = PHU_QUOC_DEBRIS_COUNT;
    if (count <= 0)
        return;
    for (int i = 0; i < count; i++)
    {
        s_scaledDebris[i] = PHU_QUOC_DEBRIS_ITEMS[i];
        s_scaledDebris[i].position.x *= s_scaleXZ;
        s_scaledDebris[i].position.z *= s_scaleXZ;
        Vector3 gp = {0}, gn = {0};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, s_scaledDebris[i].position.x, s_scaledDebris[i].position.z, &gp, &gn))
            s_scaledDebris[i].position.y = gp.y;
        else
            s_scaledDebris[i].position.y *= s_scaleY;
        s_scaledDebris[i].scale *= (1.0f + 0.18f * (s_scaleXZ - 1.0f));
    }
    s_debris = MapProp_CreateDebrisSystem(s_scaledDebris, count, &s_ground, kMapCenter, 38.0f * s_scaleXZ);
}

static float PhuQuocFlowerHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static bool PhuQuocFlowerExcluded(float x, float z, void *ctx)
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

static void BuildPhuQuocFlora(void)
{
    Vector2 minB = {14.0f * s_scaleXZ, 14.0f * s_scaleXZ};
    Vector2 maxB = {MAP_WIDTH - 14.0f * s_scaleXZ, MAP_DEPTH - 14.0f * s_scaleXZ};
#ifdef __ANDROID__
    const float grassSpacing = 1.35f;
    const int maxGrass = 2400;
#else
    float grassSpacing = 0.72f;
    const int maxGrass = PHU_QUOC_MAX_GRASS;
    if (s_scaleXZ > 1.5f)
    {
        float halfWin = 200.0f;
        minB.x = fmaxf(14.0f * s_scaleXZ, s_floraWindowCenter.x - halfWin);
        minB.y = fmaxf(14.0f * s_scaleXZ, s_floraWindowCenter.z - halfWin);
        maxB.x = fminf(MAP_WIDTH - 14.0f * s_scaleXZ, s_floraWindowCenter.x + halfWin);
        maxB.y = fminf(MAP_DEPTH - 14.0f * s_scaleXZ, s_floraWindowCenter.z + halfWin);
        grassSpacing = 1.30f;
    }
#endif
    MapMeadowDistribution dist = {
        .minBounds = minB,
        .maxBounds = maxB,
        .spacing = grassSpacing,
        .jitter = 0.28f,
        .minRadius = 0.16f,
        .maxRadius = 0.26f,
        .minHeight = 0.20f,
        .maxHeight = 0.38f,
        .yOffset = 0.0f,
        .seed = 0x7A4B91u,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, maxGrass,
        &s_ground, kMapCenter,
        dist, PhuQuocMineralGrassDensity, NULL);

    // Shoreline reeds + silver-white Cỏ Lau plumes around Kim Tinh Đàm placer lagoon
    s_reedCount = 0;
    float lakeCx = PHU_QUOC_LAKE_X * s_scaleXZ;
    float lakeCz = PHU_QUOC_LAKE_Z * s_scaleXZ;
    float scanR = 24.0f * s_scaleXZ;
    unsigned int rRng = 0x628194u;
    for (int a = 0; a < 440 && s_reedCount < PHU_QUOC_MAX_REEDS; a++)
    {
        rRng = rRng * 1664525u + 1013904223u;
        float u0 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        rRng = rRng * 1664525u + 1013904223u;
        float u1 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        float ang = u0 * (2.0f * PI);
        float rad = (5.5f + u1 * scanR);
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

    // 7 colonies of Kim Cúc (Golden Chrysanthemum) & Bạch Cúc (Silver-White Daisy)
    static MapFloraFlowerCluster s_flowerClusters[7];
    const int clusterCount = 7;
    const int flowersPerCluster = 52;
    const float arenaCx = PHU_QUOC_ARENA_X * s_scaleXZ;
    const float arenaCz = PHU_QUOC_ARENA_Z * s_scaleXZ;
    for (int i = 0; i < clusterCount; i++)
    {
        float ang = (float)i * (6.2831853f / (float)clusterCount) + 0.25f;
        float distR = (17.5f + (float)(i % 2) * 3.6f) * s_scaleXZ;
        float fx = arenaCx + cosf(ang) * distR;
        float fz = arenaCz + sinf(ang) * distR;
        float fy = MapDem_GetGroundHeight(&s_dem, &s_ground, fx, fz);
        bool isSilver = (i % 3 == 1);
        int sp = isSilver ? MAP_FLOWER_SPECIES_TUYET_LIEN : MAP_FLOWER_SPECIES_KIM_CUC;
        s_flowerClusters[i] = (MapFloraFlowerCluster){
            .center = {fx, fy, fz},
            .radius = {3.2f * s_scaleXZ, 2.8f * s_scaleXZ},
            .flowerCount = flowersPerCluster,
            .primarySpecies = sp,
            .species = {(unsigned char)sp, (unsigned char)sp, (unsigned char)sp, (unsigned char)sp},
            .primaryColor = isSilver ? (Color){236, 242, 250, 255} : (Color){248, 206, 72, 255},
            .accentColor = isSilver ? (Color){206, 218, 236, 255} : (Color){255, 232, 134, 255},
            .discalCenterColor = {245, 190, 62, 255},
        };
    }
    s_flowerCount = clusterCount * flowersPerCluster;
    MapFlora_GenerateFlowerPlacements(s_flowerPlacements, s_flowerCount, s_flowerClusters, clusterCount,
                                      PhuQuocFlowerHeight, PhuQuocFlowerExcluded, NULL, 0xC49F12u);

    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        s_flowerPlacements, s_flowerClusters, clusterCount,
        (MapFloraStyle){
            .grassRootColor = {72, 66, 46, 255},
            .grassTipColor = {216, 194, 128, 255},
            .bladesPerClump = 5,
            .bladeSegments = 3,
            .bladeWidthScale = 0.125f,
            .lodDistance = 24.0f,
            .midLodDistance = 16.0f,
            .drawDistance = 44.0f,
            .shadowDistance = (s_scaleXZ > 1.5f) ? -1.0f : 22.0f,
            .grassState = MAP_GRASS_STATE_DRY,
            .dryness = 0.72f,
            .scorch = 0.0f,
            .reedRootColor = {68, 78, 56, 255},
            .reedTipColor = {218, 224, 214, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 0.85f,
            .enableCoLauPlumes = true,
            .coLauOnReeds = true,
            .coLauPlumeDensity = 0.85f,
            .coLauPlumeTint = {244, 238, 220, 255},
        });
}

void InitPhuQuocMap(void)
{
    s_scaleXZ = MapManager_GetWorldScaleXZ();
    s_scaleY = MapManager_GetWorldScaleY();
    s_mapWidth = PHU_QUOC_MAP_WIDTH * s_scaleXZ;
    s_mapDepth = PHU_QUOC_MAP_DEPTH * s_scaleXZ;
    s_heightRange = PHU_QUOC_HEIGHT_RANGE * s_scaleY;
    kMapCenter = (Vector3){s_mapWidth * 0.5f, 0.0f, s_mapDepth * 0.5f};

    MapManager_SetActiveMetadata(&(MapSpatialMetadata){
        .spawnPoint = {101.0f * s_scaleXZ, 0.0f, 144.0f * s_scaleXZ},
        .arenaCenter = {101.0f * s_scaleXZ, 0.0f, 144.0f * s_scaleXZ},
        .arenaRadius = 140.0f * s_scaleXZ,
        .hasPlayableRect = true,
        .minX = 8.0f * s_scaleXZ, .maxX = 221.0f * s_scaleXZ,
        .minZ = 8.0f * s_scaleXZ, .maxZ = 292.0f * s_scaleXZ,
    });
    s_staticShadowCenter = kMapCenter;
    s_floraWindowCenter = (Vector3){PHU_QUOC_ARENA_X * s_scaleXZ, 0.0f, PHU_QUOC_ARENA_Z * s_scaleXZ};

    s_phuQuocZones[0] = (MapZone){.center = {PHU_QUOC_ARENA_X * s_scaleXZ, 0.0f, PHU_QUOC_ARENA_Z * s_scaleXZ}, .radius = 28.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};
    s_phuQuocZones[1] = (MapZone){.center = {PHU_QUOC_LAKE_X * s_scaleXZ, 0.0f, PHU_QUOC_LAKE_Z * s_scaleXZ}, .radius = 26.0f * s_scaleXZ, .type = NAT_RIVER};
    s_phuQuocZones[2] = (MapZone){.center = {PHU_QUOC_PORTAL_X * s_scaleXZ, 0.0f, PHU_QUOC_PORTAL_Z * s_scaleXZ}, .radius = 24.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};

    s_prevWindMacro = Wind_GetMacro();
    WindMacroConfig coastalWind = {
        .baseDirection = {3.20f, 0.0f, -1.85f},
        .gustAmplitude = 1.60f,
        .noiseScale = 0.064f,
        .noiseSpeed = 1.85f,
        .terrainLiftK = 1.45f,
        .heightGradientK = 0.85f,
    };
    Wind_SetMacro(&coastalWind);

    s_skipMask = MapProp_GetSkipMask();
    if (s_skipMask & MAP_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else if (!ColorGradeLut_Load("assets/maps/phu_quoc/phu_quoc_grade.png"))
        ColorGradeLut_UseNeutral();

    // 5 Metal-Element Time-of-Day Presets (Default: 16:00 Golden-Silver Metallic Afternoon)
    static const EnvLightingPreset kPhuQuocToD[5] = {
        {
            .ambientColor = {84, 96, 118, 255},
            .sunColor = {255, 214, 158, 255},
            .sunDirection = {0.78f, -0.30f, -0.55f},
            .shadowColor = {38, 46, 68, 165},
            .fog = {.enabled = true, .color = {182, 192, 208, 255}, .start = 34.0f, .end = 225.0f, .density = 0.0044f},
            .atmosphere = {
                .color = {182, 192, 208, 255}, .start = 34.0f, .end = 225.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0092f, 0.0135f, 0.0235f}, .mieScattering = 0.0048f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.0044f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {98, 112, 134, 255},
            .sunColor = {255, 242, 216, 255},
            .sunDirection = {0.32f, -0.88f, -0.35f},
            .shadowColor = {44, 54, 78, 160},
            .fog = {.enabled = true, .color = {196, 206, 220, 255}, .start = 40.0f, .end = 245.0f, .density = 0.0036f},
            .atmosphere = {
                .color = {196, 206, 220, 255}, .start = 40.0f, .end = 245.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0082f, 0.0128f, 0.0225f}, .mieScattering = 0.0038f, .mieAnisotropy = 0.76f, .multipleScatteringAmp = 1.9f},
                .density = {.baseDensity = 0.0036f, .heightFalloff = 0.22f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {88, 98, 120, 255},
            .sunColor = {255, 216, 152, 255},
            .sunDirection = {0.72f, -0.38f, -0.58f},
            .shadowColor = {38, 44, 68, 170},
            .fog = {.enabled = true, .color = {194, 198, 210, 255}, .start = 32.0f, .end = 220.0f, .density = 0.0042f},
            .atmosphere = {
                .color = {194, 198, 210, 255}, .start = 32.0f, .end = 220.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0094f, 0.0138f, 0.0235f}, .mieScattering = 0.0052f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.1f},
                .density = {.baseDensity = 0.0042f, .heightFalloff = 0.26f, .baseAltitude = -2.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {72, 80, 106, 255},
            .sunColor = {255, 172, 114, 255},
            .sunDirection = {-0.82f, -0.24f, -0.52f},
            .shadowColor = {32, 38, 62, 175},
            .fog = {.enabled = true, .color = {178, 172, 184, 255}, .start = 28.0f, .end = 205.0f, .density = 0.0052f},
            .atmosphere = {
                .color = {178, 172, 184, 255}, .start = 28.0f, .end = 205.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0108f, 0.0148f, 0.0250f}, .mieScattering = 0.0058f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 2.2f},
                .density = {.baseDensity = 0.0052f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {26, 36, 62, 255},
            .sunColor = {206, 220, 246, 255},
            .sunDirection = {0.50f, -0.46f, -0.73f},
            .shadowColor = {10, 14, 28, 210},
            .fog = {.enabled = true, .color = {24, 32, 52, 255}, .start = 26.0f, .end = 200.0f, .density = 0.0058f},
            .atmosphere = {
                .color = {24, 32, 52, 255}, .start = 26.0f, .end = 200.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0052f, 0.0080f, 0.0185f}, .mieScattering = 0.0044f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 1.85f},
                .density = {.baseDensity = 0.0050f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        }
    };
    static const float kPhuQuocTimes[5] = {0.26f, 0.50f, 0.66f, 0.77f, 0.92f};
    Environment_SetTimeOfDayPresets(kPhuQuocToD, kPhuQuocTimes, 5);
    Environment_SetTimeOfDay(0.66f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){88, 98, 120, 255});
    Environment_SetSunColor((Color){255, 216, 152, 255});
    Environment_SetSunIntensity(1.85f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.72f, -0.38f, -0.58f}));
    Environment_SetShadowColor((Color){38, 44, 68, 170});
    if (Environment_IsNightMode())
        Environment_ApplyMoonlightNight();

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.02f,
        .contrast = 1.12f,
        .saturation = 1.06f,
        .bloomIntensity = 0.25f,
        .shadowTint = {0.86f, 0.90f, 1.06f},
        .highlightTint = {1.08f, 1.00f, 0.88f},
        .bloomThreshold = 1.08f,
        .bloomKnee = 0.44f,
        .hueRestore = 0.62f,
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);

    AtmosphereProfile atmos = kPhuQuocToD[2].atmosphere;
    atmos.enabled = false;
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(1.05f);
    VolumetricFog_SetDistantCoverage(0.55f);
    s_prevAtmoMode = Atmosphere_GetMode();
    Atmosphere_SetMode(ATMO_MODE_SPIRIT_SPARKS);
    Atmosphere_Configure(s_floraWindowCenter, (Vector3){32.0f, 4.5f, 32.0f}, 360, (Color){255, 224, 152, 255});
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.24f,
        .worldSize = 160.0f,
        .planeHeight = 88.0f,
        .coverage = 0.36f,
        .softness = 0.34f,
        .windSpeedScale = 0.85f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);

    // Activate Metal-Element Biome Shaders
    MapGroundBiomeProfile metallicBiome = {
        .aridFactor = 0.0f,
        .strataStrength = 0.88f,
        .grusColor = {0.58f, 0.54f, 0.46f},
        .hardpanColor = {0.36f, 0.34f, 0.32f},
        .lichenColor = {0.64f, 0.56f, 0.28f},
        .volcanicFactor = 0.0f,
        .magmaGlowStrength = 0.0f,
        .ashColor = {0.16f, 0.15f, 0.16f},
        .scoriaColor = {0.34f, 0.14f, 0.10f},
        .sulfurColor = {0.62f, 0.50f, 0.16f},
        .alpineFactor = 0.0f,
        .snowSparkleStrength = 0.0f,
        .snowColor = {0.88f, 0.92f, 0.97f},
        .permafrostColor = {0.22f, 0.26f, 0.32f},
        .glacialIceColor = {0.36f, 0.72f, 0.86f},
        .metallicFactor = 1.0f,
        .oreVeinStrength = 1.35f,
        .goldVeinColor = {1.00f, 0.78f, 0.24f},
        .silverVeinColor = {0.92f, 0.95f, 0.99f},
        .ironSlateColor = {0.22f, 0.24f, 0.28f},
    };
    MapProp_SetGroundBiomeProfile(&metallicBiome);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetRockVolcanicMode(0.0f);
    MapProp_SetRockAlpineMode(0.0f);
    MapProp_SetRockMetallicMode(1.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(0.0f);
    MapProp_SetCliffAlpineMode(0.0f);
    MapProp_SetCliffMetallicMode(1.0f);
    MapProp_SetCliffSeaLevel(PHU_QUOC_SEA_Y * s_scaleY);
    MapProp_SetGrassState(MAP_GRASS_STATE_DRY, 0.0f);
    {
        MapGrassCondition mineralGrass = MapProp_GetGrassCondition();
        mineralGrass.dryness = 0.72f;
        MapProp_SetGrassCondition(mineralGrass);
    }

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawPhuQuocShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        CapturePhuQuocStaticShadows();
        if (!(s_skipMask & MAP_SKIP_FOGVOLUMES))
            SpawnPhuQuocMineralMist();
        MapManager_SetZones(s_phuQuocZones, PHU_QUOC_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = camera.target;
    EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/phu_quoc/phu_quoc_ecology_splat.png",
                      "assets/maps/phu_quoc/phu_quoc_water_height.png");

    const char *hfPath = (s_scaleXZ > 1.5f && FileExists("assets/maps/phu_quoc/phu_quoc_heightfield_ow.png"))
                             ? "assets/maps/phu_quoc/phu_quoc_heightfield_ow.png"
                             : "assets/maps/phu_quoc/phu_quoc_heightfield.png";
    s_ground = MapProp_CreateGroundHeightfield16(
        hfPath, MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 36.0f,
        "assets/maps/phu_quoc/phu_quoc_ecology_splat.png",
        "assets/maps/phu_quoc/textures/auriferous_quartz_diffuse.png",
        "assets/maps/phu_quoc/textures/argentite_slate_diffuse.png");

    MapEcology_LoadBakedAssets(&s_habitat,
                               "assets/maps/phu_quoc/phu_quoc_habitat.png",
                               "assets/maps/phu_quoc/phu_quoc_habitat_distance.png",
                               (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH},
                               NULL, 0, NULL, NULL);
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    MapProp_SetGroundTint(&s_ground, (Color){206, 202, 194, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/maps/phu_quoc/textures/auriferous_quartz_material.png",
                                 "assets/maps/phu_quoc/textures/argentite_slate_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/maps/phu_quoc/phu_quoc_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/phu_quoc/phu_quoc_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rocks/slate_diffuse.png",
                              "assets/textures/rocks/slate_normal.png");

    s_sky = MapProp_CreateSkyDome();
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    const float seaY = PHU_QUOC_SEA_Y * s_scaleY;
    const float seaSpan = (s_scaleXZ > 1.5f) ? (620.0f * s_scaleXZ) : 840.0f;
    const int seaSegs = (s_scaleXZ > 1.5f) ? 84 : 72;
    s_coastalSea = MapProp_CreateWaterPatch(
        (Vector3){kMapCenter.x, seaY, kMapCenter.z},
        seaSpan, seaSpan,
        (MapWaterConfig){
            .ecosystem = WATER_ECO_CUSTOM,
            .segments = seaSegs,
            .rings = seaSegs,
            .waveHeight = 0.26f,
            .waveScale = 0.45f,
            .waveSpeed = 1.15f,
            .flowVelocity = {0.20f, 0.14f},
            .detailScale = 0.24f,
            .detailStrength = 0.40f,
            .maxDepth = 4.2f,
            .absorption = {0.58f, 0.16f, 0.08f},
            .scatterColor = {0.06f, 0.48f, 0.50f},
            .scatterCoeff = 0.46f,
            .causticsStrength = 0.40f,
            .causticsScale = 1.45f,
            .foamThreshold = 0.28f,
            .deepColor = {6, 40, 68, 255},
            .shallowColor = {24, 148, 152, 235},
            .foamColor = {248, 246, 240, 235},
        });
    if (s_coastalSea.ready)
    {
        const int res = 512;
        const float patchSpan = seaSpan;
        const float shoreMaxDist = 24.0f * (seaSpan / 840.0f);
        const float cell = patchSpan / (float)(res - 1);
        const float diag = cell * 1.41421356f;
        float *distGrid = (float *)malloc((size_t)(res * res) * sizeof(float));
        unsigned char *rgba = (unsigned char *)malloc((size_t)(res * res * 4));
        if (distGrid && rgba)
        {
            float minX = kMapCenter.x - patchSpan * 0.5f;
            float minZ = kMapCenter.z - patchSpan * 0.5f;
            for (int j = 0; j < res; j++)
            {
                float wz = minZ + (float)j * cell;
                for (int i = 0; i < res; i++)
                {
                    float wx = minX + (float)i * cell;
                    bool isLand = false;
                    if (wx >= 0.0f && wx <= MAP_WIDTH && wz >= 0.0f && wz <= MAP_DEPTH)
                    {
                        Vector3 gp = {0}, gn = {0};
                        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, wx, wz, &gp, &gn) &&
                            gp.y > seaY + 0.05f)
                        {
                            isLand = true;
                        }
                    }
                    distGrid[j * res + i] = isLand ? 0.0f : 1000.0f;
                }
            }
            for (int j = 1; j < res - 1; j++)
            {
                for (int i = 1; i < res - 1; i++)
                {
                    int idx = j * res + i;
                    float d = distGrid[idx];
                    float dL = distGrid[idx - 1] + cell;
                    float dU = distGrid[idx - res] + cell;
                    float dUL = distGrid[idx - res - 1] + diag;
                    float dUR = distGrid[idx - res + 1] + diag;
                    if (dL < d) d = dL;
                    if (dU < d) d = dU;
                    if (dUL < d) d = dUL;
                    if (dUR < d) d = dUR;
                    distGrid[idx] = d;
                }
            }
            for (int j = res - 2; j >= 1; j--)
            {
                for (int i = res - 2; i >= 1; i--)
                {
                    int idx = j * res + i;
                    float d = distGrid[idx];
                    float dR = distGrid[idx + 1] + cell;
                    float dD = distGrid[idx + res] + cell;
                    float dDL = distGrid[idx + res - 1] + diag;
                    float dDR = distGrid[idx + res + 1] + diag;
                    if (dR < d) d = dR;
                    if (dD < d) d = dD;
                    if (dDL < d) d = dDL;
                    if (dDR < d) d = dDR;
                    distGrid[idx] = d;
                }
            }
            for (int j = 0; j < res; j++)
            {
                int j0 = (j > 0) ? j - 1 : 0;
                int j1 = (j < res - 1) ? j + 1 : res - 1;
                for (int i = 0; i < res; i++)
                {
                    int i0 = (i > 0) ? i - 1 : 0;
                    int i1 = (i < res - 1) ? i + 1 : res - 1;
                    int idx = j * res + i;
                    float gx = distGrid[j * res + i0] - distGrid[j * res + i1];
                    float gz = distGrid[j0 * res + i] - distGrid[j1 * res + i];
                    float len = sqrtf(gx * gx + gz * gz);
                    float nx = (len > 1e-4f) ? (gx / len) : 0.0f;
                    float nz = (len > 1e-4f) ? (gz / len) : 0.0f;
                    float normDist = distGrid[idx] / shoreMaxDist;
                    if (normDist > 1.0f) normDist = 1.0f;
                    rgba[idx * 4 + 0] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
                    rgba[idx * 4 + 1] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
                    rgba[idx * 4 + 2] = (unsigned char)(normDist * 255.0f + 0.5f);
                    rgba[idx * 4 + 3] = 255;
                }
            }
            Image shoreImg = {
                .data = rgba,
                .width = res,
                .height = res,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
            };
            s_coastalSea.flowFieldTex = LoadTextureFromImage(shoreImg);
            if (s_coastalSea.flowFieldTex.id > 0)
            {
                SetTextureFilter(s_coastalSea.flowFieldTex, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(s_coastalSea.flowFieldTex, TEXTURE_WRAP_CLAMP);
                s_coastalSea.hydrologyReady = true;
            }
        }
        free(distGrid);
        free(rgba);
    }
    MapProp_SetWaterTerrainSource(&s_coastalSea, PhuQuocCoastalBed, PhuQuocCoastalSurface, NULL);

    // Interior Placer-Gold Streams & Quarry Tarns (Suối Đá Bàn / Kim Tinh Đàm / Bạch Ngân Hồ)
    s_mineralWater = MapProp_CreateTerrainWater(
        "assets/maps/phu_quoc/phu_quoc_water.obj",
        (MapWaterConfig){
            .ecosystem = WATER_ECO_ALPINE_STREAM,
            .center = {PHU_QUOC_LAKE_X * s_scaleXZ, 0.0f, PHU_QUOC_LAKE_Z * s_scaleXZ},
            .radiusX = PHU_QUOC_LAKE_RX * s_scaleXZ,
            .radiusZ = PHU_QUOC_LAKE_RZ * s_scaleXZ,
            .waveHeight = 0.012f,
            .waveScale = 1.25f,
            .waveSpeed = 0.65f,
            .flowVelocity = {0.18f, 0.14f},
            .detailScale = 0.22f,
            .detailStrength = 0.16f,
            .maxDepth = 1.80f,
            .absorption = {0.42f, 0.22f, 0.10f},
            .scatterColor = {0.14f, 0.48f, 0.44f},
            .scatterCoeff = 0.36f,
            .causticsStrength = 0.46f,
            .causticsScale = 2.00f,
            .foamThreshold = 0.14f,
            .deepColor = {12, 38, 46, 255},
            .shallowColor = {48, 138, 132, 235},
            .foamColor = {244, 242, 232, 240},
        });
    if (s_mineralWater.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_mineralWater.waterModel.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_mineralWater.boundsCenter = (Vector3){
            s_mineralWater.boundsCenter.x * s_scaleXZ,
            s_mineralWater.boundsCenter.y * s_scaleY,
            s_mineralWater.boundsCenter.z * s_scaleXZ};
        s_mineralWater.boundsRadius *= s_scaleXZ;
    }
    MapProp_SetWaterTerrainSource(&s_mineralWater, PhuQuocMineralBed, PhuQuocMineralSurface, NULL);

    BuildPhuQuocRocksAndOres();
    BuildPhuQuocDebris();
    BuildPhuQuocFlora();

    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnPhuQuocMineralMist();

    s_ready = true;
    EnvShadow_SetMapCasterCallback(DrawPhuQuocShadowCasters, NULL);
    EnvShadow_InvalidateStaticCache();
    if (s_scaleXZ > 1.5f)
    {
        s_staticShadowCenter = s_floraWindowCenter;
        EnvShadow_InvalidateStaticCache();
    }
    CapturePhuQuocStaticShadows();
    MapManager_SetZones(s_phuQuocZones, PHU_QUOC_ZONE_COUNT);
    extern void ResolveCollisionPhuQuocMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("PHU_QUOC", ResolveCollisionPhuQuocMap);
}

void DrawPhuQuocMap(void)
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
    if (s_ores.ready)
    {
        float t = (float)fmod(GetTime(), 3600.0);
        MapProp_DrawOreSystem(&s_ores, t);
    }
    if (!(s_skipMask & MAP_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        MapProp_DrawCliffSkirt(&s_cliff, camera);
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
}

void DrawTransparentPhuQuocMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_WATER))
    {
        float t = (float)fmod(GetTime(), 3600.0);
        MapProp_DrawWaterOverlay(&s_coastalSea, t);
        MapProp_DrawWaterOverlay(&s_mineralWater, t);
    }
}

void UpdatePhuQuocMap(float dt)
{
    if (!s_ready)
        return;

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_coastalSea, dt);
    MapProp_UpdateWaterSurface(&s_mineralWater, dt);

    MapDem_UpdateShadowFocusAndStaticCache(
        camera.target, &s_lastShadowFocus, &s_staticShadowCenter,
        SHADOW_FOCUS_RADIUS, SHADOW_SNAP_STEP, SHADOW_HYSTERESIS_XZ, SHADOW_HYSTERESIS_Y,
        s_scaleXZ, kMapCenter, (s_scaleXZ > 1.5f) ? 140.0f : 155.0f, &s_rocks, s_skipMask);
}

void UnloadPhuQuocMap(void)
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
    MapProp_SetRockMetallicMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(0.0f);
    MapProp_SetCliffAlpineMode(0.0f);
    MapProp_SetCliffMetallicMode(0.0f);

    if (!s_ready)
        return;

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_mineralWater);
    MapProp_UnloadWaterSurface(&s_coastalSea);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
    MapProp_UnloadOreSystem(&s_ores);
    MapProp_UnloadRockSystem(&s_rocks);
    if (s_cliff.ready)
        MapProp_UnloadCliffSkirt(&s_cliff);
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

float GetGroundHeightPhuQuocMap(float x, float z)
{
    if (!s_ready)
        return 0.0f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

bool SampleGroundSurfacePhuQuocMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (!s_ready)
        return false;
    return MapDem_SampleGroundSurface(&s_dem, &s_ground, x, z, outPosition, outNormal);
}

bool GetWaterInfoPhuQuocMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready)
        return false;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, outSurfaceY, outWaterDepth);
}

void SetWaterInteractorPhuQuocMap(Vector3 position, Vector3 velocity, float radius)
{
    if (!s_ready)
        return;
    MapDem_SetWaterInteractor(&s_dem, &s_ground, &s_mineralWater, position, velocity, radius);
}

void AddWaterRipplePhuQuocMap(Vector3 position, float radius, float intensity)
{
    if (!s_ready)
        return;
    MapDem_AddWaterRipple(&s_mineralWater, position, radius, intensity);
}

void ResolveCollisionPhuQuocMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY)
{
    if (!s_ready)
        return;
    MapProp_ResolveRockSystemCollision(&s_rocks, pos, agentRadius, stepHeight, inOutGroundY);
}

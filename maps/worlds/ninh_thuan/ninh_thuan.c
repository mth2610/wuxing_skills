#include "maps/worlds/ninh_thuan/ninh_thuan.h"
#include "assets/maps/ninh_thuan/ninh_thuan_generated_meta.h"
#include "maps/worlds/ninh_thuan/ninh_thuan_rocks_generated.h"
#include "core/gfx_quality.h"
#include "core/granular/granular_sim.h"
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
static float s_mapWidth = NINH_THUAN_MAP_WIDTH;
static float s_mapDepth = NINH_THUAN_MAP_DEPTH;
static float s_heightRange = NINH_THUAN_HEIGHT_RANGE;

#define MAP_WIDTH s_mapWidth
#define MAP_DEPTH s_mapDepth
#define CLIFF_DEPTH (NINH_THUAN_CLIFF_DEPTH * s_scaleY)
#define CLOUD_SEA_Y (-5.2f * s_scaleY)
#define SHADOW_FOCUS_RADIUS 22.0f
#define SHADOW_SNAP_STEP 0.25f
#define SHADOW_HYSTERESIS_XZ 0.50f
#define SHADOW_HYSTERESIS_Y 0.50f

static Vector3 kMapCenter = {NINH_THUAN_MAP_WIDTH * 0.5f, 0.0f, NINH_THUAN_MAP_DEPTH * 0.5f};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};
static Vector3 s_floraWindowCenter = {160.0f, 0.0f, 222.0f};

static MapDemIsland s_dem = {0};
static MapGroundSurface s_ground = {0};
static MapEcology s_habitat = {0};
static MapSkyDome s_sky = {0};
static MapWaterSurface s_coastalSea = {0};
static MapCliffSkirt s_cliff = {0};
static MapWaterSurface s_oasis = {0};
static MapRockSystem s_rocks = {0};
static MapFloraSystem s_flora = {0};

#define NINH_THUAN_SEA_Y -1.25f

static WindMacroConfig s_prevWindMacro = {0};
static AtmosphereMode s_prevAtmoMode = ATMO_MODE_MOONLIGHT_DUST;
static Vector3 s_lastShadowFocus = {0};
static unsigned int s_skipMask = 0;
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

static MapZone s_ninhThuanZones[4];
#define NINH_THUAN_ZONE_COUNT 4

static float NinhThuanOasisBed(float x, float z, void *user)
{
    (void)user;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static float NinhThuanOasisSurface(float x, float z, void *user)
{
    (void)user;
    float surfaceY = NINH_THUAN_LAKE_WATER_Y, depth = 0.0f;
    if (MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &surfaceY, &depth))
        return surfaceY;
    return NINH_THUAN_LAKE_WATER_Y;
}

static float NinhThuanCoastalBed(float x, float z, void *user)
{
    (void)user;
    if (x < 0.0f || x > MAP_WIDTH || z < 0.0f || z > MAP_DEPTH)
        return -4.8f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static float NinhThuanCoastalSurface(float x, float z, void *user)
{
    (void)x;
    (void)z;
    (void)user;
    return NINH_THUAN_SEA_Y * s_scaleY;
}

static MapDemShadowCasterCtx s_ninhThuanShadowCtx = {0};

static void DrawNinhThuanShadowCasters(Shader depthShader, void *user)
{
    (void)user;
    s_ninhThuanShadowCtx.flora = &s_flora;
    s_ninhThuanShadowCtx.rocks = &s_rocks;
    s_ninhThuanShadowCtx.ores = NULL;
    s_ninhThuanShadowCtx.skipMask = s_skipMask;
    MapDem_DefaultShadowCasterCallback(depthShader, &s_ninhThuanShadowCtx);
}

static void CaptureNinhThuanStaticShadows(void)
{
    float radius = (s_scaleXZ > 1.5f) ? 140.0f : 155.0f;
    Vector3 center = (s_scaleXZ > 1.5f) ? s_staticShadowCenter : kMapCenter;
    MapDem_CaptureStaticShadows(&s_rocks, center, radius, s_skipMask);
}

static void SpawnNinhThuanDustHaze(void)
{
    FogVolume_ClearAll();
    static const LocalFogVolume kDustVolumes[] = {
        {.shape = FOG_SHAPE_CYLINDER, .position = {94.1f, 0.45f, 150.0f}, .extents = {18.0f, 0.70f, 16.0f}, .color = {242, 212, 172, 255}, .density = 0.14f, .edgeSoftness = 0.92f, .driftVelocity = {0.85f, 0.0f, 0.48f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {NINH_THUAN_LAKE_CENTER_X, 0.45f, NINH_THUAN_LAKE_CENTER_Z}, .extents = {16.0f, 0.55f, 14.0f}, .color = {236, 228, 198, 255}, .density = 0.14f, .edgeSoftness = 0.92f, .driftVelocity = {0.60f, 0.0f, 0.35f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {135.0f, 0.45f, 195.0f}, .extents = {16.0f, 0.65f, 16.0f}, .color = {240, 210, 168, 255}, .density = 0.12f, .edgeSoftness = 0.92f, .driftVelocity = {0.75f, 0.0f, 0.40f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {65.0f, 0.55f, 95.0f}, .extents = {18.0f, 0.75f, 18.0f}, .color = {236, 196, 154, 255}, .density = 0.15f, .edgeSoftness = 0.92f, .driftVelocity = {0.90f, 0.0f, 0.50f}, .active = true},
    };
    for (int i = 0; i < (int)(sizeof(kDustVolumes) / sizeof(kDustVolumes[0])); i++)
    {
        LocalFogVolume v = kDustVolumes[i];
        v.position.x *= s_scaleXZ;
        v.position.z *= s_scaleXZ;
        v.position.y = MapDem_GetGroundHeight(&s_dem, &s_ground, v.position.x, v.position.z) + 0.45f * s_scaleY;
        v.extents.x *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        v.extents.y *= s_scaleY;
        v.extents.z *= (1.0f + 0.65f * (s_scaleXZ - 1.0f));
        FogVolume_Create(&v);
    }
}

static float NinhThuanDryGrassDensity(float x, float z, void *user)
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

    // Sparse xerophytic grass tufts: clustered in sheltered rock-lee pockets & oasis fringe, leaving open dunes bare
    float oasisDist = hypotf(x - NINH_THUAN_LAKE_CENTER_X * s_scaleXZ, z - NINH_THUAN_LAKE_CENTER_Z * s_scaleXZ);
    float oasisFringe = (oasisDist < 24.0f * s_scaleXZ) ? (1.0f - fmaxf(0.0f, (oasisDist - 12.0f * s_scaleXZ) / (12.0f * s_scaleXZ))) * 0.65f : 0.0f;

    float rockShelter = 0.0f;
    for (int i = 0; i < NINH_THUAN_ROCK_COUNT; i++)
    {
        const MapRockItem *rk = &NINH_THUAN_ROCK_ITEMS[i];
        if (rk->archetype == MAP_ROCK_SCREE)
            continue;
        float rkx = rk->position.x * s_scaleXZ;
        float rkz = rk->position.z * s_scaleXZ;
        float rScale = rk->radiusScale * (1.0f + 0.35f * (s_scaleXZ - 1.0f));
        float dx = x - rkx;
        float dz = z - rkz;
        float d2 = dx * dx + dz * dz;
        float maxR = rScale + 3.2f;
        if (d2 < maxR * maxR)
        {
            float dist = sqrtf(d2) - rScale * 0.90f;
            if (dist < 0.05f)
                return 0.0f; // never place inside boulder footprint
            float w = fmaxf(0.0f, 1.0f - dist / 2.8f);
            if (w > rockShelter)
                rockShelter = w;
        }
    }

    float patch = sinf(x * 0.14f + z * 0.10f) * cosf(z * 0.17f - x * 0.12f);
    float pocket = (patch > 0.48f) ? ((patch - 0.48f) * 0.85f) : 0.0f;
    return fminf(0.75f, rockShelter * 0.65f + oasisFringe + pocket);
}

static void BuildNinhThuanRocks(void)
{
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rocks/granite_diffuse.png",
        "assets/textures/rocks/granite_normal.png",
        "assets/textures/rocks/granite_roughness.png",
        NINH_THUAN_ROCK_COUNT + 32);

    float lastAnchorBaseY = 0.0f;
    float lastAnchorHScale = 1.0f;
    for (int i = 0; i < NINH_THUAN_ROCK_COUNT; i++)
    {
        const MapRockItem *item = &NINH_THUAN_ROCK_ITEMS[i];
        Vector3 pos = { item->position.x * s_scaleXZ, item->position.y * s_scaleY, item->position.z * s_scaleXZ };
        float rScale = item->radiusScale * (1.0f + 0.52f * (s_scaleXZ - 1.0f));
        float hScale = item->heightScale * (1.0f + 0.25f * (s_scaleY - 1.0f));
        Vector3 gPos = pos, gNorm = {0.0f, 1.0f, 0.0f};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &gPos, &gNorm))
        {
            // Stacked tors/shoulders sit directly relative to their base anchor's runtime top
            // instead of scaling raw item->position.y by s_scaleY (prevents OW floating capstones).
            if (i > 0 && item->position.y > NINH_THUAN_ROCK_ITEMS[i - 1].position.y + 0.25f &&
                hypotf(item->position.x - NINH_THUAN_ROCK_ITEMS[i - 1].position.x,
                       item->position.z - NINH_THUAN_ROCK_ITEMS[i - 1].position.z) < 4.5f)
            {
                pos.y = lastAnchorBaseY + lastAnchorHScale * 0.42f - hScale * 0.14f;
            }
            else
            {
                float sink = (item->archetype == MAP_ROCK_SCREE) ? hScale * 0.18f : hScale * 0.22f;
                pos.y = gPos.y - sink;
                lastAnchorBaseY = pos.y;
                lastAnchorHScale = hScale;
            }
        }
        MapProp_AddRockItem(&s_rocks, pos, rScale, hScale, item->rotationDeg, item->archetype);
    }
}

#define NINH_THUAN_MAX_GRASS 42000
#define NINH_THUAN_MAX_REEDS 320
static MapMeadowPlacement s_grassPlacements[NINH_THUAN_MAX_GRASS];
static MapMeadowPlacement s_reedPlacements[NINH_THUAN_MAX_REEDS];
static int s_grassCount = 0;
static int s_reedCount = 0;

static void BuildNinhThuanFlora(void)
{
    Vector2 minB = {12.0f * s_scaleXZ, 12.0f * s_scaleXZ};
    Vector2 maxB = {MAP_WIDTH - 12.0f * s_scaleXZ, MAP_DEPTH - 12.0f * s_scaleXZ};
#ifdef __ANDROID__
    const float grassSpacing = 1.25f;
    const int   maxGrass     = 3200;
#else
    float grassSpacing = 0.62f;
    const int maxGrass = NINH_THUAN_MAX_GRASS;
    if (s_scaleXZ > 1.5f)
    {
        // Cover the entire active Open World desert play area once at init
        // (zero mid-gameplay flora unload/rebuild stalls).
        float halfWin = 220.0f;
        minB.x = fmaxf(12.0f * s_scaleXZ, s_floraWindowCenter.x - halfWin);
        minB.y = fmaxf(12.0f * s_scaleXZ, s_floraWindowCenter.z - halfWin);
        maxB.x = fminf(MAP_WIDTH - 12.0f * s_scaleXZ, s_floraWindowCenter.x + halfWin);
        maxB.y = fminf(MAP_DEPTH - 12.0f * s_scaleXZ, s_floraWindowCenter.z + halfWin);
        grassSpacing = 0.65f;
    }
#endif
    MapMeadowDistribution dist = {
        .minBounds = minB,
        .maxBounds = maxB,
        .spacing = grassSpacing,
        .jitter = 0.28f,
        .minRadius = 0.17f,
        .maxRadius = 0.28f,
        .minHeight = 0.24f,
        .maxHeight = 0.44f,
        .yOffset = 0.0f,
        .seed = 90421u,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, maxGrass,
        &s_ground, kMapCenter,
        dist, NinhThuanDryGrassDensity, NULL);

    // Organic oasis shoreline reeds + 3D Cỏ Lau plumes hugging the true oasis water margin
    s_reedCount = 0;
    float oasisCx = NINH_THUAN_LAKE_CENTER_X * s_scaleXZ;
    float oasisCz = NINH_THUAN_LAKE_CENTER_Z * s_scaleXZ;
    float scanR = 24.0f * s_scaleXZ;
    unsigned int rRng = 0x541923u;
    for (int a = 0; a < 420 && s_reedCount < NINH_THUAN_MAX_REEDS; a++)
    {
        rRng = rRng * 1664525u + 1013904223u;
        float u0 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        rRng = rRng * 1664525u + 1013904223u;
        float u1 = (float)((rRng >> 16) & 0xFFFFu) / 65535.0f;
        float ang = u0 * (2.0f * PI);
        float rad = (6.0f + u1 * scanR);
        float rx = oasisCx + cosf(ang) * rad;
        float rz = oasisCz + sinf(ang) * rad;
        float wY = 0.0f, wD = 0.0f;
        if (MapDem_GetWaterInfo(&s_dem, &s_ground, rx, rz, &wY, &wD))
            continue;
        // Check if within ~1.6m of oasis water edge
        bool nearWater = false;
        for (int k = 0; k < 4 && !nearWater; k++)
        {
            float ka = (float)k * (PI * 0.5f);
            if (MapDem_GetWaterInfo(&s_dem, &s_ground, rx + cosf(ka) * 1.5f, rz + sinf(ka) * 1.5f, &wY, &wD))
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
                .height = 0.92f + 0.34f * u1,
                .rotationDeg = u0 * 360.0f,
                .phase = u1,
            };
        }
    }

    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        NULL, NULL, 0,
        (MapFloraStyle){
            .grassRootColor = {112, 82, 48, 255},
            .grassTipColor = {224, 192, 122, 255},
            .bladesPerClump = 6,
            .bladeSegments = 3,
            .bladeWidthScale = 0.132f,
            .lodDistance = 36.0f,
            .midLodDistance = 24.0f,
            .drawDistance = 120.0f,
            .shadowDistance = (s_scaleXZ > 1.5f) ? -1.0f : 24.0f,
            .grassState = MAP_GRASS_STATE_DRY,
            .dryness = 0.84f,
            .reedRootColor = {38, 62, 26, 255},
            .reedTipColor = {138, 156, 74, 255},
            .hasReedPlumes = false,
            .botanicalVariation = 1.0f,
            .enableCoLauPlumes = true,
            .coLauOnReeds = true,
            .coLauPlumeDensity = 0.88f,
            .coLauPlumeTint = {240, 224, 186, 255},
        });
}

void InitNinhThuanMap(void)
{
    s_scaleXZ = MapManager_GetWorldScaleXZ();
    s_scaleY  = MapManager_GetWorldScaleY();
    s_mapWidth = NINH_THUAN_MAP_WIDTH * s_scaleXZ;
    s_mapDepth = NINH_THUAN_MAP_DEPTH * s_scaleXZ;
    s_heightRange = NINH_THUAN_HEIGHT_RANGE * s_scaleY;
    kMapCenter = (Vector3){s_mapWidth * 0.5f, 0.0f, s_mapDepth * 0.5f};

    MapManager_SetActiveMetadata(&(MapSpatialMetadata){
        .spawnPoint = {160.0f * s_scaleXZ, 0.0f, 222.0f * s_scaleXZ},
        .arenaCenter = {94.15f * s_scaleXZ, 0.0f, 150.0f * s_scaleXZ},
        .arenaRadius = 135.0f * s_scaleXZ,
        .hasPlayableRect = true,
        .minX = 6.0f * s_scaleXZ, .maxX = 182.0f * s_scaleXZ,
        .minZ = 6.0f * s_scaleXZ, .maxZ = 294.0f * s_scaleXZ,
    });
    s_staticShadowCenter = kMapCenter;

    s_ninhThuanZones[0] = (MapZone){.center = {NINH_THUAN_LAKE_CENTER_X * s_scaleXZ, 0.0f, NINH_THUAN_LAKE_CENTER_Z * s_scaleXZ}, .radius = 22.0f * s_scaleXZ, .type = NAT_RIVER};
    s_ninhThuanZones[1] = (MapZone){.center = {135.0f * s_scaleXZ, 0.0f, 195.0f * s_scaleXZ}, .radius = 32.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};
    s_ninhThuanZones[2] = (MapZone){.center = {94.10f * s_scaleXZ, 0.0f, 150.0f * s_scaleXZ}, .radius = 45.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};
    s_ninhThuanZones[3] = (MapZone){.center = {65.0f * s_scaleXZ, 0.0f, 95.0f * s_scaleXZ}, .radius = 40.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};

    s_prevWindMacro = Wind_GetMacro();
    WindMacroConfig aridWind = {
        .baseDirection = {3.85f, 0.0f, 2.15f},
        .gustAmplitude = 1.75f,
        .noiseScale = 0.065f,
        .noiseSpeed = 1.85f,
        .terrainLiftK = 1.35f,
        .heightGradientK = 0.80f,
    };
    Wind_SetMacro(&aridWind);

    s_skipMask = MapProp_GetSkipMask();
    if (!ColorGradeLut_Load("assets/maps/ninh_thuan/ninh_thuan_grade.png"))
        ColorGradeLut_UseNeutral();

    // Sun-Bleached Golden Afternoon (Ninh Thuan Coastal Semi-Arid Stone Park)
    static const EnvLightingPreset kNinhThuanToD[5] = {
        {
            .ambientColor = {112, 120, 138, 255},
            .sunColor = {255, 206, 148, 255},
            .sunDirection = {0.82f, -0.30f, -0.49f},
            .shadowColor = {66, 74, 106, 155},
            .fog = {.enabled = true, .color = {238, 214, 182, 255}, .start = 42.0f, .end = 240.0f, .density = 0.0042f},
            .atmosphere = {
                .color = {238, 214, 182, 255}, .start = 42.0f, .end = 240.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0092f, 0.0148f, 0.0255f}, .mieScattering = 0.0052f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.1f},
                .density = {.baseDensity = 0.0042f, .heightFalloff = 0.22f, .baseAltitude = -6.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {124, 134, 152, 255},
            .sunColor = {255, 240, 208, 255},
            .sunDirection = {0.34f, -0.88f, -0.33f},
            .shadowColor = {72, 82, 114, 155},
            .fog = {.enabled = true, .color = {236, 224, 204, 255}, .start = 48.0f, .end = 260.0f, .density = 0.0034f},
            .atmosphere = {
                .color = {236, 224, 204, 255}, .start = 48.0f, .end = 260.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0082f, 0.0138f, 0.0240f}, .mieScattering = 0.0038f, .mieAnisotropy = 0.76f, .multipleScatteringAmp = 1.9f},
                .density = {.baseDensity = 0.0034f, .heightFalloff = 0.20f, .baseAltitude = -6.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {114, 124, 144, 255},
            .sunColor = {255, 218, 166, 255},
            .sunDirection = {0.74f, -0.42f, -0.52f},
            .shadowColor = {64, 74, 108, 165},
            .fog = {.enabled = true, .color = {240, 216, 184, 255}, .start = 42.0f, .end = 235.0f, .density = 0.0028f},
            .atmosphere = {
                .color = {240, 216, 184, 255}, .start = 42.0f, .end = 235.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0088f, 0.0145f, 0.0250f}, .mieScattering = 0.0052f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.0028f, .heightFalloff = 0.45f, .baseAltitude = -2.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {94, 98, 124, 255},
            .sunColor = {255, 168, 108, 255},
            .sunDirection = {-0.84f, -0.24f, -0.49f},
            .shadowColor = {60, 66, 98, 150},
            .fog = {.enabled = true, .color = {242, 198, 166, 255}, .start = 36.0f, .end = 220.0f, .density = 0.0048f},
            .atmosphere = {
                .color = {242, 198, 166, 255}, .start = 36.0f, .end = 220.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0110f, 0.0168f, 0.0290f}, .mieScattering = 0.0060f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 2.3f},
                .density = {.baseDensity = 0.0048f, .heightFalloff = 0.22f, .baseAltitude = -6.0f, .enableSigmoidLayer = false}
            }
        },
        // 22:00 Midnight Moonlight: cool desert indigo ambient, crisp silvery lunar key light
        {
            .ambientColor = {18, 28, 58, 255},
            .sunColor = {165, 195, 250, 255},
            .sunDirection = {0.52f, -0.44f, -0.73f},
            .shadowColor = {4, 7, 18, 215},
            .fog = {.enabled = true, .color = {12, 18, 42, 255}, .start = 32.0f, .end = 215.0f, .density = 0.0062f},
            .atmosphere = {
                .color = {12, 18, 42, 255}, .start = 32.0f, .end = 215.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0042f, 0.0078f, 0.0195f}, .mieScattering = 0.0045f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 1.75f},
                .density = {.baseDensity = 0.0048f, .heightFalloff = 0.24f, .baseAltitude = -6.0f, .enableSigmoidLayer = false}
            }
        }
    };
    static const float kNinhThuanTimes[5] = {0.25f, 0.50f, 0.64f, 0.7708f, 0.92f};
    Environment_SetTimeOfDayPresets(kNinhThuanToD, kNinhThuanTimes, 5);
    Environment_SetTimeOfDay(0.64f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){114, 124, 144, 255});
    Environment_SetSunColor((Color){255, 218, 166, 255});
    Environment_SetSunIntensity(1.72f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.74f, -0.42f, -0.52f}));
    Environment_SetShadowColor((Color){64, 74, 108, 165});
    if (Environment_IsNightMode())
        Environment_ApplyMoonlightNight();

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.02f,
        .contrast = 1.10f,
        .saturation = 1.07f,
        .bloomIntensity = 0.21f,
        .shadowTint = {0.86f, 0.92f, 1.08f},
        .highlightTint = {1.08f, 0.99f, 0.89f},
        .bloomThreshold = 1.10f,
        .bloomKnee = 0.42f,
        .hueRestore = 0.62f,
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);

    AtmosphereProfile atmos = kNinhThuanToD[2].atmosphere;
    atmos.enabled = false;
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(0.95f);
    VolumetricFog_SetDistantCoverage(0.40f);
    s_prevAtmoMode = Atmosphere_GetMode();
    Atmosphere_SetMode(ATMO_MODE_DESERT_DUST);
    Atmosphere_Configure(kMapCenter, (Vector3){28.0f, 1.8f, 28.0f}, 360, (Color){248, 220, 174, 255});
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.22f,
        .worldSize = 160.0f,
        .planeHeight = 90.0f,
        .coverage = 0.36f,
        .softness = 0.34f,
        .windSpeedScale = 0.85f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);
    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnNinhThuanDustHaze();

    // Activate Earth/Arid Biome Shaders (Terrain, Rock, Cliff)
    MapGroundBiomeProfile earthBiome = {
        .aridFactor = 1.0f,
        .strataStrength = 0.92f,
        .grusColor = {0.68f, 0.56f, 0.42f},
        .hardpanColor = {0.54f, 0.41f, 0.30f},
        .lichenColor = {0.58f, 0.42f, 0.22f},
    };
    MapProp_SetGroundBiomeProfile(&earthBiome);
    MapProp_SetRockAridMode(1.0f);
    MapProp_SetCliffAridMode(1.0f);
    MapProp_SetCliffSeaLevel(NINH_THUAN_SEA_Y * s_scaleY);
    MapProp_SetGrassState(MAP_GRASS_STATE_DRY, 0.0f);
    {
        MapGrassCondition aridGrass = MapProp_GetGrassCondition();
        aridGrass.dryness = 0.88f;
        MapProp_SetGrassCondition(aridGrass);
    }

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawNinhThuanShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        if (s_scaleXZ > 1.5f)
    {
        float sdx = camera.target.x - s_staticShadowCenter.x;
        float sdz = camera.target.z - s_staticShadowCenter.z;
        if (sdx * sdx + sdz * sdz > 70.0f * 70.0f)
        {
            s_staticShadowCenter = camera.target;
            EnvShadow_InvalidateStaticCache();
        }
    }
    CaptureNinhThuanStaticShadows();
        MapManager_SetZones(s_ninhThuanZones, NINH_THUAN_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = camera.target;
    EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/ninh_thuan/ninh_thuan_ecology_splat.png",
                      "assets/maps/ninh_thuan/ninh_thuan_water_height.png");

    const char *hfPath = (s_scaleXZ > 1.5f && FileExists("assets/maps/ninh_thuan/ninh_thuan_heightfield_ow.png"))
                             ? "assets/maps/ninh_thuan/ninh_thuan_heightfield_ow.png"
                             : "assets/maps/ninh_thuan/ninh_thuan_heightfield.png";
    s_ground = MapProp_CreateGroundHeightfield16(
        hfPath, MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 3.6f,
        "assets/maps/ninh_thuan/ninh_thuan_ecology_splat.png",
        "assets/textures/dirt_diffuse.png",
        "assets/textures/dirt_diffuse.png");

    MapEcology_LoadBakedAssets(&s_habitat,
                               "assets/maps/ninh_thuan/ninh_thuan_habitat.png",
                               "assets/maps/ninh_thuan/ninh_thuan_habitat_distance.png",
                               (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH},
                               NULL, 0, NULL, NULL);
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    MapProp_SetGroundTint(&s_ground, (Color){204, 186, 154, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/dirt_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/maps/ninh_thuan/ninh_thuan_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/ninh_thuan/ninh_thuan_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rocks/granite_diffuse.png",
                              "assets/textures/rocks/granite_normal.png");

    s_sky = MapProp_CreateSkyDome();
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    const float seaY = NINH_THUAN_SEA_Y * s_scaleY;
    const float seaSpan = (s_scaleXZ > 1.5f) ? (620.0f * s_scaleXZ) : 840.0f;
    const int seaSegs = (s_scaleXZ > 1.5f) ? 84 : 72;
    s_coastalSea = MapProp_CreateWaterPatch(
        (Vector3){kMapCenter.x, seaY, kMapCenter.z},
        seaSpan, seaSpan,
        (MapWaterConfig){
            .ecosystem = WATER_ECO_CUSTOM,
            .segments = seaSegs,
            .rings = seaSegs,
            .waveHeight = 0.28f,
            .waveScale = 0.45f,
            .waveSpeed = 1.20f,
            .flowVelocity = {0.20f, 0.14f},
            .detailScale = 0.24f,
            .detailStrength = 0.42f,
            .maxDepth = 4.2f,
            .absorption = {0.58f, 0.15f, 0.06f},
            .scatterColor = {0.06f, 0.52f, 0.50f},
            .scatterCoeff = 0.48f,
            .causticsStrength = 0.42f,
            .causticsScale = 1.45f,
            .foamThreshold = 0.28f,
            .deepColor = {6, 44, 74, 255},
            .shallowColor = {26, 158, 152, 235},
            .foamColor = {250, 248, 242, 235},
        });
    if (s_coastalSea.ready)
    {
        // Bake 512x512 coastal cliff distance & inward-normal map using exact seaSpan so surf matches coastline
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
    MapProp_SetWaterTerrainSource(&s_coastalSea, NinhThuanCoastalBed, NinhThuanCoastalSurface, NULL);

    s_oasis = MapProp_CreateTerrainWater("assets/maps/ninh_thuan/ninh_thuan_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .center = {NINH_THUAN_LAKE_CENTER_X * s_scaleXZ, 0.0f, NINH_THUAN_LAKE_CENTER_Z * s_scaleXZ},
                                             .radiusX = NINH_THUAN_LAKE_RADIUS_X * s_scaleXZ,
                                             .radiusZ = NINH_THUAN_LAKE_RADIUS_Z * s_scaleXZ,
                                             .waveHeight = 0.008f,
                                             .waveScale = 1.15f,
                                             .waveSpeed = 0.45f,
                                             .flowVelocity = {0.06f, 0.08f},
                                             .detailScale = 0.18f,
                                             .detailStrength = 0.10f,
                                             .maxDepth = 1.65f,
                                             .absorption = {0.50f, 0.14f, 0.08f},
                                             .scatterColor = {0.10f, 0.42f, 0.36f},
                                             .scatterCoeff = 0.30f,
                                             .causticsStrength = 0.42f,
                                             .causticsScale = 2.20f,
                                             .foamThreshold = 0.085f,
                                             .deepColor = {10, 38, 42, 236},
                                             .shallowColor = {42, 102, 88, 96},
                                             .foamColor = {245, 240, 224, 200},
                                         });
    if (s_oasis.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_oasis.waterModel.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_oasis.boundsCenter = (Vector3){
            s_oasis.boundsCenter.x * s_scaleXZ,
            s_oasis.boundsCenter.y * s_scaleY,
            s_oasis.boundsCenter.z * s_scaleXZ
        };
        s_oasis.boundsRadius *= s_scaleXZ;
    }
    MapProp_SetWaterTerrainSource(&s_oasis, NinhThuanOasisBed, NinhThuanOasisSurface, NULL);

    BuildNinhThuanRocks();
    s_floraWindowCenter = (s_scaleXZ > 1.5f)
                              ? (Vector3){160.0f * s_scaleXZ, 0.0f, 222.0f * s_scaleXZ}
                              : kMapCenter;
    BuildNinhThuanFlora();

    // Seed interactive arid granular debris bed (warm loess & gravel only, no black twig specks)
#ifndef __ANDROID__
    {
        enum { NINH_THUAN_BED_CAP = 1200 };
        GranularGrainState *bedGrains = (GranularGrainState *)MemAlloc((size_t)NINH_THUAN_BED_CAP * sizeof(GranularGrainState));
        if (bedGrains)
        {
            static const uint32_t kAridMats[2] = {
                GRANULAR_MAT_LOESS_SOIL,
                GRANULAR_MAT_COARSE_GRAVEL
            };
            float bedMinX = (s_scaleXZ > 1.5f) ? fmaxf(22.0f, s_floraWindowCenter.x - 110.0f) : 22.0f;
            float bedMaxX = (s_scaleXZ > 1.5f) ? fminf(MAP_WIDTH - 22.0f, s_floraWindowCenter.x + 110.0f) : (MAP_WIDTH - 22.0f);
            float bedMinZ = (s_scaleXZ > 1.5f) ? fmaxf(22.0f, s_floraWindowCenter.z - 110.0f) : 22.0f;
            float bedMaxZ = (s_scaleXZ > 1.5f) ? fminf(MAP_DEPTH - 22.0f, s_floraWindowCenter.z + 110.0f) : (MAP_DEPTH - 22.0f);
            int count = 0;
            for (int i = 0; i < NINH_THUAN_BED_CAP * 2 && count < NINH_THUAN_BED_CAP; i++)
            {
                unsigned int h = (unsigned int)i * 747796405u + 2891336453u;
                float u0 = (float)((h ^ (h >> 15)) & 0xFFFFu) / 65536.0f;
                float u1 = (float)(((h * 1664525u + 1013904223u) >> 16) & 0xFFFFu) / 65536.0f;
                float u2 = (float)(((h * 22695477u + 1u) >> 16) & 0xFFFFu) / 65536.0f;
                float wx = bedMinX + u0 * (bedMaxX - bedMinX);
                float wz = bedMinZ + u1 * (bedMaxZ - bedMinZ);
                Vector3 gPos = {0}, gNorm = {0};
                if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, wx, wz, &gPos, &gNorm) &&
                    gNorm.y >= 0.72f && gPos.y > seaY + 0.25f)
                {
                    gPos.y += 0.012f;
                    bedGrains[count++] = (GranularGrainState){
                        .position = gPos,
                        .velocity = {0.0f, 0.0f, 0.0f},
                        .radius = 0.018f + 0.010f * u2,
                        .lifetime = 999.0f,
                        .seed = u2,
                        .materialId = kAridMats[(int)(u2 * 2.0f) & 1]
                    };
                }
            }
            GranularSim_SeedMapBedFromGrains(bedGrains, count);
            MemFree(bedGrains);
        }
    }
#endif

    s_ready = true;
    EnvShadow_SetMapCasterCallback(DrawNinhThuanShadowCasters, NULL);
    EnvShadow_InvalidateStaticCache();
    if (s_scaleXZ > 1.5f)
    {
        s_staticShadowCenter = s_floraWindowCenter;
        EnvShadow_InvalidateStaticCache();
    }
    CaptureNinhThuanStaticShadows();
    MapManager_SetZones(s_ninhThuanZones, NINH_THUAN_ZONE_COUNT);
    extern void ResolveCollisionNinhThuanMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("NINH_THUAN", ResolveCollisionNinhThuanMap);
}

void DrawNinhThuanMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    if (!(s_skipMask & MAP_SKIP_ROCKS))
        MapProp_DrawRockSystem(&s_rocks, false);
    if (!(s_skipMask & MAP_SKIP_GROUND))
        MapProp_DrawGround(&s_ground, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        MapProp_DrawCliffSkirt(&s_cliff, camera);
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);

    /* s_coastalSea sits over s_cliff / deep abyss, so DrawWaterBed is redundant here */
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
}

void DrawTransparentNinhThuanMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_WATER))
    {
        float t = (float)fmod(GetTime(), 3600.0);
        MapProp_DrawWaterOverlay(&s_coastalSea, t);
        MapProp_DrawWaterOverlay(&s_oasis, t);
    }
}

void UpdateNinhThuanMap(float dt)
{
    if (!s_ready)
        return;

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_coastalSea, dt);
    MapProp_UpdateWaterSurface(&s_oasis, dt);

    MapDem_UpdateShadowFocusAndStaticCache(
        camera.target, &s_lastShadowFocus, &s_staticShadowCenter,
        SHADOW_FOCUS_RADIUS, SHADOW_SNAP_STEP, SHADOW_HYSTERESIS_XZ, SHADOW_HYSTERESIS_Y,
        s_scaleXZ, kMapCenter, (s_scaleXZ > 1.5f) ? 140.0f : 155.0f, &s_rocks, s_skipMask);
}

void UnloadNinhThuanMap(void)
{
    Wind_SetMacro(&s_prevWindMacro);
    Atmosphere_SetMode(s_prevAtmoMode);
    ColorGradeLut_UseNeutral();
    PostFX_ClearMapProfile();
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    Environment_SetSunIntensity(1.0f);
    MapProp_SetGroundBiomeProfile(NULL);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);

    if (!s_ready)
        return;

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.22f,
        .worldSize = 160.0f,
        .planeHeight = 90.0f,
        .coverage = 0.36f,
        .softness = 0.34f,
        .windSpeedScale = 0.85f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_oasis);
    MapProp_UnloadWaterSurface(&s_coastalSea);
    MapProp_UnloadFloraSystem(&s_flora);
    GranularSim_ClearMapBed();
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

float GetGroundHeightNinhThuanMap(float x, float z)
{
    if (!s_ready)
        return 0.0f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

bool SampleGroundSurfaceNinhThuanMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (!s_ready)
        return false;
    return MapDem_SampleGroundSurface(&s_dem, &s_ground, x, z, outPosition, outNormal);
}

bool GetWaterInfoNinhThuanMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready)
        return false;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, outSurfaceY, outWaterDepth);
}

void SetWaterInteractorNinhThuanMap(Vector3 position, Vector3 velocity, float radius)
{
    if (!s_ready)
        return;
    MapDem_SetWaterInteractor(&s_dem, &s_ground, &s_oasis, position, velocity, radius);
}

void AddWaterRippleNinhThuanMap(Vector3 position, float radius, float intensity)
{
    if (!s_ready)
        return;
    MapProp_AddWaterRipple(&s_oasis, position, radius, intensity);
}

void SetRainNinhThuanMap(float intensity)
{
    (void)intensity;
}

void ResolveCollisionNinhThuanMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY)
{
    if (!s_ready)
        return;
    MapProp_ResolveRockSystemCollision(&s_rocks, pos, agentRadius, stepHeight, inOutGroundY);
}


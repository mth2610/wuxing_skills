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
#define SHADOW_FOCUS_RADIUS 18.0f

static Vector3 kMapCenter = {NINH_THUAN_MAP_WIDTH * 0.5f, 0.0f, NINH_THUAN_MAP_DEPTH * 0.5f};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};

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
    return NINH_THUAN_SEA_Y;
}

static void DrawNinhThuanShadowCasters(Shader depthShader, void *user)
{
    (void)user;
    if ((s_skipMask & MAP_SKIP_SHADOWCASTERS) || (s_skipMask & MAP_SKIP_ROCKS))
        return;
    if (!s_rocks.ready || !s_rocks.items || s_rocks.count <= 0)
        return;

    /* Submit nearby large boulders to the dynamic shadow pass for crisp high-res
     * shadows around the player, while distant tors use the static shadow cache. */
    Vector3 focus = EnvShadow_GetFocus();
    float maxDist = EnvShadow_GetHalfExtent() + 4.0f;
    float maxDistSq = maxDist * maxDist;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < s_rocks.count; i++)
    {
        const MapRockItem *p = &s_rocks.items[i];
        if (p->archetype == MAP_ROCK_SCREE || p->radiusScale < 1.20f)
            continue;
        float dx = p->position.x - focus.x;
        float dz = p->position.z - focus.z;
        if (dx * dx + dz * dz > maxDistSq)
            continue;
        int arch = (int)p->archetype;
        if (arch < 0 || arch >= MAP_ROCK_ARCHETYPE_COUNT)
            arch = MAP_ROCK_GRANITE;
        const MapRockSet *set = &s_rocks.models[arch];
        if (!set->ready || set->model.materialCount < 1)
            continue;
        Shader prev = set->model.materials[0].shader;
        set->model.materials[0].shader = depthShader;
        DrawModelEx(set->model, p->position, rotAxis, p->rotationDeg,
                    (Vector3){p->radiusScale, p->heightScale, p->radiusScale}, WHITE);
        set->model.materials[0].shader = prev;
    }
    rlDrawRenderBatchActive();
}

static void CaptureNinhThuanStaticShadows(void)
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
    EnvShadow_EndStaticCapture();
}

static void SpawnNinhThuanDustHaze(void)
{
    FogVolume_ClearAll();
    static const LocalFogVolume kDustVolumes[] = {
        {.shape = FOG_SHAPE_CYLINDER, .position = {94.1f, 0.35f, 150.0f}, .extents = {18.0f, 0.65f, 16.0f}, .color = {240, 214, 176, 255}, .density = 0.14f, .edgeSoftness = 0.92f, .driftVelocity = {0.85f, 0.0f, 0.48f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {NINH_THUAN_LAKE_CENTER_X, 0.45f, NINH_THUAN_LAKE_CENTER_Z}, .extents = {16.0f, 0.55f, 14.0f}, .color = {242, 222, 190, 255}, .density = 0.15f, .edgeSoftness = 0.92f, .driftVelocity = {0.60f, 0.0f, 0.35f}, .active = true},
    };
    for (int i = 0; i < (int)(sizeof(kDustVolumes) / sizeof(kDustVolumes[0])); i++)
    {
        FogVolume_Create(&kDustVolumes[i]);
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
        "assets/textures/rock_diffuse.png",
        "assets/textures/rock_normal.png",
        "assets/textures/rock_roughness.png",
        NINH_THUAN_ROCK_COUNT + 32);

    for (int i = 0; i < NINH_THUAN_ROCK_COUNT; i++)
    {
        const MapRockItem *item = &NINH_THUAN_ROCK_ITEMS[i];
        Vector3 pos = { item->position.x * s_scaleXZ, item->position.y * s_scaleY, item->position.z * s_scaleXZ };
        float rScale = item->radiusScale * (1.0f + 0.35f * (s_scaleXZ - 1.0f));
        float hScale = item->heightScale * (1.0f + 0.25f * (s_scaleY - 1.0f));
        Vector3 gPos = pos, gNorm = {0.0f, 1.0f, 0.0f};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &gPos, &gNorm))
        {
            // Preserve elevated y for stacked perched corestones (tors), seated into the lower rock's sunk top
            if (item->position.y * s_scaleY > gPos.y + 0.35f)
            {
                float baseSink = (i > 0) ? (NINH_THUAN_ROCK_ITEMS[i - 1].heightScale * (1.0f + 0.25f * (s_scaleY - 1.0f)) * 0.22f) : 0.0f;
                float contactOverlap = hScale * 0.16f;
                pos.y = item->position.y * s_scaleY - baseSink - contactOverlap;
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

#define NINH_THUAN_MAX_GRASS 12000
#define NINH_THUAN_MAX_REEDS 120
static MapMeadowPlacement s_grassPlacements[NINH_THUAN_MAX_GRASS];
static MapMeadowPlacement s_reedPlacements[NINH_THUAN_MAX_REEDS];
static int s_grassCount = 0;
static int s_reedCount = 0;

static void BuildNinhThuanFlora(void)
{
#ifdef __ANDROID__
    const float grassSpacing = 1.25f;
    const int   maxGrass     = 3200;
#else
    const float grassSpacing = (s_scaleXZ > 1.5f) ? 1.35f : 0.64f;
    const int   maxGrass     = NINH_THUAN_MAX_GRASS;
#endif
    MapMeadowDistribution dist = {
        .minBounds = {12.0f * s_scaleXZ, 12.0f * s_scaleXZ},
        .maxBounds = {MAP_WIDTH - 12.0f * s_scaleXZ, MAP_DEPTH - 12.0f * s_scaleXZ},
        .spacing = grassSpacing,
        .jitter = 0.26f,
        .minRadius = 0.14f,
        .maxRadius = 0.24f,
        .minHeight = 0.18f,
        .maxHeight = 0.32f,
        .yOffset = 0.0f,
        .seed = 90421u,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, maxGrass,
        &s_ground, kMapCenter,
        dist, NinhThuanDryGrassDensity, NULL);

    s_reedCount = 0;
    for (int i = 0; i < 24 && s_reedCount < NINH_THUAN_MAX_REEDS; i++)
    {
        float ang = (float)i * 0.2617994f;
        float rDist = (13.5f + 4.5f * sinf((float)i * 1.7f)) * s_scaleXZ;
        float rx = NINH_THUAN_LAKE_CENTER_X * s_scaleXZ + cosf(ang) * rDist;
        float rz = NINH_THUAN_LAKE_CENTER_Z * s_scaleXZ + sinf(ang) * rDist;
        Vector3 gp, gn;
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, rx, rz, &gp, &gn) && gn.y > 0.78f && gp.y > 0.12f)
        {
            s_reedPlacements[s_reedCount++] = (MapMeadowPlacement){
                .position = gp,
                .radius = 0.24f,
                .height = 0.95f + 0.25f * sinf((float)i * 2.3f),
                .rotationDeg = (float)(i * 47 % 360),
            };
        }
    }

    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        s_reedPlacements, s_reedCount,
        NULL, NULL, 0,
        (MapFloraStyle){
            .grassRootColor = {72, 54, 36, 255},
            .grassTipColor = {214, 188, 128, 255},
            .bladesPerClump = 6,
            .bladeSegments = 3,
            .bladeWidthScale = 0.128f,
            .lodDistance = 24.0f,
            .midLodDistance = 20.0f,
            .drawDistance = 42.0f,
            .shadowDistance = 14.0f,
            .grassState = MAP_GRASS_STATE_DRY,
            .dryness = 0.88f,
            .reedRootColor = {88, 72, 46, 255},
            .reedTipColor = {168, 144, 92, 255},
            .hasReedPlumes = true,
            .botanicalVariation = 1.0f,
            .enableCoLauPlumes = true,
            .coLauOnReeds = true,
            .coLauPlumeDensity = 0.85f,
            .coLauPlumeTint = {236, 218, 182, 255},
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
    static const EnvLightingPreset kNinhThuanToD[4] = {
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
        }
    };
    static const float kNinhThuanTimes[4] = {0.25f, 0.50f, 0.64f, 0.7708f};
    Environment_SetTimeOfDayPresets(kNinhThuanToD, kNinhThuanTimes, 4);
    Environment_SetTimeOfDay(0.64f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){114, 124, 144, 255});
    Environment_SetSunColor((Color){255, 218, 166, 255});
    Environment_SetSunIntensity(1.72f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.74f, -0.42f, -0.52f}));
    Environment_SetShadowColor((Color){64, 74, 108, 165});

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.02f,
        .contrast = 1.10f,
        .saturation = 1.02f,
        .bloomIntensity = 0.18f,
        .shadowTint = {0.88f, 0.93f, 1.08f},
        .highlightTint = {1.07f, 0.99f, 0.90f},
        .bloomThreshold = 1.12f,
        .bloomKnee = 0.42f,
        .hueRestore = 0.60f,
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
        .strataStrength = 0.90f,
        .grusColor = {0.62f, 0.54f, 0.43f},
        .hardpanColor = {0.48f, 0.40f, 0.31f},
        .lichenColor = {0.58f, 0.42f, 0.22f},
    };
    MapProp_SetGroundBiomeProfile(&earthBiome);
    MapProp_SetRockAridMode(1.0f);
    MapProp_SetCliffAridMode(1.0f);
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
    s_lastShadowFocus = kMapCenter;
    EnvShadow_SetFocus(kMapCenter, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/ninh_thuan/ninh_thuan_ecology_splat.png",
                      "assets/maps/ninh_thuan/ninh_thuan_water_height.png");

    s_ground = MapProp_CreateGroundHeightfield16(
        "assets/maps/ninh_thuan/ninh_thuan_heightfield.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 3.6f,
        "assets/maps/ninh_thuan/ninh_thuan_ecology_splat.png",
        "assets/textures/dirt_diffuse.png",
        "assets/textures/dirt_diffuse.png");

    s_habitat.habitatTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/ninh_thuan/ninh_thuan_habitat.png", true,
        TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP);
    {
        Image distImg = LoadImage("assets/maps/ninh_thuan/ninh_thuan_habitat_distance.png");
        if (distImg.data != NULL)
        {
            ImageFormat(&distImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            unsigned char *bytes = (unsigned char *)distImg.data;
            int totalPx = distImg.width * distImg.height;
            const float kPathScale = (32.0f / 65535.0f) * (1.0f / 5.1f);
            const float kPathBias = (-14.35f) * (1.0f / 5.1f);
            const float kShoreScale = (32.0f / 65535.0f) * (1.0f / 5.5f);
            const float kShoreBias = (-12.90f) * (1.0f / 5.5f);
            for (int i = 0; i < totalPx; i++)
            {
                unsigned char *p = &bytes[i * 4];
                float rawPath = (float)(((unsigned int)p[0] << 8) | (unsigned int)p[1]);
                float rawShore = (float)(((unsigned int)p[2] << 8) | (unsigned int)p[3]);
                float encPath = rawPath * kPathScale + kPathBias;
                float encShore = rawShore * kShoreScale + kShoreBias;
                encPath = (encPath > 0.0f) ? ((encPath < 1.0f) ? encPath : 1.0f) : 0.0f;
                encShore = (encShore > 0.0f) ? ((encShore < 1.0f) ? encShore : 1.0f) : 0.0f;
                p[0] = (unsigned char)(encPath * 255.0f + 0.5f);
                p[1] = (unsigned char)(encShore * 255.0f + 0.5f);
                p[2] = 0;
                p[3] = 255;
            }
            s_habitat.distanceTexture = LoadTextureFromImage(distImg);
            UnloadImage(distImg);
            if (s_habitat.distanceTexture.id > 0)
            {
                SetTextureFilter(s_habitat.distanceTexture, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(s_habitat.distanceTexture, TEXTURE_WRAP_CLAMP);
            }
        }
    }
    s_habitat.rect = (Vector4){0, 0, MAP_WIDTH, MAP_DEPTH};
    s_habitat.ready = s_habitat.habitatTexture.id > 0 && s_habitat.distanceTexture.id > 0;
    MapProp_SetGroundEcology(&s_ground, &s_habitat);

    MapProp_SetGroundTint(&s_ground, (Color){204, 186, 154, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/dirt_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/maps/ninh_thuan/ninh_thuan_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/ninh_thuan/ninh_thuan_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rock_diffuse.png",
                              "assets/textures/rock_normal.png");

    s_sky = MapProp_CreateSkyDome();
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    s_coastalSea = MapProp_CreateWaterPatch(
        (Vector3){kMapCenter.x, NINH_THUAN_SEA_Y, kMapCenter.z},
        560.0f * s_scaleXZ, 560.0f * s_scaleXZ,
        (MapWaterConfig){
            .ecosystem = WATER_ECO_CUSTOM,
            .segments = 56,
            .rings = 56,
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
        // Bake 512x512 coastal cliff distance & inward-normal map for crashing surf waves & shallow gradient
        const int res = 512;
        const float patchSpan = 820.0f;
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
                            gp.y > NINH_THUAN_SEA_Y + 0.05f)
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
                    float normDist = distGrid[idx] / 24.0f;
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
    BuildNinhThuanFlora();

    // Seed interactive arid granular debris bed (skipped on Android to reduce load time & thermal load)
#ifndef __ANDROID__
    {
        enum { NINH_THUAN_BED_CAP = 2600 };
        GranularGrainState *bedGrains = (GranularGrainState *)MemAlloc((size_t)NINH_THUAN_BED_CAP * sizeof(GranularGrainState));
        if (bedGrains)
        {
            static const uint32_t kAridMats[4] = {
                GRANULAR_MAT_LOESS_SOIL,
                GRANULAR_MAT_COARSE_GRAVEL,
                GRANULAR_MAT_DRY_TWIG,
                GRANULAR_MAT_DRY_LEAF
            };
            int count = 0;
            for (int i = 0; i < NINH_THUAN_BED_CAP * 2 && count < NINH_THUAN_BED_CAP; i++)
            {
                unsigned int h = (unsigned int)i * 747796405u + 2891336453u;
                float u0 = (float)((h ^ (h >> 15)) & 0xFFFFu) / 65536.0f;
                float u1 = (float)(((h * 1664525u + 1013904223u) >> 16) & 0xFFFFu) / 65536.0f;
                float u2 = (float)(((h * 22695477u + 1u) >> 16) & 0xFFFFu) / 65536.0f;
                float wx = 22.0f + u0 * (MAP_WIDTH - 44.0f);
                float wz = 22.0f + u1 * (MAP_DEPTH - 44.0f);
                Vector3 gPos = {0}, gNorm = {0};
                if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, wx, wz, &gPos, &gNorm) &&
                    gNorm.y >= 0.68f && gPos.y > NINH_THUAN_SEA_Y + 0.18f)
                {
                    gPos.y += 0.014f;
                    bedGrains[count++] = (GranularGrainState){
                        .position = gPos,
                        .velocity = {0.0f, 0.0f, 0.0f},
                        .radius = 0.022f + 0.014f * u2,
                        .lifetime = 999.0f,
                        .seed = u2,
                        .materialId = kAridMats[(int)(u2 * 4.0f) % 4]
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
    extern void ResolveCollisionNinhThuanMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("NINH_THUAN", ResolveCollisionNinhThuanMap);
}

void DrawNinhThuanMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    bool useFakeRockShadows = !EnvShadow_IsEnabled() && GfxQuality_Get() != GFX_UNLIT;
    if (!useFakeRockShadows)
    {
        if (!(s_skipMask & MAP_SKIP_ROCKS))
            MapProp_DrawRockSystem(&s_rocks, false);
        if (!(s_skipMask & MAP_SKIP_GROUND))
            MapProp_DrawGround(&s_ground, kMapCenter);
        if (!(s_skipMask & MAP_SKIP_CLIFF))
            MapProp_DrawCliffSkirt(&s_cliff, camera);
        if (!(s_skipMask & MAP_SKIP_FLORA))
            MapProp_DrawFloraSystem(&s_flora);
    }
    else
    {
        if (!(s_skipMask & MAP_SKIP_GROUND))
            MapProp_DrawGround(&s_ground, kMapCenter);
        if (!(s_skipMask & MAP_SKIP_CLIFF))
            MapProp_DrawCliffSkirt(&s_cliff, camera);
        if (!(s_skipMask & MAP_SKIP_ROCKS))
            MapProp_DrawRockSystem(&s_rocks, true);
        if (!(s_skipMask & MAP_SKIP_FLORA))
            MapProp_DrawFloraSystem(&s_flora);
    }

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

    float dx = camera.target.x - s_lastShadowFocus.x;
    float dz = camera.target.z - s_lastShadowFocus.z;
    if (dx * dx + dz * dz > 4.0f)
    {
        s_lastShadowFocus = camera.target;
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
    }
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


#include "maps/worlds/ninh_thuan/ninh_thuan.h"
#include "assets/maps/ninh_thuan/ninh_thuan_generated_meta.h"
#include "maps/worlds/ninh_thuan/ninh_thuan_rocks_generated.h"
#include "core/resource_manager.h"
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
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define MAP_WIDTH NINH_THUAN_MAP_WIDTH
#define MAP_DEPTH NINH_THUAN_MAP_DEPTH
#define CLIFF_DEPTH NINH_THUAN_CLIFF_DEPTH
#define CLOUD_SEA_Y -5.2f
#define SHADOW_FOCUS_RADIUS 28.0f

static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

static MapDemIsland s_dem = {0};
static MapGroundSurface s_ground = {0};
static MapEcology s_habitat = {0};
static MapSkyDome s_sky = {0};
static MapCloudSea s_cloudSea = {0};
static MapCliffSkirt s_cliff = {0};
static MapWaterSurface s_oasis = {0};
static MapRockSystem s_rocks = {0};
static MapFloraSystem s_flora = {0};

static WindMacroConfig s_prevWindMacro = {0};
static Vector3 s_lastShadowFocus = {0};
static unsigned int s_skipMask = 0;
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

static const MapZone NINH_THUAN_ZONES[] = {
    {.center = {94.10f, 150.0f}, .radius = 24.0f, .type = NAT_DESERT_ZONE},
    {.center = {NINH_THUAN_LAKE_CENTER_X, NINH_THUAN_LAKE_CENTER_Z}, .radius = 22.0f, .type = NAT_DESERT_ZONE},
};
#define NINH_THUAN_ZONE_COUNT ((int)(sizeof(NINH_THUAN_ZONES) / sizeof(NINH_THUAN_ZONES[0])))

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

static void DrawNinhThuanShadowCasters(Shader depthShader, void *user)
{
    (void)depthShader;
    (void)user;
    if (!s_ready || (s_skipMask & MAP_SKIP_SHADOWCASTERS))
        return;
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraShadowCasters(&s_flora);
}

static void CaptureNinhThuanStaticShadows(void)
{
    if (!EnvShadow_NeedsStaticCapture())
        return;
    if ((s_skipMask & MAP_SKIP_SHADOWCASTERS) || (s_skipMask & MAP_SKIP_ROCKS))
        return;
    EnvShadow_BeginStaticCapture(kMapCenter, 96.0f);
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
        {.shape = FOG_SHAPE_CYLINDER, .position = {94.1f, 0.35f, 150.0f}, .extents = {14.0f, 0.55f, 12.0f}, .color = {236, 210, 174, 255}, .density = 0.042f, .edgeSoftness = 0.92f, .driftVelocity = {0.03f, 0.0f, 0.015f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {70.0f, 1.80f, 118.0f}, .extents = {16.0f, 0.60f, 14.0f}, .color = {236, 210, 174, 255}, .density = 0.048f, .edgeSoftness = 0.92f, .driftVelocity = {0.03f, 0.0f, 0.015f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {122.0f, 2.20f, 185.0f}, .extents = {15.0f, 0.60f, 14.0f}, .color = {236, 210, 174, 255}, .density = 0.045f, .edgeSoftness = 0.92f, .driftVelocity = {0.03f, 0.0f, 0.015f}, .active = true},
        {.shape = FOG_SHAPE_CYLINDER, .position = {NINH_THUAN_LAKE_CENTER_X, 0.45f, NINH_THUAN_LAKE_CENTER_Z}, .extents = {14.0f, 0.50f, 12.0f}, .color = {238, 218, 186, 255}, .density = 0.052f, .edgeSoftness = 0.92f, .driftVelocity = {0.02f, 0.0f, 0.01f}, .active = true},
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

    // Sparse xerophytic grass tufts: ~12-15% coverage in wind-sheltered pockets & oasis fringe
    float oasisDist = hypotf(x - NINH_THUAN_LAKE_CENTER_X, z - NINH_THUAN_LAKE_CENTER_Z);
    float oasisFringe = (oasisDist < 24.0f) ? (1.0f - fmaxf(0.0f, (oasisDist - 12.0f) / 12.0f)) * 0.55f : 0.0f;

    float patch = sinf(x * 0.16f + z * 0.11f) * cosf(z * 0.19f - x * 0.13f);
    float fine = sinf(x * 0.52f - z * 0.44f) * 0.5f + 0.5f;
    float pocket = (patch > 0.36f) ? ((patch - 0.36f) * 1.15f * fine) : 0.0f;
    return fminf(0.70f, pocket + oasisFringe);
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
        Vector3 pos = item->position;
        Vector3 gPos = pos, gNorm = {0.0f, 1.0f, 0.0f};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &gPos, &gNorm))
        {
            // Preserve elevated y for stacked perched corestones (tors), sink grounded rocks slightly
            if (pos.y > gPos.y + 0.35f)
            {
                pos.y = item->position.y;
            }
            else
            {
                float sink = (item->archetype == MAP_ROCK_SCREE) ? item->heightScale * 0.18f : item->heightScale * 0.22f;
                pos.y = gPos.y - sink;
            }
        }
        MapProp_AddRockItem(&s_rocks, pos, item->radiusScale, item->heightScale, item->rotationDeg, item->archetype);
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
    MapMeadowDistribution dist = {
        .minBounds = {12.0f, 12.0f},
        .maxBounds = {MAP_WIDTH - 12.0f, MAP_DEPTH - 12.0f},
        .spacing = 0.68f,
        .jitter = 0.25f,
        .minRadius = 0.12f,
        .maxRadius = 0.20f,
        .minHeight = 0.16f,
        .maxHeight = 0.28f,
        .yOffset = 0.0f,
        .seed = 90421u,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, NINH_THUAN_MAX_GRASS,
        &s_ground, kMapCenter,
        dist, NinhThuanDryGrassDensity, NULL);

    s_reedCount = 0;
    for (int i = 0; i < 24 && s_reedCount < NINH_THUAN_MAX_REEDS; i++)
    {
        float ang = (float)i * 0.2617994f;
        float rDist = 13.5f + 4.5f * sinf((float)i * 1.7f);
        float rx = NINH_THUAN_LAKE_CENTER_X + cosf(ang) * rDist;
        float rz = NINH_THUAN_LAKE_CENTER_Z + sinf(ang) * rDist;
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
            .grassRootColor = {92, 72, 42, 255},
            .grassTipColor = {178, 148, 86, 255},
            .bladesPerClump = 4,
            .bladeSegments = 3,
            .bladeWidthScale = 0.088f,
            .lodDistance = 24.0f,
            .midLodDistance = 20.0f,
            .drawDistance = 42.0f,
            .shadowDistance = 0.0f,
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
    ColorGradeLut_UseNeutral();

    // Sun-Bleached Golden Afternoon (Ninh Thuan Coastal Semi-Arid Stone Park)
    static const EnvLightingPreset kNinhThuanToD[4] = {
        {
            .ambientColor = {104, 96, 88, 255},
            .sunColor = {255, 206, 148, 255},
            .sunDirection = {0.82f, -0.30f, -0.49f},
            .shadowColor = {48, 44, 48, 140},
            .fog = {.enabled = true, .color = {238, 214, 182, 255}, .start = 24.0f, .end = 185.0f, .density = 0.0085f},
            .atmosphere = {
                .color = {238, 214, 182, 255}, .start = 24.0f, .end = 185.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0092f, 0.0148f, 0.0255f}, .mieScattering = 0.0052f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.1f},
                .density = {.baseDensity = 0.0085f, .heightFalloff = 0.34f, .baseAltitude = -1.2f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {122, 118, 114, 255},
            .sunColor = {255, 240, 208, 255},
            .sunDirection = {0.34f, -0.88f, -0.33f},
            .shadowColor = {56, 54, 58, 150},
            .fog = {.enabled = true, .color = {236, 224, 204, 255}, .start = 28.0f, .end = 210.0f, .density = 0.0068f},
            .atmosphere = {
                .color = {236, 224, 204, 255}, .start = 28.0f, .end = 210.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0082f, 0.0138f, 0.0240f}, .mieScattering = 0.0038f, .mieAnisotropy = 0.76f, .multipleScatteringAmp = 1.9f},
                .density = {.baseDensity = 0.0068f, .heightFalloff = 0.32f, .baseAltitude = -1.2f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {112, 108, 106, 255},
            .sunColor = {255, 224, 174, 255},
            .sunDirection = {0.62f, -0.56f, -0.55f},
            .shadowColor = {50, 48, 54, 150},
            .fog = {.enabled = true, .color = {240, 218, 188, 255}, .start = 26.0f, .end = 195.0f, .density = 0.0078f},
            .atmosphere = {
                .color = {240, 218, 188, 255}, .start = 26.0f, .end = 195.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0088f, 0.0145f, 0.0250f}, .mieScattering = 0.0046f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.0078f, .heightFalloff = 0.34f, .baseAltitude = -1.2f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {86, 80, 92, 255},
            .sunColor = {255, 168, 108, 255},
            .sunDirection = {-0.84f, -0.24f, -0.49f},
            .shadowColor = {42, 38, 52, 145},
            .fog = {.enabled = true, .color = {242, 198, 166, 255}, .start = 20.0f, .end = 170.0f, .density = 0.010f},
            .atmosphere = {
                .color = {242, 198, 166, 255}, .start = 20.0f, .end = 170.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0110f, 0.0168f, 0.0290f}, .mieScattering = 0.0060f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 2.3f},
                .density = {.baseDensity = 0.0100f, .heightFalloff = 0.36f, .baseAltitude = -1.2f, .enableSigmoidLayer = false}
            }
        }
    };
    static const float kNinhThuanTimes[4] = {0.25f, 0.50f, 0.64f, 0.7708f};
    Environment_SetTimeOfDayPresets(kNinhThuanToD, kNinhThuanTimes, 4);
    Environment_SetTimeOfDay(0.64f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){112, 108, 106, 255});
    Environment_SetSunColor((Color){255, 224, 174, 255});
    Environment_SetSunIntensity(2.25f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.62f, -0.56f, -0.55f}));
    Environment_SetShadowColor((Color){50, 48, 54, 150});

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.00f,
        .contrast = 1.10f,
        .saturation = 1.04f,
        .bloomIntensity = 0.14f,
        .shadowTint = {0.95f, 0.94f, 0.98f},
        .highlightTint = {1.06f, 1.01f, 0.92f},
        .bloomThreshold = 1.18f,
        .bloomKnee = 0.42f,
        .hueRestore = 0.60f,
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);

    AtmosphereProfile atmos = kNinhThuanToD[2].atmosphere;
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(0.35f);
    VolumetricFog_SetDistantCoverage(0.45f);
    Environment_SetCloudShadowConfig(NULL);
    if (s_skipMask & MAP_SKIP_FOGVOLUMES)
        FogVolume_ClearAll();
    else
        SpawnNinhThuanDustHaze();

    // Activate Earth/Arid Biome Shaders (Terrain, Rock, Cliff)
    MapGroundBiomeProfile earthBiome = {
        .aridFactor = 1.0f,
        .strataStrength = 0.90f,
        .grusColor = {0.49f, 0.43f, 0.35f},
        .hardpanColor = {0.36f, 0.30f, 0.23f},
        .lichenColor = {0.46f, 0.38f, 0.21f},
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
        CaptureNinhThuanStaticShadows();
        MapManager_SetZones(NINH_THUAN_ZONES, NINH_THUAN_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = kMapCenter;
    EnvShadow_SetFocus(kMapCenter, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, NINH_THUAN_HEIGHT_RANGE, CLIFF_DEPTH,
                      "assets/maps/ninh_thuan/ninh_thuan_ecology_splat.png",
                      "assets/maps/ninh_thuan/ninh_thuan_water_height.png");

    s_ground = MapProp_CreateGroundHeightfield16(
        "assets/maps/ninh_thuan/ninh_thuan_heightfield.png", MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + NINH_THUAN_HEIGHT_RANGE, CLIFF_DEPTH, 3.6f,
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

    MapProp_SetGroundTint(&s_ground, (Color){168, 134, 96, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/textures/dirt_material.png",
                                 "assets/textures/dirt_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/textures/verdant_terrain_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/ninh_thuan/ninh_thuan_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rock_diffuse.png",
                              "assets/textures/rock_normal.png");

    s_sky = MapProp_CreateSkyDome();
    s_cloudSea = MapProp_CreateCloudSea(1400.0f, 1400.0f, 65.0f);
    MapProp_SetCloudSeaGroundBoundary(
        &s_cloudSea, &s_ground, -4.85f,
        &(MapBoundaryMistStyle){
            .innerWidth = 1.2f,
            .outerWidth = 3.6f,
            .heightOffset = 0.22f,
            .outerDrop = 0.42f,
            .opacity = 0.28f,
        });
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f, -26.0f);

    s_oasis = MapProp_CreateTerrainWater("assets/maps/ninh_thuan/ninh_thuan_water.obj",
                                         (MapWaterConfig){
                                             .ecosystem = WATER_ECO_CUSTOM,
                                             .center = {NINH_THUAN_LAKE_CENTER_X, 0.0f, NINH_THUAN_LAKE_CENTER_Z},
                                             .radiusX = NINH_THUAN_LAKE_RADIUS_X,
                                             .radiusZ = NINH_THUAN_LAKE_RADIUS_Z,
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
    MapProp_SetWaterTerrainSource(&s_oasis, NinhThuanOasisBed, NinhThuanOasisSurface, NULL);

    BuildNinhThuanRocks();
    BuildNinhThuanFlora();

    s_ready = true;
    EnvShadow_SetMapCasterCallback(DrawNinhThuanShadowCasters, NULL);
    EnvShadow_InvalidateStaticCache();
    CaptureNinhThuanStaticShadows();
    MapManager_SetZones(NINH_THUAN_ZONES, NINH_THUAN_ZONE_COUNT);
}

void DrawNinhThuanMap(void)
{
    if (!s_ready)
        return;
    MapProp_ResetNatureRenderStats();
    PropLit_UpdateLighting();

    bool useFakeRockShadows = !EnvShadow_IsEnabled();
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

    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
}

void DrawTransparentNinhThuanMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_CLOUDSEA))
        MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
    if (!(s_skipMask & MAP_SKIP_WATER))
        MapProp_DrawWaterOverlay(&s_oasis, (float)fmod(GetTime(), 3600.0));
}

void UpdateNinhThuanMap(float dt)
{
    if (!s_ready)
        return;

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_oasis, dt);

    float dx = camera.target.x - s_lastShadowFocus.x;
    float dz = camera.target.z - s_lastShadowFocus.z;
    if (dx * dx + dz * dz > 16.0f)
    {
        s_lastShadowFocus = camera.target;
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
    }
    CaptureNinhThuanStaticShadows();
}

void UnloadNinhThuanMap(void)
{
    Wind_SetMacro(&s_prevWindMacro);
    ColorGradeLut_UseNeutral();
    PostFX_ClearMapProfile();
    Environment_SetSunIntensity(1.0f);
    MapProp_SetGroundBiomeProfile(NULL);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);

    if (!s_ready)
        return;

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    Environment_SetCloudShadowConfig(NULL);
    VolumetricFog_SetDistantCoverage(1.0f);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadWaterSurface(&s_oasis);
    MapProp_UnloadFloraSystem(&s_flora);
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

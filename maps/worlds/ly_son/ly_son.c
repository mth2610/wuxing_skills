#include "maps/worlds/ly_son/ly_son.h"
#include "assets/maps/ly_son/ly_son_generated_meta.h"
#include "maps/worlds/ly_son/ly_son_rocks_generated.h"
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
static float s_mapWidth = LY_SON_MAP_WIDTH;
static float s_mapDepth = LY_SON_MAP_DEPTH;
static float s_heightRange = LY_SON_HEIGHT_RANGE;

#define MAP_WIDTH s_mapWidth
#define MAP_DEPTH s_mapDepth
#define CLIFF_DEPTH (LY_SON_CLIFF_DEPTH * s_scaleY)
#define LY_SON_SEA_Y -1.25f
#define SHADOW_FOCUS_RADIUS 22.0f
#define SHADOW_SNAP_STEP 0.25f
#define SHADOW_HYSTERESIS_XZ 0.50f
#define SHADOW_HYSTERESIS_Y 0.50f

static Vector3 kMapCenter = {LY_SON_MAP_WIDTH * 0.5f, 0.0f, LY_SON_MAP_DEPTH * 0.5f};
static Vector3 s_staticShadowCenter = {-9999.0f, 0.0f, -9999.0f};
static Vector3 s_floraWindowCenter = {LY_SON_ARENA_X, 0.0f, LY_SON_ARENA_Z};

static MapDemIsland s_dem = {0};
static MapGroundSurface s_ground = {0};
static MapEcology s_habitat = {0};
static MapSkyDome s_sky = {0};
static MapWaterSurface s_coastalSea = {0};
static MapCliffSkirt s_cliff = {0};
static MapLavaSurface s_lava = {0};
static MapVolcanoVentSystem s_vents = {0};
static MapRockSystem s_rocks = {0};
static MapDebrisSystem s_debris = {0};
static MapFloraSystem s_flora = {0};

static WindMacroConfig s_prevWindMacro = {0};
static AtmosphereMode s_prevAtmoMode = ATMO_MODE_MOONLIGHT_DUST;
static Vector3 s_lastShadowFocus = {0};
static unsigned int s_skipMask = 0;
static bool s_shadowWasEnabled = false;
static bool s_ready = false;

static MapZone s_lySonZones[3];
#define LY_SON_ZONE_COUNT 3

static float LySonCoastalBed(float x, float z, void *user)
{
    (void)user;
    if (x < 0.0f || x > MAP_WIDTH || z < 0.0f || z > MAP_DEPTH)
        return -4.8f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static float LySonCoastalSurface(float x, float z, void *user)
{
    (void)x;
    (void)z;
    (void)user;
    return LY_SON_SEA_Y * s_scaleY;
}

static void DrawLySonShadowCasters(Shader depthShader, void *user)
{
    (void)user;
    if (s_skipMask & MAP_SKIP_SHADOWCASTERS)
        return;

    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraShadowCasters(&s_flora);

    if ((s_skipMask & MAP_SKIP_ROCKS) || EnvShadow_HasStaticCache() ||
        !s_rocks.ready || !s_rocks.items || s_rocks.count <= 0)
        return;

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
            arch = MAP_ROCK_BASALT;
        unsigned int vh = (unsigned int)i * 2654435761u ^
                          (unsigned int)((int)(p->position.x * 13.0f) * 73856093) ^
                          (unsigned int)((int)(p->position.z * 17.0f) * 19349663);
        int variant = (int)(vh & 3u);
        const MapRockSet *set = (variant > 0 && variant < MAP_ROCK_VARIANTS_PER_ARCHETYPE && s_rocks.variants[arch][variant - 1].ready)
                                    ? &s_rocks.variants[arch][variant - 1]
                                    : &s_rocks.models[arch];
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

static void CaptureLySonStaticShadows(void)
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

static float LySonScorchedGrassDensity(float x, float z, void *user)
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

    // Sparse scorched grass tufts surviving in sheltered ash pockets away from active magma
    float patch = sinf(x * 0.16f + z * 0.12f) * cosf(z * 0.19f - x * 0.14f);
    float pocket = (patch > 0.36f) ? ((patch - 0.36f) * 0.90f) : 0.0f;
    return fminf(0.68f, pocket);
}

static void BuildLySonRocks(void)
{
    s_rocks = MapProp_CreateRockSystem(
        "assets/textures/rocks/basalt_diffuse.png",
        "assets/textures/rocks/basalt_normal.png",
        "assets/textures/rocks/basalt_roughness.png",
        LY_SON_ROCK_COUNT + 16);

    for (int i = 0; i < LY_SON_ROCK_COUNT; i++)
    {
        const MapRockItem *item = &LY_SON_ROCK_ITEMS[i];
        Vector3 pos = {item->position.x * s_scaleXZ, item->position.y * s_scaleY, item->position.z * s_scaleXZ};
        float rScale = item->radiusScale * (1.0f + 0.35f * (s_scaleXZ - 1.0f));
        float hScale = item->heightScale * (1.0f + 0.25f * (s_scaleY - 1.0f));
        Vector3 gPos = pos, gNorm = {0.0f, 1.0f, 0.0f};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, pos.x, pos.z, &gPos, &gNorm))
        {
            // Keep elevated arch capstones (Cổng Tò Vò lintels) at their relative arch height
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

#define LY_SON_MAX_GRASS 9000
#define LY_SON_MAX_FLOWERS 360
static MapMeadowPlacement s_grassPlacements[LY_SON_MAX_GRASS];
static int s_grassCount = 0;
static MapFlowerPlacement s_flowerPlacements[LY_SON_MAX_FLOWERS];
static int s_flowerCount = 0;
static MapDebrisPlacement s_scaledDebris[LY_SON_DEBRIS_COUNT > 0 ? LY_SON_DEBRIS_COUNT : 1];

static void BuildLySonDebris(void)
{
    int count = LY_SON_DEBRIS_COUNT;
    if (count <= 0)
        return;
    for (int i = 0; i < count; i++)
    {
        s_scaledDebris[i] = LY_SON_DEBRIS_ITEMS[i];
        s_scaledDebris[i].position.x *= s_scaleXZ;
        s_scaledDebris[i].position.z *= s_scaleXZ;
        Vector3 gp = {0}, gn = {0};
        if (MapProp_SampleGroundSurface(&s_ground, kMapCenter, s_scaledDebris[i].position.x, s_scaledDebris[i].position.z, &gp, &gn))
            s_scaledDebris[i].position.y = gp.y;
        else
            s_scaledDebris[i].position.y *= s_scaleY;
        s_scaledDebris[i].scale *= (1.0f + 0.18f * (s_scaleXZ - 1.0f));
    }
    s_debris = MapProp_CreateDebrisSystem(s_scaledDebris, count, &s_ground, kMapCenter, 36.0f * s_scaleXZ);
}

static float LySonFlowerHeight(float x, float z, void *ctx)
{
    (void)ctx;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

static bool LySonFlowerExcluded(float x, float z, void *ctx)
{
    (void)ctx;
    Vector3 gp = {0}, gn = {0};
    float wy = 0.0f, wd = 0.0f;
    if (!MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, &gp, &gn))
        return true;
    if (gn.y < 0.78f || gp.y < 0.25f)
        return true;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, &wy, &wd);
}

static void BuildLySonFlora(void)
{
    Vector2 minB = {14.0f * s_scaleXZ, 14.0f * s_scaleXZ};
    Vector2 maxB = {MAP_WIDTH - 14.0f * s_scaleXZ, MAP_DEPTH - 14.0f * s_scaleXZ};
#ifdef __ANDROID__
    const float grassSpacing = 1.35f;
    const int maxGrass = 2400;
#else
    float grassSpacing = 0.72f;
    const int maxGrass = LY_SON_MAX_GRASS;
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
        .seed = 0x71F0E3u,
    };
    s_grassCount = MapProp_GenerateMeadowPlacements(
        s_grassPlacements, maxGrass,
        &s_ground, kMapCenter,
        dist, LySonScorchedGrassDensity, NULL);

    // Sparse crimson Bỉ Ngạn Hoa (Spider Lily) colonies around sheltered obsidian arena margins
    static MapFloraFlowerCluster s_flowerClusters[6];
    const int clusterCount = 6;
    const int flowersPerCluster = 45;
    const float arenaCx = LY_SON_ARENA_X * s_scaleXZ;
    const float arenaCz = LY_SON_ARENA_Z * s_scaleXZ;
    for (int i = 0; i < clusterCount; i++)
    {
        float ang = (float)i * (6.2831853f / (float)clusterCount) + 0.35f;
        float distR = (17.5f + (float)(i % 2) * 3.5f) * s_scaleXZ;
        float fx = arenaCx + cosf(ang) * distR;
        float fz = arenaCz + sinf(ang) * distR;
        float fy = MapDem_GetGroundHeight(&s_dem, &s_ground, fx, fz);
        s_flowerClusters[i] = (MapFloraFlowerCluster){
            .center = {fx, fy, fz},
            .radius = {3.2f * s_scaleXZ, 2.8f * s_scaleXZ},
            .flowerCount = flowersPerCluster,
            .primarySpecies = MAP_FLOWER_SPECIES_BI_NGAN,
            .species = {MAP_FLOWER_SPECIES_BI_NGAN, MAP_FLOWER_SPECIES_BI_NGAN, MAP_FLOWER_SPECIES_BI_NGAN, MAP_FLOWER_SPECIES_BI_NGAN},
            .primaryColor = {212, 28, 34, 255},
            .accentColor = {245, 72, 42, 255},
            .discalCenterColor = {255, 190, 68, 255},
        };
    }
    s_flowerCount = clusterCount * flowersPerCluster;
    MapFlora_GenerateFlowerPlacements(s_flowerPlacements, s_flowerCount, s_flowerClusters, clusterCount,
                                      LySonFlowerHeight, LySonFlowerExcluded, NULL, 0xB146A4u);

    s_flora = MapProp_CreateFloraSystem(
        s_grassPlacements, s_grassCount,
        NULL, 0,
        s_flowerPlacements, s_flowerClusters, clusterCount,
        (MapFloraStyle){
            .grassRootColor = {42, 34, 24, 255},
            .grassTipColor = {168, 134, 74, 255},
            .bladesPerClump = 5,
            .bladeSegments = 3,
            .bladeWidthScale = 0.125f,
            .lodDistance = 24.0f,
            .midLodDistance = 16.0f,
            .drawDistance = 44.0f,
            .shadowDistance = (s_scaleXZ > 1.5f) ? -1.0f : 22.0f,
            .grassState = MAP_GRASS_STATE_DRY,
            .dryness = 0.94f,
            .scorch = 0.0f,
            .botanicalVariation = 0.85f,
        });
}

void InitLySonMap(void)
{
    s_scaleXZ = MapManager_GetWorldScaleXZ();
    s_scaleY = MapManager_GetWorldScaleY();
    s_mapWidth = LY_SON_MAP_WIDTH * s_scaleXZ;
    s_mapDepth = LY_SON_MAP_DEPTH * s_scaleXZ;
    s_heightRange = LY_SON_HEIGHT_RANGE * s_scaleY;
    kMapCenter = (Vector3){s_mapWidth * 0.5f, 0.0f, s_mapDepth * 0.5f};
    s_staticShadowCenter = kMapCenter;
    s_floraWindowCenter = (Vector3){LY_SON_ARENA_X * s_scaleXZ, 0.0f, LY_SON_ARENA_Z * s_scaleXZ};

    s_lySonZones[0] = (MapZone){.center = {LY_SON_ARENA_X * s_scaleXZ, 0.0f, LY_SON_ARENA_Z * s_scaleXZ}, .radius = 28.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};
    s_lySonZones[1] = (MapZone){.center = {LY_SON_CALDERA_X * s_scaleXZ, 0.0f, LY_SON_CALDERA_Z * s_scaleXZ}, .radius = 36.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};
    s_lySonZones[2] = (MapZone){.center = {LY_SON_TOVO_X * s_scaleXZ, 0.0f, LY_SON_TOVO_Z * s_scaleXZ}, .radius = 24.0f * s_scaleXZ, .type = NAT_DESERT_ZONE};

    s_prevWindMacro = Wind_GetMacro();
    WindMacroConfig volcanicWind = {
        .baseDirection = {3.40f, 0.0f, 1.95f},
        .gustAmplitude = 1.65f,
        .noiseScale = 0.062f,
        .noiseSpeed = 1.95f,
        .terrainLiftK = 1.55f,
        .heightGradientK = 0.85f,
    };
    Wind_SetMacro(&volcanicWind);

    s_skipMask = MapProp_GetSkipMask();
    if (s_skipMask & MAP_SKIP_GRADE)
        ColorGradeLut_UseNeutral();
    else if (!ColorGradeLut_Load("assets/maps/ly_son/ly_son_grade.png"))
        ColorGradeLut_UseNeutral();

    // 5 Volcanic Time-of-Day Presets (Default: 16:30 Cold-vs-Hot Volcanic Ash Dusk)
    static const EnvLightingPreset kLySonToD[5] = {
        {
            .ambientColor = {68, 80, 102, 255},
            .sunColor = {255, 160, 106, 255},
            .sunDirection = {0.82f, -0.26f, -0.51f},
            .shadowColor = {24, 28, 42, 175},
            .fog = {.enabled = true, .color = {88, 94, 112, 255}, .start = 26.0f, .end = 185.0f, .density = 0.0068f},
            .atmosphere = {
                .color = {88, 94, 112, 255}, .start = 26.0f, .end = 185.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0110f, 0.0135f, 0.0220f}, .mieScattering = 0.0062f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.2f},
                .density = {.baseDensity = 0.0065f, .heightFalloff = 0.28f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {84, 96, 118, 255},
            .sunColor = {255, 220, 182, 255},
            .sunDirection = {0.32f, -0.88f, -0.35f},
            .shadowColor = {32, 36, 52, 165},
            .fog = {.enabled = true, .color = {112, 120, 138, 255}, .start = 36.0f, .end = 220.0f, .density = 0.0048f},
            .atmosphere = {
                .color = {112, 120, 138, 255}, .start = 36.0f, .end = 220.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0090f, 0.0125f, 0.0210f}, .mieScattering = 0.0045f, .mieAnisotropy = 0.78f, .multipleScatteringAmp = 2.0f},
                .density = {.baseDensity = 0.0048f, .heightFalloff = 0.24f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {64, 78, 104, 255},
            .sunColor = {255, 166, 94, 255},
            .sunDirection = {0.68f, -0.34f, -0.65f},
            .shadowColor = {22, 26, 40, 180},
            .fog = {.enabled = true, .color = {86, 94, 114, 255}, .start = 28.0f, .end = 195.0f, .density = 0.0058f},
            .atmosphere = {
                .color = {86, 94, 114, 255}, .start = 28.0f, .end = 195.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0108f, 0.0138f, 0.0225f}, .mieScattering = 0.0064f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 2.25f},
                .density = {.baseDensity = 0.0058f, .heightFalloff = 0.28f, .baseAltitude = -2.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {56, 62, 86, 255},
            .sunColor = {255, 118, 66, 255},
            .sunDirection = {-0.84f, -0.22f, -0.49f},
            .shadowColor = {20, 22, 36, 175},
            .fog = {.enabled = true, .color = {82, 82, 102, 255}, .start = 24.0f, .end = 180.0f, .density = 0.0070f},
            .atmosphere = {
                .color = {82, 82, 102, 255}, .start = 24.0f, .end = 180.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0125f, 0.0145f, 0.0240f}, .mieScattering = 0.0068f, .mieAnisotropy = 0.82f, .multipleScatteringAmp = 2.3f},
                .density = {.baseDensity = 0.0068f, .heightFalloff = 0.28f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        },
        {
            .ambientColor = {26, 30, 48, 255},
            .sunColor = {168, 184, 232, 255},
            .sunDirection = {0.50f, -0.46f, -0.73f},
            .shadowColor = {8, 10, 20, 215},
            .fog = {.enabled = true, .color = {22, 24, 38, 255}, .start = 24.0f, .end = 180.0f, .density = 0.0072f},
            .atmosphere = {
                .color = {22, 24, 38, 255}, .start = 24.0f, .end = 180.0f, .enabled = true,
                .optics = {.rayleighLMS = {0.0055f, 0.0072f, 0.0165f}, .mieScattering = 0.0050f, .mieAnisotropy = 0.80f, .multipleScatteringAmp = 1.85f},
                .density = {.baseDensity = 0.0058f, .heightFalloff = 0.26f, .baseAltitude = -3.0f, .enableSigmoidLayer = false}
            }
        }
    };
    static const float kLySonTimes[5] = {0.25f, 0.50f, 0.68f, 0.78f, 0.92f};
    Environment_SetTimeOfDayPresets(kLySonToD, kLySonTimes, 5);
    Environment_SetTimeOfDay(0.68f);
    Environment_SetTimeOfDaySpeed(0.0f);
    Environment_SetAmbientColor((Color){64, 78, 104, 255});
    Environment_SetSunColor((Color){255, 166, 94, 255});
    Environment_SetSunIntensity(1.95f);
    Environment_SetSunDirection(Vector3Normalize((Vector3){0.68f, -0.34f, -0.65f}));
    Environment_SetShadowColor((Color){22, 26, 40, 180});
    if (Environment_IsNightMode())
        Environment_ApplyMoonlightNight();

    PostFXMapProfile postProfile = {
        .enabled = true,
        .exposure = 1.04f,
        .contrast = 1.14f,
        .saturation = 1.08f,
        .bloomIntensity = 0.26f,
        .shadowTint = {0.88f, 0.90f, 1.06f},
        .highlightTint = {1.10f, 0.96f, 0.84f},
        .bloomThreshold = 1.05f,
        .bloomKnee = 0.44f,
        .hueRestore = 0.62f,
    };
    if (s_skipMask & MAP_SKIP_GRADE)
        PostFX_ClearMapProfile();
    else
        PostFX_SetMapProfile(postProfile);

    AtmosphereProfile atmos = kLySonToD[2].atmosphere;
    atmos.enabled = false;
    Environment_SetAtmosphereProfile(&atmos);
    VolumetricFog_SetGodRayIntensity(1.15f);
    VolumetricFog_SetDistantCoverage(0.55f);
    s_prevAtmoMode = Atmosphere_GetMode();
    Atmosphere_SetMode(ATMO_MODE_WAR_EMBERS);
    Atmosphere_Configure(s_floraWindowCenter, (Vector3){32.0f, 4.5f, 32.0f}, 380, (Color){255, 132, 48, 255});
    PostFX_SetHeatHaze(0.0018f, 0.15f, 0.85f);

    EnvCloudShadowConfig cloudConfig = {
        .enabled = true,
        .strength = 0.28f,
        .worldSize = 155.0f,
        .planeHeight = 85.0f,
        .coverage = 0.40f,
        .softness = 0.32f,
        .windSpeedScale = 0.85f,
    };
    Environment_SetCloudShadowConfig((s_skipMask & MAP_SKIP_CLOUDSHADOW) ? NULL : &cloudConfig);

    // Activate Volcanic Fire-Element Biome Shaders
    MapGroundBiomeProfile volcanicBiome = {
        .aridFactor = 0.0f,
        .strataStrength = 0.85f,
        .grusColor = {0.22f, 0.20f, 0.21f},
        .hardpanColor = {0.16f, 0.15f, 0.16f},
        .lichenColor = {0.58f, 0.46f, 0.16f},
        .volcanicFactor = 1.0f,
        .magmaGlowStrength = 1.35f,
        .ashColor = {0.15f, 0.14f, 0.15f},
        .scoriaColor = {0.34f, 0.13f, 0.09f},
        .sulfurColor = {0.64f, 0.50f, 0.16f},
    };
    MapProp_SetGroundBiomeProfile(&volcanicBiome);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetRockVolcanicMode(1.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(1.0f);
    MapProp_SetCliffSeaLevel(LY_SON_SEA_Y * s_scaleY);
    MapProp_SetGrassState(MAP_GRASS_STATE_DRY, 0.65f);
    {
        MapGrassCondition dryGrass = MapProp_GetGrassCondition();
        dryGrass.dryness = 0.94f;
        dryGrass.scorch = 0.0f;
        dryGrass.emberIntensity = 0.65f;
        MapProp_SetGrassCondition(dryGrass);
    }

    if (s_ready)
    {
        EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        EnvShadow_SetMapCasterCallback(DrawLySonShadowCasters, NULL);
        EnvShadow_InvalidateStaticCache();
        CaptureLySonStaticShadows();
        MapManager_SetZones(s_lySonZones, LY_SON_ZONE_COUNT);
        return;
    }

#if !defined(__ANDROID__)
    s_shadowWasEnabled = EnvShadow_IsEnabled();
    EnvShadow_SetEnabled(true);
#endif
    s_lastShadowFocus = camera.target;
    EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);

    MapDem_InitIsland(&s_dem, MAP_WIDTH, MAP_DEPTH, s_heightRange, CLIFF_DEPTH,
                      "assets/maps/ly_son/ly_son_ecology_splat.png",
                      "assets/maps/ly_son/ly_son_water_height.png");

    const char *hfPath = (s_scaleXZ > 1.5f && FileExists("assets/maps/ly_son/ly_son_heightfield_ow.png"))
                             ? "assets/maps/ly_son/ly_son_heightfield_ow.png"
                             : "assets/maps/ly_son/ly_son_heightfield.png";
    s_ground = MapProp_CreateGroundHeightfield16(
        hfPath, MAP_WIDTH, MAP_DEPTH,
        CLIFF_DEPTH + s_heightRange, CLIFF_DEPTH, 44.0f,
        "assets/maps/ly_son/ly_son_ecology_splat.png",
        "assets/maps/ly_son/textures/pahoehoe_crust_diffuse.png",
        "assets/maps/ly_son/textures/volcanic_ash_diffuse.png");

    s_habitat.habitatTexture = ResourceManager_LoadTextureVariant(
        "assets/maps/ly_son/ly_son_habitat.png", true,
        TEXTURE_FILTER_TRILINEAR, TEXTURE_WRAP_CLAMP);
    {
        Image distImg = LoadImage("assets/maps/ly_son/ly_son_habitat_distance.png");
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

    MapProp_SetGroundTint(&s_ground, (Color){142, 134, 132, 255});
    MapProp_SetGroundSurfaceMaps(&s_ground,
                                 "assets/maps/ly_son/textures/pahoehoe_crust_material.png",
                                 "assets/maps/ly_son/textures/volcanic_ash_material.png");
    MapProp_SetGroundReliefMap(&s_ground, "assets/maps/ly_son/ly_son_relief.png");
    MapProp_SetGroundDebrisMap(&s_ground, "assets/maps/ly_son/ly_son_debris_map.png");
    MapProp_SetGroundRockMaps(&s_ground,
                              "assets/textures/rocks/basalt_diffuse.png",
                              "assets/textures/rocks/basalt_normal.png");

    s_sky = MapProp_CreateSkyDome();
    if (!(s_skipMask & MAP_SKIP_CLIFF))
        s_cliff = MapProp_CreateCliffSkirt(&s_ground, kMapCenter, -0.45f * s_scaleY, -26.0f * s_scaleY);

    const float seaY = LY_SON_SEA_Y * s_scaleY;
    const float seaSpan = (s_scaleXZ > 1.5f) ? (620.0f * s_scaleXZ) : 840.0f;
    const int seaSegs = (s_scaleXZ > 1.5f) ? 84 : 72;
    s_coastalSea = MapProp_CreateWaterPatch(
        (Vector3){kMapCenter.x, seaY, kMapCenter.z},
        seaSpan, seaSpan,
        (MapWaterConfig){
            .ecosystem = WATER_ECO_CUSTOM,
            .segments = seaSegs,
            .rings = seaSegs,
            .waveHeight = 0.30f,
            .waveScale = 0.45f,
            .waveSpeed = 1.25f,
            .flowVelocity = {0.22f, 0.16f},
            .detailScale = 0.24f,
            .detailStrength = 0.42f,
            .maxDepth = 4.4f,
            .absorption = {0.62f, 0.22f, 0.12f},
            .scatterColor = {0.06f, 0.42f, 0.46f},
            .scatterCoeff = 0.42f,
            .causticsStrength = 0.32f,
            .causticsScale = 1.40f,
            .foamThreshold = 0.30f,
            .deepColor = {6, 32, 56, 255},
            .shallowColor = {20, 118, 126, 235},
            .foamColor = {246, 240, 232, 235},
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
    MapProp_SetWaterTerrainSource(&s_coastalSea, LySonCoastalBed, LySonCoastalSurface, NULL);

    // Conformal Molten Magma Lakes & Radial Lava Rivers
    s_lava = MapProp_CreateTerrainLava("assets/maps/ly_son/ly_son_water.obj",
                                         (MapLavaConfig){
                                             .center = {LY_SON_CALDERA_X * s_scaleXZ, 0.0f, LY_SON_CALDERA_Z * s_scaleXZ},
                                             .radiusX = LY_SON_CALDERA_RX * s_scaleXZ,
                                             .radiusZ = LY_SON_CALDERA_RZ * s_scaleXZ,
                                             .flowSpeed = 0.30f,
                                             .flowDirection = {-0.65f, 0.75f},
                                             .crustCoverage = 0.62f,
                                             .glowIntensity = 2.60f,
                                             .pulseSpeed = 0.90f,
                                             .waveHeight = 0.025f,
                                             .coreColor = {255, 228, 122, 255},
                                             .magmaColor = {255, 82, 16, 255},
                                             .crustColor = {32, 29, 34, 255},
                                             .flowMapPath = "assets/maps/ly_son/ly_son_lava_flow.png",
                                             .mapWidth = MAP_WIDTH,
                                             .mapDepth = MAP_DEPTH,
                                         });
    if (s_lava.ready && (s_scaleXZ != 1.0f || s_scaleY != 1.0f))
    {
        s_lava.model.transform = MatrixScale(s_scaleXZ, s_scaleY, s_scaleXZ);
        s_lava.boundsCenter = (Vector3){
            s_lava.boundsCenter.x * s_scaleXZ,
            s_lava.boundsCenter.y * s_scaleY,
            s_lava.boundsCenter.z * s_scaleXZ};
        s_lava.boundsRadius *= s_scaleXZ;
    }

    BuildLySonRocks();
    BuildLySonDebris();
    BuildLySonFlora();

    FogVolume_ClearAll();
    if (!(s_skipMask & MAP_SKIP_FOGVOLUMES))
    {
        MapVolcanoVent scaledVents[16];
        int vCount = (LY_SON_VENT_COUNT > 16) ? 16 : LY_SON_VENT_COUNT;
        for (int i = 0; i < vCount; i++)
        {
            scaledVents[i] = LY_SON_VOLCANO_VENTS[i];
            scaledVents[i].position.x *= s_scaleXZ;
            scaledVents[i].position.z *= s_scaleXZ;
            scaledVents[i].position.y = MapDem_GetGroundHeight(&s_dem, &s_ground, scaledVents[i].position.x, scaledVents[i].position.z);
            scaledVents[i].radius *= (1.0f + 0.45f * (s_scaleXZ - 1.0f));
        }
        s_vents = MapProp_CreateVolcanoVentSystem(scaledVents, vCount);
    }

    s_ready = true;
    EnvShadow_SetMapCasterCallback(DrawLySonShadowCasters, NULL);
    EnvShadow_InvalidateStaticCache();
    if (s_scaleXZ > 1.5f)
    {
        s_staticShadowCenter = s_floraWindowCenter;
        EnvShadow_InvalidateStaticCache();
    }
    CaptureLySonStaticShadows();
    MapManager_SetZones(s_lySonZones, LY_SON_ZONE_COUNT);
    extern void ResolveCollisionLySonMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY);
    MapManager_RegisterCollisionHooks("LY_SON", ResolveCollisionLySonMap);
}

void DrawLySonMap(void)
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
        MapProp_DrawLavaSurface(&s_lava, t);
    }
    if (!(s_skipMask & MAP_SKIP_FLORA))
        MapProp_DrawFloraSystem(&s_flora);
    if (!(s_skipMask & MAP_SKIP_SKY))
        MapProp_DrawSkyDome(&s_sky);
}

void DrawTransparentLySonMap(void)
{
    if (!s_ready)
        return;
    if (!(s_skipMask & MAP_SKIP_WATER))
    {
        float t = (float)fmod(GetTime(), 3600.0);
        MapProp_DrawWaterOverlay(&s_coastalSea, t);
    }
}

void UpdateLySonMap(float dt)
{
    if (!s_ready)
        return;

    MapProp_UpdateFloraSystem(&s_flora, camera.target, dt);
    MapProp_UpdateWaterSurface(&s_coastalSea, dt);
    MapProp_UpdateVolcanoVentSystem(&s_vents, camera.target, dt);

    float rawDx = fabsf(camera.target.x - s_lastShadowFocus.x);
    float rawDy = fabsf(camera.target.y - s_lastShadowFocus.y);
    float rawDz = fabsf(camera.target.z - s_lastShadowFocus.z);
    if (rawDx > SHADOW_HYSTERESIS_XZ || rawDz > SHADOW_HYSTERESIS_XZ || rawDy > SHADOW_HYSTERESIS_Y)
    {
        Vector3 focus = MapProp_SnapShadowFocus(camera.target, SHADOW_SNAP_STEP);
        if (fabsf(focus.x - s_lastShadowFocus.x) > 0.01f ||
            fabsf(focus.y - s_lastShadowFocus.y) > 0.01f ||
            fabsf(focus.z - s_lastShadowFocus.z) > 0.01f)
        {
            s_lastShadowFocus = focus;
            EnvShadow_SetFocus(s_lastShadowFocus, SHADOW_FOCUS_RADIUS);
        }
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
    CaptureLySonStaticShadows();
}

void UnloadLySonMap(void)
{
    Wind_SetMacro(&s_prevWindMacro);
    Atmosphere_SetMode(s_prevAtmoMode);
    ColorGradeLut_UseNeutral();
    PostFX_ClearMapProfile();
    PostFX_SetHeatHaze(0.0f, 0.0f, 0.0f);
    Environment_SetSunIntensity(1.0f);
    MapProp_SetGroundBiomeProfile(NULL);
    MapProp_SetRockAridMode(0.0f);
    MapProp_SetRockVolcanicMode(0.0f);
    MapProp_SetCliffAridMode(0.0f);
    MapProp_SetCliffVolcanicMode(0.0f);

    if (!s_ready)
        return;

    EnvShadow_SetMapCasterCallback(NULL, NULL);
    EnvShadow_InvalidateStaticCache();
    VolumetricFog_SetDistantCoverage(1.0f);
    MapProp_UnloadVolcanoVentSystem(&s_vents);
    FogVolume_ClearAll();

    MapProp_ClearNatureInteraction();
    MapProp_SetGroundEcology(&s_ground, NULL);

    MapProp_UnloadLavaSurface(&s_lava);
    MapProp_UnloadWaterSurface(&s_coastalSea);
    MapProp_UnloadFloraSystem(&s_flora);
    MapProp_UnloadDebrisSystem(&s_debris);
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

float GetGroundHeightLySonMap(float x, float z)
{
    if (!s_ready)
        return 0.0f;
    return MapDem_GetGroundHeight(&s_dem, &s_ground, x, z);
}

bool SampleGroundSurfaceLySonMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    if (!s_ready)
        return false;
    return MapDem_SampleGroundSurface(&s_dem, &s_ground, x, z, outPosition, outNormal);
}

bool GetWaterInfoLySonMap(float x, float z, float *outSurfaceY, float *outWaterDepth)
{
    if (!s_ready)
        return false;
    return MapDem_GetWaterInfo(&s_dem, &s_ground, x, z, outSurfaceY, outWaterDepth);
}

void SetWaterInteractorLySonMap(Vector3 position, Vector3 velocity, float radius)
{
    (void)position;
    (void)velocity;
    (void)radius;
}

void AddWaterRippleLySonMap(Vector3 position, float radius, float intensity)
{
    (void)position;
    (void)radius;
    (void)intensity;
}

void SetRainLySonMap(float intensity)
{
    (void)intensity;
}

void ResolveCollisionLySonMap(Vector3 *pos, float agentRadius, float stepHeight, float *inOutGroundY)
{
    if (!s_ready)
        return;
    MapProp_ResolveRockSystemCollision(&s_rocks, pos, agentRadius, stepHeight, inOutGroundY);
}

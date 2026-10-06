#include "vfx_test.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/camera_fx.h"
#include "core/time_fx.h"   // TimeFX_RawDelta — headless captures pin dt; GetFrameTime does not
#include "sandbox/auto_test.h"
#include "sandbox/colour_probe.h"
#include "sandbox/fresnel_probe.h"
#include "sandbox/gradient_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/decals/decal_system.h"
#include "core/particles/particle_system.h"
#include "core/screen_distort.h"
#include "core/trails/trail_system.h"
#include "core/vfx_light.h"
#include "core/post_fx.h"
#include "core/presets/vfx_presets.h"
#include "core/composition/visual_composer.h"
#include "core/material/material_system.h"
#include "core/skill_helper.h"
#include "core/path_spline.h"
#include "core/ribbon_strip.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "core/volumetric/volumetric_fog.h"

#define TEST_PATH_POINT_COUNT 16
static Vector3 s_testPathPoints[TEST_PATH_POINT_COUNT];
static bool s_hasTestPath = false;
#include "core/geometry/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "sandbox/sandbox_core.h" // Sandbox_GetPlayerAgentId — CHARACTER AURA attaches to the real player agent
#include "rlgl.h"
#include "raymath.h"
#include "core/geometry/sdf_capsule.h"
#include "core/surface_material.h"
#include "character/character_model.h"

// Messiah Engine Feature Demos (Items 2, 3, 4)
static bool    s_demoMeshDistortActive = false;
static float   s_demoMeshDistortTimer = 0.0f;
static Vector3 s_demoMeshDistortPos = {0};
static float   s_demoMeshDistortYaw = 0.0f;
static float   s_currentPlayerYaw = 0.0f;

void VFXTest_SetPlayerYaw(float yaw) {
    s_currentPlayerYaw = yaw;
}

static bool    s_demoSSSActive = false;
static float   s_demoSSSAngle = 0.0f;

static bool    s_demoMeshEmitterActive = false;
static int     s_demoMeshEmitterHandle = -1;
static Model   s_demoMeshEmitterModel = {0};

static bool    s_demoCatmullActive = false;
static float   s_demoCatmullTimer = 0.0f;
static Vector3 s_demoCatmullPos = {0};
static float   s_demoCatmullYaw = 0.0f;

static bool    s_demoIaidoActive = false;
static float   s_demoIaidoTimer = 0.0f;
static Vector3 s_demoIaidoPos = {0};
static float   s_demoIaidoYaw = 0.0f;
static bool    s_demoGuidingWindActive = false;
static float   s_demoGuidingWindTimer = 0.0f;
static Vector3 s_demoGuidingWindStartPos = {0};
static Vector3 s_demoGuidingWindTarget   = {0};
static int     s_demoGuidingWindCycle    = -1;
static Texture2D s_demoParticleTex = {0};

// Prefab Tester UI config
#define PREFAB_UI_X 20.0f
#define PREFAB_UI_Y 20.0f
#define PREFAB_BTN_W 140.0f
#define PREFAB_BTN_H 30.0f
#define PREFAB_BTN_SPACING 10.0f

typedef enum
{
    TEST_CAT_MESH = 0,
    TEST_CAT_NEWFX,
    TEST_CAT_COUNT
} PrefabTestCategory;

typedef enum
{
    NEWFX_CAT_FIRE = 0,
    NEWFX_CAT_WATER,
    NEWFX_CAT_WOOD,
    NEWFX_CAT_METAL,
    NEWFX_CAT_EARTH,
    NEWFX_CAT_TAIJI,
    NEWFX_CAT_COMMON,
    NEWFX_CAT_COUNT
} NewFXCategory;

static int s_testCategory = TEST_CAT_MESH;
static int s_testIndex = 0;
static bool s_isPlayingMesh = false;
static float s_meshTime = 0.0f;
static Vector3 s_prefabStartPos = {0};
/* Camera của khung hình, và cờ yêu cầu chụp. Việc chụp phải xảy ra SAU khi pass
 * 3D vẽ xong — LoadImageFromScreen giữa pass 3D bắt được một khung dở dang —
 * nên phím chỉ đặt cờ, còn ảnh do VFXTest_DrawHUD chụp. */
static Camera3D s_lastCam = {0};
static bool s_shotWanted = false;
static int s_shotSerial = 0;
static float s_shotRadius = 3.2f;

/* Mặc định HIỆN nhân vật để tiện test VFX và SSS/Aura trực tiếp trên model. */
static bool s_hideCharacterRef = false;
static bool VFXTest_IsNewFxNamed(const char *name);
bool VFXTest_ShouldHideCharacterRef(void)
{
    if (s_demoIaidoActive) return true;
    if (VFXTest_IsNewFxNamed("SILHOUETTE GLOW")) return true;
    if (VFXTest_IsNewFxNamed("IAIDO STANCE")) return true;
    return s_hideCharacterRef;
}

static Vector3 s_currentPlayerPos = {0};

static bool s_hideDebugOverlays = true;
bool VFXTest_ShouldHideDebugOverlays(void) { return s_hideDebugOverlays; }

/* Master switch — tabs, mesh/newfx buttons, FF/VF TEST circles, toggle/back
 * buttons, all of it. C/TAB/N/R above still work while this is on (they're
 * handled in UpdateAndHandleInput, not gated here), only the 2D drawing stops. */
static bool s_hideAllUI = false;

void VFXTest_SetCamera(Camera3D cam) { s_lastCam = cam; }

// Shared state for generated FOLLOWER fixtures. Every `.inl` gets one bench
// entry, and follower primaries get a stable transform/handle rather than being
// respawned every frame. The generator owns the per-entry details.
#define VFXTEST_FIXTURE_SLOTS 96
static Matrix s_vfxFixtureXf[VFXTEST_FIXTURE_SLOTS];
static int s_vfxFixtureHandle[VFXTEST_FIXTURE_SLOTS];
static float s_vfxFixtureLastTime[VFXTEST_FIXTURE_SLOTS];
static bool s_vfxFixturesReady = false;
// The tester supplies its current player model as a target only. The core mesh
// APIs take arbitrary Mesh/Model + Matrix and never query this state.
static Model s_meshParticleFixtureModel = {0};
static VFX_MeshParticleVariant s_meshParticleFixtureVariant = VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC;
static VFX_SmokeStyle s_smokeVolumeFixtureStyle = VFX_SMOKE_STYLE_ROIL;
static VFX_MeshSurfaceAuraVariant s_meshSurfaceAuraFixtureVariant = VFX_MESH_SURFACE_AURA_CYAN;
static VFX_DecalVariant s_decalFixtureVariant = VFX_DECAL_VARIANT_IMPACT;
static VFX_SurfaceParticleRingVariant s_surfaceParticleRingFixtureVariant = VFX_SURFACE_PARTICLE_RING_VARIANT_DUST;
static VFX_ImpactDustVariant s_impactDustFixtureVariant = VFX_IMPACT_DUST_VARIANT_DUST_PUFF;
static TrailPresetId s_motionRibbonFixturePreset = MOTION_RIBBON_ENERGY_SILK;
static VFX_FlameStyle s_ambientFireFixtureStyle = VFX_FLAME_STYLE_NIAGARA_ROIL;
static VFX_WoodVineConfig   s_liveWoodVineConfig;
static VFX_WoodLeavesConfig s_liveWoodLeavesConfig;
static VFX_WoodFlowerConfig s_liveWoodFlowerConfig;
static VFX_WoodPetalConfig  s_liveWoodPetalConfig;
static bool                 s_liveWoodConfigsInit = false;
// @gen:newfx_guided_state begin
static VFX_GuidedParticleConfig s_liveGuidedParticleConfig;
static bool s_liveGuidedParticleConfigInit = false;
static int s_guidedFixturePreset = 0;
static const char *s_guidedFixturePresetNames[] = {
    "Shell / release", "Stream / orbit", "Shell / blast",
    "Stream / target turbulence", "Catch leaves / sustained swirl", "Shell / disappear",
    "Catch leaves / traveling pulse", "Stream / coherent flow", "Stream / no added flow"
};
#define VFXTEST_GUIDED_PRESET_COUNT 9

static void VFXTest_SetGuidedPreset(int preset)
{
    s_guidedFixturePreset = preset;
    s_liveGuidedParticleConfig = VFX_GuidedParticle_DefaultConfig();
    s_liveGuidedParticleConfigInit = true;
    s_liveGuidedParticleConfig.targetPreset = VFX_GUIDED_TARGET_NONE;
    s_liveGuidedParticleConfig.arrival = MOTION_ARRIVAL_RELEASE;
    s_liveGuidedParticleConfig.guideMode = MOTION_GUIDE_SUSTAINED;
    s_liveGuidedParticleConfig.duration = 5.0f;
    s_liveGuidedParticleConfig.particleRadius = 0.11f;
    if (preset == 3 || preset == 4 || preset == 6 || preset == 7) {
        s_liveGuidedParticleConfig.targetPreset = VFX_GUIDED_TARGET_FLOW;
        s_liveGuidedParticleConfig.targetLifetime = 4.0f;
    }
    if (preset == 1 || preset == 3 || preset == 7 || preset == 8) {
        s_liveGuidedParticleConfig.formation = MOTION_FORMATION_STREAM;
        s_liveGuidedParticleConfig.emitDuration = 2.5f;
    }
    if (preset == 1) {
        s_liveGuidedParticleConfig.arrival = MOTION_ARRIVAL_ORBIT;
        s_liveGuidedParticleConfig.arrivalFlow.swirlSpeedMps = 3.0f;
    } else if (preset == 2) {
        s_liveGuidedParticleConfig.targetPreset = VFX_GUIDED_TARGET_BLAST;
        s_liveGuidedParticleConfig.targetLifetime = 4.0f;
    } else if (preset == 3) {
        s_liveGuidedParticleConfig.targetFlow.turbulenceSpeedMps = 8.0f;
    } else if (preset == 4 || preset == 6) {
        s_liveGuidedParticleConfig.count = 0;
        s_liveGuidedParticleConfig.formation = MOTION_FORMATION_STREAM;
        s_liveGuidedParticleConfig.speed = 3.0f;
        s_liveGuidedParticleConfig.duration = 8.0f;
        s_liveGuidedParticleConfig.guideRadius = 1.2f;
        s_liveGuidedParticleConfig.maxForceNewtons = 0.4f;
        s_liveGuidedParticleConfig.targetFlow.swirlSpeedMps = 8.0f;
        if (preset == 6) {
            s_liveGuidedParticleConfig.guideMode = MOTION_GUIDE_PULSE;
            s_liveGuidedParticleConfig.pulseLength = 2.0f;
            s_liveGuidedParticleConfig.targetLifetime = 2.5f;
        }
    } else if (preset == 5) {
        s_liveGuidedParticleConfig.arrival = MOTION_ARRIVAL_DESTROY;
    } else if (preset == 7) {
        s_liveGuidedParticleConfig.motionFlow.turbulenceSpeedMps = 0.6f;
        s_liveGuidedParticleConfig.motionFlow.swirlSpeedMps = 1.5f;
        s_liveGuidedParticleConfig.targetFlow.turbulenceSpeedMps = 1.0f;
        s_liveGuidedParticleConfig.targetFlow.swirlSpeedMps = 2.0f;
    }
}

static void VFXTest_InitGuidedConfig(void)
{
    if (!s_liveGuidedParticleConfigInit) {
        // Deterministic capture selection; interactive >/< uses the same presets.
        const char *capturePreset = getenv("WUXING_GUIDED_PRESET");
        int preset = capturePreset ? atoi(capturePreset) : 0;
        if (preset < 0 || preset >= VFXTEST_GUIDED_PRESET_COUNT) preset = 0;
        VFXTest_SetGuidedPreset(preset);
    }
}

static void VFXTest_FireGuidedParticle(Vector3 source, Vector3 target)
{
    VFXTest_InitGuidedConfig();
    // Keep this event fixture selected for preset controls and the inspector.
    s_isPlayingMesh = true;
    VFX_GuidedParticleConfig cfg = s_liveGuidedParticleConfig;
    cfg.source = source;
    cfg.target = target;
    if (s_guidedFixturePreset == 4 || s_guidedFixturePreset == 6) {
        VFX_WoodLeavesConfig leaves = VFX_WoodLeaves_DefaultConfig();
        Vector3 leafCenter = Vector3Add(Vector3Lerp(source, target, 0.40f), (Vector3){0.0f, 0.45f, 0.0f});
        VFX_Foliage_SpawnFreeLeaves(leafCenter,
                                  0.45f, 80, leaves.mass, leaves.style);
    }
    TraceLog(LOG_INFO, "GUIDED PARTICLE: %s", s_guidedFixturePresetNames[s_guidedFixturePreset]);
    if (VFX_ComposeGuidedParticleEx(&cfg) == MOTION_FIELD_INVALID)
        TraceLog(LOG_WARNING, "GUIDED PARTICLE: cast rejected (invalid configuration or exhausted field/emitter pool)");
}
// @gen:newfx_guided_state end


#define VFX_TEST_MAX_INSPECTOR_PARAMS 32
static VFX_ParamDef s_inspectorParams[VFX_TEST_MAX_INSPECTOR_PARAMS];
static int          s_inspectorParamCount = 0;
static int          s_inspectorSelectedParam = 0;
static int          s_lastInspectedFixtureIndex = -999;
static bool         s_vfxAnimationPaused = false;

static void VFXTest_InitLiveConfigs(void)
{
    if (s_liveWoodConfigsInit) return;
    s_liveWoodVineConfig = VFX_WoodVine_DefaultConfig();
    s_liveWoodLeavesConfig = VFX_WoodLeaves_DefaultConfig();
    s_liveWoodFlowerConfig = VFX_WoodFlower_DefaultConfig();
    s_liveWoodPetalConfig  = VFX_WoodPetal_DefaultConfig();
    s_liveWoodLeavesConfig.attached = true;
    s_liveWoodFlowerConfig.attached = true;
    s_liveWoodConfigsInit = true;
}

static void VFXTest_RefreshInspectorParams(bool force)
{
    VFXTest_InitLiveConfigs();

    if (!force && s_testIndex == s_lastInspectedFixtureIndex)
        return;
    s_lastInspectedFixtureIndex = s_testIndex;
    s_inspectorSelectedParam = 0;
    s_inspectorParamCount = 0;

// @gen:newfx_guided_inspector begin
    if (VFXTest_IsNewFxNamed("GUIDED PARTICLE")) {
        VFXTest_InitGuidedConfig();
        s_inspectorParamCount = VFX_GuidedParticle_GetParams(&s_liveGuidedParticleConfig,
                                                          s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
    }
// @gen:newfx_guided_inspector end

    if (VFXTest_IsNewFxNamed("WOOD VINE"))
    {
        s_inspectorParamCount = VFX_WoodVine_GetParams(&s_liveWoodVineConfig, s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
    }
    else if (VFXTest_IsNewFxNamed("WOOD LEAVES"))
    {
        s_inspectorParamCount = VFX_WoodLeaves_GetParams(&s_liveWoodLeavesConfig, s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
    }
    else if (VFXTest_IsNewFxNamed("WOOD FLOWER"))
    {
        s_inspectorParamCount = VFX_WoodFlower_GetParams(&s_liveWoodFlowerConfig, s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
    }
    else if (VFXTest_IsNewFxNamed("WOOD PETALS"))
    {
        s_inspectorParamCount = VFX_WoodPetals_GetParams(&s_liveWoodPetalConfig, s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
    }
}

static inline float VFXTest_Smoothstep(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static VFX_WoodVineConfig VFXTest_BuildWoodVineConfig(Vector3 startPos, Vector3 playerPos, float meshTime)
{
    VFXTest_InitLiveConfigs();
    VFX_WoodVineConfig cfg = s_liveWoodVineConfig;
    cfg.startPos = startPos;

    bool nearPlayer = (Vector3Distance(startPos, playerPos) > 0.05f && Vector3Distance(startPos, playerPos) < 2.5f);
    cfg.targetPos = nearPlayer ? playerPos : Vector3Add(startPos, (Vector3){0.0f, 2.2f, 0.0f});
    cfg.targetRadius = nearPlayer ? 0.38f : 0.0f;
    cfg.targetHeight = 1.8f;
    cfg.length = 3.2f;
    cfg.baseRadius = 0.09f;
    cfg.swayAmp = 0.05f;
    cfg.coilRadius = 0.35f;
    cfg.coilTurns = 2.4f;
    cfg.castShadow = true;
    cfg.seed = 98765;

    // -------------------------------------------------------------------------
    // WUXING WOOD VINE TEST TIMELINE (Total 6.0 seconds per cycle)
    // Phase 1 (0.0s -> 1.6s): Mọc lên (Sprouting & climbing growth)
    // Phase 2 (1.6s -> 2.6s): Trưởng thành & đứng im 1s (Mature stand: inspect bark & thorns)
    // Phase 3 (2.6s -> 5.0s): Phản ứng nguyên tố (Water: lá & hoa; Metal: rụng đứt; Fire: cháy tro)
    // Phase 4 (5.0s -> 6.0s): Hòa hoãn rút lui / reset
    // -------------------------------------------------------------------------
    float cycleT = fmodf(meshTime, 6.0f);
    cfg.sapPhase = meshTime * 2.0f;

    if (cycleT < 1.6f)
    {
        // Phase 1: Mọc lên
        cfg.growth = VFXTest_Smoothstep(0.0f, 1.6f, cycleT);
        cfg.waterFactor = 0.0f;
        cfg.severArc = 1.0f;
        cfg.wither = 0.0f;
        cfg.fireFactor = 0.0f;
    }
    else if (cycleT < 2.6f)
    {
        // Phase 2: Trưởng thành & đứng im 1s (lá và hoa chưa nở để nhìn rõ thân cành!)
        cfg.growth = 1.0f;
        cfg.waterFactor = 0.0f;
        cfg.severArc = 1.0f;
        cfg.wither = 0.0f;
        cfg.fireFactor = 0.0f;
    }
    else if (cycleT < 5.0f)
    {
        // Phase 3: Phản ứng nguyên tố
        cfg.growth = 1.0f;
        float rxnSmooth = VFXTest_Smoothstep(2.6f, 5.0f, cycleT);

        switch (cfg.reaction)
        {
            case WOOD_REACTION_WATER: // Thủy sinh Mộc: Tưới nước -> nở lá -> nở hoa
                cfg.waterFactor = rxnSmooth;
                cfg.severArc = 1.0f;
                cfg.wither = 0.0f;
                cfg.fireFactor = 0.0f;
                break;
            case WOOD_REACTION_METAL: // Kim khắc Mộc: Chém đứt -> rụng rơi
                cfg.severArc = 0.55f;
                cfg.wither = rxnSmooth * 0.70f;
                cfg.waterFactor = 0.0f;
                cfg.fireFactor = 0.0f;
                break;
            case WOOD_REACTION_FIRE:  // Hỏa thiêu Mộc: Bốc cháy -> hóa tro
                cfg.fireFactor = rxnSmooth;
                cfg.wither = (rxnSmooth > 0.15f) ? ((rxnSmooth - 0.15f) / 0.85f) : 0.0f;
                cfg.severArc = 1.0f;
                cfg.waterFactor = 0.0f;
                break;
            case WOOD_REACTION_NORMAL:
            default:
                cfg.waterFactor = 0.0f;
                cfg.severArc = 1.0f;
                cfg.wither = 0.0f;
                cfg.fireFactor = 0.0f;
                break;
        }
    }
    else
    {
        // Phase 4: Hòa hoãn rút lui / reset
        float exitSmooth = 1.0f - VFXTest_Smoothstep(5.0f, 6.0f, cycleT);
        cfg.growth = exitSmooth;
        if (cfg.reaction == WOOD_REACTION_WATER) {
            cfg.waterFactor = exitSmooth;
            cfg.severArc = 1.0f;
        } else if (cfg.reaction == WOOD_REACTION_METAL) {
            cfg.severArc = 0.55f;
            cfg.wither = 0.70f;
        } else if (cfg.reaction == WOOD_REACTION_FIRE) {
            cfg.fireFactor = exitSmooth;
            cfg.wither = 1.0f;
            cfg.severArc = 1.0f;
        } else {
            cfg.severArc = 1.0f;
        }
    }

    return cfg;
}

static VFX_WoodLeavesConfig VFXTest_BuildWoodLeavesConfig(Vector3 startPos, Vector3 playerPos, float meshTime)
{
    (void)playerPos;
    VFXTest_InitLiveConfigs();
    VFX_WoodLeavesConfig cfg = s_liveWoodLeavesConfig;
    cfg.origin = Vector3Add(startPos, (Vector3){0.0f, 1.35f, 0.0f});
    cfg.radius = 1.4f;
    cfg.count = 80;
    cfg.seed = 12345;

    static VFX_BotanicalSocket s_testLeafSockets[10];
    // Elegant eye-level fan branch facing camera, dynamically rooted at startPos
    for (int i = 0; i < 10; i++) {
        float t = (float)i / 9.0f;
        float ang = (t - 0.5f) * 1.6f;
        float r = 0.28f + t * 0.18f;
        s_testLeafSockets[i].pos = Vector3Add(startPos, (Vector3){ sinf(ang) * r, 1.15f + t * 0.40f, cosf(ang) * 0.15f });
        s_testLeafSockets[i].normal = Vector3Normalize((Vector3){ sinf(ang) * 0.6f, 0.40f, 0.90f });
        s_testLeafSockets[i].tangent = (Vector3){ cosf(ang), 0.7f, -sinf(ang) * 0.3f };
        s_testLeafSockets[i].arc = t;
        s_testLeafSockets[i].stemRadius = 0.035f;
    }

    float cycleT = fmodf(meshTime, 4.0f);
    cfg.growth = (cycleT < 2.6f) ? VFXTest_Smoothstep(0.0f, 1.8f, cycleT)
               : (cycleT < 3.5f ? 1.0f : (1.0f - VFXTest_Smoothstep(3.5f, 4.0f, cycleT)));

    if (cfg.attached)
    {
        cfg.sockets = s_testLeafSockets;
        cfg.socketCount = 10;
    }
    return cfg;
}

static VFX_WoodFlowerConfig VFXTest_BuildWoodFlowerConfig(Vector3 startPos, Vector3 playerPos, float meshTime)
{
    (void)playerPos;
    VFXTest_InitLiveConfigs();
    VFX_WoodFlowerConfig cfg = s_liveWoodFlowerConfig;
    cfg.origin = Vector3Add(startPos, (Vector3){0.0f, 1.35f, 0.0f});
    cfg.radius = 1.2f;
    cfg.count = 64;
    cfg.mass = 0.002f;
    cfg.size = 0.22f;
    cfg.seed = 54321;

    static VFX_BotanicalSocket s_testFlowerSockets[5];
    // Central Hero Flower facing camera, dynamically rooted at startPos
    s_testFlowerSockets[0].pos = Vector3Add(startPos, (Vector3){0.0f, 1.35f, 0.0f});
    s_testFlowerSockets[0].normal = (Vector3){0.0f, 0.35f, 0.94f}; // Facing forward/upward to camera
    s_testFlowerSockets[0].tangent = (Vector3){0.0f, 1.0f, 0.0f};
    s_testFlowerSockets[0].arc = 0.0f;
    s_testFlowerSockets[0].stemRadius = 0.04f;

    // 4 companion flowers around it at different angles
    for (int i = 1; i < 5; i++) {
        float ang = ((float)(i - 1) / 4.0f) * 2.0f * PI + 0.35f;
        float r = 0.42f;
        s_testFlowerSockets[i].pos = Vector3Add(startPos, (Vector3){ cosf(ang) * r, 1.22f + sinf(ang * 2.0f) * 0.12f, sinf(ang) * r * 0.7f });
        s_testFlowerSockets[i].normal = Vector3Normalize((Vector3){ cosf(ang) * 0.65f, 0.55f, 0.85f });
        s_testFlowerSockets[i].tangent = (Vector3){ -sinf(ang), 0.6f, cosf(ang) };
        s_testFlowerSockets[i].arc = (float)i / 4.0f;
        s_testFlowerSockets[i].stemRadius = 0.035f;
    }

    // 4.5s Organic Bud-to-Bloom Showcase Timeline:
    // 0.0s -> 0.6s: Tight conical bud
    // 0.6s -> 2.4s: Sepals part, petals part and bloom with easeOutBack flourish
    // 2.4s -> 3.8s: Full glorious bloom with glowing stamen core
    // 3.8s -> 4.5s: Soft reset
    float cycleT = fmodf(meshTime, 4.5f);
    if (cycleT < 2.4f) {
        cfg.growth = VFXTest_Smoothstep(0.0f, 1.8f, cycleT);
    } else if (cycleT < 3.8f) {
        cfg.growth = 1.0f;
    } else {
        cfg.growth = 1.0f - VFXTest_Smoothstep(3.8f, 4.5f, cycleT);
    }

    if (cfg.attached)
    {
        cfg.sockets = s_testFlowerSockets;
        cfg.socketCount = 5;
    }
    return cfg;
}

static VFX_WoodPetalConfig VFXTest_BuildWoodPetalConfig(Vector3 startPos, Vector3 playerPos, float meshTime)
{
    (void)playerPos;
    (void)meshTime;
    VFXTest_InitLiveConfigs();
    VFX_WoodPetalConfig cfg = s_liveWoodPetalConfig;
    cfg.origin = Vector3Add(startPos, (Vector3){0.0f, 1.8f, 0.0f});
    cfg.radius = 1.4f;
    cfg.count = 64;
    cfg.seed = 67890;
    return cfg;
}

// @gen:newfx_surface_impact_selector_state begin
static VFX_ImpactSurface s_surfaceImpactFixtureSurface = VFX_IMPACT_SURFACE_EARTH;
static const char *VFXTest_SurfaceImpactReceiverName(VFX_ImpactSurface surface)
{
    static const char *const names[VFX_IMPACT_SURFACE_COUNT] = {
        "Earth", "Fire", "Wood", "Metal", "Water"};
    if (surface < VFX_IMPACT_SURFACE_EARTH || surface >= VFX_IMPACT_SURFACE_COUNT)
        return names[VFX_IMPACT_SURFACE_EARTH];
    return names[surface];
}
// @gen:newfx_surface_impact_selector_state end

static const char *MotionRibbonFixturePresetName(TrailPresetId preset)
{
    switch (preset)
    {
    case MOTION_RIBBON_SMOKE_WISP: return "SMOKE WISP";
    case MOTION_RIBBON_EMBER_FILAMENT: return "EMBER FILAMENT";
    case MOTION_RIBBON_WATER_STREAM: return "WATER STREAM";
    default: return "ENERGY SILK";
    }
}
static VC_MaterialId MotionRibbonFixtureMaterial(TrailPresetId preset)
{
    switch (preset)
    {
    case MOTION_RIBBON_SMOKE_WISP: return VC_MAT_METAL;
    case MOTION_RIBBON_EMBER_FILAMENT: return VC_MAT_FIRE;
    case MOTION_RIBBON_WATER_STREAM: return VC_MAT_WATER;
    default: return VC_MAT_LIGHTNING;
    }
}

/* Sandbox-only contract fixture. It deliberately bypasses every production
 * composition so adopting the new EffectMaterial path here cannot migrate or
 * visually change an existing VFX. */
#define MATERIAL_OUTPUT_FIXTURE_INDEX 9
static EffectMaterial s_materialOutputFixture[3];
static bool s_materialOutputFixtureReady = false;

static void VFXTest_InitMaterialOutputFixture(void)
{
    if (s_materialOutputFixtureReady)
        return;

    EffectMaterialParams params = {
        .baseColor = (Color){70, 135, 255, 255},
        .rimStrength = 0.8f,
        .fresnelPower = 3.5f,
        .translucency = 0.25f,
    };
    EffectMaterialVFXOutput outputs[3] = {
        {
            .surface = VFX_SURFACE_ALPHA,
            .bodyOpacity = 0.72f,
            .emissionColor = (Color){70, 135, 255, 255},
            .emissionIntensity = 0.0f,
            .coreMask = 0.0f,
            .geometryMode = EFFECT_MATERIAL_GEOMETRY_IMMEDIATE,
        },
        {
            .surface = VFX_SURFACE_PREMULTIPLIED,
            .bodyOpacity = 0.72f,
            .emissionColor = (Color){70, 135, 255, 255},
            .emissionIntensity = 2.2f,
            .coreMask = 1.0f,
            .geometryMode = EFFECT_MATERIAL_GEOMETRY_IMMEDIATE,
        },
        {
            .surface = VFX_SURFACE_ADDITIVE,
            .bodyOpacity = 0.0f,
            .emissionColor = (Color){70, 135, 255, 255},
            .emissionIntensity = 2.2f,
            .coreMask = 1.0f,
            .geometryMode = EFFECT_MATERIAL_GEOMETRY_IMMEDIATE,
        },
    };

    for (int i = 0; i < 3; i++)
        Material_LoadCustomVFX(&s_materialOutputFixture[i], &params, &outputs[i]);
    s_materialOutputFixtureReady = true;
}

static void VFXTest_DrawMaterialOutputFixture(Vector3 center)
{
    static const float offsets[3] = {-1.25f, 0.0f, 1.25f};
    static const VFXRenderPass passes[3] = {
        VFX_RENDER_PASS_BODY,
        VFX_RENDER_PASS_BODY,
        VFX_RENDER_PASS_EMISSION,
    };
    Vector3 cameraForward = Vector3Normalize(
        Vector3Subtract(s_lastCam.target, s_lastCam.position));
    Vector3 cameraRight = Vector3CrossProduct(cameraForward, s_lastCam.up);
    if (Vector3LengthSqr(cameraRight) < 0.0001f)
        cameraRight = (Vector3){1.0f, 0.0f, 0.0f};
    else
        cameraRight = Vector3Normalize(cameraRight);

    VFXTest_InitMaterialOutputFixture();
    for (int i = 0; i < 3; i++)
    {
        VFXRenderScope scope;
        Vector3 pos = Vector3Add(center, Vector3Scale(cameraRight, offsets[i]));
        pos.y += 0.72f;
        if (!Material_BeginVFX(s_materialOutputFixture[i], passes[i], false, &scope))
            continue;
        DrawCoreSphere(pos, 0.62f, 28, 28, WHITE);
        Material_EndVFX(&scope);
    }
}

static void VFXTest_InitFixtures(void)
{
    if (s_vfxFixturesReady)
        return;
    for (int i = 0; i < VFXTEST_FIXTURE_SLOTS; i++)
    {
        s_vfxFixtureHandle[i] = -1;
        s_vfxFixtureLastTime[i] = -1.0f;
    }
    s_vfxFixturesReady = true;
}

// Release every fixture-owned handle before changing test context. Generated
// calls use each composition's public Kill API; trails then drain naturally.
static void VFXTest_StopFixtures(void)
{
    VFXTest_InitFixtures();
    // @gen:newfx_stop begin
    if (s_vfxFixtureHandle[6] >= 0)
        VFX_KillFlowShield(s_vfxFixtureHandle[6]);
    s_vfxFixtureHandle[6] = -1;
    s_vfxFixtureLastTime[6] = -1.0f;
    if (s_vfxFixtureHandle[7] >= 0)
        VFX_KillGasMaterialLab(s_vfxFixtureHandle[7]);
    s_vfxFixtureHandle[7] = -1;
    s_vfxFixtureLastTime[7] = -1.0f;
    if (s_vfxFixtureHandle[8] >= 0)
        VFX_KillGasPlume(s_vfxFixtureHandle[8]);
    s_vfxFixtureHandle[8] = -1;
    s_vfxFixtureLastTime[8] = -1.0f;
    if (s_vfxFixtureHandle[10] >= 0)
        VFX_KillGasVortex(s_vfxFixtureHandle[10]);
    s_vfxFixtureHandle[10] = -1;
    s_vfxFixtureLastTime[10] = -1.0f;
    if (s_vfxFixtureHandle[19] >= 0)
        VFX_KillMeshParticleEmitter(s_vfxFixtureHandle[19]);
    s_vfxFixtureHandle[19] = -1;
    s_vfxFixtureLastTime[19] = -1.0f;
    if (s_vfxFixtureHandle[23] >= 0)
        VFX_KillRefBands(s_vfxFixtureHandle[23]);
    s_vfxFixtureHandle[23] = -1;
    s_vfxFixtureLastTime[23] = -1.0f;
    if (s_vfxFixtureHandle[24] >= 0)
        VFX_KillRefParticles(s_vfxFixtureHandle[24]);
    s_vfxFixtureHandle[24] = -1;
    s_vfxFixtureLastTime[24] = -1.0f;
    if (s_vfxFixtureHandle[25] >= 0)
        VFX_RiftBolt_Stop(s_vfxFixtureHandle[25]);
    s_vfxFixtureHandle[25] = -1;
    s_vfxFixtureLastTime[25] = -1.0f;
    if (s_vfxFixtureHandle[27] >= 0)
        VFX_KillShieldShell(s_vfxFixtureHandle[27]);
    s_vfxFixtureHandle[27] = -1;
    s_vfxFixtureLastTime[27] = -1.0f;
    if (s_vfxFixtureHandle[29] >= 0)
        VFX_SmokeColumn_Stop(s_vfxFixtureHandle[29]);
    s_vfxFixtureHandle[29] = -1;
    s_vfxFixtureLastTime[29] = -1.0f;
    if (s_vfxFixtureHandle[35] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[35]);
    s_vfxFixtureHandle[35] = -1;
    s_vfxFixtureLastTime[35] = -1.0f;
    if (s_vfxFixtureHandle[38] >= 0)
        VFX_KillVolumeTrail(s_vfxFixtureHandle[38]);
    s_vfxFixtureHandle[38] = -1;
    s_vfxFixtureLastTime[38] = -1.0f;
// @gen:newfx_stop end
}

// All burst fixtures live here. UI clicks and automated render warm-up both
// call this single dispatcher, so each source .inl owns one compose call.
static bool VFXTest_FireNewFx(int newfxIndex, Vector3 pos)
{
    VFXTest_InitFixtures();
    // @gen:newfx_fire begin
    int posSeed = (int)(pos.x * 17.0f + pos.z * 31.0f) & 0xFFFF;
    switch (newfxIndex) {
    case 0: VFX_ComposeContactSpark(pos, VC_MAT_FIRE, 1.5f, 0.0f); return true;
    case 1: VFX_ComposeDebrisShards(pos, (Vector3){1.4f, 2.2f, 0.5f}, VC_MAT_METAL, 1.5f, 5); return true;
    case 2: VFX_ComposeDecalVariant(pos, VC_MAT_FIRE, 1.5f, 0.65f, 1.5f, s_decalFixtureVariant); return false;
    case 4: VFX_ComposeEmberBurst(pos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 1.0f); return true;
    case 5: VFX_ComposeFlameJet(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, NULL); return true;
    case 6:
        if (s_vfxFixtureHandle[6] >= 0) VFX_KillFlowShield(s_vfxFixtureHandle[6]);
        s_vfxFixtureHandle[6] = VFX_FlowShield_Spawn(pos, VC_MAT_WATER, 1.5f, 1.0f);
        return true;
    case 9: VFX_ComposeGasShockwave(pos, VC_MAT_VOID, NULL); return true;
    case 12: VFXTest_FireGuidedParticle(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f})); return true;
    case 15: VFX_ComposeImpactDustVariant(pos, s_impactDustFixtureVariant == VFX_IMPACT_DUST_VARIANT_ENERGY_WISP ? VC_MAT_LIGHTNING : VC_MAT_EARTH, 1.5f, 1.0f, s_impactDustFixtureVariant); return false;
    case 17: VFX_ComposeLightningArc(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_LIGHTNING, 0.055f); return true;
    case 18: VFX_ComposeLightningGroundRicochet(pos, VC_MAT_LIGHTNING, 1.0f, posSeed); return true;
    case 19:
        if (s_vfxFixtureHandle[19] >= 0) VFX_KillMeshParticleEmitter(s_vfxFixtureHandle[19]);
        s_vfxFixtureHandle[19] = VFX_ComposeMeshParticleEmitter(&(VFX_MeshParticleEmitterDesc){.model=&s_meshParticleFixtureModel, .transform=MatrixMultiply(MatrixRotateY(s_currentPlayerYaw), MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z)), .variant=s_meshParticleFixtureVariant, .material=VC_MAT_LIGHTNING, .intensity=1.0f, .seed=0x4d455348u});
        return false;
    case 21: VFX_ComposeMistVeil(pos, 5.5f, 4.5f); return true;
    case 27:
        if (s_vfxFixtureHandle[27] >= 0) VFX_KillShieldShell(s_vfxFixtureHandle[27]);
        s_vfxFixtureHandle[27] = VFX_ShieldShell_Spawn(pos, VC_MAT_WATER, 1.5f, 1.0f);
        return true;
    case 30: VFX_ComposeSmokePuff(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 32: VFX_ComposeSurfaceImpact(pos, s_surfaceImpactFixtureSurface); return false;
    case 33: VFX_ComposeSurfaceParticleRing(pos, s_surfaceParticleRingFixtureVariant == VFX_SURFACE_PARTICLE_RING_VARIANT_ENERGY_WISP ? VC_MAT_LIGHTNING : VC_MAT_EARTH, 1.5f, 1.0f, s_surfaceParticleRingFixtureVariant); return false;
    case 42: VFX_ComposeFireballBurst(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 44: VFX_ComposeIceCrystal(pos, posSeed); return true;
    case 46: VFX_ComposeLiquidImpact(pos); return true;
    case 47: VFX_ComposeWaterOrb(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f})); return true;
    default: return false;
    }
// @gen:newfx_fire end
}

static bool s_isPanelOpen = false;
static bool s_clickedOnUI = false;
static int s_newfxFilter = NEWFX_CAT_COMMON;

// MESH: 0-8=DrawEffectMesh presets; 9=sandbox-only material output contract.
static const char *s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON",
    "VFX OUTPUT"};

// @gen:newfx_names begin
// 54 entries — auto-managed by sync_vfx_test.py
static const char* s_newFxNames[] = {
    "CONTACT SPARK", "DEBRIS SHARDS", "DECAL", "DISSOLVE EXIT", "[PARTICLE] EMBER BURST", "FLAME JET",
    "FLOW SHIELD", "GAS MATERIAL LAB", "[GAS] GAS PLUME", "GAS SHOCKWAVE", "GAS VORTEX", "GROUND WAVE",
    "GUIDED PARTICLE", "GUIDING WIND", "IAIDO STANCE", "IMPACT DUST", "LIGHT SHAFT", "LIGHTNING ARC",
    "LIGHTNING IMPACT", "MESH PARTICLE EMITTER", "MESH SURFACE AURA", "MIST VEIL", "OPTICAL FLARE", "REF BANDS",
    "REF PARTICLES", "RIFT BOLT", "RUNE CIRCLE", "SHIELD SHELL", "SHOCK RING", "SMOKE COLUMN",
    "SMOKE PUFF", "[PARTICLE] SMOKE VOLUME", "SURFACE IMPACT", "SURFACE PARTICLE RING", "SWEEP SLASH", "MOTION RIBBON TRAIL",
    "VACUUM CONVERGE", "VACUUM RING", "[TRAIL/FLOW] VOLUME TRAIL", "FISSURE STREAK", "STONE PILLAR", "AMBIENT FIRE",
    "FIREBALL BURST", "BLACK HOLE", "ICE CRYSTAL", "LIQUID BENCH", "LIQUID IMPACT", "WATER ORB",
    "WATER RING", "WATER STREAM", "WOOD FLOWER", "WOOD LEAVES", "WOOD PETALS", "WOOD VINE",
};
// @gen:newfx_names end

static int VFXTest_NewFxCount(void)
{
    return (int)(sizeof(s_newFxNames) / sizeof(s_newFxNames[0]));
}

static bool VFXTest_IsNewFxNamed(const char *name)
{
    return s_testCategory == TEST_CAT_NEWFX &&
           s_testIndex >= 0 && s_testIndex < VFXTest_NewFxCount() &&
           name != NULL && s_newFxNames[s_testIndex] != NULL &&
           strcmp(s_newFxNames[s_testIndex], name) == 0;
}

// @gen:newfx_categories begin
// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 COMMON=6
static const int s_newFxCategories[] = {
    6, 3, 6, 6, 0, 0, 6, 6, 6, 1,
    6, 4, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 1, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 4,
    4, 0, 0, 5, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2,
};
// @gen:newfx_categories end

static int g_activeCountCache = 0;

// Touch button for GPU compute force field test
#define FF_TEST_BTN_X 70.0f
#define FF_TEST_BTN_Y 400.0f
#define FF_TEST_BTN_RADIUS 45.0f

// Touch button for FORCE_VECTOR_TEXTURE test
#define VF_TEST_BTN_X 70.0f
#define VF_TEST_BTN_Y 300.0f
#define VF_TEST_BTN_RADIUS 45.0f


// @gen:vfx_ui_helpers begin
// Canonical VFX tester UI. sync_vfx_test.py copies this into sandbox/vfx_test.c.
static bool s_vfxHelpOpen = false;
static bool s_vfxPointerCaptured = false;
static bool s_vfxButtonArmed = false;
static Rectangle s_vfxArmedButton;
static int s_vfxInspectorScroll = 0;
static int s_vfxBrowserScroll = 0;
static float s_vfxTelemetryFps, s_vfxTelemetryFrameMs, s_vfxTelemetryPeakMs;
static float s_vfxTelemetryTilt = 20.0f, s_vfxTelemetryDistance = 16.46f, s_vfxTelemetryZoom = 1.25f;
static int s_vfxPendingTiltStep;

void VFXTest_SetPerformanceTelemetry(float fps, float frameTimeMs, float peakFrameMs)
{
    s_vfxTelemetryFps = fps;
    s_vfxTelemetryFrameMs = frameTimeMs;
    s_vfxTelemetryPeakMs = peakFrameMs;
}

void VFXTest_SetCameraTelemetry(float tiltDegrees, float zoomRatio, float distanceMeters)
{
    s_vfxTelemetryTilt = tiltDegrees;
    s_vfxTelemetryDistance = distanceMeters;
    s_vfxTelemetryZoom = zoomRatio;
}

int VFXTest_ConsumeCameraTiltStep(void)
{
    int step = s_vfxPendingTiltStep;
    s_vfxPendingTiltStep = 0;
    return step;
}

typedef struct VFXTest_UILayout {
    Rectangle header, inspector, browser, help, metrics[3], cameraStatus;
    int inspectorRows, browserColumns, browserRows;
    float rowHeight, browserCellWidth;
} VFXTest_UILayout;

static VFXTest_UILayout VFXTest_UIGetLayout(void)
{
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
    float top = 16.0f;
#if defined(PLATFORM_ANDROID)
    top = 92.0f; /* ENGINE_LANDMINES: keep touch targets below the 84px inset. */
#endif
    float panelWidth = fminf(420.0f, fmaxf(300.0f, w * 0.36f));
    panelWidth = fminf(panelWidth, w - 24.0f);
    VFXTest_UILayout ui = {0};
    ui.header = (Rectangle){12.0f, top, w - 24.0f, 40.0f};
    float statusBottom = top + 40.0f;
    if (w >= 900.0f) {
        for (int i = 0; i < 3; ++i) ui.metrics[i] = (Rectangle){178.0f + i * 76.0f, top, 76.0f, 40.0f};
        ui.cameraStatus = (Rectangle){412.0f, top, 240.0f, 40.0f};
    } else {
        ui.header.height = 116.0f;
        for (int i = 0; i < 3; ++i) ui.metrics[i] = (Rectangle){18.0f + i * ((w - 36.0f) / 3.0f), top + 40.0f, (w - 36.0f) / 3.0f, 36.0f};
        ui.cameraStatus = (Rectangle){18.0f, top + 76.0f, w - 36.0f, 36.0f};
        statusBottom = top + 116.0f;
    }
    float panelTop = fmaxf(176.0f, statusBottom + 40.0f);
    ui.rowHeight = 38.0f;
    float panelHeight = fminf(76.0f + fmaxf(1.0f, (float)s_inspectorParamCount) * ui.rowHeight, fmaxf(116.0f, h - panelTop - 48.0f));
    ui.inspector = (Rectangle){w - panelWidth - 12.0f, panelTop, panelWidth, panelHeight};
    ui.inspectorRows = (int)((ui.inspector.height - 76.0f) / ui.rowHeight);
    if (ui.inspectorRows < 1) ui.inspectorRows = 1;
    ui.browser = (Rectangle){12.0f, statusBottom + 12.0f, fminf(720.0f, w - 24.0f), fmaxf(140.0f, h - statusBottom - 60.0f)};
    ui.browserColumns = (int)((ui.browser.width - 20.0f) / 150.0f);
    if (ui.browserColumns < 1) ui.browserColumns = 1;
    ui.browserCellWidth = (ui.browser.width - 20.0f) / (float)ui.browserColumns;
    ui.browserRows = (int)((ui.browser.height - 100.0f) / 40.0f);
    if (ui.browserRows < 1) ui.browserRows = 1;
    ui.help = (Rectangle){12.0f, statusBottom + 12.0f, fminf(480.0f, w - 24.0f), fminf(320.0f, h - statusBottom - 32.0f)};
    return ui;
}

static Rectangle VFXTest_UIHeaderButton(VFXTest_UILayout ui, int index)
{
    float width = ui.header.width < 640.0f ? 52.0f : 74.0f;
    return (Rectangle){ui.header.x + ui.header.width - (4 - index) * (width + 6.0f) - 4.0f,
                       ui.header.y + 5.0f, width, 30.0f};
}

static Rectangle VFXTest_UIInspectorRow(VFXTest_UILayout ui, int visibleRow)
{
    return (Rectangle){ui.inspector.x + 8.0f, ui.inspector.y + 42.0f + visibleRow * ui.rowHeight,
                       ui.inspector.width - 16.0f, ui.rowHeight - 4.0f};
}

static Rectangle VFXTest_UIParameterButton(Rectangle row, int direction)
{
    return (Rectangle){row.x + row.width - (direction < 0 ? 68.0f : 32.0f), row.y + 2.0f, 30.0f, row.height - 4.0f};
}

static Rectangle VFXTest_UITiltButton(VFXTest_UILayout ui, int direction)
{
    return (Rectangle){ui.cameraStatus.x + ui.cameraStatus.width - (direction < 0 ? 72.0f : 36.0f),
                       ui.cameraStatus.y + (ui.cameraStatus.height - 30.0f) * 0.5f, 30.0f, 30.0f};
}

static Rectangle VFXTest_UIBrowserCell(VFXTest_UILayout ui, int visibleIndex)
{
    return (Rectangle){ui.browser.x + 10.0f + (visibleIndex % ui.browserColumns) * ui.browserCellWidth,
                       ui.browser.y + 82.0f + (visibleIndex / ui.browserColumns) * 40.0f,
                       ui.browserCellWidth - 6.0f, 34.0f};
}

static Rectangle VFXTest_UIBrowserTab(VFXTest_UILayout ui, int category)
{
    return (Rectangle){ui.browser.x + 10.0f + category * 106.0f, ui.browser.y + 8.0f, 100.0f, 28.0f};
}

static Rectangle VFXTest_UIBrowserFilter(VFXTest_UILayout ui, int filter)
{
    float width = (ui.browser.width - 20.0f) / NEWFX_CAT_COUNT;
    return (Rectangle){ui.browser.x + 10.0f + filter * width, ui.browser.y + 42.0f, width - 3.0f, 26.0f};
}

static Rectangle VFXTest_UIForceButton(VFXTest_UILayout ui, bool vectorField)
{
    return (Rectangle){ui.help.x + (vectorField ? 124.0f : 12.0f), ui.help.y + ui.help.height - 42.0f, 106.0f, 30.0f};
}

bool VFXTest_IsPointerOverUI(void)
{
    if (s_hideAllUI) return false;
    if (s_vfxPointerCaptured) return true;
    VFXTest_UILayout ui = VFXTest_UIGetLayout();
    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, ui.header)) return true;
    if (CheckCollisionPointRec(mouse, ui.cameraStatus)) return true;
    for (int i = 0; i < 3; ++i) if (CheckCollisionPointRec(mouse, ui.metrics[i])) return true;
    if (s_isPanelOpen || s_vfxHelpOpen) return true; /* Modal panels own their backdrop. */
    if (s_isPlayingMesh && s_inspectorParamCount > 0 && CheckCollisionPointRec(mouse, ui.inspector)) return true;
    if (!s_hideDebugOverlays && CheckCollisionPointRec(mouse, (Rectangle){12.0f, 164.0f, 298.0f, 76.0f})) return true;
    return false;
}

static bool VFXTest_UIButtonReleased(Rectangle rect)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
    if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !s_vfxButtonArmed) {
        s_vfxButtonArmed = true;
        s_vfxArmedButton = rect;
    }
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && s_vfxButtonArmed &&
           s_vfxArmedButton.x == rect.x && s_vfxArmedButton.y == rect.y &&
           s_vfxArmedButton.width == rect.width && s_vfxArmedButton.height == rect.height;
}

static int VFXTest_UIBrowserCount(void)
{
    if (s_testCategory == TEST_CAT_MESH) return (int)(sizeof(s_meshNames) / sizeof(s_meshNames[0]));
    int count = 0;
    for (int i = 0; i < VFXTest_NewFxCount(); ++i) if (s_newFxCategories[i] == s_newfxFilter) ++count;
    return count;
}

static int VFXTest_UIBrowserIndex(int visibleIndex)
{
    if (s_testCategory == TEST_CAT_MESH) return visibleIndex;
    for (int i = 0; i < VFXTest_NewFxCount(); ++i) {
        if (s_newFxCategories[i] != s_newfxFilter) continue;
        if (visibleIndex-- == 0) return i;
    }
    return -1;
}

static void VFXTest_UISelectFixture(int index, Vector3 playerPos)
{
    VFXTest_StopFixtures();
    s_testIndex = index;
    s_prefabStartPos = playerPos;
    s_meshTime = 0.0f;
    s_isPlayingMesh = true;
    if (s_testCategory == TEST_CAT_NEWFX) {
        VFXTest_FireNewFx(index, playerPos);
        s_isPlayingMesh = true; /* Keep event fixtures available for controls. */
    }
    s_isPanelOpen = false;
    s_vfxInspectorScroll = 0;
    VFXTest_RefreshInspectorParams(true);
}

static void VFXTest_UIRevealSelectedParameter(void)
{
    VFXTest_UILayout ui = VFXTest_UIGetLayout();
    if (s_inspectorSelectedParam < s_vfxInspectorScroll) s_vfxInspectorScroll = s_inspectorSelectedParam;
    if (s_inspectorSelectedParam >= s_vfxInspectorScroll + ui.inspectorRows)
        s_vfxInspectorScroll = s_inspectorSelectedParam - ui.inspectorRows + 1;
}

static void VFXTest_UILogParameter(int index)
{
    char value[64];
    VFX_Param_FormatValue(&s_inspectorParams[index], value, sizeof(value));
    TraceLog(LOG_INFO, "[VFXUI] %s / %s: %s", s_inspectorParams[index].group, s_inspectorParams[index].name, value);
}

static void VFXTest_UIEditParameter(int index, int direction)
{
    VFX_ParamDef *param = &s_inspectorParams[index];
    if (!param->valPtr) return;
    if (param->type == VFX_PARAM_FLOAT) {
        float *value = (float *)param->valPtr;
        *value = fmaxf(param->minFloat, fminf(param->maxFloat, *value + direction * param->stepFloat));
    } else if (param->type == VFX_PARAM_INT) {
        int *value = (int *)param->valPtr;
        if (direction > 0 && *value < param->maxInt) ++*value;
        if (direction < 0 && *value > param->minInt) --*value;
        if (*value < param->minInt) *value = param->minInt;
        if (*value > param->maxInt) *value = param->maxInt;
    } else if (direction > 0) {
        VFX_Param_CycleNext(param);
    } else {
        VFX_Param_CyclePrev(param);
    }
}

static bool VFXTest_UIHandleInput(Vector3 playerPos)
{
    if (s_hideAllUI) { s_vfxPointerCaptured = false; s_vfxButtonArmed = false; return false; }
    VFXTest_UILayout ui = VFXTest_UIGetLayout();
    s_clickedOnUI = VFXTest_IsPointerOverUI();
    if (s_clickedOnUI && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) s_vfxPointerCaptured = true;
    bool back = false;
    if (VFXTest_UIButtonReleased(VFXTest_UIHeaderButton(ui, 0))) { s_isPanelOpen = !s_isPanelOpen; s_vfxHelpOpen = false; }
    if (VFXTest_UIButtonReleased(VFXTest_UIHeaderButton(ui, 1))) { Fog_CycleRenderMode(); }
    if (VFXTest_UIButtonReleased(VFXTest_UIHeaderButton(ui, 2))) { s_vfxHelpOpen = !s_vfxHelpOpen; s_isPanelOpen = false; }
    if (VFXTest_UIButtonReleased(VFXTest_UIHeaderButton(ui, 3))) back = true;
    if (!s_isPanelOpen && !s_vfxHelpOpen) {
        if (VFXTest_UIButtonReleased(VFXTest_UITiltButton(ui, -1))) s_vfxPendingTiltStep = -1;
        if (VFXTest_UIButtonReleased(VFXTest_UITiltButton(ui, 1))) s_vfxPendingTiltStep = 1;
    }
    if (s_isPanelOpen) {
        for (int i = 0; i < TEST_CAT_COUNT; ++i) if (VFXTest_UIButtonReleased(VFXTest_UIBrowserTab(ui, i))) {
            s_testCategory = i; s_vfxBrowserScroll = 0;
        }
        if (s_testCategory == TEST_CAT_NEWFX) for (int i = 0; i < NEWFX_CAT_COUNT; ++i)
            if (VFXTest_UIButtonReleased(VFXTest_UIBrowserFilter(ui, i))) { s_newfxFilter = i; s_vfxBrowserScroll = 0; }
        int count = VFXTest_UIBrowserCount();
        int maxScroll = (count + ui.browserColumns - 1) / ui.browserColumns - ui.browserRows;
        if (maxScroll < 0) maxScroll = 0;
        if (CheckCollisionPointRec(GetMousePosition(), ui.browser)) s_vfxBrowserScroll -= (int)GetMouseWheelMove();
        if (s_vfxBrowserScroll < 0) s_vfxBrowserScroll = 0;
        if (s_vfxBrowserScroll > maxScroll) s_vfxBrowserScroll = maxScroll;
        int first = s_vfxBrowserScroll * ui.browserColumns;
        for (int i = 0; i < ui.browserColumns * ui.browserRows && first + i < count; ++i)
            if (VFXTest_UIButtonReleased(VFXTest_UIBrowserCell(ui, i))) {
                int index = VFXTest_UIBrowserIndex(first + i);
                if (index >= 0) VFXTest_UISelectFixture(index, playerPos);
                break;
            }
    } else if (!s_vfxHelpOpen && s_isPlayingMesh && s_inspectorParamCount > 0) {
        int maxScroll = s_inspectorParamCount - ui.inspectorRows;
        if (maxScroll < 0) maxScroll = 0;
        if (CheckCollisionPointRec(GetMousePosition(), ui.inspector)) s_vfxInspectorScroll -= (int)GetMouseWheelMove();
        if (s_vfxInspectorScroll < 0) s_vfxInspectorScroll = 0;
        if (s_vfxInspectorScroll > maxScroll) s_vfxInspectorScroll = maxScroll;
        for (int row = 0; row < ui.inspectorRows && row + s_vfxInspectorScroll < s_inspectorParamCount; ++row) {
            int index = row + s_vfxInspectorScroll;
            Rectangle rect = VFXTest_UIInspectorRow(ui, row);
            if (VFXTest_UIButtonReleased(VFXTest_UIParameterButton(rect, -1))) {
                s_inspectorSelectedParam = index; VFXTest_UIEditParameter(index, -1); VFXTest_UILogParameter(index);
            } else if (VFXTest_UIButtonReleased(VFXTest_UIParameterButton(rect, 1))) {
                s_inspectorSelectedParam = index; VFXTest_UIEditParameter(index, 1); VFXTest_UILogParameter(index);
            } else if (VFXTest_UIButtonReleased(rect)) s_inspectorSelectedParam = index;
        }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { s_vfxButtonArmed = false; s_vfxPointerCaptured = false; }
    return back;
}

static void VFXTest_UIText(const char *text, Rectangle rect, int fontSize, Color color)
{
    BeginScissorMode((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height);
    DrawText(text, (int)rect.x + 6, (int)rect.y + (int)(rect.height - fontSize) / 2, fontSize, color);
    EndScissorMode();
}

static void VFXTest_UIDrawButton(Rectangle rect, const char *text, bool active)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
    Color fill = active ? (Color){48, 83, 90, 245} : (hover ? (Color){48, 57, 68, 245} : (Color){28, 35, 44, 245});
    DrawRectangleRounded(rect, 0.15f, 4, fill);
    DrawRectangleRoundedLines(rect, 0.15f, 4, active ? SKYBLUE : (Color){75, 85, 97, 255});
    VFXTest_UIText(text, rect, 12, active ? SKYBLUE : RAYWHITE);
}

static const char *VFXTest_UIActiveVariant(void)
{
    if (VFXTest_IsNewFxNamed("GUIDED PARTICLE")) return s_guidedFixturePresetNames[s_guidedFixturePreset];
    if (VFXTest_IsNewFxNamed("MESH PARTICLE EMITTER")) return VFX_MeshParticleVariant_Name(s_meshParticleFixtureVariant);
    if (VFXTest_IsNewFxNamed("MOTION RIBBON TRAIL")) return MotionRibbonFixturePresetName(s_motionRibbonFixturePreset);
    if (VFXTest_IsNewFxNamed("[PARTICLE] SMOKE VOLUME")) return VFX_SmokeStyle_Name(s_smokeVolumeFixtureStyle);
    if (VFXTest_IsNewFxNamed("MESH SURFACE AURA")) return VFX_MeshSurfaceAuraVariant_Name(s_meshSurfaceAuraFixtureVariant);
    if (VFXTest_IsNewFxNamed("DECAL")) return VFX_DecalVariant_Name(s_decalFixtureVariant);
    if (VFXTest_IsNewFxNamed("SURFACE PARTICLE RING")) return VFX_SurfaceParticleRingVariant_Name(s_surfaceParticleRingFixtureVariant);
    if (VFXTest_IsNewFxNamed("IMPACT DUST")) return VFX_ImpactDustVariant_Name(s_impactDustFixtureVariant);
    if (VFXTest_IsNewFxNamed("AMBIENT FIRE")) return VFX_FlameStyle_Name(s_ambientFireFixtureStyle);
    if (VFXTest_IsNewFxNamed("SURFACE IMPACT")) return VFXTest_SurfaceImpactReceiverName(s_surfaceImpactFixtureSurface);
    return NULL;
}

static void VFXTest_UIDraw(void)
{
    VFXTest_UILayout ui = VFXTest_UIGetLayout();
    DrawRectangleRounded(ui.header, 0.12f, 4, (Color){15, 20, 27, 230});
    const char *name = s_testCategory == TEST_CAT_NEWFX && s_testIndex >= 0 && s_testIndex < VFXTest_NewFxCount()
                       ? s_newFxNames[s_testIndex] : "MESH PREVIEW";
    Rectangle title = {ui.header.x + 6.0f, ui.header.y, VFXTest_UIHeaderButton(ui, 0).x - ui.header.x - 10.0f, 40.0f};
    if (GetScreenWidth() >= 900) title.width = 150.0f;
    VFXTest_UIText(name, title, 14, RAYWHITE);
    VFXTest_UIDrawButton(VFXTest_UIHeaderButton(ui, 0), "Fixtures", s_isPanelOpen);
    const char *fogShort = Fog_GetRenderMode() == FOG_MODE_HEIGHT ? "Fog: Hgt"
                         : (Fog_GetRenderMode() == FOG_MODE_VOLUMETRIC ? "Fog: Vol" : "Fog: Off");
    VFXTest_UIDrawButton(VFXTest_UIHeaderButton(ui, 1), fogShort, Fog_GetRenderMode() != FOG_MODE_OFF);
    VFXTest_UIDrawButton(VFXTest_UIHeaderButton(ui, 2), "Help", s_vfxHelpOpen);
    VFXTest_UIDrawButton(VFXTest_UIHeaderButton(ui, 3), "Back", false);
    const char *variant = VFXTest_UIActiveVariant();
    if (variant) VFXTest_UIText(TextFormat("%s   |   < / > preset", variant),
                             (Rectangle){12.0f, ui.header.y + ui.header.height + 6.0f, fmaxf(180.0f, GetScreenWidth() - 340.0f), 24.0f}, 12, SKYBLUE);
    VFXTest_UIText(TextFormat("%.0f FPS", s_vfxTelemetryFps), ui.metrics[0], 12, RAYWHITE);
    VFXTest_UIText(TextFormat("%.1f ms", s_vfxTelemetryFrameMs), ui.metrics[1], 12, LIGHTGRAY);
    VFXTest_UIText(TextFormat("peak %.1f", s_vfxTelemetryPeakMs), ui.metrics[2], 11, LIGHTGRAY);
    Rectangle cameraLabel = {ui.cameraStatus.x, ui.cameraStatus.y, ui.cameraStatus.width - 78.0f, ui.cameraStatus.height};
    VFXTest_UIText(TextFormat("Tilt %.0f deg  Zoom %.2fx", s_vfxTelemetryTilt, s_vfxTelemetryZoom), cameraLabel, 12, RAYWHITE);
    VFXTest_UIDrawButton(VFXTest_UITiltButton(ui, -1), "-", false);
    VFXTest_UIDrawButton(VFXTest_UITiltButton(ui, 1), "+", false);
    if (!s_isPanelOpen && !s_vfxHelpOpen && s_isPlayingMesh && s_inspectorParamCount > 0) {
        DrawRectangleRounded(ui.inspector, 0.035f, 4, (Color){15, 20, 27, 235});
        VFXTest_UIText("Parameters", (Rectangle){ui.inspector.x + 8, ui.inspector.y + 6, ui.inspector.width - 16, 28}, 14, RAYWHITE);
        for (int row = 0; row < ui.inspectorRows && row + s_vfxInspectorScroll < s_inspectorParamCount; ++row) {
            int index = row + s_vfxInspectorScroll;
            Rectangle rect = VFXTest_UIInspectorRow(ui, row);
            bool selected = index == s_inspectorSelectedParam;
            DrawRectangleRounded(rect, 0.08f, 3, selected ? (Color){34, 56, 66, 245} : (Color){23, 29, 37, 245});
            char value[64]; VFX_Param_FormatValue(&s_inspectorParams[index], value, sizeof(value));
            VFXTest_UIText(TextFormat("%s / %s", s_inspectorParams[index].group, s_inspectorParams[index].name),
                          (Rectangle){rect.x, rect.y, rect.width - 76, 17}, 11, selected ? SKYBLUE : LIGHTGRAY);
            VFXTest_UIText(value, (Rectangle){rect.x, rect.y + 16, rect.width - 76, 18}, 12, RAYWHITE);
            VFXTest_UIDrawButton(VFXTest_UIParameterButton(rect, -1), "<", false);
            VFXTest_UIDrawButton(VFXTest_UIParameterButton(rect, 1), ">", false);
        }
        VFXTest_UIText(TextFormat("%d-%d / %d   |   scroll or Tab   |   / edit", s_vfxInspectorScroll + 1,
                                  (int)fminf(s_inspectorParamCount, s_vfxInspectorScroll + ui.inspectorRows), s_inspectorParamCount),
                      (Rectangle){ui.inspector.x + 8, ui.inspector.y + ui.inspector.height - 28, ui.inspector.width - 16, 22}, 11, GRAY);
    }
    if (s_isPanelOpen) {
        DrawRectangle(0, (int)(ui.header.y + ui.header.height + 2), GetScreenWidth(), GetScreenHeight(), ColorAlpha(BLACK, 0.22f));
        DrawRectangleRounded(ui.browser, 0.025f, 4, (Color){15, 20, 27, 248});
        VFXTest_UIDrawButton(VFXTest_UIBrowserTab(ui, 0), "Meshes", s_testCategory == TEST_CAT_MESH);
        VFXTest_UIDrawButton(VFXTest_UIBrowserTab(ui, 1), "Effects", s_testCategory == TEST_CAT_NEWFX);
        if (s_testCategory == TEST_CAT_NEWFX) {
            const char *filters[] = {"Fire", "Water", "Wood", "Metal", "Earth", "Taiji", "Common"};
            for (int i = 0; i < NEWFX_CAT_COUNT; ++i) VFXTest_UIDrawButton(VFXTest_UIBrowserFilter(ui, i), filters[i], s_newfxFilter == i);
        }
        int first = s_vfxBrowserScroll * ui.browserColumns, count = VFXTest_UIBrowserCount();
        for (int i = 0; i < ui.browserRows * ui.browserColumns && first + i < count; ++i) {
            int index = VFXTest_UIBrowserIndex(first + i);
            const char *label = s_testCategory == TEST_CAT_NEWFX ? s_newFxNames[index] : s_meshNames[index];
            VFXTest_UIDrawButton(VFXTest_UIBrowserCell(ui, i), label, s_testIndex == index);
        }
        VFXTest_UIText(TextFormat("%d-%d / %d   |   scroll to browse", count ? first + 1 : 0,
                                  (int)fminf(count, first + ui.browserRows * ui.browserColumns), count),
                      (Rectangle){ui.browser.x + 10, ui.browser.y + ui.browser.height - 24, ui.browser.width - 20, 20}, 11, GRAY);
    }
    if (s_vfxHelpOpen) {
        DrawRectangleRounded(ui.help, 0.035f, 4, (Color){15, 20, 27, 248});
        const char *lines[] = {"Preview controls", "Click the scene to cast. Q/E orbit, wheel zoom.",
            "Tab / CapsLock: select parameter. /: next value.", "Shift /: previous value. < / >: fixture preset.",
            "V: fixture clock. B: character. U: hide UI.", "F3: diagnostics. N: background. R: reset view.",
            "Wood: ; burst, ' homing, backslash detach.", "Demos: 1 mesh, 2 SSS, 3 particles, 4 slash,",
            "5 Iaido, 6 wind, 7 path flow. Z/C: actions."};
        for (int i = 0; i < 9; ++i) VFXTest_UIText(lines[i], (Rectangle){ui.help.x + 8, ui.help.y + 8 + i * 26.0f, ui.help.width - 16, 24}, i == 0 ? 14 : 12, i == 0 ? RAYWHITE : LIGHTGRAY);
        VFXTest_UIDrawButton(VFXTest_UIForceButton(ui, false), "Force test", false);
        VFXTest_UIDrawButton(VFXTest_UIForceButton(ui, true), "Vector field", false);
    }
}
// @gen:vfx_ui_helpers end
bool VFXTest_UpdateAndHandleInput(Vector3 playerPos, Vector3 mouseTarget3D, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex)
{
    // Reset cache count each frame
    g_activeCountCache = 0;
    s_clickedOnUI = false;
    s_currentPlayerPos = playerPos;
    s_demoParticleTex = globalParticleTex;
    if (s_demoParticleTex.id == 0) s_demoParticleTex = testAtlasTex;

    if (!s_hasTestPath)
    {
        s_prefabStartPos = playerPos;
        Vector3 pathStart = Vector3Add(playerPos, (Vector3){0.0f, 0.3f, 0.0f});
        Vector3 pathEnd = Vector3Add(playerPos, (Vector3){3.0f, 0.0f, 0.0f});

        for (int idx = 0; idx < TEST_PATH_POINT_COUNT; idx++)
        {
            float t = (float)idx / (float)(TEST_PATH_POINT_COUNT - 1);
            s_testPathPoints[idx] = Vector3Lerp(pathStart, pathEnd, t);
        }
        s_hasTestPath = true;
    }

    float dt = TimeFX_RawDelta();
    (void)dt;

    /* P — chụp CHỈ vùng hiệu ứng ra autotest_output/vfx_<n>_<số>.png.
     *
     * Toàn màn hình là cách phán đoán sai: một cột 1.6 m trong khung 1280 px
     * chỉ chiếm vài chục pixel, mà ở cỡ đó một dải chuyển mềm và một nét cắt
     * cứng là cùng ngần ấy pixel. Ảnh cắt sát mới đọc được biên.
     *
     * [ và ] thu/phóng vùng cắt. */
    if (IsKeyPressed(KEY_LEFT_BRACKET))
        s_shotRadius *= 0.75f;
    if (IsKeyPressed(KEY_RIGHT_BRACKET))
        s_shotRadius *= 1.33f;
    if (IsKeyPressed(KEY_P))
        s_shotWanted = true;
    /* O — thăm dò đường ống màu. Xem sandbox/colour_probe.c. */
    if (IsKeyPressed(KEY_O))
        ColourProbe_Arm();
    /* I — thăm dò quy ước fresnel |N.V|. Xem sandbox/fresnel_probe.c. */
    if (IsKeyPressed(KEY_I))
        FresnelProbe_Arm();
    /* G — thăm dò dải gradient: một hình chữ nhật đổ màu đi qua đúng đường ống
     * của VFX, để tách "hiệu ứng vẽ sai" khỏi "đường ống làm vỡ dải màu".
     * Xem sandbox/gradient_probe.c. */
    if (IsKeyPressed(KEY_G))
        GradientProbe_Arm();
    /* B — hiện/ẩn nhân vật tham chiếu tỉ lệ. */
    if (IsKeyPressed(KEY_B))
        s_hideCharacterRef = !s_hideCharacterRef;
    /* F3 — hiện/ẩn HUD debug (pool GPU particle, skill manager, core-test). */
    if (IsKeyPressed(KEY_F3))
        s_hideDebugOverlays = !s_hideDebugOverlays;
    /* U — tắt/bật TOÀN BỘ UI của màn hình này (tabs, nút mesh/newfx, 2 hình
     * tròn FF/VF TEST, nút toggle/back). Dùng khi cần chụp/quan sát VFX hoàn
     * toàn sạch — bấm lại U để lấy control panel về chọn hiệu ứng khác. */
    if (IsKeyPressed(KEY_U))
    {
        s_hideAllUI = !s_hideAllUI;
        TraceLog(LOG_INFO, "[VFXTest] UI %s (U de doi lai)", s_hideAllUI ? "AN" : "HIEN");
    }

    // -------------------------------------------------------------------------
    // DEMO MESSIAH VFX FEATURES: [1] Mesh Distort | [2] SSS | [3] Capsule SDF
    // -------------------------------------------------------------------------
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1))
    {
        s_demoMeshDistortActive = true;
        s_demoMeshDistortTimer = 0.0f;
        s_demoMeshDistortPos = playerPos;
        s_demoMeshDistortYaw = s_currentPlayerYaw;
        ScreenDistort_RequestMeshPass();
        TraceLog(LOG_INFO, "[Messiah VFX Demo] 1: Mesh Distortion Triggered!");
    }

    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2))
    {
        s_demoSSSActive = !s_demoSSSActive;
        if (s_demoSSSActive)
        {
            SurfaceMaterial_SetSSSExt(1.6f, 2.5f, (Color){ 255, 120, 50, 255 }, 0.35f);
            TraceLog(LOG_INFO, "[Messiah VFX Demo] 2: SSS Translucency ENABLED!");
        }
        else
        {
            SurfaceMaterial_ClearSSS();
            TraceLog(LOG_INFO, "[Messiah VFX Demo] 2: SSS Translucency DISABLED!");
        }
    }

    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3))
    {
        s_demoMeshEmitterActive = !s_demoMeshEmitterActive;
        if (!s_demoMeshEmitterActive && s_demoMeshEmitterHandle >= 0) {
            VFX_KillMeshParticleEmitter(s_demoMeshEmitterHandle);
            s_demoMeshEmitterHandle = -1;
        }
        TraceLog(LOG_INFO, "[Messiah VFX Demo] 3: Skinned Mesh Emitter (O(1)) %s!", s_demoMeshEmitterActive ? "ENABLED" : "DISABLED");
    }

    if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4))
    {
        s_demoCatmullActive = true;
        s_demoCatmullTimer = 0.0f;
        s_demoCatmullPos = playerPos;
        s_demoCatmullYaw = s_currentPlayerYaw;
        TraceLog(LOG_INFO, "[Messiah VFX Demo] 4: Centripetal Catmull-Rom Sword Arc Triggered!");
    }

    if (IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5))
    {
        s_demoIaidoActive = !s_demoIaidoActive;
        s_demoIaidoTimer = 0.0f;
        TraceLog(LOG_INFO, "[Messiah VFX Demo] 5: Iaido Quick-Draw Stance %s!", s_demoIaidoActive ? "ENABLED" : "DISABLED");
    }

    if (IsKeyPressed(KEY_SIX) || IsKeyPressed(KEY_KP_6))
    {
        s_demoGuidingWindActive = !s_demoGuidingWindActive;
        s_demoGuidingWindTimer = 0.0f;
        s_demoGuidingWindCycle = -1;
        if (!s_demoGuidingWindActive) {
            Wind_StopGuidingWind();
        }
        TraceLog(LOG_INFO, "[Messiah VFX Demo] 6: Guiding Wind %s!", s_demoGuidingWindActive ? "ENABLED" : "DISABLED");
    }

    if (IsKeyPressed(KEY_T))
    {
        CameraFX_Shake(0.5f);
        ScreenDistort_Add(playerPos, 0.45f, 0.35f, 0.35f, 1.0f);

        VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 1.5f, 9999.0f, VFX_PRIORITY_LOW);
        DecalSystem_Add(playerPos, (float)GetRandomValue(0, 360), 0.3f,
                        globalParticleTex, 3.0f, ORANGE);

        static ColorGradient g;
        static bool gradientInit = false;
        if (!gradientInit)
        {
            ColorGradient_AddStop(&g, 0.0f, RED);
            ColorGradient_AddStop(&g, 0.25f, ORANGE);
            ColorGradient_AddStop(&g, 0.5f, YELLOW);
            ColorGradient_AddStop(&g, 0.75f, GREEN);
            ColorGradient_AddStop(&g, 1.0f, BLUE);
            gradientInit = true;
        }

        static SpriteAnim anim;
        static bool animInit = false;
        if (!animInit)
        {
            SpriteAnim_Init(&anim, 2, 2, 4, 8.0f, ANIM_LOOP);
            animInit = true;
        }

        static ParticleConfig deathChildConfig;
        deathChildConfig.velocity = (Vector3){0.0f, 0.0f, 0.0f};
        deathChildConfig.colorStart = BLUE;
        deathChildConfig.colorEnd = BLACK;
        deathChildConfig.radius = 0.025f;
        deathChildConfig.lifetime = 1.5f;
        deathChildConfig.gradient = &g;
        deathChildConfig.forceField = NULL;
        deathChildConfig.spriteAnim = NULL;
        deathChildConfig.onLiveEmit = NULL;
        deathChildConfig.onDeathEmit = NULL;

        static ParticleConfig liveChildConfig;
        liveChildConfig.velocity = (Vector3){0.0f, 0.01f, 0.0f};
        liveChildConfig.colorStart = ORANGE;
        liveChildConfig.colorEnd = RED;
        liveChildConfig.radius = 0.03f;
        liveChildConfig.lifetime = 0.8f;
        liveChildConfig.gradient = &g;
        liveChildConfig.forceField = NULL;
        liveChildConfig.spriteAnim = NULL;
        liveChildConfig.onLiveEmit = NULL;
        liveChildConfig.onDeathEmit = NULL;

        ParticleConfig motherConfig = {0};
        motherConfig.position =
            Vector3Add(playerPos, (Vector3){-0.06f, 0.015f, 0.0f});
        motherConfig.velocity = (Vector3){0.12f, 0.0f, 0.0f};
        motherConfig.radius = 0.04f;
        motherConfig.lifetime = 1.5f;
        motherConfig.colorStart = WHITE;
        motherConfig.colorEnd = YELLOW;
        motherConfig.gradient = &g;
        motherConfig.onLiveEmit = &liveChildConfig;
        motherConfig.onLiveEmitRate = 35.0f;
        motherConfig.onDeathEmit = &deathChildConfig;
        motherConfig.onDeathEmitCount = 12;

        SpawnParticle(motherConfig);

        TrailConfig tConfig = {0};
        tConfig.type = TRAIL_TYPE_PROJECTILE;
        tConfig.pos = Vector3Add(playerPos, (Vector3){0.5f, 0.3f, 0.0f});
        tConfig.vel = (Vector3){4.5f, 0.0f, 0.0f};
        tConfig.len = 0.4f;
        tConfig.thick = 0.08f;
        tConfig.trailLength = 1.5f;
        tConfig.life = 3.0f;
        tConfig.gradient = &g;
        tConfig.spriteAnim = &anim;
        tConfig.tex = testAtlasTex;
        SpawnTrailEntity(tConfig);
    }

    // -------------------------------------------------------------------------
    // Test GPU compute force field
    // -------------------------------------------------------------------------
    bool ffTestTouched = !s_hideAllUI && s_vfxHelpOpen &&
        VFXTest_UIButtonReleased(VFXTest_UIForceButton(VFXTest_UIGetLayout(), false));
    if (IsKeyPressed(KEY_F) || ffTestTouched)
    {
        static ForceField s_gpuTestField;
        // Dựng lại field MỖI lần nhấn: origin vortex bám vị trí hiện tại của nhân vật.
        // Bản cũ init một lần -> trục xoáy đóng băng ở vị trí lần nhấn ĐẦU TIÊN, đứng
        // xa trục thì lực 4/(dist+1) yếu dần -> "tỏa hẹp / đứng im" tùy chỗ đứng.
        ForceField_Clear(&s_gpuTestField);
        {
            ForceLayer vortex = {0};
            vortex.type = FORCE_VORTEX;
            vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
            vortex.direction = (Vector3){0.0f, 1.0f, 0.0f};
            vortex.strength = 4.0f;
            ForceField_AddLayer(&s_gpuTestField, vortex);
        }

        Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
        int i;
        for (i = 0; i < 40; i++)
        {
            float ang = ((float)GetRandomValue(0, 359)) * DEG2RAD;
            GpuParticleConfig cfg = {0};
            cfg.position = center;
            cfg.velocity = (Vector3){cosf(ang) * 0.15f, 0.0f, sinf(ang) * 0.15f};
            cfg.colorStart = (Color){80, 200, 255, 255};
            cfg.colorEnd = (Color){80, 200, 255, 0};
            cfg.radius = 0.25f; // 0.06f gần như dưới-pixel ở khoảng cách camera arena --
                                // GPU path chạy lần đầu (GL cũ dùng CPU) mới lộ ra
            cfg.lifetime = 2.5f;
            cfg.drag = 0.0f;
            cfg.forceField = &s_gpuTestField;
            GpuParticleSystem_Spawn(cfg);
        }
    }

    // -------------------------------------------------------------------------
    // Test FORCE_VECTOR_TEXTURE
    // -------------------------------------------------------------------------
    bool vfTestTouched = !s_hideAllUI && s_vfxHelpOpen &&
        VFXTest_UIButtonReleased(VFXTest_UIForceButton(VFXTest_UIGetLayout(), true));
    if (IsKeyPressed(KEY_Y) || vfTestTouched)
    {
        static Texture2D s_flowTex = {0};
        static ForceField s_flowField;
        if (s_flowTex.id == 0)
        {
            Image img = GenImageColor(4, 4, (Color){255, 128, 0, 255});
            s_flowTex = LoadTextureFromImage(img);
            UnloadImage(img);
            GpuParticleSystem_SetVectorFieldTexture(0, s_flowTex);
        }
        // Dựng lại field MỖI lần nhấn (origin bám nhân vật) và mở rộng hộp:
        // FORCE_VECTOR_TEXTURE là HARD BOX (direction.xz = bán kích thước, ngoài hộp
        // lực = 0 — xem gpu_particles.comp). Bản cũ: hộp 0.6x0.6m đóng băng ở lần nhấn
        // đầu, trong khi hàng hạt spawn dài ±0.8m -> đa số hạt ngoài hộp đứng im,
        // hạt trôi ra mép hộp là dừng, đi chỗ khác nhấn thì đứng im toàn bộ.
        ForceField_Clear(&s_flowField);
        {
            ForceLayer vf = {0};
            vf.type = FORCE_VECTOR_TEXTURE;
            vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
            vf.direction = (Vector3){2.5f, 0.0f, 2.5f}; // hộp 5x5m phủ trọn hàng spawn + lối trôi
            vf.strength = 2.5f;
            vf.noiseScale = 0.0f;
            ForceField_AddLayer(&s_flowField, vf);
        }

        Vector3 spawnPos =
            Vector3Add(playerPos, (Vector3){-0.25f, 0.04f, 0.0f});
        int i;
        for (i = 0; i < 20; i++)
        {
            GpuParticleConfig cfg = {0};
            cfg.position = Vector3Add(
                spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80) * 0.01f});
            cfg.velocity = (Vector3){0.0f, 0.0f, 0.0f};
            cfg.colorStart = (Color){255, 220, 100, 255};
            cfg.colorEnd = (Color){255, 220, 100, 0};
            cfg.radius = 0.12f; // 0.008f (8mm) vô hình ở khoảng cách camera -- xem ghi chú FF test
            cfg.lifetime = 3.0f;
            cfg.drag = 0.0f;
            cfg.forceField = &s_flowField;
            GpuParticleSystem_Spawn(cfg);
        }
    }

    // -------------------------------------------------------------------------
    // PREFAB TESTER UI INPUT
    // -------------------------------------------------------------------------
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (VFXTest_UIHandleInput(playerPos)) return true;

    // Click outside UI → spawn
    if (clicked && !s_clickedOnUI)
    {
        // LIGHTNING ARC is the interaction fixture for source -> click, not a
        // static display line. Its fixed generated preview remains available
        // from the panel button, but clicking the world must originate at the
        // actual character socket and terminate exactly at this frame's raycast.
        if (VFXTest_IsNewFxNamed("LIGHTNING ARC"))
        {
            Vector3 castSocket = Vector3Add(playerPos, (Vector3){0.0f, 0.78f, 0.0f});
            VFX_ComposeLightningArc(castSocket, mouseTarget3D, VC_MAT_LIGHTNING, 0.055f);
            return false;
        }
        if (VFXTest_IsNewFxNamed("GUIDED PARTICLE"))
        {
            Vector3 castSocket = Vector3Add(playerPos, (Vector3){0.0f, 0.78f, 0.0f});
            VFXTest_FireGuidedParticle(castSocket, mouseTarget3D);
            s_prefabStartPos = mouseTarget3D;
            return false;
        }

        VFXTest_StopFixtures();
        s_prefabStartPos = mouseTarget3D;

        // Generate random spline path from playerPos (chest) to clicked ground
        Vector3 p0 = Vector3Add(playerPos, (Vector3){0.0f, 0.3f, 0.0f});
        Vector3 p3 = mouseTarget3D;
        float dist = Vector3Distance(p0, p3);
        float offsetScale = dist * 0.25f;
        if (offsetScale < 0.5f)
            offsetScale = 0.5f;

        if (dist > 0.01f)
        {
            Vector3 dir = Vector3Normalize(Vector3Subtract(p3, p0));
            Vector3 upVec = (Vector3){0.0f, 1.0f, 0.0f};
            if (fabsf(dir.y) > 0.9f)
                upVec = (Vector3){1.0f, 0.0f, 0.0f};
            Vector3 right = Vector3Normalize(Vector3CrossProduct(upVec, dir));
            upVec = Vector3CrossProduct(dir, right);

            Vector3 p1 = Vector3Lerp(p0, p3, 0.33f);
            float r1 = ((float)GetRandomValue(-100, 100) / 100.0f) * offsetScale;
            float u1 = ((float)GetRandomValue(-20, 100) / 100.0f) * offsetScale;
            p1 = Vector3Add(p1, Vector3Scale(right, r1));
            p1 = Vector3Add(p1, Vector3Scale(upVec, u1));

            Vector3 p2 = Vector3Lerp(p0, p3, 0.66f);
            float r2 = ((float)GetRandomValue(-100, 100) / 100.0f) * offsetScale;
            float u2 = ((float)GetRandomValue(-20, 100) / 100.0f) * offsetScale;
            p2 = Vector3Add(p2, Vector3Scale(right, r2));
            p2 = Vector3Add(p2, Vector3Scale(upVec, u2));

            for (int idx = 0; idx < TEST_PATH_POINT_COUNT; idx++)
            {
                float t = (float)idx / (float)(TEST_PATH_POINT_COUNT - 1);
                s_testPathPoints[idx] = GetBezierPoint(p0, p1, p2, p3, t);
            }
            s_hasTestPath = true;
        }
        else
        {
            for (int idx = 0; idx < TEST_PATH_POINT_COUNT; idx++)
            {
                s_testPathPoints[idx] = p0;
            }
            s_hasTestPath = true;
        }

        if (s_testCategory == TEST_CAT_MESH)
        {
            s_isPlayingMesh = true;
            s_meshTime = 0.0f;
        }
        else if (s_testCategory == TEST_CAT_NEWFX)
        {
            s_isPlayingMesh = false;
            // @gen:newfx_trigger begin
        if (s_testCategory == TEST_CAT_NEWFX && s_testIndex == 12) {
            Vector3 castSocket = Vector3Add(playerPos, (Vector3){0.0f, 0.78f, 0.0f});
            Vector3 guidedTarget = mouseTarget3D;
            if (s_clickedOnUI) {
                guidedTarget = Vector3Add(playerPos, (Vector3){2.5f, 0.0f, 0.8f});
                guidedTarget.y = MapManager_GetGroundHeightAt(guidedTarget.x, guidedTarget.z);
            }
            VFXTest_FireGuidedParticle(castSocket, guidedTarget);
            return false;
        }
          if (!VFXTest_FireNewFx(s_testIndex, s_prefabStartPos)) {
              /* continuous — handled per-frame in VFXTest_Draw3D */
              switch (s_testIndex) {
              default: break;
              }
              s_isPlayingMesh = true;
              s_meshTime = 0.0f;
          }
// @gen:newfx_trigger end
        }
    }

    return false;
}

static void VFXTest_DrawGroundCircle(Vector3 center, float radius, Color color) {
    int segments = 32;
    Vector3 prevPt = { center.x + radius, center.y, center.z };
    for (int i = 1; i <= segments; i++) {
        float angle = ((float)i / segments) * 2.0f * PI;
        Vector3 currPt = {
            center.x + cosf(angle) * radius,
            center.y,
            center.z + sinf(angle) * radius
        };
        DrawLine3D(prevPt, currPt, color);
        prevPt = currPt;
    }
}

#define MESH_DISTORT_DEMO_DURATION 1.15f

void VFXTest_DrawRefraction(Camera3D cam)
{
    (void)cam;
    if (s_demoMeshDistortActive)
    {
        float progress = s_demoMeshDistortTimer / MESH_DISTORT_DEMO_DURATION;
        if (progress <= 1.0f)
        {
            Vector3 forward = { sinf(s_demoMeshDistortYaw), 0.0f, cosf(s_demoMeshDistortYaw) };
            Vector3 right   = { cosf(s_demoMeshDistortYaw), 0.0f, -sinf(s_demoMeshDistortYaw) };

            // Bay vút về phía trước theo hướng nhân vật đối diện: 0.8m -> 8.5m
            float dist = 0.8f + progress * 7.7f;
            Vector3 slashCenter = Vector3Add(s_demoMeshDistortPos, Vector3Scale(forward, dist));
            slashCenter.y += 0.50f; // Quét sát mặt sàn 50cm để nhìn rõ sàn đấu bị bẻ cong

            float radius = 1.8f + progress * 0.85f;
            float width = 0.85f * (1.0f - progress * 0.25f);
            float arcAngle = 2.4f; // Sải cánh cung rộng 137 độ
            float tiltAngle = 0.18f;

            ScreenDistort_BeginMeshPass((Texture2D){0}, 0.12f, (Vector2){ 2.0f, 0.5f }, (Color){ 160, 225, 255, 100 });
            ProceduralMesh_DrawCrescentSlash(slashCenter, forward, right, radius, width, arcAngle, tiltAngle, WHITE);
            ScreenDistort_EndMeshPass();
        }
        else
        {
            s_demoMeshDistortActive = false;
        }
    }
}

void VFXTest_Draw3D(void)
{
    VFXTest_InitFixtures();
    float dt = TimeFX_RawDelta();

    // -------------------------------------------------------------------------
    // DEMO MESSIAH VFX: [1] Mesh Distort | [2] SSS | [3] Capsule SDF
    // -------------------------------------------------------------------------
    if (s_demoMeshDistortActive)
    {
        s_demoMeshDistortTimer += dt;
        ScreenDistort_RequestMeshPass();
        float progress = s_demoMeshDistortTimer / MESH_DISTORT_DEMO_DURATION;
        if (progress <= 1.0f)
        {
            Vector3 forward = { sinf(s_demoMeshDistortYaw), 0.0f, cosf(s_demoMeshDistortYaw) };
            float dist = 0.8f + progress * 7.7f;
            Vector3 slashCenter = Vector3Add(s_demoMeshDistortPos, Vector3Scale(forward, dist));
            slashCenter.y += 0.50f;
            VFXLight_Spawn(slashCenter, (Color){ 160, 220, 255, 255 }, 2.2f, 0.03f, VFX_PRIORITY_HIGH_ULTIMATE);
            // Gió rẽ dạt cỏ tự nhiên ôm sát theo nhát kiếm khí đang lướt tới
            Wind_SpawnRadialBlast(slashCenter, 1.8f, 3.5f, 0.08f);
            Wind_SpawnGust(slashCenter, forward, 1.5f, 3.8f, 0.08f);
        }
    }

    if (s_demoSSSActive)
    {
        s_demoSSSAngle += dt * 1.8f;
        Vector3 orbitPos = Vector3Add(s_currentPlayerPos, (Vector3){ cosf(s_demoSSSAngle) * 1.5f, 1.15f, sinf(s_demoSSSAngle) * 1.5f });
        VFXLight_Spawn(orbitPos, (Color){ 255, 140, 40, 255 }, 3.8f, 0.05f, VFX_PRIORITY_HIGH_ULTIMATE);
        DrawSphere(orbitPos, 0.14f, (Color){ 255, 230, 110, 255 });
        DrawCircle3D(orbitPos, 0.40f, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f, (Color){ 255, 160, 40, 160 });
    }

    if (s_demoMeshEmitterActive)
    {
        // Dynamic golden aura light centered at chest
        Vector3 chestPos = Vector3Add(s_currentPlayerPos, (Vector3){ 0.0f, 1.10f, 0.0f });
        VFXLight_Spawn(chestPos, (Color){ 255, 200, 75, 255 }, 2.8f, 0.05f, VFX_PRIORITY_HIGH_ULTIMATE);

        if (CharacterModel_IsLoaded()) {
            Matrix target = MatrixMultiply(MatrixRotateY(s_currentPlayerYaw),
                                            MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z));
            s_demoMeshEmitterModel = CharacterModel_GetModel();
            if (s_demoMeshEmitterHandle < 0) {
                s_demoMeshEmitterHandle = VFX_MeshParticleEmitter_Spawn(&(VFX_MeshParticleEmitterDesc){
                    .model = &s_demoMeshEmitterModel, .transform = target,
                    .variant = VFX_MESH_PARTICLE_VARIANT_EMBER_SPARK_LIFT,
                    .material = VC_MAT_FIRE, .intensity = 1.0f, .seed = 3u,
                });
            }
            VFX_MeshParticleEmitter_SetTransform(s_demoMeshEmitterHandle, target);
        }
    }

    if (s_demoCatmullActive)
    {
        s_demoCatmullTimer += dt;
        const float duration = 0.55f;
        float progress = s_demoCatmullTimer / duration;
        if (progress >= 1.0f)
        {
            s_demoCatmullActive = false;
        }
        else
        {
            Color primaryCol = WHITE;
            Color accentCol = (Color){ 65, 215, 255, 255 };

            // Core API: Compose Centripetal Catmull-Rom Slash
            VFX_ComposeCentripetalSlashEx(s_demoCatmullPos, s_demoCatmullYaw, primaryCol, accentCol, progress, duration, s_lastCam);
        }
    }

    if (s_demoIaidoActive)
    {
        s_demoIaidoTimer += dt;
        const float duration = 1.35f;
        float progress = fmodf(s_demoIaidoTimer, duration) / duration;

        // Core API: Compose Iaido Quick-Draw Stance (tracks player continuous movement!)
        VFX_ComposeIaidoStance(s_currentPlayerPos, s_currentPlayerYaw, progress, duration, s_lastCam, NULL);
    }

    if (s_demoGuidingWindActive)
    {
        s_demoGuidingWindTimer += dt;
        const float duration = 2.4f;
        int curCycle = (int)(s_demoGuidingWindTimer / duration);
        if (curCycle != s_demoGuidingWindCycle)
        {
            s_demoGuidingWindCycle = curCycle;
            Vector3 forward = (Vector3){ sinf(s_currentPlayerYaw), 0.0f, cosf(s_currentPlayerYaw) };
            if (Vector3Length(forward) < 0.1f) forward = (Vector3){ 0.0f, 0.0f, 1.0f };
            s_demoGuidingWindStartPos = Vector3Subtract(s_currentPlayerPos, Vector3Scale(forward, 2.8f));
            s_demoGuidingWindTarget   = Vector3Add(s_currentPlayerPos, Vector3Scale(forward, 24.0f));
            Wind_TriggerGuidingWind(s_currentPlayerPos, s_demoGuidingWindTarget, 18.0f, duration);
        }

        float progress = fmodf(s_demoGuidingWindTimer, duration) / duration;

        // Core API: Compose Ghost of Tsushima Guiding Wind
        VFX_ComposeGuidingWind(s_demoGuidingWindStartPos, s_demoGuidingWindTarget, progress, s_lastCam);
    }

    if (s_isPlayingMesh)
    {
        if (IsKeyPressed(KEY_V))
        {
            s_vfxAnimationPaused = !s_vfxAnimationPaused;
            TraceLog(LOG_INFO, "[VFX] Animation %s", s_vfxAnimationPaused ? "PAUSED" : "RESUMED");
        }
        if (!s_vfxAnimationPaused)
        {
            s_meshTime += dt;
        }
        if (CharacterModel_IsLoaded())
            s_meshParticleFixtureModel = CharacterModel_GetModel();

        VFXTest_RefreshInspectorParams(false);

// @gen:newfx_guided_input begin
        if (VFXTest_IsNewFxNamed("GUIDED PARTICLE")) {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0) {
                VFXTest_SetGuidedPreset((s_guidedFixturePreset + direction + VFXTEST_GUIDED_PRESET_COUNT) % VFXTEST_GUIDED_PRESET_COUNT);
                VFXTest_RefreshInspectorParams(true);
                TraceLog(LOG_INFO, "GUIDED PARTICLE preset: %s (>, next; <, previous)",
                         s_guidedFixturePresetNames[s_guidedFixturePreset]);
            } else {
                // Emission and pulse controls appear only while applicable.
                // Rebind the descriptors without losing the selected parameter.
                const char *selectedName = s_inspectorParamCount > 0 ? s_inspectorParams[s_inspectorSelectedParam].name : NULL;
                const char *selectedGroup = s_inspectorParamCount > 0 ? s_inspectorParams[s_inspectorSelectedParam].group : NULL;
                s_inspectorParamCount = VFX_GuidedParticle_GetParams(&s_liveGuidedParticleConfig,
                                                                  s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
                s_inspectorSelectedParam = 0;
                for (int i = 0; i < s_inspectorParamCount; ++i) {
                    if (selectedName && selectedGroup && strcmp(selectedName, s_inspectorParams[i].name) == 0 &&
                        strcmp(selectedGroup, s_inspectorParams[i].group) == 0) {
                        s_inspectorSelectedParam = i;
                        break;
                    }
                }
            }
        }
// @gen:newfx_guided_input end

        if (s_inspectorParamCount > 0)
        {
            // CapsLock (or Tab) cycles through variables [c1 -> c2 -> ... -> a1 -> ... -> b1 ...]
            if (IsKeyPressed(KEY_CAPS_LOCK) || IsKeyPressed(KEY_TAB))
            {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                    s_inspectorSelectedParam = (s_inspectorSelectedParam - 1 + s_inspectorParamCount) % s_inspectorParamCount;
                else
                    s_inspectorSelectedParam = (s_inspectorSelectedParam + 1) % s_inspectorParamCount;
                VFXTest_UIRevealSelectedParameter();

                char valBuf[64];
                VFX_Param_FormatValue(&s_inspectorParams[s_inspectorSelectedParam], valBuf, sizeof(valBuf));
                TraceLog(LOG_INFO, "[Inspector] Selected [%d/%d] %s: %s",
                         s_inspectorSelectedParam + 1, s_inspectorParamCount,
                         s_inspectorParams[s_inspectorSelectedParam].name, valBuf);
            }

            // Slash '/' (and '>' or '.') cycles the value of the selected variable
            if ((IsKeyPressed(KEY_SLASH) || (IsKeyPressed(KEY_PERIOD) && !VFXTest_IsNewFxNamed("GUIDED PARTICLE"))))
            {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                    VFXTest_UIEditParameter(s_inspectorSelectedParam, -1);
                else
                    VFXTest_UIEditParameter(s_inspectorSelectedParam, 1);

                char valBuf[64];
                VFX_Param_FormatValue(&s_inspectorParams[s_inspectorSelectedParam], valBuf, sizeof(valBuf));
                TraceLog(LOG_INFO, "[Inspector] Param '%s' = %s",
                         s_inspectorParams[s_inspectorSelectedParam].name, valBuf);
            }
            else if (IsKeyPressed(KEY_COMMA) && !VFXTest_IsNewFxNamed("GUIDED PARTICLE"))
            {
                VFXTest_UIEditParameter(s_inspectorSelectedParam, -1);
                char valBuf[64];
                VFX_Param_FormatValue(&s_inspectorParams[s_inspectorSelectedParam], valBuf, sizeof(valBuf));
                TraceLog(LOG_INFO, "[Inspector] Param '%s' = %s",
                         s_inspectorParams[s_inspectorSelectedParam].name, valBuf);
            }
        }

        if (VFXTest_IsNewFxNamed("MESH PARTICLE EMITTER"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                int variant = ((int)s_meshParticleFixtureVariant + direction + VFX_MESH_PARTICLE_VARIANT_COUNT) % VFX_MESH_PARTICLE_VARIANT_COUNT;
                s_meshParticleFixtureVariant = (VFX_MeshParticleVariant)variant;
                TraceLog(LOG_INFO, "MESH PARTICLE EMITTER variant: %s (>, next; ,, previous)", VFX_MeshParticleVariant_Name(s_meshParticleFixtureVariant));
            }
            if (s_vfxFixtureHandle[s_testIndex] >= 0)
            {
                Matrix target = MatrixMultiply(MatrixRotateY(s_currentPlayerYaw), MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z));
                VFX_MeshParticleEmitter_SetTransform(s_vfxFixtureHandle[s_testIndex], target);
                VFX_MeshParticleEmitter_SetVariant(s_vfxFixtureHandle[s_testIndex], s_meshParticleFixtureVariant);
            }
        }
        else if (VFXTest_IsNewFxNamed("MOTION RIBBON TRAIL"))
        {
            static const TrailPresetId k_presets[] = {
                MOTION_RIBBON_ENERGY_SILK, MOTION_RIBBON_SMOKE_WISP,
                MOTION_RIBBON_EMBER_FILAMENT, MOTION_RIBBON_WATER_STREAM};
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                int p;
                for (p = 0; p < 4 && k_presets[p] != s_motionRibbonFixturePreset; ++p) {}
                s_motionRibbonFixturePreset = k_presets[(p + direction + 4) % 4];
                if (s_vfxFixtureHandle[s_testIndex] >= 0)
                    VFX_KillTrail(s_vfxFixtureHandle[s_testIndex]);
                s_vfxFixtureHandle[s_testIndex] = -1;
                TraceLog(LOG_INFO, "MOTION RIBBON TRAIL: %s (>, next; ,, previous)",
                         MotionRibbonFixturePresetName(s_motionRibbonFixturePreset));
            }
        }
        else if (VFXTest_IsNewFxNamed("[PARTICLE] SMOKE VOLUME"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_smokeVolumeFixtureStyle = (VFX_SmokeStyle)(((int)s_smokeVolumeFixtureStyle + direction + VFX_SMOKE_STYLE_COUNT) % VFX_SMOKE_STYLE_COUNT);
                TraceLog(LOG_INFO, "SMOKE VOLUME variant: %s (>, next; <, previous)", VFX_SmokeStyle_Name(s_smokeVolumeFixtureStyle));
            }
        }
        else if (VFXTest_IsNewFxNamed("MESH SURFACE AURA"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_meshSurfaceAuraFixtureVariant = (VFX_MeshSurfaceAuraVariant)(((int)s_meshSurfaceAuraFixtureVariant + direction + VFX_MESH_SURFACE_AURA_VARIANT_COUNT) % VFX_MESH_SURFACE_AURA_VARIANT_COUNT);
                TraceLog(LOG_INFO, "MESH SURFACE AURA variant: %s (>, next; <, previous)", VFX_MeshSurfaceAuraVariant_Name(s_meshSurfaceAuraFixtureVariant));
            }
        }
        else if (VFXTest_IsNewFxNamed("DECAL"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_decalFixtureVariant = (VFX_DecalVariant)(((int)s_decalFixtureVariant + direction + VFX_DECAL_VARIANT_COUNT) % VFX_DECAL_VARIANT_COUNT);
                TraceLog(LOG_INFO, "DECAL variant: %s (>, next; <, previous)", VFX_DecalVariant_Name(s_decalFixtureVariant));
                VFX_ComposeDecalVariant(s_prefabStartPos, VC_MAT_FIRE, 1.5f, 0.65f, 1.5f, s_decalFixtureVariant);
            }
        }
        else if (VFXTest_IsNewFxNamed("SURFACE PARTICLE RING"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_surfaceParticleRingFixtureVariant = (VFX_SurfaceParticleRingVariant)(((int)s_surfaceParticleRingFixtureVariant + direction + VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT) % VFX_SURFACE_PARTICLE_RING_VARIANT_COUNT);
                TraceLog(LOG_INFO, "SURFACE PARTICLE RING variant: %s (>, next; <, previous)", VFX_SurfaceParticleRingVariant_Name(s_surfaceParticleRingFixtureVariant));
                VFX_ComposeSurfaceParticleRing(s_prefabStartPos,
                    (s_surfaceParticleRingFixtureVariant == VFX_SURFACE_PARTICLE_RING_VARIANT_ENERGY_WISP ||
                     s_surfaceParticleRingFixtureVariant == VFX_SURFACE_PARTICLE_RING_VARIANT_PLASMA_VORTEX) ? VC_MAT_LIGHTNING : VC_MAT_EARTH,
                    1.5f, 1.0f, s_surfaceParticleRingFixtureVariant);
            }
        }
        else if (VFXTest_IsNewFxNamed("IMPACT DUST"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_impactDustFixtureVariant = (VFX_ImpactDustVariant)(((int)s_impactDustFixtureVariant + direction + VFX_IMPACT_DUST_VARIANT_COUNT) % VFX_IMPACT_DUST_VARIANT_COUNT);
                TraceLog(LOG_INFO, "IMPACT DUST variant: %s (>, next; <, previous)", VFX_ImpactDustVariant_Name(s_impactDustFixtureVariant));
                VFX_ComposeImpactDustVariant(s_prefabStartPos,
                    s_impactDustFixtureVariant == VFX_IMPACT_DUST_VARIANT_ENERGY_WISP ? VC_MAT_LIGHTNING : VC_MAT_EARTH,
                    1.5f, 1.0f, s_impactDustFixtureVariant);
            }
        }
        else if (VFXTest_IsNewFxNamed("AMBIENT FIRE"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_ambientFireFixtureStyle = (VFX_FlameStyle)(((int)s_ambientFireFixtureStyle + direction + VFX_FLAME_STYLE_COUNT) % VFX_FLAME_STYLE_COUNT);
                TraceLog(LOG_INFO, "AMBIENT FIRE style: %s (>, next; <, previous)", VFX_FlameStyle_Name(s_ambientFireFixtureStyle));
            }
        }
        else if (VFXTest_IsNewFxNamed("WOOD VINE") || VFXTest_IsNewFxNamed("WOOD LEAVES") || VFXTest_IsNewFxNamed("WOOD FLOWER") || VFXTest_IsNewFxNamed("WOOD PETALS"))
        {
            if (IsKeyPressed(KEY_SEMICOLON))
            {
                Vector3 spawnOrigin = Vector3Add(s_prefabStartPos, (Vector3){0, 3.2f, 0});
                if (VFXTest_IsNewFxNamed("WOOD FLOWER") || VFXTest_IsNewFxNamed("WOOD PETALS"))
                {
                    VFX_WoodFlowerType flType = VFXTest_IsNewFxNamed("WOOD PETALS") ? s_liveWoodPetalConfig.type : s_liveWoodFlowerConfig.type;
                    VFX_WoodVineStyle flStyle = VFXTest_IsNewFxNamed("WOOD PETALS") ? s_liveWoodPetalConfig.style : s_liveWoodFlowerConfig.style;
                    int count = VFX_Foliage_SpawnFreePetals(spawnOrigin, 1.4f, 80, 0.002f, flType, flStyle);
                    TraceLog(LOG_INFO, "[Foliage] Spawned %d physical petals", count);
                }
                else
                {
                    VFX_WoodVineStyle st = VFXTest_IsNewFxNamed("WOOD VINE") ? s_liveWoodVineConfig.style : s_liveWoodLeavesConfig.style;
                    int count = VFX_Foliage_SpawnFreeLeaves(spawnOrigin, 1.8f, 120, 0.004f, st);
                    TraceLog(LOG_INFO, "[Foliage] Spawned %d physical leaves", count);
                }
            }
            if (IsKeyPressed(KEY_APOSTROPHE))
            {
                if (VFX_FoliageSystem_IsHomingActive())
                {
                    VFX_FoliageSystem_ClearHomingTarget();
                    TraceLog(LOG_INFO, "[Foliage] Homing vortex CLEARED");
                }
                else
                {
                    VFX_FoliageSystem_SetHomingTarget(s_currentPlayerPos, 14.0f, 22.0f);
                    TraceLog(LOG_INFO, "[Foliage] Homing vortex ACTIVATED toward player");
                }
            }
            if (IsKeyPressed(KEY_BACKSLASH))
            {
                int detached = VFX_Foliage_DetachInRadius(s_prefabStartPos, 3.5f, (Vector3){0, 2.5f, 0});
                TraceLog(LOG_INFO, "[Foliage] Detached %d foliage particles in radius 3.5m", detached);
            }
        }


// @gen:newfx_surface_impact_selector_input begin
        else if (VFXTest_IsNewFxNamed("SURFACE IMPACT"))
        {
            int direction = IsKeyPressed(KEY_PERIOD) ? 1 : (IsKeyPressed(KEY_COMMA) ? -1 : 0);
            if (direction != 0)
            {
                s_surfaceImpactFixtureSurface = (VFX_ImpactSurface)(((int)s_surfaceImpactFixtureSurface + direction + VFX_IMPACT_SURFACE_COUNT) % VFX_IMPACT_SURFACE_COUNT);
                TraceLog(LOG_INFO, "SURFACE IMPACT receiver: %s (>, next; ,, previous)",
                         VFXTest_SurfaceImpactReceiverName(s_surfaceImpactFixtureSurface));
                VFX_ComposeSurfaceImpact(s_prefabStartPos, s_surfaceImpactFixtureSurface);
            }
        }
        // @gen:newfx_surface_impact_selector_input end
        if (s_testCategory == TEST_CAT_MESH)
        {
            if (s_testIndex == MATERIAL_OUTPUT_FIXTURE_INDEX)
            {
                VFXTest_DrawMaterialOutputFixture(s_prefabStartPos);
            }
            else
            {
                // DrawEffectMesh preset (0-8)
                Color color = WHITE;
                if (s_testIndex == 0)
                    color = (Color){200, 200, 255, 180}; // Disc
                else if (s_testIndex == 1)
                    color = (Color){255, 200, 100, 180}; // Ring
                DrawEffectMesh((MeshPresetType)s_testIndex, s_prefabStartPos,
                               (Vector3){2.0f, 2.0f, 2.0f}, color);
            }
        }
        else if (s_testCategory == TEST_CAT_NEWFX)
        {
            // @gen:newfx_draw begin
          float progress = fmodf(s_meshTime, 2.0f) * 0.5f;
          switch (s_testIndex) {
              case 3: VFX_ComposeDissolveExit(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress); break;
              case 7:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[7] && s_vfxFixtureHandle[7] >= 0)
                      VFX_KillGasMaterialLab(s_vfxFixtureHandle[7]);
                  if (s_meshTime < s_vfxFixtureLastTime[7]) s_vfxFixtureHandle[7] = -1;
                  s_vfxFixtureLastTime[7] = s_meshTime;
                  if (s_vfxFixtureHandle[7] < 0)
                      s_vfxFixtureHandle[7] = VFX_ComposeGasMaterialLab(s_prefabStartPos, VC_MAT_FIRE);
                  break;
              }
              case 8:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[8] && s_vfxFixtureHandle[8] >= 0)
                      VFX_KillGasPlume(s_vfxFixtureHandle[8]);
                  if (s_meshTime < s_vfxFixtureLastTime[8]) s_vfxFixtureHandle[8] = -1;
                  s_vfxFixtureLastTime[8] = s_meshTime;
                  if (s_vfxFixtureHandle[8] < 0)
                      s_vfxFixtureHandle[8] = VFX_ComposeGasPlume(s_prefabStartPos, VC_MAT_FIRE, NULL);
                  break;
              }
              case 10:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[10] && s_vfxFixtureHandle[10] >= 0)
                      VFX_KillGasVortex(s_vfxFixtureHandle[10]);
                  if (s_meshTime < s_vfxFixtureLastTime[10]) s_vfxFixtureHandle[10] = -1;
                  s_vfxFixtureLastTime[10] = s_meshTime;
                  if (s_vfxFixtureHandle[10] < 0)
                      s_vfxFixtureHandle[10] = VFX_ComposeGasVortex(s_prefabStartPos, VC_MAT_LIGHTNING, NULL);
                  break;
              }
              case 11: VFX_ComposeGroundWave(s_prefabStartPos, VC_MAT_EARTH, 1.5f, progress, VFX_GroundHeightFromMap, NULL); break;
              case 13: VFX_ComposeGuidingWind(s_currentPlayerPos, Vector3Add(s_currentPlayerPos, Vector3Scale((Vector3){sinf(s_currentPlayerYaw), 0.0f, cosf(s_currentPlayerYaw)}, 24.0f)), progress, s_lastCam); break;
              case 14: VFX_ComposeIaidoStance(s_currentPlayerPos, s_currentPlayerYaw, progress, 1.35f, s_lastCam, NULL); break;
              case 16: VFX_ComposeLightShaft(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, 0.8f, 1.35f); break;
              case 20: VFX_DrawModelSurfaceAura(s_meshParticleFixtureModel, MatrixMultiply(MatrixRotateY(s_currentPlayerYaw), MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z)), &(VFX_MeshSurfaceAuraParams){.materialColor=VFX_MeshSurfaceAuraParams_MakeVariant(s_meshSurfaceAuraFixtureVariant).materialColor, .rimWidth=VFX_MeshSurfaceAuraParams_MakeVariant(s_meshSurfaceAuraFixtureVariant).rimWidth, .rimIntensity=VFX_MeshSurfaceAuraParams_MakeVariant(s_meshSurfaceAuraFixtureVariant).rimIntensity, .opacity=VFX_MeshSurfaceAuraParams_MakeVariant(s_meshSurfaceAuraFixtureVariant).opacity}); break;
              case 22: VFX_ComposeOpticalFlare(Vector3Add(s_currentPlayerPos, (Vector3){0.0f, 1.05f, 0.0f}), 0.55f, 2.4f, 1.0f, s_lastCam); break;
              case 23:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[23] && s_vfxFixtureHandle[23] >= 0)
                      VFX_KillRefBands(s_vfxFixtureHandle[23]);
                  if (s_meshTime < s_vfxFixtureLastTime[23]) s_vfxFixtureHandle[23] = -1;
                  s_vfxFixtureLastTime[23] = s_meshTime;
                  if (s_vfxFixtureHandle[23] < 0)
                      s_vfxFixtureHandle[23] = VFX_ComposeRefBands(s_prefabStartPos, 1.5f);
                  break;
              }
              case 24:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[24] && s_vfxFixtureHandle[24] >= 0)
                      VFX_KillRefParticles(s_vfxFixtureHandle[24]);
                  if (s_meshTime < s_vfxFixtureLastTime[24]) s_vfxFixtureHandle[24] = -1;
                  s_vfxFixtureLastTime[24] = s_meshTime;
                  if (s_vfxFixtureHandle[24] < 0)
                      s_vfxFixtureHandle[24] = VFX_ComposeRefParticles(s_prefabStartPos, 1.5f);
                  break;
              }
              case 25:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[25] && s_vfxFixtureHandle[25] >= 0)
                      VFX_RiftBolt_Stop(s_vfxFixtureHandle[25]);
                  if (s_meshTime < s_vfxFixtureLastTime[25]) s_vfxFixtureHandle[25] = -1;
                  s_vfxFixtureLastTime[25] = s_meshTime;
                  s_vfxFixtureXf[25] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[25] < 0)
                      s_vfxFixtureHandle[25] = VFX_ComposeRiftBolt(&s_vfxFixtureXf[25], VC_MAT_FIRE, 0.08f);
                  break;
              }
              case 26: VFX_ComposeRuneCircle(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress, 5); break;
              case 28: VFX_ComposeShockRing(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress); break;
              case 29:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[29] && s_vfxFixtureHandle[29] >= 0)
                      VFX_SmokeColumn_Stop(s_vfxFixtureHandle[29]);
                  if (s_meshTime < s_vfxFixtureLastTime[29]) s_vfxFixtureHandle[29] = -1;
                  s_vfxFixtureLastTime[29] = s_meshTime;
                  if (s_vfxFixtureHandle[29] < 0)
                      s_vfxFixtureHandle[29] = VFX_ComposeSmokeColumn(s_prefabStartPos, VC_MAT_METAL, 0.55f, 5.0f, VFX_COLUMN_SMOKE, true);
                  break;
              }
              case 31: VFX_ComposeSmokeVolume(s_prefabStartPos, 1.5f, 1.0f, s_smokeVolumeFixtureStyle); break;
              case 34: VFX_ComposeSweepSlash(s_prefabStartPos, (Vector3){1.0f, 0.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 90.0f, progress); break;
              case 35:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[35] && s_vfxFixtureHandle[35] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[35]);
                  if (s_meshTime < s_vfxFixtureLastTime[35]) s_vfxFixtureHandle[35] = -1;
                  s_vfxFixtureLastTime[35] = s_meshTime;
                  s_vfxFixtureXf[35] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[35] < 0)
                      s_vfxFixtureHandle[35] = VFX_ComposeTrail(&s_vfxFixtureXf[35], MotionRibbonFixtureMaterial(s_motionRibbonFixturePreset), 0.18f, 2.0f, s_motionRibbonFixturePreset);
                  break;
              }
              case 36: VFX_ComposeVacuumConverge(Vector3Add(s_currentPlayerPos, (Vector3){0.0f, 1.05f, 0.0f}), 2.7f, progress, s_lastCam); break;
              case 37: VFX_ComposeVacuumRing(s_currentPlayerPos, 2.2f, progress); break;
              case 38:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[38] && s_vfxFixtureHandle[38] >= 0)
                      VFX_KillVolumeTrail(s_vfxFixtureHandle[38]);
                  if (s_meshTime < s_vfxFixtureLastTime[38]) s_vfxFixtureHandle[38] = -1;
                  s_vfxFixtureLastTime[38] = s_meshTime;
                  s_vfxFixtureXf[38] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[38] < 0)
                      s_vfxFixtureHandle[38] = VFX_ComposeVolumeTrail(&s_vfxFixtureXf[38], VC_MAT_FIRE, 1.5f, 2.0f, VOL_ENERGY, false);
                  break;
              }
              case 39: VFX_ComposeFissureStreak(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.1f, progress, s_meshTime); break;
              case 40: VFX_ComposeStonePillar(s_prefabStartPos, progress); break;
              case 41: VFX_ComposeAmbientFireEx(s_prefabStartPos, VC_MAT_FIRE, 1.5f, 1.0f, s_ambientFireFixtureStyle); break;
              case 43: VFX_ComposeBlackHole(VC_MAT_FIRE, s_prefabStartPos, 1.5f, s_meshTime); break;
              case 45: VFX_ComposeLiquidBench(s_prefabStartPos, 1.1f, 1.0f); break;
              case 48: VFX_ComposeWaterRing(s_prefabStartPos, 0.9f, 1.0f); break;
              case 49: VFX_ComposeWaterStream(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.33f), (Vector3){0.0f, 0.9f, 0.7f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.66f), (Vector3){0.0f, 0.5f, -0.7f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 1.5f, progress, s_meshTime); break;
              case 50: do { VFX_WoodFlowerConfig _cfg = VFXTest_BuildWoodFlowerConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime); VFX_ComposeWoodFlower(&_cfg); } while(0); break;
              case 51: do { VFX_WoodLeavesConfig _cfg = VFXTest_BuildWoodLeavesConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime); VFX_ComposeWoodLeaves(&_cfg); } while(0); break;
              case 52: do { VFX_WoodPetalConfig _cfg = VFXTest_BuildWoodPetalConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime); VFX_ComposeWoodPetals(&_cfg); } while(0); break;
              case 53: do { VFX_WoodVineConfig _cfg = VFXTest_BuildWoodVineConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime); VFX_ComposeWoodVine(&_cfg); } while(0); break;
          }
// @gen:newfx_draw end
        }
    }

    if (VFXTest_IsNewFxNamed("GUIDED PARTICLE") &&
        (s_prefabStartPos.x != 0.0f || s_prefabStartPos.z != 0.0f)) {
        VFXTest_DrawGroundCircle((Vector3){s_prefabStartPos.x, s_prefabStartPos.y + 0.015f, s_prefabStartPos.z}, 0.25f, ColorAlpha(SKYBLUE, 0.6f));
        VFXTest_DrawGroundCircle((Vector3){s_prefabStartPos.x, s_prefabStartPos.y + 0.015f, s_prefabStartPos.z}, 0.06f, ColorAlpha(WHITE, 0.8f));
    }

    FresnelProbe_Draw3D(s_lastCam);
}

void VFXTest_DrawShadowPass(void)
{
    if (s_isPlayingMesh && s_testIndex >= 0)
    {
        float progress = fmodf(s_meshTime, 2.0f) * 0.5f;

        if (VFXTest_IsNewFxNamed("WOOD VINE"))
        {
            VFX_WoodVineConfig cfg = VFXTest_BuildWoodVineConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime);
            VFX_ComposeWoodVine(&cfg);
        }
        else if (VFXTest_IsNewFxNamed("WOOD LEAVES"))
        {
            VFX_WoodLeavesConfig cfg = VFXTest_BuildWoodLeavesConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime);
            VFX_ComposeWoodLeaves(&cfg);
        }
        else if (VFXTest_IsNewFxNamed("WOOD FLOWER"))
        {
            VFX_WoodFlowerConfig cfg = VFXTest_BuildWoodFlowerConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime);
            VFX_ComposeWoodFlower(&cfg);
        }
        else if (VFXTest_IsNewFxNamed("WOOD PETALS"))
        {
            VFX_WoodPetalConfig cfg = VFXTest_BuildWoodPetalConfig(s_prefabStartPos, s_currentPlayerPos, s_meshTime);
            VFX_ComposeWoodPetals(&cfg);
        }
    }
    VFX_FoliageSystem_DrawShadowPass();
}

void VFXTest_DrawHUD(void)
{
    /* Vẽ trước, đọc sau — cả hai trong pass 2D, trên khung hình đã có pass 3D. */
    ColourProbe_Draw2D();
    ColourProbe_Readback();
    FresnelProbe_Readback();

    /* ĐẦU hàm, trước khi HUD được vẽ — nếu không thì chữ HUD lọt vào ảnh và
     * lại thành một thứ nữa để phán đoán nhầm. */
    if (s_shotWanted)
    {
        s_shotWanted = false;
        char nm[96];
        snprintf(nm, sizeof(nm), "vfx_%d_%02d", s_testIndex, ++s_shotSerial);
        AutoTest_SaveScreenshotWorld(nm, s_lastCam, s_prefabStartPos, s_shotRadius);
    }

    if (s_hideAllUI)
        return; // U — see UpdateAndHandleInput. Nothing below draws.

    // @gen:vfx_ui_draw begin
    VFXTest_UIDraw();
    // @gen:vfx_ui_draw end
}

void VFXTest_SetRenderTarget(int newfxIndex, Vector3 spawnPos)
{
    VFXTest_StopFixtures();
    s_testCategory = TEST_CAT_NEWFX;
    s_testIndex = newfxIndex;
    s_prefabStartPos = spawnPos;
    s_isPlayingMesh = true;
    s_meshTime = 0.0f;

    const char *envR = getenv("WUXING_WOOD_REACTION");
    if (envR && *envR) {
        VFXTest_InitLiveConfigs();
        s_liveWoodVineConfig.reaction = (VFX_WoodReactionState)(atoi(envR) % WOOD_REACTION_COUNT);
    }
    VFXTest_RefreshInspectorParams(true);

    // Fire oneshots immediately for warmup rendering.
    // @gen:newfx_render_trigger begin
    (void)VFXTest_FireNewFx(newfxIndex, spawnPos);
// @gen:newfx_render_trigger end
}

void VFXTest_SetNeutralSmokeRenderTarget(Vector3 spawnPos)
{
    // -1 selects no generated fixture. The ordinary tester still updates and
    // draws the particle systems, but cannot retrigger an elemental preset.
    VFXTest_SetRenderTarget(-1, spawnPos);
    VFX_ComposeSmokePuff(spawnPos, VC_MAT_EARTH, 1.5f, 1.0f);
}

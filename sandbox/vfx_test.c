#include "vfx_test.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/camera_fx.h"
#include "core/time_fx.h"   // TimeFX_RawDelta — headless captures pin dt; GetFrameTime does not
#include "sandbox/auto_test.h"
#include "sandbox/colour_probe.h"
#include "sandbox/fresnel_probe.h"
#include "sandbox/gradient_probe.h"
#include <stdio.h>
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
    if (s_vfxFixtureHandle[24] >= 0)
        VFX_KillRefBands(s_vfxFixtureHandle[24]);
    s_vfxFixtureHandle[24] = -1;
    s_vfxFixtureLastTime[24] = -1.0f;
    if (s_vfxFixtureHandle[25] >= 0)
        VFX_KillRefParticles(s_vfxFixtureHandle[25]);
    s_vfxFixtureHandle[25] = -1;
    s_vfxFixtureLastTime[25] = -1.0f;
    if (s_vfxFixtureHandle[26] >= 0)
        VFX_RiftBolt_Stop(s_vfxFixtureHandle[26]);
    s_vfxFixtureHandle[26] = -1;
    s_vfxFixtureLastTime[26] = -1.0f;
    if (s_vfxFixtureHandle[28] >= 0)
        VFX_KillShieldShell(s_vfxFixtureHandle[28]);
    s_vfxFixtureHandle[28] = -1;
    s_vfxFixtureLastTime[28] = -1.0f;
    if (s_vfxFixtureHandle[30] >= 0)
        VFX_SmokeColumn_Stop(s_vfxFixtureHandle[30]);
    s_vfxFixtureHandle[30] = -1;
    s_vfxFixtureLastTime[30] = -1.0f;
    if (s_vfxFixtureHandle[34] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[34]);
    s_vfxFixtureHandle[34] = -1;
    s_vfxFixtureLastTime[34] = -1.0f;
    if (s_vfxFixtureHandle[35] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[35]);
    s_vfxFixtureHandle[35] = -1;
    s_vfxFixtureLastTime[35] = -1.0f;
    if (s_vfxFixtureHandle[36] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[36]);
    s_vfxFixtureHandle[36] = -1;
    s_vfxFixtureLastTime[36] = -1.0f;
    if (s_vfxFixtureHandle[37] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[37]);
    s_vfxFixtureHandle[37] = -1;
    s_vfxFixtureLastTime[37] = -1.0f;
    if (s_vfxFixtureHandle[38] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[38]);
    s_vfxFixtureHandle[38] = -1;
    s_vfxFixtureLastTime[38] = -1.0f;
    if (s_vfxFixtureHandle[39] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[39]);
    s_vfxFixtureHandle[39] = -1;
    s_vfxFixtureLastTime[39] = -1.0f;
    if (s_vfxFixtureHandle[40] >= 0)
        VFX_KillTrail(s_vfxFixtureHandle[40]);
    s_vfxFixtureHandle[40] = -1;
    s_vfxFixtureLastTime[40] = -1.0f;
    if (s_vfxFixtureHandle[43] >= 0)
        VFX_KillVolumeTrail(s_vfxFixtureHandle[43]);
    s_vfxFixtureHandle[43] = -1;
    s_vfxFixtureLastTime[43] = -1.0f;
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
    case 2: VFX_ComposeDecal(pos, VC_MAT_FIRE, 1.5f, 0.0f, 1.5f); return true;
    case 4: VFX_ComposeEmberBurst(pos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 1.0f); return true;
    case 5: VFX_ComposeFlameJet(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, NULL); return true;
    case 6:
        if (s_vfxFixtureHandle[6] >= 0) VFX_KillFlowShield(s_vfxFixtureHandle[6]);
        s_vfxFixtureHandle[6] = VFX_FlowShield_Spawn(pos, VC_MAT_WATER, 1.5f, 1.0f);
        return true;
    case 9: VFX_ComposeGasShockwave(pos, VC_MAT_VOID, NULL); return true;
    case 11: VFX_ComposeGroundDustRing(pos, VC_MAT_EARTH, 1.5f, 1.0f); return true;
    case 15: VFX_ComposeImpactDust(pos, VC_MAT_EARTH, 1.5f, 0.0f); return true;
    case 17: VFX_ComposeLightningArc(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_LIGHTNING, 0.055f); return true;
    case 18: VFX_ComposeLightningGroundRicochet(pos, VC_MAT_LIGHTNING, 1.0f, posSeed); return true;
    case 19:
        if (s_vfxFixtureHandle[19] >= 0) VFX_KillMeshParticleEmitter(s_vfxFixtureHandle[19]);
        s_vfxFixtureHandle[19] = VFX_ComposeMeshParticleEmitter(&(VFX_MeshParticleEmitterDesc){.model=&s_meshParticleFixtureModel, .transform=MatrixMultiply(MatrixRotateY(s_currentPlayerYaw), MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z)), .variant=s_meshParticleFixtureVariant, .material=VC_MAT_LIGHTNING, .intensity=1.0f, .seed=0x4d455348u});
        return true;
    case 21: VFX_ComposeMistVeil(pos, 5.5f, 4.5f); return true;
    case 23: VFX_ComposeGuidedParticle(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f})); return true;
    case 28:
        if (s_vfxFixtureHandle[28] >= 0) VFX_KillShieldShell(s_vfxFixtureHandle[28]);
        s_vfxFixtureHandle[28] = VFX_ShieldShell_Spawn(pos, VC_MAT_WATER, 1.5f, 1.0f);
        return true;
    case 31: VFX_ComposeSmokePuff(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 47: VFX_ComposeFireballBurst(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 49: VFX_ComposeFluidImpact(pos); return true;
    case 50: VFX_ComposeIceCrystal(pos, posSeed); return true;
    case 52: VFX_ComposeWaterOrb(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f})); return true;
    default: return false;
    }
// @gen:newfx_fire end
}

static bool s_isPanelOpen = true;
static bool s_clickedOnUI = false;
static int s_newfxFilter = NEWFX_CAT_COMMON;

// MESH: 0-8=DrawEffectMesh presets; 9=sandbox-only material output contract.
static const char *s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON",
    "VFX OUTPUT"};

// @gen:newfx_names begin
// 55 entries — auto-managed by sync_vfx_test.py
static const char* s_newFxNames[] = {
    "CONTACT SPARK", "DEBRIS SHARDS", "DECAL", "DISSOLVE EXIT", "[PARTICLE] EMBER BURST", "FLAME JET",
    "FLOW SHIELD", "GAS MATERIAL LAB", "[GAS] GAS PLUME", "GAS SHOCKWAVE", "GAS VORTEX", "GROUND DUST RING",
    "GROUND WAVE", "GUIDING WIND", "IAIDO STANCE", "IMPACT DUST", "LIGHT SHAFT", "LIGHTNING ARC",
    "LIGHTNING IMPACT", "MESH PARTICLE EMITTER", "MESH SURFACE AURA", "MIST VEIL", "OPTICAL FLARE", "GUIDED PARTICLE",
    "REF BANDS", "REF PARTICLES", "RIFT BOLT", "RUNE CIRCLE", "SHIELD SHELL", "SHOCK RING",
    "SMOKE COLUMN", "SMOKE PUFF", "[PARTICLE] SMOKE VOLUME", "SWEEP SLASH", "TRAIL MAIN", "TRAIL ENERGY",
    "TRAIL BLADE", "TRAIL WISP", "TRAIL BACKDROP", "TRAIL SMOKE", "TRAIL MAGIC", "VACUUM CONVERGE",
    "VACUUM RING", "[TRAIL/FLOW] VOLUME TRAIL", "FISSURE STREAK", "STONE PILLAR", "AMBIENT FIRE", "FIREBALL BURST",
    "BLACK HOLE", "FLUID IMPACT", "ICE CRYSTAL", "LIQUID BENCH", "WATER ORB", "WATER RING",
    "WATER STREAM",
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
    6, 4, 4, 6, 6, 6, 6, 6, 6, 6,
    6, 1, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 4, 4, 0, 0, 5, 1,
    1, 1, 1, 1, 1,
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
    /* TAB — hiện/ẩn HUD debug (pool GPU particle, skill manager, core-test). */
    if (IsKeyPressed(KEY_TAB))
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
    bool ffTestTouched =
        !s_hideAllUI &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointCircle(GetMousePosition(),
                                  (Vector2){FF_TEST_BTN_X, FF_TEST_BTN_Y},
                                  FF_TEST_BTN_RADIUS);
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
    bool vfTestTouched =
        !s_hideAllUI &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointCircle(GetMousePosition(),
                                  (Vector2){VF_TEST_BTN_X, VF_TEST_BTN_Y},
                                  VF_TEST_BTN_RADIUS);
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
    Vector2 mousePos = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // y=95 (not 15): Android reserves the top 84px as a mandatory system-gesture inset that
    // intermittently steals finger taps there (see the long note in sandbox/ui_panel.c). Also
    // arm-on-DOWN / fire-on-RELEASE instead of IsMouseButtonPressed: the down-frame position is
    // often stale on Android, so a naive down-edge check highlights but never fires.
    Rectangle toggleBtn = {20, 95, 180, 32};
    Rectangle backBtn = {210, 95, 180, 32};
    bool downNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    static bool s_toggleArmed = false, s_backArmed = false;

    bool overToggleBtn = CheckCollisionPointRec(mousePos, toggleBtn);
    if (overToggleBtn)
    {
        s_clickedOnUI = true;
        if (downNow)
            s_toggleArmed = true;
    }
    if (s_toggleArmed && !downNow)
    {
        s_toggleArmed = false;
        if (overToggleBtn)
            s_isPanelOpen = !s_isPanelOpen;
    }

    bool overBackBtn = CheckCollisionPointRec(mousePos, backBtn);
    if (overBackBtn)
    {
        s_clickedOnUI = true;
        if (downNow)
            s_backArmed = true;
    }
    if (s_backArmed && !downNow)
    {
        s_backArmed = false;
        if (overBackBtn)
            return true; // Request back to menu
    }

    if (s_isPlayingMesh && s_testCategory == TEST_CAT_MESH)
    {
        s_meshTime += TimeFX_RawDelta();
        if (s_testIndex != MATERIAL_OUTPUT_FIXTURE_INDEX && s_meshTime > 5.0f)
            s_isPlayingMesh = false;
    }

    if (s_isPanelOpen)
    {
        float startX = 20.0f;
        float startY = 150.0f; // shifted down with the toggle/back row (clear of top-84px gesture inset)
        float tabW = 120.0f;
        float tabH = 35.0f;
        float spacing = 10.0f;

        // Check tabs (2 tabs: MESH and NEWFX)
        {
            int i;
            for (i = 0; i < TEST_CAT_COUNT; i++)
            {
                Rectangle tabRec = {startX + i * (tabW + spacing), startY, tabW, tabH};
                if (CheckCollisionPointRec(mousePos, tabRec))
                {
                    s_clickedOnUI = true;
                    if (clicked)
                    {
                        VFXTest_StopFixtures();
                        s_testCategory = i;
                        s_testIndex = 0;
                    }
                }
            }
        }

        // NEWFX sub-filter row
        if (s_testCategory == TEST_CAT_NEWFX)
        {
            float filterY = startY + tabH + 8.0f;
            float filterBtnW = 72.0f;
            float filterBtnH = 26.0f;
            int fi;
            for (fi = 0; fi < NEWFX_CAT_COUNT; fi++)
            {
                Rectangle fBtnRec = {startX + fi * (filterBtnW + 4.0f), filterY, filterBtnW, filterBtnH};
                if (CheckCollisionPointRec(mousePos, fBtnRec))
                {
                    s_clickedOnUI = true;
                    if (clicked)
                    {
                        VFXTest_StopFixtures();
                        s_newfxFilter = fi;
                        s_isPlayingMesh = false;
                        for (int newfxIdx = 0;
                             newfxIdx < (int)(sizeof(s_newFxCategories) / sizeof(s_newFxCategories[0]));
                             newfxIdx++)
                        {
                            if (s_newFxCategories[newfxIdx] == s_newfxFilter)
                            {
                                s_testIndex = newfxIdx;
                                break;
                            }
                        }
                    }
                }
            }
        }

        float gridY = startY + tabH + 20.0f;
        float btnW = 110.0f;
        float btnH = 35.0f;
        int columns = 6;

        // Push gridY down for filter row when in NEWFX
        if (s_testCategory == TEST_CAT_NEWFX)
            gridY += 26.0f + 10.0f;

        if (s_testCategory == TEST_CAT_MESH)
        {
            int maxIdx = (int)(sizeof(s_meshNames) / sizeof(s_meshNames[0]));
            int i;
            for (i = 0; i < maxIdx; i++)
            {
                int col = i % columns;
                int row = i / columns;
                Rectangle btnRec = {startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH};
                if (CheckCollisionPointRec(mousePos, btnRec))
                {
                    s_clickedOnUI = true;
                    if (clicked)
                    {
                        VFXTest_StopFixtures();
                        s_testIndex = i;
                        s_isPlayingMesh = false;
                        s_meshTime = 0.0f;
                    }
                }
            }
        }
        else if (s_testCategory == TEST_CAT_NEWFX)
        {
            int maxIdx;
            const char **names;
            int globalIdx;
            int visualIdx;
            maxIdx = 55;
            names = s_newFxNames; // @gen:newfx_count
            visualIdx = 0;
            (void)names;
            for (globalIdx = 0; globalIdx < maxIdx; globalIdx++)
            {
                int cat = s_newFxCategories[globalIdx];
                if (cat != s_newfxFilter)
                    continue;
                int col = visualIdx % columns;
                int row = visualIdx / columns;
                Rectangle btnRec = {startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH};
                if (CheckCollisionPointRec(mousePos, btnRec))
                {
                    s_clickedOnUI = true;
                    if (clicked)
                    {
                        VFXTest_StopFixtures();
                        s_testIndex = globalIdx;
                        s_isPlayingMesh = false;
                        // @gen:newfx_trigger begin
        if (s_testCategory == TEST_CAT_NEWFX && s_testIndex == 23) {
            Vector3 castSocket = Vector3Add(playerPos, (Vector3){0.0f, 0.78f, 0.0f});
            Vector3 guidedTarget = mouseTarget3D;
            if (s_clickedOnUI) {
                guidedTarget = Vector3Add(playerPos, (Vector3){2.5f, 0.0f, 0.8f});
                guidedTarget.y = MapManager_GetGroundHeightAt(guidedTarget.x, guidedTarget.z);
            }
            VFX_ComposeGuidedParticle(castSocket, guidedTarget);
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
                visualIdx++;
            }
        }

        // Mark UI if hovering over background panel
        {
            float bgW = columns * (btnW + spacing) + 10.0f;
            float bgH = (s_testCategory == TEST_CAT_NEWFX) ? 490.0f : 360.0f;
            Rectangle bgBox = {startX - 10, startY - 10, bgW, bgH};
            if (CheckCollisionPointRec(mousePos, bgBox))
                s_clickedOnUI = true;
        }
    }

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
            VFX_ComposeGuidedParticle(castSocket, mouseTarget3D);
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
        if (s_testCategory == TEST_CAT_NEWFX && s_testIndex == 23) {
            Vector3 castSocket = Vector3Add(playerPos, (Vector3){0.0f, 0.78f, 0.0f});
            Vector3 guidedTarget = mouseTarget3D;
            if (s_clickedOnUI) {
                guidedTarget = Vector3Add(playerPos, (Vector3){2.5f, 0.0f, 0.8f});
                guidedTarget.y = MapManager_GetGroundHeightAt(guidedTarget.x, guidedTarget.z);
            }
            VFX_ComposeGuidedParticle(castSocket, guidedTarget);
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
        s_meshTime += dt;
        if (CharacterModel_IsLoaded())
            s_meshParticleFixtureModel = CharacterModel_GetModel();

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
              case 12: VFX_ComposeGroundWave(s_prefabStartPos, VC_MAT_EARTH, 1.5f, progress, VFX_GroundHeightFromMap, NULL); break;
              case 13: VFX_ComposeGuidingWind(s_currentPlayerPos, Vector3Add(s_currentPlayerPos, Vector3Scale((Vector3){sinf(s_currentPlayerYaw), 0.0f, cosf(s_currentPlayerYaw)}, 24.0f)), progress, s_lastCam); break;
              case 14: VFX_ComposeIaidoStance(s_currentPlayerPos, s_currentPlayerYaw, progress, 1.35f, s_lastCam, NULL); break;
              case 16: VFX_ComposeLightShaft(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, 0.8f, 1.35f); break;
              case 20: VFX_DrawModelSurfaceAura(s_meshParticleFixtureModel, MatrixMultiply(MatrixRotateY(s_currentPlayerYaw), MatrixTranslate(s_currentPlayerPos.x, s_currentPlayerPos.y, s_currentPlayerPos.z)), &(VFX_MeshSurfaceAuraParams){.materialColor=(Color){130, 210, 255, 255}, .rimWidth=2.8f, .rimIntensity=0.70f, .opacity=0.32f}); break;
              case 22: VFX_ComposeOpticalFlare(Vector3Add(s_currentPlayerPos, (Vector3){0.0f, 1.05f, 0.0f}), 0.55f, 2.4f, 1.0f, s_lastCam); break;
              case 24:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[24] && s_vfxFixtureHandle[24] >= 0)
                      VFX_KillRefBands(s_vfxFixtureHandle[24]);
                  if (s_meshTime < s_vfxFixtureLastTime[24]) s_vfxFixtureHandle[24] = -1;
                  s_vfxFixtureLastTime[24] = s_meshTime;
                  if (s_vfxFixtureHandle[24] < 0)
                      s_vfxFixtureHandle[24] = VFX_ComposeRefBands(s_prefabStartPos, 1.5f);
                  break;
              }
              case 25:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[25] && s_vfxFixtureHandle[25] >= 0)
                      VFX_KillRefParticles(s_vfxFixtureHandle[25]);
                  if (s_meshTime < s_vfxFixtureLastTime[25]) s_vfxFixtureHandle[25] = -1;
                  s_vfxFixtureLastTime[25] = s_meshTime;
                  if (s_vfxFixtureHandle[25] < 0)
                      s_vfxFixtureHandle[25] = VFX_ComposeRefParticles(s_prefabStartPos, 1.5f);
                  break;
              }
              case 26:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[26] && s_vfxFixtureHandle[26] >= 0)
                      VFX_RiftBolt_Stop(s_vfxFixtureHandle[26]);
                  if (s_meshTime < s_vfxFixtureLastTime[26]) s_vfxFixtureHandle[26] = -1;
                  s_vfxFixtureLastTime[26] = s_meshTime;
                  s_vfxFixtureXf[26] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[26] < 0)
                      s_vfxFixtureHandle[26] = VFX_ComposeRiftBolt(&s_vfxFixtureXf[26], VC_MAT_FIRE, 0.08f);
                  break;
              }
              case 27: VFX_ComposeRuneCircle(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress, 5); break;
              case 29: VFX_ComposeShockRing(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress); break;
              case 30:
              {
                  if (s_meshTime < s_vfxFixtureLastTime[30] && s_vfxFixtureHandle[30] >= 0)
                      VFX_SmokeColumn_Stop(s_vfxFixtureHandle[30]);
                  if (s_meshTime < s_vfxFixtureLastTime[30]) s_vfxFixtureHandle[30] = -1;
                  s_vfxFixtureLastTime[30] = s_meshTime;
                  if (s_vfxFixtureHandle[30] < 0)
                      s_vfxFixtureHandle[30] = VFX_ComposeSmokeColumn(s_prefabStartPos, VC_MAT_METAL, 0.55f, 5.0f, VFX_COLUMN_SMOKE, true);
                  break;
              }
              case 32: VFX_ComposeSmokeVolume(s_prefabStartPos, 1.5f, 1.0f, 0); break;
              case 33: VFX_ComposeSweepSlash(s_prefabStartPos, (Vector3){1.0f, 0.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 90.0f, progress); break;
              case 34:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[34] && s_vfxFixtureHandle[34] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[34]);
                  if (s_meshTime < s_vfxFixtureLastTime[34]) s_vfxFixtureHandle[34] = -1;
                  s_vfxFixtureLastTime[34] = s_meshTime;
                  s_vfxFixtureXf[34] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[34] < 0)
                      s_vfxFixtureHandle[34] = VFX_ComposeTrail(&s_vfxFixtureXf[34], VC_MAT_FIRE, 0.1f, 2.0f, TRAIL_PRESET_MAIN);
                  break;
              }
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
                      s_vfxFixtureHandle[35] = VFX_ComposeTrail(&s_vfxFixtureXf[35], VC_MAT_FIRE, 0.0f, 2.0f, TRAIL_PRESET_ENERGY);
                  break;
              }
              case 36:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[36] && s_vfxFixtureHandle[36] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[36]);
                  if (s_meshTime < s_vfxFixtureLastTime[36]) s_vfxFixtureHandle[36] = -1;
                  s_vfxFixtureLastTime[36] = s_meshTime;
                  s_vfxFixtureXf[36] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[36] < 0)
                      s_vfxFixtureHandle[36] = VFX_ComposeTrail(&s_vfxFixtureXf[36], VC_MAT_FIRE, 0.1f, 2.0f, TRAIL_PRESET_BLADE);
                  break;
              }
              case 37:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[37] && s_vfxFixtureHandle[37] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[37]);
                  if (s_meshTime < s_vfxFixtureLastTime[37]) s_vfxFixtureHandle[37] = -1;
                  s_vfxFixtureLastTime[37] = s_meshTime;
                  s_vfxFixtureXf[37] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[37] < 0)
                      s_vfxFixtureHandle[37] = VFX_ComposeTrail(&s_vfxFixtureXf[37], VC_MAT_FIRE, 0.1f, 2.0f, TRAIL_PRESET_WISP);
                  break;
              }
              case 38:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[38] && s_vfxFixtureHandle[38] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[38]);
                  if (s_meshTime < s_vfxFixtureLastTime[38]) s_vfxFixtureHandle[38] = -1;
                  s_vfxFixtureLastTime[38] = s_meshTime;
                  s_vfxFixtureXf[38] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[38] < 0)
                      s_vfxFixtureHandle[38] = VFX_ComposeTrail(&s_vfxFixtureXf[38], VC_MAT_FIRE, 0.15f, 2.0f, TRAIL_PRESET_BACKDROP);
                  break;
              }
              case 39:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[39] && s_vfxFixtureHandle[39] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[39]);
                  if (s_meshTime < s_vfxFixtureLastTime[39]) s_vfxFixtureHandle[39] = -1;
                  s_vfxFixtureLastTime[39] = s_meshTime;
                  s_vfxFixtureXf[39] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[39] < 0)
                      s_vfxFixtureHandle[39] = VFX_ComposeTrail(&s_vfxFixtureXf[39], VC_MAT_FIRE, 0.0f, 2.0f, TRAIL_PRESET_SMOKE);
                  break;
              }
              case 40:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[40] && s_vfxFixtureHandle[40] >= 0)
                      VFX_KillTrail(s_vfxFixtureHandle[40]);
                  if (s_meshTime < s_vfxFixtureLastTime[40]) s_vfxFixtureHandle[40] = -1;
                  s_vfxFixtureLastTime[40] = s_meshTime;
                  s_vfxFixtureXf[40] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[40] < 0)
                      s_vfxFixtureHandle[40] = VFX_ComposeTrail(&s_vfxFixtureXf[40], VC_MAT_FIRE, 0.0f, 2.0f, TRAIL_PRESET_MAGIC);
                  break;
              }
              case 41: VFX_ComposeVacuumConverge(Vector3Add(s_currentPlayerPos, (Vector3){0.0f, 1.05f, 0.0f}), 2.7f, progress, s_lastCam); break;
              case 42: VFX_ComposeVacuumRing(s_currentPlayerPos, 2.2f, progress); break;
              case 43:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[43] && s_vfxFixtureHandle[43] >= 0)
                      VFX_KillVolumeTrail(s_vfxFixtureHandle[43]);
                  if (s_meshTime < s_vfxFixtureLastTime[43]) s_vfxFixtureHandle[43] = -1;
                  s_vfxFixtureLastTime[43] = s_meshTime;
                  s_vfxFixtureXf[43] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[43] < 0)
                      s_vfxFixtureHandle[43] = VFX_ComposeVolumeTrail(&s_vfxFixtureXf[43], VC_MAT_FIRE, 1.5f, 2.0f, VOL_ENERGY, false);
                  break;
              }
              case 44: VFX_ComposeFissureStreak(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.1f, progress, s_meshTime); break;
              case 45: VFX_ComposeStonePillar(s_prefabStartPos, progress); break;
              case 46: VFX_ComposeAmbientFire(s_prefabStartPos, VC_MAT_FIRE, 1.5f, 1.0f); break;
              case 48: VFX_ComposeBlackHole(VC_MAT_FIRE, s_prefabStartPos, 1.5f, s_meshTime); break;
              case 51: VFX_ComposeLiquidBench(s_prefabStartPos, 1.1f, 1.0f); break;
              case 53: VFX_ComposeWaterRing(s_prefabStartPos, 0.9f, 1.0f); break;
              case 54: VFX_ComposeWaterStream(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.33f), (Vector3){0.0f, 0.9f, 0.7f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.66f), (Vector3){0.0f, 0.5f, -0.7f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 1.5f, progress, s_meshTime); break;
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

    DrawText(TextFormat("[1] MESH DISTORT | [2] SSS %s | [3] EMITTER %s | [4] SLASH | [5] IAIDO %s | [6] GUIDING WIND %s",
                        s_demoSSSActive ? "[ON]" : "[OFF]",
                        s_demoMeshEmitterActive ? "[ON]" : "[OFF]",
                        s_demoIaidoActive ? "[ON]" : "[OFF]",
                        s_demoGuidingWindActive ? "[ON]" : "[OFF]"),
             10, 565, 16, YELLOW);
    DrawText(TextFormat("B: character ref %s | Z/C: punch/kick | TAB: debug HUD %s | N: dark bg | R: reset view",
                        s_hideCharacterRef ? "hidden" : "shown",
                        s_hideDebugOverlays ? "hidden" : "shown"),
             10, 590, 16, GRAY);
    if (s_testCategory == TEST_CAT_MESH &&
        s_testIndex == MATERIAL_OUTPUT_FIXTURE_INDEX && s_isPlayingMesh)
    {
        DrawText("VFX OUTPUT: LEFT ALPHA | CENTER PREMULT | RIGHT ADDITIVE",
                 10, 570, 16, SKYBLUE);
        DrawText("Contract check: same hue family; brightness is intentionally not equal",
                 10, 550, 14, LIGHTGRAY);
    }
    if (s_isPlayingMesh && VFXTest_IsNewFxNamed("MESH PARTICLE EMITTER"))
    {
        DrawText(TextFormat("MESH PARTICLE EMITTER: %s   > next   , previous",
                            VFX_MeshParticleVariant_Name(s_meshParticleFixtureVariant)),
                 10, 525, 16, SKYBLUE);
    }
    if (!s_hideDebugOverlays)
    {
        VFXLightData activeLights[MAX_VFX_LIGHTS];
        int activeCount = 0;
        VFXLight_GetActive(activeLights, &activeCount, MAX_VFX_LIGHTS);
        DrawText(TextFormat("Active VFX Lights: %d / 8", activeCount), 10, 610,
                 20, ORANGE);
        GpuParticleSystem_DrawDebug(10, 635);
    }

    // FF TEST button
    DrawCircle((int)FF_TEST_BTN_X, (int)FF_TEST_BTN_Y, FF_TEST_BTN_RADIUS,
               ColorAlpha(SKYBLUE, 0.5f));
    DrawCircleLines((int)FF_TEST_BTN_X, (int)FF_TEST_BTN_Y, FF_TEST_BTN_RADIUS,
                    SKYBLUE);
    DrawText("FF", (int)FF_TEST_BTN_X - 14, (int)FF_TEST_BTN_Y - 12, 20, WHITE);
    DrawText("TEST", (int)FF_TEST_BTN_X - 22, (int)FF_TEST_BTN_Y + 10, 14, WHITE);

    // VF TEST button
    DrawCircle((int)VF_TEST_BTN_X, (int)VF_TEST_BTN_Y, VF_TEST_BTN_RADIUS,
               ColorAlpha(GOLD, 0.5f));
    DrawCircleLines((int)VF_TEST_BTN_X, (int)VF_TEST_BTN_Y, VF_TEST_BTN_RADIUS,
                    GOLD);
    DrawText("VF", (int)VF_TEST_BTN_X - 14, (int)VF_TEST_BTN_Y - 12, 20, WHITE);
    DrawText("TEST", (int)VF_TEST_BTN_X - 22, (int)VF_TEST_BTN_Y + 10, 14, WHITE);

    // Toggle and back buttons (y=95: clear of the top-84px Android system-gesture inset)
    Rectangle toggleBtn = {20, 95, 180, 32};
    Rectangle backBtn = {210, 95, 180, 32};
    Vector2 mousePos = GetMousePosition();

    bool isOverToggle = CheckCollisionPointRec(mousePos, toggleBtn);
    Color toggleCol = s_isPanelOpen ? (isOverToggle ? RED : MAROON) : (isOverToggle ? LIME : DARKGREEN);
    DrawRectangleRounded(toggleBtn, 0.2f, 10, toggleCol);
    DrawRectangleRoundedLines(toggleBtn, 0.2f, 10, WHITE);
    {
        const char *toggleText = s_isPanelOpen ? "[X] AN BANG DIEU KHIEN" : "[+] HIEN BANG DIEU KHIEN";
        int tW = MeasureText(toggleText, 10);
        DrawText(toggleText, (int)(toggleBtn.x + (toggleBtn.width - tW) / 2), (int)(toggleBtn.y + 11), 10, WHITE);
    }

    bool isOverBack = CheckCollisionPointRec(mousePos, backBtn);
    Color backCol = isOverBack ? MAROON : DARKGRAY;
    DrawRectangleRounded(backBtn, 0.2f, 10, backCol);
    DrawRectangleRoundedLines(backBtn, 0.2f, 10, WHITE);
    {
        const char *backText = "[<] QUAY LAI MENU";
        int bW = MeasureText(backText, 10);
        DrawText(backText, (int)(backBtn.x + (backBtn.width - bW) / 2), (int)(backBtn.y + 11), 10, WHITE);
    }

    if (!s_isPanelOpen)
        return;

    // Background dim
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), ColorAlpha(BLACK, 0.4f));

    float startX = 20.0f;
    float startY = 150.0f; // shifted down with the toggle/back row (clear of top-84px gesture inset)
    float tabW = 120.0f;
    float tabH = 35.0f;
    float spacing = 10.0f;

    float btnW = 110.0f;
    float btnH = 35.0f;
    int columns = 6;

    {
        float bgW = columns * (btnW + spacing) + 10.0f;
        float bgH = (s_testCategory == TEST_CAT_NEWFX) ? 490.0f : 360.0f;
        Rectangle bgBox = {startX - 10, startY - 10, bgW, bgH};
        DrawRectangleRounded(bgBox, 0.05f, 10, ColorAlpha(BLACK, 0.6f));
        DrawRectangleRoundedLines(bgBox, 0.05f, 10, ColorAlpha(WHITE, 0.3f));
    }

    // Draw tabs (2 tabs)
    {
        const char *tabNames[] = {"MESH", "NEW FX"};
        int i;
        for (i = 0; i < TEST_CAT_COUNT; i++)
        {
            Rectangle tabRec = {startX + i * (tabW + spacing), startY, tabW, tabH};
            bool isHover = CheckCollisionPointRec(mousePos, tabRec);
            bool isSelected = (s_testCategory == i);
            Color btnCol = isSelected ? ORANGE : (isHover ? DARKGRAY : ColorAlpha(DARKGRAY, 0.5f));
            DrawRectangleRounded(tabRec, 0.3f, 10, btnCol);
            DrawRectangleRoundedLines(tabRec, 0.3f, 10, WHITE);
            int textW = MeasureText(tabNames[i], 12);
            DrawText(tabNames[i], (int)(tabRec.x + (tabW - textW) / 2), (int)(tabRec.y + 11), 12, isSelected ? BLACK : WHITE);
        }
    }
    float gridY = startY + tabH + 20.0f;

    // NEWFX sub-filter row
    if (s_testCategory == TEST_CAT_NEWFX)
    {
        const char *filterNames[] = {"FIRE", "WATER", "WOOD", "METAL", "EARTH", "TAIJI", "COMMON"};
        float filterY = startY + tabH + 8.0f;
        float filterBtnW = 72.0f;
        float filterBtnH = 26.0f;
        int fi;
        for (fi = 0; fi < NEWFX_CAT_COUNT; fi++)
        {
            Rectangle fBtnRec = {startX + fi * (filterBtnW + 4.0f), filterY, filterBtnW, filterBtnH};
            bool fHover = CheckCollisionPointRec(mousePos, fBtnRec);
            bool fSel = (s_newfxFilter == fi);
            Color fCol = fSel ? ORANGE : (fHover ? DARKGRAY : ColorAlpha(DARKGRAY, 0.5f));
            DrawRectangleRounded(fBtnRec, 0.3f, 6, fCol);
            DrawRectangleRoundedLines(fBtnRec, 0.3f, 6, WHITE);
            int fw = MeasureText(filterNames[fi], 10);
            DrawText(filterNames[fi], (int)(fBtnRec.x + (filterBtnW - fw) / 2), (int)(fBtnRec.y + 8), 10, fSel ? BLACK : WHITE);
        }
        // Push gridY down to accommodate filter row
        gridY += filterBtnH + 10.0f;
    }

    // Draw button grid
    if (s_testCategory == TEST_CAT_MESH)
    {
        int maxIdx = (int)(sizeof(s_meshNames) / sizeof(s_meshNames[0]));
        int i;
        for (i = 0; i < maxIdx; i++)
        {
            int col = i % columns;
            int row = i / columns;
            Rectangle btnRec = {startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH};
            bool isHover = CheckCollisionPointRec(mousePos, btnRec);
            Color btnCol = (s_testIndex == i) ? ORANGE : (isHover ? MAROON : ColorAlpha(DARKGRAY, 0.5f));
            DrawRectangleRounded(btnRec, 0.3f, 10, btnCol);
            DrawRectangleRoundedLines(btnRec, 0.3f, 10, WHITE);
            int textW = MeasureText(s_meshNames[i], 12);
            DrawText(s_meshNames[i], (int)(btnRec.x + (btnW - textW) / 2), (int)(btnRec.y + 11), 12, WHITE);
        }
    }
    else if (s_testCategory == TEST_CAT_NEWFX)
    {
        int maxIdx;
        const char **names;
        int gi;
        int vIdx;
        maxIdx = 55;
        names = s_newFxNames; // @gen:newfx_count
        vIdx = 0;
        (void)names;
        for (gi = 0; gi < maxIdx; gi++)
        {
            if (s_newFxCategories[gi] != s_newfxFilter)
                continue;
            int col = vIdx % columns;
            int row = vIdx / columns;
            Rectangle btnRec = {startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH};
            bool isHover = CheckCollisionPointRec(mousePos, btnRec);
            Color btnCol = (s_testIndex == gi) ? ORANGE : (isHover ? MAROON : ColorAlpha(DARKGRAY, 0.5f));
            DrawRectangleRounded(btnRec, 0.3f, 10, btnCol);
            DrawRectangleRoundedLines(btnRec, 0.3f, 10, WHITE);
            int tw = MeasureText(s_newFxNames[gi], 12);
            DrawText(s_newFxNames[gi], (int)(btnRec.x + (btnW - tw) / 2), (int)(btnRec.y + 11), 12, WHITE);
            vIdx++;
        }
    }
}

void VFXTest_SetRenderTarget(int newfxIndex, Vector3 spawnPos)
{
    VFXTest_StopFixtures();
    s_testCategory = TEST_CAT_NEWFX;
    s_testIndex = newfxIndex;
    s_prefabStartPos = spawnPos;
    s_isPlayingMesh = true;
    s_meshTime = 0.0f;

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

#include "vfx_test.h"
#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/camera_fx.h"
#include "core/decals/decal_system.h"
#include "core/particles/particle_system.h"
#include "core/screen_distort.h"
#include "core/trails/trail_system.h"
#include "core/vfx_light.h"
#include "core/post_fx.h"
#include "core/presets/vfx_presets.h"
#include "core/composition/visual_composer.h"
#include "core/skill_helper.h"
#include "core/path_spline.h"

#define TEST_PATH_POINT_COUNT 16
static Vector3 s_testPathPoints[TEST_PATH_POINT_COUNT];
static bool s_hasTestPath = false;
#include "core/geometry/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "sandbox/sandbox_core.h" // Sandbox_GetPlayerAgentId — CHARACTER AURA attaches to the real player agent
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

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

// Shared state for generated FOLLOWER fixtures. Every `.inl` gets one bench
// entry, and follower primaries get a stable transform/handle rather than being
// respawned every frame. The generator owns the per-entry details.
#define VFXTEST_FIXTURE_SLOTS 96
static Matrix s_vfxFixtureXf[VFXTEST_FIXTURE_SLOTS];
static int s_vfxFixtureHandle[VFXTEST_FIXTURE_SLOTS];
static float s_vfxFixtureLastTime[VFXTEST_FIXTURE_SLOTS];
static bool s_vfxFixturesReady = false;
static VFX_ShieldSurface s_shieldFlowPreview = {0};
static bool s_shieldFlowPreviewLoaded = false;

// Tester-only semantic profile. Production compositions still receive a
// VFX_ShieldSurface from their owner and never invent asset paths.
static const VFX_ShieldSurface *VFXTest_ShieldFlowSurface(void)
{
    if (!s_shieldFlowPreviewLoaded)
    {
        s_shieldFlowPreviewLoaded = true;
        s_shieldFlowPreview.body = ResourceManager_LoadTexture("assets/textures/energy_volume.png");
        s_shieldFlowPreview.flowMap = ResourceManager_LoadTexture("assets/textures/energy_volume_flow.png");
        if (s_shieldFlowPreview.body.id != 0 && s_shieldFlowPreview.flowMap.id != 0)
        {
            SetTextureFilter(s_shieldFlowPreview.body, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_shieldFlowPreview.body, TEXTURE_WRAP_REPEAT);
            SetTextureFilter(s_shieldFlowPreview.flowMap, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_shieldFlowPreview.flowMap, TEXTURE_WRAP_REPEAT);
            s_shieldFlowPreview.flowSpeed = 0.85f;
            s_shieldFlowPreview.flowStrength = 0.12f;
            s_shieldFlowPreview.flowTiling = 1.25f;
            s_shieldFlowPreview.maskTiling = 1.0f;
        }
    }
    return s_shieldFlowPreview.body.id != 0 ? &s_shieldFlowPreview : NULL;
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
    if (s_vfxFixtureHandle[1] >= 0)
        VFX_KillCharacterAura(s_vfxFixtureHandle[1]);
    s_vfxFixtureHandle[1] = -1;
    s_vfxFixtureLastTime[1] = -1.0f;
    if (s_vfxFixtureHandle[6] >= 0)
        KillTrail(s_vfxFixtureHandle[6]);
    s_vfxFixtureHandle[6] = -1;
    s_vfxFixtureLastTime[6] = -1.0f;
    if (s_vfxFixtureHandle[9] >= 0)
        VFX_KillEmberTrail(s_vfxFixtureHandle[9]);
    s_vfxFixtureHandle[9] = -1;
    s_vfxFixtureLastTime[9] = -1.0f;
    if (s_vfxFixtureHandle[18] >= 0)
        VFX_KillProjectile(s_vfxFixtureHandle[18]);
    s_vfxFixtureHandle[18] = -1;
    s_vfxFixtureLastTime[18] = -1.0f;
    if (s_vfxFixtureHandle[19] >= 0)
        VFX_KillRibbonTrail(s_vfxFixtureHandle[19]);
    s_vfxFixtureHandle[19] = -1;
    s_vfxFixtureLastTime[19] = -1.0f;
    if (s_vfxFixtureHandle[22] >= 0)
        VFX_KillShieldShell(s_vfxFixtureHandle[22]);
    s_vfxFixtureHandle[22] = -1;
    s_vfxFixtureLastTime[22] = -1.0f;
    if (s_vfxFixtureHandle[27] >= 0)
        VFX_KillVolumeTrail(s_vfxFixtureHandle[27]);
    s_vfxFixtureHandle[27] = -1;
    s_vfxFixtureLastTime[27] = -1.0f;
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
    case 1:
        if (s_vfxFixtureHandle[1] >= 0) VFX_KillCharacterAura(s_vfxFixtureHandle[1]);
        s_vfxFixtureHandle[1] = VFX_ComposeCharacterAura(Sandbox_GetPlayerAgentId(), VC_MAT_FIRE, 1.0f);
        return true;
    case 3: VFX_ComposeContactSpark(pos, VC_MAT_FIRE, 1.5f, 0.0f); return true;
    case 7: VFX_ComposeDebrisShards(pos, (Vector3){1.4f, 2.2f, 0.5f}, VC_MAT_METAL, 1.5f, 5); return true;
    case 9:
        if (s_vfxFixtureHandle[9] >= 0) VFX_KillEmberTrail(s_vfxFixtureHandle[9]);
        s_vfxFixtureHandle[9] = VFX_ComposeEmberTrail(pos, (Vector3){1.4f, 2.2f, 0.5f}, VC_MAT_FIRE, 1.5f, 14.0f);
        return true;
    case 10: VFX_ComposeEnergyBurst(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 14: VFX_ComposeImpactDust(pos, VC_MAT_EARTH, 1.5f, 0.0f); return true;
    case 15: VFX_ComposeImpactPackage(pos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, 0.4f); return true;
    case 21: VFX_ComposeScorch(pos, VC_MAT_FIRE, 1.5f, 0.0f); return true;
    case 22:
        if (s_vfxFixtureHandle[22] >= 0) VFX_KillShieldShell(s_vfxFixtureHandle[22]);
        s_vfxFixtureHandle[22] = VFX_ShieldShell_SpawnEx(pos, VC_MAT_FIRE, 1.5f, 1.0f, VFXTest_ShieldFlowSurface());
        return true;
    case 24: VFX_ComposeSmokePuff(pos, VC_MAT_FIRE, 1.5f, 1.0f); return true;
    case 25: VFX_ComposeSparkTrail(pos, (Vector3){1.4f, 2.2f, 0.5f}, VC_MAT_FIRE, 1.0f, 2.0f); return true;
    case 32: VFX_ComposeFluidImpact(pos); return true;
    case 33: VFX_ComposeIceCrystal(pos, posSeed); return true;
    default: return false;
    }
// @gen:newfx_fire end
}

static bool s_isPanelOpen = true;
static bool s_clickedOnUI = false;
static int s_newfxFilter = NEWFX_CAT_COMMON;

// MESH: 0-8=DrawEffectMesh presets (raw meshes)
static const char *s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON"};

// @gen:newfx_names begin
// 35 entries — auto-managed by sync_vfx_test.py
static const char* s_newFxNames[] = {
    "BEAM", "CHARACTER AURA", "CHARGE CONVERGE", "CONTACT SPARK", "CONVERGE MOTES", "CORE GLOW",
    "SMOKE TRAIL", "DEBRIS SHARDS", "DISSOLVE EXIT", "EMBER TRAIL", "ENERGY BURST", "ENERGY ORB",
    "GLINT SPARKLE", "GROUND WAVE", "IMPACT DUST", "IMPACT PACKAGE", "LIGHT SHAFT", "PORTAL DISC",
    "PROJECTILE", "RIBBON TRAIL", "RUNE CIRCLE", "SCORCH", "SHIELD SHELL", "SHOCK RING",
    "SMOKE PUFF", "SPARK TRAIL", "SWEEP SLASH", "VOLUME TRAIL", "FISSURE STREAK", "STONE PILLAR",
    "FLAME VOLUME", "BLACK HOLE", "FLUID IMPACT", "ICE CRYSTAL", "WATER STREAM",
};
// @gen:newfx_names end

// @gen:newfx_categories begin
// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 COMMON=6
static const int s_newFxCategories[] = {
    6, 6, 6, 6, 6, 6, 6, 3, 6, 0,
    6, 6, 6, 4, 6, 6, 6, 6, 6, 6,
    6, 0, 6, 6, 6, 6, 6, 6, 4, 4,
    0, 5, 1, 1, 1,
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

    float dt = GetFrameTime();
    (void)dt;

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
        s_meshTime += GetFrameTime();
        if (s_meshTime > 5.0f)
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
            int maxIdx = 9;
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
            maxIdx = 35;
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
          if (!VFXTest_FireNewFx(s_testIndex, s_prefabStartPos)) {
              /* continuous — handled per-frame in VFXTest_Draw3D */
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
            float bgW = (tabW + spacing) * TEST_CAT_COUNT + 10.0f;
            Rectangle bgBox = {startX - 10, startY - 10, bgW, 440};
            if (CheckCollisionPointRec(mousePos, bgBox))
                s_clickedOnUI = true;
        }
    }

    // Click outside UI → spawn
    if (clicked && !s_clickedOnUI)
    {
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
          if (!VFXTest_FireNewFx(s_testIndex, s_prefabStartPos)) {
              /* continuous — handled per-frame in VFXTest_Draw3D */
              s_isPlayingMesh = true;
              s_meshTime = 0.0f;
          }
// @gen:newfx_trigger end
        }
    }

    return false;
}

void VFXTest_Draw3D(void)
{
    VFXTest_InitFixtures();
    float dt = GetFrameTime();

    if (s_isPlayingMesh)
    {
        s_meshTime += dt;

        if (s_testCategory == TEST_CAT_MESH)
        {
            // DrawEffectMesh preset (0-8)
            Color color = WHITE;
            if (s_testIndex == 0)
                color = (Color){200, 200, 255, 180}; // Disc
            else if (s_testIndex == 1)
                color = (Color){255, 200, 100, 180}; // Ring
            DrawEffectMesh((MeshPresetType)s_testIndex, s_prefabStartPos, (Vector3){2.0f, 2.0f, 2.0f}, color);
        }
        else if (s_testCategory == TEST_CAT_NEWFX)
        {
            // @gen:newfx_draw begin
          float progress = fmodf(s_meshTime, 2.0f) * 0.5f;
          switch (s_testIndex) {
              case 0: VFX_ComposeBeam(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, 0.1f, progress); break;
              case 2: VFX_ComposeChargeConverge(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress, 5); break;
              case 4: VFX_ComposeConvergeMotes(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress, 5); break;
              case 5: VFX_ComposeCoreGlow(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress); break;
              case 6:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[6] && s_vfxFixtureHandle[6] >= 0)
                      KillTrail(s_vfxFixtureHandle[6]);
                  if (s_meshTime < s_vfxFixtureLastTime[6]) s_vfxFixtureHandle[6] = -1;
                  s_vfxFixtureLastTime[6] = s_meshTime;
                  s_vfxFixtureXf[6] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[6] < 0)
                      s_vfxFixtureHandle[6] = VFX_ComposeSmokeTrail(&s_vfxFixtureXf[6], VC_MAT_FIRE, 1.5f, 2.0f);
                  break;
              }
              case 8: VFX_ComposeDissolveExit(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress); break;
              case 11: VFX_ComposeEnergyOrb(s_prefabStartPos, VC_MAT_FIRE, 1.5f, progress); break;
              case 12: VFX_ComposeGlintSparkle(s_prefabStartPos, VC_MAT_FIRE, 1.5f, s_meshTime); break;
              case 13: VFX_ComposeGroundWave(s_prefabStartPos, VC_MAT_EARTH, 1.5f, progress, VFX_GroundHeightFromMap, NULL); break;
              case 16: VFX_ComposeLightShaft(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), VC_MAT_FIRE, 0.8f, 1.35f); break;
              case 17: VFX_ComposePortalDisc(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress); break;
              case 18:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[18] && s_vfxFixtureHandle[18] >= 0)
                      VFX_KillProjectile(s_vfxFixtureHandle[18]);
                  if (s_meshTime < s_vfxFixtureLastTime[18]) s_vfxFixtureHandle[18] = -1;
                  s_vfxFixtureLastTime[18] = s_meshTime;
                  s_vfxFixtureXf[18] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[18] < 0)
                      s_vfxFixtureHandle[18] = VFX_ComposeProjectile(&s_vfxFixtureXf[18], VC_MAT_FIRE, 1.5f);
                  break;
              }
              case 19:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[19] && s_vfxFixtureHandle[19] >= 0)
                      VFX_KillRibbonTrail(s_vfxFixtureHandle[19]);
                  if (s_meshTime < s_vfxFixtureLastTime[19]) s_vfxFixtureHandle[19] = -1;
                  s_vfxFixtureLastTime[19] = s_meshTime;
                  s_vfxFixtureXf[19] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[19] < 0)
                      s_vfxFixtureHandle[19] = VFX_ComposeRibbonTrail(&s_vfxFixtureXf[19], VC_MAT_FIRE, 0.1f, 2.0f, VFX_RIBBON_MAIN);
                  break;
              }
              case 20: VFX_ComposeRuneCircle(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress, 5); break;
              case 23: VFX_ComposeShockRing(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, VC_MAT_FIRE, 1.5f, progress); break;
              case 26: VFX_ComposeSweepSlash(s_prefabStartPos, (Vector3){1.0f, 0.0f, 0.0f}, VC_MAT_FIRE, 1.0f, 90.0f, progress); break;
              case 27:
              {
                  float a = s_meshTime * 1.35f;
                  Vector3 fixturePos = Vector3Add(s_prefabStartPos,
                      (Vector3){3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)});
                  if (s_meshTime < s_vfxFixtureLastTime[27] && s_vfxFixtureHandle[27] >= 0)
                      VFX_KillVolumeTrail(s_vfxFixtureHandle[27]);
                  if (s_meshTime < s_vfxFixtureLastTime[27]) s_vfxFixtureHandle[27] = -1;
                  s_vfxFixtureLastTime[27] = s_meshTime;
                  s_vfxFixtureXf[27] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);
                  if (s_vfxFixtureHandle[27] < 0)
                      s_vfxFixtureHandle[27] = VFX_ComposeVolumeTrail(&s_vfxFixtureXf[27], VC_MAT_FIRE, 1.5f, 2.0f, VOL_ENERGY);
                  break;
              }
              case 28: VFX_ComposeFissureStreak(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.1f, progress, s_meshTime); break;
              case 29: VFX_ComposeStonePillar(s_prefabStartPos, progress); break;
              case 30: VFX_ComposeFlameVolume(s_prefabStartPos, VC_MAT_FIRE, 1.5f, 1.0f); break;
              case 31: VFX_ComposeBlackHole(VC_MAT_FIRE, s_prefabStartPos, 1.5f, s_meshTime); break;
              case 34: VFX_ComposeWaterStream(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.33f), (Vector3){0.0f, 0.9f, 0.7f}), Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.66f), (Vector3){0.0f, 0.5f, -0.7f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 1.5f, progress, s_meshTime); break;
          }
// @gen:newfx_draw end
        }
    }
}

void VFXTest_DrawHUD(void)
{
    VFXLightData activeLights[MAX_VFX_LIGHTS];
    int activeCount = 0;
    VFXLight_GetActive(activeLights, &activeCount, MAX_VFX_LIGHTS);
    DrawText(TextFormat("Active VFX Lights: %d / 8", activeCount), 10, 610,
             20, ORANGE);
    GpuParticleSystem_DrawDebug(10, 635);

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

    {
        float bgW = (tabW + spacing) * TEST_CAT_COUNT + 10.0f;
        Rectangle bgBox = {startX - 10, startY - 10, bgW, 440};
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
    float btnW = 110.0f;
    float btnH = 35.0f;
    int columns = 6;

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
        int maxIdx = 9;
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
        maxIdx = 35;
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

#include "vfx_test.h"
#include "compute/gpu_particle_system.h"
#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/screen_distort.h"
#include "core/trail_system.h"
#include "core/vfx_light.h"
#include "core/post_fx.h"
#include "core/composition/vfx_sequence.h"        // RADIAL BURST E1A — exercises the transient path
#include "core/presets/vfx_presets.h"
#include "core/composition/visual_composer.h"
#include "core/skill_helper.h"
#include "core/path_spline.h"

// E3 adapters: a beat calls fn(pos, scale, ud), so a VFX_Compose* needs a
// 3-line wrapper. This is the intended shape — see vfx_sequence.h's COMPOSE row.
static void VFXTest_SeqPuff(Vector3 pos, float scale, void *ud)
{
    (void)ud;
    VFX_ComposeSmokePuff(pos, VC_MAT_EARTH, scale * 0.8f, 0.6f);
}
static void VFXTest_SeqBurst(Vector3 pos, float scale, void *ud)
{
    (void)ud;
    VFX_ComposeImpact(pos, EFFECT_PRESET_FIRE_EXPLOSION, scale * 1.4f);
    VFX_ComposeSmokePuff(pos, VC_MAT_FIRE, scale, 1.0f);
}

#define TEST_PATH_POINT_COUNT 16
static Vector3 s_testPathPoints[TEST_PATH_POINT_COUNT];
static bool s_hasTestPath = false;
#include "core/geometry/procedural_mesh_utils.h"
#include "core/vfx_proc_ray.h"
#include "core/resource_manager.h"
#include "core/map_manager.h"
#include "sandbox/sandbox_core.h"   // Sandbox_GetPlayerAgentId — CHARACTER AURA attaches to the real player agent
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
    NEWFX_CAT_ALL = 0,
    NEWFX_CAT_FIRE,
    NEWFX_CAT_WATER,
    NEWFX_CAT_WOOD,
    NEWFX_CAT_METAL,
    NEWFX_CAT_EARTH,
    NEWFX_CAT_TAIJI,
    NEWFX_CAT_UTIL,
    NEWFX_CAT_COUNT
} NewFXCategory;

static int s_testCategory = TEST_CAT_MESH;
static int s_testIndex = 0;
static bool s_isPlayingMesh = false;
static float s_meshTime = 0.0f;
static Vector3 s_prefabStartPos = {0};
// BEAM (util) fires from the character to the click point — captured at click
// time (the generated draw block reads these; referenced by name in the
// vfx_test_manifest.json draw_call so sync passes them through verbatim).
static Vector3 s_beamStart = {0};
static Vector3 s_beamEnd = {0};

// GROUND SMOKE (util) demo — queries the REAL active map's terrain via
// MapManager_GetGroundHeightAt (routes to whichever map's GetGroundHeight
// hook is registered — see core/map_manager.h). On a flat map (no hook
// registered) this returns 0.0f, same as passing heightFn=NULL directly.
static float VFXTest_GroundHeightAt(float x, float z, void *userData)
{
    (void)userData;
    return MapManager_GetGroundHeightAt(x, z);
}

static bool s_isPanelOpen = true;
static bool s_clickedOnUI = false;
static int s_newfxFilter = NEWFX_CAT_ALL;

// MESH: 0-8=DrawEffectMesh presets (raw meshes)
static const char *s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON"};

// @gen:newfx_names begin
// 67 entries — auto-managed by sync_vfx_test.py
static const char* s_newFxNames[] = {
    "BURN GROUND", "IMPACT FIRE", "CAST FIRE", "SPLASH", "BUBBLES", "ICE CRYSTAL",
    "PUDDLE", "WATER STREAM", "IMPACT WATER", "CAST WATER", "GLOW VINE", "IMPACT WOOD",
    "CAST WOOD", "PLASMA ORB", "ENERGY SMOKE", "IMPACT METAL", "CAST METAL", "STONE PILLAR",
    "FISSURE", "IMPACT EARTH", "CAST EARTH", "ELEM MIST", "IMPACT TAIJI", "CAST TAIJI",
    "GUST SLASH", "SMOKE COLUMN", "SHIELD", "GND PATTERN", "PROJ FIRE", "PROJ WATER",
    "PROJ METAL", "CYLINDER AURA", "GROUND AURA", "BLACK HOLE", "DRAW ICE CRYSTAL BURST", "SMOKE COLUMN F X",
    "GROUND SMOKE", "SMOKE ON PLANE", "MAGIC FILAMENTS", "MAGIC FILAMENTS ON PLANE", "SHARD DEBRIS", "WATER STREAM ON PATH",
    "MESH ELECTRICITY", "PARTICLE UPGRADES TEST", "TAIJI ARC STRIKE", "TORNADO", "RELEASE TORNADO", "FIRE FUNNEL",
    "ENERGY FLOW", "LIGHTNING BOLT", "PROC BEAM", "BEAM", "ORBITALS", "LEAF SWIRL",
    "CHAIN LINK", "CROWN SPLASH", "SMOKE PUFF", "FLAME VOLUME", "EMBER DRIFT", "GLINT BURST",
    "SHOCKWAVE RING", "STREAK FLARE", "EXPLOSION", "CHARACTER AURA", "RADIAL BURST E1A", "SEQUENCE ENVELOPE E3",
    "GLINT SPARKLE",
};
// @gen:newfx_names end

// @gen:newfx_categories begin
// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 UTIL=6
static const int s_newFxCategories[] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 3, 5, 3, 3, 4, 4, 4,
    4, 5, 5, 5, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 1, 6, 6, 6, 6, 6,
    6, 1, 6, 6, 5, 6, 6, 0, 6, 6,
    6, 6, 6, 2, 6, 1, 6, 0, 0, 6,
    1, 0, 6, 5, 6, 6, 3,
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
        s_beamStart = Vector3Add(playerPos, (Vector3){0.0f, 0.3f, 0.0f});
        s_beamEnd = Vector3Add(playerPos, (Vector3){3.0f, 0.0f, 0.0f});
        
        for (int idx = 0; idx < TEST_PATH_POINT_COUNT; idx++)
        {
            float t = (float)idx / (float)(TEST_PATH_POINT_COUNT - 1);
            s_testPathPoints[idx] = Vector3Lerp(s_beamStart, s_beamEnd, t);
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
            cfg.radius = 0.25f;  // 0.06f gần như dưới-pixel ở khoảng cách camera arena --
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
            vf.direction = (Vector3){2.5f, 0.0f, 2.5f};   // hộp 5x5m phủ trọn hàng spawn + lối trôi
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
            cfg.radius = 0.12f;  // 0.008f (8mm) vô hình ở khoảng cách camera -- xem ghi chú FF test
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
        if (downNow) s_toggleArmed = true;
    }
    if (s_toggleArmed && !downNow)
    {
        s_toggleArmed = false;
        if (overToggleBtn) s_isPanelOpen = !s_isPanelOpen;
    }

    bool overBackBtn = CheckCollisionPointRec(mousePos, backBtn);
    if (overBackBtn)
    {
        s_clickedOnUI = true;
        if (downNow) s_backArmed = true;
    }
    if (s_backArmed && !downNow)
    {
        s_backArmed = false;
        if (overBackBtn) return true; // Request back to menu
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
                        s_newfxFilter = fi;
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
            maxIdx = 67;
            names = s_newFxNames; // @gen:newfx_count
            visualIdx = 0;
            (void)names;
            for (globalIdx = 0; globalIdx < maxIdx; globalIdx++)
            {
                int cat = s_newFxCategories[globalIdx];
                if (s_newfxFilter != NEWFX_CAT_ALL && cat != (s_newfxFilter - 1))
                    continue;
                int col = visualIdx % columns;
                int row = visualIdx / columns;
                Rectangle btnRec = {startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH};
                if (CheckCollisionPointRec(mousePos, btnRec))
                {
                    s_clickedOnUI = true;
                    if (clicked)
                    {
                        s_testIndex = globalIdx;
                        s_isPlayingMesh = false;
                        // @gen:newfx_trigger begin
          int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;
          if (s_testIndex == 1) { /* IMPACT FIRE */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 2) { /* CAST FIRE */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 3) { /* SPLASH */
              VFX_ComposeSplashBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 8) { /* IMPACT WATER */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 9) { /* CAST WATER */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 11) { /* IMPACT WOOD */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 12) { /* CAST WOOD */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 15) { /* IMPACT METAL */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 16) { /* CAST METAL */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 19) { /* IMPACT EARTH */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 20) { /* CAST EARTH */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 22) { /* IMPACT TAIJI */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 23) { /* CAST TAIJI */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 24) { /* GUST SLASH */
              VFX_ComposeGustSlash(Vector3Add(s_prefabStartPos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f);
          } else if (s_testIndex == 25) { /* SMOKE COLUMN */
              VFX_SpawnSmokeColumn(s_prefabStartPos, 5.0f);
          } else if (s_testIndex == 28) { /* PROJ FIRE */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f);
          } else if (s_testIndex == 29) { /* PROJ WATER */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f);
          } else if (s_testIndex == 30) { /* PROJ METAL */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f);
          } else if (s_testIndex == 34) { /* DRAW ICE CRYSTAL BURST */
              VFX_DrawIceCrystalBurst(s_prefabStartPos, 5, posSeed, 1.0f);
          } else if (s_testIndex == 40) { /* SHARD DEBRIS */
              VFX_ComposeShardDebris(s_prefabStartPos, 12, 4.5f, VC_MAT_ICE);
          } else if (s_testIndex == 42) { /* MESH ELECTRICITY */
              VFX_ComposeMeshElectricity(s_prefabStartPos, (Color){100, 220, 255, 255}, 2.5f);
          } else if (s_testIndex == 43) { /* PARTICLE UPGRADES TEST */
              VFX_ComposeParticleUpgradesTest(s_prefabStartPos);
          } else if (s_testIndex == 44) { /* TAIJI ARC STRIKE */
              VFX_ComposeTaijiArcStrike(s_prefabStartPos, 1.5f);
          } else if (s_testIndex == 46) { /* RELEASE TORNADO */
              VFX_ReleaseTornado(s_prefabStartPos);
          } else if (s_testIndex == 48) { /* ENERGY FLOW */
              VFX_ComposeEnergyFlow(s_prefabStartPos, s_prefabStartPos, 1.5f, 2.0f);
          } else if (s_testIndex == 49) { /* LIGHTNING BOLT */
              VFX_ComposeLightningBolt(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0}), 1.5f);
          } else if (s_testIndex == 50) { /* PROC BEAM */
              VFX_SpawnProcBeam(s_prefabStartPos, s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 0.1f, 2.0f);
          } else if (s_testIndex == 52) { /* ORBITALS */
              VFX_SpawnOrbitals(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 5, 1.5f, 2.0f);
          } else if (s_testIndex == 55) { /* CROWN SPLASH */
              VFX_ComposeCrownSplash(s_prefabStartPos, 1.5f, 2.0f, 2.0f, VC_MAT_WATER);
          } else if (s_testIndex == 56) { /* SMOKE PUFF */
              VFX_ComposeSmokePuff(s_prefabStartPos, VC_MAT_EARTH, 1.0f, 1.0f);
          } else if (s_testIndex == 58) { /* EMBER DRIFT */
              VFX_ComposeEmberDrift(s_prefabStartPos, 1.5f, 5, WHITE);
          } else if (s_testIndex == 59) { /* GLINT BURST */
              VFX_ComposeGlintBurst(s_prefabStartPos, 5, 1.0f, WHITE);
          } else if (s_testIndex == 60) { /* SHOCKWAVE RING */
              VFX_ComposeShockwaveRing(s_prefabStartPos, 1.5f, 2.0f, WHITE);
          } else if (s_testIndex == 61) { /* STREAK FLARE */
              VFX_ComposeStreakFlare(s_prefabStartPos, 1.5f, WHITE);
          } else if (s_testIndex == 62) { /* EXPLOSION */
              VFX_TriggerExplosion(VC_MAT_FIRE, s_prefabStartPos, 1.5f, false);
          } else if (s_testIndex == 64) { /* RADIAL BURST E1A */
              PostFX_RadialBurst(s_prefabStartPos, 0.18f, 0.7f);
          } else if (s_testIndex == 65) { /* SEQUENCE ENVELOPE E3 */
              { VFX_Sequence *sq = VFX_SeqPreset(s_prefabStartPos, VC_MAT_FIRE, 1.0f, 0.35f, 0.10f, 0.35f, 0.5f); if (sq) { VFX_SeqAt(sq, 0.00f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqPuff }); VFX_SeqAt(sq, 0.35f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqBurst }); VFX_SeqPlay(sq); } };
          } else {
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
        s_prefabStartPos = mouseTarget3D;
        // BEAM: shoot from the character (chest height) to the click point.
        s_beamStart = Vector3Add(playerPos, (Vector3){0.0f, 0.3f, 0.0f});
        s_beamEnd   = mouseTarget3D;
        
        // Generate random spline path from playerPos (chest) to clicked ground
        Vector3 p0 = s_beamStart;
        Vector3 p3 = mouseTarget3D;
        float dist = Vector3Distance(p0, p3);
        float offsetScale = dist * 0.25f;
        if (offsetScale < 0.5f) offsetScale = 0.5f;

        if (dist > 0.01f) {
            Vector3 dir = Vector3Normalize(Vector3Subtract(p3, p0));
            Vector3 upVec = (Vector3){0.0f, 1.0f, 0.0f};
            if (fabsf(dir.y) > 0.9f) upVec = (Vector3){1.0f, 0.0f, 0.0f};
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
        } else {
            for (int idx = 0; idx < TEST_PATH_POINT_COUNT; idx++) {
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
          int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;
          if (s_testIndex == 1) { /* IMPACT FIRE */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 2) { /* CAST FIRE */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 3) { /* SPLASH */
              VFX_ComposeSplashBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 8) { /* IMPACT WATER */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 9) { /* CAST WATER */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 11) { /* IMPACT WOOD */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 12) { /* CAST WOOD */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 15) { /* IMPACT METAL */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 16) { /* CAST METAL */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 19) { /* IMPACT EARTH */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 20) { /* CAST EARTH */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 22) { /* IMPACT TAIJI */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 23) { /* CAST TAIJI */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 24) { /* GUST SLASH */
              VFX_ComposeGustSlash(Vector3Add(s_prefabStartPos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f);
          } else if (s_testIndex == 25) { /* SMOKE COLUMN */
              VFX_SpawnSmokeColumn(s_prefabStartPos, 5.0f);
          } else if (s_testIndex == 28) { /* PROJ FIRE */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f);
          } else if (s_testIndex == 29) { /* PROJ WATER */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f);
          } else if (s_testIndex == 30) { /* PROJ METAL */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f);
          } else if (s_testIndex == 34) { /* DRAW ICE CRYSTAL BURST */
              VFX_DrawIceCrystalBurst(s_prefabStartPos, 5, posSeed, 1.0f);
          } else if (s_testIndex == 40) { /* SHARD DEBRIS */
              VFX_ComposeShardDebris(s_prefabStartPos, 12, 4.5f, VC_MAT_ICE);
          } else if (s_testIndex == 42) { /* MESH ELECTRICITY */
              VFX_ComposeMeshElectricity(s_prefabStartPos, (Color){100, 220, 255, 255}, 2.5f);
          } else if (s_testIndex == 43) { /* PARTICLE UPGRADES TEST */
              VFX_ComposeParticleUpgradesTest(s_prefabStartPos);
          } else if (s_testIndex == 44) { /* TAIJI ARC STRIKE */
              VFX_ComposeTaijiArcStrike(s_prefabStartPos, 1.5f);
          } else if (s_testIndex == 46) { /* RELEASE TORNADO */
              VFX_ReleaseTornado(s_prefabStartPos);
          } else if (s_testIndex == 48) { /* ENERGY FLOW */
              VFX_ComposeEnergyFlow(s_prefabStartPos, s_prefabStartPos, 1.5f, 2.0f);
          } else if (s_testIndex == 49) { /* LIGHTNING BOLT */
              VFX_ComposeLightningBolt(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0}), 1.5f);
          } else if (s_testIndex == 50) { /* PROC BEAM */
              VFX_SpawnProcBeam(s_prefabStartPos, s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 0.1f, 2.0f);
          } else if (s_testIndex == 52) { /* ORBITALS */
              VFX_SpawnOrbitals(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 5, 1.5f, 2.0f);
          } else if (s_testIndex == 55) { /* CROWN SPLASH */
              VFX_ComposeCrownSplash(s_prefabStartPos, 1.5f, 2.0f, 2.0f, VC_MAT_WATER);
          } else if (s_testIndex == 56) { /* SMOKE PUFF */
              VFX_ComposeSmokePuff(s_prefabStartPos, VC_MAT_EARTH, 1.0f, 1.0f);
          } else if (s_testIndex == 58) { /* EMBER DRIFT */
              VFX_ComposeEmberDrift(s_prefabStartPos, 1.5f, 5, WHITE);
          } else if (s_testIndex == 59) { /* GLINT BURST */
              VFX_ComposeGlintBurst(s_prefabStartPos, 5, 1.0f, WHITE);
          } else if (s_testIndex == 60) { /* SHOCKWAVE RING */
              VFX_ComposeShockwaveRing(s_prefabStartPos, 1.5f, 2.0f, WHITE);
          } else if (s_testIndex == 61) { /* STREAK FLARE */
              VFX_ComposeStreakFlare(s_prefabStartPos, 1.5f, WHITE);
          } else if (s_testIndex == 62) { /* EXPLOSION */
              VFX_TriggerExplosion(VC_MAT_FIRE, s_prefabStartPos, 1.5f, false);
          } else if (s_testIndex == 64) { /* RADIAL BURST E1A */
              PostFX_RadialBurst(s_prefabStartPos, 0.18f, 0.7f);
          } else if (s_testIndex == 65) { /* SEQUENCE ENVELOPE E3 */
              { VFX_Sequence *sq = VFX_SeqPreset(s_prefabStartPos, VC_MAT_FIRE, 1.0f, 0.35f, 0.10f, 0.35f, 0.5f); if (sq) { VFX_SeqAt(sq, 0.00f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqPuff }); VFX_SeqAt(sq, 0.35f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqBurst }); VFX_SeqPlay(sq); } };
          } else {
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
          float progress = fminf(s_meshTime / 1.0f, 1.0f);
          int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;
          switch (s_testIndex) {
              case 0: VFX_ComposeBurningGround(s_prefabStartPos, 0.9f, s_meshTime); break;
              case 4: VFX_ComposeBubbleStream(s_prefabStartPos, 0.8f, s_meshTime); break;
              case 5: VFX_ComposeIceCrystal(s_prefabStartPos, posSeed); break;
              case 6: VFX_ComposeMagicPuddle(s_prefabStartPos); break;
              case 7: {
                      Vector3 wsP0 = s_prefabStartPos;
                      Vector3 wsP1 = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 0.8f, 0.5f});
                      Vector3 wsP2 = Vector3Add(s_prefabStartPos, (Vector3){2.0f, 1.2f, 0});
                      Vector3 wsP3 = Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0});
                      VFX_ComposeWaterStream(wsP0, wsP1, wsP2, wsP3, 0.06f, progress, s_meshTime);
                  break;
              }
              case 10: {
                      Vector3 gvStart = s_prefabStartPos;
                      Vector3 gvEnd   = Vector3Add(s_prefabStartPos, (Vector3){0, 0, 3.0f});
                      Vector3 gvC1    = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 1.0f, 0.8f});
                      Vector3 gvC2    = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 1.0f, 2.0f});
                      VFX_ComposeGlowingVine(gvStart, gvEnd, gvC1, gvC2, gvEnd, progress, s_meshTime, 1.0f, 0, 1);
                  break;
              }
              case 13: VFX_ComposePlasmaOrb(Vector3Add(s_prefabStartPos, (Vector3){0, 0.9f, 0}), 0.5f, s_meshTime); break;
              case 14: VFX_ComposeEnergySmoke(s_prefabStartPos, 1.0f, progress, s_meshTime, (Vector2){0.0f, 0.0f}); break;
              case 17: VFX_ComposeStonePillar(s_prefabStartPos, progress); break;
              case 18: VFX_ComposeFissureStreak(s_prefabStartPos, (Vector3){s_prefabStartPos.x + 3, MapManager_GetGroundHeightAt(s_prefabStartPos.x + 3, s_prefabStartPos.z), s_prefabStartPos.z}, 0.4f, progress, s_meshTime); break;
              case 21: VFX_ComposeElementalMist(VC_MAT_ICE, s_prefabStartPos, 1.0f, s_meshTime); break;
              case 26: VFX_ComposeShield(VC_MAT_METAL, s_prefabStartPos, 1.2f, fminf(progress, 0.5f), s_meshTime); break;
              case 27: VFX_GroundPattern(GROUND_CRACK_RADIAL, s_prefabStartPos, 1.5f, progress, s_meshTime); break;
              case 31: VFX_ComposeCylinderAura(VC_MAT_FIRE, s_prefabStartPos, 1.5f, fminf(progress, 0.99f), s_meshTime); break;
              case 32: VFX_ComposeGroundAura(VC_MAT_FIRE, s_prefabStartPos, 1.5f, 0.9f, s_meshTime); break;
              case 33: VFX_ComposeBlackHole(VC_MAT_VOID, Vector3Add(s_prefabStartPos, (Vector3){0, 2.5f, 0}), 1.0f, s_meshTime); break;
              case 35: VFX_ComposeSmokeColumnFX(s_prefabStartPos, 0.1f, 2.0f, fminf(progress, 0.99f), 5); break;
              case 36: VFX_ComposeGroundSmoke(s_prefabStartPos, 1.5f, fminf(progress, 0.99f), VFXTest_GroundHeightAt, NULL); break;
              case 37: VFX_ComposeSmokeOnPlane(s_prefabStartPos, (Vector3){1.0f, 0.0f, 0.0f}, 1.5f, fminf(progress, 0.99f), WHITE); break;
              case 38: VFX_ComposeMagicFilaments(s_prefabStartPos, 1.5f, fminf(progress, 0.99f), (Color){100, 200, 255, 200}, 0.8f, 3.5f, 4.0f, (Vector2){0.0f, 0.0f}); break;
              case 39: VFX_ComposeMagicFilamentsOnPlane(s_prefabStartPos, (Vector3){0.0f, 1.0f, 0.0f}, 1.5f, fminf(progress, 0.99f), (Color){100, 200, 255, 200}, 0.8f, 3.5f, 4.0f, (Vector2){0.0f, 0.0f}); break;
              case 41: { static bool splashed = false; if (progress < 0.05f) splashed = false; if (progress >= 0.83f && !splashed) { VFX_ComposeSplashBurst(s_testPathPoints[15], 1.2f); splashed = true; } VFX_ComposeWaterStreamOnPath(s_testPathPoints, 16, 0.25f, progress * 1.2f, 0.25f, s_meshTime); }; break;
              case 45: VFX_ComposeTornado(VC_MAT_FIRE, s_prefabStartPos, 1.5f, 2.0f, s_meshTime); break;
              case 47: VFX_ComposeFireFunnel(s_prefabStartPos, 0.6f, 0.08f, 1.5f, s_meshTime); break;
              case 51: VFX_ComposeBeam(VC_MAT_FIRE, s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0}), 0.1f, fminf(progress, 0.99f), s_meshTime); break;
              case 53: VFX_ComposeLeafSwirl(s_prefabStartPos, 1.5f, s_meshTime); break;
              case 54: VFX_ComposeChainLink(VC_MAT_FIRE, s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0}), 0.1f, 1.0f, fminf(progress, 0.99f), s_meshTime); break;
              case 57: VFX_ComposeFlameVolume(s_prefabStartPos, VC_MAT_FIRE, 1.0f, 1.0f); break;
              case 63: { static int auraH = -1; static bool auraDone = false; if (progress < 0.5f) auraDone = false; if (!auraDone && auraH < 0) auraH = VFX_ComposeCharacterAura(Sandbox_GetPlayerAgentId(), VC_MAT_QI, 0.0f); if (auraH >= 0) VFX_AuraSetIntensity(auraH, (progress < 0.80f) ? (progress / 0.80f) : (1.0f - (progress - 0.80f) / 0.20f)); if (progress >= 0.99f && auraH >= 0) { VFX_KillCharacterAura(auraH); auraH = -1; auraDone = true; } }; break;
              case 66: VFX_ComposeGlintSparkle(s_prefabStartPos, VC_MAT_HOLY, 1.0f, s_meshTime); break;
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
        const char *filterNames[] = {"ALL", "FIRE", "WATER", "WOOD", "METAL", "EARTH", "TAIJI", "UTIL"};
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
        maxIdx = 67;
        names = s_newFxNames; // @gen:newfx_count
        vIdx = 0;
        (void)names;
        for (gi = 0; gi < maxIdx; gi++)
        {
            if (s_newfxFilter != NEWFX_CAT_ALL && s_newFxCategories[gi] != (s_newfxFilter - 1))
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
    Vector3 pos = spawnPos; /* alias used by @gen:newfx_render_trigger */
    s_testCategory = TEST_CAT_NEWFX;
    s_testIndex = newfxIndex;
    s_prefabStartPos = spawnPos;
    s_isPlayingMesh = true;
    s_meshTime = 0.0f;

    // Fire oneshots immediately for warmup rendering.
    // @gen:newfx_render_trigger begin
    switch (newfxIndex) {
    case 1: VFX_ComposeImpact(pos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f); break;
    case 2: VFX_ComposeCast(pos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f); break;
    case 3: VFX_ComposeSplashBurst(pos, 1.0f); break;
    case 8: VFX_ComposeImpact(pos, EFFECT_PRESET_WATER_SPLASH, 1.5f); break;
    case 9: VFX_ComposeCast(pos, EFFECT_PRESET_WATER_SPLASH, 1.5f); break;
    case 11: VFX_ComposeImpact(pos, EFFECT_PRESET_WOOD_BLOOM, 1.5f); break;
    case 12: VFX_ComposeCast(pos, EFFECT_PRESET_WOOD_BLOOM, 1.5f); break;
    case 15: VFX_ComposeImpact(pos, EFFECT_PRESET_METAL_SHARD, 1.5f); break;
    case 16: VFX_ComposeCast(pos, EFFECT_PRESET_METAL_SHARD, 1.5f); break;
    case 19: VFX_ComposeImpact(pos, EFFECT_PRESET_EARTH_CRACK, 1.5f); break;
    case 20: VFX_ComposeCast(pos, EFFECT_PRESET_EARTH_CRACK, 1.5f); break;
    case 22: VFX_ComposeImpact(pos, EFFECT_PRESET_TAIJI_BURST, 1.5f); break;
    case 23: VFX_ComposeCast(pos, EFFECT_PRESET_TAIJI_BURST, 1.5f); break;
    case 24: VFX_ComposeGustSlash(Vector3Add(pos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f); break;
    case 25: VFX_SpawnSmokeColumn(pos, 5.0f); break;
    case 28: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f); break;
    case 29: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f); break;
    case 30: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f); break;
    case 34: VFX_DrawIceCrystalBurst(pos, 5, 0, 1.0f); break;
    case 40: VFX_ComposeShardDebris(pos, 12, 4.5f, VC_MAT_ICE); break;
    case 42: VFX_ComposeMeshElectricity(pos, (Color){100, 220, 255, 255}, 2.5f); break;
    case 43: VFX_ComposeParticleUpgradesTest(pos); break;
    case 44: VFX_ComposeTaijiArcStrike(pos, 1.5f); break;
    case 46: VFX_ReleaseTornado(pos); break;
    case 48: VFX_ComposeEnergyFlow(pos, pos, 1.5f, 2.0f); break;
    case 49: VFX_ComposeLightningBolt(pos, Vector3Add(pos, (Vector3){3.0f, 0, 0}), 1.5f); break;
    case 50: VFX_SpawnProcBeam(pos, pos, EFFECT_PRESET_FIRE_EXPLOSION, 0.1f, 2.0f); break;
    case 52: VFX_SpawnOrbitals(pos, EFFECT_PRESET_FIRE_EXPLOSION, 5, 1.5f, 2.0f); break;
    case 55: VFX_ComposeCrownSplash(pos, 1.5f, 2.0f, 2.0f, VC_MAT_WATER); break;
    case 56: VFX_ComposeSmokePuff(pos, VC_MAT_EARTH, 1.0f, 1.0f); break;
    case 58: VFX_ComposeEmberDrift(pos, 1.5f, 5, WHITE); break;
    case 59: VFX_ComposeGlintBurst(pos, 5, 1.0f, WHITE); break;
    case 60: VFX_ComposeShockwaveRing(pos, 1.5f, 2.0f, WHITE); break;
    case 61: VFX_ComposeStreakFlare(pos, 1.5f, WHITE); break;
    case 62: VFX_TriggerExplosion(VC_MAT_FIRE, pos, 1.5f, false); break;
    case 64: PostFX_RadialBurst(pos, 0.18f, 0.7f); break;
    case 65: { VFX_Sequence *sq = VFX_SeqPreset(pos, VC_MAT_FIRE, 1.0f, 0.35f, 0.10f, 0.35f, 0.5f); if (sq) { VFX_SeqAt(sq, 0.00f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqPuff }); VFX_SeqAt(sq, 0.35f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = VFXTest_SeqBurst }); VFX_SeqPlay(sq); } }; break;
    default: break;
    }
// @gen:newfx_render_trigger end
}

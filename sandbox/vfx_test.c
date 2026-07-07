#include "vfx_test.h"
#include "compute/gpu_particle_system.h"
#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/screen_distort.h"
#include "core/trail_system.h"
#include "core/vfx_light.h"
#include "core/presets/vfx_presets.h"
#include "core/composition/visual_composer.h"
#include "core/skill_helper.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/vfx_proc_ray.h"
#include "core/resource_manager.h"
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

static bool s_isPanelOpen = true;
static bool s_clickedOnUI = false;
static int s_newfxFilter = NEWFX_CAT_ALL;

// MESH: 0-8=DrawEffectMesh presets (raw meshes)
static const char *s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON"};

// @gen:newfx_names begin
// 72 entries — auto-managed by sync_vfx_test.py
static const char* s_newFxNames[] = {
    "FLAME WISP", "FIRE PILLAR", "FIREBALL", "FIRE BREATH", "BURN GROUND", "FIRE WHIRL",
    "EMBER DRIFT", "IMPACT FIRE", "CAST FIRE", "SPLASH", "BUBBLES", "MIST VEIL",
    "ICE CRYSTAL", "PUDDLE", "WATER STREAM", "IMPACT WATER", "CAST WATER", "LEAF SWIRL",
    "BLOOM BURST", "LEAF FALL", "GLOW VINE", "IMPACT WOOD", "CAST WOOD", "METAL SHARD",
    "PLASMA ORB", "BLADE RING", "BLADE STORM", "SHRAPNEL", "RICOCHET", "STATIC FLD",
    "BOLT", "PROC BEAM", "ORBITALS", "IMPACT METAL", "CAST METAL", "ROCK BURST",
    "FLOAT STONE", "QUAKE", "STONE PILLAR", "BOULDER", "FISSURE", "IMPACT EARTH",
    "CAST EARTH", "YIN YANG", "ELEM MIST", "QI AURA", "AURA RING", "IMPACT TAIJI",
    "CAST TAIJI", "SHOCKWAVE", "GLINT BURST", "STREAK FLARE", "GUST SLASH", "SMOKE PUFF",
    "SMOKE TRAIL", "SHIELD", "CHAIN", "ZONE", "SLASH ARC", "CHARGE UP",
    "CYCLONE", "BEAM", "PROJECTILE", "AURA", "GND PATTERN", "SUMMON RING",
    "EXPLOSION", "GROUND WAVE", "PROJ FIRE", "PROJ WATER", "PROJ METAL", "CYLINDER AURA",
};
// @gen:newfx_names end

// @gen:newfx_categories begin
// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 UTIL=6
static const int s_newFxCategories[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
    2, 2, 2, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 4, 4, 4, 4, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6,
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
        static bool s_gpuTestFieldInit = false;
        if (!s_gpuTestFieldInit)
        {
            ForceField_Clear(&s_gpuTestField);
            ForceLayer vortex = {0};
            vortex.type = FORCE_VORTEX;
            vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
            vortex.direction = (Vector3){0.0f, 1.0f, 0.0f};
            vortex.strength = 4.0f;
            ForceField_AddLayer(&s_gpuTestField, vortex);
            s_gpuTestFieldInit = true;
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
            cfg.radius = 0.006f;
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
        static bool s_flowInit = false;
        if (!s_flowInit)
        {
            Image img = GenImageColor(4, 4, (Color){255, 128, 0, 255});
            s_flowTex = LoadTextureFromImage(img);
            UnloadImage(img);
            GpuParticleSystem_SetVectorFieldTexture(0, s_flowTex);

            ForceField_Clear(&s_flowField);
            ForceLayer vf = {0};
            vf.type = FORCE_VECTOR_TEXTURE;
            vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
            vf.direction = (Vector3){0.3f, 0.0f, 0.3f};
            vf.strength = 2.5f;
            vf.noiseScale = 0.0f;
            ForceField_AddLayer(&s_flowField, vf);
            s_flowInit = true;
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
            cfg.radius = 0.008f;
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

    Rectangle toggleBtn = {20, 15, 180, 32};
    Rectangle backBtn = {210, 15, 180, 32};

    if (CheckCollisionPointRec(mousePos, toggleBtn))
    {
        s_clickedOnUI = true;
        if (clicked)
            s_isPanelOpen = !s_isPanelOpen;
    }
    if (CheckCollisionPointRec(mousePos, backBtn))
    {
        s_clickedOnUI = true;
        if (clicked)
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
        float startY = 70.0f;
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
            maxIdx = 72;
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
          if (s_testIndex == 6) { /* EMBER DRIFT */
              VFX_ComposeEmberDrift(s_prefabStartPos, 0.8f, 12, (Color){255, 140, 60, 255});
          } else if (s_testIndex == 7) { /* IMPACT FIRE */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 8) { /* CAST FIRE */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 9) { /* SPLASH */
              VFX_ComposeSplashBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 15) { /* IMPACT WATER */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 16) { /* CAST WATER */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 18) { /* BLOOM BURST */
              VFX_ComposeBloomBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 21) { /* IMPACT WOOD */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 22) { /* CAST WOOD */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 27) { /* SHRAPNEL */
              VFX_ComposeShrapnelBurst(Vector3Add(s_prefabStartPos, (Vector3){0, 0.25f, 0}), 1.0f);
          } else if (s_testIndex == 28) { /* RICOCHET */
              VFX_ComposeRicochetSpark(Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0}), (Vector3){0.7f, 0.7f, 0.0f}, 1.0f);
          } else if (s_testIndex == 30) { /* BOLT */
              VFX_ComposeLightningBolt(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){2, 1, 0}), 1.0f);
          } else if (s_testIndex == 31) { /* PROC BEAM */
              VFX_SpawnProcBeam(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){2, 1, 0}), EFFECT_PRESET_METAL_SHARD, 0.1f, 3.0f);
          } else if (s_testIndex == 32) { /* ORBITALS */
              VFX_SpawnOrbitals(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 5, 0.8f, 4.0f);
          } else if (s_testIndex == 33) { /* IMPACT METAL */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 34) { /* CAST METAL */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 35) { /* ROCK BURST */
              VFX_ComposeRockBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 40) { /* FISSURE */
              VFX_ComposeFissureStreak(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3, 0, 0}), 0.15f);
          } else if (s_testIndex == 41) { /* IMPACT EARTH */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 42) { /* CAST EARTH */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 46) { /* AURA RING */
              VFX_SpawnAuraRing(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.0f, 5.0f);
          } else if (s_testIndex == 47) { /* IMPACT TAIJI */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 48) { /* CAST TAIJI */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 49) { /* SHOCKWAVE */
              VFX_ComposeShockwaveRing(s_prefabStartPos, 1.5f, 0.6f, (Color){255, 200, 80, 255});
          } else if (s_testIndex == 50) { /* GLINT BURST */
              VFX_ComposeGlintBurst(s_prefabStartPos, 14, 0.4f, (Color){180, 230, 255, 255});
          } else if (s_testIndex == 51) { /* STREAK FLARE */
              VFX_ComposeStreakFlare(s_prefabStartPos, 1.0f, (Color){255, 250, 220, 255});
          } else if (s_testIndex == 52) { /* GUST SLASH */
              VFX_ComposeGustSlash(Vector3Add(s_prefabStartPos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f);
          } else if (s_testIndex == 66) { /* EXPLOSION */
              VFX_TriggerExplosion(VC_MAT_FIRE, s_prefabStartPos, 1.0f, false);
          } else if (s_testIndex == 67) { /* GROUND WAVE */
              VFX_SpawnGroundWave(s_prefabStartPos, (Vector3){1, 0, 0}, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f, 2.0f);
          } else if (s_testIndex == 68) { /* PROJ FIRE */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f);
          } else if (s_testIndex == 69) { /* PROJ WATER */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f);
          } else if (s_testIndex == 70) { /* PROJ METAL */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f);
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

        if (s_testCategory == TEST_CAT_MESH)
        {
            s_isPlayingMesh = true;
            s_meshTime = 0.0f;
        }
        else if (s_testCategory == TEST_CAT_NEWFX)
        {
            s_isPlayingMesh = false;
            // @gen:newfx_trigger begin
          if (s_testIndex == 6) { /* EMBER DRIFT */
              VFX_ComposeEmberDrift(s_prefabStartPos, 0.8f, 12, (Color){255, 140, 60, 255});
          } else if (s_testIndex == 7) { /* IMPACT FIRE */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 8) { /* CAST FIRE */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f);
          } else if (s_testIndex == 9) { /* SPLASH */
              VFX_ComposeSplashBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 15) { /* IMPACT WATER */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 16) { /* CAST WATER */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WATER_SPLASH, 1.5f);
          } else if (s_testIndex == 18) { /* BLOOM BURST */
              VFX_ComposeBloomBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 21) { /* IMPACT WOOD */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 22) { /* CAST WOOD */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_WOOD_BLOOM, 1.5f);
          } else if (s_testIndex == 27) { /* SHRAPNEL */
              VFX_ComposeShrapnelBurst(Vector3Add(s_prefabStartPos, (Vector3){0, 0.25f, 0}), 1.0f);
          } else if (s_testIndex == 28) { /* RICOCHET */
              VFX_ComposeRicochetSpark(Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0}), (Vector3){0.7f, 0.7f, 0.0f}, 1.0f);
          } else if (s_testIndex == 30) { /* BOLT */
              VFX_ComposeLightningBolt(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){2, 1, 0}), 1.0f);
          } else if (s_testIndex == 31) { /* PROC BEAM */
              VFX_SpawnProcBeam(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){2, 1, 0}), EFFECT_PRESET_METAL_SHARD, 0.1f, 3.0f);
          } else if (s_testIndex == 32) { /* ORBITALS */
              VFX_SpawnOrbitals(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 5, 0.8f, 4.0f);
          } else if (s_testIndex == 33) { /* IMPACT METAL */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 34) { /* CAST METAL */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_METAL_SHARD, 1.5f);
          } else if (s_testIndex == 35) { /* ROCK BURST */
              VFX_ComposeRockBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 40) { /* FISSURE */
              VFX_ComposeFissureStreak(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3, 0, 0}), 0.15f);
          } else if (s_testIndex == 41) { /* IMPACT EARTH */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 42) { /* CAST EARTH */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_EARTH_CRACK, 1.5f);
          } else if (s_testIndex == 46) { /* AURA RING */
              VFX_SpawnAuraRing(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.0f, 5.0f);
          } else if (s_testIndex == 47) { /* IMPACT TAIJI */
              VFX_ComposeImpact(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 48) { /* CAST TAIJI */
              VFX_ComposeCast(s_prefabStartPos, EFFECT_PRESET_TAIJI_BURST, 1.5f);
          } else if (s_testIndex == 49) { /* SHOCKWAVE */
              VFX_ComposeShockwaveRing(s_prefabStartPos, 1.5f, 0.6f, (Color){255, 200, 80, 255});
          } else if (s_testIndex == 50) { /* GLINT BURST */
              VFX_ComposeGlintBurst(s_prefabStartPos, 14, 0.4f, (Color){180, 230, 255, 255});
          } else if (s_testIndex == 51) { /* STREAK FLARE */
              VFX_ComposeStreakFlare(s_prefabStartPos, 1.0f, (Color){255, 250, 220, 255});
          } else if (s_testIndex == 52) { /* GUST SLASH */
              VFX_ComposeGustSlash(Vector3Add(s_prefabStartPos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f);
          } else if (s_testIndex == 66) { /* EXPLOSION */
              VFX_TriggerExplosion(VC_MAT_FIRE, s_prefabStartPos, 1.0f, false);
          } else if (s_testIndex == 67) { /* GROUND WAVE */
              VFX_SpawnGroundWave(s_prefabStartPos, (Vector3){1, 0, 0}, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f, 2.0f);
          } else if (s_testIndex == 68) { /* PROJ FIRE */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f);
          } else if (s_testIndex == 69) { /* PROJ WATER */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f);
          } else if (s_testIndex == 70) { /* PROJ METAL */
              VFX_ComposeProjectileTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f);
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
              case 0: VFX_ComposeFlameWisp(s_prefabStartPos, s_meshTime); break;
              case 1: VFX_ComposeFirePillar(s_prefabStartPos, progress); break;
              case 2: VFX_ComposeFireball(Vector3Add(s_prefabStartPos, (Vector3){0, 0.8f, 0}), s_meshTime); break;
              case 3: VFX_ComposeFlameBreath(Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0}), (Vector3){1.0f, 0.05f, 0.0f}, 1.0f, s_meshTime); break;
              case 4: VFX_ComposeBurningGround(s_prefabStartPos, 0.9f, s_meshTime); break;
              case 5: VFX_ComposeFireWhirl(s_prefabStartPos, 0.5f, s_meshTime); break;
              case 10: VFX_ComposeBubbleStream(s_prefabStartPos, 0.8f, s_meshTime); break;
              case 11: VFX_ComposeMistVeil(s_prefabStartPos, 1.2f, s_meshTime); break;
              case 12: VFX_ComposeIceCrystal(s_prefabStartPos, posSeed); break;
              case 13: VFX_ComposeMagicPuddle(s_prefabStartPos); break;
              case 14: {
                      Vector3 wsP0 = s_prefabStartPos;
                      Vector3 wsP1 = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 0.8f, 0.5f});
                      Vector3 wsP2 = Vector3Add(s_prefabStartPos, (Vector3){2.0f, 1.2f, 0});
                      Vector3 wsP3 = Vector3Add(s_prefabStartPos, (Vector3){3.0f, 0, 0});
                      VFX_ComposeWaterStream(wsP0, wsP1, wsP2, wsP3, 0.06f, progress, s_meshTime);
                  break;
              }
              case 17: VFX_ComposeLeafSwirl(s_prefabStartPos, 0.8f, s_meshTime); break;
              case 19: VFX_ComposeLeafFall(s_prefabStartPos, 1.2f, s_meshTime); break;
              case 20: {
                      Vector3 gvStart = s_prefabStartPos;
                      Vector3 gvEnd   = Vector3Add(s_prefabStartPos, (Vector3){0, 0, 3.0f});
                      Vector3 gvC1    = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 1.0f, 0.8f});
                      Vector3 gvC2    = Vector3Add(s_prefabStartPos, (Vector3){0.5f, 1.0f, 2.0f});
                      VFX_ComposeGlowingVine(gvStart, gvEnd, gvC1, gvC2, gvEnd, progress, s_meshTime, 1.0f, 0, 1);
                  break;
              }
              case 23: VFX_ComposeMetalShardCluster(s_prefabStartPos, posSeed); break;
              case 24: VFX_ComposePlasmaOrb(Vector3Add(s_prefabStartPos, (Vector3){0, 0.9f, 0}), 0.5f, s_meshTime); break;
              case 25: VFX_ComposeBladeRing(s_prefabStartPos, 0.6f, 5, s_meshTime * 60.0f); break;
              case 26: VFX_ComposeBladeStorm(s_prefabStartPos, 0.7f, s_meshTime); break;
              case 29: VFX_ComposeStaticField(Vector3Add(s_prefabStartPos, (Vector3){0, 0.7f, 0}), 0.5f, s_meshTime); break;
              case 36: VFX_ComposeFloatingStones(s_prefabStartPos, 0.7f, s_meshTime); break;
              case 37: VFX_ComposeQuakeRumble(s_prefabStartPos, 1.3f, s_meshTime); break;
              case 38: VFX_ComposeStonePillar(s_prefabStartPos, progress); break;
              case 39: VFX_ComposeBoulder(Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0})); break;
              case 43: VFX_ComposeYinYangOrbit(Vector3Add(s_prefabStartPos, (Vector3){0, 0.8f, 0}), 0.45f, s_meshTime); break;
              case 44: VFX_ComposeElementalMist(VC_MAT_ICE, s_prefabStartPos, 1.0f, s_meshTime); break;
              case 45: VFX_ComposeQiAura(VC_MAT_FIRE, Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0}), progress, s_meshTime, 1.0f); break;
              case 53: VFX_ComposeSmokePuff(s_prefabStartPos, 0.8f); break;
              case 54: VFX_ComposeSmokeTrail(s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){0, 0, 3}), 1.0f); break;
              case 55: VFX_ComposeShield(VC_MAT_METAL, s_prefabStartPos, 1.2f, fminf(progress, 0.5f), s_meshTime); break;
              case 56: {
                      Vector3 chainTargets[4] = {
                          s_prefabStartPos,
                          Vector3Add(s_prefabStartPos, (Vector3){2.0f, 0, 1.0f}),
                          Vector3Add(s_prefabStartPos, (Vector3){3.5f, 0, -1.0f}),
                          Vector3Add(s_prefabStartPos, (Vector3){5.5f, 0, 0.5f}),
                      };
                      VFX_ComposeChain(VC_MAT_LIGHTNING, chainTargets, 4, progress, s_meshTime);
                  break;
              }
              case 57: VFX_ComposeZone(VC_MAT_FIRE, s_prefabStartPos, 1.2f, fminf(progress, 0.5f), s_meshTime); break;
              case 58: VFX_ComposeSlashArc(VC_MAT_METAL, s_prefabStartPos, (Vector3){1, 0, 0}, 1.0f, 120.0f, fminf(s_meshTime / 0.6f, 0.999f), s_meshTime); break;
              case 59: VFX_ComposeChargeUp(VC_MAT_FIRE, s_prefabStartPos, 1.0f, progress, s_meshTime); break;
              case 60: VFX_ComposeCyclone(s_prefabStartPos, 0.6f, s_meshTime); break;
              case 61: VFX_ComposeBeam(VC_MAT_LIGHTNING, s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){0, 1.5f, 3.0f}), 0.07f, progress, s_meshTime); break;
              case 62: VFX_ComposeProjectile(VC_MAT_FIRE, s_prefabStartPos, Vector3Add(s_prefabStartPos, (Vector3){3, 0, 0}), progress, 0.3f, s_meshTime); break;
              case 63: VFX_ComposeAura(VC_MAT_FIRE, s_prefabStartPos, 1.0f, s_meshTime); break;
              case 64: VFX_GroundPattern(GROUND_CRACK_RADIAL, s_prefabStartPos, 1.5f, progress, s_meshTime); break;
              case 65: VFX_SummonCircle(s_prefabStartPos, 1.5f, progress, s_meshTime, (Color){100, 200, 255, 255}); break;
              case 71: VFX_ComposeCylinderAura(VC_MAT_FIRE, s_prefabStartPos, 1.5f, fminf(progress, 0.99f), s_meshTime); break;
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

    // Toggle and back buttons
    Rectangle toggleBtn = {20, 15, 180, 32};
    Rectangle backBtn = {210, 15, 180, 32};
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
    float startY = 70.0f;
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
        maxIdx = 72;
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
    case 6: VFX_ComposeEmberDrift(pos, 0.8f, 12, (Color){255, 140, 60, 255}); break;
    case 7: VFX_ComposeImpact(pos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f); break;
    case 8: VFX_ComposeCast(pos, EFFECT_PRESET_FIRE_EXPLOSION, 1.5f); break;
    case 9: VFX_ComposeSplashBurst(pos, 1.0f); break;
    case 15: VFX_ComposeImpact(pos, EFFECT_PRESET_WATER_SPLASH, 1.5f); break;
    case 16: VFX_ComposeCast(pos, EFFECT_PRESET_WATER_SPLASH, 1.5f); break;
    case 18: VFX_ComposeBloomBurst(pos, 1.0f); break;
    case 21: VFX_ComposeImpact(pos, EFFECT_PRESET_WOOD_BLOOM, 1.5f); break;
    case 22: VFX_ComposeCast(pos, EFFECT_PRESET_WOOD_BLOOM, 1.5f); break;
    case 27: VFX_ComposeShrapnelBurst(Vector3Add(pos, (Vector3){0, 0.25f, 0}), 1.0f); break;
    case 28: VFX_ComposeRicochetSpark(Vector3Add(pos, (Vector3){0, 0.5f, 0}), (Vector3){0.7f, 0.7f, 0.0f}, 1.0f); break;
    case 30: VFX_ComposeLightningBolt(pos, Vector3Add(pos, (Vector3){2, 1, 0}), 1.0f); break;
    case 31: VFX_SpawnProcBeam(pos, Vector3Add(pos, (Vector3){2, 1, 0}), EFFECT_PRESET_METAL_SHARD, 0.1f, 3.0f); break;
    case 32: VFX_SpawnOrbitals(pos, EFFECT_PRESET_METAL_SHARD, 5, 0.8f, 4.0f); break;
    case 33: VFX_ComposeImpact(pos, EFFECT_PRESET_METAL_SHARD, 1.5f); break;
    case 34: VFX_ComposeCast(pos, EFFECT_PRESET_METAL_SHARD, 1.5f); break;
    case 35: VFX_ComposeRockBurst(pos, 1.0f); break;
    case 40: VFX_ComposeFissureStreak(pos, Vector3Add(pos, (Vector3){3, 0, 0}), 0.15f); break;
    case 41: VFX_ComposeImpact(pos, EFFECT_PRESET_EARTH_CRACK, 1.5f); break;
    case 42: VFX_ComposeCast(pos, EFFECT_PRESET_EARTH_CRACK, 1.5f); break;
    case 46: VFX_SpawnAuraRing(pos, EFFECT_PRESET_TAIJI_BURST, 1.0f, 5.0f); break;
    case 47: VFX_ComposeImpact(pos, EFFECT_PRESET_TAIJI_BURST, 1.5f); break;
    case 48: VFX_ComposeCast(pos, EFFECT_PRESET_TAIJI_BURST, 1.5f); break;
    case 49: VFX_ComposeShockwaveRing(pos, 1.5f, 0.6f, (Color){255, 200, 80, 255}); break;
    case 50: VFX_ComposeGlintBurst(pos, 14, 0.4f, (Color){180, 230, 255, 255}); break;
    case 51: VFX_ComposeStreakFlare(pos, 1.0f, (Color){255, 250, 220, 255}); break;
    case 52: VFX_ComposeGustSlash(Vector3Add(pos, (Vector3){0, 0.3f, 0}), (Vector3){1.0f, 0.0f, 0.0f}, 1.0f); break;
    case 66: VFX_TriggerExplosion(VC_MAT_FIRE, pos, 1.0f, false); break;
    case 67: VFX_SpawnGroundWave(pos, (Vector3){1, 0, 0}, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f, 2.0f); break;
    case 68: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_FIRE_EXPLOSION, 1.0f, 5.0f); break;
    case 69: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_WATER_SPLASH, 1.0f, 5.0f); break;
    case 70: VFX_ComposeProjectileTrail(pos, Vector3Add(pos, (Vector3){4, 0, 0}), EFFECT_PRESET_METAL_SHARD, 1.0f, 5.0f); break;
    default: break;
    }
// @gen:newfx_render_trigger end
}

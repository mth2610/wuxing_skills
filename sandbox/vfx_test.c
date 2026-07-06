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
#include <stddef.h> // Đảm bảo định nghĩa từ khóa NULL chuẩn xác

// Prefab Tester UI config
#define PREFAB_UI_X 20.0f
#define PREFAB_UI_Y 20.0f
#define PREFAB_BTN_W 140.0f
#define PREFAB_BTN_H 30.0f
#define PREFAB_BTN_SPACING 10.0f

typedef enum {
    TEST_CAT_IMPACT = 0,
    TEST_CAT_CAST,
    TEST_CAT_PROJECTILE,
    TEST_CAT_COMPOSER,
    TEST_CAT_MESH,
    TEST_CAT_BURST,
    TEST_CAT_NEWFX, // Phase 1-3 additions, migrated in from skills/taiji/core_test
    TEST_CAT_COUNT
} PrefabTestCategory;

static int s_testCategory = TEST_CAT_IMPACT;
static int s_testIndex = 0;
static bool s_isPlayingMesh = false;
static float s_meshTime = 0.0f;
static Vector3 s_prefabStartPos = {0};

static bool s_isPanelOpen = true;
static bool s_clickedOnUI = false;

static const char* s_elementNames[] = {
    "FIRE", "ICE", "WATER", "LIGHTNING", "EARTH", "WOOD", "METAL", "TAIJI"
};
// COMPOSER: 0=Smoke Puff, 1=Smoke Trail, 2=Fissure, 3=BOLT, 4=Stone Pillar, 5=Boulder, 6=Ice Crystal, 7=Magic Puddle, 8=Fireball
static const char* s_composerNames[] = {
    "SMOKE PUFF", "SMOKE TRAIL", "FISSURE", "BOLT",
    "STONE PILLAR", "BOULDER", "ICE CRYSTAL", "PUDDLE", "FIREBALL"
};
// MESH: 0-8=DrawEffectMesh presets (raw meshes)
static const char* s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON"
};
static const char* s_burstNames[] = {
    "FIRE", "ICE", "WATER", "LIGHTNING", "EARTH", "WOOD", "METAL", "TAIJI"
};
// NEWFX 0-4: continuous (mesh-style, needs s_isPlayingMesh), 5-8 + 15 + 18-19: one-shot burst,
// 9-14, 16-17: continuous archetype (progress-driven)
static const char* s_newFxNames[] = {
    "FLAME WISP", "FIRE PILLAR", "METAL SHARD", "PLASMA ORB", "BLADE RING",
    "SHOCKWAVE", "GLINT BURST", "EMBER DRIFT", "STREAK FLARE",
    "SHIELD", "CHAIN", "ZONE", "SLASH ARC", "CHARGE UP",
    "LEAF SWIRL", "BLOOM BURST", "LEAF FALL",
    "BLADE STORM", "SHRAPNEL", "RICOCHET"
};

// State cho ProcBolt (dùng trong COMPOSER tab - BOLT SKY)
typedef struct {
    int boltId;
    float boltTimer;
    Vector3 boltStart;
    Vector3 boltEnd;
    bool boltActive;
} CoreTestState;
static CoreTestState s_coreTest = {0};

typedef struct {
    bool active;
    Vector3 start;
    Vector3 end;
    float elapsed;
    float duration;
    float maxLife;
    float width;
    float spawnedDist;
} TestFissureStreak;
static TestFissureStreak s_testFissure = {0};

static int g_activeCountCache = 0;

// Nút test riêng ở góc trên-trái — vùng duy nhất chưa bị chiếm bởi joystick
// (bottom-left), dash/jump/fly (bottom-right) hay cam (top-right) trong
// sandbox_core.c. Touch trên Android tự map sang mouse position/button qua
// raylib, nên IsMouseButtonPressed hoạt động cho cả desktop lẫn cảm ứng.
// Đặt ở giữa-trái, tránh đè lên nút "[X] ẩn bảng điều khiển" (góc trên-trái)
// và panel debug skill-test (chiếm phần lớn nửa trên bên phải màn hình).
#define FF_TEST_BTN_X      70.0f
#define FF_TEST_BTN_Y      400.0f
#define FF_TEST_BTN_RADIUS 45.0f

// Nút test FORCE_VECTOR_TEXTURE — đặt ngay TRÊN nút FF TEST, cùng cột X (khu
// vực đã xác nhận an toàn). KHÔNG đặt phía dưới — trên thiết bị thật, vùng đó
// lấn vào bán kính kích hoạt joystick ảo (xem joystickCenter/joystickBaseRadius
// trong sandbox_core.c, neo theo % chiều cao màn hình nên khác trên từng máy).
#define VF_TEST_BTN_X      70.0f
#define VF_TEST_BTN_Y      300.0f
#define VF_TEST_BTN_RADIUS 45.0f

bool VFXTest_UpdateAndHandleInput(Vector3 playerPos, Vector3 mouseTarget3D, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex) {
  // Reset cache count mỗi frame
  g_activeCountCache = 0;
  s_clickedOnUI = false;

  float dt = GetFrameTime();
  if (s_testFissure.active) {
      s_testFissure.elapsed += dt;
      if (s_testFissure.elapsed >= s_testFissure.maxLife) {
          s_testFissure.active = false;
      }
  }

  if (IsKeyPressed(KEY_T)) {
    CameraFX_Shake(0.5f);
    ScreenDistort_Add(playerPos, 0.45f, 0.35f, 0.35f, 1.0f);

    VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 1.5f, 9999.0f, VFX_PRIORITY_LOW);
    DecalSystem_Add(playerPos, (float)GetRandomValue(0, 360), 0.3f,
                globalParticleTex, 3.0f, ORANGE);

    static ColorGradient g;
    static bool gradientInit = false;
    if (!gradientInit) {
      ColorGradient_AddStop(&g, 0.0f, RED);
      ColorGradient_AddStop(&g, 0.25f, ORANGE);
      ColorGradient_AddStop(&g, 0.5f, YELLOW);
      ColorGradient_AddStop(&g, 0.75f, GREEN);
      ColorGradient_AddStop(&g, 1.0f, BLUE);
      gradientInit = true;
    }

    static SpriteAnim anim;
    static bool animInit = false;
    if (!animInit) {
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

  // ---------------------------------------------------------------------
  // Test GPU compute force field — spawn hạt burst quanh 1 vortex.
  // Trigger bằng KEY_F (desktop) hoặc chạm nút "FF TEST" góc trên-trái
  // (Android không có bàn phím vật lý, IsKeyPressed(KEY_F) không bao giờ
  // kích hoạt trên NativeActivity — bắt buộc phải có đường touch).
  // Xác nhận trên Android: xem overlay GpuParticleSystem_DrawDebug() (HUD)
  // báo "COMPUTE (GPU)" thay vì "CPU / VBO", và hạt phải xoáy tròn quanh
  // playerPos thay vì bay thẳng ra — nếu vẫn bay thẳng nghĩa là force field
  // không tới được shader (registry/SSBO sai) dù path đang là COMPUTE.
  // ---------------------------------------------------------------------
  bool ffTestTouched =
      IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      CheckCollisionPointCircle(GetMousePosition(),
                                (Vector2){FF_TEST_BTN_X, FF_TEST_BTN_Y},
                                FF_TEST_BTN_RADIUS);
  if (IsKeyPressed(KEY_F) || ffTestTouched) {
    static ForceField s_gpuTestField;
    static bool s_gpuTestFieldInit = false;
    if (!s_gpuTestFieldInit) {
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
    for (int i = 0; i < 40; i++) {
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

  // ---------------------------------------------------------------------
  // Test FORCE_VECTOR_TEXTURE — hạt spawn đứng yên (velocity=0, drag=0) ở
  // mép trái một "box" flow texture thuần +X, KỲ VỌNG bị đẩy trôi sang phải
  // suốt bề rộng box rồi văng ra ngoài giữ nguyên vận tốc cuối (ngoài box =
  // gia tốc 0, không phải drag).
  // Cách đọc kết quả (KHÔNG đoán màu — nhìn chuyển động thô):
  //   - COMPUTE path + field hoạt động đúng: hạt trôi mượt sang +X ngay khi
  //     xuất hiện.
  //   - COMPUTE path nhưng field/texture sai (registry, UV, slot...): hạt
  //     đứng yên tại chỗ (vì velocity=0, drag=0, không lực nào khác).
  //   - CPU/VBO fallback (luôn đúng trên macOS — xem GpuParticleSystem_Draw-
  //     Debug HUD): hạt LUÔN đứng yên, vì FORCE_VECTOR_TEXTURE là no-op trên
  //     CPU path theo thiết kế (không phải bug).
  // Trigger bằng KEY_Y (desktop) hoặc nút "VF TEST" chạm (Android).
  // ---------------------------------------------------------------------
  bool vfTestTouched =
      IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      CheckCollisionPointCircle(GetMousePosition(),
                                (Vector2){VF_TEST_BTN_X, VF_TEST_BTN_Y},
                                VF_TEST_BTN_RADIUS);
  if (IsKeyPressed(KEY_Y) || vfTestTouched) {
    static Texture2D s_flowTex = {0};
    static ForceField s_flowField;
    static bool       s_flowInit = false;
    if (!s_flowInit) {
      // R=255 -> +X thuần (texel.r*2-1 = 1.0), G=128 -> ~0 theo Z.
      Image img = GenImageColor(4, 4, (Color){255, 128, 0, 255});
      s_flowTex = LoadTextureFromImage(img);
      UnloadImage(img);
      GpuParticleSystem_SetVectorFieldTexture(0, s_flowTex);

      ForceField_Clear(&s_flowField);
      ForceLayer vf = {0};
      vf.type      = FORCE_VECTOR_TEXTURE;
      vf.origin    = Vector3Add(playerPos, (Vector3){0.0f, 0.04f, 0.0f});
      vf.direction = (Vector3){0.3f, 0.0f, 0.3f}; // half-extent box (xz), 3m
      vf.strength  = 2.5f;
      vf.noiseScale = 0.0f; // slot 0
      ForceField_AddLayer(&s_flowField, vf);
      s_flowInit = true;
    }

    Vector3 spawnPos =
        Vector3Add(playerPos, (Vector3){-0.25f, 0.04f, 0.0f}); // mép trái box
    for (int i = 0; i < 20; i++) {
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

  // ---------------------------------------------------------------------
  // PREFAB TESTER UI INPUT
  // ---------------------------------------------------------------------
  Vector2 mousePos = GetMousePosition();
  bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

  Rectangle toggleBtn = {20, 15, 180, 32};
  Rectangle backBtn = {210, 15, 180, 32};

  if (CheckCollisionPointRec(mousePos, toggleBtn)) {
      s_clickedOnUI = true;
      if (clicked) s_isPanelOpen = !s_isPanelOpen;
  }
  if (CheckCollisionPointRec(mousePos, backBtn)) {
      s_clickedOnUI = true;
      if (clicked) return true; // Request back to menu
  }

  if (s_isPlayingMesh && (s_testCategory == TEST_CAT_MESH || s_testCategory == TEST_CAT_COMPOSER)) {
      s_meshTime += GetFrameTime();
      if (s_meshTime > 5.0f) s_isPlayingMesh = false; // Turn off mesh drawing after 5 seconds
  }

  if (s_isPanelOpen) {
      // Layout params
  float startX = 20.0f;
  float startY = 70.0f;
  float tabW = 120.0f;
  float tabH = 35.0f;
  float spacing = 10.0f;

  // Check tabs
  for (int i = 0; i < TEST_CAT_COUNT; i++) {
      Rectangle tabRec = { startX + i * (tabW + spacing), startY, tabW, tabH };
      if (CheckCollisionPointRec(mousePos, tabRec)) {
          s_clickedOnUI = true;
          if (clicked) {
              s_testCategory = i;
          }
      }
  }

  // Check buttons grid
  int maxIdx = 1;
  const char** names = NULL;
  if (s_testCategory == TEST_CAT_IMPACT || s_testCategory == TEST_CAT_CAST || s_testCategory == TEST_CAT_PROJECTILE) { maxIdx = 8; names = s_elementNames; }
  else if (s_testCategory == TEST_CAT_BURST) { maxIdx = 8; names = s_burstNames; }
  else if (s_testCategory == TEST_CAT_COMPOSER) { maxIdx = 9; names = s_composerNames; }
  else if (s_testCategory == TEST_CAT_MESH) { maxIdx = 9; names = s_meshNames; }
  else if (s_testCategory == TEST_CAT_NEWFX) { maxIdx = 20; names = s_newFxNames; }


  float gridY = startY + tabH + 20.0f;
  float btnW = 110.0f;
  float btnH = 35.0f;
  int columns = 6;

  for (int i = 0; i < maxIdx; i++) {
      int col = i % columns;
      int row = i / columns;
      Rectangle btnRec = { startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH };
      
      if (CheckCollisionPointRec(mousePos, btnRec)) {
          s_clickedOnUI = true;
          if (clicked) {
              s_testIndex = i;
          }
      }
  }

      // also mark clicked on UI if hovering over the whole UI background box
      Rectangle bgBox = { startX - 10, startY - 10, (tabW + spacing) * TEST_CAT_COUNT + 10, 440 };
      if (CheckCollisionPointRec(mousePos, bgBox)) {
          s_clickedOnUI = true;
      }
  }

  // Nếu click ra ngoài UI -> Spawn
  if (clicked && !s_clickedOnUI) {
      s_prefabStartPos = mouseTarget3D;
      Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){5.0f, 0.0f, 0.0f});
      
      if (s_testCategory == TEST_CAT_IMPACT) {
          VFX_ComposeImpact(s_prefabStartPos, (EffectPresetType)s_testIndex, 3.0f);
      } else if (s_testCategory == TEST_CAT_CAST) {
          VFX_ComposeCast(s_prefabStartPos, (EffectPresetType)s_testIndex, 3.0f);
      } else if (s_testCategory == TEST_CAT_PROJECTILE) {
          Vector3 projStart = Vector3Add(s_prefabStartPos, (Vector3){0.0f, 0.4f, 0.0f});
          Vector3 projEnd = Vector3Add(projStart, (Vector3){5.0f, 0.0f, 0.0f});
          VFX_ComposeProjectileTrail(projStart, projEnd, (EffectPresetType)s_testIndex, 1.0f, 8.0f);
      } else if (s_testCategory == TEST_CAT_COMPOSER) {
          // Beam/trail đi từ điểm click về phía trước (trục Z+)
          Vector3 composerEnd = Vector3Add(s_prefabStartPos, (Vector3){0.0f, 0.0f, 3.0f});
          if (s_testIndex == 0) VFX_ComposeSmokePuff(s_prefabStartPos, 1.0f);
          else if (s_testIndex == 1) VFX_ComposeSmokeTrail(s_prefabStartPos, composerEnd, 1.0f);
          else if (s_testIndex == 2) {
              TraceLog(LOG_INFO, "TRIGGER FISSURE: start=(%f,%f,%f) end=(%f,%f,%f)", playerPos.x, playerPos.y, playerPos.z, s_prefabStartPos.x, s_prefabStartPos.y, s_prefabStartPos.z);
              s_testFissure.active = true;
              s_testFissure.start = playerPos;
              s_testFissure.end = s_prefabStartPos;
              s_testFissure.elapsed = 0.0f;
              s_testFissure.duration = 0.6f;
              s_testFissure.maxLife = 4.6f; // total life
              s_testFissure.width = 0.4f;   // width of the crack
              s_testFissure.spawnedDist = 0.0f;
          }
          else if (s_testIndex == 3) { // BOLT: từ tay nhân vật đến điểm click
              TraceLog(LOG_INFO, "TRIGGER BOLT: start=(%f,%f,%f) end=(%f,%f,%f)", playerPos.x, playerPos.y + 0.4f, playerPos.z, s_prefabStartPos.x, s_prefabStartPos.y, s_prefabStartPos.z);
              if (s_coreTest.boltActive) ProcBolt_Kill(s_coreTest.boltId);
              s_coreTest.boltEnd   = s_prefabStartPos;
              s_coreTest.boltStart = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});
              s_coreTest.boltId    = VFX_ComposeLightningBolt(s_coreTest.boltStart, s_coreTest.boltEnd, 1.0f);
              s_coreTest.boltTimer = 0.5f;
              s_coreTest.boltActive = true;
          } else if (s_testIndex >= 4) {
              s_isPlayingMesh = true;
              s_meshTime = 0.0f;
          }
      } else if (s_testCategory == TEST_CAT_MESH) {
          s_isPlayingMesh = true;
          s_meshTime = 0.0f;
      } else if (s_testCategory == TEST_CAT_BURST) {
          // Lấy preset tương ứng của element (s_testIndex)
          const VFX_ImpactPreset *elemPreset = VFX_Preset_GetImpact((EffectPresetType)s_testIndex);
          if (elemPreset != NULL) {
              ImpactBurstConfig config = {0};
              
              // 1. Copy Screen Distortion
              config.distortEnabled = elemPreset->distortEnabled;
              config.distortRadius = elemPreset->distortRadius;
              config.distortStrength = elemPreset->distortStrength;
              config.distortLife = elemPreset->distortLife;
              config.distortSpeed = elemPreset->distortSpeed;
              
              // 2. Copy Ground Decal
              config.decalEnabled = elemPreset->decalEnabled;
              config.decalScale = elemPreset->decalScale;
              config.decalLife = elemPreset->decalLife;
              config.decalRandomRotation = true;
              
              // Xác định texture và màu decal cho từng element dựa theo preset
              Texture2D decalTex = {0};
              Color decalTint = WHITE;
              switch (elemPreset->decalPreset) {
                  case DECAL_PRESET_CRACK:
                      decalTex = ResourceManager_LoadTexture("assets/textures/crack.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.7f);
                      break;
                  case DECAL_PRESET_EARTH_SHATTER:
                      decalTex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.75f);
                      break;
                  case DECAL_PRESET_EARTH_RUNE:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_earth_rune.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f);
                      break;
                  case DECAL_PRESET_BURN:
                      decalTex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.65f);
                      break;
                  case DECAL_PRESET_FIRE_LAVA:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f);
                      break;
                  case DECAL_PRESET_WATER:
                      decalTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
                      break;
                  case DECAL_PRESET_WATER_SPLASH:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
                      break;
                  case DECAL_PRESET_WATER_RIPPLE:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
                      break;
                  case DECAL_PRESET_ICE:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
                      decalTint = ColorAlpha(WHITE, 0.55f);
                      break;
                  case DECAL_PRESET_WOOD_ROOT:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.75f);
                      break;
                  case DECAL_PRESET_WOOD_MOSS:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f);
                      break;
                  case DECAL_PRESET_METAL_SLASH:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_slash_mark.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f);
                      break;
                  case DECAL_PRESET_METAL_CRATER:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_METAL, 0.75f);
                      break;
                  case DECAL_PRESET_METAL_RUNE:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_metal_rune.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f);
                      break;
                  case DECAL_PRESET_TAIJI_RING:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_taiji_ring.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.8f);
                      break;
                  case DECAL_PRESET_TAIJI_LIGHTNING:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_lightning_char.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.85f);
                      break;
                  case DECAL_PRESET_TAIJI_WIND:
                      decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
                      decalTint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.75f);
                      break;
                  default:
                      decalTex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
                      decalTint = ORANGE;
                      break;
              }
              config.decalTex = decalTex;
              config.decalTint = decalTint;
              
              // 3. Copy Point Light Flash
              config.lightEnabled = elemPreset->lightEnabled;
              config.lightColor = elemPreset->lightColor;
              config.lightRadius = elemPreset->lightRadius;
              config.lightLife = elemPreset->lightLife;
              
              // 4. Copy Radial Particle Burst
              config.particlesEnabled = elemPreset->particlesEnabled;
              if (elemPreset->particlesEnabled) {
                  config.particles = elemPreset->particles;
                  // Tinh chỉnh số lượng hạt cho phù hợp với test burst (thường 20-30 hạt)
                  config.particles.countMin = 20;
                  config.particles.countMax = 30;
              }
              
              VFX_ComposeTriggerImpactBurst(s_prefabStartPos, 3.0f, &config);
          }
      } else if (s_testCategory == TEST_CAT_NEWFX) {
          if (s_testIndex == 15) {
              VFX_ComposeBloomBurst(s_prefabStartPos, 1.0f);
          } else if (s_testIndex == 18) {
              VFX_ComposeShrapnelBurst(Vector3Add(s_prefabStartPos, (Vector3){0, 0.25f, 0}), 1.0f);
          } else if (s_testIndex == 19) {
              // Deflect direction: up + outward along +X so the fan is easy to read.
              VFX_ComposeRicochetSpark(Vector3Add(s_prefabStartPos, (Vector3){0, 0.5f, 0}),
                                       (Vector3){0.7f, 0.7f, 0.0f}, 1.0f);
          } else if (s_testIndex <= 4 || s_testIndex >= 9) {
              // Continuous: needs per-frame redraw, handled in VFXTest_Draw3D.
              s_isPlayingMesh = true;
              s_meshTime = 0.0f;
          } else if (s_testIndex == 5) {
              VFX_ComposeShockwaveRing(s_prefabStartPos, 1.5f, 0.6f, (Color){255, 200, 80, 255});
          } else if (s_testIndex == 6) {
              VFX_ComposeGlintBurst(s_prefabStartPos, 14, 0.4f, (Color){180, 230, 255, 255});
          } else if (s_testIndex == 7) {
              VFX_ComposeEmberDrift(s_prefabStartPos, 0.8f, 12, (Color){255, 140, 60, 255});
          } else if (s_testIndex == 8) {
              VFX_ComposeStreakFlare(s_prefabStartPos, 1.0f, (Color){255, 250, 220, 255});
          }
      }
  }

  return false;
}

void VFXTest_Draw3D(void) {
  float dt = GetFrameTime();

  // C\u1eadp nh\u1eadt bolt timer (d\u00f9ng cho COMPOSER - BOLT SKY)
  if (s_coreTest.boltActive) {
      s_coreTest.boltTimer -= dt;
      if (s_coreTest.boltTimer <= 0.0f) {
          ProcBolt_Kill(s_coreTest.boltId);
          s_coreTest.boltActive = false;
      } else {
          ProcBolt_Update(s_coreTest.boltId, s_coreTest.boltStart, s_coreTest.boltEnd, 1.0f, dt);
          float brightness = s_coreTest.boltTimer / 0.5f;
          ProcBolt_SetBrightness(s_coreTest.boltId, brightness);
          ProcBolt_Draw(s_coreTest.boltId, camera);
      }
  }

  if (s_testFissure.active) {
      float progress = fminf(s_testFissure.elapsed / s_testFissure.duration, 1.0f);
      float alpha = 1.0f;
      if (s_testFissure.elapsed > s_testFissure.duration) {
          float rem = s_testFissure.maxLife - s_testFissure.elapsed;
          alpha = fminf(rem / 1.0f, 1.0f);
      }
      
      float totalDist = Vector3Distance(s_testFissure.start, s_testFissure.end);
      if (totalDist > 0.001f) {
          Vector3 dir = Vector3Normalize(Vector3Subtract(s_testFissure.end, s_testFissure.start));
          Vector3 currentEnd = Vector3Add(s_testFissure.start, Vector3Scale(dir, totalDist * progress));
          Vector3 right = (Vector3){ -dir.z, 0.0f, dir.x };
          float halfW = s_testFissure.width * 0.5f;
          
          Vector3 p1 = Vector3Add(s_testFissure.start, Vector3Scale(right, -halfW));
          Vector3 p2 = Vector3Add(currentEnd, Vector3Scale(right, -halfW));
          Vector3 p3 = Vector3Add(currentEnd, Vector3Scale(right, halfW));
          Vector3 p4 = Vector3Add(s_testFissure.start, Vector3Scale(right, halfW));
          
          float yOffset = 0.02f;
          p1.y += yOffset;
          p2.y += yOffset;
          p3.y += yOffset;
          p4.y += yOffset;
          
          Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
          
          rlDrawRenderBatchActive();
          BeginBlendMode(BLEND_ALPHA);
          rlDisableDepthMask();
          rlDisableBackfaceCulling();
          
          rlSetTexture(tex.id);
          rlBegin(RL_QUADS);
              rlColor4ub(255, 255, 255, (unsigned char)(255 * alpha));
              
              rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
              rlTexCoord2f(progress, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
              rlTexCoord2f(progress, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
              rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p4.x, p4.y, p4.z);
          rlEnd();
          rlSetTexture(0);
          
          rlDrawRenderBatchActive();
          rlEnableBackfaceCulling();
          rlEnableDepthMask();
          EndBlendMode();
      }
  }

  if (s_isPlayingMesh) {
      s_meshTime += dt;
      if (s_testCategory == TEST_CAT_MESH) {
          // DrawEffectMesh preset (0-8)
          Color color = WHITE;
          if (s_testIndex == 0) color = (Color){200, 200, 255, 180}; // Disc
          else if (s_testIndex == 1) color = (Color){255, 200, 100, 180}; // Ring
          DrawEffectMesh((MeshPresetType)s_testIndex, s_prefabStartPos, (Vector3){2.0f, 2.0f, 2.0f}, color);
      } else if (s_testCategory == TEST_CAT_COMPOSER && s_testIndex >= 4) {
          // Call visual_composer wrapper functions
          float progress = fminf(s_meshTime / 1.0f, 1.0f);
          int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;
          int idx = s_testIndex - 4;
          if (idx == 0) VFX_ComposeStonePillar(s_prefabStartPos, progress);
          else if (idx == 1) VFX_ComposeBoulder(s_prefabStartPos);
          else if (idx == 2) VFX_ComposeIceCrystal(s_prefabStartPos, posSeed);
          else if (idx == 3) VFX_ComposeMagicPuddle(s_prefabStartPos);
          else if (idx == 4) VFX_ComposeFireball(s_prefabStartPos, s_meshTime);
      } else if (s_testCategory == TEST_CAT_NEWFX) {
          float progress = fminf(s_meshTime / 1.0f, 1.0f);
          int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;
          switch (s_testIndex) {
              case 0: VFX_ComposeFlameWisp(s_prefabStartPos, s_meshTime); break;
              case 1: VFX_ComposeFirePillar(s_prefabStartPos, progress); break;
              case 2: VFX_ComposeMetalShardCluster(s_prefabStartPos, posSeed); break;
              case 3: VFX_ComposePlasmaOrb(Vector3Add(s_prefabStartPos, (Vector3){0, 0.9f, 0}), 0.5f, s_meshTime); break;
              case 4: VFX_ComposeBladeRing(s_prefabStartPos, 0.6f, 5, s_meshTime * 60.0f); break;
              case 9: VFX_ComposeShield(SHIELD_METAL, s_prefabStartPos, 1.2f, fminf(progress, 0.5f), s_meshTime); break;
              case 10: {
                  Vector3 chainTargets[4] = {
                      s_prefabStartPos,
                      Vector3Add(s_prefabStartPos, (Vector3){2.0f, 0, 1.0f}),
                      Vector3Add(s_prefabStartPos, (Vector3){3.5f, 0, -1.0f}),
                      Vector3Add(s_prefabStartPos, (Vector3){5.5f, 0, 0.5f}),
                  };
                  VFX_ComposeChain(CHAIN_LIGHTNING, chainTargets, 4, progress, s_meshTime);
                  break;
              }
              case 11: VFX_ComposeZone(ZONE_LAVA, s_prefabStartPos, 1.2f, fminf(progress, 0.5f), s_meshTime); break;
              case 12: VFX_ComposeSlashArc(SLASH_METAL, s_prefabStartPos, (Vector3){1, 0, 0}, 1.0f, 120.0f, fminf(s_meshTime / 0.6f, 0.999f), s_meshTime); break;
              case 13: VFX_ComposeChargeUp(CHARGE_FIRE, s_prefabStartPos, 1.0f, progress, s_meshTime); break;
              case 14: VFX_ComposeLeafSwirl(s_prefabStartPos, 0.8f, s_meshTime); break;
              case 16: VFX_ComposeLeafFall(s_prefabStartPos, 1.2f, s_meshTime); break;
              case 17: VFX_ComposeBladeStorm(s_prefabStartPos, 0.7f, s_meshTime); break;
          }
      }
  }
}

void VFXTest_DrawHUD(void) {
  VFXLightData activeLights[MAX_VFX_LIGHTS];
  int activeCount = 0;
  VFXLight_GetActive(activeLights, &activeCount, MAX_VFX_LIGHTS);
  DrawText(TextFormat("Active VFX Lights: %d / 8", activeCount), 10, 610,
           20, ORANGE);
  GpuParticleSystem_DrawDebug(10, 635);

  // Nút chạm test force field GPU (góc trên-trái, xem VFXTest_UpdateAndHandleInput)
  DrawCircle((int)FF_TEST_BTN_X, (int)FF_TEST_BTN_Y, FF_TEST_BTN_RADIUS,
             ColorAlpha(SKYBLUE, 0.5f));
  DrawCircleLines((int)FF_TEST_BTN_X, (int)FF_TEST_BTN_Y, FF_TEST_BTN_RADIUS,
                  SKYBLUE);
  DrawText("FF", (int)FF_TEST_BTN_X - 14, (int)FF_TEST_BTN_Y - 12, 20, WHITE);
  DrawText("TEST", (int)FF_TEST_BTN_X - 22, (int)FF_TEST_BTN_Y + 10, 14, WHITE);

  // Nút chạm test FORCE_VECTOR_TEXTURE (xem VFXTest_UpdateAndHandleInput)
  DrawCircle((int)VF_TEST_BTN_X, (int)VF_TEST_BTN_Y, VF_TEST_BTN_RADIUS,
             ColorAlpha(GOLD, 0.5f));
  DrawCircleLines((int)VF_TEST_BTN_X, (int)VF_TEST_BTN_Y, VF_TEST_BTN_RADIUS,
                  GOLD);
  DrawText("VF", (int)VF_TEST_BTN_X - 14, (int)VF_TEST_BTN_Y - 12, 20, WHITE);
  DrawText("TEST", (int)VF_TEST_BTN_X - 22, (int)VF_TEST_BTN_Y + 10, 14, WHITE);

  // Prefab Tester UI - toggle and back buttons
  Rectangle toggleBtn = {20, 15, 180, 32};
  Rectangle backBtn = {210, 15, 180, 32};
  Vector2 mousePos = GetMousePosition();

  // Draw toggle
  bool isOverToggle = CheckCollisionPointRec(mousePos, toggleBtn);
  Color toggleCol = s_isPanelOpen ? (isOverToggle ? RED : MAROON) : (isOverToggle ? LIME : DARKGREEN);
  DrawRectangleRounded(toggleBtn, 0.2f, 10, toggleCol);
  DrawRectangleRoundedLines(toggleBtn, 0.2f, 10, WHITE);
  const char *toggleText = s_isPanelOpen ? "[X] AN BANG DIEU KHIEN" : "[+] HIEN BANG DIEU KHIEN";
  int tW = MeasureText(toggleText, 10);
  DrawText(toggleText, (int)(toggleBtn.x + (toggleBtn.width - tW) / 2), (int)(toggleBtn.y + 11), 10, WHITE);

  // Draw back
  bool isOverBack = CheckCollisionPointRec(mousePos, backBtn);
  Color backCol = isOverBack ? MAROON : DARKGRAY;
  DrawRectangleRounded(backBtn, 0.2f, 10, backCol);
  DrawRectangleRoundedLines(backBtn, 0.2f, 10, WHITE);
  const char *backText = "[<] QUAY LAI MENU";
  int bW = MeasureText(backText, 10);
  DrawText(backText, (int)(backBtn.x + (backBtn.width - bW) / 2), (int)(backBtn.y + 11), 10, WHITE);

  if (!s_isPanelOpen) return;

  // Prefab Tester UI
  float startX = 20.0f;
  float startY = 70.0f;
  float tabW = 120.0f;
  float tabH = 35.0f;
  float spacing = 10.0f;

  // Draw background
  DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), ColorAlpha(BLACK, 0.4f)); // subtle screen dim
  Rectangle bgBox = { startX - 10, startY - 10, (tabW + spacing) * TEST_CAT_COUNT + 10, 340 };
  DrawRectangleRounded(bgBox, 0.05f, 10, ColorAlpha(BLACK, 0.6f));
  DrawRectangleRoundedLines(bgBox, 0.05f, 10, ColorAlpha(WHITE, 0.3f));

  // Draw Tabs
  const char* tabNames[] = { "IMPACT", "CAST", "PROJECTILE", "COMPOSER", "MESH", "BURST", "NEW FX" };
  for (int i = 0; i < TEST_CAT_COUNT; i++) {
      Rectangle tabRec = { startX + i * (tabW + spacing), startY, tabW, tabH };
      bool isHover = CheckCollisionPointRec(mousePos, tabRec);
      bool isSelected = (s_testCategory == i);
      Color btnCol = isSelected ? ORANGE : (isHover ? DARKGRAY : ColorAlpha(DARKGRAY, 0.5f));
      
      DrawRectangleRounded(tabRec, 0.3f, 10, btnCol);
      DrawRectangleRoundedLines(tabRec, 0.3f, 10, WHITE);
      
      int textW = MeasureText(tabNames[i], 12);
      DrawText(tabNames[i], (int)(tabRec.x + (tabW - textW) / 2), (int)(tabRec.y + 11), 12, isSelected ? BLACK : WHITE);
  }

  // Draw Grid
  int maxIdx = 1;
  const char** names = NULL;
  if (s_testCategory == TEST_CAT_IMPACT || s_testCategory == TEST_CAT_CAST || s_testCategory == TEST_CAT_PROJECTILE) { maxIdx = 8; names = s_elementNames; }
  else if (s_testCategory == TEST_CAT_BURST) { maxIdx = 8; names = s_burstNames; }
  else if (s_testCategory == TEST_CAT_COMPOSER) { maxIdx = 9; names = s_composerNames; }
  else if (s_testCategory == TEST_CAT_MESH) { maxIdx = 9; names = s_meshNames; }
  else if (s_testCategory == TEST_CAT_NEWFX) { maxIdx = 20; names = s_newFxNames; }


  float gridY = startY + tabH + 20.0f;
  float btnW = 110.0f;
  float btnH = 35.0f;
  int columns = 6;

  for (int i = 0; i < maxIdx; i++) {
      int col = i % columns;
      int row = i / columns;
      Rectangle btnRec = { startX + col * (btnW + spacing), gridY + row * (btnH + spacing), btnW, btnH };
      
      bool isHover = CheckCollisionPointRec(mousePos, btnRec);
      Color btnCol = isHover ? MAROON : ColorAlpha(DARKGRAY, 0.5f);
      if (s_testIndex == i) {
          btnCol = ORANGE;
      }
      
      DrawRectangleRounded(btnRec, 0.3f, 10, btnCol);
      DrawRectangleRoundedLines(btnRec, 0.3f, 10, WHITE);
      
      int textW = MeasureText(names[i], 12);
      DrawText(names[i], (int)(btnRec.x + (btnW - textW) / 2), (int)(btnRec.y + 11), 12, WHITE);
  }
}
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
#include "core/resource_manager.h"
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
static const char* s_composerNames[] = {
    "SMOKE PUFF", "SMOKE TRAIL", "FISSURE", "LIGHTNING BEAM"
};
static const char* s_meshNames[] = {
    "DISC", "RING", "CONE", "TORNADO", "CYLINDER", "SPHERE", "SHOCKWAVE", "PYRAMID", "TETRAHEDRON"
};
static const char* s_burstNames[] = {
    "FIRE", "ICE", "WATER", "LIGHTNING", "EARTH", "WOOD", "METAL", "TAIJI"
};

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
    tConfig.pos = Vector3Add(playerPos, (Vector3){0.075f, 0.03f, 0.0f});
    tConfig.vel = (Vector3){0.22f, 0.0f, 0.0f};
    tConfig.len = 0.05f;
    tConfig.thick = 0.006f;
    tConfig.trailLength = 0.8f;
    tConfig.life = 4.0f;
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

  if (s_isPlayingMesh && s_testCategory == TEST_CAT_MESH) {
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
  else if (s_testCategory == TEST_CAT_COMPOSER) { maxIdx = 4; names = s_composerNames; }
  else if (s_testCategory == TEST_CAT_MESH) { maxIdx = 9; names = s_meshNames; }


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
      Rectangle bgBox = { startX - 10, startY - 10, (tabW + spacing) * TEST_CAT_COUNT + 10, 400 };
      if (CheckCollisionPointRec(mousePos, bgBox)) {
          s_clickedOnUI = true;
      }
  }

  // Nếu click ra ngoài UI -> Spawn
  if (clicked && !s_clickedOnUI) {
      s_prefabStartPos = mouseTarget3D;
      Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){0.8f, 0.0f, 0.0f});
      
      if (s_testCategory == TEST_CAT_IMPACT) {
          VFX_ComposeImpact(s_prefabStartPos, (EffectPresetType)s_testIndex, 1.0f);
      } else if (s_testCategory == TEST_CAT_CAST) {
          VFX_ComposeCast(s_prefabStartPos, (EffectPresetType)s_testIndex, 1.0f);
      } else if (s_testCategory == TEST_CAT_PROJECTILE) {
          VFX_ComposeProjectileTrail(s_prefabStartPos, endPos, (EffectPresetType)s_testIndex, 1.0f, 1.5f);
      } else if (s_testCategory == TEST_CAT_COMPOSER) {
          if (s_testIndex == 0) VFX_ComposeSmokePuff(s_prefabStartPos, 1.0f);
          else if (s_testIndex == 1) VFX_ComposeSmokeTrail(s_prefabStartPos, endPos, 1.0f);
          else if (s_testIndex == 2) VFX_ComposeFissure(s_prefabStartPos, endPos, 1.0f);
          else if (s_testIndex == 3) VFX_ComposeLightningBeam(s_prefabStartPos, endPos, 1.0f);
      } else if (s_testCategory == TEST_CAT_MESH) {
          s_isPlayingMesh = true;
          s_meshTime = 0.0f;
      } else if (s_testCategory == TEST_CAT_BURST) {
          // Test đầy đủ 4 bước của VFX_ComposeTriggerImpactBurst.
          // Gradient và forceField được lấy trực tiếp từ VFX_Preset_GetImpact
          // theo element đang chọn (s_testIndex) để đồng nhất màu sắc.
          static ImpactBurstConfig s_burstCfg = {0};
          static bool s_burstCfgInit = false;
          if (!s_burstCfgInit) {
              // Bước 1: Screen Distortion
              s_burstCfg.distortEnabled   = true;
              s_burstCfg.distortRadius    = 0.45f;
              s_burstCfg.distortStrength  = 0.35f;
              s_burstCfg.distortLife      = 0.35f;
              s_burstCfg.distortSpeed     = 1.0f;
              // Bước 2: Ground Decal (dùng texture globalParticleTex làm placeholder)
              s_burstCfg.decalEnabled          = true;
              s_burstCfg.decalScale            = 0.3f;
              s_burstCfg.decalLife             = 5.0f;
              s_burstCfg.decalTint             = ORANGE;
              s_burstCfg.decalRandomRotation   = true;
              // Bước 3: Point Light Flash
              s_burstCfg.lightEnabled  = true;
              s_burstCfg.lightColor    = (Color){255, 140, 40, 255};
              s_burstCfg.lightRadius   = 0.6f;
              s_burstCfg.lightLife     = 0.5f;
              // Bước 4: Radial Particle Burst (thông số hệ mét)
              s_burstCfg.particlesEnabled          = true;
              s_burstCfg.particles.countMin        = 30;
              s_burstCfg.particles.countMax        = 40;
              s_burstCfg.particles.speedMin        = 0.15f;
              s_burstCfg.particles.speedMax        = 0.45f;
              s_burstCfg.particles.radiusMin       = 0.008f;
              s_burstCfg.particles.radiusMax       = 0.025f;
              s_burstCfg.particles.lifetimeMin     = 0.5f;
              s_burstCfg.particles.lifetimeMax     = 1.5f;
              s_burstCfg.particles.pitchRange      = 0.6f;
              s_burstCfg.particles.upwardBias      = 0.3f;
              s_burstCfgInit = true;
          }
          // Lấy gradient và forceField theo element được chọn (s_testIndex)
          const VFX_ImpactPreset *elemPreset = VFX_Preset_GetImpact((EffectPresetType)s_testIndex);
          if (elemPreset != NULL) {
              if (elemPreset->particlesEnabled) {
                  s_burstCfg.particles.gradient   = elemPreset->particles.gradient;
                  s_burstCfg.particles.forceField = elemPreset->particles.forceField;
              }
              if (elemPreset->lightEnabled) {
                  s_burstCfg.lightColor = elemPreset->lightColor;
              }
          }
          VFX_ComposeTriggerImpactBurst(s_prefabStartPos, 1.0f, &s_burstCfg);
      }
  }
  
  return false;
}

void VFXTest_Draw3D(void) {

  if (s_isPlayingMesh && s_testCategory == TEST_CAT_MESH) {
      Color color = WHITE;
      if (s_testIndex == 0) color = (Color){200, 200, 255, 180}; // Disc
      else if (s_testIndex == 1) color = (Color){255, 200, 100, 180}; // Ring
      DrawEffectMesh((MeshPresetType)s_testIndex, s_prefabStartPos, (Vector3){2.0f, 2.0f, 2.0f}, color);
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
  Rectangle bgBox = { startX - 10, startY - 10, (tabW + spacing) * TEST_CAT_COUNT + 10, 300 };
  DrawRectangleRounded(bgBox, 0.05f, 10, ColorAlpha(BLACK, 0.6f));
  DrawRectangleRoundedLines(bgBox, 0.05f, 10, ColorAlpha(WHITE, 0.3f));

  // Draw Tabs
  const char* tabNames[] = { "IMPACT", "CAST", "PROJECTILE", "COMPOSER", "MESH", "BURST" };
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
  else if (s_testCategory == TEST_CAT_COMPOSER) { maxIdx = 4; names = s_composerNames; }
  else if (s_testCategory == TEST_CAT_MESH) { maxIdx = 9; names = s_meshNames; }


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
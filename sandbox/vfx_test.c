#include "vfx_test.h"
#include "compute/gpu_particle_system.h"
#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/screen_distort.h"
#include "core/trail_system.h"
#include "core/vfx_light.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h> // Đảm bảo định nghĩa từ khóa NULL chuẩn xác

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

void VFXTest_UpdateAndHandleInput(Vector3 playerPos, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex) {
  if (IsKeyPressed(KEY_T)) {
    CameraFX_Shake(0.5f);
    ScreenDistort_Add(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);

    VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 150.0f, 9999.0f);
    DecalSystem_Add(playerPos, (float)GetRandomValue(0, 360), 40.0f,
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
    deathChildConfig.radius = 25.0f;
    deathChildConfig.lifetime = 1.5f;
    deathChildConfig.gradient = &g;
    deathChildConfig.forceField = NULL;
    deathChildConfig.spriteAnim = NULL;
    deathChildConfig.onLiveEmit = NULL;
    deathChildConfig.onDeathEmit = NULL;

    static ParticleConfig liveChildConfig;
    liveChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};
    liveChildConfig.colorStart = ORANGE;
    liveChildConfig.colorEnd = RED;
    liveChildConfig.radius = 30.0f;
    liveChildConfig.lifetime = 0.8f;
    liveChildConfig.gradient = &g;
    liveChildConfig.forceField = NULL;
    liveChildConfig.spriteAnim = NULL;
    liveChildConfig.onLiveEmit = NULL;
    liveChildConfig.onDeathEmit = NULL;

    ParticleConfig motherConfig = {0};
    motherConfig.position =
        Vector3Add(playerPos, (Vector3){-60.0f, 15.0f, 0.0f});
    motherConfig.velocity = (Vector3){120.0f, 0.0f, 0.0f};
    motherConfig.radius = 40.0f;
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
    tConfig.pos = Vector3Add(playerPos, (Vector3){75.0f, 30.0f, 0.0f});
    tConfig.vel = (Vector3){220.0f, 0.0f, 0.0f};
    tConfig.len = 50.0f;
    tConfig.thick = 6.0f;
    tConfig.trailLength = 80.0f;
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
      vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
      vortex.direction = (Vector3){0.0f, 1.0f, 0.0f};
      vortex.strength = 400.0f;
      ForceField_AddLayer(&s_gpuTestField, vortex);
      s_gpuTestFieldInit = true;
    }

    Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
    for (int i = 0; i < 40; i++) {
      float ang = ((float)GetRandomValue(0, 359)) * DEG2RAD;
      GpuParticleConfig cfg = {0};
      cfg.position = center;
      cfg.velocity = (Vector3){cosf(ang) * 150.0f, 0.0f, sinf(ang) * 150.0f};
      cfg.colorStart = (Color){80, 200, 255, 255};
      cfg.colorEnd = (Color){80, 200, 255, 0};
      cfg.radius = 6.0f;
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
      vf.origin    = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
      vf.direction = (Vector3){300.0f, 0.0f, 300.0f}; // half-extent box (xz)
      vf.strength  = 250.0f;
      vf.noiseScale = 0.0f; // slot 0
      ForceField_AddLayer(&s_flowField, vf);
      s_flowInit = true;
    }

    Vector3 spawnPos =
        Vector3Add(playerPos, (Vector3){-250.0f, 40.0f, 0.0f}); // mép trái box
    for (int i = 0; i < 20; i++) {
      GpuParticleConfig cfg = {0};
      cfg.position = Vector3Add(
          spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80)});
      cfg.velocity = (Vector3){0.0f, 0.0f, 0.0f};
      cfg.colorStart = (Color){255, 220, 100, 255};
      cfg.colorEnd = (Color){255, 220, 100, 0};
      cfg.radius = 8.0f;
      cfg.lifetime = 3.0f;
      cfg.drag = 0.0f;
      cfg.forceField = &s_flowField;
      GpuParticleSystem_Spawn(cfg);
    }
  }
}

void VFXTest_DrawDebugLights3D(void) {
  VFXLightData activeLights[MAX_VFX_LIGHTS];
  VFXLight_GetActive(activeLights, &g_activeCountCache, MAX_VFX_LIGHTS);
  for (int i = 0; i < g_activeCountCache; i++) {
    DrawSphereWires(activeLights[i].position, activeLights[i].radius, 8, 8,
                    ColorAlpha(activeLights[i].color, 0.2f));
    DrawSphere(activeLights[i].position, 5.0f, activeLights[i].color);
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
}
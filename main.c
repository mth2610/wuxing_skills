#include "core/camera_fx.h"
#include "core/particle_system.h" // Thư viện quản lý hạt toàn cục (Compute Shader)
#include "core/post_fx.h"
#include "core/sandbox_core.h"
#include "core/screen_distort.h"
#include "core/skill_manager.h"
#include "core/trail_system.h" // MỚI: Thư viện quản lý dải khí, kiếm bay (Smart Projectiles)
#include "core/ui_panel.h"
#include "core/vfx_light.h" // MỚI: Tích hợp hệ thống Dynamic Point Light
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "skills/metal/sword_rain/sword_rain_skill.h"
#include <math.h>
#include <stdio.h>

// Biến camera toàn cục
Camera3D camera = {0};
PlayerEntity player = {0};

static void MyBeginMode3D(Camera3D camera) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPushMatrix();
  rlLoadIdentity();
  float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();

  if (camera.projection == CAMERA_PERSPECTIVE) {
    double top = 10.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    rlFrustum(-right, right, -top, top, 10.0, 15000.0);
  } else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
    double top = camera.fovy / 2.0;
    double right = top * aspect;
    rlOrtho(-right, right, -top, top, 0.01, 15000.0);
  }

  rlMatrixMode(RL_MODELVIEW);
  rlPushMatrix();
  rlLoadIdentity();
  Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
  rlMultMatrixf(MatrixToFloat(matView));
  rlEnableDepthTest();
}

static void MyEndMode3D(void) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPopMatrix();
  rlMatrixMode(RL_MODELVIEW);
  rlPopMatrix();
  rlLoadIdentity();
  rlDisableDepthTest();
}

// =============================================================================
// ĐẶT HÀM TEST ĐỊNH NGHĨA TRƯỚC MAIN ĐỂ TRÁNH LỖI IMPLICIT DECLARATION
// =============================================================================
static void Test_VFX_AllFeatures(Vector3 playerPos, Texture2D testAtlasTex) {
  // 1. Kích hoạt hiệu ứng môi trường/camera
  CameraFX_Shake(0.5f);
  ScreenDistort_AddSource(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);

  // 2. Spawn Dynamic Point Light ảo tại vị trí người chơi để quản lý
  VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 200.0f, 1.5f);

  // 3. Khởi tạo dải màu Gradient dùng chung (Tĩnh - Tạo 1 lần)
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

  // 4. Khởi tạo SpriteAnim cho Trail (Tĩnh - Tạo 1 lần)
  static SpriteAnim anim;
  static bool animInit = false;
  if (!animInit) {
    SpriteAnim_Init(&anim, 2, 2, 4, 8.0f, ANIM_LOOP);
    animInit = true;
  }

  // 5. CẤU HÌNH SUB-EMITTER
  // Hạt con sinh ra khi mẹ chết
  static ParticleConfig deathChildConfig;
  deathChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};
  deathChildConfig.colorStart = BLUE;
  deathChildConfig.colorEnd = BLACK;
  deathChildConfig.radius = 8.0f;
  deathChildConfig.lifetime = 1.0f;
  deathChildConfig.gradient = &g;
  deathChildConfig.forceField = NULL;
  deathChildConfig.spriteAnim = NULL;
  deathChildConfig.onLiveEmit = NULL;
  deathChildConfig.onDeathEmit = NULL;

  // Hạt con nhả vệt đuôi khi mẹ đang bay
  static ParticleConfig liveChildConfig;
  liveChildConfig.velocity = (Vector3){0.0f, -5.0f, 0.0f};
  liveChildConfig.colorStart = ORANGE;
  liveChildConfig.colorEnd = RED;
  liveChildConfig.radius = 2.0f;
  liveChildConfig.lifetime = 0.5f;
  liveChildConfig.gradient = &g;
  liveChildConfig.forceField = NULL;
  liveChildConfig.spriteAnim = NULL;
  liveChildConfig.onLiveEmit = NULL;
  liveChildConfig.onDeathEmit = NULL;

  // Hạt mẹ chính bắn lên trời
  ParticleConfig motherConfig = {0};
  motherConfig.position = Vector3Add(playerPos, (Vector3){-50.0f, 10.0f, 0.0f});
  motherConfig.velocity = (Vector3){0.0f, 180.0f, 0.0f};
  motherConfig.radius = 25.0f;
  motherConfig.lifetime = 1.2f;
  motherConfig.colorStart = WHITE;
  motherConfig.colorEnd = YELLOW;
  motherConfig.gradient = &g;
  motherConfig.onLiveEmit = &liveChildConfig;
  motherConfig.onLiveEmitRate = 40.0f;
  motherConfig.onDeathEmit = &deathChildConfig;
  motherConfig.onDeathEmitCount = 15;

  SpawnParticle(motherConfig);

  // 6. BẮN TRAIL PROJECTILE (Bên phải player)
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

int main(void) {
  const int screenWidth = 1200;
  const int screenHeight = 700;
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");
  rlSetClipPlanes(0.1f, 15000.0f);

  // -----------------------------------------------------------------
  // KHỞI TẠO HỆ THỐNG VFX
  // -----------------------------------------------------------------
  InitParticleSystem();
  Shader defaultTrailShader =
      LoadShader(0, "skills/metal/metal_projectile/metal.fs");
  InitTrailSystem(defaultTrailShader);
  VFXLight_Init(); // Khởi tạo hệ thống Dynamic Point Light
  ScreenDistort_Init(screenWidth, screenHeight);
  PostFX_Init(screenWidth, screenHeight);

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  UnloadImage(img);

  Image atlasImg = GenImageColor(128, 128, BLANK);
  ImageDrawCircle(&atlasImg, 32, 32, 20, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 12, 12, 40, 40, WHITE);
  ImageDrawCircle(&atlasImg, 32, 96, 12, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 16, 64 + 16, 32, 32, WHITE);
  Texture2D testAtlasTex = LoadTextureFromImage(atlasImg);
  UnloadImage(atlasImg);

  InitSkillManager(screenWidth, screenHeight);
  RegisterStaticOccluder((Vector3){400.0f, 0.0f, 320.0f}, 25.0f, 62.5f);
  RegisterStaticOccluder((Vector3){800.0f, 0.0f, 520.0f}, 30.0f, 75.0f);
  RegisterStaticOccluder((Vector3){600.0f, 0.0f, 260.0f}, 20.0f, 50.0f);
  InitSwordRainSkill();
  InitUIPanel();

  EnemyEntity enemy;
  InitSandbox(&player, &enemy);

  UIPanelState uiState = {0};
  uiState.activeSkillIndex = 0;
  uiState.currentParams.level = 1;
  uiState.currentParams.milestone = 1;
  uiState.currentParams.sizeScale = 1.0f;
  uiState.currentParams.quantity = 3;
  uiState.currentParams.anchorType = CAST_ANCHOR_TARGET;
  uiState.currentParams.pathType = CAST_PATH_PROJECTILE;
  uiState.currentParams.showPortal = true;
  uiState.currentParams.damage = 100.0f;

  PostFXConfig postFXConfig = {.bloomEnabled = true,
                               .bloomThreshold = 0.65f,
                               .bloomIntensity = 0.8f,
                               .chromaticEnabled = true,
                               .chromaticStrength = 0.15f,
                               .vignetteEnabled = true,
                               .vignetteRadius = 0.85f,
                               .vignetteSoftness = 0.45f,
                               .colorGradeEnabled = true,
                               .contrast = 1.05f,
                               .saturation = 1.15f,
                               .colorTint = {1.0f, 1.0f, 1.0f}};

  SetTargetFPS(60);

  // Đưa biến quản lý đếm light ra phạm vi hàm main() để dùng được cả trong pass
  // 3D và HUD 2D
  VFXLightData activeLights[MAX_VFX_LIGHTS];
  int activeLightCount = 0;

  while (!WindowShouldClose()) {
    float dt = GetFrameTime();

    UpdateUIPanel(GetMousePosition(), &uiState);

    Vector3 mouseTarget3D = {0};
    UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);

    CameraFX_Update(&camera, dt);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      CastSkill(uiState.activeSkillIndex, player.position, mouseTarget3D,
                uiState.currentParams);
    }

    // TEST FEATURE: Gọi hàm tổng bốc tách gọn gàng ngoài main
    if (IsKeyPressed(KEY_T)) {
      Test_VFX_AllFeatures(player.position, testAtlasTex);
    }

    UpdateSkillManager(dt, enemy.position, 35.0f);
    UpdateParticles(dt);
    UpdateTrailSystem(dt);
    VFXLight_Update(dt); // Cập nhật trạng thái đếm ngược của các Light
    ScreenDistort_Update(dt);

    BeginDrawing();

    ScreenDistort_Begin();
    ClearBackground(GetColor(0x111111FF));

    MyBeginMode3D(camera);
    DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    DrawSkillManagerWorld3D();

    // Thu thập danh sách light phục vụ render debug trong không gian 3D
    VFXLight_GetActive(activeLights, &activeLightCount, MAX_VFX_LIGHTS);
    for (int i = 0; i < activeLightCount; i++) {
      DrawSphereWires(activeLights[i].position, activeLights[i].radius, 8, 8,
                      ColorAlpha(activeLights[i].color, 0.2f));
      DrawSphere(activeLights[i].position, 5.0f, activeLights[i].color);
    }

    DrawTrailEntities(camera);

    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawParticles(camera, globalParticleTex);
    EndBlendMode();
    rlEnableDepthMask();

    MyEndMode3D();
    ScreenDistort_End();

    PostFX_Begin();
    ClearBackground(BLACK);
    ScreenDistort_Draw(camera);
    PostFX_End();

    ClearBackground(BLACK);
    PostFX_Draw(&postFXConfig);

    DrawSkillManagerOverlay();

    Vector2 enemyScreenHead = GetWorldToScreen(
        (Vector3){enemy.position.x, enemy.position.y + 55.0f, enemy.position.z},
        camera);
    DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
             WHITE);

    DrawUIPanel(&uiState);

    DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);

    // In trạng thái đếm số lượng Light hoạt động lên HUD (Đã có biến
    // activeLightCount hợp lệ)
    DrawText(TextFormat("Active VFX Lights: %d / 8", activeLightCount), 10, 610,
             20, ORANGE);

    DrawText("Phím P: Đổi chế độ quái | Click trái: Tung chiêu | X: Khinh công "
             "| Phím T: Test Light/Gradient/Anim",
             10, 670, 20, LIGHTGRAY);
    EndDrawing();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  UnloadTrailSystem();
  ScreenDistort_Unload();
  UnloadSkillManager();
  CloseWindow();

  return 0;
}
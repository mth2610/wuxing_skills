#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/post_fx.h"
#include "core/sandbox_core.h"
#include "core/screen_distort.h"
#include "core/skill_manager.h"
#include "core/trail_system.h"
#include "core/ui_panel.h"
#include "core/vfx_light.h"
#include "core/vfx_test.h" // MỚI: Chỉ giữ duy nhất file test này để điều phối
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
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

int main(void) {
  const int screenWidth = 1200;
  const int screenHeight = 700;
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");
  rlSetClipPlanes(0.1f, 15000.0f);

  // Tự động sinh các texture cơ bản nếu thiếu trong thư mục assets/textures
  if (!FileExists("assets/textures/noise.png")) {
      Image noiseImg = GenImagePerlinNoise(256, 256, 0, 0, 16.0f);
      ExportImage(noiseImg, "assets/textures/noise.png");
      UnloadImage(noiseImg);
  }
  if (!FileExists("assets/textures/flare.png")) {
      Image flareImg = GenImageGradientRadial(128, 128, 0.0f, WHITE, BLANK);
      ExportImage(flareImg, "assets/textures/flare.png");
      UnloadImage(flareImg);
  }
  if (!FileExists("assets/textures/crack.png")) {
      Image crackImg = GenImageCellular(256, 256, 32);
      ExportImage(crackImg, "assets/textures/crack.png");
      UnloadImage(crackImg);
  }
  if (!FileExists("assets/textures/water_caustics.png")) {
      Image causticsImg = GenImageCellular(256, 256, 16);
      ExportImage(causticsImg, "assets/textures/water_caustics.png");
      UnloadImage(causticsImg);
  }

  // -----------------------------------------------------------------
  // KHỞI TẠO CÁC HỆ THỐNG ĐỒ HỌA VFX
  // -----------------------------------------------------------------
  InitParticleSystem();
  Shader defaultTrailShader =
      LoadShader(0, FileExists("skills/metal/metal_projectile/metal.fs") ? "skills/metal/metal_projectile/metal.fs" : NULL);
  InitTrailSystem(defaultTrailShader);
  VFXLight_Init();
  DecalSystem_Init();
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

    // =========================================================================
    // MỚI: TOÀN BỘ LOGIC NHẬN PHÍM T ĐỂ CHẠY HIỆU ỨNG TEST ĐÃ ĐƯỢC ĐẨY VÀO HÀM
    // NÀY[cite: 11]
    // =========================================================================
    VFXTest_UpdateAndHandleInput(player.position, testAtlasTex,
                                 globalParticleTex);

    UpdateSkillManager(dt, enemy.position, 35.0f);
    UpdateParticles(dt);
    UpdateTrailSystem(dt);
    VFXLight_Update(dt);
    DecalSystem_Update(dt);
    ScreenDistort_Update(dt);

    BeginDrawing();

    ScreenDistort_Begin();
    ClearBackground(GetColor(0x111111FF));

    MyBeginMode3D(camera);
    DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    DrawSkillManagerWorld3D();

    // Vẽ Decal hệ thống sát sàn đấu
    DecalSystem_Draw();

    // =========================================================================
    // MỚI: TOÀN BỘ PHẦN TRUY XUẤT VÀ VẼ KHỐI CẦU DEBUG LIGHT ĐÃ ĐƯỢC BỐC SANG
    // ĐÂY
    // =========================================================================
    VFXTest_DrawDebugLights3D();

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

    // =========================================================================
    // MỚI: IN THÔNG TIN TEXT DEBUG LÊN HUD CŨNG ĐƯỢC QUẢN LÝ TẬP TRUNG
    // =========================================================================
    VFXTest_DrawHUD();

    DrawText("Phím P: Đổi chế độ quái | Click trái: Tung chiêu | X: Khinh công "
             "| Phím T: Test Light/Gradient/Anim",
             10, 670, 20, LIGHTGRAY);
    EndDrawing();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  UnloadTrailSystem();
  DecalSystem_Unload();
  ScreenDistort_Unload();
  UnloadSkillManager();
  CloseWindow();

  return 0;
}
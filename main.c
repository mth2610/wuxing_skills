#include "particle_system.h" // MỚI: Thêm thư viện quản lý hạt toàn cục
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "sandbox_core.h"
#include "skill_manager.h"
#include "sword_rain_skill.h"
#include "ui_panel.h"
#include <math.h>
#include <stdio.h>

// Biến camera toàn cục
Camera3D camera = {0};

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

  // -----------------------------------------------------------------
  // KHỞI TẠO HỆ THỐNG HẠT TOÀN CỤC & TẠO TEXTURE DÙNG CHUNG
  // -----------------------------------------------------------------
  InitParticleSystem();
  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  UnloadImage(img); // Giải phóng bộ nhớ RAM của Image sau khi đã chuyển lên
                    // VRAM thành Texture

  InitSkillManager(screenWidth, screenHeight);
  RegisterStaticOccluder((Vector3){400.0f, 0.0f, 320.0f}, 25.0f, 62.5f);
  RegisterStaticOccluder((Vector3){800.0f, 0.0f, 520.0f}, 30.0f, 75.0f);
  RegisterStaticOccluder((Vector3){600.0f, 0.0f, 260.0f}, 20.0f, 50.0f);
  InitSwordRainSkill();
  InitUIPanel();

  PlayerEntity player;
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

  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    float dt = GetFrameTime();

    UpdateUIPanel(GetMousePosition(), &uiState);

    Vector3 mouseTarget3D = {0};
    UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      CastSkill(uiState.activeSkillIndex, player.position, mouseTarget3D,
                uiState.currentParams);
    }

    UpdateSkillManager(dt, enemy.position, 35.0f);

    // MỚI: Cập nhật vật lý (Trọng lực, lực cản, nhiễu loạn) cho TOÀN BỘ hạt
    // đang bay
    UpdateParticles(dt);

    BeginDrawing();
    ClearBackground(GetColor(0x111111FF));

    MyBeginMode3D(camera);
    DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    DrawSkillManagerWorld3D();

    // -------------------------------------------------------------
    // VẼ TẬP TRUNG TOÀN BỘ HẠT HIỆU ỨNG (BATCHED DRAWING PASS)
    // -------------------------------------------------------------
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    DrawParticles(camera, globalParticleTex);

    EndBlendMode();
    rlEnableDepthMask();
    // -------------------------------------------------------------

    MyEndMode3D();

    DrawSkillManagerOverlay();

    Vector2 enemyScreenHead = GetWorldToScreen(
        (Vector3){enemy.position.x, enemy.position.y + 55.0f, enemy.position.z},
        camera);
    DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
             WHITE);

    DrawUIPanel(&uiState);

    DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);
    DrawText("Phím P: Đổi chế độ quái | Click trái: Tung chiêu | X: Khinh công",
             10, 670, 20, LIGHTGRAY);
    EndDrawing();
  }

  // Giải phóng tài nguyên hệ thống
  UnloadTexture(
      globalParticleTex); // MỚI: Giải phóng texture dùng chung toàn cục
  UnloadParticleSystem(); // Giải phóng particle system (CPU array hoặc GPU SSBO)
  UnloadSkillManager();
  CloseWindow();

  return 0;
}
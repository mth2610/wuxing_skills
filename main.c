#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/post_fx.h"
#include "sandbox/sandbox_core.h"
#include "core/screen_distort.h"
#include "sandbox/skill_debugger.h"
#include "core/skill_manager.h"
#include "core/trail_system.h"
#include "sandbox/ui_panel.h"
#include "core/vfx_light.h"
#include "sandbox/vfx_test.h" // MỚI: Chỉ giữ duy nhất file test này để điều phối
#include "core/resource_manager.h"
#include "core/skill_helper.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include "environment/environment_system.h"
#include "core/map_manager.h"
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

  ResourceManager_Init();
  InitSkillManager(screenWidth, screenHeight);
  DamageVolume_Init();
  EmitterSystem_Init();
  RegisterStaticOccluder((Vector3){400.0f, 0.0f, 320.0f}, 25.0f, 62.5f);
  RegisterStaticOccluder((Vector3){800.0f, 0.0f, 520.0f}, 30.0f, 75.0f);
  RegisterStaticOccluder((Vector3){600.0f, 0.0f, 260.0f}, 20.0f, 50.0f);
  InitUIPanel();
  SkillDebugger_Init();
  Environment_Init();
  MapManager_Init();

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
  uiState.isPanelOpen = true;

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

  bool g_gamePaused = false;
  bool g_stepNextFrame = false;
  bool g_slowMotion = false;

  while (!WindowShouldClose()) {
    float dt = GetFrameTime();

    // -------------------------------------------------------------------------
    // TIME CONTROL FOR DEBUGGING / SCREENSHOTTING
    // -------------------------------------------------------------------------
    if (IsKeyPressed(KEY_V)) g_gamePaused = !g_gamePaused;
    if (IsKeyPressed(KEY_B)) g_stepNextFrame = true;
    if (IsKeyPressed(KEY_M)) g_slowMotion = !g_slowMotion;
    if (IsKeyPressed(KEY_K)) {
        int nextMap = (MapManager_GetActiveIndex() + 1) % MapManager_GetCount();
        MapManager_SetActiveIndex(nextMap);
    }

    if (g_gamePaused) {
        if (g_stepNextFrame) {
            dt = 1.0f / 60.0f; // Force exactly 1 frame of time
            g_stepNextFrame = false;
        } else {
            dt = 0.0f;
        }
    } else if (g_slowMotion) {
        dt *= 0.1f; // Slow motion 10% speed
    }
    
    SkillDebugger_CheckInput();
    if (g_isDebuggerCapturing) {
        dt = 0.0f; // Freeze time during automated screenshot capture
    }

    UpdateUIPanel(GetMousePosition(), &uiState);

    Vector3 mouseTarget3D = {0};
    UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);

    CameraFX_Update(&camera, dt);

    static bool isDragging = false;
    static int pathCount = 0;
    static Vector3 pathPoints[32];

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      isDragging = true;
      pathCount = 1;
      pathPoints[0] = mouseTarget3D;
    }

    if (isDragging) {
      // Add points if distance > 5.0f
      if (pathCount < 32) {
        if (Vector3Distance(mouseTarget3D, pathPoints[pathCount - 1]) > 5.0f) {
          pathPoints[pathCount++] = mouseTarget3D;
        }
      }

      // Draw the drag path for visual feedback
      for (int i = 0; i < pathCount - 1; i++) {
        DrawLine3D(pathPoints[i], pathPoints[i + 1], GREEN);
      }

      if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        isDragging = false;
        uiState.currentParams.pathPointCount = pathCount;
        for (int i = 0; i < pathCount; i++) {
          uiState.currentParams.pathPoints[i] = pathPoints[i];
        }
        CastSkill(uiState.activeSkillIndex, player.position, mouseTarget3D,
                  uiState.currentParams);
      }
    }

    // =========================================================================
    // MỚI: TOÀN BỘ LOGIC NHẬN PHÍM T ĐỂ CHẠY HIỆU ỨNG TEST ĐÃ ĐƯỢC ĐẨY VÀO HÀM
    // NÀY[cite: 11]
    // =========================================================================
    VFXTest_UpdateAndHandleInput(player.position, testAtlasTex,
                                 globalParticleTex);

    UpdateSkillManager(dt, enemy.position, 35.0f);
    DamageVolume_Update(dt);
    EmitterSystem_Update(dt);
    UpdateParticles(dt);
    UpdateTrailSystem(dt);
    VFXLight_Update(dt);
    DecalSystem_Update(dt);
    ScreenDistort_Update(dt);
    Environment_Update(dt);
    MapManager_Update(dt);

    SkillDebugger_PreRender();

    BeginDrawing();

    ScreenDistort_Begin();
    if (g_isDebuggerCapturing) {
        ClearBackground(BLACK);
    } else {
        ClearBackground(GetColor(0x111111FF));
    }

    MyBeginMode3D(camera);
    MapManager_DrawActive();
    if (!g_isDebuggerCapturing) {
        DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    }

    // Vẽ Decal hệ thống sát sàn đấu
    if (!g_debugHideDecals) {
        DecalSystem_Draw();
    }

    if (!g_debugHideMeshes) {
        DrawSkillManagerWorld3D();
    }

    // =========================================================================
    // MỚI: TOÀN BỘ PHẦN TRUY XUẤT VÀ VẼ KHỐI CẦU DEBUG LIGHT ĐÃ ĐƯỢC BỐC SANG
    // ĐÂY
    // =========================================================================
    // VFXTest_DrawDebugLights3D();

    if (!g_debugHideTrails) {
        DrawTrailEntities(camera);
    }

    if (!g_debugHideParticles) {
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawParticles(camera, globalParticleTex);
        EndBlendMode();
        rlEnableDepthMask();
    }

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

    if (!g_isDebuggerCapturing) {
        DrawUIPanel(&uiState);

        DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);

        // =========================================================================
        // MỚI: IN THÔNG TIN TEXT DEBUG LÊN HUD CŨNG ĐƯỢC QUẢN LÝ TẬP TRUNG
        // =========================================================================
        VFXTest_DrawHUD();
        DrawSandboxHUD();

        DrawText("Phím P: Đổi chế độ quái | Click trái: Tung chiêu | X: Khinh công", 10, 650, 20, LIGHTGRAY);
        DrawText("V: Pause | B: Step Forward | N: Screenshot | M: Slow Motion (10%) | K: Đổi Map", 10, 675, 20, ORANGE);
    }
             
    SkillDebugger_PostRender(uiState.activeSkillIndex, player.position, mouseTarget3D);

    EndDrawing();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  UnloadTrailSystem();
  DecalSystem_Unload();
  ScreenDistort_Unload();
  UnloadSkillManager();
  DamageVolume_Unload();
  EmitterSystem_Unload();
  ResourceManager_Unload();
  MapManager_Unload();
  CloseWindow();

  return 0;
}
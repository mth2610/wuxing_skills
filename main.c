#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/metaball_fx.h"
#include "core/particle_system.h"
#include "compute/gpu_particle_system.h"
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
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/tuning.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "environment/environment_system.h"
#include "core/map_manager.h"
#include "skills/taiji/core_test/core_test_skill.h"
#include "sandbox/auto_test.h"
#include "sandbox/visual_verify.h"
#include "sandbox/pool_stats.h"
#include "core/status_vfx.h"
#include "core/afterimage.h"
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
    // near/far real-world-scaled (root CLAUDE.md "Standard coordinates &
    // scale") — NOT a straight ÷100 of the old 10.0/15000.0. Empirically,
    // near values below ~1.0 render a fully blank scene in this project's
    // rlFrustum() setup (bisected via autotest screenshots; root cause not
    // identified — suspected precision issue at very small frustum extents,
    // not a near/far *ratio* problem since the same ratio at 0.1/150 also
    // failed). sandbox_core.c's g_camDist is clamped with margin above this
    // near plane — keep core/screen_distort.c's SOFT_PARTICLE_SCENE_NEAR/FAR
    // in sync if this ever changes.
    double top = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    rlFrustum(-right, right, -top, top, 1.0, 1000.0);
  } else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
    double top = camera.fovy / 2.0;
    double right = top * aspect;
    rlOrtho(-right, right, -top, top, 0.0001, 150.0);
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

// Smoke test for the autotest harness itself (sandbox/auto_test.h) — proves
// the whole pipeline (env var -> headless window -> fixed-dt frames ->
// registration -> step -> log -> summary -> exit code) works end to end.
static AutoTestResult AutoTest_SmokeStep(int frameInCase, char *outReason, int outReasonSize) {
  (void)frameInCase;
  return AutoTest_ExpectTrue(GetRegisteredSkillCount() > 0,
                             "skill manager has registered skills",
                             outReason, outReasonSize)
             ? AUTOTEST_PASS
             : AUTOTEST_FAIL;
}

int main(int argc, char **argv) {
  // Widened/heightened from 1200x700 so the sandbox tuning panel
  // (sandbox/ui_panel.c) has room for multi-column tunable layouts as skills
  // gain more sandbox-tunable parameters (CORE_ISSUES.md Item 34 follow-up).
  const int screenWidth = 1280;
  const int screenHeight = 720;

  // --render-vfx <index> [--warmup <frames>] [--out <path>]
  // Renders NEWFX tab entry <index> headlessly, saves PNG, exits.
  bool        renderVFXMode   = false;
  int         renderVFXIndex  = 0;
  int         renderVFXWarmup = 90;
  const char *renderVFXOut    = "autotest_output/vfx_eval.png";
  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--render-vfx") == 0 && i + 1 < argc)
          { renderVFXIndex = atoi(argv[++i]); renderVFXMode = true; }
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
          renderVFXWarmup = atoi(argv[++i]);
      else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
          renderVFXOut = argv[++i];
  }

  bool autoTestMode     = AutoTest_IsEnabled();
  bool visualVerifyMode = VisualVerify_IsEnabled();
  if (autoTestMode || visualVerifyMode || renderVFXMode) {
      // Off-screen SetWindowPosition was tried first, but produced the exact
      // same GetWorldToScreen() output as FLAG_WINDOW_HIDDEN below (proving
      // the odd coordinates aren't a window-position artifact) — kept
      // FLAG_WINDOW_HIDDEN since it doesn't touch window placement at all,
      // closer to normal interactive behavior.
      SetConfigFlags(FLAG_WINDOW_HIDDEN);
  }
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");
  rlSetClipPlanes(0.001f, 150.0f);

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
  GpuParticleSystem_Init();
  Shader defaultTrailShader = LoadShader(0, "core/shaders/trail_glow.fs");
  InitTrailSystem(defaultTrailShader);
  VFXLight_Init();
  DecalSystem_Init();
  ScreenDistort_Init(screenWidth, screenHeight);
  PostFX_Init(screenWidth, screenHeight);
  MetaballFX_Init(screenWidth, screenHeight);

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  Image trailImg = {
      .data = MemAlloc(64 * sizeof(Color)),
      .width = 64,
      .height = 1,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
  };
  Color *trailPixels = (Color *)trailImg.data;
  for(int i=0; i<64; i++) {
      float u = i / 63.0f;
      float dist = fabsf(u - 0.5f) * 2.0f;
      float alpha = 1.0f - dist;
      trailPixels[i] = (Color){255, 255, 255, (unsigned char)(255 * alpha)};
  }
  Texture2D globalTrailTex = LoadTextureFromImage(trailImg);
  UnloadImage(trailImg);
  TrailSystem_SetGlobalTexture(globalTrailTex);
  UnloadImage(img);

  Image atlasImg = GenImageColor(128, 128, BLANK);
  ImageDrawCircle(&atlasImg, 32, 32, 20, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 12, 12, 40, 40, WHITE);
  ImageDrawCircle(&atlasImg, 32, 96, 12, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 16, 64 + 16, 32, 32, WHITE);
  Texture2D testAtlasTex = LoadTextureFromImage(atlasImg);
  UnloadImage(atlasImg);

  ResourceManager_Init();
  Tuning_Init("tuning.cfg");
  InitSkillManager(screenWidth, screenHeight);
  if (autoTestMode) {
      AutoTest_Register("smoke_skill_manager_init", AutoTest_SmokeStep, 5);
  }
  DamageVolume_Init();
  EmitterSystem_Init();
  Afterimage_Init();
  PoolStats_Init();
  RegisterStaticOccluder((Vector3){4.0f, 0.0f, 3.2f}, 0.25f, 0.625f);
  RegisterStaticOccluder((Vector3){8.0f, 0.0f, 5.2f}, 0.3f, 0.75f);
  RegisterStaticOccluder((Vector3){6.0f, 0.0f, 2.6f}, 0.2f, 0.5f);
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
  uiState.isPanelOpen = false;

  PostFXConfig postFXConfig = {.bloomEnabled = true,
                               .bloomThreshold = 0.5f,
                               .bloomIntensity = 2.0f,
                               .chromaticEnabled = true,
                               .chromaticStrength = 0.15f,
                               .vignetteEnabled = true,
                               .vignetteRadius = 0.85f,
                               .vignetteSoftness = 0.45f,
                               .colorGradeEnabled = true,
                               .contrast = 1.05f,
                               .saturation = 1.15f,
                               .colorTint = {1.0f, 1.0f, 1.0f}};

  if (visualVerifyMode) {
      VisualVerify_Init(Skill_GetIndexByName(VisualVerify_GetSkillName()));
  }

  if (!autoTestMode && !visualVerifyMode) SetTargetFPS(60);

  bool g_gamePaused = false;
  bool g_stepNextFrame = false;
  bool g_slowMotion = false;

  float g_totalElapsed = 0.0f;

  typedef enum {
      SCREEN_MAIN_MENU,
      SCREEN_SKILL_SANDBOX,
      SCREEN_VFX_TESTER
  } GameScreen;
  GameScreen currentScreen = SCREEN_MAIN_MENU;
  int renderVFXFrame = 0;
  if (renderVFXMode) {
      currentScreen    = SCREEN_VFX_TESTER;
      player.position  = (Vector3){6.0f, 0.0f, 4.4f}; // arena center
      VFXTest_SetRenderTarget(renderVFXIndex, player.position);
  }
  while (autoTestMode     ? !AutoTest_IsFinished()      :
         visualVerifyMode ? !VisualVerify_IsFinished()  :
         renderVFXMode    ? (renderVFXFrame <= renderVFXWarmup) :
         !WindowShouldClose()) {
    float dt = (autoTestMode || visualVerifyMode || renderVFXMode) ? (1.0f / 60.0f) : TimeFX_Apply(GetFrameTime());
    g_totalElapsed += dt;

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

    if (currentScreen == SCREEN_MAIN_MENU) {
        Vector2 mousePos = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        Rectangle btnSandbox = { sw/2 - 150, sh/2 - 60, 300, 50 };
        Rectangle btnVFX = { sw/2 - 150, sh/2 + 20, 300, 50 };
        
        if (CheckCollisionPointRec(mousePos, btnSandbox) && clicked) {
            currentScreen = SCREEN_SKILL_SANDBOX;
        }
        if (CheckCollisionPointRec(mousePos, btnVFX) && clicked) {
            currentScreen = SCREEN_VFX_TESTER;
        }
        
        BeginDrawing();
        ClearBackground(DARKGRAY);
        
        const char* title = "WUXING SKILLS TESTBED";
        int titleW = MeasureText(title, 30);
        DrawText(title, sw/2 - titleW/2, sh/2 - 150, 30, WHITE);
        
        DrawRectangleRounded(btnSandbox, 0.2f, 10, CheckCollisionPointRec(mousePos, btnSandbox) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnSandbox, 0.2f, 10, WHITE);
        DrawText("1. ENTER SKILL SANDBOX", (int)btnSandbox.x + 30, (int)btnSandbox.y + 15, 20, BLACK);
        
        DrawRectangleRounded(btnVFX, 0.2f, 10, CheckCollisionPointRec(mousePos, btnVFX) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnVFX, 0.2f, 10, WHITE);
        DrawText("2. ENTER VFX PREFAB TESTER", (int)btnVFX.x + 10, (int)btnVFX.y + 15, 20, BLACK);
        
        EndDrawing();
        continue;
    }

    Vector3 mouseTarget3D = {0};

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        UpdateUIPanel(GetMousePosition(), &uiState);
        if (uiState.requestedBackToMenu) currentScreen = SCREEN_MAIN_MENU;

        UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);
        CameraFX_Update(&camera, dt);
    } else if (currentScreen == SCREEN_VFX_TESTER) {
        static float vfxCameraAngle = 0.0f;
        float speed = 20.0f;
        float s = sinf(vfxCameraAngle);
        float c = cosf(vfxCameraAngle);

        if (IsKeyDown(KEY_W)) { player.position.x -= s * speed * dt; player.position.z -= c * speed * dt; }
        if (IsKeyDown(KEY_S)) { player.position.x += s * speed * dt; player.position.z += c * speed * dt; }
        if (IsKeyDown(KEY_A)) { player.position.x -= c * speed * dt; player.position.z += s * speed * dt; }
        if (IsKeyDown(KEY_D)) { player.position.x += c * speed * dt; player.position.z -= s * speed * dt; }

        if (IsKeyDown(KEY_Q)) vfxCameraAngle -= 2.5f * dt;
        if (IsKeyDown(KEY_E)) vfxCameraAngle += 2.5f * dt;

        static float vfxCamDist = 8.4f;
        vfxCamDist -= GetMouseWheelMove() * 0.5f;
        if (vfxCamDist < 2.0f) vfxCamDist = 2.0f;
        if (vfxCamDist > 30.0f) vfxCamDist = 30.0f;

        camera.target = (Vector3){ player.position.x, player.position.y + 0.2f, player.position.z };
        camera.position = (Vector3){ 
            player.position.x + sinf(vfxCameraAngle) * vfxCamDist, 
            player.position.y + vfxCamDist * 0.8f, 
            player.position.z + cosf(vfxCameraAngle) * vfxCamDist
        };

        Ray mouseRay = GetScreenToWorldRay(GetMousePosition(), camera);
        float t = -mouseRay.position.y / mouseRay.direction.y;
        mouseTarget3D = (Vector3){
            mouseRay.position.x + mouseRay.direction.x * t,
            0.0f,
            mouseRay.position.z + mouseRay.direction.z * t
        };

        if (VFXTest_UpdateAndHandleInput(player.position, mouseTarget3D, testAtlasTex, globalParticleTex)) {
            currentScreen = SCREEN_MAIN_MENU;
        }
        CameraFX_Update(&camera, dt);
    }

    static bool isDragging = false;
    static int pathCount = 0;
    static Vector3 pathPoints[32];

    if (currentScreen == SCREEN_SKILL_SANDBOX && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
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
        CastSkill(uiState.activeSkillIndex, player.agentId, player.position,
                  mouseTarget3D, uiState.currentParams);
      }
    }

    Tuning_Update();
    UpdateSkillManager(dt, enemy.position, 0.35f);
    DamageVolume_Update(dt);
    VFX_Compose_Update(dt);
    EmitterSystem_Update(dt);
    StatusVFX_Update(dt);
    Afterimage_Update(dt);
    UpdateParticles(dt);
    GpuParticleSystem_Update(dt);
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
    if (!g_isDebuggerCapturing && currentScreen == SCREEN_SKILL_SANDBOX) {
        DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    }

    // Vẽ Decal hệ thống sát sàn đấu
    if (!g_debugHideDecals) {
        DecalSystem_SetCamera(camera);
        DecalSystem_Draw();
    }

    if (!g_debugHideMeshes) {
        DrawSkillManagerWorld3D();
    }

    // =========================================================================
    // MỚI: TOÀN BỘ PHẦN TRUY XUẤT VÀ VẼ KHỐI CẦU DEBUG LIGHT ĐÃ ĐƯỢC BỐC SANG ĐÂY
    // + ĐƯỢC BỔ SUNG THÊM VIỆC VẼ MESH TỪ PREFAB TESTER
    // =========================================================================
    if (currentScreen == SCREEN_VFX_TESTER) {
        VFXTest_Draw3D();
        
        // Draw player character (same as sandbox)
        Environment_DrawSmartShadow(player.position, ENV_SHAPE_SPHERE, 0.25f, 0.25f);
        DrawCharacter3D(player.position, 0.25f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF), true, mouseTarget3D);
    }

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        VFX_Compose_Draw3D(camera);
    }
    Afterimage_Draw();

    if (!g_debugHideTrails) {
        DrawTrailEntities(camera);
    }

    if (!g_debugHideParticles) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawParticles(camera, globalParticleTex);
        GpuParticleSystem_Draw(camera, globalParticleTex);
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    MyEndMode3D();
    ScreenDistort_End();
    ScreenDistort_SnapshotDepth(); // soft particles: snapshot this frame's depth for next frame's sampling

    PostFX_Begin();
    ClearBackground(BLACK);
    ScreenDistort_Draw(camera);
    PostFX_End();

    ClearBackground(BLACK);
    PostFX_Draw(&postFXConfig);

    // Metaballs: composite directly onto the screen, after post-process —
    // must run outside PostFX_Begin/End (BeginTextureMode can't nest) and
    // after PostFX_Draw (which would otherwise overwrite it).
    MetaballFX_DrawRegistered(camera, ELEMENT_COLOR_WATER, 0.3f, 0.12f);

    DrawSkillManagerOverlay();
    DrawCoreTestSkillDebugHUD();
    PoolStats_DrawOverlay(); // CORE_ISSUES.md Item 3 test — on-screen depth readback (press L)

    Vector2 enemyScreenHead = GetWorldToScreen(
        (Vector3){enemy.position.x, enemy.position.y + 0.55f, enemy.position.z},
        camera);
    DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
             WHITE);

    if (!g_isDebuggerCapturing) {
        if (currentScreen == SCREEN_SKILL_SANDBOX) {
            DrawUIPanel(&uiState);
            DrawSandboxTouchControls(&player);
            if (uiState.isPanelOpen) {
                DrawSandboxHUD();
            }
        } else if (currentScreen == SCREEN_VFX_TESTER) {
            VFXTest_DrawHUD();
        }

        DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);
    }
             
    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        SkillDebugger_PostRender(uiState.activeSkillIndex, player.position, mouseTarget3D);
    }

    EndDrawing();

    if (autoTestMode)     AutoTest_RunFrame();
    if (visualVerifyMode) VisualVerify_RunFrame(g_totalElapsed);
    if (renderVFXMode) {
        renderVFXFrame++;
        if (renderVFXFrame >= renderVFXWarmup) {
            if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
            Image img = LoadImageFromScreen();
            ExportImage(img, renderVFXOut);
            UnloadImage(img);
            break;
        }
    }
  }

  int exitCode = 0;
  if (autoTestMode) {
      AutoTest_PrintSummary();
      exitCode = AutoTest_GetExitCode();
  }
  if (visualVerifyMode) {
      exitCode = VisualVerify_GetExitCode();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  GpuParticleSystem_Unload();
  UnloadTrailSystem();
  DecalSystem_Unload();
  ScreenDistort_Unload();
  MetaballFX_Unload();
  UnloadSkillManager();
  DamageVolume_Unload();
  EmitterSystem_Unload();
  ResourceManager_Unload();
  MapManager_Unload();
  CloseWindow();

  return exitCode;
}
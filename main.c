#include "core/camera_fx.h"
#include "core/particle_system.h" // Thư viện quản lý hạt toàn cục (Compute Shader)
#include "core/post_fx.h"
#include "core/sandbox_core.h"
#include "core/screen_distort.h"
#include "core/skill_manager.h"
#include "core/trail_system.h" // MỚI: Thư viện quản lý dải khí, kiếm bay (Smart Projectiles)
#include "core/ui_panel.h"
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

int main(void) {
  const int screenWidth = 1200;
  const int screenHeight = 700;
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");
  rlSetClipPlanes(0.1f, 15000.0f);
  // -----------------------------------------------------------------
  // KHỞI TẠO HỆ THỐNG VFX (HẠT VÀ DẢI NĂNG LƯỢNG)
  // -----------------------------------------------------------------
  InitParticleSystem();
  Shader defaultTrailShader = LoadShader(
      0, "skills/metal/metal_projectile/metal.fs"); // shader fallback chung
  InitTrailSystem(defaultTrailShader); // MỚI: Khởi tạo hệ thống Trail System
  ScreenDistort_Init(screenWidth,
                     screenHeight); // MỚI: Khởi tạo Screen Distortion pass
  PostFX_Init(screenWidth, screenHeight); // MỚI: Khởi tạo Post-Processing Stack

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  UnloadImage(img);

  // Tạo texture atlas 2x2 cho việc test hoạt cảnh (SpriteAnim) với các hình thù
  // khác nhau
  Image atlasImg = GenImageColor(128, 128, BLANK);
  ImageDrawCircle(&atlasImg, 32, 32, 20, WHITE); // Frame 0: Hình tròn lớn
  ImageDrawRectangle(&atlasImg, 64 + 12, 12, 40, 40,
                     WHITE);                     // Frame 1: Hình vuông lớn
  ImageDrawCircle(&atlasImg, 32, 96, 12, WHITE); // Frame 2: Hình tròn nhỏ
  ImageDrawRectangle(&atlasImg, 64 + 16, 64 + 16, 32, 32,
                     WHITE); // Frame 3: Hình vuông nhỏ
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

  // Cấu hình các hiệu ứng xử lý hậu kỳ (Post-Processing)
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

  PostFX_Init(screenWidth, screenHeight);

  while (!WindowShouldClose()) {
    float dt = GetFrameTime();

    UpdateUIPanel(GetMousePosition(), &uiState);

    Vector3 mouseTarget3D = {0};
    UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);

    // Cập nhật rung chấn camera
    CameraFX_Update(&camera, dt);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      CastSkill(uiState.activeSkillIndex, player.position, mouseTarget3D,
                uiState.currentParams);
    }

    // TEST FEATURE: Nhấn phím T để test dải màu (ColorGradient) và hoạt cảnh
    // hạt (SpriteAnim)
    if (IsKeyPressed(KEY_T)) {
      CameraFX_Shake(
          0.5f); // Kích hoạt rung lắc camera test với lực vừa phải (0.5)
      ScreenDistort_AddSource(
          player.position, 120.0f, 0.8f, 1.2f,
          250.0f); // MỚI: Thêm sóng kích nổ test tại vị trí người chơi
      // 1. Khởi tạo dải màu Gradient: Đỏ -> Cam -> Vàng -> Xanh lá -> Xanh
      // dương
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

      // 2. Khởi tạo SpriteAnim giả lập lưới 2x2
      static SpriteAnim anim;
      static bool animInit = false;
      if (!animInit) {
        SpriteAnim_Init(&anim, 2, 2, 4, 8.0f, ANIM_LOOP);
        animInit = true;
      }

      // Spawn một hạt dùng gradient ở phía BÊN TRÁI player, bắn sang trái
      ParticleConfig pConfig = {0};
      pConfig.position =
          Vector3Add(player.position, (Vector3){-75.0f, 30.0f, 0.0f});
      pConfig.velocity = (Vector3){-30.0f, 50.0f, 0.0f};
      pConfig.radius = 45.0f;
      pConfig.lifetime = 4.0f;
      pConfig.gradient = &g;
      SpawnParticle(pConfig);

      // Spawn một trail dùng gradient và anim ở phía BÊN PHẢI player, bắn sang
      // phải Gán texture là testAtlasTex để đầu đạn thay đổi hình dạng (Circle
      // -> Square -> Circle -> Square)
      TrailConfig tConfig = {0};
      tConfig.type = TRAIL_TYPE_PROJECTILE;
      tConfig.pos = Vector3Add(player.position, (Vector3){75.0f, 30.0f, 0.0f});
      tConfig.vel = (Vector3){
          220.0f, 0.0f, 0.0f}; // bay nhanh hơn để kéo dãn các node lịch sử
      tConfig.len = 50.0f; // chiều dài đầu đạn
      tConfig.thick = 6.0f; // độ dày của đuôi (giảm xuống để tránh giao nhau tự
                            // thân tạo hình bướm)
      tConfig.trailLength = 80.0f;
      tConfig.life = 4.0f;
      tConfig.gradient = &g;
      tConfig.spriteAnim = &anim; // dùng anim cho đầu quad
      tConfig.tex = testAtlasTex; // dùng atlas có các hình dạng tròn/vuông để
                                  // thấy rõ hoạt cảnh
      SpawnTrailEntity(tConfig);
    }

    UpdateSkillManager(dt, enemy.position, 35.0f);

    // Cập nhật hệ thống Hạt (Tia lửa, vụ nổ)
    UpdateParticles(dt);

    // MỚI: Cập nhật hệ thống Trail (Kiếm bay, dải khí, cổng phép thuật)
    UpdateTrailSystem(dt);

    // MỚI: Cập nhật các nguồn chấn động sóng kích biến dạng màn hình
    ScreenDistort_Update(dt);

    // BẮT BUỘC: Gọi BeginDrawing() đầu tiên của khung hình trước mọi lệnh
    // vẽ/Offscreen RenderTexture
    BeginDrawing();

    // 1. Vẽ thế giới vào RenderTexture phụ của ScreenDistort để bẻ cong hình
    // (KHÔNG ĐƯỢC NEST FBO)
    ScreenDistort_Begin();
    ClearBackground(GetColor(0x111111FF));

    MyBeginMode3D(camera);
    DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    DrawSkillManagerWorld3D();

    // VẼ TẬP TRUNG TOÀN BỘ VFX HIỆU ỨNG (BATCHED DRAWING PASS)
    DrawTrailEntities(camera);

    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawParticles(camera, globalParticleTex);
    EndBlendMode();
    rlEnableDepthMask();

    MyEndMode3D();
    ScreenDistort_End();

    // 2. Bắt đầu pass hậu kỳ chính: Vẽ tấm bình phong 3D đã méo hình vào buffer
    // chính của PostFX
    PostFX_Begin();
    ClearBackground(BLACK);
    ScreenDistort_Draw(camera);
    PostFX_End();

    // 2. Vẽ kết quả chuỗi hậu kỳ lên màn hình chính
    ClearBackground(BLACK);

    PostFX_Draw(&postFXConfig); // Áp dụng Bloom + CA + Vignette + Color Grade

    DrawSkillManagerOverlay();

    Vector2 enemyScreenHead = GetWorldToScreen(
        (Vector3){enemy.position.x, enemy.position.y + 55.0f, enemy.position.z},
        camera);
    DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
             WHITE);

    DrawUIPanel(&uiState);

    DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);
    DrawText("Phím P: Đổi chế độ quái | Click trái: Tung chiêu | X: Khinh công "
             "| Phím T: Test Gradient/Anim",
             10, 670, 20, LIGHTGRAY);
    EndDrawing();
  }

  // Giải phóng tài nguyên hệ thống
  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  UnloadTrailSystem(); // MỚI: Giải phóng Trail System
  ScreenDistort_Unload(); // MỚI: Giải phóng tài nguyên biến dạng màn hình
  UnloadSkillManager();
  CloseWindow();

  return 0;
}
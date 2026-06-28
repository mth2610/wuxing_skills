#include "core/screen_distort.h"
#include <string.h>

static RenderTexture2D renderTex;
static Shader distortShader;
static DistortionSource sources[MAX_DISTORTION_SOURCES];
static int activeSourcesCount = 0;

// Uniform locations
static int centersLoc;
static int radiiLoc;
static int strengthsLoc;
static int progressLoc;
static int countLoc;
static int aspectLoc;

void ScreenDistort_Init(int width, int height) {
  renderTex = LoadRenderTexture(width, height);
  
  distortShader = LoadShader(0, "core/shaders/distortion.fs");
  
  centersLoc = GetShaderLocation(distortShader, "u_centers");
  radiiLoc = GetShaderLocation(distortShader, "u_radii");
  strengthsLoc = GetShaderLocation(distortShader, "u_strengths");
  progressLoc = GetShaderLocation(distortShader, "u_progress");
  countLoc = GetShaderLocation(distortShader, "u_count");
  aspectLoc = GetShaderLocation(distortShader, "u_aspectRatio");
  
  activeSourcesCount = 0;
  memset(sources, 0, sizeof(sources));
}

void ScreenDistort_Unload(void) {
  UnloadRenderTexture(renderTex);
  UnloadShader(distortShader);
}

void ScreenDistort_Begin(void) {
  BeginTextureMode(renderTex);
}

void ScreenDistort_End(void) {
  EndTextureMode();
}

void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed) {
  if (activeSourcesCount >= MAX_DISTORTION_SOURCES) {
    // Tìm phần tử có lifetime thấp nhất để ghi đè lên nếu hết slot tự do
    int minIdx = 0;
    float minLife = sources[0].lifetime;
    for (int i = 1; i < MAX_DISTORTION_SOURCES; i++) {
      if (sources[i].lifetime < minLife) {
        minLife = sources[i].lifetime;
        minIdx = i;
      }
    }
    sources[minIdx] = (DistortionSource){ worldPos, radius, strength, lifetime, lifetime, speed };
    return;
  }
  
  sources[activeSourcesCount] = (DistortionSource){ worldPos, radius, strength, lifetime, lifetime, speed };
  activeSourcesCount++;
}

void ScreenDistort_Update(float dt) {
  for (int i = activeSourcesCount - 1; i >= 0; i--) {
    sources[i].lifetime -= dt;
    if (sources[i].lifetime <= 0.0f) {
      // Dịch chuyển phần tử cuối ghi đè lên phần tử chết (Dense array)
      sources[i] = sources[activeSourcesCount - 1];
      activeSourcesCount--;
    }
  }
}

void ScreenDistort_Draw(Camera3D camera) {
  float screenWidth = (float)GetScreenWidth();
  float screenHeight = (float)GetScreenHeight();
  float aspect = screenWidth / screenHeight;
  
  // Chuẩn bị dữ liệu uniform
  static Vector2 centers[MAX_DISTORTION_SOURCES];
  static float radii[MAX_DISTORTION_SOURCES];
  static float strengths[MAX_DISTORTION_SOURCES];
  static float progress[MAX_DISTORTION_SOURCES];
  
  int validCount = 0;
  for (int i = 0; i < activeSourcesCount; i++) {
    // Chiếu từ world 3D sang toạ độ màn hình 2D
    Vector2 screenPos = GetWorldToScreen(sources[i].worldPos, camera);
    
    // Nếu toạ độ nằm ngoài rìa sâu bên ngoài màn hình, bỏ qua không xử lý
    if (screenPos.x < -200.0f || screenPos.x > screenWidth + 200.0f ||
        screenPos.y < -200.0f || screenPos.y > screenHeight + 200.0f) {
      continue;
    }
    
    // Chuẩn hóa tọa độ màn hình sang khoảng [0.0 .. 1.0]
    // Lật trục Y vì RenderTexture trong OpenGL có gốc y=0 nằm ở góc DƯỚI bên trái
    centers[validCount].x = screenPos.x / screenWidth;
    centers[validCount].y = 1.0f - (screenPos.y / screenHeight);
    
    // Chuẩn hóa bán kính theo chiều rộng màn hình để sóng có độ rộng tròn đều trên UV
    radii[validCount] = sources[i].radius / screenWidth;
    strengths[validCount] = sources[i].strength;
    
    float t = 1.0f - (sources[i].lifetime / sources[i].maxLifetime);
    progress[validCount] = t;
    
    validCount++;
  }
  
  // Gán các giá trị cho Shader Uniforms
  SetShaderValue(distortShader, aspectLoc, &aspect, SHADER_UNIFORM_FLOAT);
  SetShaderValue(distortShader, countLoc, &validCount, SHADER_UNIFORM_INT);
  
  if (validCount > 0) {
    SetShaderValueV(distortShader, centersLoc, centers, SHADER_UNIFORM_VEC2, validCount);
    SetShaderValueV(distortShader, radiiLoc, radii, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, strengthsLoc, strengths, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, progressLoc, progress, SHADER_UNIFORM_FLOAT, validCount);
  }
  
  // Vẽ tấm bình phong fullscreen với texture cảnh nền đã render, áp dụng shader khúc xạ méo UV
  BeginShaderMode(distortShader);
    // Sử dụng chiều cao âm (height = -renderTex.texture.height) để lật trục Y cho hiển thị đúng hướng
    DrawTextureRec(renderTex.texture, (Rectangle){ 0, 0, (float)renderTex.texture.width, -(float)renderTex.texture.height }, (Vector2){ 0, 0 }, WHITE);
  EndShaderMode();
}

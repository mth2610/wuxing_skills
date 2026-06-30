#include "core/screen_distort.h"
#include "rlgl.h"
#include <string.h>

static RenderTexture2D renderTex;
static Shader distortShader;
static DistortionSource sources[MAX_DISTORTION_SOURCES];
static int activeSourcesCount = 0;

// --- Soft particles: previous-frame depth snapshot (see header comment) ---
static RenderTexture2D prevDepthTex;
static Shader depthCopyShader;
static int depthCopyNearLoc;
static int depthCopyFarLoc;

// Uniform locations
static int centersLoc;
static int radiiLoc;
static int strengthsLoc;
static int progressLoc;
static int countLoc;
static int aspectLoc;

// LoadRenderTexture() mặc định gắn depth attachment là RENDERBUFFER (không
// sample được trong shader). Build framebuffer thủ công qua rlgl để depth
// attachment là TEXTURE thật. Color attachment vẫn RGBA8 giống hệt
// LoadRenderTexture() nên không ảnh hưởng pipeline distortion hiện có.
static RenderTexture2D LoadRenderTextureWithDepthTexture(int width, int height) {
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0) {
    rlEnableFramebuffer(target.id);

    target.texture.id =
        rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    target.texture.mipmaps = 1;

    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19; // DEPTH_COMPONENT_24BIT (rlgl internal format id)
    target.depth.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_TEXTURE2D, 0);

    if (!rlFramebufferComplete(target.id)) {
      TraceLog(LOG_WARNING, "ScreenDistort: depth-texture framebuffer incomplete");
    }
    rlDisableFramebuffer();
  }
  return target;
}

// Single-channel 32-bit FLOAT color target (no depth attachment). Used to
// snapshot LINEARIZED scene depth: an 8-bit RGBA copy crushes all far depths
// (near=0.1, far=15000) to 255, making scene vs particle depth
// indistinguishable (soft factor always 0). R32F keeps full precision.
static RenderTexture2D LoadLinearDepthTarget(int width, int height) {
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0) {
    rlEnableFramebuffer(target.id);

    target.texture.id =
        rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    target.texture.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);

    if (!rlFramebufferComplete(target.id)) {
      TraceLog(LOG_WARNING, "ScreenDistort: linear-depth framebuffer incomplete");
    }
    rlDisableFramebuffer();
  }
  // R32F float textures are NOT linear-filterable on many GL3.3 drivers —
  // sampling with the default BILINEAR filter returns 0/undefined. Force
  // POINT (nearest) filtering, and clamp to avoid edge wrap artifacts.
  SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return target;
}

void ScreenDistort_Init(int width, int height) {
  // renderTex needs a sampleable depth texture (real scene depth source for
  // soft particles — see screen_distort.h note). prevDepthTex stores the
  // LINEARIZED snapshot in R32F (see LoadLinearDepthTarget).
  renderTex = LoadRenderTextureWithDepthTexture(width, height);
  prevDepthTex = LoadLinearDepthTarget(width, height);

  distortShader = LoadShader(0, "core/shaders/distortion.fs");
  depthCopyShader = LoadShader(0, "core/shaders/depth_copy.fs");
  depthCopyNearLoc = GetShaderLocation(depthCopyShader, "u_near");
  depthCopyFarLoc = GetShaderLocation(depthCopyShader, "u_far");

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
  UnloadRenderTexture(prevDepthTex);
  UnloadShader(distortShader);
  UnloadShader(depthCopyShader);
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

void ScreenDistort_SnapshotDepth(void) {
  // Copy scene depth (renderTex.depth) into prevDepthTex's COLOR attachment
  // via depthCopyShader (writes depth value to color R). Sampling that color
  // texture downstream is reliable, unlike sampling the depth texture
  // directly (which read back as 0 → soft factor 0 → invisible particles).
  float nearVal = rlGetCullDistanceNear();
  float farVal = rlGetCullDistanceFar();
  BeginTextureMode(prevDepthTex);
  BeginShaderMode(depthCopyShader);
  if (depthCopyNearLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyNearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
  if (depthCopyFarLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyFarLoc, &farVal, SHADER_UNIFORM_FLOAT);
  DrawTextureRec(renderTex.depth,
                 (Rectangle){0, 0, (float)renderTex.texture.width,
                             (float)renderTex.texture.height},
                 (Vector2){0, 0}, WHITE);
  EndShaderMode();
  EndTextureMode();
}

Texture2D ScreenDistort_GetDepthTexture(void) { return prevDepthTex.texture; }

void ScreenDistort_BindDepthForSoftParticles(Shader shader, int textureSlot) {
  if (shader.id == 0 || shader.locs == NULL)
    return;

  int depthLoc = GetShaderLocation(shader, "u_cameraDepthTex");
  if (depthLoc >= 0) {
    rlActiveTextureSlot(textureSlot);
    rlEnableTexture(prevDepthTex.texture.id); // color attachment holding copied depth
    rlActiveTextureSlot(0);
    SetShaderValue(shader, depthLoc, &textureSlot, SHADER_UNIFORM_INT);
  }

  int nearLoc = GetShaderLocation(shader, "u_cameraNear");
  int farLoc = GetShaderLocation(shader, "u_cameraFar");
  if (nearLoc >= 0 || farLoc >= 0) {
    float nearVal = rlGetCullDistanceNear();
    float farVal = rlGetCullDistanceFar();
    if (nearLoc >= 0)
      SetShaderValue(shader, nearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
    if (farLoc >= 0)
      SetShaderValue(shader, farLoc, &farVal, SHADER_UNIFORM_FLOAT);
  }

  int resLoc = GetShaderLocation(shader, "u_resolution");
  if (resLoc >= 0) {
    Vector2 res = {(float)renderTex.texture.width, (float)renderTex.texture.height};
    SetShaderValue(shader, resLoc, &res, SHADER_UNIFORM_VEC2);
  }
}

void ScreenDistort_UnbindSoftParticleDepth(int textureSlot) {
  rlActiveTextureSlot(textureSlot);
  rlDisableTexture();
  rlActiveTextureSlot(0);
}

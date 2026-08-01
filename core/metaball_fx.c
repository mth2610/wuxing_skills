#include "core/metaball_fx.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>

static RenderTexture2D maskTex;
static RenderTexture2D blurTex;
static Shader blurShader;
static Shader thresholdShader;

static int blurDirectionLoc;
static int thresholdTintLoc, thresholdValueLoc, thresholdSmoothnessLoc;
static bool s_prepared = false;
static Vector4 s_preparedTint;
static float s_preparedThreshold, s_preparedSmoothness;

typedef struct {
  Vector3 worldPos;
  float radius;
} MetaballBlobEntry;

static MetaballBlobEntry s_registry[METABALL_MAX_BLOBS];
static int s_registryCount = 0;

void MetaballFX_Init(int width, int height) {
  // Độ phân giải thấp hơn màn hình: blur rẻ hơn + biên tự nhiên mượt hơn
  // (đặc trưng của kỹ thuật metaball, không phải nhược điểm).
  int w = width / 2;
  int h = height / 2;
  maskTex = LoadRenderTexture(w, h);
  blurTex = LoadRenderTexture(w, h);

  SetTextureFilter(maskTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(maskTex.texture, TEXTURE_WRAP_CLAMP);
  SetTextureFilter(blurTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(blurTex.texture, TEXTURE_WRAP_CLAMP);

  // Tái dùng nguyên blur shader của bloom pipeline (separable Gaussian
  // generic, không phụ thuộc gì riêng bloom) thay vì viết lại.
  blurShader = LoadShader(0, "core/shaders/bloom_blur.fs");
  thresholdShader = LoadShader(0, "core/shaders/metaball_threshold.fs");

  blurDirectionLoc = GetShaderLocation(blurShader, "u_direction");
  thresholdTintLoc = GetShaderLocation(thresholdShader, "u_tint");
  thresholdValueLoc = GetShaderLocation(thresholdShader, "u_threshold");
  thresholdSmoothnessLoc = GetShaderLocation(thresholdShader, "u_smoothness");
}

void MetaballFX_Unload(void) {
  UnloadRenderTexture(maskTex);
  UnloadRenderTexture(blurTex);
  UnloadShader(blurShader);
  UnloadShader(thresholdShader);
}

void MetaballFX_RegisterBlob(Vector3 worldPos, float radius) {
  if (s_registryCount >= METABALL_MAX_BLOBS)
    return;
  s_registry[s_registryCount].worldPos = worldPos;
  s_registry[s_registryCount].radius = radius;
  s_registryCount++;
}

bool MetaballFX_HasRegisteredBlobs(void) { return s_registryCount > 0; }

void MetaballFX_Prepare(Camera3D camera, Color tint, float threshold,
                        float smoothness) {
  int blobCount = s_registryCount;
  s_registryCount = 0; // registry tồn tại đúng 1 frame, xoá ngay cả khi blobCount==0
  if (blobCount == 0) {
    s_prepared = false;
    return;
  }

  int w = maskTex.texture.width;
  int h = maskTex.texture.height;
  int screenH = GetScreenHeight();

  // PASS 1: vẽ blob world-space -> đĩa tròn screen-space, additive (chồng
  // lấn = density cao hơn -> hoà vào nhau rõ hơn sau threshold).
  BeginTextureMode(maskTex);
  ClearBackground(BLANK);
  if (blobCount > 0) {
    BeginBlendMode(BLEND_ADDITIVE);
    float halfFovyRad = camera.fovy * DEG2RAD * 0.5f;
    for (int i = 0; i < blobCount; i++) {
      float dist = Vector3Distance(camera.position, s_registry[i].worldPos);
      if (dist < 0.001f)
        continue;
      float pixelsPerUnit = (float)screenH / (2.0f * dist * tanf(halfFovyRad));
      Vector2 screenPos = GetWorldToScreen(s_registry[i].worldPos, camera);
      // maskTex ở nửa độ phân giải -> scale toạ độ/bán kính theo tỉ lệ buffer
      Vector2 maskPos = {screenPos.x * 0.5f, screenPos.y * 0.5f};
      float maskRadius = s_registry[i].radius * pixelsPerUnit * 0.5f;
      DrawCircleV(maskPos, maskRadius, WHITE);
    }
    EndBlendMode();
  }
  EndTextureMode();

  // PASS 2-3: blur Gaussian 2 chiều (giống pipeline bloom)
  BeginTextureMode(blurTex);
  ClearBackground(BLANK);
  BeginShaderMode(blurShader);
  Vector2 dirH = {2.0f / (float)w, 0.0f};
  SetShaderValue(blurShader, blurDirectionLoc, &dirH, SHADER_UNIFORM_VEC2);
  DrawTextureRec(maskTex.texture, (Rectangle){0, 0, (float)w, (float)h},
                (Vector2){0, 0}, WHITE);
  EndShaderMode();
  EndTextureMode();

  BeginTextureMode(maskTex);
  ClearBackground(BLANK);
  BeginShaderMode(blurShader);
  Vector2 dirV = {0.0f, 2.0f / (float)h};
  SetShaderValue(blurShader, blurDirectionLoc, &dirV, SHADER_UNIFORM_VEC2);
  DrawTextureRec(blurTex.texture, (Rectangle){0, 0, (float)w, (float)h},
                (Vector2){0, 0}, WHITE);
  EndShaderMode();
  EndTextureMode();

  s_preparedTint = ColorNormalize(tint);
  s_preparedThreshold = threshold;
  s_preparedSmoothness = smoothness;
  s_prepared = true;
}

void MetaballFX_Composite(void) {
  if (!s_prepared) return;
  s_prepared = false;

  // PASS 4: threshold + composite lên render target hiện tại. main.c sends
  // this pass to the VFX body target, where the HDR compositor protects its
  // tint from a bright destination.
  int w = maskTex.texture.width;
  int h = maskTex.texture.height;
  // maskTex ở NỬA độ phân giải — phải kéo giãn (DrawTexturePro) phủ TOÀN
  // màn hình, không DrawTextureRec ở kích thước gốc (sẽ chỉ phủ 1/4 góc
  // trên-trái, làm blob bị dồn/lệch về góc — chính là lỗi "blob bắn xa").
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(thresholdShader);
  SetShaderValue(thresholdShader, thresholdTintLoc, &s_preparedTint, SHADER_UNIFORM_VEC4);
  SetShaderValue(thresholdShader, thresholdValueLoc, &s_preparedThreshold, SHADER_UNIFORM_FLOAT);
  SetShaderValue(thresholdShader, thresholdSmoothnessLoc, &s_preparedSmoothness,
                 SHADER_UNIFORM_FLOAT);
  DrawTexturePro(maskTex.texture,
                 (Rectangle){0, 0, (float)w, -(float)h}, // source (flip Y)
                 (Rectangle){0, 0, (float)GetScreenWidth(),
                             (float)GetScreenHeight()}, // dest: full screen
                 (Vector2){0, 0}, 0.0f, WHITE);
  EndShaderMode();
  EndBlendMode();
}

void MetaballFX_DrawRegistered(Camera3D camera, Color tint, float threshold,
                               float smoothness) {
  MetaballFX_Prepare(camera, tint, threshold, smoothness);
  MetaballFX_Composite();
}

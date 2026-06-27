#include "core/post_fx.h"
#include "rlgl.h"
#include <string.h>

static RenderTexture2D mainRenderTex;
static RenderTexture2D bloomTex;
static RenderTexture2D blurTex;

static Shader brightShader;
static Shader blurShader;
static Shader compositeShader;

// Uniform locations cho bộ tổng hợp hậu kỳ (composite shader)
static int bloomEnabledLoc;
static int bloomIntensityLoc;
static int bloomTexLoc;
static int chromaticEnabledLoc;
static int chromaticStrengthLoc;
static int vignetteEnabledLoc;
static int vignetteRadiusLoc;
static int vignetteSoftnessLoc;
static int colorGradeEnabledLoc;
static int contrastLoc;
static int saturationLoc;
static int colorTintLoc;

// Uniform locations cho shader lọc sáng (bright shader)
static int brightThresholdLoc;

// Uniform locations cho shader làm mờ (blur shader)
static int blurDirectionLoc;

void PostFX_Init(int width, int height) {
  // RenderTexture chính ở độ phân giải gốc để hứng toàn bộ cảnh game
  mainRenderTex = LoadRenderTexture(width, height);

  // Downscale buffers xuống 1/4 độ phân giải để tối ưu hiệu năng blur và tạo
  // quầng sáng rộng
  bloomTex = LoadRenderTexture(width / 4, height / 4);
  blurTex = LoadRenderTexture(width / 4, height / 4);

  // --- SỬ DỤNG HÀM THUẦN RAYLIB ĐỂ ĐỔI WRAP MODE TRÁNH LỖI BIÊN DỊCH ---
  // Khử răng cưa và ép biên không cho lặp texture (Dập tắt hoàn toàn ảo ảnh rìa
  // màn hình)
  SetTextureFilter(bloomTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(bloomTex.texture, TEXTURE_WRAP_CLAMP);

  SetTextureFilter(blurTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(blurTex.texture, TEXTURE_WRAP_CLAMP);
  // --------------------------------------------------------------------

  brightShader = LoadShader(0, "core/shaders/bloom_bright.fs");
  blurShader = LoadShader(0, "core/shaders/bloom_blur.fs");
  compositeShader = LoadShader(0, "core/shaders/post_process.fs");

  // Đăng ký vị trí uniforms cho lọc sáng và mờ
  brightThresholdLoc = GetShaderLocation(brightShader, "u_threshold");
  blurDirectionLoc = GetShaderLocation(blurShader, "u_direction");

  // Đăng ký vị trí uniforms cho tổng hợp hậu kỳ
  bloomEnabledLoc = GetShaderLocation(compositeShader, "u_bloomEnabled");
  bloomIntensityLoc = GetShaderLocation(compositeShader, "u_bloomIntensity");
  bloomTexLoc = GetShaderLocation(compositeShader, "u_bloomTex");
  chromaticEnabledLoc =
      GetShaderLocation(compositeShader, "u_chromaticEnabled");
  chromaticStrengthLoc =
      GetShaderLocation(compositeShader, "u_chromaticStrength");
  vignetteEnabledLoc = GetShaderLocation(compositeShader, "u_vignetteEnabled");
  vignetteRadiusLoc = GetShaderLocation(compositeShader, "u_vignetteRadius");
  vignetteSoftnessLoc =
      GetShaderLocation(compositeShader, "u_vignetteSoftness");
  colorGradeEnabledLoc =
      GetShaderLocation(compositeShader, "u_colorGradeEnabled");
  contrastLoc = GetShaderLocation(compositeShader, "u_contrast");
  saturationLoc = GetShaderLocation(compositeShader, "u_saturation");
  colorTintLoc = GetShaderLocation(compositeShader, "u_colorTint");
}

void PostFX_Unload(void) {
  UnloadRenderTexture(mainRenderTex);
  UnloadRenderTexture(bloomTex);
  UnloadRenderTexture(blurTex);

  UnloadShader(brightShader);
  UnloadShader(blurShader);
  UnloadShader(compositeShader);
}

void PostFX_Begin(void) { BeginTextureMode(mainRenderTex); }

void PostFX_End(void) { EndTextureMode(); }

void PostFX_Draw(const PostFXConfig *config) {
  int width = mainRenderTex.texture.width;
  int height = mainRenderTex.texture.height;

  if (config->bloomEnabled) {
    // PASS 1: Trích xuất các pixel sáng từ mainRenderTex sang bloomTex (1/4
    // size)
    BeginTextureMode(bloomTex);
    ClearBackground(BLACK);
    BeginShaderMode(brightShader);
    SetShaderValue(brightShader, brightThresholdLoc, &config->bloomThreshold,
                   SHADER_UNIFORM_FLOAT);
    DrawTextureRec(mainRenderTex.texture,
                   (Rectangle){0, 0, (float)width, (float)height},
                   (Vector2){0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    // PASS 2: Khử răng cưa và làm mờ theo chiều ngang (bloomTex -> blurTex)
    BeginTextureMode(blurTex);
    ClearBackground(BLACK);
    BeginShaderMode(blurShader);
    Vector2 dirH = {2.5f / (float)bloomTex.texture.width, 0.0f};
    SetShaderValue(blurShader, blurDirectionLoc, &dirH, SHADER_UNIFORM_VEC2);
    DrawTextureRec(bloomTex.texture,
                   (Rectangle){0, 0, (float)bloomTex.texture.width,
                               (float)bloomTex.texture.height},
                   (Vector2){0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    // PASS 3: Làm mờ theo chiều dọc (blurTex -> bloomTex)
    BeginTextureMode(bloomTex);
    ClearBackground(BLACK);
    BeginShaderMode(blurShader);
    Vector2 dirV = {0.0f, 2.5f / (float)bloomTex.texture.height};
    SetShaderValue(blurShader, blurDirectionLoc, &dirV, SHADER_UNIFORM_VEC2);
    DrawTextureRec(blurTex.texture,
                   (Rectangle){0, 0, (float)blurTex.texture.width,
                               (float)blurTex.texture.height},
                   (Vector2){0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();
  }

  // PASS 4: Composite vẽ lên màn hình chính
  BeginShaderMode(compositeShader);
  int bloomTexSlot = 1;
  rlActiveTextureSlot(bloomTexSlot);
  rlEnableTexture(bloomTex.texture.id);
  rlActiveTextureSlot(0);

  SetShaderValue(compositeShader, bloomTexLoc, &bloomTexSlot,
                 SHADER_UNIFORM_INT);

  float bloomEnabledVal = config->bloomEnabled ? 1.0f : 0.0f;
  SetShaderValue(compositeShader, bloomEnabledLoc, &bloomEnabledVal,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, bloomIntensityLoc, &config->bloomIntensity,
                 SHADER_UNIFORM_FLOAT);

  float chromaticEnabledVal = config->chromaticEnabled ? 1.0f : 0.0f;
  SetShaderValue(compositeShader, chromaticEnabledLoc, &chromaticEnabledVal,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, chromaticStrengthLoc,
                 &config->chromaticStrength, SHADER_UNIFORM_FLOAT);

  float vignetteEnabledVal = config->vignetteEnabled ? 1.0f : 0.0f;
  SetShaderValue(compositeShader, vignetteEnabledLoc, &vignetteEnabledVal,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, vignetteRadiusLoc, &config->vignetteRadius,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, vignetteSoftnessLoc,
                 &config->vignetteSoftness, SHADER_UNIFORM_FLOAT);

  float colorGradeEnabledVal = config->colorGradeEnabled ? 1.0f : 0.0f;
  SetShaderValue(compositeShader, colorGradeEnabledLoc, &colorGradeEnabledVal,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, contrastLoc, &config->contrast,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, saturationLoc, &config->saturation,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, colorTintLoc, &config->colorTint,
                 SHADER_UNIFORM_VEC3);

  // Vẽ lật trục Y duy nhất một lần ra màn hình chính
  DrawTextureRec(mainRenderTex.texture,
                 (Rectangle){0, 0, (float)width, -(float)height},
                 (Vector2){0, 0}, WHITE);
  EndShaderMode();

  rlActiveTextureSlot(1);
  rlDisableTexture();
  rlActiveTextureSlot(0);
}
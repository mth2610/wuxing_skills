#include "core/post_fx.h"
#include "core/screen_distort.h" // ScreenDistort_IsHDR — scene buffer is the HDR authority
#include "rlgl.h"
#include <string.h>

static RenderTexture2D mainRenderTex;
static RenderTexture2D bloomTex; // bright-pass output + final upsampled result (1/4)

// HDR (Đợt G) — the whole offscreen chain (scene + bloom pyramid) uses a
// 16-bit half-float color format so additive VFX / emissive can exceed 1.0 and
// survive until the composite pass tone-maps HDR→LDR. Probed once at init:
// GLES2 devices without EXT_color_buffer_half_float fall back to RGBA8 (the old
// LDR path) so nothing goes black on weak hardware. Query via PostFX_IsHDR().
static bool s_hdrActive = false;

// Dual-filter pyramid: dfTex[0]=1/8, dfTex[1]=1/16 of full resolution.
#define DUAL_FILTER_LEVELS 2
static RenderTexture2D dfTex[DUAL_FILTER_LEVELS];

static Shader brightShader;
static Shader dsShader; // dual-filter downsample
static Shader usShader; // dual-filter upsample
static Shader compositeShader;

// Uniform locations — composite
static int bloomEnabledLoc;
static int bloomIntensityLoc;
static int bloomTexLoc;
static int chromaticEnabledLoc;
static int chromaticStrengthLoc;
static int vignetteEnabledLoc;
static int vignetteRadiusLoc;
static int vignetteSoftnessLoc;
static float s_monochrome = 0.0f; // Thái Cực overlay — see PostFX_SetMonochrome

static int colorGradeEnabledLoc;
static int contrastLoc;
static int saturationLoc;
static int colorTintLoc;
static int shadowTintLoc;
static int highlightTintLoc;
static int tonemapEnabledLoc;
static int exposureLoc;

// Uniform locations — bright pass
static int brightThresholdLoc;

// Uniform locations — dual-filter passes
static int dsTexSizeLoc;
static int usTexSizeLoc;

static RenderTexture2D LoadRenderTextureWithFormat(int width, int height, int format)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0)
  {
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(NULL, width, height, format, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = format;
    target.texture.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);

    rlDisableFramebuffer();
  }
  return target;
}

void PostFX_Init(int width, int height)
{
  // --- HDR: match ScreenDistort's authoritative decision. ScreenDistort_Init
  // runs first (main.c) and probes the real scene buffer (float color + depth);
  // PostFX only receives the composited distort quad, so it must use the SAME
  // format to preserve > 1.0 values into bloom/tone-map. If ScreenDistort fell
  // back to LDR, so do we. (A color-only float target is a strict subset of the
  // color+depth float FBO ScreenDistort already validated, so no re-probe here.)
  const int hdrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  const int ldrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  s_hdrActive = ScreenDistort_IsHDR();
  const int colorFmt = s_hdrActive ? hdrFmt : ldrFmt;
  mainRenderTex = LoadRenderTextureWithFormat(width, height, colorFmt);
  TraceLog(LOG_INFO, "PostFX: %s pipeline (%s)", s_hdrActive ? "HDR float" : "LDR",
           s_hdrActive ? "R16G16B16A16" : "R8G8B8A8");

  bloomTex = LoadRenderTextureWithFormat(width / 4, height / 4, colorFmt);
  SetTextureFilter(bloomTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(bloomTex.texture, TEXTURE_WRAP_CLAMP);

  int w = width / 4, h = height / 4;
  for (int i = 0; i < DUAL_FILTER_LEVELS; i++)
  {
    w /= 2;
    h /= 2;
    dfTex[i] = LoadRenderTextureWithFormat(w, h, colorFmt);
    SetTextureFilter(dfTex[i].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(dfTex[i].texture, TEXTURE_WRAP_CLAMP);
  }

  brightShader = LoadShader(0, "core/shaders/bloom_bright.fs");
  dsShader = LoadShader(0, "core/shaders/bloom_downsample.fs");
  usShader = LoadShader(0, "core/shaders/bloom_upsample.fs");
  compositeShader = LoadShader(0, "core/shaders/post_process.fs");

  brightThresholdLoc = GetShaderLocation(brightShader, "u_threshold");
  dsTexSizeLoc = GetShaderLocation(dsShader, "u_texelSize");
  usTexSizeLoc = GetShaderLocation(usShader, "u_texelSize");

  bloomEnabledLoc = GetShaderLocation(compositeShader, "u_bloomEnabled");
  bloomIntensityLoc = GetShaderLocation(compositeShader, "u_bloomIntensity");
  bloomTexLoc = GetShaderLocation(compositeShader, "u_bloomTex");
  chromaticEnabledLoc = GetShaderLocation(compositeShader, "u_chromaticEnabled");
  chromaticStrengthLoc = GetShaderLocation(compositeShader, "u_chromaticStrength");
  vignetteEnabledLoc = GetShaderLocation(compositeShader, "u_vignetteEnabled");
  vignetteRadiusLoc = GetShaderLocation(compositeShader, "u_vignetteRadius");
  vignetteSoftnessLoc = GetShaderLocation(compositeShader, "u_vignetteSoftness");
  colorGradeEnabledLoc = GetShaderLocation(compositeShader, "u_colorGradeEnabled");
  contrastLoc = GetShaderLocation(compositeShader, "u_contrast");
  saturationLoc = GetShaderLocation(compositeShader, "u_saturation");
  colorTintLoc = GetShaderLocation(compositeShader, "u_colorTint");
  shadowTintLoc = GetShaderLocation(compositeShader, "u_shadowTint");
  highlightTintLoc = GetShaderLocation(compositeShader, "u_highlightTint");
  tonemapEnabledLoc = GetShaderLocation(compositeShader, "u_tonemapEnabled");
  exposureLoc = GetShaderLocation(compositeShader, "u_exposure");
}

bool PostFX_IsHDR(void) { return s_hdrActive; }

void PostFX_Unload(void)
{
  UnloadRenderTexture(mainRenderTex);
  UnloadRenderTexture(bloomTex);
  for (int i = 0; i < DUAL_FILTER_LEVELS; i++)
    UnloadRenderTexture(dfTex[i]);

  UnloadShader(brightShader);
  UnloadShader(dsShader);
  UnloadShader(usShader);
  UnloadShader(compositeShader);
}

void PostFX_Begin(void) { BeginTextureMode(mainRenderTex); }

void PostFX_End(void) { EndTextureMode(); }

// Helper: blit src into current render target (dstW×dstH), applying shader.
// Uses DrawTexturePro so src is scaled to fill dst — required when src and dst
// are different sizes (e.g. each level of the downsample/upsample pyramid).
static void DualFilterPass(Shader sh, int texSizeLoc, Texture2D src,
                           int srcW, int srcH, int dstW, int dstH)
{
  BeginShaderMode(sh);

  // [TỐI ƯU]: Chuyển sang ép kiểu nghịch đảo
  Vector2 ts = {1.0f / (float)srcW, 1.0f / (float)srcH};
  SetShaderValue(sh, texSizeLoc, &ts, SHADER_UNIFORM_VEC2);

  // [TỐI ƯU 1]: Tắt Alpha Blending để GPU ghi đè 100% pixel ảnh thay vì trộn màu
  rlDisableColorBlend();
  DrawTexturePro(src,
                 (Rectangle){0, 0, (float)srcW, (float)srcH},
                 (Rectangle){0, 0, (float)dstW, (float)dstH},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlEnableColorBlend();

  EndShaderMode();
}

void PostFX_Draw(const PostFXConfig *config)
{
  int width = mainRenderTex.texture.width;
  int height = mainRenderTex.texture.height;

  if (config->bloomEnabled)
  {
    // PASS 1: Extract bright pixels → bloomTex (1/4)
    BeginTextureMode(bloomTex);
    // [TỐI ƯU 1]: Xóa ClearBackground(BLACK) gây lãng phí bộ nhớ FBO
    BeginShaderMode(brightShader);
    SetShaderValue(brightShader, brightThresholdLoc, &config->bloomThreshold,
                   SHADER_UNIFORM_FLOAT);

    rlDisableColorBlend();
    DrawTexturePro(mainRenderTex.texture,
                   (Rectangle){0, 0, (float)width, -(float)height},
                   (Rectangle){0, 0, (float)bloomTex.texture.width,
                               (float)bloomTex.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlEnableColorBlend();

    EndShaderMode();
    EndTextureMode();

    // PASSES 2–3: Downsample chain  bloomTex(1/4) → dfTex[0](1/8) → dfTex[1](1/16)
    Texture2D prevTex = bloomTex.texture;
    int prevW = bloomTex.texture.width, prevH = bloomTex.texture.height;
    for (int i = 0; i < DUAL_FILTER_LEVELS; i++)
    {
      int dstW = dfTex[i].texture.width, dstH = dfTex[i].texture.height;
      BeginTextureMode(dfTex[i]);
      // [TỐI ƯU 1]: Xóa ClearBackground(BLACK)
      DualFilterPass(dsShader, dsTexSizeLoc, prevTex, prevW, prevH, dstW, dstH);
      EndTextureMode();
      prevTex = dfTex[i].texture;
      prevW = dstW;
      prevH = dstH;
    }

    // PASSES 4–5: Upsample chain  dfTex[1](1/16) → dfTex[0](1/8) → bloomTex(1/4)
    for (int i = DUAL_FILTER_LEVELS - 1; i >= 0; i--)
    {
      RenderTexture2D dst = (i == 0) ? bloomTex : dfTex[i - 1];
      int dstW = dst.texture.width, dstH = dst.texture.height;
      BeginTextureMode(dst);
      // [TỐI ƯU 1]: Xóa ClearBackground(BLACK)
      DualFilterPass(usShader, usTexSizeLoc, prevTex, prevW, prevH, dstW, dstH);
      EndTextureMode();
      prevTex = dst.texture;
      prevW = dstW;
      prevH = dstH;
    }
  }

  // PASS 6: Composite → screen
  BeginShaderMode(compositeShader);
  SetShaderValueTexture(compositeShader, bloomTexLoc, bloomTex.texture);

  // [TỐI ƯU 2]: Ép kiểu (float) thẳng từ bool, thay thế cho toán tử rẽ nhánh `? 1.0f : 0.0f`
  float bloomEnabledVal = (float)config->bloomEnabled;
  SetShaderValue(compositeShader, bloomEnabledLoc, &bloomEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, bloomIntensityLoc, &config->bloomIntensity, SHADER_UNIFORM_FLOAT);

  float chromaticEnabledVal = (float)config->chromaticEnabled;
  SetShaderValue(compositeShader, chromaticEnabledLoc, &chromaticEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, chromaticStrengthLoc, &config->chromaticStrength, SHADER_UNIFORM_FLOAT);

  float vignetteEnabledVal = (float)config->vignetteEnabled;
  SetShaderValue(compositeShader, vignetteEnabledLoc, &vignetteEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, vignetteRadiusLoc, &config->vignetteRadius, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, vignetteSoftnessLoc, &config->vignetteSoftness, SHADER_UNIFORM_FLOAT);

  float colorGradeEnabledVal = (float)(config->colorGradeEnabled || s_monochrome > 0.0f);
  float saturationVal = config->colorGradeEnabled ? config->saturation : 1.0f;
  saturationVal *= (1.0f - s_monochrome);
  SetShaderValue(compositeShader, colorGradeEnabledLoc, &colorGradeEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, contrastLoc, &config->contrast, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, saturationLoc, &saturationVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, colorTintLoc, &config->colorTint, SHADER_UNIFORM_VEC3);

  Vector3 shadowTint = (config->shadowTint.x + config->shadowTint.y + config->shadowTint.z > 0.0f)
                           ? config->shadowTint
                           : (Vector3){1.0f, 1.0f, 1.0f};
  Vector3 highlightTint = (config->highlightTint.x + config->highlightTint.y + config->highlightTint.z > 0.0f)
                              ? config->highlightTint
                              : (Vector3){1.0f, 1.0f, 1.0f};
  SetShaderValue(compositeShader, shadowTintLoc, &shadowTint, SHADER_UNIFORM_VEC3);
  SetShaderValue(compositeShader, highlightTintLoc, &highlightTint, SHADER_UNIFORM_VEC3);

  float tonemapEnabledVal = (float)config->tonemapEnabled;
  float exposureVal = (config->exposure > 0.0f) ? config->exposure : 1.0f;
  SetShaderValue(compositeShader, tonemapEnabledLoc, &tonemapEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, exposureLoc, &exposureVal, SHADER_UNIFORM_FLOAT);

  // [TỐI ƯU 1]: Áp dụng vô hiệu hóa blend cho composite cuối cùng
  rlDisableColorBlend();
  DrawTextureRec(mainRenderTex.texture,
                 (Rectangle){0, 0, (float)width, -(float)height},
                 (Vector2){0, 0}, WHITE);
  rlEnableColorBlend();

  EndShaderMode();
}

void PostFX_SetMonochrome(float intensity01)
{
  if (intensity01 < 0.0f)
    intensity01 = 0.0f;
  if (intensity01 > 1.0f)
    intensity01 = 1.0f;
  s_monochrome = intensity01;
}
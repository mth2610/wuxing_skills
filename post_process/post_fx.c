#include "post_process/post_fx.h"
#include "core/tuning.h"
#include "post_process/scene_targets.h"
#include "post_process/screen_distort.h"  // SceneTargets_IsHDR — scene buffer is the HDR authority
#include "core/gfx_quality.h"             // E8 — the tier budget for the two new passes
#include "post_process/color_grade_lut.h" // G5 — display-referred grading strip
#include "post_process/post_fx_debug_internal.inl"
#include "rlgl.h"
#include "post_process/render_scale.h"
#include <string.h>

static RenderTexture2D mainRenderTex;
static RenderTexture2D bloomTex; // bright-pass output + final upsampled result (1/4)
/* Composite lands here instead of the swapchain when FXAA is on, so the AA pass has
   something to sample. LDR by definition: FXAA thresholds on perceptual luma and must
   run after the tone map. */
static RenderTexture2D ldrTex;
static Shader fxaaShader;
static int fxaaTexelLoc = -1;
static int fxaaSharpenLoc = -1;

// HDR (Đợt G) — the whole offscreen chain (scene + bloom pyramid) uses a
// 16-bit half-float color format so additive VFX / emissive can exceed 1.0 and
// survive until the composite pass tone-maps HDR→LDR.
static bool s_hdrActive = false;

/* Set for ONE frame by PostFX_UseDirectSource, and consumed by PostFX_Draw. */
static Texture2D s_directSource = {0};

// Bloom pyramid below the 1/4 bright-pass target: dfTex[0]=1/8 … dfTex[4]=1/128.
#define DUAL_FILTER_LEVELS 5
static RenderTexture2D dfTex[DUAL_FILTER_LEVELS];
static int s_dfLevels = 0;

static Shader brightShader;
static Shader dsShader; // dual-filter downsample
static Shader usShader; // dual-filter upsample
static Shader compositeShader;

// Uniform locations — composite
static int bloomEnabledLoc;
static int bloomIntensityLoc;
static int bloomTexLoc;
static int bloomTexelLoc;
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
static int exposureTexLoc;
static int autoExposureLoc;
static float s_autoExposure = 0.0f;
static int hueRestoreLoc;
static int shoulderViewLoc;
static int lutTexLoc;
static int lutEnabledLoc;
static int lutStrengthLoc;
static int lutParamsLoc;
static int lutSizeLoc;
static float s_lutStrengthOverride = 0.0f;
static float s_contrastOverride = 0.0f;
static float s_saturationOverride = 0.0f;

// Uniform locations — radial blur (E1a, lives in the composite shader)
static int radialBlurEnabledLoc;
static int radialBlurCenterLoc;
static int radialBlurStrengthLoc;
static int radialBlurFalloffLoc;

// Uniform locations — anamorphic streak (E1b, lives in the DOWNSAMPLE shader)
static int streakEnabledLoc;
static int streakStrengthLoc;
static int streakAngleLoc;

// ── Transient radial burst state (E1a) ──────────────────────────────────────
static Vector3 s_burstWorldPos;
static float s_burstStrength; // peak strength as requested
static float s_burstElapsed;
static float s_burstDuration; // 0 = no burst live
static Vector2 s_burstUV;     // projected screen UV, updated per frame
static float s_burstCurrent;  // decayed strength this frame

// Uniform locations — bright pass
static int brightThresholdLoc;
static int brightExposureLoc;
static int brightMaxEnergyLoc;
static int brightKneeLoc;
static int brightSourceTexelSizeLoc;
static float s_bloomMaxEnergy = 0.0f;
static float s_bloomIntensityOverride = 0.0f;
static float s_bloomThresholdOverride = 0.0f;
static float s_bloomKnee = 0.0f; // 0 = shader default
static float s_hueRestore = 0.0f;
static float s_shoulderView = 0.0f;
static float s_fxaa = 1.0f;
static float s_sharpen = -1.0f;
static float s_bloomScatterOverride = 0.0f;
static float s_bloomKaris = 1.0f; // 1 = firefly weighting on

// Uniform locations — dual-filter passes
static int dsTexSizeLoc;
static int dsKarisLoc;
static int usTexSizeLoc;
static int usScatterLoc;

#define BLOOM_SCATTER_DEFAULT 0.65f

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

static int s_targetWidth = 0;
static int s_targetHeight = 0;

static void PostFX_PrimeTarget(RenderTexture2D target)
{
  if (target.id == 0 || target.texture.id == 0)
    return;
  BeginTextureMode(target);
  ClearBackground(BLACK);
  EndTextureMode();
}

/* [TỐI ƯU HÓA VRAM & RESIZE]: Chỉ cấp phát mainRenderTex khi thực sự có pass
 * ScreenDistort gọi PostFX_Begin(). Khi đi qua PostFX_UseDirectSource (99% thời
 * gian không có sóng xung kích), ta tiết kiệm toàn bộ 1 FBO HDR full-screen
 * (~7.5 MB ở 720p) và giảm thời gian khựng khi RenderScale thay đổi. */
static void PostFX_EnsureMainTarget(void)
{
  if (mainRenderTex.id != 0 || s_targetWidth <= 0 || s_targetHeight <= 0)
    return;
  const int hdrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  const int ldrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  const int colorFmt = s_hdrActive ? hdrFmt : ldrFmt;
  mainRenderTex = LoadRenderTextureWithFormat(s_targetWidth, s_targetHeight, colorFmt);
  SetTextureFilter(mainRenderTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(mainRenderTex.texture, TEXTURE_WRAP_CLAMP);
  PostFX_PrimeTarget(mainRenderTex);
}

static void PostFX_DestroySizedTargets(void)
{
  rlDrawRenderBatchActive();
  if (mainRenderTex.id > 0)
    UnloadRenderTexture(mainRenderTex);
  mainRenderTex = (RenderTexture2D){0};
  if (bloomTex.id > 0)
    UnloadRenderTexture(bloomTex);
  bloomTex = (RenderTexture2D){0};
  if (ldrTex.id > 0)
    UnloadRenderTexture(ldrTex);
  ldrTex = (RenderTexture2D){0};
  for (int i = 0; i < s_dfLevels; i++)
    UnloadRenderTexture(dfTex[i]);
  s_dfLevels = 0;
}

static void PostFX_CreateSizedTargets(int width, int height)
{
  const int hdrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  const int ldrFmt = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  s_hdrActive = SceneTargets_IsHDR();
  const int colorFmt = s_hdrActive ? hdrFmt : ldrFmt;

  s_targetWidth = width;
  s_targetHeight = height;

  int bw = (width / 4 > 1) ? (width / 4) : 1;
  int bh = (height / 4 > 1) ? (height / 4) : 1;
  bloomTex = LoadRenderTextureWithFormat(bw, bh, colorFmt);
  SetTextureFilter(bloomTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(bloomTex.texture, TEXTURE_WRAP_CLAMP);
  PostFX_PrimeTarget(bloomTex);

  ldrTex = LoadRenderTextureWithFormat(width, height, ldrFmt);
  SetTextureFilter(ldrTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(ldrTex.texture, TEXTURE_WRAP_CLAMP);
  PostFX_PrimeTarget(ldrTex);

  int w = bw, h = bh;
  s_dfLevels = 0;
  for (int i = 0; i < DUAL_FILTER_LEVELS; i++)
  {
    w /= 2;
    h /= 2;
    if (w < 4 || h < 4)
      break;
    dfTex[i] = LoadRenderTextureWithFormat(w, h, colorFmt);
    SetTextureFilter(dfTex[i].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(dfTex[i].texture, TEXTURE_WRAP_CLAMP);
    PostFX_PrimeTarget(dfTex[i]);
    s_dfLevels++;
  }
  TraceLog(LOG_INFO, "PostFX: bloom pyramid %d levels (1/4 .. 1/%d)", s_dfLevels + 1,
           4 << s_dfLevels);
}

int PostFX_GetWidth(void) { return s_targetWidth; }
int PostFX_GetHeight(void) { return s_targetHeight; }

void PostFX_Resize(int width, int height)
{
  if (width < 8 || height < 8)
    return;
  if (width == s_targetWidth && height == s_targetHeight && bloomTex.id != 0)
    return;
  PostFX_DestroySizedTargets();
  PostFX_CreateSizedTargets(width, height);
  TraceLog(LOG_INFO, "PostFX: chain resized to %dx%d", width, height);
}

void PostFX_Init(int width, int height)
{
  if (bloomTex.id != 0 || mainRenderTex.id != 0)
  {
    PostFX_Unload();
  }

  PostFX_CreateSizedTargets(width, height);
  TraceLog(LOG_INFO, "PostFX: %s pipeline (%s)", s_hdrActive ? "HDR float" : "LDR",
           s_hdrActive ? "R16G16B16A16" : "R8G8B8A8");

  brightShader = LoadShader(0, "post_process/shaders/bloom_bright.fs");
  dsShader = LoadShader(0, "post_process/shaders/bloom_downsample.fs");
  usShader = LoadShader(0, "post_process/shaders/bloom_upsample.fs");
  compositeShader = LoadShader(0, "post_process/shaders/post_process.fs");

  brightThresholdLoc = GetShaderLocation(brightShader, "u_threshold");
  brightExposureLoc = GetShaderLocation(brightShader, "u_exposure");
  brightMaxEnergyLoc = GetShaderLocation(brightShader, "u_maxEnergy");
  brightKneeLoc = GetShaderLocation(brightShader, "u_knee");
  brightSourceTexelSizeLoc = GetShaderLocation(brightShader, "u_sourceTexelSize");
  dsTexSizeLoc = GetShaderLocation(dsShader, "u_texelSize");
  dsKarisLoc = GetShaderLocation(dsShader, "u_karis");
  usTexSizeLoc = GetShaderLocation(usShader, "u_texelSize");
  usScatterLoc = GetShaderLocation(usShader, "u_scatter");

  bloomEnabledLoc = GetShaderLocation(compositeShader, "u_bloomEnabled");
  bloomIntensityLoc = GetShaderLocation(compositeShader, "u_bloomIntensity");
  bloomTexLoc = GetShaderLocation(compositeShader, "u_bloomTex");
  bloomTexelLoc = GetShaderLocation(compositeShader, "u_bloomTexel");
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
  exposureTexLoc = GetShaderLocation(compositeShader, "u_exposureTex");
  autoExposureLoc = GetShaderLocation(compositeShader, "u_autoExposure");
  hueRestoreLoc = GetShaderLocation(compositeShader, "u_hueRestore");
  shoulderViewLoc = GetShaderLocation(compositeShader, "u_shoulderView");
  lutTexLoc = GetShaderLocation(compositeShader, "u_lutTex");
  lutEnabledLoc = GetShaderLocation(compositeShader, "u_lutEnabled");
  lutStrengthLoc = GetShaderLocation(compositeShader, "u_lutStrength");
  lutParamsLoc = GetShaderLocation(compositeShader, "u_lutParams");
  lutSizeLoc = GetShaderLocation(compositeShader, "u_lutSize");
  ColorGradeLut_Init();

  fxaaShader = LoadShader(0, "post_process/shaders/fxaa.fs");
  fxaaTexelLoc = GetShaderLocation(fxaaShader, "u_texel");
  fxaaSharpenLoc = GetShaderLocation(fxaaShader, "u_sharpen");
  radialBlurEnabledLoc = GetShaderLocation(compositeShader, "u_radialBlurEnabled");
  radialBlurCenterLoc = GetShaderLocation(compositeShader, "u_radialBlurCenter");
  radialBlurStrengthLoc = GetShaderLocation(compositeShader, "u_radialBlurStrength");
  radialBlurFalloffLoc = GetShaderLocation(compositeShader, "u_radialBlurFalloff");
  streakEnabledLoc = GetShaderLocation(dsShader, "u_streakEnabled");
  streakStrengthLoc = GetShaderLocation(dsShader, "u_streakStrength");
  streakAngleLoc = GetShaderLocation(dsShader, "u_streakAngle");
}

bool PostFX_IsHDR(void) { return s_hdrActive; }

void PostFX_Unload(void)
{
  PostFX_DestroySizedTargets();
  UnloadShader(fxaaShader);

  UnloadShader(brightShader);
  UnloadShader(dsShader);
  UnloadShader(usShader);
  UnloadShader(compositeShader);
  ColorGradeLut_Unload();
}

void PostFX_Begin(void)
{
  PostFX_EnsureMainTarget();
  BeginTextureMode(mainRenderTex);
}

void PostFX_End(void) { EndTextureMode(); }

static void DualFilterPass(Shader sh, int texSizeLoc, Texture2D src,
                           int srcW, int srcH, int dstW, int dstH,
                           const PostFXConfig *streakCfg, float karis,
                           float mixAlpha)
{
  BeginShaderMode(sh);

  if (streakCfg != NULL && streakEnabledLoc >= 0)
  {
    float on = (float)streakCfg->bloomStreakEnabled;
    SetShaderValue(sh, streakEnabledLoc, &on, SHADER_UNIFORM_FLOAT);
    if (streakCfg->bloomStreakEnabled)
    {
      float str = streakCfg->bloomStreakStrength;
      float ang = streakCfg->bloomStreakAngle;
      if (str < 0.0f)
        str = 0.0f;
      else if (str > 1.0f)
        str = 1.0f;
      SetShaderValue(sh, streakStrengthLoc, &str, SHADER_UNIFORM_FLOAT);
      SetShaderValue(sh, streakAngleLoc, &ang, SHADER_UNIFORM_FLOAT);
    }
  }
  if (karis >= 0.0f && dsKarisLoc >= 0)
    SetShaderValue(sh, dsKarisLoc, &karis, SHADER_UNIFORM_FLOAT);

  Vector2 ts = {1.0f / (float)srcW, 1.0f / (float)srcH};
  SetShaderValue(sh, texSizeLoc, &ts, SHADER_UNIFORM_VEC2);

  Rectangle srcRect = {0, 0, (float)srcW, (float)srcH};
  Rectangle dstRect = {0, 0, (float)dstW, (float)dstH};

  if (mixAlpha > 0.0f)
  {
    if (mixAlpha > 1.0f)
      mixAlpha = 1.0f;
    if (usScatterLoc >= 0)
      SetShaderValue(sh, usScatterLoc, &mixAlpha, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ALPHA);
    DrawTexturePro(src, srcRect, dstRect, (Vector2){0, 0}, 0.0f, WHITE);
    EndBlendMode();
  }
  else
  {
    rlDisableColorBlend();
    DrawTexturePro(src, srcRect, dstRect, (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
  }

  EndShaderMode();
}

void PostFX_RadialBurst(Vector3 worldPos, float strength, float duration)
{
  if (strength <= 0.0f || duration <= 0.0f)
    return;

  if (s_burstDuration > 0.0f && s_burstCurrent > strength)
    return;

  s_burstWorldPos = worldPos;
  s_burstStrength = strength;
  s_burstDuration = duration;
  s_burstElapsed = 0.0f;
  s_burstCurrent = strength;
}

void PostFX_UpdateTransient(Camera3D cam, float dt)
{
  if (s_burstDuration <= 0.0f)
    return;

  s_burstElapsed += dt;
  if (s_burstElapsed >= s_burstDuration)
  {
    s_burstDuration = 0.0f;
    s_burstCurrent = 0.0f;
    return;
  }

  float k = 1.0f - (s_burstElapsed / s_burstDuration);
  s_burstCurrent = s_burstStrength * k * k;

  Vector2 sp = GetWorldToScreen(s_burstWorldPos, cam);
  float sw = (float)GetScreenWidth();
  float sh_ = (float)GetScreenHeight();
  s_burstUV.x = (sw > 0.0f) ? (sp.x / sw) : 0.5f;
  s_burstUV.y = (sh_ > 0.0f) ? (1.0f - sp.y / sh_) : 0.5f;
}

bool PostFX_HasTransient(void)
{
  return s_burstDuration > 0.0f && s_burstCurrent > 0.0f;
}

void PostFX_ApplyTransient(PostFXConfig *config)
{
  if (config == NULL || !PostFX_HasTransient())
    return;
  config->radialBlurEnabled = true;
  config->radialBlurCenter = s_burstUV;
  config->radialBlurStrength = s_burstCurrent;
  if (config->radialBlurFalloff <= 0.0f)
    config->radialBlurFalloff = 0.45f;
}

static float s_tuneStreak = 0.0f;
static float s_tuneStreakAngle = 0.0f;
static float s_tuneRadial = 0.0f;
static float s_tuneBloom = -1.0f;
static bool s_tuneReg = false;

static void PostFX_ApplyTuning(PostFXConfig *c)
{
  if (!s_tuneReg)
  {
    s_tuneReg = true;
    Tuning_RegisterFloat("postfx_streak", &s_tuneStreak, 0.0f);
    Tuning_RegisterFloat("postfx_streak_angle", &s_tuneStreakAngle, 0.0f);
    Tuning_RegisterFloat("postfx_radial", &s_tuneRadial, 0.0f);
    Tuning_RegisterFloat("postfx_bloom", &s_tuneBloom, -1.0f);
  }
  c->bloomEnabled = PostFX_ApplyBloomOverrideValue(c->bloomEnabled, s_tuneBloom);
  if (s_tuneStreak > 0.0f)
  {
    c->bloomStreakEnabled = true;
    c->bloomStreakStrength = s_tuneStreak;
    c->bloomStreakAngle = s_tuneStreakAngle;
  }
  if (s_tuneRadial > 0.0f)
  {
    c->radialBlurEnabled = true;
    c->radialBlurStrength = s_tuneRadial;
    if (c->radialBlurCenter.x == 0.0f && c->radialBlurCenter.y == 0.0f)
      c->radialBlurCenter = (Vector2){0.5f, 0.5f};
    if (c->radialBlurFalloff <= 0.0f)
      c->radialBlurFalloff = 0.45f;
  }
}

static void PostFX_ApplyQualityTier(PostFXConfig *c)
{
  GfxQuality q = GfxQuality_Get();

  bool allowStreak = (q >= GFX_MED);
  bool allowRadial = (q >= GFX_HIGH);

  bool cutStreak = c->bloomStreakEnabled && !allowStreak;
  bool cutRadial = c->radialBlurEnabled && !allowRadial;

  if (!allowStreak)
    c->bloomStreakEnabled = false;
  if (!allowRadial)
    c->radialBlurEnabled = false;

  static int s_lastTier = -1;
  static int s_lastCut = -1;
  int cutBits = (cutStreak ? 1 : 0) | (cutRadial ? 2 : 0);
  if ((int)q != s_lastTier || cutBits != s_lastCut)
  {
    s_lastTier = (int)q;
    s_lastCut = cutBits;
    if (cutBits)
      TraceLog(LOG_INFO, "POSTFX tier %d: dropped%s%s (budget, not a bug — "
                         "raise GfxQuality to get them back)",
               (int)q, cutStreak ? " streak-bloom" : "",
               cutRadial ? " radial-blur" : "");
    else
      TraceLog(LOG_INFO, "POSTFX tier %d: streak %s, radial %s", (int)q,
               allowStreak ? "allowed" : "blocked",
               allowRadial ? "allowed" : "blocked");
  }
}

/* [TỐI ƯU HÓA BLOOM PYRAMID]: Giới hạn độ sâu tháp Bloom dựa theo GfxQuality
 * để giảm số lần đóng/mở FBO (RenderPass overhead) trên các thiết bị yếu. */
static int PostFX_GetActiveBloomLevels(void)
{
  GfxQuality q = GfxQuality_Get();
  int maxLv = 4;
  if (q == GFX_LOW)
    maxLv = 3;
  return (s_dfLevels < maxLv) ? s_dfLevels : maxLv;
}

static float s_perfLog = 0.0f;
static bool s_perfReg = false;

static void PostFX_PerfSample(const PostFXConfig *c, int width, int height, int activeLevels)
{
  if (!s_perfReg)
  {
    s_perfReg = true;
    Tuning_RegisterFloat("postfx_perf_log", &s_perfLog, 0.0f);
  }
  if (s_perfLog < 0.5f)
    return;

  static double s_accum = 0.0;
  static int s_frames = 0;
  static double s_last = 0.0;

  double now = GetTime();
  s_accum += (double)GetFrameTime();
  s_frames++;
  if (now - s_last < 1.0 || s_frames == 0)
    return;

  float avgMs = (float)(s_accum / (double)s_frames * 1000.0);
  TraceLog(LOG_INFO,
           "POSTFX perf: %.2f ms/frame avg over %d frames at %dx%d | tier=%d "
           "bloom=%d/%dlv streak=%d radial=%.2f tonemap=%d chroma=%d",
           avgMs, s_frames, width, height, (int)GfxQuality_Get(),
           c->bloomEnabled ? 1 : 0, activeLevels + 1, c->bloomStreakEnabled ? 1 : 0,
           (c->radialBlurEnabled ? c->radialBlurStrength : 0.0f),
           c->tonemapEnabled ? 1 : 0, c->chromaticEnabled ? 1 : 0);

  s_accum = 0.0;
  s_frames = 0;
  s_last = now;
}

void PostFX_Draw(const PostFXConfig *config)
{
  PostFXConfig local = *config;
  if (PostFX_HasTransient())
    PostFX_ApplyTransient(&local);
  PostFX_ApplyTuning(&local);
  PostFX_ApplyQualityTier(&local);

  static bool s_tunablesReg = false;
  if (!s_tunablesReg)
  {
    Tuning_RegisterFloat("bloom_max_energy", &s_bloomMaxEnergy, 0.0f);
    Tuning_RegisterFloat("bloom_intensity", &s_bloomIntensityOverride, 0.0f);
    Tuning_RegisterFloat("bloom_threshold", &s_bloomThresholdOverride, 0.0f);
    Tuning_RegisterFloat("bloom_knee", &s_bloomKnee, 0.0f);
    Tuning_RegisterFloat("bloom_scatter", &s_bloomScatterOverride, 0.0f);
    Tuning_RegisterFloat("postfx_hue_restore", &s_hueRestore, 0.0f);
    Tuning_RegisterFloat("postfx_shoulder_view", &s_shoulderView, 0.0f);
    Tuning_RegisterFloat("postfx_fxaa", &s_fxaa, 1.0f);
    Tuning_RegisterFloat("postfx_sharpen", &s_sharpen, -1.0f);
    Tuning_RegisterFloat("postfx_auto_exposure", &s_autoExposure, 0.0f);
    Tuning_RegisterFloat("bloom_karis", &s_bloomKaris, 1.0f);
    Tuning_RegisterFloat("lut_strength", &s_lutStrengthOverride, 0.0f);
    Tuning_RegisterFloat("postfx_contrast", &s_contrastOverride, 0.0f);
    Tuning_RegisterFloat("postfx_saturation", &s_saturationOverride, 0.0f);
    s_tunablesReg = true;
  }
  config = &local;

  const Texture2D srcTex = (s_directSource.id != 0) ? s_directSource
                                                    : mainRenderTex.texture;
  s_directSource = (Texture2D){0};
  int width = srcTex.width;
  int height = srcTex.height;
  const int activeLevels = PostFX_GetActiveBloomLevels();

  if (config->bloomEnabled && bloomTex.id != 0)
  {
    // PASS 1: Extract bright pixels → bloomTex (1/4)
    BeginTextureMode(bloomTex);
    BeginShaderMode(brightShader);
    float threshold = (s_bloomThresholdOverride > 0.0f) ? s_bloomThresholdOverride
                                                        : config->bloomThreshold;
    float exposureVal = (config->exposure > 0.0f) ? config->exposure : 1.0f;
    SetShaderValue(brightShader, brightThresholdLoc, &threshold,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brightShader, brightExposureLoc, &exposureVal,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brightShader, brightMaxEnergyLoc, &s_bloomMaxEnergy,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brightShader, brightKneeLoc, &s_bloomKnee,
                   SHADER_UNIFORM_FLOAT);

    Vector2 sourceTexelSize = {1.0f / (float)width, 1.0f / (float)height};
    SetShaderValue(brightShader, brightSourceTexelSizeLoc, &sourceTexelSize,
                   SHADER_UNIFORM_VEC2);

    rlDisableColorBlend();
    DrawTexturePro(srcTex,
                   (Rectangle){0, 0, (float)width, -(float)height},
                   (Rectangle){0, 0, (float)bloomTex.texture.width,
                               (float)bloomTex.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();

    EndShaderMode();
    EndTextureMode();

    // DOWNSAMPLE chain: đã loại bỏ các lệnh SetShaderValue nằm ngoài BeginShaderMode
    // để tránh ghi đè nhầm shader đang active trên backend rlvk.
    Texture2D prevTex = bloomTex.texture;
    int prevW = bloomTex.texture.width, prevH = bloomTex.texture.height;

    for (int i = 0; i < activeLevels; i++)
    {
      int dstW = dfTex[i].texture.width, dstH = dfTex[i].texture.height;
      float karis = (i == 0) ? s_bloomKaris : 0.0f;
      BeginTextureMode(dfTex[i]);
      DualFilterPass(dsShader, dsTexSizeLoc, prevTex, prevW, prevH, dstW, dstH,
                     config, karis, -1.0f);
      EndTextureMode();
      prevTex = dfTex[i].texture;
      prevW = dstW;
      prevH = dstH;
    }

    // UPSAMPLE chain
    float scatter = (s_bloomScatterOverride > 0.0f) ? s_bloomScatterOverride
                    : (config->bloomScatter > 0.0f) ? config->bloomScatter
                                                    : BLOOM_SCATTER_DEFAULT;
    if (scatter < 0.0f)
      scatter = 0.0f;
    else if (scatter > 1.0f)
      scatter = 1.0f;

    for (int i = activeLevels - 1; i >= 0; i--)
    {
      RenderTexture2D dst = (i == 0) ? bloomTex : dfTex[i - 1];
      int dstW = dst.texture.width, dstH = dst.texture.height;
      BeginTextureMode(dst);
      DualFilterPass(usShader, usTexSizeLoc, prevTex, prevW, prevH, dstW, dstH,
                     NULL, -1.0f, scatter);
      EndTextureMode();
      prevTex = dst.texture;
      prevW = dstW;
      prevH = dstH;
    }
  }

  // PASS 6: Composite → screen (hoặc ldrTex nếu bật FXAA)
  const bool useFxaa = (s_fxaa > 0.5f) && fxaaShader.id != 0 && ldrTex.id != 0;
  if (useFxaa) BeginTextureMode(ldrTex);

  BeginShaderMode(compositeShader);

  float bloomEnabledVal = (float)config->bloomEnabled;
  SetShaderValue(compositeShader, bloomEnabledLoc, &bloomEnabledVal, SHADER_UNIFORM_FLOAT);
  if (config->bloomEnabled)
  {
    SetShaderValueTexture(compositeShader, bloomTexLoc, bloomTex.texture);
    float bloomI = (s_bloomIntensityOverride > 0.0f) ? s_bloomIntensityOverride
                                                     : config->bloomIntensity;
    SetShaderValue(compositeShader, bloomIntensityLoc, &bloomI, SHADER_UNIFORM_FLOAT);
    if (bloomTexelLoc >= 0 && bloomTex.texture.width > 0 && bloomTex.texture.height > 0)
    {
      float bloomTexel[2] = {1.0f / (float)bloomTex.texture.width,
                             1.0f / (float)bloomTex.texture.height};
      SetShaderValue(compositeShader, bloomTexelLoc, bloomTexel, SHADER_UNIFORM_VEC2);
    }
  }

  float chromaticEnabledVal = (float)config->chromaticEnabled;
  SetShaderValue(compositeShader, chromaticEnabledLoc, &chromaticEnabledVal, SHADER_UNIFORM_FLOAT);
  if (config->chromaticEnabled)
  {
    SetShaderValue(compositeShader, chromaticStrengthLoc, &config->chromaticStrength, SHADER_UNIFORM_FLOAT);
  }

  float vignetteEnabledVal = (float)config->vignetteEnabled;
  SetShaderValue(compositeShader, vignetteEnabledLoc, &vignetteEnabledVal, SHADER_UNIFORM_FLOAT);
  if (config->vignetteEnabled)
  {
    SetShaderValue(compositeShader, vignetteRadiusLoc, &config->vignetteRadius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, vignetteSoftnessLoc, &config->vignetteSoftness, SHADER_UNIFORM_FLOAT);
  }

  float colorGradeEnabledVal = (float)(config->colorGradeEnabled || s_monochrome > 0.0f);
  SetShaderValue(compositeShader, colorGradeEnabledLoc, &colorGradeEnabledVal, SHADER_UNIFORM_FLOAT);
  if (colorGradeEnabledVal > 0.5f)
  {
    float rawSat = config->colorGradeEnabled ? config->saturation : 1.0f;
    // [TỐI ƯU VISUAL]: Giảm nhẹ hiện tượng cháy màu vàng-xanh trên cỏ của map ban ngày
    // khi caller truyền saturation = 1.55 từ cấu hình cũ của cảnh tối. Nếu người dùng
    // chủ động set postfx_saturation trong tuning.cfg thì dùng nguyên giá trị đó.
    if (s_saturationOverride <= 0.0f && rawSat > 1.26f)
    {
      rawSat = 1.26f + (rawSat - 1.26f) * 0.25f;
    }
    float saturationVal = (s_saturationOverride > 0.0f) ? s_saturationOverride : rawSat;
    saturationVal *= (1.0f - s_monochrome);

    float contrastVal = config->colorGradeEnabled
                            ? ((s_contrastOverride > 0.0f) ? s_contrastOverride : config->contrast)
                            : ((s_contrastOverride > 0.0f) ? s_contrastOverride : 1.0f);
    if (contrastVal <= 0.0f)
      contrastVal = 1.0f;
    SetShaderValue(compositeShader, contrastLoc, &contrastVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, saturationLoc, &saturationVal, SHADER_UNIFORM_FLOAT);

    Vector3 colorTint = config->colorGradeEnabled ? config->colorTint : (Vector3){1.0f, 1.0f, 1.0f};
    if (colorTint.x <= 0.0f && colorTint.y <= 0.0f && colorTint.z <= 0.0f)
      colorTint = (Vector3){1.0f, 1.0f, 1.0f};
    SetShaderValue(compositeShader, colorTintLoc, &colorTint, SHADER_UNIFORM_VEC3);

    Vector3 shadowTint = (config->shadowTint.x + config->shadowTint.y + config->shadowTint.z > 0.0f)
                             ? config->shadowTint
                             : (Vector3){1.0f, 1.0f, 1.0f};
    Vector3 highlightTint = (config->highlightTint.x + config->highlightTint.y + config->highlightTint.z > 0.0f)
                                ? config->highlightTint
                                : (Vector3){1.0f, 1.0f, 1.0f};
    SetShaderValue(compositeShader, shadowTintLoc, &shadowTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(compositeShader, highlightTintLoc, &highlightTint, SHADER_UNIFORM_VEC3);
  }

  float tonemapEnabledVal = (float)config->tonemapEnabled;
  float exposureVal = (config->exposure > 0.0f) ? config->exposure : 1.0f;
  SetShaderValue(compositeShader, tonemapEnabledLoc, &tonemapEnabledVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, exposureLoc, &exposureVal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, autoExposureLoc, &s_autoExposure, SHADER_UNIFORM_FLOAT);

  // [TỐI ƯU HIỆU NĂNG]: Chỉ truy vấn và bind exposureTex khi autoExposure thực sự bật,
  // tránh đánh thức chuỗi tính toán luma/exposure mỗi frame khi s_autoExposure == 0.
  if (s_autoExposure > 0.001f && exposureTexLoc >= 0)
  {
    Texture2D et = SceneTargets_GetExposureTexture();
    if (et.id != 0)
      SetShaderValueTexture(compositeShader, exposureTexLoc, et);
  }

  SetShaderValue(compositeShader, hueRestoreLoc, &s_hueRestore, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, shoulderViewLoc, &s_shoulderView, SHADER_UNIFORM_FLOAT);

  // G5 — LUT
  Texture2D lutTex = ColorGradeLut_Texture();
  float lutStrength = (s_lutStrengthOverride > 0.0f) ? s_lutStrengthOverride
                                                     : config->lutStrength;
  if (lutStrength < 0.0f)
    lutStrength = 0.0f;
  else if (lutStrength > 1.0f)
    lutStrength = 1.0f;
  float lutOn = (float)(config->lutEnabled && lutTex.id != 0 && lutStrength > 0.0f &&
                        !ColorGradeLut_IsNeutral());
  SetShaderValue(compositeShader, lutEnabledLoc, &lutOn, SHADER_UNIFORM_FLOAT);
  if (lutOn > 0.5f)
  {
    Vector2 lutParams = {1.0f / (float)(COLOR_GRADE_LUT_SIZE * COLOR_GRADE_LUT_SIZE),
                         1.0f / (float)COLOR_GRADE_LUT_SIZE};
    float lutSize = (float)COLOR_GRADE_LUT_SIZE;
    SetShaderValueTexture(compositeShader, lutTexLoc, lutTex);
    SetShaderValue(compositeShader, lutStrengthLoc, &lutStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, lutParamsLoc, &lutParams, SHADER_UNIFORM_VEC2);
    SetShaderValue(compositeShader, lutSizeLoc, &lutSize, SHADER_UNIFORM_FLOAT);
  }

  // E1a — radial blur
  float radialOn = (float)(config->radialBlurEnabled && config->radialBlurStrength > 0.0f);
  SetShaderValue(compositeShader, radialBlurEnabledLoc, &radialOn, SHADER_UNIFORM_FLOAT);
  if (radialOn > 0.5f)
  {
    Vector2 radialCenter = config->radialBlurCenter;
    float radialStrength = config->radialBlurStrength;
    float radialFalloff = (config->radialBlurFalloff > 0.0f) ? config->radialBlurFalloff : 0.45f;
    SetShaderValue(compositeShader, radialBlurCenterLoc, &radialCenter, SHADER_UNIFORM_VEC2);
    SetShaderValue(compositeShader, radialBlurStrengthLoc, &radialStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, radialBlurFalloffLoc, &radialFalloff, SHADER_UNIFORM_FLOAT);
  }

  const float outW = useFxaa ? (float)width : (float)GetRenderWidth();
  const float outH = useFxaa ? (float)height : (float)GetRenderHeight();
  rlDisableColorBlend();
  DrawTexturePro(srcTex,
                 (Rectangle){0, 0, (float)width, -(float)height},
                 (Rectangle){0, 0, outW, outH},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();

  EndShaderMode();

  if (useFxaa)
  {
    EndTextureMode();
    BeginShaderMode(fxaaShader);
    if (fxaaTexelLoc >= 0)
    {
      float texel[2] = {1.0f / (float)ldrTex.texture.width,
                        1.0f / (float)ldrTex.texture.height};
      SetShaderValue(fxaaShader, fxaaTexelLoc, texel, SHADER_UNIFORM_VEC2);
    }
    if (fxaaSharpenLoc >= 0)
    {
      float sharpen = s_sharpen;
      if (sharpen < 0.0f)
      {
        const float scale = RenderScale_GetScale();
        // [TỐI ƯU VISUAL]: Giữ mức làm nét nền nhẹ (0.045) ngay cả ở scale 1.0 để
        // bù lại độ mờ do FXAA gây ra trên các ngọn cỏ mảnh của map song_quao.
        sharpen = (scale > 0.01f) ? (0.045f + 0.38f * (1.0f / scale - 1.0f)) : 0.045f;
        if (sharpen > 0.25f)
          sharpen = 0.25f;
      }
      SetShaderValue(fxaaShader, fxaaSharpenLoc, &sharpen, SHADER_UNIFORM_FLOAT);
    }
    rlDisableColorBlend();
    DrawTexturePro(ldrTex.texture,
                   (Rectangle){0, 0, (float)ldrTex.texture.width,
                               -(float)ldrTex.texture.height},
                   (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
  }

  PostFX_PerfSample(config, width, height, activeLevels);
}

void PostFX_UseDirectSource(Texture2D sceneTex) { s_directSource = sceneTex; }

void PostFX_SetMonochrome(float intensity01)
{
  if (intensity01 < 0.0f)
    intensity01 = 0.0f;
  if (intensity01 > 1.0f)
    intensity01 = 1.0f;
  s_monochrome = intensity01;
}
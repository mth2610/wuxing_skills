#include "core/post_fx.h"
#include "core/tuning.h"
#include "core/screen_distort.h" // ScreenDistort_IsHDR — scene buffer is the HDR authority
#include "core/gfx_quality.h"    // E8 — the tier budget for the two new passes
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
// One slot, not a pool: this is a full-SCREEN effect, so two simultaneous
// bursts cannot both be shown — they would fight over one focal point and the
// result would read as a camera glitch. The strongest live burst wins.
static Vector3 s_burstWorldPos;
static float   s_burstStrength;   // peak strength as requested
static float   s_burstElapsed;
static float   s_burstDuration;   // 0 = no burst live
static Vector2 s_burstUV;         // projected screen UV, updated per frame
static float   s_burstCurrent;    // decayed strength this frame

// Uniform locations — bright pass
static int brightThresholdLoc;
static int brightMaxEnergyLoc;
static float s_bloomMaxEnergy = 4.0f;  // 4.0 = the shader's historical constant
// <=0 means "use whatever the caller configured" — so this knob can only ever
// be an explicit override, never a silent change to a caller's setting.
static float s_bloomIntensityOverride = 0.0f;

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
  brightMaxEnergyLoc = GetShaderLocation(brightShader, "u_maxEnergy");
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
// `streakCfg` non-NULL = this is the DOWNSAMPLE pass and it carries E1b's
// anamorphic uniforms. They are set INSIDE BeginShaderMode deliberately: under
// rlvk, SetShaderValue writes into whichever shader is ACTIVE, so setting them
// before the mode switch would silently land them on the previous shader.
static void DualFilterPass(Shader sh, int texSizeLoc, Texture2D src,
                           int srcW, int srcH, int dstW, int dstH,
                           const PostFXConfig *streakCfg)
{
  BeginShaderMode(sh);

  if (streakCfg != NULL && streakEnabledLoc >= 0)
  {
    float on = (float)streakCfg->bloomStreakEnabled;
    float str = streakCfg->bloomStreakStrength;
    float ang = streakCfg->bloomStreakAngle;
    if (str < 0.0f) str = 0.0f;
    else if (str > 1.0f) str = 1.0f;
    SetShaderValue(sh, streakEnabledLoc, &on, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, streakStrengthLoc, &str, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, streakAngleLoc, &ang, SHADER_UNIFORM_FLOAT);
  }

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

void PostFX_RadialBurst(Vector3 worldPos, float strength, float duration)
{
  if (strength <= 0.0f || duration <= 0.0f)
    return;

  // Strongest LIVE burst wins. Comparing against the decayed value, not the
  // stored peak, is deliberate: a burst that has almost finished should lose to
  // a fresh smaller one, otherwise a single big explosion locks out everything
  // that follows it for the rest of its duration.
  if (s_burstDuration > 0.0f && s_burstCurrent > strength)
    return;

  s_burstWorldPos = worldPos;
  s_burstStrength = strength;
  s_burstDuration = duration;
  s_burstElapsed  = 0.0f;
  s_burstCurrent  = strength;
}

void PostFX_UpdateTransient(Camera3D cam, float dt)
{
  if (s_burstDuration <= 0.0f)
    return;

  s_burstElapsed += dt;
  if (s_burstElapsed >= s_burstDuration)
  {
    s_burstDuration = 0.0f;
    s_burstCurrent  = 0.0f;
    return;
  }

  // Quadratic decay, same shape as CameraFX_Shake's trauma: a burst should hit
  // hard and leave quickly. Linear decay reads as a slow camera drift instead
  // of an impact.
  float k = 1.0f - (s_burstElapsed / s_burstDuration);
  s_burstCurrent = s_burstStrength * k * k;

  // Project to screen UV with the SAME camera the scene was rendered with —
  // a mismatch puts the sharp focal point somewhere the effect is not, which
  // looks like the blur is centred at random.
  Vector2 sp = GetWorldToScreen(s_burstWorldPos, cam);
  float sw = (float)GetScreenWidth();
  float sh_ = (float)GetScreenHeight();
  s_burstUV.x = (sw > 0.0f) ? (sp.x / sw) : 0.5f;
  // Flip Y: GetWorldToScreen is top-left origin, the composite samples in UV
  // space whose origin is bottom-left.
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
  config->radialBlurEnabled  = true;
  config->radialBlurCenter   = s_burstUV;
  config->radialBlurStrength = s_burstCurrent;
  // A focal region that stays sharp. Fixed rather than per-burst: it is a
  // framing decision, not a per-effect one, and exposing it invites every
  // caller to pick a different one.
  if (config->radialBlurFalloff <= 0.0f)
    config->radialBlurFalloff = 0.45f;
}

// Live authoring switches for the two E1 features (tuning.cfg, no rebuild).
// Both default to 0 = OFF, so the spec's "default OFF in every PostFXConfig
// initialiser" still holds — these only ever turn something ON that the caller
// left off, they never disable what a caller asked for. They exist because both
// features are pure look decisions that have to be judged by eye, and the
// alternative is a rebuild per adjustment (core/CLAUDE.md §5).
static float s_tuneStreak       = 0.0f;  // >0 = force streak on, value = strength
static float s_tuneStreakAngle  = 0.0f;  // radians
static float s_tuneRadial       = 0.0f;  // >0 = force a constant radial blur, value = strength
static bool  s_tuneReg          = false;

static void PostFX_ApplyTuning(PostFXConfig *c)
{
  if (!s_tuneReg)
  {
    s_tuneReg = true;
    Tuning_RegisterFloat("postfx_streak", &s_tuneStreak, 0.0f);
    Tuning_RegisterFloat("postfx_streak_angle", &s_tuneStreakAngle, 0.0f);
    Tuning_RegisterFloat("postfx_radial", &s_tuneRadial, 0.0f);
  }
  if (s_tuneStreak > 0.0f)
  {
    c->bloomStreakEnabled  = true;
    c->bloomStreakStrength = s_tuneStreak;
    c->bloomStreakAngle    = s_tuneStreakAngle;
  }
  if (s_tuneRadial > 0.0f)
  {
    c->radialBlurEnabled  = true;
    c->radialBlurStrength = s_tuneRadial;
    if (c->radialBlurCenter.x == 0.0f && c->radialBlurCenter.y == 0.0f)
      c->radialBlurCenter = (Vector2){0.5f, 0.5f};
    if (c->radialBlurFalloff <= 0.0f)
      c->radialBlurFalloff = 0.45f;
  }
}

// E8 — quality tiers. Radial blur and the streak tap are the two fill-hungry
// additions of Đợt E, and the Mali A33 is the binding constraint:
//   GFX_LOW  = neither    GFX_MED = streak only    GFX_HIGH = both
//
// THIS FUNCTION ONLY EVER CLAMPS DOWN. It can turn a feature off; it can never
// turn one on. That direction matters — the tier is a BUDGET, and a budget that
// can enable things is not a budget, it is a second configuration source that
// silently overrides the caller's intent. It runs last, after the transient and
// the tuning overrides, precisely so that neither can smuggle a HIGH-tier pass
// onto a LOW-tier device.
static void PostFX_ApplyQualityTier(PostFXConfig *c)
{
  GfxQuality q = GfxQuality_Get();

  bool allowStreak = (q >= GFX_MED);
  bool allowRadial = (q >= GFX_HIGH);

  bool cutStreak = c->bloomStreakEnabled && !allowStreak;
  bool cutRadial = c->radialBlurEnabled  && !allowRadial;

  if (!allowStreak) c->bloomStreakEnabled = false;
  if (!allowRadial) c->radialBlurEnabled  = false;

  // Announce on CHANGE, not once at startup: the tier is switchable at runtime
  // and `tuning.cfg` hot-reloads, so a one-shot line scrolls away long before
  // the values that matter arrive — and then "my postfx_radial edit did
  // nothing" is indistinguishable from "it applied and had no effect"
  // (core/CLAUDE.md §4).
  static int s_lastTier = -1;
  static int s_lastCut  = -1;
  int cutBits = (cutStreak ? 1 : 0) | (cutRadial ? 2 : 0);
  if ((int)q != s_lastTier || cutBits != s_lastCut)
  {
    s_lastTier = (int)q;
    s_lastCut  = cutBits;
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

// E8 step 3 — the instrument for "profile post-FX cost per frame".
//
// CPU wall-clock around the whole chain, not a GPU timer query: the chain is a
// handful of fullscreen draws, so what it actually costs is GPU fill, and the
// CPU time here is submission only. What this measures reliably is the SHAPE of
// the answer — which passes are on, at what resolution, and how the frame time
// moves when they are switched. That is the question E8 asks; a real per-pass
// GPU number needs timestamp queries and is a Renderer Agent job.
//
// Print once per second, and print WHAT WAS ON alongside the number, because a
// timing with no configuration attached cannot be compared against anything.
static float s_perfLog = 0.0f;      // tuning.cfg: postfx_perf_log = 1
static bool  s_perfReg = false;

static void PostFX_PerfSample(const PostFXConfig *c, int width, int height)
{
  if (!s_perfReg)
  {
    s_perfReg = true;
    Tuning_RegisterFloat("postfx_perf_log", &s_perfLog, 0.0f);
  }
  if (s_perfLog < 0.5f)
    return;

  static double s_accum = 0.0;
  static int    s_frames = 0;
  static double s_last = 0.0;

  double now = GetTime();
  s_accum += (double)GetFrameTime();
  s_frames++;
  if (now - s_last < 1.0 || s_frames == 0)
    return;

  float avgMs = (float)(s_accum / (double)s_frames * 1000.0);
  TraceLog(LOG_INFO,
           "POSTFX perf: %.2f ms/frame avg over %d frames at %dx%d | tier=%d "
           "bloom=%d streak=%d radial=%.2f tonemap=%d chroma=%d",
           avgMs, s_frames, width, height, (int)GfxQuality_Get(),
           c->bloomEnabled ? 1 : 0, c->bloomStreakEnabled ? 1 : 0,
           (c->radialBlurEnabled ? c->radialBlurStrength : 0.0f),
           c->tonemapEnabled ? 1 : 0, c->chromaticEnabled ? 1 : 0);

  s_accum = 0.0;
  s_frames = 0;
  s_last = now;
}

void PostFX_Draw(const PostFXConfig *config)
{
  // A live burst overrides whatever the caller configured. Copying rather than
  // mutating the caller's struct keeps PostFX_Draw's const contract honest.
  PostFXConfig local = *config;
  if (PostFX_HasTransient())
    PostFX_ApplyTransient(&local);
  PostFX_ApplyTuning(&local);
  PostFX_ApplyQualityTier(&local);   // last: the budget outranks both of the above
  config = &local;
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
    // Registered lazily, never from an Init — Tuning_Init runs after the
    // subsystem inits, so an early registration silently keeps the default
    // (core/docs/LANDMINES.md).
    static bool s_maxEnergyReg = false;
    if (!s_maxEnergyReg) {
      Tuning_RegisterFloat("bloom_max_energy", &s_bloomMaxEnergy, 4.0f);
      Tuning_RegisterFloat("bloom_intensity", &s_bloomIntensityOverride, 0.0f);
      s_maxEnergyReg = true;
    }
    SetShaderValue(brightShader, brightMaxEnergyLoc, &s_bloomMaxEnergy,
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
      DualFilterPass(dsShader, dsTexSizeLoc, prevTex, prevW, prevH, dstW, dstH, config);
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
      DualFilterPass(usShader, usTexSizeLoc, prevTex, prevW, prevH, dstW, dstH, NULL);
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
  // tuning.cfg -> bloom_intensity overrides the caller's value when set above
  // zero; at 0 the caller's setting is passed through untouched.
  float bloomI = (s_bloomIntensityOverride > 0.0f) ? s_bloomIntensityOverride
                                                   : config->bloomIntensity;
  SetShaderValue(compositeShader, bloomIntensityLoc, &bloomI, SHADER_UNIFORM_FLOAT);

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

  // E1a — radial blur. Strength 0 is treated as OFF as well as the bool, so a
  // fully-decayed burst costs nothing even if a caller leaves the flag set.
  float radialOn = (float)(config->radialBlurEnabled && config->radialBlurStrength > 0.0f);
  Vector2 radialCenter = config->radialBlurCenter;
  float radialStrength = config->radialBlurStrength;
  float radialFalloff = (config->radialBlurFalloff > 0.0f) ? config->radialBlurFalloff : 0.45f;
  SetShaderValue(compositeShader, radialBlurEnabledLoc, &radialOn, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, radialBlurCenterLoc, &radialCenter, SHADER_UNIFORM_VEC2);
  SetShaderValue(compositeShader, radialBlurStrengthLoc, &radialStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, radialBlurFalloffLoc, &radialFalloff, SHADER_UNIFORM_FLOAT);

  // [TỐI ƯU 1]: Áp dụng vô hiệu hóa blend cho composite cuối cùng
  // DrawTexturePro (dest = GetRenderWidth/Height) instead of DrawTextureRec's implicit 1:1:
  // mainRenderTex is sized to GetScreenWidth/Height (the logical window size), which is NOT
  // always the same as the real render/swapchain target size (rlvk/Vulkan on Android: the
  // display's full native resolution, no GL-style OS buffer upscale) - a 1:1 draw only covers
  // a sub-rectangle there, leaving the rest of the screen black. See RLVK_HANDOFF.md §7.14.
  rlDisableColorBlend();
  DrawTexturePro(mainRenderTex.texture,
                 (Rectangle){0, 0, (float)width, -(float)height},
                 (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlEnableColorBlend();

  EndShaderMode();

  PostFX_PerfSample(config, width, height);
}

void PostFX_SetMonochrome(float intensity01)
{
  if (intensity01 < 0.0f)
    intensity01 = 0.0f;
  if (intensity01 > 1.0f)
    intensity01 = 1.0f;
  s_monochrome = intensity01;
}

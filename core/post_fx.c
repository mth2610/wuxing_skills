#include "core/post_fx.h"
#include "core/tuning.h"
#include "core/scene_targets.h"
#include "core/screen_distort.h" // SceneTargets_IsHDR — scene buffer is the HDR authority
#include "core/gfx_quality.h"    // E8 — the tier budget for the two new passes
#include "core/color_grade_lut.h" // G5 — display-referred grading strip
#include "rlgl.h"
#include <string.h>

static RenderTexture2D mainRenderTex;
static RenderTexture2D bloomTex; // bright-pass output + final upsampled result (1/4)
/* Composite lands here instead of the swapchain when FXAA is on, so the AA pass has
   something to sample. LDR by definition: FXAA thresholds on perceptual luma and must
   run after the tone map. */
static RenderTexture2D ldrTex;
static Shader fxaaShader;
static int fxaaTexelLoc = -1;

// HDR (Đợt G) — the whole offscreen chain (scene + bloom pyramid) uses a
// 16-bit half-float color format so additive VFX / emissive can exceed 1.0 and
// survive until the composite pass tone-maps HDR→LDR. Probed once at init:
// GLES2 devices without EXT_color_buffer_half_float fall back to RGBA8 (the old
// LDR path) so nothing goes black on weak hardware. Query via PostFX_IsHDR().
static bool s_hdrActive = false;

/* Set for ONE frame by PostFX_UseDirectSource, and consumed by PostFX_Draw. */
static Texture2D s_directSource = {0};

// Bloom pyramid below the 1/4 bright-pass target: dfTex[0]=1/8 … dfTex[4]=1/128.
//
// Was 2 levels (stopping at 1/16). That is the single biggest reason the old
// glow read as "pasted on" rather than "radiating": the widest blur the chain
// could produce was one 1/16 texel across, so a bright core got a tight halo and
// nothing beyond it. AAA bloom gets its soft, far-reaching bleed from the deep
// end of the pyramid — 1/64 and 1/128 contribute almost no detail and almost
// all of the "rực rỡ". They are also nearly free: 1/128 of a 1080p frame is
// 15x8 texels.
#define DUAL_FILTER_LEVELS 5
static RenderTexture2D dfTex[DUAL_FILTER_LEVELS];
// How many levels actually got allocated. A small window (or a low-res mobile
// backbuffer) runs out of pixels before 5 halvings, and a 0-sized render
// texture is a driver-dependent failure rather than a black one, so the count
// is resolved at init and every loop below is bounded by it.
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
/* tuning.cfg -> postfx_auto_exposure. 0 = off (shipping default until the look is
   signed off); 1 = full. The metered value can only darken, so enabling it
   changes nothing in a night scene. */
static float s_autoExposure = 0.0f;
static int hueRestoreLoc;
static int shoulderViewLoc;
static int lutTexLoc;
static int lutEnabledLoc;
static int lutStrengthLoc;
static int lutParamsLoc;
static int lutSizeLoc;
static float s_lutStrengthOverride = 0.0f; // tuning.cfg -> lut_strength, 0 = caller's
/* The display-referred grade, live. These were the only post knobs with no
   tunable, which is why the pair below had never been re-examined: `saturation`
   ships at 1.28 with the note "ACES desaturates — lift richness back", written
   BEFORE the hue-preserving tone map existed (§12.1). Two corrections for the
   same loss are now stacked, and neither could be moved without a rebuild.
   0 = use the caller's value, same convention as the bloom overrides. */
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
static int brightExposureLoc;
static int brightMaxEnergyLoc;
static int brightKneeLoc;
static int brightSourceTexelSizeLoc;
// 0 = let the shader pick its own default (a SOFT ceiling at 12.0, not the old
// hard cut at 4.0 — see bloom_bright.fs). Only ever an explicit override.
static float s_bloomMaxEnergy = 0.0f;
// <=0 means "use whatever the caller configured" — so these knobs can only ever
// be an explicit override, never a silent change to a caller's setting.
static float s_bloomIntensityOverride = 0.0f;
static float s_bloomThresholdOverride = 0.0f;
static float s_bloomKnee = 0.0f;      // 0 = shader default
// Hue-preserving highlight restoration. SHIPPING AT 0.0 since 19/08/2026 — i.e. OFF, and
// the tone map is the plain per-channel ACES curve again.
//
// It shipped at 0.6 from 17/08 on a blind A/B and a careful set of measured gates. The
// gates were sound; what none of them could see is the assumption underneath the method.
// Hue restoration keeps a pixel's CHANNEL RATIO and tone-maps its PEAK, which silently
// assumes that ratio IS the emitter's colour. True only when the background contributes
// nothing — and every scene it was judged in was the night arena, where it does not.
//
// On bright scenery the pixel is background + emitter, so forcing the emitter's ratio onto
// the sum is arithmetically the same as SUBTRACTING the background. Measured on a white
// backdrop: (0.915, 0.877, 0.823) at 0 becomes (0.915, 0.762, 0.631) at 0.6 — R untouched,
// G and B pulled BELOW the background's own 0.804. A purely additive effect then reads as
// occluding, which is what the owner reported as the volume trail's see-through region
// disappearing. It also costs ~23% of internal structure at EVERY background luminance,
// because collapsing three channels onto one peak-mapped scalar flattens the differences
// between them, and those differences are the texture.
//
// The chroma it bought is recovered by the display-referred saturation below, which is
// about 8x cheaper in structure for the same chroma (+0.145 chroma for -0.011 structure,
// against hue restore's +0.085 for -0.052). main.c ships saturation at 1.55 for this.
//
// It is kept as a knob, not deleted: the METHOD is right, it is the STAGE that is wrong.
// Applied per-effect before compositing, or on a separate emission buffer, it would have
// the information it needs. Full record: BRIGHT_BACKGROUND_VFX_SPEC.md §12.1.
//
// Because this lives in tuning.cfg it PERSISTS ACROSS SESSIONS - check the file before
// trusting any visual A/B, and record its value alongside every capture.
static float s_hueRestore = 0.0f;
// Diagnostic overlay for §11b gate 3, which expires whenever the scene gets brighter.
static float s_shoulderView = 0.0f;
/* The scene rasterises into an offscreen HDR target, so raylib's MSAA hint (which
   applies to the window framebuffer) cannot antialias it and every silhouette in the
   game lands with binary coverage. Off = the old aliased output, for A/B. */
static float s_fxaa = 1.0f;
static float s_bloomScatterOverride = 0.0f;
static float s_bloomKaris = 1.0f;     // 1 = firefly weighting on (see below)

// Uniform locations — dual-filter passes
static int dsTexSizeLoc;
static int dsKarisLoc;
static int usTexSizeLoc;
static int usScatterLoc;

// How much of each upsampled level is mixed into the level above it.
// The upsample chain used to OVERWRITE its destination, which meant the final
// 1/4 buffer held nothing but the smallest mip stretched back up — every bit of
// near-range detail the pyramid had just computed was thrown away. It now
// lerps: dst = mix(dst, tent(src), scatter). 0 = only the tight near halo,
// 1 = only the widest haze; ~0.65 is the range that reads as a real lens.
// The lerp form is self-normalising, so a deeper pyramid does not double the
// total bloom energy the way an additive accumulate would.
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
  s_hdrActive = SceneTargets_IsHDR();
  const int colorFmt = s_hdrActive ? hdrFmt : ldrFmt;
  mainRenderTex = LoadRenderTextureWithFormat(width, height, colorFmt);
  TraceLog(LOG_INFO, "PostFX: %s pipeline (%s)", s_hdrActive ? "HDR float" : "LDR",
           s_hdrActive ? "R16G16B16A16" : "R8G8B8A8");

  bloomTex = LoadRenderTextureWithFormat(width / 4, height / 4, colorFmt);
  SetTextureFilter(bloomTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(bloomTex.texture, TEXTURE_WRAP_CLAMP);

  /* ALWAYS LDR, never colorFmt: this holds the tone-mapped result, and FXAA's contrast
     thresholds are perceptual. Clamped wrap so the 3x3 neighbourhood at the frame border
     cannot wrap around and invent an edge there. */
  ldrTex = LoadRenderTextureWithFormat(width, height, ldrFmt);
  SetTextureFilter(ldrTex.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(ldrTex.texture, TEXTURE_WRAP_CLAMP);

  int w = width / 4, h = height / 4;
  s_dfLevels = 0;
  for (int i = 0; i < DUAL_FILTER_LEVELS; i++)
  {
    w /= 2;
    h /= 2;
    // Stop before the pyramid degenerates. Below 4 texels the 13-tap
    // downsample's ±2-texel footprint is entirely outside the image and the
    // level contributes clamped edge colour, not blur.
    if (w < 4 || h < 4)
      break;
    dfTex[i] = LoadRenderTextureWithFormat(w, h, colorFmt);
    SetTextureFilter(dfTex[i].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(dfTex[i].texture, TEXTURE_WRAP_CLAMP);
    s_dfLevels++;
  }
  TraceLog(LOG_INFO, "PostFX: bloom pyramid %d levels (1/4 .. 1/%d)", s_dfLevels + 1,
           4 << s_dfLevels);

  brightShader = LoadShader(0, "core/shaders/bloom_bright.fs");
  dsShader = LoadShader(0, "core/shaders/bloom_downsample.fs");
  usShader = LoadShader(0, "core/shaders/bloom_upsample.fs");
  compositeShader = LoadShader(0, "core/shaders/post_process.fs");

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
  fxaaShader = LoadShader(0, "core/shaders/fxaa.fs");
  fxaaTexelLoc = GetShaderLocation(fxaaShader, "u_texel");
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
  UnloadRenderTexture(ldrTex);
  UnloadShader(fxaaShader);
  for (int i = 0; i < s_dfLevels; i++)
    UnloadRenderTexture(dfTex[i]);
  s_dfLevels = 0;

  UnloadShader(brightShader);
  UnloadShader(dsShader);
  UnloadShader(usShader);
  UnloadShader(compositeShader);
  ColorGradeLut_Unload();
}

void PostFX_Begin(void) { BeginTextureMode(mainRenderTex); }

void PostFX_End(void) { EndTextureMode(); }

// Helper: blit src into current render target (dstW×dstH), applying shader.
// Uses DrawTexturePro so src is scaled to fill dst — required when src and dst
// are different sizes (e.g. each level of the downsample/upsample pyramid).
// `streakCfg` non-NULL = this is the DOWNSAMPLE pass and it carries E1b's
// anamorphic uniforms plus the Karis firefly weight. They are set INSIDE
// BeginShaderMode deliberately: under rlvk, SetShaderValue writes into whichever
// shader is ACTIVE, so setting them before the mode switch would silently land
// them on the previous shader.
//
// `mixAlpha` <= 0 overwrites the destination (downsample: the destination holds
// nothing worth keeping). > 0 lerps onto what is already there — that is how the
// upsample chain folds a wide level into the tighter one above it instead of
// replacing it. The factor travels as the upsample shader's `u_scatter` uniform,
// which it emits as fragment alpha for BLEND_ALPHA to consume; it is NOT the
// draw tint, which would have to survive vertex-colour plumbing on both
// backends to mean anything.
static void DualFilterPass(Shader sh, int texSizeLoc, Texture2D src,
                           int srcW, int srcH, int dstW, int dstH,
                           const PostFXConfig *streakCfg, float karis,
                           float mixAlpha)
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
  if (streakCfg != NULL && dsKarisLoc >= 0)
    SetShaderValue(sh, dsKarisLoc, &karis, SHADER_UNIFORM_FLOAT);

  // [TỐI ƯU]: Chuyển sang ép kiểu nghịch đảo
  Vector2 ts = {1.0f / (float)srcW, 1.0f / (float)srcH};
  SetShaderValue(sh, texSizeLoc, &ts, SHADER_UNIFORM_VEC2);

  Rectangle srcRect = {0, 0, (float)srcW, (float)srcH};
  Rectangle dstRect = {0, 0, (float)dstW, (float)dstH};

  if (mixAlpha > 0.0f)
  {
    if (mixAlpha > 1.0f) mixAlpha = 1.0f;
    if (usScatterLoc >= 0)
      SetShaderValue(sh, usScatterLoc, &mixAlpha, SHADER_UNIFORM_FLOAT);
    // Straight (non-premultiplied) alpha: result = src.rgb*a + dst.rgb*(1-a),
    // i.e. an exact mix() with a = the fragment alpha the shader emits.
    BeginBlendMode(BLEND_ALPHA);
    DrawTexturePro(src, srcRect, dstRect, (Vector2){0, 0}, 0.0f, WHITE);
    EndBlendMode();
  }
  else
  {
    // [TỐI ƯU 1]: Tắt Alpha Blending để GPU ghi đè 100% pixel ảnh thay vì trộn màu
    //
    // AND FLUSH INSIDE THE WINDOW. rlDisableColorBlend() is flush-scoped on both
    // backends: without the flush the toggle never reaches the GPU before it is
    // undone, so this draw goes through the blender after all. It happens to be
    // harmless today only because bloom_downsample.fs writes a literal 1.0
    // alpha, which makes BLEND_ALPHA an exact no-op — i.e. the correctness of
    // this line currently lives in a different file that says nothing about it.
    // Same defect as the final composite below, where it was NOT harmless.
    // Found by core/tests/scene_target_alpha_contract_test.c.
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
           "bloom=%d/%dlv streak=%d radial=%.2f tonemap=%d chroma=%d",
           avgMs, s_frames, width, height, (int)GfxQuality_Get(),
           c->bloomEnabled ? 1 : 0, s_dfLevels + 1, c->bloomStreakEnabled ? 1 : 0,
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

  // Registered lazily, never from an Init — Tuning_Init runs after the
  // subsystem inits, so an early registration silently keeps the default
  // (core/docs/LANDMINES.md). Registered HERE rather than inside the bloom
  // branch below: a knob that only comes into existence when an unrelated
  // feature happens to be enabled is a knob that reads as broken.
  static bool s_tunablesReg = false;
  if (!s_tunablesReg) {
    Tuning_RegisterFloat("bloom_max_energy", &s_bloomMaxEnergy, 0.0f);
    Tuning_RegisterFloat("bloom_intensity", &s_bloomIntensityOverride, 0.0f);
    Tuning_RegisterFloat("bloom_threshold", &s_bloomThresholdOverride, 0.0f);
    Tuning_RegisterFloat("bloom_knee", &s_bloomKnee, 0.0f);
    Tuning_RegisterFloat("bloom_scatter", &s_bloomScatterOverride, 0.0f);
    Tuning_RegisterFloat("postfx_hue_restore", &s_hueRestore, 0.0f);
    Tuning_RegisterFloat("postfx_shoulder_view", &s_shoulderView, 0.0f);
    Tuning_RegisterFloat("postfx_fxaa", &s_fxaa, 1.0f);
    Tuning_RegisterFloat("postfx_auto_exposure", &s_autoExposure, 0.0f);
    Tuning_RegisterFloat("bloom_karis", &s_bloomKaris, 1.0f);
    Tuning_RegisterFloat("lut_strength", &s_lutStrengthOverride, 0.0f);
    Tuning_RegisterFloat("postfx_contrast", &s_contrastOverride, 0.0f);
    Tuning_RegisterFloat("postfx_saturation", &s_saturationOverride, 0.0f);
    s_tunablesReg = true;
  }
  config = &local;

  /* THE SOURCE. Normally PostFX's own full-resolution copy, which the distortion
     pass writes. On a frame with no live distortion source that copy is a
     read+write of a full-screen HDR target (~15 MB at 720p) performing an
     identity transform, so the frame loop points us straight at the scene
     target instead and skips PostFX_Begin/End altogether.

     Both targets are created at the same size, but the dimensions are taken
     from whichever is actually in use rather than assumed equal — a quality
     tier or a resize that changed one and not the other would otherwise scale
     the whole frame silently. */
  const Texture2D srcTex = (s_directSource.id != 0) ? s_directSource
                                                    : mainRenderTex.texture;
  s_directSource = (Texture2D){0};   // one frame only
  int width = srcTex.width;
  int height = srcTex.height;

  if (config->bloomEnabled)
  {
    // PASS 1: Extract bright pixels → bloomTex (1/4)
    BeginTextureMode(bloomTex);
    // [TỐI ƯU 1]: Xóa ClearBackground(BLACK) gây lãng phí bộ nhớ FBO
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
    // The target is quarter-resolution, but the bright prefilter must inspect
    // the full-resolution HDR scene so a thin emissive mesh does not disappear
    // before the bloom threshold ever sees it.
    Vector2 sourceTexelSize = {1.0f / (float)width, 1.0f / (float)height};
    SetShaderValue(brightShader, brightSourceTexelSizeLoc, &sourceTexelSize,
                   SHADER_UNIFORM_VEC2);

    rlDisableColorBlend();
    DrawTexturePro(srcTex,
                   (Rectangle){0, 0, (float)width, -(float)height},
                   (Rectangle){0, 0, (float)bloomTex.texture.width,
                               (float)bloomTex.texture.height},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();   // see the note at the final composite below
    rlEnableColorBlend();

    EndShaderMode();
    EndTextureMode();

    // DOWNSAMPLE chain: bloomTex(1/4) → dfTex[0](1/8) → … → dfTex[n-1](1/128).
    // Karis firefly weighting runs on the FIRST step only: that is the one fed
    // by the full-rate bright pass, where a single blazing texel can still be a
    // single texel. Every later level is already an average of averages, and
    // weighting those would just dim legitimately large bright regions.
    Texture2D prevTex = bloomTex.texture;
    int prevW = bloomTex.texture.width, prevH = bloomTex.texture.height;
    for (int i = 0; i < s_dfLevels; i++)
    {
      int dstW = dfTex[i].texture.width, dstH = dfTex[i].texture.height;
      float karis = (i == 0) ? s_bloomKaris : 0.0f;
      BeginTextureMode(dfTex[i]);
      // [TỐI ƯU 1]: Xóa ClearBackground(BLACK)
      DualFilterPass(dsShader, dsTexSizeLoc, prevTex, prevW, prevH, dstW, dstH,
                     config, karis, -1.0f);
      EndTextureMode();
      prevTex = dfTex[i].texture;
      prevW = dstW;
      prevH = dstH;
    }

    // UPSAMPLE chain, folding each level back into the one above it:
    //   dst = mix(dst, tent(src), scatter)
    // The mix is the whole point. Overwriting (what this used to do) discarded
    // every level except the smallest, so the pyramid's depth bought nothing.
    float scatter = (s_bloomScatterOverride > 0.0f) ? s_bloomScatterOverride
                    : (config->bloomScatter > 0.0f) ? config->bloomScatter
                                                    : BLOOM_SCATTER_DEFAULT;
    for (int i = s_dfLevels - 1; i >= 0; i--)
    {
      RenderTexture2D dst = (i == 0) ? bloomTex : dfTex[i - 1];
      int dstW = dst.texture.width, dstH = dst.texture.height;
      BeginTextureMode(dst);
      // [TỐI ƯU 1]: Xóa ClearBackground(BLACK)
      DualFilterPass(usShader, usTexSizeLoc, prevTex, prevW, prevH, dstW, dstH,
                     NULL, 0.0f, scatter);
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
  float saturationVal = config->colorGradeEnabled
                            ? ((s_saturationOverride > 0.0f) ? s_saturationOverride
                                                             : config->saturation)
                            : 1.0f;
  saturationVal *= (1.0f - s_monochrome);
  SetShaderValue(compositeShader, colorGradeEnabledLoc, &colorGradeEnabledVal, SHADER_UNIFORM_FLOAT);
  float contrastVal = (s_contrastOverride > 0.0f) ? s_contrastOverride : config->contrast;
  SetShaderValue(compositeShader, contrastLoc, &contrastVal, SHADER_UNIFORM_FLOAT);
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
  SetShaderValue(compositeShader, autoExposureLoc, &s_autoExposure, SHADER_UNIFORM_FLOAT);
  {
    Texture2D et = SceneTargets_GetExposureTexture();
    if (et.id != 0 && exposureTexLoc >= 0)
      SetShaderValueTexture(compositeShader, exposureTexLoc, et);
  }
  SetShaderValue(compositeShader, hueRestoreLoc, &s_hueRestore, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, shoulderViewLoc, &s_shoulderView, SHADER_UNIFORM_FLOAT);

  /* bloomTex is a QUARTER-resolution target, and the composite used to magnify it 4x
   * with a single bilinear fetch — which reconstructs as piecewise-linear patches that
   * kink every 4 screen pixels, i.e. visible stair-steps along any bright curved edge.
   * The composite now runs the same 3x3 tent the upsample chain uses, so it needs that
   * target's texel size. Computed from the live texture rather than from the window,
   * so a resize or a quality tier that changes the bloom resolution cannot desync it. */
  if (bloomTexelLoc >= 0 && bloomTex.texture.width > 0 && bloomTex.texture.height > 0) {
    float bloomTexel[2] = {1.0f / (float)bloomTex.texture.width,
                           1.0f / (float)bloomTex.texture.height};
    SetShaderValue(compositeShader, bloomTexelLoc, bloomTexel, SHADER_UNIFORM_VEC2);
  }

  // G5 — LUT. The texture is bound unconditionally: leaving a sampler unbound
  // while its branch is merely disabled is how you get a driver-dependent
  // "works here, black frame there", and the branch costs nothing when off.
  Texture2D lutTex = ColorGradeLut_Texture();
  float lutStrength = (s_lutStrengthOverride > 0.0f) ? s_lutStrengthOverride
                                                     : config->lutStrength;
  if (lutStrength < 0.0f) lutStrength = 0.0f;
  else if (lutStrength > 1.0f) lutStrength = 1.0f;
  // Gated on the LUT not being the identity strip: with no graded asset present
  // the branch stays off, so leaving lutEnabled on by default costs a disabled
  // uniform rather than a per-pixel tap-and-lerp that provably cannot change
  // anything. Mali is the reason that distinction is worth the extra condition.
  float lutOn = (float)(config->lutEnabled && lutTex.id != 0 && lutStrength > 0.0f &&
                        !ColorGradeLut_IsNeutral());
  Vector2 lutParams = {1.0f / (float)(COLOR_GRADE_LUT_SIZE * COLOR_GRADE_LUT_SIZE),
                       1.0f / (float)COLOR_GRADE_LUT_SIZE};
  float lutSize = (float)COLOR_GRADE_LUT_SIZE;
  if (lutTex.id != 0) SetShaderValueTexture(compositeShader, lutTexLoc, lutTex);
  SetShaderValue(compositeShader, lutEnabledLoc, &lutOn, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, lutStrengthLoc, &lutStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(compositeShader, lutParamsLoc, &lutParams, SHADER_UNIFORM_VEC2);
  SetShaderValue(compositeShader, lutSizeLoc, &lutSize, SHADER_UNIFORM_FLOAT);

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
  /* WITH FXAA the composite is no longer the last pass, so it lands in ldrTex at 1:1 and
     the AA pass does the swapchain-sized draw. Without it, nothing changes: the composite
     goes straight to the swapchain exactly as before, which is what keeps `postfx_fxaa=0`
     a true A/B against the old output rather than a different code path. */
  const bool useFxaa = (s_fxaa > 0.5f) && fxaaShader.id != 0 && ldrTex.id != 0;
  if (useFxaa) BeginTextureMode(ldrTex);
  const float outW = useFxaa ? (float)width : (float)GetRenderWidth();
  const float outH = useFxaa ? (float)height : (float)GetRenderHeight();
  rlDisableColorBlend();
  DrawTexturePro(srcTex,
                 (Rectangle){0, 0, (float)width, -(float)height},
                 (Rectangle){0, 0, outW, outH},
                 (Vector2){0, 0}, 0.0f, WHITE);
  // FLUSH INSIDE THE DISABLED WINDOW. rlDisableColorBlend() is flush-scoped on both
  // backends (it is glDisable(GL_BLEND) under GL and a pipeline-state bump under rlvk):
  // the toggle only reaches the GPU when the batch is drawn. Re-enabling before the
  // flush therefore hands the draw back to the blender, and this composite blends with
  // post_process.fs's own alpha - which is the HDR scene target's ACCUMULATED alpha.
  // Additive VFX push that above 1.0, so every VFX region came out multiplied by ~1.5
  // and clipped to white on the 8-bit swapchain: the exact "everything blows out" symptom
  // BRIGHT_BACKGROUND_VFX_SPEC.md exists to fix. Pinned by rlvk's colorblend_flush scenario.
  rlDrawRenderBatchActive();
  rlEnableColorBlend();

  EndShaderMode();

  if (useFxaa)
  {
    EndTextureMode();
    /* Source height NEGATIVE again, not positive. The composite above already wrote
       ldrTex with a flip, and this second flip is what returns the image to screen
       orientation — the same RT->RT convention the rest of this codebase uses. Getting
       this wrong shows up instantly as an upside-down frame, which is the cheap failure;
       the expensive one is a PARTIAL rect, where the flip has to be composed with a
       mirrored destination (see SceneTargets_SnapshotDepth). This draw is full-frame,
       so it does not have that problem. */
    BeginShaderMode(fxaaShader);
    if (fxaaTexelLoc >= 0)
    {
      float texel[2] = {1.0f / (float)ldrTex.texture.width,
                        1.0f / (float)ldrTex.texture.height};
      SetShaderValue(fxaaShader, fxaaTexelLoc, texel, SHADER_UNIFORM_VEC2);
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

  PostFX_PerfSample(config, width, height);
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

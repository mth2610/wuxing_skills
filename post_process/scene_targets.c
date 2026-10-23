/* SCENE TARGETS — the render targets the whole frame is built in. */
#include "post_process/scene_targets.h"
#include "core/vfx_render.h"
#include "post_process/render_target_probe.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>
#include <stdlib.h> // getenv/atoi (WUXING_NO_HDR, WUXING_MSAA)

#define SOFT_PARTICLE_SCENE_NEAR 1.0f
#define SOFT_PARTICLE_SCENE_FAR 1000.0f

static RenderTexture2D renderTex;
static Rectangle s_softDepthRegion;
static bool s_softDepthRegionValid;

// --- Soft particles: previous-frame depth snapshot ---
static RenderTexture2D prevDepthTex;
static Shader depthCopyShader;
static int depthCopyNearLoc;
static int depthCopyFarLoc;

// --- Refraction tap: sample-safe copy of the finished scene ---
static RenderTexture2D s_sceneSnapshot;
static bool s_sceneSnapshotRequested = false;

#define SOFT_PARTICLE_SHADER_CACHE_SIZE 8
typedef struct
{
  unsigned int shaderId;
  int depthLoc, nearLoc, farLoc, resLoc;
} SoftParticleShaderCacheEntry;
static SoftParticleShaderCacheEntry s_softCache[SOFT_PARTICLE_SHADER_CACHE_SIZE];
static int s_softCacheCount = 0;

static inline SoftParticleShaderCacheEntry *GetSoftParticleLocs(Shader shader)
{
  for (int i = 0; i < s_softCacheCount; i++)
  {
    if (s_softCache[i].shaderId == shader.id)
      return &s_softCache[i];
  }
  if (s_softCacheCount >= SOFT_PARTICLE_SHADER_CACHE_SIZE)
    return NULL;

  SoftParticleShaderCacheEntry *entry = &s_softCache[s_softCacheCount++];
  entry->shaderId = shader.id;
  entry->depthLoc = GetShaderLocation(shader, "u_cameraDepthTex");
  entry->nearLoc = GetShaderLocation(shader, "u_cameraNear");
  entry->farLoc = GetShaderLocation(shader, "u_cameraFar");
  entry->resLoc = GetShaderLocation(shader, "u_resolution");

  float nearVal = SOFT_PARTICLE_SCENE_NEAR;
  float farVal = SOFT_PARTICLE_SCENE_FAR;
  if (entry->nearLoc >= 0)
    SetShaderValue(shader, entry->nearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
  if (entry->farLoc >= 0)
    SetShaderValue(shader, entry->farLoc, &farVal, SHADER_UNIFORM_FLOAT);

  return entry;
}

static bool s_hdrActive = false;
static bool s_depthTextureActive = false;

static RenderTexture2D LoadRenderTextureWithDepthTexture(int width, int height, int colorFormat)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0)
  {
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(NULL, width, height, colorFormat, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = colorFormat;
    target.texture.mipmaps = 1;

    target.depth.id = rlLoadTextureDepth(width, height, false);
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19; // DEPTH_COMPONENT_24BIT
    target.depth.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

    if (!rlFramebufferComplete(target.id))
    {
      TraceLog(LOG_WARNING, "SceneTargets: depth-texture framebuffer incomplete");
    }
    rlDisableFramebuffer();
  }
  return target;
}

/* [TỐI ƯU HÓA FBO]: Target chỉ chứa Color attachment, không gắn Depth Texture 24-bit
 * dư thừa cho các pass 2D như s_bgLuma và s_exposure. */
static RenderTexture2D LoadColorOnlyTarget(int width, int height, int colorFormat)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0)
  {
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(NULL, width, height, colorFormat, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = colorFormat;
    target.texture.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    if (!rlFramebufferComplete(target.id))
    {
      TraceLog(LOG_WARNING, "SceneTargets: color-only framebuffer incomplete");
    }
    rlDisableFramebuffer();
  }
  return target;
}

#define SOFT_DEPTH_DOWNSCALE 2
#define BG_LUMA_DOWNSCALE 16
#define BG_LUMA_KEEPALIVE_FRAMES 15

static RenderTexture2D s_bgLuma;
static Shader s_bgLumaShader;
static int s_bgLumaTexelLoc = -1;
static bool s_bgLumaValid = false;
static int s_bgLumaIdleFrames = BG_LUMA_KEEPALIVE_FRAMES + 1;
static int s_bgLumaThrottle = 0;

static RenderTexture2D s_exposure[2];
static int s_exposureCur = 0;
static Shader s_exposureShader;
static int s_expLocLuma = -1, s_expLocSize = -1, s_expLocDt = -1;
static int s_expLocGrey = -1, s_expLocMin = -1, s_expLocDown = -1, s_expLocUp = -1;
static bool s_exposureReady = false;
static int s_probeFrame = 0;

static RenderTexture2D LoadLinearDepthTarget(int width, int height)
{
  RenderTexture2D target = LoadColorOnlyTarget(width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32);
  SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return target;
}

#if defined(GRAPHICS_API_VULKAN)
extern int rlvkSetFramebufferSamples(unsigned int fbId, int samples);
extern bool rlvkSetFramebufferDepthSampleOnDemand(unsigned int fbId, bool enabled);
extern bool rlvkRefreshFramebufferDepthTexture(unsigned int fbId);
#endif
static bool s_depthSampleOnDemand = false;
static int s_sceneSamples = 1;

int SceneTargets_GetSceneSamples(void) { return s_sceneSamples; }

static int s_targetWidth = 0;
static int s_targetHeight = 0;

static void SceneTargets_PrimeTarget(RenderTexture2D target)
{
  if (target.id == 0 || target.texture.id == 0)
    return;
  BeginTextureMode(target);
  ClearBackground(BLACK);
  EndTextureMode();
}

static void SceneTargets_DestroySizedTargets(void)
{
  rlDrawRenderBatchActive();
  if (renderTex.id > 0)
    UnloadRenderTexture(renderTex);
  renderTex = (RenderTexture2D){0};
  if (prevDepthTex.id > 0)
    UnloadRenderTexture(prevDepthTex);
  prevDepthTex = (RenderTexture2D){0};
  if (s_bgLuma.id > 0)
    UnloadRenderTexture(s_bgLuma);
  s_bgLuma = (RenderTexture2D){0};
  s_bgLumaValid = false;
}

static void SceneTargets_CreateSizedTargets(int width, int height)
{
  bool forceLdr = (getenv("WUXING_NO_HDR") != NULL);
  s_depthTextureActive = false;

  if (!forceLdr)
  {
    renderTex = LoadRenderTextureWithDepthTexture(width, height, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (renderTex.id > 0 && rlFramebufferComplete(renderTex.id))
    {
      s_hdrActive = true;
      s_depthTextureActive = true;
      TraceLog(LOG_INFO, "SceneTargets: HDR float scene buffer active (R16G16B16A16)");
    }
    else
    {
      if (renderTex.id > 0)
        UnloadRenderTexture(renderTex);
      s_hdrActive = false;
    }
  }
  else
  {
    s_hdrActive = false;
  }

  if (!s_depthTextureActive)
  {
    renderTex = LoadRenderTextureWithDepthTexture(width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (renderTex.id > 0 && rlFramebufferComplete(renderTex.id))
    {
      s_depthTextureActive = true;
      TraceLog(LOG_INFO, "SceneTargets: LDR depth-texture scene buffer active (RGBA8)");
    }
    else
    {
      if (renderTex.id > 0)
        UnloadRenderTexture(renderTex);
      renderTex = LoadRenderTexture(width, height);
      s_depthTextureActive = false;
      TraceLog(LOG_WARNING, "SceneTargets: depth-texture unsupported, falling back to standard FBO (no depth sampling)");
    }
  }

  if (s_depthTextureActive)
  {
    int softDepthWidth = (width + SOFT_DEPTH_DOWNSCALE - 1) / SOFT_DEPTH_DOWNSCALE;
    int softDepthHeight = (height + SOFT_DEPTH_DOWNSCALE - 1) / SOFT_DEPTH_DOWNSCALE;
    prevDepthTex = LoadLinearDepthTarget(softDepthWidth, softDepthHeight);
    if (prevDepthTex.id == 0 || !rlFramebufferComplete(prevDepthTex.id))
    {
      TraceLog(LOG_WARNING, "SceneTargets: linear-depth framebuffer incomplete, falling back to standard FBO");
      if (prevDepthTex.id > 0)
        UnloadRenderTexture(prevDepthTex);
      prevDepthTex = (RenderTexture2D){0};

      UnloadRenderTexture(renderTex);
      renderTex = LoadRenderTexture(width, height);
      s_depthTextureActive = false;
    }
  }
  else
  {
    prevDepthTex = (RenderTexture2D){0};
  }

  s_sceneSamples = 1;
#if defined(GRAPHICS_API_VULKAN)
  if (renderTex.id > 0)
  {
    const char *msaaEnv = getenv("WUXING_MSAA");
    const int wanted = (msaaEnv != NULL) ? atoi(msaaEnv) : 1;
    if (wanted > 1)
      s_sceneSamples = rlvkSetFramebufferSamples(renderTex.id, wanted);
  }
#endif
  TraceLog(LOG_INFO, "SceneTargets: scene target MSAA x%d", s_sceneSamples);
  {
    int lw = (width + BG_LUMA_DOWNSCALE - 1) / BG_LUMA_DOWNSCALE;
    int lh = (height + BG_LUMA_DOWNSCALE - 1) / BG_LUMA_DOWNSCALE;
    if (lw < 4)
      lw = 4;
    if (lh < 4)
      lh = 4;
    s_bgLuma = LoadColorOnlyTarget(lw, lh, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    SetTextureFilter(s_bgLuma.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_bgLuma.texture, TEXTURE_WRAP_CLAMP);
    SceneTargets_PrimeTarget(s_bgLuma);
    s_bgLumaValid = false;
    TraceLog(LOG_INFO, "SceneTargets: background-luma target %dx%d (color-only)", lw, lh);
  }

  SceneTargets_PrimeTarget(renderTex);
  SceneTargets_PrimeTarget(prevDepthTex);
}

void SceneTargets_Init(int width, int height)
{
  s_depthSampleOnDemand = false;
  s_targetWidth = width;
  s_targetHeight = height;
  SceneTargets_CreateSizedTargets(width, height);

  s_bgLumaShader = LoadShader(0, "core/shaders/bg_luma.fs");
  s_bgLumaTexelLoc = GetShaderLocation(s_bgLumaShader, "u_srcTexel");

  for (int i = 0; i < 2; i++)
  {
    s_exposure[i] = LoadColorOnlyTarget(1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    SetTextureFilter(s_exposure[i].texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(s_exposure[i]);
    ClearBackground((Color){255, 255, 255, 255});
    EndTextureMode();
  }
  s_exposureReady = true;

  s_exposureShader = LoadShader(0, "core/shaders/exposure.fs");
  s_expLocLuma = GetShaderLocation(s_exposureShader, "u_lumaTex");
  s_expLocSize = GetShaderLocation(s_exposureShader, "u_lumaSize");
  s_expLocDt = GetShaderLocation(s_exposureShader, "u_dt");
  s_expLocGrey = GetShaderLocation(s_exposureShader, "u_targetGrey");
  s_expLocMin = GetShaderLocation(s_exposureShader, "u_minExposure");
  s_expLocDown = GetShaderLocation(s_exposureShader, "u_speedDown");
  s_expLocUp = GetShaderLocation(s_exposureShader, "u_speedUp");

  depthCopyShader = LoadShader(0, "core/shaders/depth_copy.fs");
  depthCopyNearLoc = GetShaderLocation(depthCopyShader, "u_near");
  depthCopyFarLoc = GetShaderLocation(depthCopyShader, "u_far");

  float nearVal = SOFT_PARTICLE_SCENE_NEAR;
  float farVal = SOFT_PARTICLE_SCENE_FAR;
  if (depthCopyNearLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyNearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
  if (depthCopyFarLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyFarLoc, &farVal, SHADER_UNIFORM_FLOAT);

  s_softCacheCount = 0;
}

void SceneTargets_Resize(int width, int height)
{
  if (width < 8 || height < 8)
    return;
  if (width == s_targetWidth && height == s_targetHeight && renderTex.id > 0)
    return;

  if (s_sceneSnapshot.id > 0)
    UnloadRenderTexture(s_sceneSnapshot);
  s_sceneSnapshot = (RenderTexture2D){0};
  s_sceneSnapshotRequested = false;

  SceneTargets_DestroySizedTargets();
  s_targetWidth = width;
  s_targetHeight = height;
  SceneTargets_CreateSizedTargets(width, height);
  TraceLog(LOG_INFO, "SceneTargets: scene chain resized to %dx%d", width, height);
}

int SceneTargets_GetWidth(void) { return s_targetWidth; }
int SceneTargets_GetHeight(void) { return s_targetHeight; }

bool SceneTargets_IsHDR(void) { return s_hdrActive; }

void SceneTargets_Unload(void)
{
  SceneTargets_DestroySizedTargets();
  for (int i = 0; i < 2; i++)
  {
    if (s_exposure[i].id > 0)
      UnloadRenderTexture(s_exposure[i]);
    s_exposure[i] = (RenderTexture2D){0};
  }
  s_exposureReady = false;

  if (s_sceneSnapshot.id > 0)
    UnloadRenderTexture(s_sceneSnapshot);
  s_sceneSnapshot = (RenderTexture2D){0};
  s_sceneSnapshotRequested = false;

  if (s_bgLumaShader.id > 0)
    UnloadShader(s_bgLumaShader);
  s_bgLumaShader = (Shader){0};
  if (s_exposureShader.id > 0)
    UnloadShader(s_exposureShader);
  s_exposureShader = (Shader){0};
  if (depthCopyShader.id > 0)
    UnloadShader(depthCopyShader);
  depthCopyShader = (Shader){0};
  s_softCacheCount = 0;
}

void SceneTargets_Begin(void)
{
  BeginTextureMode(renderTex);
  s_softDepthRegionValid = false;
}

void SceneTargets_End(void)
{
  EndTextureMode();
  if (RenderTargetProbe_MatchesFrame(s_probeFrame))
    RenderTargetProbe_Dump(renderTex, "scene", true);
  if (RenderTargetProbe_MatchesFrame(s_probeFrame) && s_bgLumaValid)
    RenderTargetProbe_Dump(s_bgLuma, "bgluma", false);
  if (RenderTargetProbe_MatchesFrame(s_probeFrame) && s_exposureReady)
    RenderTargetProbe_Dump(s_exposure[s_exposureCur], "exposure", false);
  s_probeFrame++;
}

void SceneTargets_BeginVFXBody(void)
{
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(renderTex.id);
}
void SceneTargets_BeginVFXEmission(void)
{
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(renderTex.id);
}
void SceneTargets_EndVFXLayer(void)
{
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(renderTex.id);
}

void SceneTargets_BeginWorldRefraction(void)
{
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(renderTex.id);
  rlViewport(0, 0, s_targetWidth, s_targetHeight);
}

void SceneTargets_EndWorldRefraction(void)
{
  rlDrawRenderBatchActive();
  rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}

void VFXRender_BeginPass(VFXRenderPass pass)
{
  if (pass == VFX_RENDER_PASS_EMISSION)
    SceneTargets_BeginVFXEmission();
  else
    SceneTargets_BeginVFXBody();
}

void VFXRender_EndPass(void)
{
  SceneTargets_EndVFXLayer();
}

bool VFXRender_AppearanceDrawsPass(VFXResolvedAppearance appearance,
                                   VFXRenderPass pass)
{
  return pass == VFX_RENDER_PASS_EMISSION
             ? VFXResolvedAppearance_UsesEmission(appearance)
             : VFXResolvedAppearance_UsesBody(appearance);
}

const char *VFXRender_OutputDefines(VFXSurfaceMode surface)
{
  switch (surface)
  {
  case VFX_SURFACE_ADDITIVE:
    return "#define OUTPUT_EMISSION 1\n";
  case VFX_SURFACE_PREMULTIPLIED:
    return "#define OUTPUT_PREMULTIPLIED 1\n";
  case VFX_SURFACE_ALPHA:
  default:
    return "#define OUTPUT_BODY 1\n";
  }
}

VFXContrastLayer VFXRender_ContrastLayer(VFXRenderPass pass)
{
  return pass == VFX_RENDER_PASS_EMISSION
             ? VFX_CONTRAST_EMISSION
             : VFX_CONTRAST_BODY;
}

VFXRenderScope VFXRender_BeginDraw(VFXRenderPass pass,
                                   VFXSurfaceMode surface,
                                   bool depthWrite)
{
  VFXRenderScope scope = {true, depthWrite, pass, surface};
  VFXRender_BeginPass(pass);
  rlDrawRenderBatchActive();
  if (depthWrite)
    rlEnableDepthMask();
  else
    rlDisableDepthMask();
  BeginBlendMode(surface == VFX_SURFACE_ADDITIVE
                     ? BLEND_ADDITIVE
                     : (surface == VFX_SURFACE_PREMULTIPLIED
                            ? BLEND_ALPHA_PREMULTIPLY
                            : BLEND_ALPHA));
  rlDrawRenderBatchActive();
  return scope;
}

VFXRenderScope VFXRender_BeginAppearance(VFXRenderPass pass,
                                         VFXAppearanceId appearanceId,
                                         VFXResolvedAppearance legacy,
                                         bool depthWrite,
                                         VFXResolvedAppearance *outResolved)
{
  VFXResolvedAppearance resolved = VFXAppearance_Resolve(appearanceId, legacy);
  if (outResolved != NULL)
    *outResolved = resolved;
  if (!VFXRender_AppearanceDrawsPass(resolved, pass))
    return (VFXRenderScope){0};
  VFXSurfaceMode passSurface = resolved.surface;
  return VFXRender_BeginDraw(pass, passSurface, depthWrite);
}

void VFXRender_EndDraw(VFXRenderScope *scope)
{
  if (scope == NULL || !scope->active)
    return;
  rlDrawRenderBatchActive();
  EndBlendMode();
  rlEnableDepthMask();
  rlDrawRenderBatchActive();
  VFXRender_EndPass();
  scope->active = false;
}

#define SOFT_DEPTH_KEEPALIVE_FRAMES 20
static int s_softDepthIdleFrames = SOFT_DEPTH_KEEPALIVE_FRAMES + 1;

Rectangle SceneTargets_WindowRectToTarget(Rectangle windowRect)
{
  const float sx = (GetScreenWidth() > 0 && s_targetWidth > 0)
                       ? (float)s_targetWidth / (float)GetScreenWidth()
                       : 1.0f;
  const float sy = (GetScreenHeight() > 0 && s_targetHeight > 0)
                       ? (float)s_targetHeight / (float)GetScreenHeight()
                       : 1.0f;
  return (Rectangle){windowRect.x * sx, windowRect.y * sy,
                     windowRect.width * sx, windowRect.height * sy};
}

void SceneTargets_RequestSoftDepthRegion(Rectangle screenRegion)
{
  s_softDepthIdleFrames = 0;
  const float maxW = (float)renderTex.texture.width;
  const float maxH = (float)renderTex.texture.height;
  if (screenRegion.width <= 0.0f || screenRegion.height <= 0.0f || maxW <= 0.0f || maxH <= 0.0f)
    return;
  const float margin = 48.0f;
  float x0 = fmaxf(0.0f, screenRegion.x - margin);
  float y0 = fmaxf(0.0f, screenRegion.y - margin);
  float x1 = fminf(maxW, screenRegion.x + screenRegion.width + margin);
  float y1 = fminf(maxH, screenRegion.y + screenRegion.height + margin);
  if (x1 <= x0 || y1 <= y0)
    return;
  if (!s_softDepthRegionValid)
  {
    s_softDepthRegion = (Rectangle){x0, y0, x1 - x0, y1 - y0};
    s_softDepthRegionValid = true;
  }
  else
  {
    float oldX1 = s_softDepthRegion.x + s_softDepthRegion.width;
    float oldY1 = s_softDepthRegion.y + s_softDepthRegion.height;
    s_softDepthRegion.x = fminf(s_softDepthRegion.x, x0);
    s_softDepthRegion.y = fminf(s_softDepthRegion.y, y0);
    s_softDepthRegion.width = fmaxf(oldX1, x1) - s_softDepthRegion.x;
    s_softDepthRegion.height = fmaxf(oldY1, y1) - s_softDepthRegion.y;
  }
}

void SceneTargets_RequestSceneSnapshot(void)
{
  s_sceneSnapshotRequested = true;
  if (renderTex.texture.width > 0 && renderTex.texture.height > 0)
    SceneTargets_RequestSoftDepthRegion(
        (Rectangle){0, 0, (float)renderTex.texture.width,
                    (float)renderTex.texture.height});
  if (s_softDepthIdleFrames > 0)
    s_softDepthIdleFrames = 0;
}

static RenderTexture2D LoadSceneSnapshotTarget(int width, int height, int format)
{
  RenderTexture2D target = LoadColorOnlyTarget(width, height, format);
  SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return target;
}

void SceneTargets_SnapshotScene(void)
{
  if (!s_sceneSnapshotRequested)
    return;
  s_sceneSnapshotRequested = false;
  if (renderTex.id == 0 || renderTex.texture.id == 0)
    return;

  const int w = renderTex.texture.width;
  const int h = renderTex.texture.height;
  if (s_sceneSnapshot.id == 0 || s_sceneSnapshot.texture.width != w ||
      s_sceneSnapshot.texture.height != h ||
      s_sceneSnapshot.texture.format != renderTex.texture.format)
  {
    if (s_sceneSnapshot.id)
      UnloadRenderTexture(s_sceneSnapshot);
    s_sceneSnapshot = LoadSceneSnapshotTarget(w, h, renderTex.texture.format);
  }
  if (s_sceneSnapshot.id == 0)
    return;

  BeginTextureMode(s_sceneSnapshot);
  rlDisableColorBlend();
  DrawTextureRec(renderTex.texture,
                 (Rectangle){0, 0, (float)w, -(float)h}, (Vector2){0, 0}, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndTextureMode();
}

Texture2D SceneTargets_GetSceneSnapshotTexture(void) { return s_sceneSnapshot.texture; }

/* [TỐI ƯU HÓA NGẮT RENDERPASS]: Chỉ chụp s_bgLuma khi có hệ thống yêu cầu
 * (VFX gọi GetBackgroundLuma, AutoExposure gọi GetExposureTexture, frame đầu
 * tiên sau khi khởi tạo/resize, hoặc frame đang chạy RenderTargetProbe).
 * Khi không có VFX nào cần, tránh đóng/mở lại renderTex giữa chừng! */
void SceneTargets_CaptureBackgroundLuma(void)
{
  if (s_bgLuma.id == 0 || s_bgLumaShader.id == 0)
    return;
  const int lw = s_bgLuma.texture.width, lh = s_bgLuma.texture.height;
  const int sw = renderTex.texture.width, sh = renderTex.texture.height;
  if (lw <= 0 || sw <= 0)
    return;

  const bool probeActive = RenderTargetProbe_MatchesFrame(s_probeFrame);
  if (s_bgLumaValid && !probeActive)
  {
    if (s_bgLumaIdleFrames > BG_LUMA_KEEPALIVE_FRAMES)
    {
      // Đảm bảo framebuffer và viewport vẫn trỏ về renderTex cho phần còn lại của frame
      rlEnableFramebuffer(renderTex.id);
      rlViewport(0, 0, sw, sh);
      return;
    }
    s_bgLumaIdleFrames++;
    // Khi đang active (nhưng không phải frame probe), cập nhật cách nhịp 2 frame/lần
    s_bgLumaThrottle = (s_bgLumaThrottle + 1) & 1;
    if (s_bgLumaThrottle != 0)
    {
      rlEnableFramebuffer(renderTex.id);
      rlViewport(0, 0, sw, sh);
      return;
    }
  }

  BeginTextureMode(s_bgLuma);
  BeginShaderMode(s_bgLumaShader);
  if (s_bgLumaTexelLoc >= 0)
  {
    float texel[2] = {1.0f / (float)sw, 1.0f / (float)sh};
    SetShaderValue(s_bgLumaShader, s_bgLumaTexelLoc, texel, SHADER_UNIFORM_VEC2);
  }
  rlDisableColorBlend();
  DrawTexturePro(renderTex.texture,
                 (Rectangle){0, 0, (float)sw, -(float)sh},
                 (Rectangle){0, 0, (float)lw, (float)lh},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndShaderMode();
  EndTextureMode();

  rlEnableFramebuffer(renderTex.id);
  rlViewport(0, 0, sw, sh);
  s_bgLumaValid = true;
}

Texture2D SceneTargets_GetBackgroundLuma(void)
{
  s_bgLumaIdleFrames = 0;
  return s_bgLumaValid ? s_bgLuma.texture : (Texture2D){0};
}

void SceneTargets_UpdateExposure(float dt, float targetGrey, float minExposure,
                                 float speedDown, float speedUp)
{
  if (s_exposureShader.id == 0 || !s_bgLumaValid)
    return;

  const bool probeActive = RenderTargetProbe_MatchesFrame(s_probeFrame - 1);
  if (s_exposureReady && !probeActive && s_bgLumaIdleFrames > BG_LUMA_KEEPALIVE_FRAMES)
    return;

  const int dst = 1 - s_exposureCur;

  BeginTextureMode(s_exposure[dst]);
  BeginShaderMode(s_exposureShader);
  float sz[2] = {(float)s_bgLuma.texture.width, (float)s_bgLuma.texture.height};
  if (s_expLocLuma >= 0)
    SetShaderValueTexture(s_exposureShader, s_expLocLuma, s_bgLuma.texture);
  if (s_expLocSize >= 0)
    SetShaderValue(s_exposureShader, s_expLocSize, sz, SHADER_UNIFORM_VEC2);
  if (s_expLocDt >= 0)
    SetShaderValue(s_exposureShader, s_expLocDt, &dt, SHADER_UNIFORM_FLOAT);
  if (s_expLocGrey >= 0)
    SetShaderValue(s_exposureShader, s_expLocGrey, &targetGrey, SHADER_UNIFORM_FLOAT);
  if (s_expLocMin >= 0)
    SetShaderValue(s_exposureShader, s_expLocMin, &minExposure, SHADER_UNIFORM_FLOAT);
  if (s_expLocDown >= 0)
    SetShaderValue(s_exposureShader, s_expLocDown, &speedDown, SHADER_UNIFORM_FLOAT);
  if (s_expLocUp >= 0)
    SetShaderValue(s_exposureShader, s_expLocUp, &speedUp, SHADER_UNIFORM_FLOAT);
  rlDisableColorBlend();
  DrawTexturePro(s_exposure[s_exposureCur].texture,
                 (Rectangle){0, 0, 1, -1}, (Rectangle){0, 0, 1, 1},
                 (Vector2){0, 0}, 0.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndShaderMode();
  EndTextureMode();

  s_exposureCur = dst;
  s_exposureReady = true;
}

Texture2D SceneTargets_GetExposureTexture(void)
{
  s_bgLumaIdleFrames = 0;
  return s_exposureReady ? s_exposure[s_exposureCur].texture : (Texture2D){0};
}

void SceneTargets_SnapshotDepth(void)
{
  if (!s_depthTextureActive)
    return;
  if (s_softDepthIdleFrames > SOFT_DEPTH_KEEPALIVE_FRAMES)
    return;
  if (!s_softDepthRegionValid)
    return;
  s_softDepthIdleFrames++;

  BeginTextureMode(prevDepthTex);
  BeginShaderMode(depthCopyShader);
  const float regionBottom = s_softDepthRegion.y + s_softDepthRegion.height;
  const float mirroredY = (float)renderTex.texture.height - regionBottom;
  Rectangle source = {s_softDepthRegion.x, s_softDepthRegion.y,
                      s_softDepthRegion.width, -s_softDepthRegion.height};
  Rectangle destination = {s_softDepthRegion.x / SOFT_DEPTH_DOWNSCALE,
                           mirroredY / SOFT_DEPTH_DOWNSCALE,
                           s_softDepthRegion.width / SOFT_DEPTH_DOWNSCALE,
                           s_softDepthRegion.height / SOFT_DEPTH_DOWNSCALE};
  rlDisableColorBlend();
  DrawTexturePro(renderTex.depth, source, destination, (Vector2){0, 0}, 0.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndShaderMode();
  EndTextureMode();
}

Texture2D SceneTargets_GetDepthTexture(void) { return prevDepthTex.texture; }
Texture2D SceneTargets_GetSceneTexture(void) { return renderTex.texture; }
bool SceneTargets_PrepareRawDepth(void)
{
  if (!s_depthTextureActive)
    return false;
#if defined(GRAPHICS_API_VULKAN)
  if (s_depthSampleOnDemand)
    return rlvkRefreshFramebufferDepthTexture(renderTex.id);
#endif
  return true;
}
Texture2D SceneTargets_GetRawDepthTexture(void) { return s_depthTextureActive ? renderTex.depth : (Texture2D){0}; }

void SceneTargets_BindDepthForSoftParticles(Shader shader, int textureSlot)
{
  if (!s_depthTextureActive)
    return;
  s_softDepthIdleFrames = 0;
  if (shader.id == 0 || shader.locs == NULL)
    return;

  SoftParticleShaderCacheEntry *locs = GetSoftParticleLocs(shader);
  if (locs == NULL)
    return;

  if (locs->depthLoc >= 0)
  {
    SetShaderValue(shader, locs->depthLoc, &textureSlot, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(textureSlot);
    rlEnableTexture(prevDepthTex.texture.id);
  }

  if (locs->resLoc >= 0)
  {
    Vector2 res = {(float)renderTex.texture.width, (float)renderTex.texture.height};
    SetShaderValue(shader, locs->resLoc, &res, SHADER_UNIFORM_VEC2);
  }
}

void SceneTargets_UnbindSoftParticleDepth(int textureSlot)
{
  (void)textureSlot;
}
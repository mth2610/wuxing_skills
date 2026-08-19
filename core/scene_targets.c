/* SCENE TARGETS — the render targets the whole frame is built in.
 *
 * This is the module ScreenDistort had grown into. Of ~140 references to
 * `ScreenDistort_*` across 24 files, only 19 were about distortion; the rest
 * asked it for the scene colour buffer, the scene depth, the previous frame's
 * depth snapshot, the refraction copy, the HDR flag, the MSAA sample count, or
 * the VFX draw target. A module whose name describes an effect but whose API is
 * mostly render-target ownership costs every reader the same detour, and the
 * long landmine comments in both this file and post_fx.c existed to pay it.
 *
 * So the ownership lives here and ScreenDistort keeps what its name says: a
 * list of shockwave sources and one full-screen pass that reads this module's
 * colour target.
 *
 * WHAT THIS OWNS
 *   renderTex        the scene: HDR (or LDR fallback) colour + a sampleable
 *                    depth texture. THE authoritative HDR decision — PostFX
 *                    matches it, never the other way round.
 *   prevDepthTex     last frame's linearised depth, half-res, for soft particles
 *   s_sceneSnapshot  a sample-safe copy of the finished scene, for refraction
 *
 * ALPHA IN THE SCENE TARGET IS UNDEFINED. Additive VFX push it past 1.0 and
 * nothing consumes it, so any pass that composites this target must disable
 * blending — see the flush note in post_fx.c, which is the bug that rule exists
 * to prevent.
 */
#include "core/scene_targets.h"
#include "core/vfx_render.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>
#include <stdlib.h> // getenv/atoi (WUXING_NO_HDR, WUXING_MSAA)

// CORE_ISSUES.md Item 3 rebuild — root cause #2: the soft-particle depth
// linearization must use the SAME near/far the scene was actually rendered
// with. rlGetCullDistanceNear/Far() reflects rlSetClipPlanes() (main.c),
// which is a DIFFERENT, unrelated global from the near plane
// MyBeginMode3D's rlFrustum() actually uses for perspective projection (see
// main.c). Using the wrong one silently produces near-zero linear depth for
// all real scene content. Keep these in sync with MyBeginMode3D's
// rlFrustum() call if that ever changes.
// Real-world-scaled (root CLAUDE.md "Standard coordinates & scale") — was
// 10.0f/15000.0f pre-rescale. NOTE: not a straight ÷100 of the old values —
// near values below ~1.0 produced a fully blank render in this project's
// rlFrustum()/GL setup (empirically bisected; root cause not identified,
// suspected precision issue with very small near-plane/frustum-extent
// values), so these track MyBeginMode3D's near=1.0f/far=1000.0f rather than
// the exact ÷100 scale used everywhere else in this rescale.
#define SOFT_PARTICLE_SCENE_NEAR 1.0f
#define SOFT_PARTICLE_SCENE_FAR 1000.0f

static RenderTexture2D renderTex;
static Rectangle s_softDepthRegion;
static bool s_softDepthRegionValid;

// --- Soft particles: previous-frame depth snapshot (see header comment) ---
static RenderTexture2D prevDepthTex;
static Shader depthCopyShader;
static int depthCopyNearLoc;
static int depthCopyFarLoc;

// --- Refraction tap: sample-safe copy of the finished scene (see header) ---
static RenderTexture2D s_sceneSnapshot;
static bool s_sceneSnapshotRequested = false;

// Per-shader cache of soft-particle uniform locations — GetShaderLocation()
// is a string-hash lookup the engine does not cache for you (CORE_API.md),
// so don't call it every frame from SceneTargets_BindDepthForSoftParticles.
#define SOFT_PARTICLE_SHADER_CACHE_SIZE 8
typedef struct
{
  unsigned int shaderId;
  int depthLoc, nearLoc, farLoc, resLoc;
} SoftParticleShaderCacheEntry;
static SoftParticleShaderCacheEntry s_softCache[SOFT_PARTICLE_SHADER_CACHE_SIZE];
static int s_softCacheCount = 0;

// [TỐI ƯU HÓA 1]: Thêm 'static inline' để triệt tiêu chi phí gọi hàm (Call Overhead)
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

  // [TỐI ƯU HÓA 2]: Cache Uniforms - Đẩy u_near và u_far lên GPU 1 lần duy nhất khi
  // shader lần đầu tiên được nạp vào cache, thay vì phải gọi mỗi frame.
  float nearVal = SOFT_PARTICLE_SCENE_NEAR;
  float farVal = SOFT_PARTICLE_SCENE_FAR;
  if (entry->nearLoc >= 0)
    SetShaderValue(shader, entry->nearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
  if (entry->farLoc >= 0)
    SetShaderValue(shader, entry->farLoc, &farVal, SHADER_UNIFORM_FLOAT);

  return entry;
}

// HDR (Đợt G) — renderTex is the AUTHORITATIVE scene buffer: the whole 3D
// world is drawn into it (SceneTargets_Begin/End), so THIS is where colors
// must be allowed to exceed 1.0 for true HDR. PostFX's mainRenderTex only
// receives the already-composited distort quad, so it just has to match. We
// probe a 16-bit half-float color + depth-texture FBO here; GLES2 devices
// without float-renderable color fall back to RGBA8 (old LDR path). Query via
// SceneTargets_IsHDR() — PostFX_Init reads it to stay in lockstep.
static bool s_hdrActive = false;
static bool s_depthTextureActive = false;

// LoadRenderTexture() mặc định gắn depth attachment là RENDERBUFFER (không
// sample được trong shader). Build framebuffer thủ công qua rlgl để depth
// attachment là TEXTURE thật. colorFormat chọn RGBA8 (LDR) hoặc R16G16B16A16
// (HDR float) — xem probe trong SceneTargets_Init.
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
    target.depth.format = 19; // DEPTH_COMPONENT_24BIT (rlgl internal format id)
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

// Single-channel 32-bit FLOAT color target (no depth attachment). Used to
// snapshot LINEARIZED scene depth: an 8-bit RGBA copy crushes all far depths
// (near=0.1, far=15000) to 255, making scene vs particle depth
// indistinguishable. R32F keeps full precision.
/* Soft fade is deliberately low-frequency: it removes a hard intersection,
 * not a geometric edge.  Half-resolution depth cuts the only full-screen
 * soft-particle pass to one quarter of its former pixel cost. */
#define SOFT_DEPTH_DOWNSCALE 2

static RenderTexture2D LoadLinearDepthTarget(int width, int height)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id > 0)
  {
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    target.texture.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    if (!rlFramebufferComplete(target.id))
    {
      TraceLog(LOG_WARNING, "SceneTargets: linear-depth framebuffer incomplete");
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

/* ANTI-ALIASING — owned by the rlvk Renderer Agent, kept to these few lines on purpose.
 * The 3D world rasterizes into `renderTex`, never into the window, so FLAG_MSAA_4X_HINT /
 * rlvkSetMsaaSamples (both swapchain-only) can never anti-alias it: every geometric silhouette
 * in the game landed with binary coverage. rlvkSetFramebufferSamples makes renderTex ITSELF a
 * 4x target and resolves into the same colour and depth textures this module already hands out,
 * so ScreenDistort_Draw / SnapshotDepth / GetRawDepthTexture are unaffected.
 * Returns the sample count actually in effect (1 when the device declines) — it can cost
 * aliasing, never correctness. GL 3.3 / GLES have no offscreen-MSAA path and stay on FXAA. */
#if defined(GRAPHICS_API_VULKAN)
extern int rlvkSetFramebufferSamples(unsigned int fbId, int samples);
#endif
static int s_sceneSamples = 1;

/* Samples the scene target rasterizes with: 4 = real MSAA, 1 = none.
   NOT a reason to switch FXAA off. MSAA and FXAA fix different edges here: with MSAA on, the
   post-3D VFX silhouettes still measured 937 -> 933 luma steps, i.e. unchanged, so dropping
   FXAA when MSAA is enabled would make exactly the edges the owner reported worse. Exported
   for diagnostics and for anyone who needs to know which path is live. */
int SceneTargets_GetSceneSamples(void) { return s_sceneSamples; }

void SceneTargets_Init(int width, int height)
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

  /* DEFAULT OFF, opt in with WUXING_MSAA=4. The capability is real and tested; what it buys
     here is not worth its price by default, and both halves of that were measured:
       - what it fixes: opaque geometry silhouettes. The map's ellipse edge went 47 -> 17 luma
         steps >20. Real, but this is a NIGHT arena — its geometry edges are low-contrast, so
         the win is hard to see even where the numbers move. It would matter on bright daylight
         scenery, which is where this branch is headed.
       - what it costs: the scene scope opens exactly TWICE per frame — measured at 1.97/frame
         and CONSTANT, the same with a heavy fixture, a light one, and an empty scene — so two
         full-screen resolves every frame, always. Each moves ~55 MB (4x RGBA16F colour + 4x
         depth at 1280x720), which is ~2-4 ms on a bandwidth-shared GPU and was measured at ~5 ms
         on the Intel Iris 6000 here. ~10 ms against a 16.6 ms budget.
     And the second of those two resolves is nearly pure waste: that scope is the post-3D VFX
     pass, where MSAA measured 937 -> 933 luma steps, because an emissive HDR silhouette is
     eaten by the tone curve and a shader-decided rim is thinner than the pixel MSAA shades
     once. See ENGINE_LANDMINES.md #19 for the three edge classes.

     An env var rather than tuning.cfg, deliberately: this decides the FBO's allocation inside
     SceneTargets_Init, and Tuning_Init runs AFTER the subsystem inits (core/docs/LANDMINES.md
     — an early Tuning_RegisterFloat silently keeps its default). Gating on GfxQuality would be
     worse, not better: the tier is cycled at runtime from main.c, so an Init-time snapshot of
     it would silently desync from what the HUD says. */
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
  depthCopyShader = LoadShader(0, "core/shaders/depth_copy.fs");
  depthCopyNearLoc = GetShaderLocation(depthCopyShader, "u_near");
  depthCopyFarLoc = GetShaderLocation(depthCopyShader, "u_far");

  // [TỐI ƯU HÓA 3]: Đẩy thẳng hằng số u_near/u_far lên shader copy chiều sâu ngay khi init
  // Thay vì phải thực hiện mỗi lần gọi SnapshotDepth.
  float nearVal = SOFT_PARTICLE_SCENE_NEAR;
  float farVal = SOFT_PARTICLE_SCENE_FAR;
  if (depthCopyNearLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyNearLoc, &nearVal, SHADER_UNIFORM_FLOAT);
  if (depthCopyFarLoc >= 0)
    SetShaderValue(depthCopyShader, depthCopyFarLoc, &farVal, SHADER_UNIFORM_FLOAT);

  s_softCacheCount = 0;
  // The split VFX render layers were retired after measurement showed the
  // composite is arithmetic that cancels — see the commit that removed them and
  // core/docs/LANDMINES.md. VFX now draw straight into the scene target, which
  // is what BeginVFXBody/BeginVFXEmission do below. The body/emission NAMES are
  // kept because the distinction still means something to the code that reasons
  // about which effects occlude; only the two render targets are gone.
}

bool SceneTargets_IsHDR(void) { return s_hdrActive; }

void SceneTargets_Unload(void)
{
  UnloadRenderTexture(renderTex);
  if (s_depthTextureActive)
  {
    UnloadRenderTexture(prevDepthTex);
  }
  if (s_sceneSnapshot.id) UnloadRenderTexture(s_sceneSnapshot);
  s_sceneSnapshot = (RenderTexture2D){0};
  s_sceneSnapshotRequested = false;
  UnloadShader(depthCopyShader);
}

void SceneTargets_Begin(void)
{
  BeginTextureMode(renderTex);
  s_softDepthRegionValid = false;
}

void SceneTargets_End(void)
{
  EndTextureMode();
}

// Both "layers" are the scene target. Kept as distinct entry points so call
// sites still declare whether they occlude (body) or only add light (emission),
// which is the distinction the particle and trail systems route on.
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

VFXContrastLayer VFXRender_ContrastLayer(VFXRenderPass pass)
{
  return pass == VFX_RENDER_PASS_EMISSION
             ? VFX_CONTRAST_EMISSION : VFX_CONTRAST_BODY;
}

VFXRenderScope VFXRender_BeginDraw(VFXRenderPass pass,
                                   VFXSurfaceMode surface,
                                   bool depthWrite)
{
  VFXRenderScope scope = {true, depthWrite, pass, surface};
  VFXRender_BeginPass(pass);
  rlDrawRenderBatchActive();
  if (depthWrite) rlEnableDepthMask();
  else rlDisableDepthMask();
  BeginBlendMode(surface == VFX_SURFACE_ADDITIVE
                     ? BLEND_ADDITIVE
                     : (surface == VFX_SURFACE_PREMULTIPLIED
                            ? BLEND_ALPHA_PREMULTIPLY : BLEND_ALPHA));
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
  if (outResolved != NULL) *outResolved = resolved;
  if (!VFXRender_AppearanceDrawsPass(resolved, pass))
    return (VFXRenderScope){0};
  /* EMISSION is radiance by definition. Named dual-layer looks such as FIRE
   * keep their authored body surface, but their second semantic draw must add
   * light rather than alpha/premult composite the same geometry twice. */
  VFXSurfaceMode passSurface = pass == VFX_RENDER_PASS_EMISSION
                                   ? VFX_SURFACE_ADDITIVE : resolved.surface;
  return VFXRender_BeginDraw(pass, passSurface, depthWrite);
}

void VFXRender_EndDraw(VFXRenderScope *scope)
{
  if (scope == NULL || !scope->active) return;
  rlDrawRenderBatchActive();
  EndBlendMode();
  /* The shared VFX default is read-only depth. Every direct scope restores
   * the engine default (writes enabled), regardless of its requested mode. */
  rlEnableDepthMask();
  rlDrawRenderBatchActive();
  VFXRender_EndPass();
  scope->active = false;
}
// PERF (2026-07-22): frames since anything last called SceneTargets_BindDepthForSoftParticles.
// The snapshot below is a FULL-SCREEN pass, and it is also the only thing that ever samples
// renderTex.depth — which under rlvk's Caps.noSampledDepth quirk (MoltenVK/Intel) is served by a
// twin the backend refills at every scope close of this render target. With no soft particles on
// screen both are pure waste. The keep-alive window is generous (soft particles read the PREVIOUS
// frame's depth by design, and effects start abruptly), so a skill that begins using them finds
// the snapshot already running.
#define SOFT_DEPTH_KEEPALIVE_FRAMES 20
static int s_softDepthIdleFrames = SOFT_DEPTH_KEEPALIVE_FRAMES + 1; // start idle: nothing has asked yet

void SceneTargets_RequestSoftDepthRegion(Rectangle screenRegion)
{
  const float maxW = (float)renderTex.texture.width;
  const float maxH = (float)renderTex.texture.height;
  if (screenRegion.width <= 0.0f || screenRegion.height <= 0.0f || maxW <= 0.0f || maxH <= 0.0f)
    return;
  const float margin = 48.0f;
  float x0 = fmaxf(0.0f, screenRegion.x - margin);
  float y0 = fmaxf(0.0f, screenRegion.y - margin);
  float x1 = fminf(maxW, screenRegion.x + screenRegion.width + margin);
  float y1 = fminf(maxH, screenRegion.y + screenRegion.height + margin);
  if (x1 <= x0 || y1 <= y0) return;
  if (!s_softDepthRegionValid) {
    s_softDepthRegion = (Rectangle){x0, y0, x1 - x0, y1 - y0};
    s_softDepthRegionValid = true;
  } else {
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
  /* The refraction consumer (glass shield) also reads the prev-frame linear
   * depth for the contact term, but SnapshotDepth only fills prevDepthTex
   * while a full-screen soft-depth region is armed and the keep-alive is not
   * idle. Arm both here so the depth stays fresh for as long as a refractive
   * effect is alive — otherwise the shield samples stale or empty depth. */
  if (renderTex.texture.width > 0 && renderTex.texture.height > 0)
    SceneTargets_RequestSoftDepthRegion(
        (Rectangle){0, 0, (float)renderTex.texture.width,
                    (float)renderTex.texture.height});
  if (s_softDepthIdleFrames > 0) s_softDepthIdleFrames = 0;
}

/* Colour-only target in the scene's own format: an exact-copy destination for
 * the refraction tap. No depth attachment — the copy is a pure blit. */
static RenderTexture2D LoadSceneSnapshotTarget(int width, int height, int format)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id == 0) return target;
  rlEnableFramebuffer(target.id);
  target.texture.id = rlLoadTexture(NULL, width, height, format, 1);
  target.texture.width = width;
  target.texture.height = height;
  target.texture.format = format;
  target.texture.mipmaps = 1;
  rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                      RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(target.id))
    TraceLog(LOG_WARNING, "SceneTargets: scene-snapshot target incomplete");
  rlDisableFramebuffer();
  SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return target;
}

void SceneTargets_SnapshotScene(void)
{
  if (!s_sceneSnapshotRequested) return;
  s_sceneSnapshotRequested = false;
  if (renderTex.id == 0 || renderTex.texture.id == 0) return;

  const int w = renderTex.texture.width;
  const int h = renderTex.texture.height;
  if (s_sceneSnapshot.id == 0 || s_sceneSnapshot.texture.width != w ||
      s_sceneSnapshot.texture.height != h ||
      s_sceneSnapshot.texture.format != renderTex.texture.format)
  {
    if (s_sceneSnapshot.id) UnloadRenderTexture(s_sceneSnapshot);
    s_sceneSnapshot = LoadSceneSnapshotTarget(w, h, renderTex.texture.format);
  }
  if (s_sceneSnapshot.id == 0) return;

  /* 2D-time copy (main.c calls this after MyEndMode3D, before the screen-space
   * composite). It must NOT run inside a 3D pass: EndTextureMode() resets the
   * projection/modelview to screen ortho, corrupting every later draw in the
   * pass (engine landmine #15 — the glass shield once vanished entirely). */
  BeginTextureMode(s_sceneSnapshot);
  /* An exact copy: blending would fold the scene's own alpha into it, and the
   * negative source height is this file's RT->RT convention, which leaves
   * storage orientation identical to the source (gl_FragCoord UVs line up). */
  rlDisableColorBlend();
  DrawTextureRec(renderTex.texture,
                 (Rectangle){0, 0, (float)w, -(float)h}, (Vector2){0, 0}, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
  EndTextureMode();
}

Texture2D SceneTargets_GetSceneSnapshotTexture(void) { return s_sceneSnapshot.texture; }

void SceneTargets_SnapshotDepth(void)
{
  if (!s_depthTextureActive)
    return;
  if (s_softDepthIdleFrames > SOFT_DEPTH_KEEPALIVE_FRAMES)
    return;             // nobody has wanted soft-particle depth for a while: skip the whole pass
  if (!s_softDepthRegionValid)
    return;
  s_softDepthIdleFrames++;
  // [TỐI ƯU HÓA]: Lược bỏ hoàn toàn khối lệnh SetShaderValue(u_near, u_far) ở đây
  // vì đã được gán chết trên GPU ở hàm _Init, giảm API Draw Call.
  BeginTextureMode(prevDepthTex);
  BeginShaderMode(depthCopyShader);
  // THE DESTINATION Y IS THE MIRROR OF THE SOURCE Y, not a copy of it. A negative
  // source height makes DrawTexturePro sample the block bottom-to-top, which is what
  // turns FBO storage order back into screen order — but it mirrors WITHIN THE BLOCK,
  // so the block also has to be placed at its mirrored position for the composition to
  // be the plain identity the sampler assumes. Writing `region.y / D` is only correct
  // when the region is the whole frame (y = 0, h = H), which is the one case this was
  // ever exercised in, so the bug hid: a full-screen region is its own mirror.
  //
  // With a partial region the depth landed `H - 2y - h` screen rows away from where it
  // was read. Every consumer then compared its fragment against the wrong row of the
  // floor, and because the floor's depth changes fastest exactly where a shell meets it,
  // the error was largest where the contact term matters most: the ShieldShell's ground
  // line measured a 0.35-1.5 m gap at the pixel where the depth TEST had already cut the
  // geometry away, so no contact band could ever be drawn there.
  const float regionBottom = s_softDepthRegion.y + s_softDepthRegion.height;
  const float mirroredY = (float)renderTex.texture.height - regionBottom;
  Rectangle source = {s_softDepthRegion.x, s_softDepthRegion.y,
                      s_softDepthRegion.width, -s_softDepthRegion.height};
  Rectangle destination = {s_softDepthRegion.x / SOFT_DEPTH_DOWNSCALE,
                           mirroredY / SOFT_DEPTH_DOWNSCALE,
                           s_softDepthRegion.width / SOFT_DEPTH_DOWNSCALE,
                           s_softDepthRegion.height / SOFT_DEPTH_DOWNSCALE};
  DrawTexturePro(renderTex.depth, source, destination, (Vector2){0, 0}, 0.0f, WHITE);
  EndShaderMode();
  EndTextureMode();
}

Texture2D SceneTargets_GetDepthTexture(void) { return prevDepthTex.texture; }
Texture2D SceneTargets_GetSceneTexture(void) { return renderTex.texture; }
Texture2D SceneTargets_GetRawDepthTexture(void) { return s_depthTextureActive ? renderTex.depth : (Texture2D){0}; }

void SceneTargets_BindDepthForSoftParticles(Shader shader, int textureSlot)
{
  if (!s_depthTextureActive)
    return;
  s_softDepthIdleFrames = 0; // re-arm the snapshot (and rlvk's depth-twin refill) for the next frames
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

  // [TỐI ƯU HÓA]: Lược bỏ SetShaderValue(u_near, u_far) mỗi frame ở đây
  // vì GetSoftParticleLocs đã lo việc gán dữ liệu 1 lần vào cache.
  // Chỉ cập nhật resolution (nếu màn hình resize)
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

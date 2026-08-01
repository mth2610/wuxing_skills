#include "core/screen_distort.h"
#include "rlgl.h"
#include <string.h>
#include <stdlib.h> // getenv (WUXING_NO_HDR)

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
static RenderTexture2D vfxBodyTex;
static RenderTexture2D vfxEmissionTex;
static bool s_vfxBodyCleared;
static bool s_vfxEmissionCleared;
static bool s_vfxBodyUsed;
static bool s_vfxEmissionUsed;
static bool s_vfxLayersActive;
static void ScreenDistort_BeginLayer(RenderTexture2D target, bool *cleared);
static Shader distortShader;
static DistortionSource sources[MAX_DISTORTION_SOURCES];
static int activeSourcesCount = 0;

// --- Soft particles: previous-frame depth snapshot (see header comment) ---
static RenderTexture2D prevDepthTex;
static Shader depthCopyShader;
static int depthCopyNearLoc;
static int depthCopyFarLoc;

// Per-shader cache of soft-particle uniform locations — GetShaderLocation()
// is a string-hash lookup the engine does not cache for you (CORE_API.md),
// so don't call it every frame from ScreenDistort_BindDepthForSoftParticles.
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

// Uniform locations
static int centersLoc;
static int radiiLoc;
static int strengthsLoc;
static int progressLoc;
static int countLoc;
static int aspectLoc;

// HDR (Đợt G) — renderTex is the AUTHORITATIVE scene buffer: the whole 3D
// world is drawn into it (ScreenDistort_Begin/End), so THIS is where colors
// must be allowed to exceed 1.0 for true HDR. PostFX's mainRenderTex only
// receives the already-composited distort quad, so it just has to match. We
// probe a 16-bit half-float color + depth-texture FBO here; GLES2 devices
// without float-renderable color fall back to RGBA8 (old LDR path). Query via
// ScreenDistort_IsHDR() — PostFX_Init reads it to stay in lockstep.
static bool s_hdrActive = false;
static bool s_depthTextureActive = false;

// LoadRenderTexture() mặc định gắn depth attachment là RENDERBUFFER (không
// sample được trong shader). Build framebuffer thủ công qua rlgl để depth
// attachment là TEXTURE thật. colorFormat chọn RGBA8 (LDR) hoặc R16G16B16A16
// (HDR float) — xem probe trong ScreenDistort_Init.
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
      TraceLog(LOG_WARNING, "ScreenDistort: depth-texture framebuffer incomplete");
    }
    rlDisableFramebuffer();
  }
  return target;
}

static RenderTexture2D LoadColorLayerTarget(int width, int height, int colorFormat)
{
  RenderTexture2D target = {0};
  target.id = rlLoadFramebuffer();
  if (target.id == 0) return target;
  rlEnableFramebuffer(target.id);
  target.texture.id = rlLoadTexture(NULL, width, height, colorFormat, 1);
  target.texture.width = width;
  target.texture.height = height;
  target.texture.format = colorFormat;
  target.texture.mipmaps = 1;
  rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlDisableFramebuffer();
  return target;
}

// The VFX targets borrow renderTex.depth. Detach it before UnloadRenderTexture:
// raylib/rlvk owns and destroys a framebuffer's attached depth texture, while
// this target must only destroy its own colour attachment and framebuffer.
static void UnloadColorLayerTarget(RenderTexture2D target)
{
  if (target.id != 0)
  {
    rlEnableFramebuffer(target.id);
    rlFramebufferAttach(target.id, 0, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
  }
  UnloadRenderTexture(target);
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

void ScreenDistort_Init(int width, int height)
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
      TraceLog(LOG_INFO, "ScreenDistort: HDR float scene buffer active (R16G16B16A16)");
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
      TraceLog(LOG_INFO, "ScreenDistort: LDR depth-texture scene buffer active (RGBA8)");
    }
    else
    {
      if (renderTex.id > 0)
        UnloadRenderTexture(renderTex);
      renderTex = LoadRenderTexture(width, height);
      s_depthTextureActive = false;
      TraceLog(LOG_WARNING, "ScreenDistort: depth-texture unsupported, falling back to standard FBO (no depth sampling)");
    }
  }

  if (s_depthTextureActive)
  {
    int softDepthWidth = (width + SOFT_DEPTH_DOWNSCALE - 1) / SOFT_DEPTH_DOWNSCALE;
    int softDepthHeight = (height + SOFT_DEPTH_DOWNSCALE - 1) / SOFT_DEPTH_DOWNSCALE;
    prevDepthTex = LoadLinearDepthTarget(softDepthWidth, softDepthHeight);
    if (prevDepthTex.id == 0 || !rlFramebufferComplete(prevDepthTex.id))
    {
      TraceLog(LOG_WARNING, "ScreenDistort: linear-depth framebuffer incomplete, falling back to standard FBO");
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

  distortShader = LoadShader(0, "core/shaders/distortion.fs");
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

  centersLoc = GetShaderLocation(distortShader, "u_centers");
  radiiLoc = GetShaderLocation(distortShader, "u_radii");
  strengthsLoc = GetShaderLocation(distortShader, "u_strengths");
  progressLoc = GetShaderLocation(distortShader, "u_progress");
  countLoc = GetShaderLocation(distortShader, "u_count");
  aspectLoc = GetShaderLocation(distortShader, "u_aspectRatio");

  activeSourcesCount = 0;
  memset(sources, 0, sizeof(sources));

  // Layer targets deliberately share renderTex.depth. They are not nested
  // BeginTextureMode calls: changing only the framebuffer preserves the active
  // 3D matrices and gives custom VFX the same depth test as the scene.
  const int layerFmt = s_hdrActive ? RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16
                                   : RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  vfxBodyTex = LoadColorLayerTarget(width, height, layerFmt);
  vfxEmissionTex = LoadColorLayerTarget(width, height, layerFmt);
  if (vfxBodyTex.id > 0 && renderTex.depth.id > 0) {
    rlEnableFramebuffer(vfxBodyTex.id);
    rlFramebufferAttach(vfxBodyTex.id, renderTex.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
  }
  if (vfxEmissionTex.id > 0 && renderTex.depth.id > 0) {
    rlEnableFramebuffer(vfxEmissionTex.id);
    rlFramebufferAttach(vfxEmissionTex.id, renderTex.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
  }
  s_vfxLayersActive = vfxBodyTex.id > 0 && vfxBodyTex.texture.id > 0 &&
                      vfxEmissionTex.id > 0 && vfxEmissionTex.texture.id > 0 &&
                      renderTex.depth.id > 0 &&
                      rlFramebufferComplete(vfxBodyTex.id) &&
                      rlFramebufferComplete(vfxEmissionTex.id);
  if (!s_vfxLayersActive)
    TraceLog(LOG_WARNING, "ScreenDistort: VFX layers unavailable; using scene-target fallback");
}

bool ScreenDistort_IsHDR(void) { return s_hdrActive; }

void ScreenDistort_Unload(void)
{
  UnloadColorLayerTarget(vfxBodyTex);
  UnloadColorLayerTarget(vfxEmissionTex);
  UnloadRenderTexture(renderTex);
  if (s_depthTextureActive)
  {
    UnloadRenderTexture(prevDepthTex);
  }
  UnloadShader(distortShader);
  UnloadShader(depthCopyShader);
}

void ScreenDistort_Begin(void)
{
  BeginTextureMode(renderTex);
  s_vfxBodyCleared = false;
  s_vfxEmissionCleared = false;
  // Layers are cleared lazily by their producer.  On an idle scene this avoids
  // two HDR full-screen clears and four framebuffer switches every frame.
  s_vfxBodyUsed = false;
  s_vfxEmissionUsed = false;
}

void ScreenDistort_End(void)
{
  EndTextureMode();
}

static void ScreenDistort_BeginLayer(RenderTexture2D target, bool *cleared)
{
  if (!s_vfxLayersActive)
  {
    // Keep every producer visible on devices where the additional HDR targets
    // could not be created. This preserves the former scene-target behaviour.
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(renderTex.id);
    return;
  }
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(target.id);
  if (!*cleared)
  {
    // The layer FBO shares scene depth for correct VFX occlusion. Clear only
    // its colour attachment once per frame; depth mask protects scene depth.
    rlDisableDepthMask();
    ClearBackground(BLANK);
    rlEnableDepthMask();
    *cleared = true;
  }
}
void ScreenDistort_BeginVFXBody(void)
{
  s_vfxBodyUsed = true;
  ScreenDistort_BeginLayer(vfxBodyTex, &s_vfxBodyCleared);
}
void ScreenDistort_BeginVFXEmission(void)
{
  s_vfxEmissionUsed = true;
  ScreenDistort_BeginLayer(vfxEmissionTex, &s_vfxEmissionCleared);
}
void ScreenDistort_EndVFXLayer(void)
{
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(renderTex.id);
}

void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed)
{
  if (activeSourcesCount >= MAX_DISTORTION_SOURCES)
  {
    int minIdx = 0;
    float minLife = sources[0].lifetime;

    // Pointer caching nhẹ nhàng cho loop
    const DistortionSource *srcPtr = sources;
    for (int i = 1; i < MAX_DISTORTION_SOURCES; i++)
    {
      if (srcPtr[i].lifetime < minLife)
      {
        minLife = srcPtr[i].lifetime;
        minIdx = i;
      }
    }
    sources[minIdx] = (DistortionSource){worldPos, radius, strength, lifetime, lifetime, speed};
    return;
  }

  sources[activeSourcesCount] = (DistortionSource){worldPos, radius, strength, lifetime, lifetime, speed};
  activeSourcesCount++;
}

void ScreenDistort_Update(float dt)
{
  for (int i = activeSourcesCount - 1; i >= 0; i--)
  {
    sources[i].lifetime -= dt;
    if (sources[i].lifetime <= 0.0f)
    {
      sources[i] = sources[activeSourcesCount - 1];
      activeSourcesCount--;
    }
  }
}

void ScreenDistort_Draw(Camera3D camera)
{
  float screenWidth = (float)GetScreenWidth();
  float screenHeight = (float)GetScreenHeight();

  // [TỐI ƯU HÓA 4]: Tính toán nghịch đảo (Inverse)
  float invScreenWidth = 1.0f / screenWidth;
  float invScreenHeight = 1.0f / screenHeight;
  float aspect = screenWidth * invScreenHeight;

  static Vector2 centers[MAX_DISTORTION_SOURCES];
  static float radii[MAX_DISTORTION_SOURCES];
  static float strengths[MAX_DISTORTION_SOURCES];
  static float progress[MAX_DISTORTION_SOURCES];

  int validCount = 0;
  for (int i = 0; i < activeSourcesCount; i++)
  {
    // [TỐI ƯU HÓA 5]: Dùng con trỏ tĩnh để CPU không phải nhảy địa chỉ mảng liên tục
    const DistortionSource *src = &sources[i];

    // Giữ nguyên GetWorldToScreen, tôn trọng tuyệt đối logic Projection của project
    Vector2 screenPos = GetWorldToScreen(src->worldPos, camera);

    if (screenPos.x < -200.0f || screenPos.x > screenWidth + 200.0f ||
        screenPos.y < -200.0f || screenPos.y > screenHeight + 200.0f)
    {
      continue;
    }

    // Sử dụng phép nhân cho hiệu năng cực cao thay vì phép chia
    centers[validCount].x = screenPos.x * invScreenWidth;
    centers[validCount].y = 1.0f - (screenPos.y * invScreenHeight);

    radii[validCount] = src->radius * invScreenWidth;
    strengths[validCount] = src->strength;

    progress[validCount] = 1.0f - (src->lifetime / src->maxLifetime);

    validCount++;
  }

  SetShaderValue(distortShader, aspectLoc, &aspect, SHADER_UNIFORM_FLOAT);
  SetShaderValue(distortShader, countLoc, &validCount, SHADER_UNIFORM_INT);

  if (validCount > 0)
  {
    SetShaderValueV(distortShader, centersLoc, centers, SHADER_UNIFORM_VEC2, validCount);
    SetShaderValueV(distortShader, radiiLoc, radii, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, strengthsLoc, strengths, SHADER_UNIFORM_FLOAT, validCount);
    SetShaderValueV(distortShader, progressLoc, progress, SHADER_UNIFORM_FLOAT, validCount);
  }

  BeginShaderMode(distortShader);
  int hasVfxLayersLoc = GetShaderLocation(distortShader, "u_hasVfxLayers");
  int hasVfxLayers = (s_vfxLayersActive && s_vfxBodyUsed) ? 1 : 0;
  SetShaderValue(distortShader, hasVfxLayersLoc, &hasVfxLayers, SHADER_UNIFORM_INT);
  if (s_vfxLayersActive && s_vfxBodyUsed)
  {
    SetShaderValueTexture(distortShader, GetShaderLocation(distortShader, "u_vfxBodyTex"), vfxBodyTex.texture);
  }
  // DrawTexturePro (dest sized to GetRenderWidth/Height, not DrawTextureRec's implicit 1:1) -
  // renderTex is created at GetScreenWidth/Height (the logical window size); on backends where
  // the real render/swapchain target is a DIFFERENT size (rlvk/Vulkan on Android: the display's
  // full native resolution, no OS-level buffer upscale the way GL's ANativeWindow_setBuffersGeometry
  // provides), a 1:1 DrawTextureRec only covers a sub-rectangle, leaving the rest of the screen
  // uncleared (black) - see RLVK_HANDOFF.md §7.14.
  DrawTexturePro(renderTex.texture,
                 (Rectangle){0, 0, (float)renderTex.texture.width, -(float)renderTex.texture.height},
                 (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                 (Vector2){0, 0}, 0.0f, WHITE);
  EndShaderMode();

  // Body composition intentionally has only scene + body samplers. Add the
  // already-premultiplied emission RGB with an ordinary fullscreen pass; this
  // avoids the fragile three-sampler descriptor path on MoltenVK/Intel.
  if (s_vfxLayersActive && s_vfxEmissionUsed)
  {
    BeginBlendMode(BLEND_ADD_COLORS);
    DrawTexturePro(vfxEmissionTex.texture,
                   (Rectangle){0, 0, (float)vfxEmissionTex.texture.width, -(float)vfxEmissionTex.texture.height},
                   (Rectangle){0, 0, (float)GetRenderWidth(), (float)GetRenderHeight()},
                   (Vector2){0, 0}, 0.0f, WHITE);
    EndBlendMode();
  }
}

// PERF (2026-07-22): frames since anything last called ScreenDistort_BindDepthForSoftParticles.
// The snapshot below is a FULL-SCREEN pass, and it is also the only thing that ever samples
// renderTex.depth — which under rlvk's Caps.noSampledDepth quirk (MoltenVK/Intel) is served by a
// twin the backend refills at every scope close of this render target. With no soft particles on
// screen both are pure waste. The keep-alive window is generous (soft particles read the PREVIOUS
// frame's depth by design, and effects start abruptly), so a skill that begins using them finds
// the snapshot already running.
#define SOFT_DEPTH_KEEPALIVE_FRAMES 20
static int s_softDepthIdleFrames = SOFT_DEPTH_KEEPALIVE_FRAMES + 1; // start idle: nothing has asked yet

void ScreenDistort_SnapshotDepth(void)
{
  if (!s_depthTextureActive)
    return;
  if (s_softDepthIdleFrames > SOFT_DEPTH_KEEPALIVE_FRAMES)
    return;             // nobody has wanted soft-particle depth for a while: skip the whole pass
  s_softDepthIdleFrames++;
  // [TỐI ƯU HÓA]: Lược bỏ hoàn toàn khối lệnh SetShaderValue(u_near, u_far) ở đây
  // vì đã được gán chết trên GPU ở hàm _Init, giảm API Draw Call.
  BeginTextureMode(prevDepthTex);
  BeginShaderMode(depthCopyShader);
  DrawTextureRec(renderTex.depth,
                 (Rectangle){0, 0, (float)renderTex.texture.width, -(float)renderTex.texture.height},
                 (Vector2){0, 0}, WHITE);
  EndShaderMode();
  EndTextureMode();
}

Texture2D ScreenDistort_GetDepthTexture(void) { return prevDepthTex.texture; }
Texture2D ScreenDistort_GetSceneTexture(void) { return renderTex.texture; }
Texture2D ScreenDistort_GetRawDepthTexture(void) { return s_depthTextureActive ? renderTex.depth : (Texture2D){0}; }

void ScreenDistort_BindDepthForSoftParticles(Shader shader, int textureSlot)
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

void ScreenDistort_UnbindSoftParticleDepth(int textureSlot)
{
  (void)textureSlot;
}

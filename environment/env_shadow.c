#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>   // fabsf
#include <stddef.h> // NULL
#include <stdio.h>  // snprintf (EnvShadow_DebugDump)
#include <stdlib.h> // getenv (one-shot static-cache verification)

// Arena bounds (root CLAUDE.md "Standard coordinates & scale") — the light's
// orthographic frustum is sized to cover this, not the whole level.
#define ARENA_CENTER ((Vector3){6.0f, 1.0f, 4.4f}) // radius 18, y raised slightly (was 3, too far above the y=0 ground the shadow actually needs to cover)
#define ARENA_RADIUS 18.0f

// The light orientation is only rebuilt once the sun has moved this far
// (squared chord distance of unit vectors, ~0.115 deg). Below it, the shadow map
// keeps its previous orientation: a continuously rotating light re-grids every
// texel each frame (shimmer) and, for the world-fixed static map, forces a full
// recapture of every map caster. Worst-case shadow offset at the threshold is
// ~1 cm per 10 m of caster height.
#define ENV_SHADOW_SUN_EPS_SQ 4.0e-6f
// halfExtent is rounded up to this step so a smoothly zooming camera doesn't
// change the texel size (and therefore the snapping grid) every frame.
#define ENV_SHADOW_EXTENT_STEP 0.5f
// Minimum seconds between SUN-DRIFT-triggered static recaptures. With a running
// Time-of-Day cycle the sun crosses ENV_SHADOW_SUN_EPS_SQ every fraction of a
// second, and each crossing redraws every map caster into the static target.
// Holding the previous capture for this long costs a sub-degree direction lag
// on the static shadows and nothing else. Explicit invalidation
// (EnvShadow_InvalidateStaticCache, quality change, re-init) is NOT throttled.
// Override at runtime with WUXING_SHADOW_STATIC_MIN_INTERVAL=<seconds>; 0
// restores the old "recapture on every EPS crossing" behaviour.
#define ENV_SHADOW_STATIC_MIN_INTERVAL 0.5

static bool s_ready = false;
static bool s_enabled = false;
static bool s_capturing = false; // true between Begin/EndCapture (EnvShadow_IsCapturing)
static bool s_staticCaptureOpen = false;
static unsigned int s_fboId = 0;
static unsigned int s_depthTexId = 0;
static unsigned int s_colorTexId = 0;
static int s_resolution = 0;
static Shader s_depthShader;
static Matrix s_lightView, s_lightProj, s_lightVP;
static Vector3 s_shadowFocus = ARENA_CENTER;
static float s_shadowHalfExtent = ARENA_RADIUS + 2.0f;
static float s_captureDistance = ARENA_RADIUS + 6.0f;

// The depth attachment above is NOT directly sampled anywhere — this
// project's rlvk (Vulkan/MoltenVK) backend has a device quirk
// ("noSampledDepth", logged at startup on Intel/MoltenVK) where FBO
// depth-attachment textures aren't reliably sampleable from an arbitrary 3D
// shader.
// NOTE TỐI ƯU: Đã xóa s_copyRT và s_copyShader khỏi đây vì depth shader đã
// ghi trực tiếp gl_FragCoord.z vào s_colorTexId (R32F). Tiết kiệm ~16MB VRAM.
static Texture2D s_shadowMapTex; // this is what callers get

// Static map casters live in a separate, lower-frequency target. Keeping its
// projection world-fixed avoids invalidating it when the camera-following
// dynamic focus moves, while the receiver combines both visibility terms.
static unsigned int s_staticFboId = 0;
static unsigned int s_staticDepthTexId = 0;
static unsigned int s_staticColorTexId = 0;
static int s_staticResolution = 0;
static Matrix s_staticLightView, s_staticLightProj, s_staticLightVP;
static float s_staticCaptureDistance = 0.0f;
static Texture2D s_staticShadowMapTex;
static Vector3 s_staticSunDir = {0};
static bool s_staticTargetReady = false;
static bool s_staticCacheValid = false;
static bool s_dynamicVerifyDumped = false;
static double s_staticCaptureTime = 0.0;      // GetTime() of the last completed static capture
static unsigned int s_staticCaptureCount = 0; // how many static captures so far (diagnostic)
#if defined(__ANDROID__)
static int s_quality = 1;
#else
static int s_quality = 2;
#endif
static EnvShadowMapCasterCallback s_mapCasterCallback = NULL;
static void *s_mapCasterUserData = NULL;

static Vector3 ProjectLS(Matrix vp, Vector3 wp);

// ---------------------------------------------------------------------------
// Diagnostic environment overrides, read ONCE. getenv() is a linear scan of the
// environment block; it used to run every frame (SetFocus x2, EndCapture x1,
// forever when the variable was unset). The values can no longer change at
// runtime, which none of these debug switches rely on.
// ---------------------------------------------------------------------------
static struct
{
    bool ready;
    bool focusOverride;
    float focusX, focusZ;
    bool dynamicVerify;
    bool staticVerify;
    int dynamicRes;           // WUXING_SHADOW_RES        (0 = unset)
    int staticRes;            // WUXING_SHADOW_STATIC_RES (0 = unset)
    double staticMinInterval; // seconds, see ENV_SHADOW_STATIC_MIN_INTERVAL
} s_env;

static void EnsureShadowEnv(void)
{
    if (s_env.ready)
        return;
    // Reusable diagnostic override for inspecting a known caster region while
    // the gameplay camera remains elsewhere. Both coordinates are required so
    // a partially configured run cannot move only one axis by accident.
    const char *focusX = getenv("WUXING_SHADOW_FOCUS_X");
    const char *focusZ = getenv("WUXING_SHADOW_FOCUS_Z");
    if (focusX != NULL && focusZ != NULL)
    {
        s_env.focusOverride = true;
        s_env.focusX = strtof(focusX, NULL);
        s_env.focusZ = strtof(focusZ, NULL);
    }
    s_env.dynamicVerify = getenv("WUXING_SHADOW_DYNAMIC_VERIFY") != NULL;
    s_env.staticVerify = getenv("WUXING_SHADOW_STATIC_VERIFY") != NULL;

    const char *dynamicRes = getenv("WUXING_SHADOW_RES");
    const char *staticRes = getenv("WUXING_SHADOW_STATIC_RES");
    s_env.dynamicRes = dynamicRes != NULL ? atoi(dynamicRes) : 0;
    s_env.staticRes = staticRes != NULL ? atoi(staticRes) : 0;

    s_env.staticMinInterval = ENV_SHADOW_STATIC_MIN_INTERVAL;
    const char *minInterval = getenv("WUXING_SHADOW_STATIC_MIN_INTERVAL");
    if (minInterval != NULL)
    {
        double v = strtod(minInterval, NULL);
        s_env.staticMinInterval = v > 0.0 ? v : 0.0;
    }
    s_env.ready = true;
}

static void DestroyShadowTarget(unsigned int fbo, unsigned int depth, unsigned int color)
{
    if (fbo != 0)
        rlUnloadFramebuffer(fbo);
    if (depth != 0)
        rlUnloadTexture(depth);
    if (color != 0)
        rlUnloadTexture(color);
}

static void ShadowQualitySizes(int quality, int *dynamicSize, int *staticSize)
{
    EnsureShadowEnv();
    *dynamicSize = quality < 2 ? 1024 : 2048;
    *staticSize = quality < 2 ? 512 : 1024;
    if (s_env.dynamicRes > 0)
        *dynamicSize = s_env.dynamicRes;
    if (s_env.staticRes > 0)
        *staticSize = s_env.staticRes;
}

// ---------------------------------------------------------------------------
// Light basis. One definition shared by the dynamic focus snapping, the
// dynamic light matrix and the static light matrix (the formulas used to be
// copy-pasted three times).
// ---------------------------------------------------------------------------
typedef struct
{
    Vector3 dir;   // direction light TRAVELS (unit)
    Vector3 up;    // look-at up vector
    Vector3 right; // light-plane axes used for texel snapping
    Vector3 lightUp;
} ShadowLightBasis;

static ShadowLightBasis MakeLightBasis(Vector3 sunDirUnit)
{
    ShadowLightBasis b;
    b.dir = sunDirUnit;
    b.up = fabsf(b.dir.y) > 0.98f
               ? (Vector3){0.0f, 0.0f, 1.0f} // avoid degenerate look-at
               : (Vector3){0.0f, 1.0f, 0.0f};
    b.right = Vector3Normalize(Vector3CrossProduct(b.up, b.dir));
    b.lightUp = Vector3Normalize(Vector3CrossProduct(b.dir, b.right));
    return b;
}

// Snap a point onto the texel grid of the light plane (grid anchored at the
// world origin, so it is stable while the focus slides).
static Vector3 SnapToLightTexels(Vector3 center, const ShadowLightBasis *b, float texelWorld)
{
    float rightCoord = Vector3DotProduct(center, b->right);
    float upCoord = Vector3DotProduct(center, b->lightUp);
    center = Vector3Add(center, Vector3Scale(
                                    b->right, roundf(rightCoord / texelWorld) * texelWorld - rightCoord));
    center = Vector3Add(center, Vector3Scale(
                                    b->lightUp, roundf(upCoord / texelWorld) * texelWorld - upCoord));
    return center;
}

static inline bool SameVec3(Vector3 a, Vector3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// The dynamic light keeps a "held" sun direction that only advances when the
// real sun has moved past ENV_SHADOW_SUN_EPS_SQ. Snapping (SetFocus) and the
// light matrix (ComputeLightVP) both read it, so they always agree on the grid
// orientation — snapping against the live sun while the matrix used a stale
// one would reintroduce exactly the sub-texel jitter snapping exists to remove.
static Vector3 s_heldSun = {0};
static ShadowLightBasis s_heldBasis;
static bool s_heldValid = false;

static const ShadowLightBasis *HeldLightBasis(void)
{
    Vector3 current = Vector3Normalize(Environment_GetSunDirection());
    if (!s_heldValid || Vector3DistanceSqr(current, s_heldSun) >= ENV_SHADOW_SUN_EPS_SQ)
    {
        s_heldSun = current;
        s_heldBasis = MakeLightBasis(current);
        s_heldValid = true;
    }
    return &s_heldBasis;
}

// Light-matrix cache keys: skip LookAt/Ortho/Multiply when nothing changed.
static Vector3 s_lastSunDir = {0};
static Vector3 s_lastShadowFocus = {1e30f, 1e30f, 1e30f};
static float s_lastShadowHalfExtent = -1.0f;

static bool CreateShadowTarget(int resolution, unsigned int *outFbo,
                               unsigned int *outDepth, unsigned int *outColor,
                               Texture2D *outMap)
{
    *outColor = rlLoadTexture(NULL, resolution, resolution,
                              RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    *outDepth = rlLoadTextureDepth(resolution, resolution, false);
    *outFbo = rlLoadFramebuffer();
    if (*outFbo == 0 || *outDepth == 0 || *outColor == 0)
    {
        DestroyShadowTarget(*outFbo, *outDepth, *outColor);
        *outFbo = *outDepth = *outColor = 0;
        return false;
    }

    rlEnableFramebuffer(*outFbo);
    rlFramebufferAttach(*outFbo, *outColor, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(*outFbo, *outDepth, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(*outFbo);
    rlDisableFramebuffer();
    if (!complete)
    {
        DestroyShadowTarget(*outFbo, *outDepth, *outColor);
        *outFbo = *outDepth = *outColor = 0;
        return false;
    }

    *outMap = (Texture2D){
        .id = *outColor,
        .width = resolution,
        .height = resolution,
        .mipmaps = 1,
        .format = RL_PIXELFORMAT_UNCOMPRESSED_R32,
    };
    // R32F linear filtering is optional on Vulkan/GLES. Receiver shaders
    // interpolate comparison results explicitly, so POINT is intentional.
    SetTextureFilter(*outMap, TEXTURE_FILTER_POINT);
    SetTextureWrap(*outMap, TEXTURE_WRAP_CLAMP);
    return true;
}

void EnvShadow_Init(void)
{
    // Re-init safety: release anything a previous call allocated.
    DestroyShadowTarget(s_fboId, s_depthTexId, s_colorTexId);
    DestroyShadowTarget(s_staticFboId, s_staticDepthTexId, s_staticColorTexId);
    s_fboId = s_depthTexId = s_colorTexId = 0;
    s_staticFboId = s_staticDepthTexId = s_staticColorTexId = 0;
    s_staticTargetReady = false;
    s_staticCacheValid = false;
    s_ready = false;
    s_lastShadowHalfExtent = -1.0f;

    ShadowQualitySizes(s_quality, &s_resolution, &s_staticResolution);

    // Compile the shader FIRST: it is the cheapest thing that can fail, and the
    // old order allocated both shadow targets (tens of MB) before discovering a
    // bad shader, then returned without freeing them.
    s_depthShader = ResourceManager_LoadShader("environment/shaders/shadow_depth.vs",
                                               "environment/shaders/shadow_depth.fs");
    if (s_depthShader.id == 0)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: shader failed to compile, shadow map disabled");
        return;
    }

    // Depth + throwaway color attachment — same recipe as
    // core/screen_distort.c's LoadRenderTextureWithDepthTexture, which is
    // proven to work on this project's rlvk (Vulkan) backend already. A
    // depth-ONLY FBO (rlActiveDrawBuffers(0), no color attachment) is a
    // different, untested configuration under a custom Vulkan reimplementation
    // of rlgl — not worth the risk here, the color texture is never sampled.
    // The color attachment IS the shadow map now: the depth shader writes gl_FragCoord.z into it
    // (R32F), so it's sampled directly — no depth->R32F copy pass. Depth attachment is only for the
    // capture's depth TEST (nearest caster wins); it is never sampled.
    // NOTE (§7.27, OPEN PERF ISSUE): this depth attachment is only ever depth-TESTED against — the
    // shadow map we sample is the R32F COLOR attachment above. But rlvk still builds a
    // Caps.noSampledDepth "sampleable twin" for it and bounces depth->buffer->twin at EVERY scope
    // close (~32 MB/frame at 2048) for a twin nobody reads. That is the prime suspect for the
    // remaining shadow cost. Do NOT "fix" it by passing useRenderBuffer=true here: raylib's own
    // LoadRenderTexture also passes true, so honouring it in rlvk strips the twin from every render
    // texture and breaks soft-particle depth sampling (VUID-…-oldLayout-01211). See the notes.
    if (!CreateShadowTarget(s_resolution, &s_fboId, &s_depthTexId,
                            &s_colorTexId, &s_shadowMapTex))
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: failed to allocate depth FBO/texture, shadow map disabled");
        return;
    }

    s_staticTargetReady = CreateShadowTarget(
        s_staticResolution, &s_staticFboId, &s_staticDepthTexId,
        &s_staticColorTexId, &s_staticShadowMapTex);
    if (!s_staticTargetReady)
        TraceLog(LOG_WARNING, "ENV_SHADOW: static cache target unavailable; dynamic shadows remain active");

    s_ready = true;
    s_enabled = true; // Real shadows remain available at every graphics tier.
    TraceLog(LOG_INFO, "ENV_SHADOW: ready, %dx%d depth target (fbo=%u depthTex=%u shadowMapTex=%u)",
             s_resolution, s_resolution, s_fboId, s_depthTexId, s_colorTexId);
    if (s_staticTargetReady)
        TraceLog(LOG_INFO, "ENV_SHADOW: static cache target ready, %dx%d (fbo=%u shadowMapTex=%u)",
                 s_staticResolution, s_staticResolution, s_staticFboId, s_staticColorTexId);
}

void EnvShadow_SetEnabled(bool enabled) { s_enabled = s_ready && enabled; }
bool EnvShadow_IsEnabled(void) { return s_ready && s_enabled; }
bool EnvShadow_IsCapturing(void) { return s_capturing; }

int EnvShadow_GetQuality(void) { return s_quality; }

bool EnvShadow_SetQuality(int resolvedTier)
{
    if (s_capturing)
        return false;
    if (resolvedTier < 0 || resolvedTier > 3)
        resolvedTier = 2;
    if (!s_ready)
    {
        s_quality = resolvedTier;
        return true;
    }
    int dynamicSize, staticSize;
    ShadowQualitySizes(resolvedTier, &dynamicSize, &staticSize);
    if (dynamicSize == s_resolution && staticSize == s_staticResolution)
    {
        s_quality = resolvedTier;
        return true;
    }

    unsigned int fbo = 0, depth = 0, color = 0;
    unsigned int staticFbo = 0, staticDepth = 0, staticColor = 0;
    Texture2D dynamicMap = {0}, staticMap = {0};
    rlDrawRenderBatchActive();
    if (!CreateShadowTarget(dynamicSize, &fbo, &depth, &color, &dynamicMap))
        return false;
    if (!CreateShadowTarget(staticSize, &staticFbo, &staticDepth, &staticColor, &staticMap))
    {
        DestroyShadowTarget(fbo, depth, color);
        return false;
    }
    DestroyShadowTarget(s_fboId, s_depthTexId, s_colorTexId);
    DestroyShadowTarget(s_staticFboId, s_staticDepthTexId, s_staticColorTexId);
    s_fboId = fbo;
    s_depthTexId = depth;
    s_colorTexId = color;
    s_shadowMapTex = dynamicMap;
    s_staticFboId = staticFbo;
    s_staticDepthTexId = staticDepth;
    s_staticColorTexId = staticColor;
    s_staticShadowMapTex = staticMap;
    s_resolution = dynamicSize;
    s_staticResolution = staticSize;
    s_quality = resolvedTier;
    s_staticTargetReady = true;
    s_staticCacheValid = false;
    s_dynamicVerifyDumped = false;
    s_lastShadowHalfExtent = -1.0f;
    TraceLog(LOG_INFO, "ENV_SHADOW: quality %d, dynamic %d, static %d",
             s_quality, s_resolution, s_staticResolution);
    return true;
}

void EnvShadow_SetFocus(Vector3 center, float halfExtent)
{
    EnsureShadowEnv();
    if (s_env.focusOverride)
    {
        center.x = s_env.focusX;
        center.z = s_env.focusZ;
    }
    if (halfExtent < 8.0f)
        halfExtent = 8.0f;
    if (halfExtent > 96.0f)
        halfExtent = 96.0f;
    // Round up to a coarse step: the snapping grid below is sized from
    // halfExtent, so a continuously changing extent (camera zoom) would move
    // the grid every frame and shimmer fine edges. Rounding up only ever
    // covers MORE than requested (<= one step), at a texel-density cost < 3%.
    halfExtent = ceilf(halfExtent / ENV_SHADOW_EXTENT_STEP) * ENV_SHADOW_EXTENT_STEP;
    if (halfExtent > 96.0f)
        halfExtent = 96.0f;
    // Stabilize the orthographic projection in the light plane. Without this,
    // every sub-centimetre camera movement shifts all shadow texels and makes
    // fine grass/contact edges shimmer even when the scene is otherwise still.
    if (s_resolution > 0)
    {
        float texelWorld = (halfExtent * 2.0f) / (float)s_resolution;
        center = SnapToLightTexels(center, HeldLightBasis(), texelWorld);
    }
    s_shadowFocus = center;
    s_shadowHalfExtent = halfExtent;
}

Vector3 EnvShadow_GetFocus(void) { return s_shadowFocus; }
float EnvShadow_GetHalfExtent(void) { return s_shadowHalfExtent; }

static Matrix ComputeLightVP(void)
{
    const ShadowLightBasis *basis = HeldLightBasis();

    // TỐI ƯU: Cache lại VP matrix. Bỏ qua các phép toán Normalize, Multiply, LookAt
    // nếu hướng nắng (đã giữ ổn định bởi HeldLightBasis), focus và extent không đổi.
    if (SameVec3(basis->dir, s_lastSunDir) &&
        Vector3DistanceSqr(s_shadowFocus, s_lastShadowFocus) < 0.000001f &&
        fabsf(s_shadowHalfExtent - s_lastShadowHalfExtent) < 0.0001f)
    {
        return s_lightVP;
    }
    s_lastSunDir = basis->dir;
    s_lastShadowFocus = s_shadowFocus;
    s_lastShadowHalfExtent = s_shadowHalfExtent;

    s_captureDistance = fmaxf(s_shadowHalfExtent * 1.65f, 24.0f);
    Vector3 lightPos = Vector3Subtract(s_shadowFocus, Vector3Scale(basis->dir, s_captureDistance));

    s_lightView = MatrixLookAt(lightPos, s_shadowFocus, basis->up);
    // ARENA_RADIUS+2: covers the 18m-radius arena with margin. (A debug
    // session once "measured" the character outside this frustum and bumped
    // it to 45 — but that measurement used the pre-fix TRANSPOSED matrix in
    // the FS, so it was garbage. The CPU numeric dump with the corrected
    // math confirms 20 frames the arena fine, and the tighter box more than
    // doubles texel density: at 45, a 0.5m character was ~6 texels wide with
    // rasterization holes — the shadow existed but as a near-invisible
    // speckle, which is exactly what "no shadow visible" looked like.)
    float halfExtent = s_shadowHalfExtent;
    s_lightProj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent,
                              0.1f, s_captureDistance * 2.0f);
    return MatrixMultiply(s_lightView, s_lightProj);
}

static Matrix ComputeStaticLightVP(Vector3 center, float halfExtent)
{
    if (halfExtent < 8.0f)
        halfExtent = 8.0f;
    if (halfExtent > 128.0f)
        halfExtent = 128.0f;
    // The static map is world-fixed and captured on demand, so it uses the
    // live sun at capture time (HasStaticCache validates against that).
    ShadowLightBasis basis = MakeLightBasis(Vector3Normalize(Environment_GetSunDirection()));
    float texelWorld = halfExtent * 2.0f / (float)s_staticResolution;
    center = SnapToLightTexels(center, &basis, texelWorld);

    s_staticCaptureDistance = fmaxf(halfExtent * 1.65f, 24.0f);
    Vector3 lightPos = Vector3Subtract(
        center, Vector3Scale(basis.dir, s_staticCaptureDistance));
    s_staticLightView = MatrixLookAt(lightPos, center, basis.up);
    s_staticLightProj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent,
                                    0.1f, s_staticCaptureDistance * 2.0f);
    return MatrixMultiply(s_staticLightView, s_staticLightProj);
}

static void BeginCaptureTarget(unsigned int fbo, int resolution, Matrix lightView,
                               float halfExtent, float captureDistance)
{
    s_capturing = true;
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlEnableFramebuffer(fbo);
    rlViewport(0, 0, resolution, resolution);
    rlClearColor(255, 255, 255, 255);
    rlClearScreenBuffers();

    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent,
            0.1, captureDistance * 2.0);
    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(lightView));
    BeginShaderMode(s_depthShader);
}

static void EndCaptureTarget(void)
{
    EndShaderMode();
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    rlLoadIdentity();
    rlDisableFramebuffer();
    rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());
    s_capturing = false;
}

void EnvShadow_BeginCapture(void)
{
    if (!s_ready || !s_enabled || s_capturing)
        return;

    s_lightVP = ComputeLightVP();
    BeginCaptureTarget(s_fboId, s_resolution, s_lightView,
                       s_shadowHalfExtent, s_captureDistance);
    if (s_mapCasterCallback != NULL)
    {
        s_mapCasterCallback(s_depthShader, s_mapCasterUserData);
        // DrawModel uses its material shader and disables it afterward. Restore
        // the environment depth shader for engine-owned casters that follow.
        BeginShaderMode(s_depthShader);
    }
}

// --- Debug readback helpers (kept out of the per-frame capture path) --------
static void ScanShadowMap(const float *pixels, int total, float *minDepth, int *occupied)
{
    float mn = 1.0f;
    int occ = 0;
    for (int i = 0; i < total; i++)
    {
        if (pixels[i] < mn)
            mn = pixels[i];
        if (pixels[i] < 0.999f)
            occ++;
    }
    *minDepth = mn;
    *occupied = occ;
}

static void VerifyDynamicShadowMap(void)
{
    float *pixels = (float *)rlReadTexturePixels(
        s_shadowMapTex.id, s_resolution, s_resolution,
        RL_PIXELFORMAT_UNCOMPRESSED_R32);
    if (pixels == NULL)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: dynamic verify readback failed");
        return;
    }
    int total = s_resolution * s_resolution;
    float minDepth;
    int occupied;
    ScanShadowMap(pixels, total, &minDepth, &occupied);

    // Exercise the same projection/depth comparison used by map
    // receivers over a regular ground grid. Reporting both readback
    // orientations makes a backend Y-flip regression unambiguous.
    int receiverShadowed = 0;
    int receiverShadowedFlipped = 0;
    const int receiverGrid = 256;
    for (int z = 0; z < receiverGrid; z++)
    {
        for (int x = 0; x < receiverGrid; x++)
        {
            float fx = ((float)x + 0.5f) / (float)receiverGrid;
            float fz = ((float)z + 0.5f) / (float)receiverGrid;
            Vector3 world = {
                s_shadowFocus.x + (fx * 2.0f - 1.0f) * s_shadowHalfExtent,
                0.0f,
                s_shadowFocus.z + (fz * 2.0f - 1.0f) * s_shadowHalfExtent,
            };
            Vector3 projected = ProjectLS(s_lightVP, world);
            if (projected.x <= 0.0f || projected.x >= 1.0f ||
                projected.y <= 0.0f || projected.y >= 1.0f ||
                projected.z <= 0.0f || projected.z >= 1.0f)
                continue;
            int tx = (int)(projected.x * (float)(s_resolution - 1));
            int ty = (int)(projected.y * (float)(s_resolution - 1));
            float compareDepth = projected.z - 0.00018f;
            if (compareDepth > pixels[ty * s_resolution + tx])
                receiverShadowed++;
            int flippedY = s_resolution - 1 - ty;
            if (compareDepth > pixels[flippedY * s_resolution + tx])
                receiverShadowedFlipped++;
        }
    }
    TraceLog(LOG_INFO,
             "ENV_SHADOW: dynamic verify minDepth=%.4f occupied=%d/%d receiver=%d/%d flipped=%d/%d",
             minDepth, occupied, total, receiverShadowed,
             receiverGrid * receiverGrid, receiverShadowedFlipped,
             receiverGrid * receiverGrid);
    MemFree(pixels);
}

static void VerifyStaticShadowMap(void)
{
    float *pixels = (float *)rlReadTexturePixels(
        s_staticShadowMapTex.id, s_staticResolution, s_staticResolution,
        RL_PIXELFORMAT_UNCOMPRESSED_R32);
    if (pixels == NULL)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: static verify readback failed");
        return;
    }
    int total = s_staticResolution * s_staticResolution;
    float minDepth;
    int occupied;
    ScanShadowMap(pixels, total, &minDepth, &occupied);
    TraceLog(LOG_INFO,
             "ENV_SHADOW: static verify minDepth=%.4f occupied=%d/%d",
             minDepth, occupied, total);
    MemFree(pixels);
}

void EnvShadow_EndCapture(void)
{
    if (!s_ready || !s_enabled || !s_capturing || s_staticCaptureOpen)
        return;
    EndCaptureTarget();
    if (!s_dynamicVerifyDumped)
    {
        EnsureShadowEnv();
        if (s_env.dynamicVerify)
        {
            s_dynamicVerifyDumped = true;
            VerifyDynamicShadowMap();
        }
    }
}

Shader EnvShadow_GetDepthShader(void) { return s_depthShader; }
Matrix EnvShadow_GetLightVP(void) { return s_lightVP; }
// The R32F color attachment the depth shader wrote gl_FragCoord.z into (depth-as-color).
Texture2D EnvShadow_GetShadowMap(void) { return s_shadowMapTex; }

void EnvShadow_SetMapCasterCallback(EnvShadowMapCasterCallback callback, void *userData)
{
    s_mapCasterCallback = callback;
    s_mapCasterUserData = callback != NULL ? userData : NULL;
}

void EnvShadow_BeginStaticCapture(Vector3 center, float halfExtent)
{
    if (!s_ready || !s_enabled || !s_staticTargetReady || s_capturing)
        return;
    if (halfExtent < 8.0f)
        halfExtent = 8.0f;
    if (halfExtent > 128.0f)
        halfExtent = 128.0f;
    s_staticCacheValid = false;
    s_staticLightVP = ComputeStaticLightVP(center, halfExtent);
    s_staticCaptureOpen = true;
    BeginCaptureTarget(s_staticFboId, s_staticResolution, s_staticLightView,
                       halfExtent, s_staticCaptureDistance);
}

void EnvShadow_EndStaticCapture(void)
{
    if (!s_staticCaptureOpen || !s_capturing)
        return;
    EndCaptureTarget();
    s_staticCaptureOpen = false;
    s_staticSunDir = Vector3Normalize(Environment_GetSunDirection());
    s_staticCacheValid = true;
    s_staticCaptureTime = GetTime();
    s_staticCaptureCount++;
    TraceLog(LOG_INFO, "ENV_SHADOW: static cache captured #%u (%dx%d)",
             s_staticCaptureCount, s_staticResolution, s_staticResolution);
    EnsureShadowEnv();
    if (s_env.staticVerify)
        VerifyStaticShadowMap();
}

void EnvShadow_InvalidateStaticCache(void)
{
    s_staticCacheValid = false;
}

bool EnvShadow_HasStaticCache(void)
{
    if (!s_ready || !s_enabled || !s_staticTargetReady || !s_staticCacheValid)
        return false;
    // Same tolerance as the dynamic light: while the Time-of-Day cycle slowly
    // rotates the sun, the static map (every map caster) is recaptured once per
    // ENV_SHADOW_SUN_EPS_SQ of movement instead of at ~4x that rate.
    Vector3 current = Vector3Normalize(Environment_GetSunDirection());
    if (Vector3DistanceSqr(current, s_staticSunDir) < ENV_SHADOW_SUN_EPS_SQ)
        return true;
    // The sun has drifted past the tolerance. Recapture, but not more often than
    // the minimum interval (0 disables the throttle).
    EnsureShadowEnv();
    return (GetTime() - s_staticCaptureTime) < s_env.staticMinInterval;
}

bool EnvShadow_NeedsStaticCapture(void)
{
    return s_ready && s_enabled && s_staticTargetReady &&
           !EnvShadow_HasStaticCache();
}

Matrix EnvShadow_GetStaticLightVP(void) { return s_staticLightVP; }
Texture2D EnvShadow_GetStaticShadowMap(void) { return s_staticShadowMapTex; }

// ---------------------------------------------------------------------------
// TEMP diagnostic (P6 bug 3) — numeric ground truth via CPU readback.
// Replicates the FS's exact projection formula on the CPU (the proven
// Vector3Transform convention: x' = m0*x + m4*y + m8*z + m12, which matched
// proj=(0.5,0.5,0.499) for ARENA_CENTER earlier), reads the R32F copy texture
// back, and prints everything needed to reason precisely: stored min/max +
// histogram, the caster/ground texels, stored depth there in BOTH Y
// orientations (settles the flip question numerically too).
// ---------------------------------------------------------------------------
static Vector3 ProjectLS(Matrix vp, Vector3 wp)
{
    float x = wp.x * vp.m0 + wp.y * vp.m4 + wp.z * vp.m8 + vp.m12;
    float y = wp.x * vp.m1 + wp.y * vp.m5 + wp.z * vp.m9 + vp.m13;
    float z = wp.x * vp.m2 + wp.y * vp.m6 + wp.z * vp.m10 + vp.m14;
    float w = wp.x * vp.m3 + wp.y * vp.m7 + wp.z * vp.m11 + vp.m15;
    if (w == 0.0f)
        w = 1.0f;
    return (Vector3){(x / w) * 0.5f + 0.5f, (y / w) * 0.5f + 0.5f, (z / w) * 0.5f + 0.5f};
}

static void DumpAtTexel(const float *px, int res, const char *label, float u, float v)
{
    int tx = (int)(u * (res - 1));
    int tyA = (int)(v * (res - 1)); // no flip
    int tyB = (res - 1) - tyA;      // flipped
    if (tx < 0 || tx >= res || tyA < 0 || tyA >= res)
    {
        TraceLog(LOG_INFO, "ENV_SHADOW dump: %s texel (%d,%d) OUT OF RANGE", label, tx, tyA);
        return;
    }
    TraceLog(LOG_INFO, "ENV_SHADOW dump: %s uv=(%.3f,%.3f) texel=(%d,%d) stored[noflip]=%.4f stored[flip]=%.4f",
             label, u, v, tx, tyA, px[tyA * res + tx], px[tyB * res + tx]);
    // 5x5 neighborhood (no-flip orientation) — shows whether the caster is
    // right next door (small alignment error) or absent entirely.
    for (int dy = -2; dy <= 2; dy++)
    {
        int yy = tyA + dy;
        if (yy < 0 || yy >= res)
            continue;
        char row[160];
        int off = 0;
        for (int dx = -2; dx <= 2; dx++)
        {
            int xx = tx + dx;
            if (xx < 0 || xx >= res)
            {
                continue;
            }
            off += snprintf(row + off, sizeof(row) - (size_t)off, "%.3f ", px[yy * res + xx]);
        }
        TraceLog(LOG_INFO, "ENV_SHADOW dump:   row y=%d: %s", yy, row);
    }
}

void EnvShadow_DebugDump(Vector3 worldPos)
{
    if (!s_ready)
    {
        TraceLog(LOG_INFO, "ENV_SHADOW dump: not ready");
        return;
    }

    // Read back the R32F shadow map (the capture's color attachment, depth-as-color).
    float *px = (float *)rlReadTexturePixels(s_shadowMapTex.id, s_resolution, s_resolution,
                                             RL_PIXELFORMAT_UNCOMPRESSED_R32);
    if (px == NULL)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW dump: rlReadTexturePixels returned NULL");
        return;
    }

    int total = s_resolution * s_resolution;
    float mn = 2.0f, mx = -2.0f;
    int bNear = 0, bMid = 0, bFar = 0, bClear = 0; // <0.1 / 0.1-0.9 / 0.9-0.999 / >=0.999
    for (int i = 0; i < total; i++)
    {
        float d = px[i];
        if (d < mn)
            mn = d;
        if (d > mx)
            mx = d;
        if (d < 0.10f)
            bNear++;
        else if (d < 0.90f)
            bMid++;
        else if (d < 0.999f)
            bFar++;
        else
            bClear++;
    }
    TraceLog(LOG_INFO, "ENV_SHADOW dump: stored depth min=%.4f max=%.4f | <0.1:%d 0.1-0.9:%d 0.9-0.999:%d >=0.999:%d (total %d)",
             mn, mx, bNear, bMid, bFar, bClear, total);

    Vector3 caster = ProjectLS(s_lightVP, (Vector3){worldPos.x, worldPos.y + 0.8f, worldPos.z});
    Vector3 ground = ProjectLS(s_lightVP, (Vector3){worldPos.x, 0.0f, worldPos.z});
    TraceLog(LOG_INFO, "ENV_SHADOW dump: caster(worldY+0.8) proj=(%.3f,%.3f,z=%.4f)", caster.x, caster.y, caster.z);
    TraceLog(LOG_INFO, "ENV_SHADOW dump: ground(y=0)        proj=(%.3f,%.3f,z=%.4f)", ground.x, ground.y, ground.z);
    DumpAtTexel(px, s_resolution, "caster", caster.x, caster.y);
    DumpAtTexel(px, s_resolution, "ground", ground.x, ground.y);

    // Walk the expected shadow line: ground points from the caster's feet
    // along the horizontal projection of the light-travel direction. For each,
    // print the CPU proj.z, the stored depth at that texel, and PASS/FAIL of
    // the exact test the FS runs. Compare with the on-screen band colors: if
    // CPU says PASS but the screen shows blue/green there, FS z != CPU z.
    {
        Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
        Vector3 flat = Vector3Normalize((Vector3){sunDir.x, 0.0f, sunDir.z});
        for (int i = 0; i <= 12; i++)
        {
            float t = 0.5f * (float)i;
            Vector3 p = {worldPos.x + flat.x * t, 0.0f, worldPos.z + flat.z * t};
            Vector3 pr = ProjectLS(s_lightVP, p);
            int tx = (int)(pr.x * (s_resolution - 1));
            int ty = (int)(pr.y * (s_resolution - 1));
            if (tx < 0 || tx >= s_resolution || ty < 0 || ty >= s_resolution)
                continue;
            float d = px[ty * s_resolution + tx];
            TraceLog(LOG_INFO, "ENV_SHADOW line: t=%.1f proj=(%.3f,%.3f) z=%.4f stored=%.4f diff=%+.4f %s",
                     t, pr.x, pr.y, pr.z, d, pr.z - d,
                     (pr.z - 0.0015f > d) ? "PASS(shadow)" : "fail");
        }
    }

    MemFree(px);
}
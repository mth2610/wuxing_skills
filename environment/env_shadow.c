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
static EnvShadowMapCasterCallback s_mapCasterCallback = NULL;
static void *s_mapCasterUserData = NULL;

// TỐI ƯU: Biến cache hướng nắng để tránh tính toán lại ma trận mỗi frame
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
        return false;

    rlEnableFramebuffer(*outFbo);
    rlFramebufferAttach(*outFbo, *outColor, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(*outFbo, *outDepth, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(*outFbo);
    rlDisableFramebuffer();
    if (!complete)
        return false;

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
#if defined(__ANDROID__)
    // 2026-07-22, on-device (Mali-G68, rlvk/Vulkan): 512 was chosen defensively before the shadow
    // path had ever run on hardware, and it is the reason the shadow looks SHATTERED there. The
    // light frustum is fixed at halfExtent = ARENA_RADIUS + 2 = 20 m, so the map covers a 40x40 m
    // box regardless of resolution: 512 gives 12.8 texels/m (7.8 cm per texel), and at the raking
    // sun angle that is exactly the "coarse diagonal texel STREAKS" failure the desktop comment
    // below describes — only 4x worse. 1024 doubles the density to 3.9 cm/texel.
    // Cost check before raising it: on a MUCH weaker GPU (Intel Iris 6000) the whole 2048-vs-1024
    // capture-fill difference measured 0.6-1.1 ms/frame (rlvk perf_shadow_ab), so 512->1024 on a
    // G68 should be well under that. VERIFY on device with the ms HUD before going further to
    // 2048 — this is a mid-range mobile GPU and the budget is 16.6 ms.
    s_resolution = 1024;
    s_staticResolution = 512;
#else
    // 2048: at the raking sun angle the shadow is very elongated, so a lower resolution shows
    // coarse diagonal texel STREAKS across the shadow (projective aliasing). Keep it high for a
    // clean shadow. NOTE (2026-07-22): dropping to 1024 did NOT improve FPS (the O(res^2) fill of
    // the capture + copy passes is not the P6 bottleneck — the fixed per-pass overhead is), so
    // there is no perf reason to lower it. Session 3's "1024 = no shadow" was the projection bug,
    // now fixed (§7.26 + the MyBeginMode3D inverse-view fold in ground_shadow.c).
    s_resolution = 2048;
    s_staticResolution = 1024;
#endif

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
        s_ready = false;
        return;
    }

    s_staticTargetReady = CreateShadowTarget(
        s_staticResolution, &s_staticFboId, &s_staticDepthTexId,
        &s_staticColorTexId, &s_staticShadowMapTex);
    if (!s_staticTargetReady)
        TraceLog(LOG_WARNING, "ENV_SHADOW: static cache target unavailable; dynamic shadows remain active");

    s_depthShader = ResourceManager_LoadShader("environment/shaders/shadow_depth.vs",
                                               "environment/shaders/shadow_depth.fs");

    if (s_depthShader.id == 0)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: shader failed to compile, shadow map disabled");
        s_ready = false;
        return;
    }

    s_ready = true;
    s_enabled = false; // opt-in only, per plan §7 "do NOT ship enabled on Mali until profiled"
    TraceLog(LOG_INFO, "ENV_SHADOW: ready, %dx%d depth target (fbo=%u depthTex=%u shadowMapTex=%u)",
             s_resolution, s_resolution, s_fboId, s_depthTexId, s_colorTexId);
    if (s_staticTargetReady)
        TraceLog(LOG_INFO, "ENV_SHADOW: static cache target ready, %dx%d (fbo=%u shadowMapTex=%u)",
                 s_staticResolution, s_staticResolution, s_staticFboId, s_staticColorTexId);
}

void EnvShadow_SetEnabled(bool enabled) { s_enabled = s_ready && enabled; }
bool EnvShadow_IsEnabled(void) { return s_ready && s_enabled; }
bool EnvShadow_IsCapturing(void) { return s_capturing; }

void EnvShadow_SetFocus(Vector3 center, float halfExtent)
{
    if (halfExtent < 8.0f) halfExtent = 8.0f;
    if (halfExtent > 96.0f) halfExtent = 96.0f;
    // Stabilize the orthographic projection in the light plane. Without this,
    // every sub-centimetre camera movement shifts all shadow texels and makes
    // fine grass/contact edges shimmer even when the scene is otherwise still.
    if (s_resolution > 0) {
        Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
        Vector3 up = fabsf(sunDir.y) > 0.98f
            ? (Vector3){0.0f, 0.0f, 1.0f}
            : (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 lightRight = Vector3Normalize(Vector3CrossProduct(up, sunDir));
        Vector3 lightUp = Vector3Normalize(Vector3CrossProduct(sunDir, lightRight));
        float texelWorld = (halfExtent * 2.0f) / (float)s_resolution;
        float rightCoord = Vector3DotProduct(center, lightRight);
        float upCoord = Vector3DotProduct(center, lightUp);
        float snappedRight = roundf(rightCoord / texelWorld) * texelWorld;
        float snappedUp = roundf(upCoord / texelWorld) * texelWorld;
        center = Vector3Add(center, Vector3Scale(lightRight, snappedRight - rightCoord));
        center = Vector3Add(center, Vector3Scale(lightUp, snappedUp - upCoord));
    }
    s_shadowFocus = center;
    s_shadowHalfExtent = halfExtent;
}

static Matrix ComputeLightVP(void)
{
    Vector3 currentSunDir = Environment_GetSunDirection();

    // TỐI ƯU: Cache lại VP matrix. Bỏ qua các phép toán Normalize, Multiply, LookAt
    // nếu hướng nắng không thay đổi giữa các frame.
    if (Vector3DistanceSqr(currentSunDir, s_lastSunDir) < 0.000001f &&
        Vector3DistanceSqr(s_shadowFocus, s_lastShadowFocus) < 0.000001f &&
        fabsf(s_shadowHalfExtent - s_lastShadowHalfExtent) < 0.0001f)
    {
        return s_lightVP;
    }
    s_lastSunDir = currentSunDir;
    s_lastShadowFocus = s_shadowFocus;
    s_lastShadowHalfExtent = s_shadowHalfExtent;

    Vector3 sunDir = Vector3Normalize(currentSunDir); // direction light TRAVELS
    s_captureDistance = fmaxf(s_shadowHalfExtent * 1.65f, 24.0f);
    Vector3 lightPos = Vector3Subtract(s_shadowFocus, Vector3Scale(sunDir, s_captureDistance));
    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
    if (fabsf(sunDir.y) > 0.98f)
        up = (Vector3){0.0f, 0.0f, 1.0f}; // avoid degenerate look-at

    s_lightView = MatrixLookAt(lightPos, s_shadowFocus, up);
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
    Matrix vp = MatrixMultiply(s_lightView, s_lightProj);
    return vp;
}

static Matrix ComputeStaticLightVP(Vector3 center, float halfExtent)
{
    if (halfExtent < 8.0f) halfExtent = 8.0f;
    if (halfExtent > 128.0f) halfExtent = 128.0f;
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    Vector3 up = fabsf(sunDir.y) > 0.98f
        ? (Vector3){0.0f, 0.0f, 1.0f}
        : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 lightRight = Vector3Normalize(Vector3CrossProduct(up, sunDir));
    Vector3 lightUp = Vector3Normalize(Vector3CrossProduct(sunDir, lightRight));
    float texelWorld = halfExtent * 2.0f / (float)s_staticResolution;
    float rightCoord = Vector3DotProduct(center, lightRight);
    float upCoord = Vector3DotProduct(center, lightUp);
    center = Vector3Add(center, Vector3Scale(
        lightRight, roundf(rightCoord / texelWorld) * texelWorld - rightCoord));
    center = Vector3Add(center, Vector3Scale(
        lightUp, roundf(upCoord / texelWorld) * texelWorld - upCoord));

    s_staticCaptureDistance = fmaxf(halfExtent * 1.65f, 24.0f);
    Vector3 lightPos = Vector3Subtract(
        center, Vector3Scale(sunDir, s_staticCaptureDistance));
    s_staticLightView = MatrixLookAt(lightPos, center, up);
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
    if (s_mapCasterCallback != NULL) {
        s_mapCasterCallback(s_depthShader, s_mapCasterUserData);
        // DrawModel uses its material shader and disables it afterward. Restore
        // the environment depth shader for engine-owned casters that follow.
        BeginShaderMode(s_depthShader);
    }
}

void EnvShadow_EndCapture(void)
{
    if (!s_ready || !s_enabled || !s_capturing || s_staticCaptureOpen)
        return;
    EndCaptureTarget();
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
    if (halfExtent < 8.0f) halfExtent = 8.0f;
    if (halfExtent > 128.0f) halfExtent = 128.0f;
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
    TraceLog(LOG_INFO, "ENV_SHADOW: static cache captured (%dx%d)",
             s_staticResolution, s_staticResolution);
    if (getenv("WUXING_SHADOW_STATIC_VERIFY") != NULL) {
        float *pixels = (float *)rlReadTexturePixels(
            s_staticShadowMapTex.id, s_staticResolution, s_staticResolution,
            RL_PIXELFORMAT_UNCOMPRESSED_R32);
        if (pixels != NULL) {
            int occupied = 0;
            float minDepth = 1.0f;
            int total = s_staticResolution * s_staticResolution;
            for (int i = 0; i < total; i++) {
                if (pixels[i] < minDepth) minDepth = pixels[i];
                if (pixels[i] < 0.999f) occupied++;
            }
            TraceLog(LOG_INFO,
                     "ENV_SHADOW: static verify minDepth=%.4f occupied=%d/%d",
                     minDepth, occupied, total);
            MemFree(pixels);
        } else {
            TraceLog(LOG_WARNING, "ENV_SHADOW: static verify readback failed");
        }
    }
}

void EnvShadow_InvalidateStaticCache(void)
{
    s_staticCacheValid = false;
}

bool EnvShadow_HasStaticCache(void)
{
    if (!s_ready || !s_enabled || !s_staticTargetReady || !s_staticCacheValid)
        return false;
    Vector3 current = Vector3Normalize(Environment_GetSunDirection());
    return Vector3DistanceSqr(current, s_staticSunDir) < 0.000001f;
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

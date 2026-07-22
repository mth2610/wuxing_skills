#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>   // fabsf
#include <stddef.h> // NULL
#include <stdio.h>  // snprintf (EnvShadow_DebugDump)

// Arena bounds (root CLAUDE.md "Standard coordinates & scale") — the light's
// orthographic frustum is sized to cover this, not the whole level.
#define ARENA_CENTER ((Vector3){6.0f, 1.0f, 4.4f}) // radius 18, y raised slightly (was 3, too far above the y=0 ground the shadow actually needs to cover)
#define ARENA_RADIUS 18.0f

static bool s_ready = false;
static bool s_enabled = false;
static bool s_capturing = false; // true between Begin/EndCapture (EnvShadow_IsCapturing)
static unsigned int s_fboId = 0;
static unsigned int s_depthTexId = 0;
static unsigned int s_colorTexId = 0;
static int s_resolution = 0;
static Shader s_depthShader;
static Matrix s_lightView, s_lightProj, s_lightVP;
static Texture2D s_depthTex2D; // wraps s_depthTexId, source for the copy pass below

// The depth attachment above is NOT directly sampled anywhere — this
// project's rlvk (Vulkan/MoltenVK) backend has a device quirk
// ("noSampledDepth", logged at startup on Intel/MoltenVK) where FBO
// depth-attachment textures aren't reliably sampleable from an arbitrary 3D
// shader.
// NOTE TỐI ƯU: Đã xóa s_copyRT và s_copyShader khỏi đây vì depth shader đã
// ghi trực tiếp gl_FragCoord.z vào s_colorTexId (R32F). Tiết kiệm ~16MB VRAM.
static Texture2D s_shadowMapTex; // this is what callers get

// TỐI ƯU: Biến cache hướng nắng để tránh tính toán lại ma trận mỗi frame
static Vector3 s_lastSunDir = {0};

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
#else
    // 2048: at the raking sun angle the shadow is very elongated, so a lower resolution shows
    // coarse diagonal texel STREAKS across the shadow (projective aliasing). Keep it high for a
    // clean shadow. NOTE (2026-07-22): dropping to 1024 did NOT improve FPS (the O(res^2) fill of
    // the capture + copy passes is not the P6 bottleneck — the fixed per-pass overhead is), so
    // there is no perf reason to lower it. Session 3's "1024 = no shadow" was the projection bug,
    // now fixed (§7.26 + the MyBeginMode3D inverse-view fold in ground_shadow.c).
    s_resolution = 2048;
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
    s_colorTexId = rlLoadTexture(NULL, s_resolution, s_resolution, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    // NOTE (§7.27, OPEN PERF ISSUE): this depth attachment is only ever depth-TESTED against — the
    // shadow map we sample is the R32F COLOR attachment above. But rlvk still builds a
    // Caps.noSampledDepth "sampleable twin" for it and bounces depth->buffer->twin at EVERY scope
    // close (~32 MB/frame at 2048) for a twin nobody reads. That is the prime suspect for the
    // remaining shadow cost. Do NOT "fix" it by passing useRenderBuffer=true here: raylib's own
    // LoadRenderTexture also passes true, so honouring it in rlvk strips the twin from every render
    // texture and breaks soft-particle depth sampling (VUID-…-oldLayout-01211). See the notes.
    s_depthTexId = rlLoadTextureDepth(s_resolution, s_resolution, false); // real texture, not a renderbuffer
    s_fboId = rlLoadFramebuffer();
    if (s_fboId == 0 || s_depthTexId == 0 || s_colorTexId == 0)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: failed to allocate depth FBO/texture, shadow map disabled");
        s_ready = false;
        return;
    }

    rlEnableFramebuffer(s_fboId);
    rlFramebufferAttach(s_fboId, s_colorTexId, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(s_fboId, s_depthTexId, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(s_fboId);
    rlDisableFramebuffer();

    if (!complete)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: depth FBO incomplete, shadow map disabled");
        s_ready = false;
        return;
    }

    s_depthShader = ResourceManager_LoadShader("environment/shaders/shadow_depth.vs",
                                               "environment/shaders/shadow_depth.fs");

    if (s_depthShader.id == 0)
    {
        TraceLog(LOG_WARNING, "ENV_SHADOW: shader failed to compile, shadow map disabled");
        s_ready = false;
        return;
    }

    s_depthTex2D = (Texture2D){
        .id = s_depthTexId,
        .width = s_resolution,
        .height = s_resolution,
        .mipmaps = 1,
        .format = 19 // DEPTH_COMPONENT_24BIT (rlgl internal format id) — matches core/screen_distort.c's depth-texture convention
    };
    // The shadow map is now the capture FBO's R32F color attachment (depth-as-color), sampled
    // directly — the s_copyRT copy target is retained only for EnvShadow_DebugDump readback.
    s_shadowMapTex = (Texture2D){
        .id = s_colorTexId,
        .width = s_resolution,
        .height = s_resolution,
        .mipmaps = 1,
        .format = RL_PIXELFORMAT_UNCOMPRESSED_R32,
    };
    // R32F is not linear-filterable on many GL3.3 drivers (same note as
    // screen_distort.c) — force POINT/CLAMP.
    SetTextureFilter(s_shadowMapTex, TEXTURE_FILTER_POINT);
    SetTextureWrap(s_shadowMapTex, TEXTURE_WRAP_CLAMP);

    s_ready = true;
    s_enabled = false; // opt-in only, per plan §7 "do NOT ship enabled on Mali until profiled"
    TraceLog(LOG_INFO, "ENV_SHADOW: ready, %dx%d depth target (fbo=%u depthTex=%u shadowMapTex=%u)",
             s_resolution, s_resolution, s_fboId, s_depthTexId, s_colorTexId);
}

void EnvShadow_SetEnabled(bool enabled) { s_enabled = s_ready && enabled; }
bool EnvShadow_IsEnabled(void) { return s_ready && s_enabled; }
bool EnvShadow_IsCapturing(void) { return s_capturing; }

static Matrix ComputeLightVP(void)
{
    Vector3 currentSunDir = Environment_GetSunDirection();

    // TỐI ƯU: Cache lại VP matrix. Bỏ qua các phép toán Normalize, Multiply, LookAt
    // nếu hướng nắng không thay đổi giữa các frame.
    if (Vector3DistanceSqr(currentSunDir, s_lastSunDir) < 0.000001f)
    {
        return s_lightVP;
    }
    s_lastSunDir = currentSunDir;

    Vector3 sunDir = Vector3Normalize(currentSunDir); // direction light TRAVELS
    float distance = ARENA_RADIUS + 6.0f;
    Vector3 lightPos = Vector3Subtract(ARENA_CENTER, Vector3Scale(sunDir, distance));
    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
    if (fabsf(sunDir.y) > 0.98f)
        up = (Vector3){0.0f, 0.0f, 1.0f}; // avoid degenerate look-at

    s_lightView = MatrixLookAt(lightPos, ARENA_CENTER, up);
    // ARENA_RADIUS+2: covers the 18m-radius arena with margin. (A debug
    // session once "measured" the character outside this frustum and bumped
    // it to 45 — but that measurement used the pre-fix TRANSPOSED matrix in
    // the FS, so it was garbage. The CPU numeric dump with the corrected
    // math confirms 20 frames the arena fine, and the tighter box more than
    // doubles texel density: at 45, a 0.5m character was ~6 texels wide with
    // rasterization holes — the shadow existed but as a near-invisible
    // speckle, which is exactly what "no shadow visible" looked like.)
    float halfExtent = ARENA_RADIUS + 2.0f;
    s_lightProj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1f, distance * 2.0f);
    Matrix vp = MatrixMultiply(s_lightView, s_lightProj);
    return vp;
}

void EnvShadow_BeginCapture(void)
{
    if (!s_ready || !s_enabled)
        return;

    s_lightVP = ComputeLightVP();
    s_capturing = true; // scene draw code skips non-casters while this is set

    rlDrawRenderBatchActive(); // flush the main pass's pending batch before switching FBO/matrices

    // Force depth test/write ON before the clear+capture: whatever state the
    // previous frame's 2D/UI drawing left behind (commonly OFF) would
    // otherwise make rlClearScreenBuffers()'s depth clear a no-op (GL only
    // clears the depth buffer when the depth mask is enabled) and every
    // subsequent draw would fail to WRITE depth too — silently producing an
    // all-far shadow map (nothing ever occludes -> ShadowFactor always 1.0).
    rlEnableDepthTest();
    rlEnableDepthMask();

    rlEnableFramebuffer(s_fboId);
    rlViewport(0, 0, s_resolution, s_resolution);
    // Clear the R32F color to WHITE (1.0 = far / "no occluder") so untouched texels read as lit;
    // depth clears to 1.0 too. Without this the color would clear to black (0.0) and every ground
    // fragment would test as shadowed. (Matches shadow_cast's ClearBackground(WHITE).)
    rlClearColor(255, 255, 255, 255);
    rlClearScreenBuffers();

    // Projection: use rlOrtho (rlgl builds the matrix internally) rather than
    // rlMultMatrixf(MatrixToFloat(s_lightProj)). main.c's proven-working
    // MyBeginMode3D uses rlOrtho/rlFrustum for the projection and only uses
    // rlMultMatrixf(MatrixToFloat(...)) for the VIEW — the manual-Matrix path
    // is untested-under-rlvk for projection matrices and was cramming the
    // stored depth against the far plane (bug 3, REAL_SHADING_P6_NOTES.md).
    // Must match the params used to build s_lightProj (MatrixOrtho) so the
    // captured depth agrees with the FS's proj.z.
    float distance = ARENA_RADIUS + 6.0f;
    float halfExtent = ARENA_RADIUS + 2.0f; // MUST match ComputeLightVP
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1, distance * 2.0);

    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(s_lightView));

    BeginShaderMode(s_depthShader); // primitive/shape draws (DrawSphere etc.) pick this up automatically;
                                    // Model draws don't — see SurfaceMaterial_BeginShadowCast.
}

void EnvShadow_EndCapture(void)
{
    if (!s_ready || !s_enabled)
        return;

    EndShaderMode();
    rlDrawRenderBatchActive();

    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    rlLoadIdentity();

    rlDisableFramebuffer(); // scope close transitions the R32F color attachment to SHADER_READ_ONLY

    // NO copy pass: the depth shader already wrote gl_FragCoord.z into the R32F color attachment
    // (s_colorTexId = s_shadowMapTex), which the receivers sample directly. This removes a full
    // render pass + the §7.10 depth-sample twin bounce per frame (the P6 FPS cost on MoltenVK/Intel).

    rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());
    s_capturing = false;
    // Modelview/projection get overwritten again by the normal camera's
    // BeginMode3D right after this returns — no explicit restore needed.
}

Shader EnvShadow_GetDepthShader(void) { return s_depthShader; }
Matrix EnvShadow_GetLightVP(void) { return s_lightVP; }
// The R32F color attachment the depth shader wrote gl_FragCoord.z into (depth-as-color).
Texture2D EnvShadow_GetShadowMap(void) { return s_shadowMapTex; }

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
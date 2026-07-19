#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>   // fabsf
#include <stddef.h> // NULL

// Arena bounds (root CLAUDE.md "Standard coordinates & scale") — the light's
// orthographic frustum is sized to cover this, not the whole level.
#define ARENA_CENTER ((Vector3){ 6.0f, 1.0f, 4.4f }) // radius 18, y raised slightly (was 3, too far above the y=0 ground the shadow actually needs to cover)
#define ARENA_RADIUS 18.0f

static bool     s_ready = false;
static bool     s_enabled = false;
static unsigned int s_fboId = 0;
static unsigned int s_depthTexId = 0;
static unsigned int s_colorTexId = 0;
static int      s_resolution = 0;
static Shader   s_depthShader;
static Matrix   s_lightView, s_lightProj, s_lightVP;
static Texture2D s_depthTex2D; // wraps s_depthTexId, source for the copy pass below

// The depth attachment above is NOT directly sampled anywhere — this
// project's rlvk (Vulkan/MoltenVK) backend has a device quirk
// ("noSampledDepth", logged at startup on Intel/MoltenVK) where FBO
// depth-attachment textures aren't reliably sampleable from an arbitrary 3D
// shader. The proven-working pattern already used by core/screen_distort.c
// for soft particles is: copy the depth into a plain R32F COLOR texture via
// a fullscreen-quad shader pass, then sample THAT everywhere else.
static RenderTexture2D s_copyRT = { 0 };
static Shader          s_copyShader;
static Texture2D       s_shadowMapTex; // = s_copyRT.texture; this is what callers get

void EnvShadow_Init(void) {
#if defined(__ANDROID__)
    s_resolution = 512; // Mali class — smaller depth target
#else
    s_resolution = 1024;
#endif

    // Depth + throwaway color attachment — same recipe as
    // core/screen_distort.c's LoadRenderTextureWithDepthTexture, which is
    // proven to work on this project's rlvk (Vulkan) backend already. A
    // depth-ONLY FBO (rlActiveDrawBuffers(0), no color attachment) is a
    // different, untested configuration under a custom Vulkan reimplementation
    // of rlgl — not worth the risk here, the color texture is never sampled.
    s_colorTexId = rlLoadTexture(NULL, s_resolution, s_resolution, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    s_depthTexId = rlLoadTextureDepth(s_resolution, s_resolution, false); // real texture, not a renderbuffer
    s_fboId = rlLoadFramebuffer();
    if (s_fboId == 0 || s_depthTexId == 0 || s_colorTexId == 0) {
        TraceLog(LOG_WARNING, "ENV_SHADOW: failed to allocate depth FBO/texture, shadow map disabled");
        s_ready = false;
        return;
    }

    rlEnableFramebuffer(s_fboId);
    rlFramebufferAttach(s_fboId, s_colorTexId, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(s_fboId, s_depthTexId, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(s_fboId);
    rlDisableFramebuffer();

    if (!complete) {
        TraceLog(LOG_WARNING, "ENV_SHADOW: depth FBO incomplete, shadow map disabled");
        s_ready = false;
        return;
    }

    // R32F copy target (color-only FBO) — same construction as
    // core/screen_distort.c's LoadLinearDepthTarget, driven through the
    // high-level BeginTextureMode/EndTextureMode API (not raw rlgl calls)
    // so the 2D orthographic projection for the fullscreen-quad copy is set
    // up correctly automatically.
    s_copyRT.id = rlLoadFramebuffer();
    s_copyRT.texture.id = rlLoadTexture(NULL, s_resolution, s_resolution, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    s_copyRT.texture.width = s_resolution;
    s_copyRT.texture.height = s_resolution;
    s_copyRT.texture.mipmaps = 1;
    s_copyRT.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    if (s_copyRT.id == 0 || s_copyRT.texture.id == 0) {
        TraceLog(LOG_WARNING, "ENV_SHADOW: failed to allocate shadow-copy FBO/texture, shadow map disabled");
        s_ready = false;
        return;
    }
    rlEnableFramebuffer(s_copyRT.id);
    rlFramebufferAttach(s_copyRT.id, s_copyRT.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    bool copyComplete = rlFramebufferComplete(s_copyRT.id);
    rlDisableFramebuffer();
    if (!copyComplete) {
        TraceLog(LOG_WARNING, "ENV_SHADOW: shadow-copy FBO incomplete, shadow map disabled");
        s_ready = false;
        return;
    }

    s_depthShader = ResourceManager_LoadShader("environment/shaders/shadow_depth.vs",
                                                "environment/shaders/shadow_depth.fs");
    s_copyShader = ResourceManager_LoadShader(NULL, "environment/shaders/shadow_copy.fs");
    if (s_depthShader.id == 0 || s_copyShader.id == 0) {
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
    s_shadowMapTex = s_copyRT.texture;
    // R32F is not linear-filterable on many GL3.3 drivers (same note as
    // screen_distort.c) — force POINT/CLAMP.
    SetTextureFilter(s_shadowMapTex, TEXTURE_FILTER_POINT);
    SetTextureWrap(s_shadowMapTex, TEXTURE_WRAP_CLAMP);

    s_ready = true;
    s_enabled = false; // opt-in only, per plan §7 "do NOT ship enabled on Mali until profiled"
    TraceLog(LOG_INFO, "ENV_SHADOW: ready, %dx%d depth target (fbo=%u depthTex=%u copyFbo=%u copyTex=%u)",
             s_resolution, s_resolution, s_fboId, s_depthTexId, s_copyRT.id, s_copyRT.texture.id);
}

void EnvShadow_SetEnabled(bool enabled) { s_enabled = s_ready && enabled; }
bool EnvShadow_IsEnabled(void) { return s_ready && s_enabled; }

static Matrix ComputeLightVP(void) {
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection()); // direction light TRAVELS
    float distance = ARENA_RADIUS + 6.0f;
    Vector3 lightPos = Vector3Subtract(ARENA_CENTER, Vector3Scale(sunDir, distance));
    Vector3 up = (Vector3){ 0.0f, 1.0f, 0.0f };
    if (fabsf(sunDir.y) > 0.98f) up = (Vector3){ 0.0f, 0.0f, 1.0f }; // avoid degenerate look-at

    s_lightView = MatrixLookAt(lightPos, ARENA_CENTER, up);
    // ARENA_RADIUS+2 (20) still left the character standing OUTSIDE the
    // frustum: the ortho box is built in the LIGHT's own local X/Y axes, and
    // with a low sun elevation those axes are tilted well away from the
    // world XZ ground plane — the flat 18m-radius disc's footprint in that
    // tilted local frame is elongated well past its true world radius (the
    // debug view showed a hard diagonal frustum-edge cutting right through
    // the visible arena/character). Sized generously past that distortion
    // rather than computing the exact projected footprint.
    float halfExtent = 45.0f;
    s_lightProj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1f, distance * 2.0f);
    Matrix vp = MatrixMultiply(s_lightView, s_lightProj);
    return vp;
}

void EnvShadow_BeginCapture(void) {
    if (!s_ready || !s_enabled) return;

    s_lightVP = ComputeLightVP();

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
    rlClearScreenBuffers();

    // rlMatrixMode/rlPushMatrix/rlLoadIdentity/rlMultMatrixf — NOT the direct
    // rlSetMatrixModelview/rlSetMatrixProjection setters. Those setters are
    // used nowhere else in this codebase (grepped), unlike this push/pop
    // idiom which is exactly what main.c's MyBeginMode3D uses for the game's
    // own (proven-working) orthographic camera — matching it removes an
    // untested-under-rlvk code path as a variable.
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(s_lightProj));

    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(s_lightView));

    BeginShaderMode(s_depthShader); // primitive/shape draws (DrawSphere etc.) pick this up automatically;
                                     // Model draws don't — see SurfaceMaterial_BeginShadowCast.
}

void EnvShadow_EndCapture(void) {
    if (!s_ready || !s_enabled) return;

    EndShaderMode();
    rlDrawRenderBatchActive();

    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    rlLoadIdentity();

    rlDisableFramebuffer();

    // Copy depth -> R32F color texture (see the comment on s_copyRT above —
    // sampling the depth attachment directly from surface_lit.fs/
    // ground_shadow.fs is the pattern this project's rlvk quirk breaks).
    // BeginTextureMode/EndTextureMode (not raw rlgl calls) so the 2D
    // orthographic projection for this fullscreen-quad copy is set up
    // correctly — exactly ScreenDistort_SnapshotDepth's pattern.
    BeginTextureMode(s_copyRT);
    BeginShaderMode(s_copyShader);
    // NOTE (2026-07-19 debugging): tried both with and without this negative
    // height (Y-flip) — neither fixed the "zero occlusion detected" symptom,
    // so it's inconclusive either way. Kept matching ScreenDistort_SnapshotDepth's
    // proven pattern since that's the safer default. See REAL_SHADING_P6_NOTES.md.
    DrawTextureRec(s_depthTex2D, (Rectangle){ 0, 0, (float)s_resolution, -(float)s_resolution },
                    (Vector2){ 0, 0 }, WHITE);
    EndShaderMode();
    EndTextureMode();

    rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());
    // Modelview/projection get overwritten again by the normal camera's
    // BeginMode3D right after this returns — no explicit restore needed.
}

Shader EnvShadow_GetDepthShader(void) { return s_depthShader; }
Matrix EnvShadow_GetLightVP(void) { return s_lightVP; }
Texture2D EnvShadow_GetShadowMap(void) { return s_shadowMapTex; }

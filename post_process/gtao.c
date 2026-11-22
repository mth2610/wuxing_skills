#include "post_process/gtao.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

extern bool rlvkSetFramebufferDiscardOnBegin(unsigned int fbId, bool discard) __attribute__((weak));

static RenderTexture2D s_aoRaw = {0};
static RenderTexture2D s_aoDenoised = {0};
static Shader s_gtaoShader = {0};
static Shader s_denoiseShader = {0};

static int s_locTexelSize = -1;
static int s_locProjScale = -1;
static int s_locRadius = -1;
static int s_locBias = -1;

static int s_locDenDepthTex = -1;
static int s_locDenTexelSize = -1;

static int s_halfW = 0;
static int s_halfH = 0;
static bool s_enabled = true;

static RenderTexture2D LoadAoTarget(int w, int h)
{
    RenderTexture2D target = {0};
    target.id = rlLoadFramebuffer();
    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);
        target.texture.id = rlLoadTexture(NULL, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.texture.width = w;
        target.texture.height = h;
        target.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        target.texture.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlDisableFramebuffer();
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
        if (rlvkSetFramebufferDiscardOnBegin)
            rlvkSetFramebufferDiscardOnBegin(target.id, true);
        BeginTextureMode(target);
        ClearBackground(WHITE);
        EndTextureMode();
    }
    return target;
}

static void DestroyTargets(void)
{
    if (s_aoRaw.id > 0) UnloadRenderTexture(s_aoRaw);
    if (s_aoDenoised.id > 0) UnloadRenderTexture(s_aoDenoised);
    s_aoRaw = (RenderTexture2D){0};
    s_aoDenoised = (RenderTexture2D){0};
}

static void CreateTargets(int width, int height)
{
    int hw = (width / 2 > 4) ? (width / 2) : 4;
    int hh = (height / 2 > 4) ? (height / 2) : 4;
    s_halfW = hw;
    s_halfH = hh;
    s_aoRaw = LoadAoTarget(hw, hh);
    s_aoDenoised = LoadAoTarget(hw, hh);
}

void GTAO_Init(int width, int height)
{
    const char *env = getenv("WUXING_GTAO");
    if (env != NULL)
        s_enabled = (atoi(env) != 0);

    if (s_gtaoShader.id == 0)
    {
        s_gtaoShader = LoadShader(0, "post_process/shaders/gtao.fs");
        s_locTexelSize = GetShaderLocation(s_gtaoShader, "u_texelSize");
        s_locProjScale = GetShaderLocation(s_gtaoShader, "u_projScale");
        s_locRadius = GetShaderLocation(s_gtaoShader, "u_radius");
        s_locBias = GetShaderLocation(s_gtaoShader, "u_bias");
    }
    if (s_denoiseShader.id == 0)
    {
        s_denoiseShader = LoadShader(0, "post_process/shaders/gtao_denoise.fs");
        s_locDenDepthTex = GetShaderLocation(s_denoiseShader, "u_depthTex");
        s_locDenTexelSize = GetShaderLocation(s_denoiseShader, "u_texelSize");
    }
    DestroyTargets();
    CreateTargets(width, height);
}

void GTAO_Resize(int width, int height)
{
    int hw = (width / 2 > 4) ? (width / 2) : 4;
    int hh = (height / 2 > 4) ? (height / 2) : 4;
    if (hw == s_halfW && hh == s_halfH && s_aoRaw.id != 0)
        return;
    DestroyTargets();
    CreateTargets(width, height);
}

void GTAO_Shutdown(void)
{
    DestroyTargets();
    if (s_gtaoShader.id != 0) UnloadShader(s_gtaoShader);
    if (s_denoiseShader.id != 0) UnloadShader(s_denoiseShader);
    s_gtaoShader = (Shader){0};
    s_denoiseShader = (Shader){0};
    s_halfW = 0;
    s_halfH = 0;
}

void GTAO_SetEnabled(bool enabled)
{
    const char *env = getenv("WUXING_GTAO");
    if (env != NULL)
        enabled = (atoi(env) != 0);
    s_enabled = enabled;
}

bool GTAO_IsEnabled(void)
{
    return s_enabled && s_gtaoShader.id != 0 && s_denoiseShader.id != 0 &&
           s_aoRaw.id != 0 && s_aoDenoised.id != 0;
}

Texture2D GTAO_Render(Texture2D linearDepth, Camera3D camera)
{
    if (!GTAO_IsEnabled() || camera.projection == CAMERA_ORTHOGRAPHIC ||
        linearDepth.id == 0 || linearDepth.width <= 0 || linearDepth.height <= 0)
        return (Texture2D){0};

    float tanHalfFov = tanf(camera.fovy * 0.5f * DEG2RAD);
    float aspect = (s_halfH > 0) ? ((float)s_halfW / (float)s_halfH) : 1.7777f;
    float projScale[2] = {tanHalfFov * aspect, tanHalfFov};
    float texelSize[2] = {1.0f / (float)s_halfW, 1.0f / (float)s_halfH};
    float radius = 1.35f;
    float bias = 0.035f;

    // Pass 1: Half-res GTAO horizon integration
    BeginTextureMode(s_aoRaw);
    BeginShaderMode(s_gtaoShader);
    if (s_locTexelSize >= 0) SetShaderValue(s_gtaoShader, s_locTexelSize, texelSize, SHADER_UNIFORM_VEC2);
    if (s_locProjScale >= 0) SetShaderValue(s_gtaoShader, s_locProjScale, projScale, SHADER_UNIFORM_VEC2);
    if (s_locRadius >= 0) SetShaderValue(s_gtaoShader, s_locRadius, &radius, SHADER_UNIFORM_FLOAT);
    if (s_locBias >= 0) SetShaderValue(s_gtaoShader, s_locBias, &bias, SHADER_UNIFORM_FLOAT);

    rlDisableColorBlend();
    DrawTexturePro(linearDepth,
                   (Rectangle){0.0f, 0.0f, (float)linearDepth.width, -(float)linearDepth.height},
                   (Rectangle){0.0f, 0.0f, (float)s_halfW, (float)s_halfH},
                   (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();

    // Pass 2: Depth-aware bilateral cross denoise
    rlEnableFramebuffer(s_aoDenoised.id);
    rlViewport(0, 0, s_halfW, s_halfH);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0.0, (double)s_halfW, (double)s_halfH, 0.0, 0.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();

    BeginShaderMode(s_denoiseShader);
    if (s_locDenDepthTex >= 0) SetShaderValueTexture(s_denoiseShader, s_locDenDepthTex, linearDepth);
    if (s_locDenTexelSize >= 0) SetShaderValue(s_denoiseShader, s_locDenTexelSize, texelSize, SHADER_UNIFORM_VEC2);

    rlDisableColorBlend();
    DrawTexturePro(s_aoRaw.texture,
                   (Rectangle){0.0f, 0.0f, (float)s_halfW, -(float)s_halfH},
                   (Rectangle){0.0f, 0.0f, (float)s_halfW, (float)s_halfH},
                   (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
    EndTextureMode();

    return s_aoDenoised.texture;
}

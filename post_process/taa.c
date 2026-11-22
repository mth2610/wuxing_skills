#include "taa.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

extern bool rlvkSetFramebufferDiscardOnBegin(unsigned int fbId, bool discard) __attribute__((weak));

static RenderTexture2D s_history[2] = {0};
static Shader s_taaShader = {0};
static int s_locHistoryTex = -1;
static int s_locDepthTex = -1;
static int s_locInvCurrViewProj = -1;
static int s_locPrevViewProj = -1;
static int s_locCamPos = -1;
static int s_locViewForward = -1;
static int s_locTexelSize = -1;
static int s_locFirstFrame = -1;

static int s_width = 0;
static int s_height = 0;
static int s_writeIdx = 0;
static int s_frameIdx = 0;
static bool s_enabled = true;
static bool s_historyValid = false;
static float s_jitterX = 0.0f;
static float s_jitterY = 0.0f;
static Matrix s_currViewProj = {0};
static Matrix s_prevViewProj = {0};
static bool s_hasViewProj = false;

static float Halton(int index, int base)
{
    float f = 1.0f;
    float r = 0.0f;
    int i = index;
    while (i > 0)
    {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

static RenderTexture2D LoadHdrTarget(int width, int height)
{
    RenderTexture2D target = {0};
    target.id = rlLoadFramebuffer();
    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);
        target.texture.id = rlLoadTexture(NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
        target.texture.width = width;
        target.texture.height = height;
        target.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        target.texture.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlDisableFramebuffer();
        SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
        if (rlvkSetFramebufferDiscardOnBegin)
            rlvkSetFramebufferDiscardOnBegin(target.id, true);
        BeginTextureMode(target);
        ClearBackground(BLACK);
        EndTextureMode();
    }
    return target;
}

static void DestroyTargets(void)
{
    for (int i = 0; i < 2; i++)
    {
        if (s_history[i].id > 0)
            UnloadRenderTexture(s_history[i]);
        s_history[i] = (RenderTexture2D){0};
    }
    s_historyValid = false;
}

static void CreateTargets(int width, int height)
{
    if (width < 8 || height < 8)
        return;
    s_width = width;
    s_height = height;
    for (int i = 0; i < 2; i++)
        s_history[i] = LoadHdrTarget(width, height);
    s_writeIdx = 0;
    s_historyValid = false;
}

static Matrix ComputeUnjitteredViewProj(Camera3D camera, int width, int height)
{
    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    float aspect = (height > 0) ? ((float)width / (float)height) : 1.0f;
    Matrix matProj;
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        double top = camera.fovy * 0.5;
        double right = top * aspect;
        matProj = MatrixOrtho(-right, right, -top, top, 0.0001, 150.0);
    }
    else
    {
        double top = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
        double right = top * aspect;
        matProj = MatrixFrustum(-right, right, -top, top, 1.0, 1000.0);
    }
    return MatrixMultiply(matView, matProj);
}

void TAA_Init(int width, int height)
{
    const char *env = getenv("WUXING_TAA");
    if (env != NULL)
        s_enabled = (atoi(env) != 0);

    if (s_taaShader.id == 0)
    {
#ifdef __ANDROID__
        s_taaShader = LoadShader(0, "post_process/shaders/taa.fs");
#else
        s_taaShader = LoadShader(0, "post_process/shaders/taa.fs");
#endif
        s_locHistoryTex = GetShaderLocation(s_taaShader, "u_historyTex");
        s_locDepthTex = GetShaderLocation(s_taaShader, "u_depthTex");
        s_locInvCurrViewProj = GetShaderLocation(s_taaShader, "u_invCurrViewProj");
        s_locPrevViewProj = GetShaderLocation(s_taaShader, "u_prevViewProj");
        s_locCamPos = GetShaderLocation(s_taaShader, "u_camPos");
        s_locViewForward = GetShaderLocation(s_taaShader, "u_viewForward");
        s_locTexelSize = GetShaderLocation(s_taaShader, "u_texelSize");
        s_locFirstFrame = GetShaderLocation(s_taaShader, "u_firstFrame");
    }
    CreateTargets(width, height);
}

void TAA_Resize(int width, int height)
{
    if (width < 8 || height < 8)
        return;
    if (width == s_width && height == s_height && s_history[0].id != 0)
        return;
    DestroyTargets();
    CreateTargets(width, height);
}

void TAA_Shutdown(void)
{
    DestroyTargets();
    if (s_taaShader.id != 0)
    {
        UnloadShader(s_taaShader);
        s_taaShader = (Shader){0};
    }
    s_width = 0;
    s_height = 0;
    s_hasViewProj = false;
}

void TAA_SetEnabled(bool enabled)
{
    const char *env = getenv("WUXING_TAA");
    if (env != NULL)
        enabled = (atoi(env) != 0);
    if (s_enabled != enabled)
    {
        s_enabled = enabled;
        s_historyValid = false;
    }
}

bool TAA_IsEnabled(void)
{
    return s_enabled && s_taaShader.id != 0 && s_history[0].id != 0 && s_history[1].id != 0;
}

void TAA_NextFrame(Camera3D camera, int width, int height)
{
    if (!TAA_IsEnabled())
    {
        s_jitterX = 0.0f;
        s_jitterY = 0.0f;
        return;
    }
    if (s_hasViewProj)
        s_prevViewProj = s_currViewProj;
    s_currViewProj = ComputeUnjitteredViewProj(camera, width, height);
    if (!s_hasViewProj)
    {
        s_prevViewProj = s_currViewProj;
        s_hasViewProj = true;
    }

    int idx = (s_frameIdx & 7) + 1; // 8-sample Halton(2, 3)
    s_jitterX = Halton(idx, 2) - 0.5f;
    s_jitterY = Halton(idx, 3) - 0.5f;
    s_frameIdx++;
}

void TAA_GetJitterPixels(float *outJitterX, float *outJitterY)
{
    if (!TAA_IsEnabled())
    {
        if (outJitterX) *outJitterX = 0.0f;
        if (outJitterY) *outJitterY = 0.0f;
        return;
    }
    if (outJitterX) *outJitterX = s_jitterX;
    if (outJitterY) *outJitterY = s_jitterY;
}

Texture2D TAA_Resolve(Texture2D currHdr, Texture2D linearDepth, Camera3D camera)
{
    if (!TAA_IsEnabled() || camera.projection == CAMERA_ORTHOGRAPHIC ||
        currHdr.id == 0 || currHdr.width <= 0 || currHdr.height <= 0)
        return currHdr;
    if (currHdr.width != s_width || currHdr.height != s_height)
        TAA_Resize(currHdr.width, currHdr.height);

    int writeIdx = s_writeIdx;
    int readIdx = 1 - writeIdx;
    RenderTexture2D dst = s_history[writeIdx];
    Texture2D histTex = s_history[readIdx].texture;

    Matrix invCurrVP = MatrixInvert(s_currViewProj);
    Vector3 viewFwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float texel[2] = {1.0f / (float)currHdr.width, 1.0f / (float)currHdr.height};
    float firstFrame = s_historyValid ? 0.0f : 1.0f;

    BeginTextureMode(dst);
    BeginShaderMode(s_taaShader);
    if (s_locHistoryTex >= 0) SetShaderValueTexture(s_taaShader, s_locHistoryTex, histTex);
    if (s_locDepthTex >= 0 && linearDepth.id != 0) SetShaderValueTexture(s_taaShader, s_locDepthTex, linearDepth);
    if (s_locInvCurrViewProj >= 0) SetShaderValueMatrix(s_taaShader, s_locInvCurrViewProj, invCurrVP);
    if (s_locPrevViewProj >= 0) SetShaderValueMatrix(s_taaShader, s_locPrevViewProj, s_prevViewProj);
    if (s_locCamPos >= 0) SetShaderValue(s_taaShader, s_locCamPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_locViewForward >= 0) SetShaderValue(s_taaShader, s_locViewForward, &viewFwd, SHADER_UNIFORM_VEC3);
    if (s_locTexelSize >= 0) SetShaderValue(s_taaShader, s_locTexelSize, texel, SHADER_UNIFORM_VEC2);
    if (s_locFirstFrame >= 0) SetShaderValue(s_taaShader, s_locFirstFrame, &firstFrame, SHADER_UNIFORM_FLOAT);

    rlDisableColorBlend();
    DrawTexturePro(currHdr,
                   (Rectangle){0.0f, 0.0f, (float)currHdr.width, -(float)currHdr.height},
                   (Rectangle){0.0f, 0.0f, (float)dst.texture.width, (float)dst.texture.height},
                   (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
    EndTextureMode();

    s_historyValid = true;
    s_writeIdx = readIdx;
    return dst.texture;
}

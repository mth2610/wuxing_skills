#include "taa.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

extern bool rlvkSetFramebufferDiscardOnBegin(unsigned int fbId, bool discard) __attribute__((weak));

static RenderTexture2D s_history[2] = {0};
static Shader s_taaShader = {0};
static int s_locHistoryTex = -1;
static int s_locTexelSize = -1;
static int s_locFirstFrame = -1;

static int s_width = 0;
static int s_height = 0;
static int s_writeIdx = 0;
static int s_frameIdx = 0;
static bool s_enabled = true;
static bool s_frameActive = false;
static bool s_historyValid = false;
// Compare against a fixed pose to catch gradual camera drift across frames.
static Camera3D s_cameraAnchor;
static bool s_hasCameraAnchor = false;
static int s_steadyFrames = 0;
enum { TAA_SETTLE_FRAMES = 5 };
static float s_jitterX = 0.0f;
static float s_jitterY = 0.0f;

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
    TAA_InvalidateHistory();
}

static bool SameCameraPose(Camera3D current, Camera3D anchor)
{
    const float epsilonSquared = 0.0001f * 0.0001f;
    return current.projection == anchor.projection &&
           Vector3DistanceSqr(current.position, anchor.position) <= epsilonSquared &&
           Vector3DistanceSqr(current.target, anchor.target) <= epsilonSquared &&
           Vector3DistanceSqr(current.up, anchor.up) <= epsilonSquared &&
           fabsf(current.fovy - anchor.fovy) <= 0.001f;
}

void TAA_Init(int width, int height)
{
    const char *env = getenv("WUXING_TAA");
    if (env != NULL)
        s_enabled = (atoi(env) != 0);

    if (s_taaShader.id == 0)
    {
        s_taaShader = LoadShader(0, "post_process/shaders/taa.fs");
        if (s_taaShader.id == rlGetShaderIdDefault())
            s_taaShader = (Shader){0};
        if (s_taaShader.id != 0)
        {
            s_locHistoryTex = GetShaderLocation(s_taaShader, "u_historyTex");
            s_locTexelSize = GetShaderLocation(s_taaShader, "u_texelSize");
            s_locFirstFrame = GetShaderLocation(s_taaShader, "u_firstFrame");
        }
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
    TAA_InvalidateHistory();
}

void TAA_InvalidateHistory(void)
{
    s_historyValid = false;
    s_hasCameraAnchor = false;
    s_frameActive = false;
    s_steadyFrames = 0;
    s_frameIdx = 0;
    s_jitterX = 0.0f;
    s_jitterY = 0.0f;
}

void TAA_SetEnabled(bool enabled)
{
    const char *env = getenv("WUXING_TAA");
    if (env != NULL)
        enabled = (atoi(env) != 0);
    if (s_enabled != enabled)
    {
        s_enabled = enabled;
        TAA_InvalidateHistory();
    }
}

bool TAA_IsEnabled(void)
{
    return s_frameActive;
}

void TAA_NextFrame(Camera3D camera, int width, int height)
{
    if (!s_enabled || s_taaShader.id == 0 || !s_history[0].texture.id ||
        !s_history[1].texture.id || width != s_width || height != s_height ||
        camera.projection != CAMERA_PERSPECTIVE)
    {
        TAA_InvalidateHistory();
        return;
    }

    if (!s_hasCameraAnchor || !SameCameraPose(camera, s_cameraAnchor))
    {
        TAA_InvalidateHistory();
        s_cameraAnchor = camera;
        s_hasCameraAnchor = true;
        return;
    }

    if (s_steadyFrames < TAA_SETTLE_FRAMES)
    {
        s_steadyFrames++;
        if (s_steadyFrames < TAA_SETTLE_FRAMES)
            return;
    }

    s_frameActive = true;
    int index = s_frameIdx + 1;
    s_jitterX = Halton(index, 2) - 0.5f;
    s_jitterY = Halton(index, 3) - 0.5f;
    s_frameIdx = (s_frameIdx + 1) & 7;
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

Texture2D TAA_Resolve(Texture2D currHdr)
{
    if (!TAA_IsEnabled() || currHdr.id == 0 ||
        currHdr.width != s_width || currHdr.height != s_height)
        return currHdr;

    int writeIdx = s_writeIdx;
    int readIdx = 1 - writeIdx;
    RenderTexture2D dst = s_history[writeIdx];
    Texture2D histTex = s_history[readIdx].texture;

    float texel[2] = {1.0f / (float)currHdr.width, 1.0f / (float)currHdr.height};
    float firstFrame = s_historyValid ? 0.0f : 1.0f;

    BeginTextureMode(dst);
    BeginShaderMode(s_taaShader);
    if (s_locHistoryTex >= 0) SetShaderValueTexture(s_taaShader, s_locHistoryTex, histTex);
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

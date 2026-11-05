#include "maps/toolkit/ground_shadow.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "environment/env_shadow.h"
#include "raylib.h"
#include "raymath.h" // MatrixInvert/MatrixMultiply
#include "rlgl.h"    // rlSetTexture, rlGetMatrixTransform
#include <stddef.h>

static Shader s_shader = {0};
static unsigned int s_cachedShaderId = 0;
static bool s_ready = false;
static int s_locLightVP = -1;
static int s_locShadowEnabled = -1;
static int s_locShadowTexel = -1;

// Cache các giá trị ít thay đổi để tránh tính toán lại mỗi frame
static int s_lastShadowMapWidth = -1;
static float s_cachedTexel = 1.0f / 1024.0f;

static inline void EnsureLoaded(void)
{
    if (s_ready && s_shader.id != 0 && s_cachedShaderId == s_shader.id)
        return;

    s_shader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_shadow.vs",
                                          "maps/toolkit/shaders/ground_shadow.fs");
    if (s_shader.id == 0)
    {
        s_ready = false;
        return;
    }

    s_locLightVP = GetShaderLocation(s_shader, "u_lightVP");
    s_locShadowEnabled = GetShaderLocation(s_shader, "u_shadowEnabled");
    s_locShadowTexel = GetShaderLocation(s_shader, "u_shadowTexel");
    VFXLight_RegisterShader(s_shader);
    s_cachedShaderId = s_shader.id;
    s_ready = true;
}

void GroundShadow_UpdateFrame(void)
{
    EnsureLoaded();
}

void GroundShadow_Begin(void)
{
    EnsureLoaded();
    if (!s_ready)
        return;

    BeginShaderMode(s_shader);

    Texture2D map = EnvShadow_GetShadowMap();
    bool isShadowEnabled = EnvShadow_IsEnabled() && (map.id != 0);
    float enabledVal = isShadowEnabled ? 1.0f : 0.0f;
    if (s_locShadowEnabled >= 0)
        SetShaderValue(s_shader, s_locShadowEnabled, &enabledVal, SHADER_UNIFORM_FLOAT);

    if (isShadowEnabled)
    {
        if (s_locLightVP >= 0)
        {
            Matrix currentTransform = rlGetMatrixTransform();
            Matrix worldFromVert = MatrixInvert(currentTransform);
            Matrix lightVP = EnvShadow_GetLightVP();
            SetShaderValueMatrix(s_shader, s_locLightVP, MatrixMultiply(worldFromVert, lightVP));
        }

        if (map.width != s_lastShadowMapWidth)
        {
            s_lastShadowMapWidth = map.width;
            s_cachedTexel = (map.width > 0) ? (1.0f / (float)map.width) : (1.0f / 1024.0f);
        }

        if (s_locShadowTexel >= 0)
            SetShaderValue(s_shader, s_locShadowTexel, &s_cachedTexel, SHADER_UNIFORM_FLOAT);

        rlSetTexture(map.id);
    }
    else
    {
        rlSetTexture(rlGetTextureIdDefault());
    }
}

void GroundShadow_End(void)
{
    if (!s_ready)
        return;
    rlSetTexture(0);
    EndShaderMode();
}
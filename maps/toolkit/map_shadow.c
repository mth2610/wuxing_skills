#include "maps/toolkit/map_shadow.h"

#include "core/gfx_quality.h"
#include "environment/env_shadow.h"

#include <stddef.h>

void MapShadow_ConfigureShader(Shader shader)
{
    if (shader.id == 0 || shader.locs == NULL)
        return;

    shader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(shader, "shadowMap");
    shader.locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation(shader, "staticShadowMap");
}

void MapShadow_AttachMaterial(Material *material)
{
    if (material == NULL || material->maps == NULL)
        return;

    Texture2D shadowMap = EnvShadow_GetShadowMap();
    if (shadowMap.id != 0)
        material->maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap;
    Texture2D staticShadowMap = EnvShadow_GetStaticShadowMap();
    if (staticShadowMap.id != 0)
        material->maps[MATERIAL_MAP_HEIGHT].texture = staticShadowMap;
}

void MapShadow_UpdateShader(Shader shader)
{
    if (shader.id == 0)
        return;

    float enabled = EnvShadow_IsEnabled() ? 1.0f : 0.0f;
    int enabledLoc = GetShaderLocation(shader, "u_shadowEnabled");
    if (enabledLoc >= 0)
        SetShaderValue(shader, enabledLoc, &enabled, SHADER_UNIFORM_FLOAT);

    float filterQuality = GfxQuality_Get() >= GFX_HIGH ? 2.0f
                        : GfxQuality_Get() >= GFX_MED ? 1.0f : 0.0f;
    int filterQualityLoc = GetShaderLocation(shader, "u_shadowFilterQuality");
    if (filterQualityLoc >= 0)
        SetShaderValue(shader, filterQualityLoc, &filterQuality, SHADER_UNIFORM_FLOAT);

    float thinFeatureBoost = GfxQuality_Get() >= GFX_HIGH ? 0.84f : 0.0f;
    int thinFeatureBoostLoc = GetShaderLocation(shader, "u_shadowThinFeatureBoost");
    if (thinFeatureBoostLoc >= 0)
        SetShaderValue(shader, thinFeatureBoostLoc, &thinFeatureBoost, SHADER_UNIFORM_FLOAT);

    float staticEnabled = EnvShadow_HasStaticCache() ? 1.0f : 0.0f;
    int staticEnabledLoc = GetShaderLocation(shader, "u_staticShadowEnabled");
    if (staticEnabledLoc >= 0)
        SetShaderValue(shader, staticEnabledLoc, &staticEnabled, SHADER_UNIFORM_FLOAT);

    if (enabled < 0.5f)
        return;

    int lightVpLoc = GetShaderLocation(shader, "u_lightVP");
    if (lightVpLoc >= 0)
        SetShaderValueMatrix(shader, lightVpLoc, EnvShadow_GetLightVP());

    Texture2D shadowMap = EnvShadow_GetShadowMap();
    float texel = shadowMap.width > 0 ? 1.0f / (float)shadowMap.width : 1.0f / 1024.0f;
    int texelLoc = GetShaderLocation(shader, "u_shadowTexel");
    if (texelLoc >= 0)
        SetShaderValue(shader, texelLoc, &texel, SHADER_UNIFORM_FLOAT);

    if (staticEnabled < 0.5f)
        return;

    int staticLightVpLoc = GetShaderLocation(shader, "u_staticLightVP");
    if (staticLightVpLoc >= 0)
        SetShaderValueMatrix(shader, staticLightVpLoc, EnvShadow_GetStaticLightVP());
    Texture2D staticMap = EnvShadow_GetStaticShadowMap();
    float staticTexel = staticMap.width > 0
        ? 1.0f / (float)staticMap.width : 1.0f / 512.0f;
    int staticTexelLoc = GetShaderLocation(shader, "u_staticShadowTexel");
    if (staticTexelLoc >= 0)
        SetShaderValue(shader, staticTexelLoc, &staticTexel, SHADER_UNIFORM_FLOAT);
}

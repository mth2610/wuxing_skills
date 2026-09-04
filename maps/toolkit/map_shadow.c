#include "maps/toolkit/map_shadow.h"

#include "environment/env_shadow.h"

#include <stddef.h>

void MapShadow_ConfigureShader(Shader shader)
{
    if (shader.id == 0 || shader.locs == NULL)
        return;

    shader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(shader, "shadowMap");
}

void MapShadow_AttachMaterial(Material *material)
{
    if (material == NULL || material->maps == NULL)
        return;

    Texture2D shadowMap = EnvShadow_GetShadowMap();
    if (shadowMap.id != 0)
        material->maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap;
}

void MapShadow_UpdateShader(Shader shader)
{
    if (shader.id == 0)
        return;

    float enabled = EnvShadow_IsEnabled() ? 1.0f : 0.0f;
    int enabledLoc = GetShaderLocation(shader, "u_shadowEnabled");
    if (enabledLoc >= 0)
        SetShaderValue(shader, enabledLoc, &enabled, SHADER_UNIFORM_FLOAT);

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
}

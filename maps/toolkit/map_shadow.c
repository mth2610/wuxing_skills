#include "maps/toolkit/map_shadow.h"

#include "core/gfx_quality.h"
#include "environment/env_shadow.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdbool.h>
#include <stddef.h>

static void MapShadowSetFloat(Shader shader, const char *name, float value)
{
    int loc = GetShaderLocation(shader, name);
    if (loc >= 0)
        SetShaderValue(shader, loc, &value, SHADER_UNIFORM_FLOAT);
}

static void MapShadowSetMatrix(Shader shader, const char *name, Matrix value)
{
    int loc = GetShaderLocation(shader, name);
    if (loc >= 0)
        SetShaderValueMatrix(shader, loc, value);
}

void MapShadow_ConfigureShader(Shader shader)
{
    if (shader.id == 0 || shader.locs == NULL)
        return;

    shader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(shader, "shadowMap");
    shader.locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation(shader, "staticShadowMap");
}

// Binds the Environment-owned shadow targets into the material's OCCLUSION / HEIGHT slots.
// The textures are BORROWED: never UnloadMaterial()/UnloadTexture() them from the map side.
// Call again whenever the Environment recreates a target (for example after
// EnvShadow_NeedsStaticCapture() turns true); a missing target (id 0) leaves the slot as is,
// and MapShadow_UpdateShader() below reports such a layer as disabled to the shader.
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

// Must run inside the shader's active scope (BeginShaderMode ... EndShaderMode), like every
// other custom-uniform upload in this toolkit.
void MapShadow_UpdateShader(Shader shader)
{
    if (shader.id == 0)
        return;

    // A layer is only advertised to the shader when its target texture really exists.
    // Previously a layer flagged "enabled" with a missing/recreated target made the shader
    // sample an unbound sampler, and fell back to a made-up 1/1024 texel size.
    const Texture2D shadowMap = EnvShadow_GetShadowMap();
    const Texture2D staticMap = EnvShadow_GetStaticShadowMap();
    const bool dynamicEnabled = EnvShadow_IsEnabled() && shadowMap.id != 0;
    const bool staticEnabled = EnvShadow_HasStaticCache() && staticMap.id != 0;

    const int quality = (int)GfxQuality_Get();
    const bool high = quality >= (int)GFX_HIGH;
    const bool med = quality >= (int)GFX_MED;

    MapShadowSetFloat(shader, "u_shadowEnabled", dynamicEnabled ? 1.0f : 0.0f);
    MapShadowSetFloat(shader, "u_shadowFilterQuality", high ? 2.0f : med ? 1.0f : 0.0f);
    MapShadowSetFloat(shader, "u_shadowThinFeatureBoost", high ? 0.18f : 0.0f);
    MapShadowSetFloat(shader, "u_staticShadowEnabled", staticEnabled ? 1.0f : 0.0f);

    if (!dynamicEnabled && !staticEnabled)
        return;

    // This project's custom MyBeginMode3D stores the view transform in rlgl's
    // transform matrix. DrawMesh consequently supplies material shaders with
    // `matModel = model * view`, so their `fragPosition` varying is in that
    // shader/view space despite its historical world-space name. Fold the
    // inverse view into both light matrices, exactly like ground_shadow.c does
    // for immediate geometry. Uploading raw world lightVP here made the depth
    // target valid but sampled it at unrelated coordinates on Vulkan.
    const Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());

    if (dynamicEnabled)
    {
        MapShadowSetMatrix(shader, "u_lightVP",
                           MatrixMultiply(worldFromShaderSpace, EnvShadow_GetLightVP()));
        MapShadowSetFloat(shader, "u_shadowTexel",
                          shadowMap.width > 0 ? 1.0f / (float)shadowMap.width : 1.0f / 1024.0f);
    }

    // The static layer is independent of the dynamic one. It used to be skipped whenever the
    // dynamic layer was off, which left u_staticShadowEnabled = 1 with a stale (or never
    // uploaded, all-zero) u_staticLightVP / u_staticShadowTexel: the folded inverse-view makes
    // that matrix change every frame, so the cached layer drifted or collapsed to one texel.
    if (staticEnabled)
    {
        MapShadowSetMatrix(shader, "u_staticLightVP",
                           MatrixMultiply(worldFromShaderSpace, EnvShadow_GetStaticLightVP()));
        MapShadowSetFloat(shader, "u_staticShadowTexel",
                          staticMap.width > 0 ? 1.0f / (float)staticMap.width : 1.0f / 512.0f);
    }
}

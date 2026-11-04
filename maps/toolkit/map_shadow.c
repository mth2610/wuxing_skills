#include "maps/toolkit/map_shadow.h"

#include "core/gfx_quality.h"
#include "environment/env_shadow.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdbool.h>
#include <stddef.h>

#define MAP_SHADOW_CACHE_CAPACITY 16

typedef struct MapShadowLocCache
{
    unsigned int shaderId;
    int locShadowMap;
    int locStaticShadowMap;
    int locShadowEnabled;
    int locShadowFilterQuality;
    int locShadowThinFeatureBoost;
    int locStaticShadowEnabled;
    int locLightVP;
    int locShadowTexel;
    int locStaticLightVP;
    int locStaticShadowTexel;
} MapShadowLocCache;

static MapShadowLocCache s_shadowLocCache[MAP_SHADOW_CACHE_CAPACITY];
static int s_shadowLocCacheCount = 0;

static const MapShadowLocCache *MapShadowGetOrPopulateCache(Shader shader)
{
    if (shader.id == 0)
        return NULL;

    for (int i = 0; i < s_shadowLocCacheCount; i++)
    {
        if (s_shadowLocCache[i].shaderId == shader.id)
            return &s_shadowLocCache[i];
    }

    int slot = (s_shadowLocCacheCount < MAP_SHADOW_CACHE_CAPACITY)
                   ? s_shadowLocCacheCount++
                   : (int)(shader.id % MAP_SHADOW_CACHE_CAPACITY);

    MapShadowLocCache *entry = &s_shadowLocCache[slot];
    entry->shaderId = shader.id;
    entry->locShadowMap = GetShaderLocation(shader, "shadowMap");
    entry->locStaticShadowMap = GetShaderLocation(shader, "staticShadowMap");
    entry->locShadowEnabled = GetShaderLocation(shader, "u_shadowEnabled");
    entry->locShadowFilterQuality = GetShaderLocation(shader, "u_shadowFilterQuality");
    entry->locShadowThinFeatureBoost = GetShaderLocation(shader, "u_shadowThinFeatureBoost");
    entry->locStaticShadowEnabled = GetShaderLocation(shader, "u_staticShadowEnabled");
    entry->locLightVP = GetShaderLocation(shader, "u_lightVP");
    entry->locShadowTexel = GetShaderLocation(shader, "u_shadowTexel");
    entry->locStaticLightVP = GetShaderLocation(shader, "u_staticLightVP");
    entry->locStaticShadowTexel = GetShaderLocation(shader, "u_staticShadowTexel");
    return entry;
}

static inline void MapShadowSetFloatLoc(Shader shader, int loc, float value)
{
    if (loc >= 0)
        SetShaderValue(shader, loc, &value, SHADER_UNIFORM_FLOAT);
}

static inline void MapShadowSetMatrixLoc(Shader shader, int loc, Matrix value)
{
    if (loc >= 0)
        SetShaderValueMatrix(shader, loc, value);
}

void MapShadow_ConfigureShader(Shader shader)
{
    if (shader.id == 0 || shader.locs == NULL)
        return;

    const MapShadowLocCache *cache = MapShadowGetOrPopulateCache(shader);
    if (cache != NULL)
    {
        shader.locs[SHADER_LOC_MAP_OCCLUSION] = cache->locShadowMap;
        shader.locs[SHADER_LOC_MAP_HEIGHT] = cache->locStaticShadowMap;
    }
}

// Binds the Environment-owned shadow targets into the material's OCCLUSION / HEIGHT slots.
// The textures are BORROWED: never UnloadMaterial()/UnloadTexture() them from the map side.
// Call again whenever the Environment recreates a target (for example after
// EnvShadow_NeedsStaticCapture() turns true); a missing target (id 0) falls back to rlgl's
// default 1x1 texture so Vulkan descriptors never sample an unbound slot.
void MapShadow_AttachMaterial(Material *material)
{
    if (material == NULL || material->maps == NULL)
        return;

    Texture2D fallbackTex = {
        .id = rlGetTextureIdDefault(),
        .width = 1,
        .height = 1,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

    Texture2D shadowMap = EnvShadow_GetShadowMap();
    if (shadowMap.id != 0)
        material->maps[MATERIAL_MAP_OCCLUSION].texture = shadowMap;
    else if (material->maps[MATERIAL_MAP_OCCLUSION].texture.id == 0)
        material->maps[MATERIAL_MAP_OCCLUSION].texture = fallbackTex;

    Texture2D staticShadowMap = EnvShadow_GetStaticShadowMap();
    if (staticShadowMap.id != 0)
        material->maps[MATERIAL_MAP_HEIGHT].texture = staticShadowMap;
    else if (material->maps[MATERIAL_MAP_HEIGHT].texture.id == 0)
        material->maps[MATERIAL_MAP_HEIGHT].texture = fallbackTex;
}

// Must run inside the shader's active scope (BeginShaderMode ... EndShaderMode), like every
// other custom-uniform upload in this toolkit.
void MapShadow_UpdateShader(Shader shader)
{
    if (shader.id == 0)
        return;

    const MapShadowLocCache *cache = MapShadowGetOrPopulateCache(shader);
    if (cache == NULL)
        return;

    // A layer is only advertised to the shader when its target texture really exists.
    const Texture2D shadowMap = EnvShadow_GetShadowMap();
    const Texture2D staticMap = EnvShadow_GetStaticShadowMap();
    const bool dynamicEnabled = EnvShadow_IsEnabled() && shadowMap.id != 0;
    const bool staticEnabled = EnvShadow_HasStaticCache() && staticMap.id != 0;

    const int quality = (int)GfxQuality_Get();
    const bool high = quality >= (int)GFX_HIGH;
    const bool med = quality >= (int)GFX_MED;

    MapShadowSetFloatLoc(shader, cache->locShadowEnabled, dynamicEnabled ? 1.0f : 0.0f);
    MapShadowSetFloatLoc(shader, cache->locShadowFilterQuality, high ? 2.0f : med ? 1.0f
                                                                                  : 0.0f);
    MapShadowSetFloatLoc(shader, cache->locShadowThinFeatureBoost, high ? 0.18f : med ? 0.10f
                                                                                      : 0.0f);
    MapShadowSetFloatLoc(shader, cache->locStaticShadowEnabled, staticEnabled ? 1.0f : 0.0f);

    if (!dynamicEnabled && !staticEnabled)
        return;

    // This project's custom MyBeginMode3D stores the view transform in rlgl's
    // transform matrix. DrawMesh consequently supplies material shaders with
    // `matModel = model * view`, so their `fragPosition` varying is in that
    // shader/view space despite its historical world-space name. Fold the
    // inverse view into both light matrices, exactly like ground_shadow.c does
    // for immediate geometry.
    const Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());

    if (dynamicEnabled)
    {
        MapShadowSetMatrixLoc(shader, cache->locLightVP,
                              MatrixMultiply(worldFromShaderSpace, EnvShadow_GetLightVP()));
        MapShadowSetFloatLoc(shader, cache->locShadowTexel,
                             shadowMap.width > 0 ? 1.0f / (float)shadowMap.width : 1.0f / 1024.0f);
    }

    if (staticEnabled)
    {
        MapShadowSetMatrixLoc(shader, cache->locStaticLightVP,
                              MatrixMultiply(worldFromShaderSpace, EnvShadow_GetStaticLightVP()));
        MapShadowSetFloatLoc(shader, cache->locStaticShadowTexel,
                             staticMap.width > 0 ? 1.0f / (float)staticMap.width : 1.0f / 512.0f);
    }
}
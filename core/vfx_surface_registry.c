#include "core/vfx_surface_registry.h"

#include <stddef.h>

#include "core/resource_manager.h"

static VFX_SurfaceProfile s_profiles[VFX_SURFACE_COUNT] = {
#include "core/vfx_surface_registry.generated.inl"
};
static bool s_loaded[VFX_SURFACE_COUNT];

static Texture2D VFX_SurfaceRegistry_LoadTexture(const char *path, VFX_SurfaceWrap wrap,
                                                  VFX_SurfaceFilter filter)
{
    Texture2D texture = {0};
    if (path == NULL || path[0] == '\0')
        return texture;

    texture = ResourceManager_LoadTexture(path);
    if (texture.id != 0)
    {
        SetTextureFilter(texture, filter == VFX_SURFACE_FILTER_POINT ?
                         TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, wrap == VFX_SURFACE_WRAP_REPEAT ?
                       TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
    }
    return texture;
}

const VFX_SurfaceProfile *VFX_SurfaceRegistry_Get(VFX_SurfaceId id)
{
    if (id < 0 || id >= VFX_SURFACE_COUNT)
        return NULL;

    VFX_SurfaceProfile *profile = &s_profiles[id];
    if (!s_loaded[id])
    {
        profile->body = VFX_SurfaceRegistry_LoadTexture(profile->bodyPath, profile->wrap,
                                                         profile->filter);
        profile->flowMap = VFX_SurfaceRegistry_LoadTexture(profile->flowPath, profile->wrap,
                                                            profile->filter);
        profile->mask = VFX_SurfaceRegistry_LoadTexture(profile->maskPath, profile->wrap,
                                                         profile->filter);
        profile->gradient = VFX_SurfaceRegistry_LoadTexture(profile->gradientPath, profile->wrap,
                                                             profile->filter);
        profile->fallbackBody = VFX_SurfaceRegistry_LoadTexture(profile->fallbackBodyPath,
                                                                 profile->wrap, profile->filter);
        s_loaded[id] = true;
    }
    return profile;
}

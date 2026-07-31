#ifndef VFX_SURFACE_REGISTRY_H
#define VFX_SURFACE_REGISTRY_H

// Semantic VFX surface catalog.  A composition selects a surface role; this
// registry owns the asset paths, sampling contract and runtime texture load.
// Texture memory remains owned by ResourceManager.

#include "raylib.h"

typedef enum {
    VFX_SURFACE_PRIMITIVE_RIBBON = 0,
    VFX_SURFACE_PRIMITIVE_TUBE,
    VFX_SURFACE_PRIMITIVE_PUFF,
    VFX_SURFACE_PRIMITIVE_FIRE_TONGUE,
    VFX_SURFACE_PRIMITIVE_DECAL
} VFX_SurfacePrimitive;

typedef enum {
    VFX_SURFACE_WRAP_CLAMP = 0,
    VFX_SURFACE_WRAP_REPEAT
} VFX_SurfaceWrap;

typedef enum {
    VFX_SURFACE_FILTER_BILINEAR = 0,
    VFX_SURFACE_FILTER_POINT
} VFX_SurfaceFilter;

typedef enum {
    VFX_SURFACE_BLEND_CONSUMER_DEFINED = 0,
    VFX_SURFACE_BLEND_ALPHA,
    VFX_SURFACE_BLEND_ADDITIVE,
    VFX_SURFACE_BLEND_MULTIPLIED
} VFX_SurfaceBlend;

typedef enum {
    VFX_SURFACE_ROLE_TRAIL = 0,
    VFX_SURFACE_ROLE_RESIDUE,
    VFX_SURFACE_ROLE_SCORCH,
    VFX_SURFACE_ROLE_IMPACT,
    VFX_SURFACE_ROLE_RUNE
} VFX_SurfaceRole;

typedef enum {
    VFX_SURFACE_SMOKE_RIBBON = 0,
    VFX_SURFACE_ENERGY_RIBBON,
    VFX_SURFACE_ENERGY_TUBE,
    // Preview-only until P3 visual approval: kept here so VolumeTrail never
    // owns a texture path even while those kinds stay blocked from shipping.
    VFX_SURFACE_SMOKE_TUBE,
    VFX_SURFACE_FIRE_TUBE,
    VFX_SURFACE_SMOKE_PUFF,
    VFX_SURFACE_FIRE_TONGUE,
    // P4 contracts contain no runtime asset until visual-owner approval.
    VFX_SURFACE_DECAL_RESIDUE,
    VFX_SURFACE_DECAL_SCORCH,
    VFX_SURFACE_COUNT
} VFX_SurfaceId;

typedef struct {
    VFX_SurfaceId id;
    VFX_SurfacePrimitive primitive;
    VFX_SurfaceRole role;
    VFX_SurfaceWrap wrap;
    VFX_SurfaceFilter filter;
    VFX_SurfaceBlend blend;
    const char *name;
    const char *bodyPath;
    const char *flowPath;
    const char *maskPath;
    const char *gradientPath;
    const char *fallbackBodyPath;
    const char *bodyChannels;
    const char *flowChannels;
    const char *maskChannels;
    const char *gradientChannels;
    const char *seam;
    const char *projection;
    const char *provenance;
    const char *approval;
    float lifetimeSeconds;
    float fadeInSeconds;
    float fadeOutSeconds;
    int maxDrawCalls;
    int maxTextures;
    int flipbookColumns;
    int flipbookRows;
    int flipbookFrames;
    Texture2D body;
    Texture2D flowMap;
    Texture2D mask;
    Texture2D gradient;
    Texture2D fallbackBody;
} VFX_SurfaceProfile;

// Loads the profile's semantic assets through ResourceManager on first use and
// returns a stable static profile. NULL is returned for an invalid id.
const VFX_SurfaceProfile *VFX_SurfaceRegistry_Get(VFX_SurfaceId id);

#endif // VFX_SURFACE_REGISTRY_H

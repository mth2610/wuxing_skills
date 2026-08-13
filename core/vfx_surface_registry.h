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
    VFX_SURFACE_PRIMITIVE_DECAL,
    // Flat polar UV surface.  Unlike a decal this follows no terrain.
    VFX_SURFACE_PRIMITIVE_DISC
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
    // Same primitive and channel contract as ENERGY_RIBBON, different
    // authoring: many thin faint hairs instead of few strong ones. The two
    // strand-trail styles are not interchangeable on one sheet.
    VFX_SURFACE_SMOKE_STRAND,
    VFX_SURFACE_ENERGY_TUBE,
    // Preview-only until P3 visual approval: kept here so VolumeTrail never
    // owns a texture path even while those kinds stay blocked from shipping.
    VFX_SURFACE_SMOKE_TUBE,
    VFX_SURFACE_FIRE_TUBE,
    VFX_SURFACE_SMOKE_PUFF,
    VFX_SURFACE_FIRE_TONGUE,
    // Directionless split flipbook for the legacy multi-sprite flame path.
    // Keep it distinct from FIRE_TONGUE: a tongue owns an authored +Z axis.
    VFX_SURFACE_FIRE_PUFF,
    // Same simulation as FIRE_PUFF, never split: the packed VOLUME layout
    // (R emission / G density / B self-shadow / A opacity, no colour anywhere).
    // Decoded by particle_lit.fs's volume branch, coloured by a ramp LUT.
    VFX_SURFACE_FIRE_VOLUME,
    // P4 contracts contain no runtime asset until visual-owner approval.
    VFX_SURFACE_DECAL_RESIDUE,
    VFX_SURFACE_DECAL_SCORCH,
    VFX_SURFACE_DECAL_FROST,
    VFX_SURFACE_DECAL_IMPACT,
    VFX_SURFACE_DECAL_RUNE,
    VFX_SURFACE_VOLUME_SMOKE,
    VFX_SURFACE_VOLUME_FIRE,
    VFX_SURFACE_VOLUME_STEAM,
    VFX_SURFACE_VOLUME_NOISE,
    // One stretched smoke strip mapped through polar UVs by the free-space
    // impact-shockwave composition; it is not a ground decal.
    VFX_SURFACE_IMPACT_SMOKE,
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
    // Channel declarations. These are NOT free prose: each must match the
    // grammar in assets/TEXTURE_PACKING.md §2 —
    //   "<LAYOUT> | R:<slot>/<mode> | G:... | B:... | A:...  — <prose>"
    // and scripts/validate_vfx_surface_registry.py rejects anything else at
    // CMake configure time. Read that spec before adding a sheet; the layout
    // decides which channel may carry what, and R1 (a channel cannot be both a
    // stretched SHAPE and a seamless MATERIAL) is the one that has already
    // cost debugging time.
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
    float maxSlopeDegrees;
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

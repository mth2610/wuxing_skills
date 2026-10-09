#ifndef VOLUMETRIC_FOG_QUALITY_H
#define VOLUMETRIC_FOG_QUALITY_H

#include "core/gfx_quality.h"
#include "core/volumetric/volumetric_fog.h"

// Resolve for this frame only; the user's requested mode survives tier changes.
static inline FogRenderMode VolumetricFog_ResolveMode(FogRenderMode requested, GfxQuality tier) {
    return requested == FOG_MODE_VOLUMETRIC && tier <= GFX_LOW
        ? FOG_MODE_HEIGHT : requested;
}

#endif

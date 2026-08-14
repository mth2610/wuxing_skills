#ifndef CORE_VFX_APPEARANCE_H
#define CORE_VFX_APPEARANCE_H

#include <stdbool.h>

#include "core/vfx_contrast.h"

/* Renderer-neutral visual intent shared by particles, trails, decals and
 * future geometry providers. Geometry owns vertices and lifetime; this
 * contract owns how the result interacts with the scene colour pipeline. */
typedef enum {
    VFX_APPEARANCE_INHERIT = 0, /* Exact legacy state; safe for zero-init. */
    VFX_APPEARANCE_NORMAL,
    VFX_APPEARANCE_SMOKE,
    VFX_APPEARANCE_DUST,
    VFX_APPEARANCE_GLOW,
    VFX_APPEARANCE_FIRE,
    VFX_APPEARANCE_MAGIC,
    VFX_APPEARANCE_COUNT
} VFXAppearanceId;

/* Values deliberately match VFX_BlendMode. This lets particle instances copy
 * the resolved surface without a branch or a second translation table. */
typedef enum {
    VFX_SURFACE_ALPHA = 0,
    VFX_SURFACE_ADDITIVE,
    VFX_SURFACE_PREMULTIPLIED
} VFXSurfaceMode;

typedef struct {
    VFXSurfaceMode surface;
    VFXContrastProfileId contrast;
    float bodyOpacity;
    float emissionIntensity;
    float emissionThreshold;
    bool unlit;
} VFXResolvedAppearance;

/* INHERIT returns `legacy` byte-for-field. Named appearances replace visual
 * policy centrally, so geometry renderers do not each reinvent bloom/blend. */
VFXResolvedAppearance VFXAppearance_Resolve(VFXAppearanceId id,
                                            VFXResolvedAppearance legacy);
bool VFXResolvedAppearance_UsesBody(VFXResolvedAppearance appearance);
bool VFXResolvedAppearance_UsesEmission(VFXResolvedAppearance appearance);

#endif

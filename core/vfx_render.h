#ifndef CORE_VFX_RENDER_H
#define CORE_VFX_RENDER_H

#include <stdbool.h>

#include "core/vfx_appearance.h"

/* One semantic vocabulary for every VFX producer.  A pass says what the draw
 * contributes; the surface says how it combines with the HDR scene. */
typedef enum {
    VFX_RENDER_PASS_BODY = 0,
    VFX_RENDER_PASS_EMISSION
} VFXRenderPass;

typedef struct {
    bool active;
    bool depthWrite;
    VFXRenderPass pass;
    VFXSurfaceMode surface;
} VFXRenderScope;

/* Manager/batcher entry points.  These select the authoritative HDR scene
 * target only; the manager retains blend ownership for its grouped draws. */
void VFXRender_BeginPass(VFXRenderPass pass);
void VFXRender_EndPass(void);

/* Direct-geometry entry points.  These atomically own scene target, blend and
 * depth-write state, with mandatory rlgl flushes on both sides. */
VFXRenderScope VFXRender_BeginDraw(VFXRenderPass pass,
                                   VFXSurfaceMode surface,
                                   bool depthWrite);
VFXRenderScope VFXRender_BeginAppearance(VFXRenderPass pass,
                                         VFXAppearanceId appearanceId,
                                         VFXResolvedAppearance legacy,
                                         bool depthWrite,
                                         VFXResolvedAppearance *outResolved);
void VFXRender_EndDraw(VFXRenderScope *scope);

bool VFXRender_AppearanceDrawsPass(VFXResolvedAppearance appearance,
                                   VFXRenderPass pass);

/* The `#define` block a producer's fragment shader must be COMPILED with so
 * that its VFX_ResolveOutput() resolves the way `surface` blends. Pass it to
 * ResourceManager_LoadShaderVariant; the result is cached per define set, so
 * asking repeatedly is free after the first compile.
 *
 * This exists for producers whose consumer chooses a blend at runtime (decals
 * per profile, trails per appearance, ref bands per call). A producer with ONE
 * fixed blend does not need it — it calls the matching VFX_Resolve* directly.
 *
 * Never build this string by hand at a call site: the whole value of the
 * mechanism is that one function maps surface to define, so the two cannot
 * drift apart. */
const char *VFXRender_OutputDefines(VFXSurfaceMode surface);
VFXContrastLayer VFXRender_ContrastLayer(VFXRenderPass pass);

#endif

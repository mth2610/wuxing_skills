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
VFXContrastLayer VFXRender_ContrastLayer(VFXRenderPass pass);

#endif

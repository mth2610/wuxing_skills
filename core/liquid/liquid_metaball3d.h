#ifndef CORE_LIQUID_METABALL3D_H
#define CORE_LIQUID_METABALL3D_H

#include "raylib.h"
#include "core/liquid/liquid_surface.h"
#include <stdbool.h>

#define LIQUID_METABALL3D_MAX_BLOBS 64

/* Independent raymarched bodies per frame.
 *
 * Each body owns its own blob list, optics and isosurface threshold, and is
 * drawn as its own bounding box. Before this existed the module kept ONE global
 * blob array and ONE material, so two simultaneous liquids merged into a single
 * isosurface (their kernels summed wherever they were near each other) and both
 * rendered with whichever material was bound last. A 5-body bench needs 5. */
#define LIQUID_METABALL3D_MAX_BODIES 6

typedef struct {
    Vector3 position;
    float radius;
} LiquidBlob3D;

/* Lifecycle */
void LiquidMetaball3D_Init(void);
void LiquidMetaball3D_Unload(void);

/* Reset all bodies and blobs for the current frame */
void LiquidMetaball3D_Reset(void);

/* Start a new independent body and make it the target of RegisterBlob.
 * `material`/`threshold` may be NULL/0 to keep the legacy fallbacks. Returns
 * the body index (for diagnostics). Overflow past MAX_BODIES appends to the
 * last body rather than dropping the caller's geometry. */
int LiquidMetaball3D_BeginBody(const LiquidDesc *material, float threshold);

/* Close the current body. Optional: the next BeginBody starts a new one. */
void LiquidMetaball3D_EndBody(void);

/* Register a single blob on the current body */
void LiquidMetaball3D_RegisterBlob(Vector3 position, float radius);

/* Register an array of blobs on the current body */
void LiquidMetaball3D_RegisterBlobs(const Vector3 *positions, const float *radii, int count);

/* Check if any blobs registered */
bool LiquidMetaball3D_HasBlobs(void);

/* Number of currently registered blobs across all bodies */
int LiquidMetaball3D_GetBlobCount(void);

/* Legacy global fallbacks, used by bodies begun with NULL material / 0 threshold */
void LiquidMetaball3D_SetMaterial(const LiquidDesc *material);
void LiquidMetaball3D_SetThreshold(float threshold);

/* Draw every registered body with analytical raymarching. `material` and
 * `threshold` are the fallbacks for bodies that did not supply their own. */
void LiquidMetaball3D_Draw(Camera3D camera, const LiquidDesc *material, float threshold);

#endif // CORE_LIQUID_METABALL3D_H

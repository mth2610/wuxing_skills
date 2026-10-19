#ifndef CORE_LIQUID_METABALL3D_H
#define CORE_LIQUID_METABALL3D_H

#include "raylib.h"
#include "core/liquid/liquid_surface.h"
#include <stdbool.h>

#define LIQUID_METABALL3D_MAX_BLOBS 64

typedef struct {
    Vector3 position;
    float radius;
} LiquidBlob3D;

/* Lifecycle */
void LiquidMetaball3D_Init(void);
void LiquidMetaball3D_Unload(void);

/* Reset registered blobs for the current frame */
void LiquidMetaball3D_Reset(void);

/* Register a single blob */
void LiquidMetaball3D_RegisterBlob(Vector3 position, float radius);

/* Register an array of blobs */
void LiquidMetaball3D_RegisterBlobs(const Vector3 *positions, const float *radii, int count);

/* Check if any blobs registered */
bool LiquidMetaball3D_HasBlobs(void);

/* Number of currently registered blobs */
int LiquidMetaball3D_GetBlobCount(void);

/* Set optical material for this frame's metaball draw */
void LiquidMetaball3D_SetMaterial(const LiquidDesc *material);

/* Set isosurface threshold for this frame (default 0.50f) */
void LiquidMetaball3D_SetThreshold(float threshold);

/* Draw all registered 3D metaballs with analytical raymarching.
 * Pass NULL material or 0 threshold to use the values set via SetMaterial/SetThreshold. */
void LiquidMetaball3D_Draw(Camera3D camera, const LiquidDesc *material, float threshold);

#endif // CORE_LIQUID_METABALL3D_H

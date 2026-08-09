#ifndef CORE_VFX_LAYERED_FIELD_H
#define CORE_VFX_LAYERED_FIELD_H

#include "raylib.h"

/* One coherent annular field resolved as semantic layers. The field is the
 * whole effect, not one particle, so its UV describes authored composition
 * space. Body and emission use independent masks from the same domain:
 * emission is never inferred from body alpha or from a billboard centre.
 *
 * lobeCenters are turns in [0,1); lobeWidths are half-widths in turns.
 * debugLayer: 0=normal, 1=mass, 2=structure, 3=edge, 4=accent, 5=emission. */
typedef struct VFXLayeredAnnulusParams {
    Vector3 center;
    Vector3 normal;
    float halfSize;
    float innerRadius;
    float outerRadius;
    float time;
    float phase;
    float opacity;
    float emissionStrength;
    Color bodyColor;
    Color edgeColor;
    Color accentColor;
    Color emissionColor;
    Vector4 lobeCenters;
    Vector4 lobeWidths;
    int lobeCount;
    int debugLayer;
} VFXLayeredAnnulusParams;

void VFXLayeredAnnulus_DrawBody(const VFXLayeredAnnulusParams *params);
void VFXLayeredAnnulus_DrawEmission(const VFXLayeredAnnulusParams *params);

#endif

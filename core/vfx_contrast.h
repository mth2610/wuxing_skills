#ifndef CORE_VFX_CONTRAST_H
#define CORE_VFX_CONTRAST_H

/* Shared visual hierarchy for every VFX renderer.  Geometry systems own
 * shape/motion; this module owns the common Mass -> Structure -> Accent
 * policy.  NONE is intentionally identity so legacy authored effects do not
 * change until they select a profile. */
#include "raylib.h"

typedef enum {
    VFX_CONTRAST_NONE = 0,
    VFX_CONTRAST_SMOKE,
    VFX_CONTRAST_FIRE,
    VFX_CONTRAST_ENERGY,
    VFX_CONTRAST_MAGIC,
    VFX_CONTRAST_DUST,
    VFX_CONTRAST_COUNT
} VFXContrastProfileId;

typedef enum {
    VFX_CONTRAST_BODY = 0,
    VFX_CONTRAST_EMISSION
} VFXContrastLayer;

typedef struct {
    float alpha;
    float density;
    float bodyIntensity;
    float emissionIntensity;
    float emissionThreshold;
    float edgeContrast;
    float edgeSharpness;
    float coreIntensity;
    float coreSize;
    float noiseStrength;
    float bloomIntensity;
} VFXContrastProfile;

const VFXContrastProfile *VFXContrast_Get(VFXContrastProfileId id);
Color VFXContrast_ApplyBodyColor(Color color, const VFXContrastProfile *profile);
Color VFXContrast_ApplyAccentColor(Color color, const VFXContrastProfile *profile);
unsigned char VFXContrast_ScaleAlpha(unsigned char alpha, float multiplier);

/* Renderer-facing API. NONE is an exact identity. BODY makes an occluding,
 * darker mass; EMISSION preserves a compact bright accent. Authored alpha and
 * intensity remain meaningful inputs rather than being replaced by presets. */
Color VFXContrast_ApplyColor(Color color, VFXContrastProfileId id,
                             VFXContrastLayer layer);
float VFXContrast_ApplyBodyOpacity(float authoredOpacity,
                                   VFXContrastProfileId id);
float VFXContrast_ApplyEmissionIntensity(float authoredIntensity,
                                         VFXContrastProfileId id);
float VFXContrast_ApplyEmissionThreshold(float authoredThreshold,
                                         VFXContrastProfileId id);
void VFXContrast_GetShaderParams(VFXContrastProfileId id, float outParams[4]);

#endif

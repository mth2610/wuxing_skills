#include "core/vfx_contrast.h"
#include <stddef.h>

static const VFXContrastProfile s_profiles[VFX_CONTRAST_COUNT] = {
    [VFX_CONTRAST_NONE]   = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f },
    [VFX_CONTRAST_SMOKE]  = { 1.15f, 0.82f, 0.72f, 0.0f,  1.0f, 0.80f, 0.75f, 0.0f,  0.0f,  0.35f, 0.0f },
    [VFX_CONTRAST_FIRE]   = { 1.10f, 0.90f, 0.80f, 1.50f, 0.78f, 1.40f, 1.35f, 1.20f, 0.12f, 0.25f, 0.12f },
    [VFX_CONTRAST_ENERGY] = { 1.35f, 0.92f, 0.62f, 1.55f, 0.76f, 1.65f, 1.50f, 1.25f, 0.22f, 0.12f, 0.18f },
    [VFX_CONTRAST_MAGIC]  = { 1.25f, 0.90f, 0.70f, 1.35f, 0.80f, 1.25f, 1.10f, 1.15f, 0.30f, 0.20f, 0.10f },
    [VFX_CONTRAST_DUST]   = { 1.10f, 0.80f, 0.65f, 0.0f,  1.0f, 0.75f, 0.65f, 0.0f,  0.0f,  0.30f, 0.0f }
};

const VFXContrastProfile *VFXContrast_Get(VFXContrastProfileId id)
{
    if (id < VFX_CONTRAST_NONE || id >= VFX_CONTRAST_COUNT)
        id = VFX_CONTRAST_NONE;
    return &s_profiles[id];
}

static unsigned char ScaleChannel(unsigned char value, float multiplier)
{
    float scaled = (float)value * multiplier;
    if (scaled <= 0.0f) return 0;
    if (scaled >= 255.0f) return 255;
    return (unsigned char)(scaled + 0.5f);
}

Color VFXContrast_ApplyBodyColor(Color color, const VFXContrastProfile *profile)
{
    if (profile == NULL) return color;
    color.r = ScaleChannel(color.r, profile->bodyIntensity * profile->density);
    color.g = ScaleChannel(color.g, profile->bodyIntensity * profile->density);
    color.b = ScaleChannel(color.b, profile->bodyIntensity * profile->density);
    color.a = VFXContrast_ScaleAlpha(color.a, profile->alpha);
    return color;
}

Color VFXContrast_ApplyAccentColor(Color color, const VFXContrastProfile *profile)
{
    if (profile == NULL) return color;
    color.r = ScaleChannel(color.r, profile->coreIntensity);
    color.g = ScaleChannel(color.g, profile->coreIntensity);
    color.b = ScaleChannel(color.b, profile->coreIntensity);
    color.a = VFXContrast_ScaleAlpha(color.a, profile->alpha);
    return color;
}

unsigned char VFXContrast_ScaleAlpha(unsigned char alpha, float multiplier)
{
    return ScaleChannel(alpha, multiplier);
}

Color VFXContrast_ApplyColor(Color color, VFXContrastProfileId id,
                             VFXContrastLayer layer)
{
    const VFXContrastProfile *profile = VFXContrast_Get(id);
    return layer == VFX_CONTRAST_EMISSION
               ? VFXContrast_ApplyAccentColor(color, profile)
               : VFXContrast_ApplyBodyColor(color, profile);
}

float VFXContrast_ApplyBodyOpacity(float authoredOpacity,
                                   VFXContrastProfileId id)
{
    float opacity = authoredOpacity * VFXContrast_Get(id)->alpha;
    if (opacity < 0.0f) return 0.0f;
    if (opacity > 1.0f) return 1.0f;
    return opacity;
}

float VFXContrast_ApplyEmissionIntensity(float authoredIntensity,
                                         VFXContrastProfileId id)
{
    float intensity = authoredIntensity * VFXContrast_Get(id)->emissionIntensity;
    return intensity > 0.0f ? intensity : 0.0f;
}

float VFXContrast_ApplyEmissionThreshold(float authoredThreshold,
                                         VFXContrastProfileId id)
{
    const VFXContrastProfile *profile = VFXContrast_Get(id);
    if (profile == &s_profiles[VFX_CONTRAST_NONE]) return authoredThreshold;
    return authoredThreshold < profile->emissionThreshold
               ? authoredThreshold
               : profile->emissionThreshold;
}
